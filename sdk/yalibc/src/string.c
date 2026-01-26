/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>	/* For typed integers */
#include <stddef.h>	/* For size_t / NULL */
#include <stdbool.h>	/* For bool/true/false */
#include <string.h>

/* Abstract data types to avoid casting and make
 * our intent clear when it comes to aliasing. */
union data {
	unsigned char *as_bytes;
	unsigned long *as_ulong;
	uintptr_t as_uptr;
};

union const_data {
	const unsigned char *as_bytes;
	const unsigned long *as_ulong;
	uintptr_t as_uptr;
};

#define WORD_SIZE sizeof(long)
#define WORD_MASK (WORD_SIZE - 1)

/* Endianness handling for word operations.
 * On little-endian: shift right (>>) to get low bytes, left (<<) to get high bytes
 * On big-endian: shift left (<<) to get low bytes, right (>>) to get high bytes */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SHIFT_LOW >>
#define SHIFT_HIGH <<
#else
#define SHIFT_LOW <<
#define SHIFT_HIGH >>
#endif

/* https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord */
#if __SIZEOF_LONG__ == 8
#define ONES	0x0101010101010101ULL
#define HIGHS	0x8080808080808080ULL
#else
#define ONES	0x01010101UL
#define HIGHS	0x80808080UL
#endif
#define HAS_ZERO(_x) (((_x) - ONES) & ~(_x) & HIGHS)

/* This is used to map 0 - 255 to a 256bit set and perform lookups
 * instead of going for a 256bytes array. */
#define BITMAP_SLOT(c, bitmap) \
	((bitmap)[(unsigned char)(c) / (8 * sizeof(unsigned long))])

#define BITMAP_BIT(c) \
	(1UL << ((unsigned char)(c) % (8 * sizeof(unsigned long))))


/*************\
* Memory Fill *
\*************/

/* C23 §7.26.6.1 - The memset function
 * Copies the value of c (converted to unsigned char) into each of the first
 * len characters of the object pointed to by dst_ptr. */
void*
memset(void* restrict dst_ptr, int c, size_t len)
{
	/* Nothing to do */
	if (!dst_ptr || !len)
		return dst_ptr;

	union data dst = { .as_bytes = dst_ptr };
	unsigned char byte = (unsigned char) c;

	/* Broadcast byte to all positions in word */
	unsigned long bytes = (unsigned long) c * ONES;
	size_t remaining = len;

	/* Fill up dst up to the alignment boundary */
	for(; dst.as_uptr & WORD_MASK; remaining--)
		*dst.as_bytes++ = byte;

	/* Fill up remaining words */
	for(; remaining >= WORD_SIZE; remaining -= WORD_SIZE)
		*dst.as_ulong++ = bytes;

	while(remaining-- > 0)
		*dst.as_bytes++ = byte;

	return dst_ptr;
}

/* C23 §7.26.6.2 - The memset_explicit function (Annex K)
 * Like memset, but with a guarantee that the compiler will not optimize it away.
 * Useful for clearing sensitive data like passwords or cryptographic keys. */
void*
memset_explicit(void *dst_ptr, int c, size_t n)
{
	memset(dst_ptr, c, n);
	/* Memory barrier to prevent compiler from optimizing away the memset.
	 * This is a compiler barrier (not a CPU barrier). */
	__asm__ __volatile__("" : : "r"(dst_ptr) : "memory");
	return dst_ptr;
}

/************************\
* Memory / String Search *
\************************/

/* C23 §7.26.5.1 - The memchr function
 * Locates the first occurrence of c (converted to unsigned char) in the
 * initial len characters of the object pointed to by src_ptr. */
void*
memchr(const void *src_ptr, int c, size_t len)
{
	/* Nothing to do */
	if (!src_ptr || !len)
		return NULL;

	union const_data src = { .as_bytes = src_ptr };
	unsigned char byte = (unsigned char) c;
	unsigned long mask = ONES * byte;
	size_t remaining = len;

	/* Search by byte up to the src's alignment boundary */
	for (; (src.as_uptr & WORD_MASK) && remaining > 0; remaining--, src.as_bytes++) {
		if (*src.as_bytes == byte)
			return (void *)src.as_bytes;
	}

	/* Search word by word using the mask */
	for(; remaining >= WORD_SIZE; remaining -= WORD_SIZE, src.as_ulong++) {
		unsigned long check = *src.as_ulong ^ mask;
		if(HAS_ZERO(check))
			break;
	}

	for(; remaining > 0; remaining--, src.as_bytes++) {
		if (*src.as_bytes == byte)
			return (void*) src.as_bytes;
	}

	return NULL;
}

