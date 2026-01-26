/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>		/* For typed ints */
#include <stddef.h>		/* For size_t / NULL */
#include <stdbool.h>		/* For bool */
#include <string.h>		/* For memset() */
#include <errno.h>		/* For error codes */
#include <platform/riscv/csr.h>	/* For CSR definitions / SATP_MODE_* */
#include <platform/utils/utils.h>	/* For DBG() */

/* External values from start.S */
extern const uintptr_t __ram_end;

/* Heap management - from stdlib.c */
extern uintptr_t __adjust_heap_end(uintptr_t new_heap_end);

/* Page sizes */
#define PAGE_SIZE		4096
#define NAPOT_64KB_SIZE		65536
#define MEGAPAGE_SIZE		(2 * 1024 * 1024)
#define GIGAPAGE_SIZE		(1024 * 1024 * 1024)

/* PTE flags */
#define PTE_V			(1ULL << 0)	/* Valid */
#define PTE_R			(1ULL << 1)	/* Readable */
#define PTE_W			(1ULL << 2)	/* Writable */
#define PTE_X			(1ULL << 3)	/* Executable */
#define PTE_U			(1ULL << 4)	/* User accessible */
#define PTE_G			(1ULL << 5)	/* Global */
#define PTE_A			(1ULL << 6)	/* Accessed */
#define PTE_D			(1ULL << 7)	/* Dirty */
#define PTE_N			(1ULL << 63)	/* Svnapot */

/* Page table metadata - stored in .data (minimal space) */
static uint8_t g_num_levels = 0;		/* Number of PT levels allocated */
static uint64_t g_current_mode = 0;		/* Current SATP mode */
static uintptr_t g_pt_base = 0;			/* Base address of page tables */

/* Helper to get page table address for a given level */
static inline uint64_t *get_pt_level(uint8_t level)
{
	if (level >= g_num_levels || g_pt_base == 0)
		return NULL;
	return (uint64_t *)(g_pt_base + level * PAGE_SIZE);
}

/*
 * Map a physical address range to virtual address 0
 *
 * phys_addr: Physical address to map (must be aligned to PAGE_SIZE/NAPOT_64KB_SIZE)
 * size: Pointer to size in bytes, updated with actual VA space mapped
 * mode: SATP mode (SATP_MODE_SV39/48/57)
 * napot: Use 64KB NAPOT pages if true, otherwise 4KB pages
 *
 * Returns: 0 on success, -errno on failure
 *
 * Notes:
 * - Always maps to VA 0
 * - Creates multi-level page table hierarchy (3 levels for Sv39, 4 for Sv48, 5 for Sv57)
 * - Leaf PTEs are created at the last level for 4KB or 64KB NAPOT pages
 * - If size is 0, frees the page table and restores heap
 * - If mode changes, frees old page table and creates new one
 * - Page tables allocated from end of RAM backwards
 */
