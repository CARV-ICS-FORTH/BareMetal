/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2024-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2024-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>	/* For typed ints */
#include <stddef.h>	/* For size_t / NULL */
#include <string.h>	/* For memset() */
#include <errno.h>	/* For errno and ENOMEM */
#include <platform/utils/lock.h>	/* For lock_acquire/release() */
#include <platform/utils/utils.h>	/* For console output */
#include <stdlib.h>

/***********************\
* RAND() Implementation *
\***********************/

/* xorshift64 - lightweight PRNG, Code is in the public domain.*/

/* This nothing-up-my-sleeve number comes from RFC3526 (pi in hex) */
#define DEFAULT_SEED 0xC90FDAA22168C234ULL

static uint64_t xorshift64_state = DEFAULT_SEED;

static inline uint64_t
xorshift64_next(void)
{
	uint64_t x = xorshift64_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	xorshift64_state = x;
	return x;
}

void
srand(unsigned int seed)
{
	xorshift64_state = seed ^ DEFAULT_SEED;
	if (!xorshift64_state)
		xorshift64_state = DEFAULT_SEED;
}

int
rand(void)
{
	/* Return positive int in range [0, RAND_MAX] */
	return (int)(xorshift64_next() % RAND_MAX);;
}


/**************************\
* Runtime memory allocator *
\**************************/

/*
 * This is a LIFO/stack allocator where we can only realloc/free the top allocation.
 * Its heap starts from __stack_start and grows forwards, so the first allocation is
 * guaranteed by the linker script to be 64b aligned. Since malloc is expected to always
 * return pointer-size aligned addresses, and since the start of the heap is already
 * guaranteed to be aligned, we only need to align size up to the next uintptr_t. We
 * add an extra uintptr_t at the end of each allocation that holds metadata for the
 * allocation, allowing ust to free in the reverse order that we allocated, and also
 * catch memory corruption cases.
 */

 /* External values from the linker, through boot.S (.srodata.ldvars section) */
extern const uintptr_t __stack_start;
extern const uintptr_t __ram_end;

/* Allocator's state */
static atomic_int alloc_lock = 0;
static uintptr_t heap_end = 0;
static uintptr_t last_alloc_end = 0;
static size_t total_alloc_size = 0;
static unsigned int alloc_count = 0;

/* This may be used by other allocators (e.g. for pages/page tables) to use
 * the top part of the heap and leave the rest for this one. */
uintptr_t
__adjust_heap_end(uintptr_t new_heap_end)
{
	lock_acquire(&alloc_lock);
	if ((new_heap_end <= __ram_end) && (new_heap_end > last_alloc_end))
		heap_end = new_heap_end;
	uintptr_t result = heap_end;
	lock_release(&alloc_lock);
	return result;
}

/* Some helper macros */
#define ALLOC_ALIGN	__SIZEOF_POINTER__
#define ALIGN_UP(x)	(((x) + (ALLOC_ALIGN) - 1) & ~((ALLOC_ALIGN) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))
/* Since we are always aligned the size we'll store in the metadata
 * will always have some of its lower bits set to zero. We use those
 * bits to store a part of alloc_count, so that we can use metadata
 * to also verify the allocator's consistency. */
#define METADATA_AC_MASK	(ALLOC_ALIGN - 1)