/* Same a memchr but search backwards. This is a GNU extension used for
 * strrchr, leave it static for now so that the compiler can inline it
 * and we may expose it if needed. */
static void*
memrchr(const void *src_ptr, int c, size_t len)
{
	/* Nothing to do */
	if (!src_ptr || !len)
		return NULL;

	unsigned char byte = (unsigned char) c;
	union const_data src = { .as_bytes = (const unsigned char *)src_ptr + len };
	unsigned long mask = ONES * byte;

	/* Search backwards byte-by-byte until we're word-aligned */
	while ((src.as_uptr & WORD_MASK) && src.as_bytes > (const unsigned char *)src_ptr) {
		src.as_bytes--;
		if (*src.as_bytes == byte)
			return (void *)src.as_bytes;
	}

	/* Search backwards word by word */
	while (src.as_bytes - WORD_SIZE >= (const unsigned char *)src_ptr) {
		src.as_ulong--;
		unsigned long word = *src.as_ulong;
		if (HAS_ZERO(word ^ mask)) {
			/* Found it in this word, position at end and break */
			src.as_bytes = (const unsigned char *)src.as_ulong + WORD_SIZE;
			break;
		}
	}

	/* Scan backwards byte-by-byte */
	while (src.as_bytes > (const unsigned char *)src_ptr) {
		src.as_bytes--;
		if (*src.as_bytes == byte)
			return (void *)src.as_bytes;
	}

	return NULL;
}

/* C23 §7.26.5.2 - The strchr function
 * Locates the first occurrence of c (converted to char) in the string
 * pointed to by str_ptr. The terminating null character is considered
 * part of the string. */
char*
strchr(const char* str_ptr, int c)
{
	/* Nothing to do */
	if (!str_ptr)
		return NULL;

	/* Fast path: searching for null terminator */
	unsigned char byte = (unsigned char) c;
	if (byte == '\0')
		return (char *)(str_ptr + strlen(str_ptr));

	union const_data str = { .as_bytes = (const unsigned char *)str_ptr };
	unsigned long mask = ONES * byte;

	/* Search by byte up to the str's alignment boundary */
	while (str.as_uptr & WORD_MASK) {
		if (*str.as_bytes == '\0')
			return NULL;
		if (*str.as_bytes == byte)
			return (char *)str.as_bytes;
		str.as_bytes++;
	}

	/* Search word by word, checking for both null terminator and target char.
	 * HAS_ZERO(*str.as_ulong) detects null bytes.
	 * HAS_ZERO(*str.as_ulong ^ mask) detects bytes matching our target. */
	unsigned long word = *str.as_ulong;
	while (!HAS_ZERO(word) && !HAS_ZERO(word ^ mask)) {
		str.as_ulong++;
		word = *str.as_ulong;
	}

	/* Found either null or target char in current word, scan byte-by-byte */
	while (*str.as_bytes != '\0') {
		if (*str.as_bytes == byte)
			return (char *)str.as_bytes;
		str.as_bytes++;
	}

	return NULL;
}

/* C23 §7.26.5.5 - The strrchr function
 * Locates the last occurrence of c (converted to char) in the string pointed
 * to by str_ptr. The terminating null character is considered part of the string. */
char*
strrchr(const char *str_ptr, int c)
{
	/* Note: the terminating null character is considered
	 * part of the string. */
	return memrchr(str_ptr, c, strlen(str_ptr) + 1);
}

/* C23 §K.3.7.4.4 - The strnlen_s function (Annex K)
 * Computes the length of the string pointed to by str_ptr, but examines
 * at most maxlen characters. */
size_t
strnlen_s(const char *str_ptr, size_t maxlen)
{
	char *res = memchr(str_ptr, '\0', maxlen);
	if (res != NULL)
		return (size_t)(res - str_ptr);
	return maxlen;
}

/* Alias for compatibility - strnlen is POSIX, strnlen_s is C23 Annex K */
size_t strnlen(const char *str_ptr, size_t maxlen) __attribute__((alias("strnlen_s")));

/* C23 §7.26.6.3 - The strlen function
 * Computes the length of the string pointed to by str_ptr.
 * Note: Unbounded strlen is inherently unsafe. Use strnlen where possible. */