int hart_va_map_range(uintptr_t phys_addr, size_t *size, uint64_t mode, bool napot)
{
	/* Determine number of levels based on mode */
	uint8_t num_levels;
	switch (mode) {
	case SATP_MODE_SV39:
		num_levels = 3;
		break;
	case SATP_MODE_SV48:
		num_levels = 4;
		break;
	case SATP_MODE_SV57:
		num_levels = 5;
		break;
	default:
		num_levels = 0;
		break;
	}

	/* Handle cleanup/free request */
	if (*size == 0 || (g_num_levels > 0 && g_current_mode != mode)) {
		if (g_num_levels > 0) {
			DBG("VA: Freeing %u-level page table, restoring heap_end to 0x%lx\n",
			    g_num_levels, __ram_end);
			/* Clear SATP */
			csr_write(CSR_SATP, 0);
			/* Sync TLB */
			asm volatile("sfence.vma zero, zero" ::: "memory");

			/* Clear all page table levels */
			size_t total_size = g_num_levels * PAGE_SIZE;
			memset((void *)g_pt_base, 0, total_size);

			g_pt_base = 0;
			g_num_levels = 0;
			g_current_mode = 0;
			__adjust_heap_end(__ram_end);
		}
		if (*size == 0)
			return 0;
	}

	/* Validate alignment based on page type */
	uintptr_t page_size = napot ? NAPOT_64KB_SIZE : PAGE_SIZE;
	if (phys_addr & (page_size - 1)) {
		DBG("VA: Physical address 0x%lx not aligned to %lu bytes\n",
		    phys_addr, page_size);
		return -EADDRNOTAVAIL;
	}

	/* Allocate and initialize page table hierarchy if needed */
	if (g_num_levels == 0) {
		/* Calculate space needed */
		size_t space_needed = num_levels * PAGE_SIZE;
		uintptr_t pt_base = (__ram_end - space_needed) & ~(PAGE_SIZE - 1);

		/* Make sure we have enough space */
		if (pt_base + space_needed > __ram_end) {
			DBG("VA: Not enough space for %u-level page table\n", num_levels);
			return -ENOMEM;
		}

		/* Try to adjust heap end to prevent malloc from using this space.
		 * __adjust_heap_end will check for overlap with existing allocations. */
		uintptr_t actual_heap_end = __adjust_heap_end(pt_base);
		if (actual_heap_end != pt_base) {
			DBG("VA: Page table would overlap with heap (pt_base=0x%lx, heap_end=0x%lx)\n",
			    pt_base, actual_heap_end);
			return -ENOMEM;
		}

		g_pt_base = pt_base;
		g_num_levels = num_levels;
		g_current_mode = mode;

		DBG("VA: Allocating %u-level page table at 0x%lx (mode=%lu)\n",
		    num_levels, pt_base, mode);

		/* Zero out all levels */
		memset((void *)pt_base, 0, space_needed);

		/* Initialize the hierarchy: each non-leaf level points to the next level
		 * We only need entry [0] at each level since we're mapping VA 0 */
		for (uint8_t level = 0; level < num_levels - 1; level++) {
			uint64_t *pt = get_pt_level(level);
			uintptr_t next_pt_addr = (uintptr_t)get_pt_level(level + 1);
			uint64_t next_ppn = next_pt_addr >> 12;
			/* Non-leaf PTE: V=1, but no RWX bits */
			uint64_t pte = (next_ppn << 10) | PTE_V;
			pt[0] = pte;
			DBG("VA:   Level %u[0] -> Level %u: PTE=0x%016lx\n",
			    level, level + 1, pte);
		}
	}

	/* Now add leaf PTEs at the last level */
	uint8_t leaf_level = g_num_levels - 1;
	uint64_t *leaf_pt = get_pt_level(leaf_level);

	if (leaf_pt == NULL) {
		DBG("VA: Failed to get leaf page table\n");
		return -EADDRNOTAVAIL;
	}

	/* Calculate how many PTEs we need */
	size_t num_ptes = (*size + page_size - 1) / page_size;
	size_t max_ptes = PAGE_SIZE / sizeof(uint64_t);

	if (num_ptes > max_ptes)
		num_ptes = max_ptes;

	/* Update size with actual VA space we'll map */
	*size = num_ptes * page_size;

	DBG("VA: Mapping PA 0x%lx -> VA 0x0 at level %u (%lu %s, %lu bytes total)\n",
	    phys_addr, leaf_level, num_ptes, napot ? "64KB pages" : "4KB pages", *size);

	/* Create leaf PTEs */
	for (size_t i = 0; i < num_ptes; i++) {
		uintptr_t pte_phys = phys_addr + (i * page_size);
		uint64_t ppn = pte_phys >> 12;
		uint64_t pte = (ppn << 10) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D;

		/* For NAPOT 64KB pages, set N bit and encode size in PPN[3:0] */
		if (napot) {
			/* Clear PPN[3:0] and set bit 3 for 64KB encoding */
			pte &= ~(0xFULL << 10);
			pte |= (0x8ULL << 10);
			pte |= PTE_N;
		}

		leaf_pt[i] = pte;
		DBG("VA:   PTE[%lu] = 0x%016lx (PA 0x%lx)\n", i, pte, pte_phys);
	}

	/* Set SATP with the root page table and mode */
	uint64_t *root_pt = get_pt_level(0);
	uintptr_t root_pt_addr = (uintptr_t)root_pt;
	uint64_t root_ppn = root_pt_addr >> 12;
	uint64_t satp = (mode << 60) | root_ppn;

	DBG("VA: Setting SATP to 0x%016lx (mode=%lu, root_ppn=0x%lx)\n", satp, mode, root_ppn);

	/* Dump page table hierarchy for verification */
	DBG("VA: Page table hierarchy dump:\n");
	for (uint8_t level = 0; level < g_num_levels; level++) {
		uint64_t *pt = get_pt_level(level);
		uint64_t pte = pt[0];
		DBG("VA:   Level %u @ PA 0x%lx:\n", level, (uintptr_t)pt);
		DBG("VA:     PTE[0] = 0x%016lx\n", pte);
		DBG("VA:     V=%d R=%d W=%d X=%d U=%d\n",
		    !!(pte & PTE_V), !!(pte & PTE_R), !!(pte & PTE_W),
		    !!(pte & PTE_X), !!(pte & PTE_U));
		if (level < g_num_levels - 1) {
			/* Non-leaf PTE */
			uint64_t next_ppn = (pte >> 10) & 0xFFFFFFFFFFF;
			DBG("VA:     Points to PA 0x%lx\n", next_ppn << 12);
		} else {
			/* Leaf PTE */
			uint64_t ppn = (pte >> 10) & 0xFFFFFFFFFFF;
			DBG("VA:     Maps to PA 0x%lx\n", ppn << 12);
		}
	}

	csr_write(CSR_SATP, satp);

	/* Sync TLB */
	asm volatile("sfence.vma zero, zero" ::: "memory");

	/* Read back and verify mode stuck */
	uint64_t satp_readback = csr_read(CSR_SATP);
	uint64_t mode_readback = satp_readback >> 60;

	DBG("VA: SATP readback: 0x%016lx\n", satp_readback);

	if (mode_readback != mode) {
		DBG("VA: SATP mode %lu didn't stick (got %lu)\n", mode, mode_readback);
		/* Clean up */
		size_t zero_size = 0;
		hart_va_map_range(0, &zero_size, 0, false);
		return -ENOTSUP;
	}

	return 0;
}