void*
realloc(void *ptr, size_t size)
{
	/* Do we even have anough heap for the allocator ? */
	if (__stack_start + __SIZEOF_POINTER__ >= __ram_end)
		return NULL;

	void *result = NULL;
	/* This is used so that we don't try to increase
	 * the alloc_count on realloc. */
	int new_alloc = 0;
	uintptr_t saved_metadata_val = 0;

	lock_acquire(&alloc_lock);

	if (!last_alloc_end)
		last_alloc_end = __stack_start;
	if (!heap_end)
		heap_end = __ram_end;

	/* ptr == NULL -> New allocation */
	if (ptr == NULL) {
		if (!size)
			goto done;
		ptr = (void*)last_alloc_end;
		new_alloc = 1;
 alloc:
		/* Align size up to pointer size, and add an extra uintptr_t
		 * for metadata. */
		const size_t aligned_size = ALIGN_UP(size);
		const uintptr_t start_ptr = (uintptr_t)ptr;
		const uintptr_t end_ptr = start_ptr + aligned_size + (__SIZEOF_POINTER__);

		/* Check if we have enough space, and if so do the (re)allocation */
		if (end_ptr > heap_end)
			goto done;
		last_alloc_end = end_ptr;
		alloc_count += new_alloc;

		/* Populate metadata, if this is a resize and not a new allocation
		 * preserve the existing metadata instead. */
		uintptr_t *metadata_ptr = (uintptr_t*)(start_ptr + aligned_size);
		if (new_alloc)
			*metadata_ptr = (total_alloc_size | (alloc_count & METADATA_AC_MASK));
		else
			*metadata_ptr = saved_metadata_val;
		total_alloc_size = (end_ptr - __stack_start);
		result = (void*)start_ptr;
		DBG("(re)allocation at 0x%lx, total_alloc_size: %z, alloc_count: %u\n",
		    start_ptr, total_alloc_size, alloc_count);
		goto done;
	}

	/* ptr != NULL -> Free/Resize last allocation */
	if (!alloc_count)
		goto err;

	/* Verify metadata consistency and make sure ptr points to the start of last allocation */
	const uintptr_t *last_metadata_ptr = (uintptr_t*)(last_alloc_end - (__SIZEOF_POINTER__));
	if ((*last_metadata_ptr & METADATA_AC_MASK) != (alloc_count & METADATA_AC_MASK))
		goto err;
	const size_t last_metadata_size = *last_metadata_ptr & (~METADATA_AC_MASK);
	const uintptr_t start_ptr = __stack_start + last_metadata_size;
	const size_t last_alloc_size = total_alloc_size - last_metadata_size;
	if (last_alloc_size != (last_alloc_end - start_ptr))
		goto err;
	if ((uintptr_t)ptr != start_ptr)
		goto done;

	/* Try to realloc, if it fails we won't modify allocator's state
	 * or metadata, so that the caller can retry with different sizes. */
	if (size) {
		saved_metadata_val = *last_metadata_ptr;
		goto alloc;
	}

	/* free -> rewind to previous allocation */
	last_alloc_end = start_ptr;
	total_alloc_size -= last_alloc_size;
	alloc_count--;

 done:
	lock_release(&alloc_lock);
	return result;

 err:
	ERR("Detected memory corruption at 0x%lx !\n", ptr);
	abort();
	__builtin_unreachable();
}

void *reallocarray(void *_Nullable ptr, size_t n, size_t size)
{
	/* Make sure we won't overflow on multiplication */
	size_t total_size = 0;
	if (__builtin_mul_overflow(n, size, &total_size))
		return NULL;

	void* new_ptr = realloc(ptr, total_size);
	if (new_ptr == NULL)
		return NULL;

	/* In case this is calloc() zero-out allocated memory, note
	 * that the spec for realloc only prevents initialization of
	 * existing allocations, not new ones, so we are still ok. */
	if (ptr == NULL)
		memset(new_ptr, 0, total_size);

	return new_ptr;
}

void *malloc(size_t size)
{
	return realloc(NULL, size);
}

void *calloc(size_t n, size_t size)
{
	return reallocarray(NULL, n, size);
}

void free(void *ptr)
{
	/* This will suppress the compiler warning about
	 * not using the result of realloc(). */
	void *unused = realloc(ptr, 0);
	(void)unused;
}


/*********************\
* Program Termination *
\*********************/

/* abort() - Abnormal program termination
 *
 * Per C standard (C17 7.22.4.1, C23 7.24.4.1):
 * - Causes abnormal program termination
 * - Does NOT call functions registered with atexit()
 * - Does NOT flush open output streams
 * - Does NOT close open streams
 * - Does NOT remove temporary files
 *
 * In a freestanding environment without a host, abort() raises an exception
 * for the platform layer to handle (trap handler, debugger, etc.).
 *
 * Implementation uses __builtin_trap() to emit a trap instruction, followed
 * by __builtin_unreachable() to inform the compiler that execution never
 * continues past this point.
 */
_Noreturn void abort(void)
{
	__builtin_trap();
	__builtin_unreachable();
}