size_t
strlen(const char *str_ptr)
{
	union const_data str = { .as_bytes = (const unsigned char *)str_ptr };

	if (!str_ptr)
		return 0;

	while (str.as_uptr & WORD_MASK) {
		if (*str.as_bytes == '\0')
			return (size_t) (str.as_bytes - (const unsigned char *)str_ptr);
		str.as_bytes++;
	}

	while (!HAS_ZERO(*str.as_ulong))
		str.as_ulong++;

	while (*str.as_bytes != '\0')
		str.as_bytes++;

	return (size_t) (str.as_bytes - (const unsigned char *)str_ptr);
}

/* C23 §7.26.5.6 - The strspn function
 * Computes the length of the maximum initial segment of the string pointed
 * to by str which consists entirely of characters from the string pointed to by accept. */
size_t
strspn(const char *str, const char *accept)
{
	const char *start = str;
	unsigned long bitmap[32 / sizeof(unsigned long)] = {0};
	
	/* Empty accept set means no characters match */
	if (!accept[0])
		return 0;
	
	/* Single-char accept set */
	if (!accept[1]) {
		while (*str == accept[0])
			str++;
		return (size_t)(str - start);
	}
	
	/* Build bitmap */
	for (const char *a = accept; *a; a++)
		BITMAP_SLOT(*a, bitmap) |= BITMAP_BIT(*a);

	/* Scan while character IS in accept set */
	while (*str && (BITMAP_SLOT(*str, bitmap) & BITMAP_BIT(*str)))
		str++;

	return (size_t)(str - start);
}

/* C23 §7.26.5.3 - The strcspn function
 * Computes the length of the maximum initial segment of the string pointed to
 * by str which consists entirely of characters NOT from the string pointed to by reject. */
size_t
strcspn(const char *str, const char *reject)
{
	const char *start = str;
	unsigned long bitmap[32 / sizeof(unsigned long)] = {0};

	if (!reject[0])
		return strlen(str);
	if (!reject[1]) {
		const char *match = strchr(str, reject[0]);
		return match ? (size_t)(match - str) : strlen(str);
	}

	for (const char *r = reject; *r; r++)
		BITMAP_SLOT(*r, bitmap) |= BITMAP_BIT(*r);

	/* Scan until hitting a rejected char */
	while (*str && !(BITMAP_SLOT(*str, bitmap) & BITMAP_BIT(*str)))
		str++;

	return (size_t)(str - start);
}

/* C23 §7.26.5.4 - The strpbrk function
 * Locates the first occurrence in the string pointed to by str of any character
 * from the string pointed to by accept. */
char*
strpbrk(const char *str, const char *accept)
{
	size_t len = strcspn(str, accept);
	return str[len] ? (char *)(str + len) : NULL;
}

/* C23 §7.26.5.8 - The strtok function
 * Sequentially finds tokens in the string pointed to by str, where tokens are
 * delimited by characters from delim_set. Not thread-safe. */
char*
strtok(char *restrict str, const char *restrict delim_set)
{
	/* Static state - remembers position between calls,
	 * From the spec: "The strtok function is not required
	 * to avoid data races with other calls to the strtok
	 * function.", so this is not thread-safe nor re-entrant. */
	static char *last_delim;

	/* If str is NULL, try to use saved position p,
	 * If both are NULL, we're done*/
	if (!str && !(str = last_delim))
		return NULL;

	/* Skip leading delimiters so that str points at the
	 * start of the next token. If we start with a token
	 * strspn will return 0, if we start with delimiter(s)
	 * it'll skip them and point to the next token. */
	str += strspn(str, delim_set);

	/* We hit the end of string, reset state. */
	if (!*str)
		return (last_delim = 0);

	/* str now points to the start of the token */

	/* Find the next delimiter to prepare for the next call,
	 * and also detect if that's the last token. */
	last_delim = str + strcspn(str, delim_set);

	/* If we found a delimiter, replace it with null and
	 * advance last_delim to prepare for next call. If we
	 * reached the end of the string reset state (and we'll
	 * get NULL on the next call due to the check above). */
	if (*last_delim)
		*last_delim++ = '\0';
	else
		last_delim = 0;

	return str;
}

/**********************\
* Memory / String Copy *
\**********************/

/* Forward copy data from src_ptr to dst_ptr */
static void
copy_fw(void* restrict dst_ptr, const void* restrict src_ptr, size_t len)
{
	union const_data src = { .as_bytes = src_ptr };
	union data dst = { .as_bytes = dst_ptr };
	size_t remaining = len;
	size_t src_offt = 0;

	/* Don't bother copying per-word if buffer is too small. */
	if (len < 2 * WORD_SIZE)
		goto trailing_fw;

	/* Copy per-byte up to destination's alignment boundary.
	 * Note: no need to check if remaining > 0 since we know
	 * that len >= 2 * WORD_SIZE. */
	for(; dst.as_uptr & WORD_MASK; remaining--)
		*dst.as_bytes++ = *src.as_bytes++;

	/* Destination is now aligned, if source is
	 * also aligned continue per-word copy. */
	src_offt = src.as_uptr & WORD_MASK;
	if (!src_offt) {
		for (; remaining >= WORD_SIZE; remaining -= WORD_SIZE)
			*dst.as_ulong++ = *src.as_ulong++;
	} else {
		/* Source is src_offt bytes ahead of an alignment boundary.
		 * We can write aligned words to dst, but must construct each
		 * word from two unaligned source reads: src_offt bytes from
		 * the end of one word and (WORD_SIZE - src_offt) from the next.
		 *
		 * Note: We require 2 * WORD_SIZE remaining to avoid over-reading
		 * past the source buffer, since we always read one word ahead. */
		unsigned long cur, next;
		unsigned int low_shift = (src_offt * 8);
		unsigned int high_shift = ((WORD_SIZE - src_offt) * 8);

		/* Rewind src to it alignment boundary */
		src.as_bytes -= src_offt;

		/* Read the first one outside the loop so that we only read next
		 * in the loop (instead of both cur and next) and reuse it. */
		next = *src.as_ulong;
		for (; remaining >= (2 * WORD_SIZE); remaining -= WORD_SIZE) {
			cur = next;
			next = *++src.as_ulong;
			*dst.as_ulong++ = cur SHIFT_LOW low_shift | next SHIFT_HIGH high_shift;
		}
		/* We may have trailing bytes, make sure we continue
		 * reading src from the correct offset. */
		src.as_bytes += src_offt;
	}

 trailing_fw:
	while (remaining-- > 0)
		*dst.as_bytes++ = *src.as_bytes++;
}

/* Same but backwards
 * Note: Restrict is still valid here because we're copying from high addresses
 * down, so we never read from memory we've already written. */
static void
copy_bw(void* restrict dst_ptr, const void* restrict src_ptr, size_t len)
{
	union const_data src = { .as_bytes = src_ptr + len };
	union data dst = { .as_bytes = dst_ptr + len };
	size_t remaining = len;
	size_t src_offt = 0;

	if (len < 2 * WORD_SIZE)
		goto trailing_bw;

	for(; dst.as_uptr & WORD_MASK; remaining--)
		*--dst.as_bytes = *--src.as_bytes;

	src_offt = src.as_uptr & WORD_MASK;
	if (!src_offt) {
		for (; remaining >= WORD_SIZE; remaining -= WORD_SIZE)
			*--dst.as_ulong = *--src.as_ulong;
	} else {
		unsigned long cur, prev;
		unsigned int high_shift = ((WORD_SIZE - src_offt) * 8);
		unsigned int low_shift = (src_offt * 8);
		src.as_bytes -= src_offt;
		prev = *src.as_ulong;
		for (; remaining >= (2 * WORD_SIZE); remaining -= WORD_SIZE) {
			cur = prev;
			prev = *--src.as_ulong;
			*--dst.as_ulong = cur SHIFT_HIGH high_shift | prev SHIFT_LOW low_shift;
		}
		src.as_bytes += src_offt;
	}

 trailing_bw:
	while (remaining-- > 0)
		*--dst.as_bytes = *--src.as_bytes;
}

/* C23 §7.26.2.2 - The memmove function
 * Copies len characters from the object pointed to by src into the object
 * pointed to by dst. Copying takes place as if via an intermediate buffer,
 * so the objects may overlap. */
void*
memmove(void *dst, const void *src, size_t len)
{
	/* Nothing to do */
	if (!src || !dst || dst == src || !len)
		return dst;

	/* Source and destination regions may overlap, so in
	 * order to prevent overwriting the source while copying,
	 * copy backwards when we move a region to the left and
	 * forward when we copy it to the right. */
	if (src > dst)
		copy_fw(dst, src, len);
	else
		copy_bw(dst, src, len);

	return dst;
}

/* C23 §7.26.2.1 - The memcpy function
 * Copies len characters from the object pointed to by src into the object pointed
 * to by dst. The objects shall not overlap (use memmove if they might). */
void*
memcpy(void* restrict dst, const void* restrict src, size_t len)
{
	if (!src || !dst || dst == src || !len)
		return dst;

	copy_fw(dst, src, len);
	return dst;
}

/* C23 §7.26.2.3 - The memccpy function
 * Copies characters from src to dst, stopping after the first occurrence of c
 * (converted to unsigned char) is copied, or after n characters. */
void*
memccpy(void * restrict dst, const void * restrict src, int c, size_t n)
{
	bool got_match = false;
	size_t bytes_to_copy = n;
	unsigned char* end = (unsigned char*) memchr(src, c, n);
	if(end) {
		got_match = true;
		bytes_to_copy = (size_t)(end - (unsigned char*)src) + 1;
	}
	memcpy(dst, src, bytes_to_copy);
	if (got_match)
		return (unsigned char *)dst + bytes_to_copy;
	else
		return NULL;
}

/* These are actually GNU/POSIX functions so we could export them, but since
 * we'll use them to implement as building blocks as the man pages suggest
 * let's keep them static so that the compiler has a change to inline them
 * if needed. */
static void*
mempcpy(void* restrict dst, const void* restrict src, size_t len)
{
	return (unsigned char*)memcpy(dst, src, len) + len;
}

static char*
stpcpy(char *restrict dst, const char *restrict src)
{
	char* end = mempcpy(dst, src, strlen(src));
	*end = '\0';
	return end;
}

static char*
stpncpy(char *restrict dst, const char *restrict src, size_t dsize)
{
	size_t dlen = strnlen_s(src, dsize);
	return memset(mempcpy(dst, src, dlen), 0, dsize - dlen);
}

/* C23 §7.26.2.4 - The strcpy function
 * Copies the string pointed to by src (including the terminating null character)
 * into the array pointed to by dst. The strings shall not overlap. */
char*
strcpy(char * restrict dst, const char * restrict src)
{
	stpcpy(dst, src);
	return dst;
}

/* C23 §7.26.2.5 - The strncpy function
 * Copies not more than dsize characters from src to dst. If src is shorter than
 * dsize, null characters are appended to dst until dsize characters have been written. */
char *
strncpy(char *restrict dst, const char *restrict src, size_t dsize)
{
	stpncpy(dst, src, dsize);
	return dst;
}

/* C23 §7.26.3.1 - The strcat function
 * Appends a copy of the string pointed to by src (including the terminating null
 * character) to the end of the string pointed to by dst. */
char*
strcat(char *restrict dst, const char *restrict src)
{
	stpcpy(dst + strlen(dst), src);
	return dst;
}

/* C23 §7.26.3.2 - The strncat function
 * Appends not more than ssize characters from src to the end of dst, then appends
 * a null character. */
char*
strncat(char *restrict dst, const char *restrict src, size_t ssize)
{
	char *end = mempcpy(dst + strlen(dst), src, strnlen_s(src, ssize));
	*end = '\0';
	return dst;
}

/****************************\
* Memory / String Comparison *
\****************************/

/*
 * Find the first differing byte between two words and return the
 * comparison result. This handles endianness correctly by scanning
 * from the lowest address (LSB on little-endian, MSB on big-endian).
 */
static inline int
compare_word_bytes(unsigned long a, unsigned long b)
{
	for (unsigned int i = 0; i < WORD_SIZE; i++) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		unsigned int shift = i * 8;
#else
		unsigned int shift = (WORD_SIZE - 1 - i) * 8;
#endif
		unsigned int a_byte = (a >> shift) & 0xFF;
		unsigned int b_byte = (b >> shift) & 0xFF;
		if (a_byte != b_byte)
			return (int)a_byte - (int)b_byte;
	}
	return 0;
}

/* C23 §7.26.4.1 - The memcmp function
 * Compares the first len characters of the object pointed to by s1 to the first
 * len characters of the object pointed to by s2. */
int
memcmp(const void *s1, const void *s2, size_t len)
{
	/* Nothing to do */
	if (!s1 || !s2 || s1 == s2 || !len)
		return 0;

	union const_data a = { .as_bytes = s1 };
	union const_data b = { .as_bytes = s2 };
	unsigned long a_val;
	unsigned long b_val;
	size_t remaining = len;

	if (len < 2 * WORD_SIZE)
		goto compare_bytes;

	for(; b.as_uptr & WORD_MASK; remaining--) {
		if (*a.as_bytes != *b.as_bytes)
			return (int)*a.as_bytes - (int)*b.as_bytes;
		a.as_bytes++;
		b.as_bytes++;
	}

	size_t a_offt = a.as_uptr & WORD_MASK;
	if (!a_offt) {
		for (; remaining >= WORD_SIZE; remaining -= WORD_SIZE) {
			a_val = *a.as_ulong++;
			b_val = *b.as_ulong++;
			if (a_val != b_val)
				goto compare_words;
		}
	} else {
		unsigned long a_cur, a_next;
		unsigned int low_shift = a_offt * 8;
		unsigned int high_shift = (WORD_SIZE - a_offt) * 8;
		a.as_bytes -= a_offt;
		a_next = *a.as_ulong;
		for (; remaining >= WORD_SIZE; remaining -= WORD_SIZE, b.as_ulong++) {
			a_cur = a_next;
			a_next = *++a.as_ulong;
			a_val = a_cur SHIFT_LOW low_shift | a_next SHIFT_HIGH high_shift;
			b_val = *b.as_ulong;
			if (a_val != b_val)
				goto compare_words;
		}
		a.as_bytes += a_offt;
	}

 compare_bytes:
	for (; remaining > 0; remaining--) {
		if (*a.as_bytes != *b.as_bytes)
			return (int)*a.as_bytes - (int)*b.as_bytes;
		a.as_bytes++;
		b.as_bytes++;
	}

	return 0;

 compare_words:
	return compare_word_bytes(a_val, b_val);
}

/*
 * strncmp: Compare strings up to len bytes or first null.
 *
 * Unlike memcmp, word-at-a-time optimization isn't worthwhile here:
 * we'd need to check each word for both differences AND null bytes,
 * adding complexity. Most strncmp calls involve short strings or
 * find differences early, so the setup overhead would dominate.
 */
/* C23 §7.26.4.3 - The strncmp function
 * Compares not more than len characters from the string pointed to by s1 to
 * the string pointed to by s2. */
int
strncmp(const char *s1, const char *s2, size_t len)
{
	/* Nothing to do */
	if (!s1 || !s2 || s1 == s2 || !len)
		return 0;

	while (len--) {
		unsigned char c1 = *s1++;
		unsigned char c2 = *s2++;
		if (c1 != c2)
			return (int)c1 - (int)c2;
		if (c1 == '\0')
			return 0;
	}

	return 0;
}

/* C23 §7.26.4.2 - The strcmp function
 * Compares the string pointed to by s1 to the string pointed to by s2.
 * Same as strncmp but unbounded. */
int
strcmp(const char *s1, const char *s2)
{
	/* Nothing to do */
	if (!s1 || !s2 || s1 == s2)
		return 0;

	for (;;) {
		unsigned char c1 = *s1++;
		unsigned char c2 = *s2++;
		if (c1 != c2)
			return (int)c1 - (int)c2;
		if (c1 == '\0')
			return 0;
	}
}

/*
 * String search - optimized for small needles (up to WORD_SIZE chars)
 * Uses a sliding window approach, combining needle bytes into a word
 * and comparing against haystack words as we slide through.
 */
static char*
small_needle_search(const char *haystack, const char *needle)
{
	const unsigned char *h = (const unsigned char *)haystack;
	const unsigned char *n = (const unsigned char *)needle;
	unsigned long nw = 0;
	unsigned long hw = 0;
	unsigned long mask = 0;
	size_t needle_len = 0;

	/* Pack needle into word by shifting left.
	 * This creates an endian-agnostic representation since we use
	 * the same strategy for both needle and haystack windows.
	 * For "bc" on 64-bit little-endian: nw = 0x0000000000006362
	 * For "bc" on 64-bit big-endian: nw = 0x6263000000000000 */
	while (n[needle_len] && needle_len < WORD_SIZE) {
		nw = (nw << 8) | n[needle_len];
		needle_len++;
	}

	/* If needle is longer than WORD_SIZE, bail out */
	if (n[needle_len] != '\0')
		return NULL;

	/* Build mask to match needle length.
	 * For needle_len=2 on 64-bit: mask = 0xFFFF */
	mask = (1UL << (8 * needle_len)) - 1;

	/* Pack initial haystack window */
	for (size_t i = 0; i < needle_len; i++) {
		/* Haystack too short */
		if (!h[i])
			return NULL;
		hw = (hw << 8) | h[i];
	}

	/* Position h at the last byte of the initial window */
	h += needle_len - 1;

	/* Slide through haystack, comparing windows.
	 * We mask hw to only compare the relevant bytes. */
	while (*h) {
		if ((hw & mask) == nw)
			return (char *)(h - needle_len + 1);

		/* Slide window: shift left by 8 bits and bring in next byte */
		hw = (hw << 8) | *++h;
	}

	/* We've hit the null terminator. The needle might still match at
	 * positions near the end. Keep shifting in zeros for (needle_len - 1)
	 * more iterations to check all possible end-of-string matches. */
	for (size_t i = 0; i < needle_len; i++) {
		if ((hw & mask) == nw)
			return (char *)(h - needle_len + 1 + i);
		hw = (hw << 8);
	}

	return NULL;
}

/*
 * Two-Way string matching algorithm for large needles.
 * Based on: Maxime Crochemore and Dominique Perrin,
 * "Two-way string-matching", Journal of the ACM, 38(3):651-675, July 1991.
 *
 * In signal processing terms (the way I understood this), this is basically
 * a PLL. We have an input signal (the haystack) and an impulse (the needle),
 * and we try to find synchronization points (phase-locks) in a loop, where
 * the input signal and the impulse correlate.
 *
 * The acquisition process has two stages:
 *
 * 1. Coarse frequency detection (spectral filter/gate): We check if the last
 *    sample in our window exists anywhere in the impulse. If it has zero
 *    spectral content (a freqency of zero), we reject the entire window and
 *    skip forward by needle_len - no phase alignment is physically possible.
 *
 * 2. Fine frequency alignment (shift table): If the sample exists in the impulse,
 *    we use the shift table to calculate the optimal phase offset. Instead of
 *    blindly advancing sample-by-sample, we compute how far to shift based on
 *    where that frequency component last appeared in the impulse. This is like
 *    adjusting a VCO (Voltage-Controlled Oscillator) to bring the phases closer
 *    to alignment before attempting full correlation.
 *
 * After these pre-filters, the PLL attempts to lock. The synchronization point
 * (Critical Point) is chosen to maximize phase discrimination: it splits the
 * impulse into two halves that are maximally non-overlapping under any shift.
 * This enables a two-stage discriminator: coarse phase check on the right half,
 * followed by fine confirmation on the left half (hence "two-way").
 *
 * When a mismatch occurs after partially matching the right half, we safely
 * skip forward by the amount of matched signal, since the Critical Factorization
 * Theorem guarantees no smaller phase shift could result in a valid lock. This
 * corresponds to losing lock and re-entering acquisition mode.
 *
 * When a mismatch occurs on the left half, the carrier is still valid and phase
 * alignment has been established. The algorithm advances by one period and
 * preserves a bounded amount of phase memory, remaining in tracking mode without
 * rechecking previously validated samples.
 *
 * This phase memory ensures no haystack position is examined more than a constant
 * number of times, guaranteeing linear-time behavior even for highly repetitive
 * or noise-like inputs.
 */
static char*
twoway_strstr(const unsigned char *h, const unsigned char *n)
{
	/* Shift table: stores rightmost position + 1 for each byte value.
	 * When we see a character at position needle_len-1 in the haystack window,
	 * shift[char] tells us the last occurrence of that char in the needle.
	 * We then calculate: skip = needle_len - shift[char], which aligns
	 * that character in the haystack with its last occurrence in the needle.
	 * This step comes from the Boyer-Moore-Horspool algorithm as an optimization
	 * step (or in our PLL analogy as a pre-filter). */
	size_t byteset[32 / sizeof(size_t)] = {0};	/* 256bits = 32 bytes */
	size_t shift[256];

	/* While there also calculate needle_len */
	size_t needle_len = 0;
	for (; n[needle_len] && h[needle_len]; needle_len++) {
		BITMAP_SLOT(n[needle_len], byteset) |= BITMAP_BIT(n[needle_len]);
		shift[n[needle_len]] = needle_len + 1;
	}

	/* If needle is longer than haystack (we hit end of haystack first), no match */
	if (n[needle_len])
		return NULL;

	/* Find the critical factorization position by computing the maximal suffix.
	 * We do this twice with opposite comparisons to find the optimal split point. */

	/* First pass: find maximal suffix with > comparison */
	size_t crit_pos = -1;
	size_t shift_idx = 0;
	size_t match_pos = 1;
	size_t period = 1;
	while (shift_idx + match_pos < needle_len) {
		if (n[crit_pos + match_pos] == n[shift_idx + match_pos]) {
			/* Characters match - extend current period */
			if (match_pos == period) {
				shift_idx += period;
				match_pos = 1;
			} else
				match_pos++;
		} else if (n[crit_pos + match_pos] > n[shift_idx + match_pos]) {
			/* Found a larger suffix - update */
			shift_idx += match_pos;
			match_pos = 1;
			period = shift_idx - crit_pos;
		} else {
			/* Current suffix is smaller - shift critical position */
			crit_pos = shift_idx++;
			match_pos = period = 1;
		}
	}

	size_t max_suffix = crit_pos;
	size_t period_cand = period;

	/* Second pass: find maximal suffix with < comparison */
	crit_pos = -1;
	shift_idx = 0;
	match_pos = period = 1;
	while (shift_idx + match_pos < needle_len) {
		if (n[crit_pos + match_pos] == n[shift_idx + match_pos]) {
			if (match_pos == period) {
				shift_idx += period;
				match_pos = 1;
			} else
				match_pos++;
		} else if (n[crit_pos + match_pos] < n[shift_idx + match_pos]) {
			shift_idx += match_pos;
			match_pos = 1;
			period = shift_idx - crit_pos;
		} else {
			crit_pos = shift_idx++;
			match_pos = period = 1;
		}
	}

	/* Use the larger critical position, keep corresponding period */
	if (crit_pos + 1 > max_suffix + 1)
		max_suffix = crit_pos;
	else
		period = period_cand;

	/* Check if needle is periodic. If the prefix doesn't match at period offset,
	 * then we have a non-periodic pattern and can use a larger shift. */
	size_t mem_period;
	if (memcmp(n, n + period, max_suffix + 1)) {
		/* Non-periodic: use larger period for better skipping */
		mem_period = 0;
		period = (max_suffix > needle_len - max_suffix - 1) ?
			 (max_suffix + 1) : (needle_len - max_suffix);
	} else {
		/* Periodic: remember how much we can skip on mismatch */
		mem_period = needle_len - period;
	}

	/* Main search loop */
	const unsigned char* haystack_end = h;
	size_t mem = 0;
	for (;;) {
		/* Update our estimate of where the haystack ends.
		 * We incrementally scan forward to find the null terminator. */
		if ((size_t)(haystack_end - h) < needle_len) {
			/* Round up to next 64-byte boundary (cache line size) */
			size_t grow = needle_len | 63;
			const unsigned char *null_pos = memchr(haystack_end, 0, grow);

			if (null_pos) {
				haystack_end = null_pos;
				if ((size_t)(haystack_end - h) < needle_len)
					return NULL;
			} else {
				haystack_end += grow;
			}
		}

		/* Spectral filter: check if last byte of needle appears in current window.
		 * If not in byteset, we can skip the entire needle length. */
		if (BITMAP_SLOT(h[needle_len - 1], byteset) & BITMAP_BIT(h[needle_len - 1])) {
			/* Last byte matches - check shift table to realign
			 * acquisition window. */
			match_pos = needle_len - shift[h[needle_len - 1]];

			if (match_pos) {
				/* We can skip forward, but respect
				 * memory from previous match */
				if (match_pos < mem)
					match_pos = mem;
				h += match_pos;
				mem = 0;
				continue;
			}
		} else {
			/* Last byte not in needle at all
			 * skip entire needle length */
			h += needle_len;
			mem = 0;
			continue;
		}

		/* Two-way string-matching */

		/* Compare the right part, after critical point
		 * (coarse phase discrimitator) */
		for (match_pos = (max_suffix + 1 > mem) ? (max_suffix + 1) : mem;
		     n[match_pos] && n[match_pos] == h[match_pos];
		     match_pos++);

		if (n[match_pos]) {
			/* Right half mismatch - shift past critical position */
			h += match_pos - max_suffix;
			mem = 0;
			continue;
		}

		/* Right half matched - now check left half
		 * (partial lock) */
		for (match_pos = max_suffix + 1;
		     match_pos > mem && n[match_pos - 1] == h[match_pos - 1];
		     match_pos--);

		if (match_pos <= mem) {
			/* Full match !
			 * (haystack/needle are phase-locked) */
			return (char *)h;
		}

		/* Partial match - shift by period
		 * and remember what matched */
		h += period;
		mem = mem_period;
	}
}

/* C23 §7.26.5.7 - The strstr function
 * Locates the first occurrence in the string pointed to by haystack of the
 * sequence of characters (excluding the terminating null character) in the
 * string pointed to by needle. */
char*
strstr(const char *haystack, const char *needle)
{
	/* Nothing to do */
	if (!haystack)
		return NULL;

	/* Empty needle returns haystack (spec behavior) */
	if (!needle || *needle == '\0')
		return (char *)haystack;

	/* Single char needle - use strchr */
	if (needle[1] == '\0')
		return strchr((char *)haystack, needle[0]);

	/* Try first for small needles that can fit in a word */
	char* match_ptr = small_needle_search(haystack, needle);
	if (match_ptr)
		return match_ptr;

	/* For larger needles, use Two-Way algorithm */
	return twoway_strstr((const unsigned char *)haystack,
			     (const unsigned char *)needle);
}