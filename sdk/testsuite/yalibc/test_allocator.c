/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>			/* For typed ints */
#include <platform/utils/utils.h>	/* For ANN/INF/ERR */
#include <stdlib.h>			/* For malloc */
#include <test_framework.h>		/* For test registration macros */

static int
test_allocator(void)
{
	int failures = 0;

	ANN("\n---===Memory allocator tests===---\n");

	/* Simple inline helpers as macros, so that we don't
	 * depend on stdlib (memset/memchr) */
	#define FILL_PATTERN(ptr, size, pattern) do { \
		uint8_t *p = (uint8_t*)(ptr); \
		for (size_t i = 0; i < (size); i++) \
			p[i] = (pattern); \
		} while(0)

	#define CHECK_PATTERN(ptr, size, pattern) ({ \
		uint8_t *p = (uint8_t*)(ptr); \
		int ok = 1; \
		for (size_t i = 0; i < (size); i++) { \
			if (p[i] != (pattern)) { \
				ok = 0; \
				break; \
			} \
		} \
		ok; \
	})

	#define CHECK_ZERO(ptr, size) CHECK_PATTERN(ptr, size, 0)

	/* Test 1: Basic malloc/free */
	INF("Test 1: Basic malloc/free...\n");
	{
		void *p1 = malloc(100);
		void *p2 = malloc(200);
		void *p3 = malloc(300);

		if (!p1 || !p2 || !p3) {
			ERR("Basic malloc failed\n");
			failures++;
		} else if (p2 <= p1 || p3 <= p2) {
			ERR("Allocations not increasing\n");
			failures++;
		} else {
			/* Write different patterns to each allocation */
			FILL_PATTERN(p1, 100, 0xAA);
			FILL_PATTERN(p2, 200, 0xBB);
			FILL_PATTERN(p3, 300, 0xCC);

			/* Verify ALL bytes in each allocation */
			if (!CHECK_PATTERN(p1, 100, 0xAA)) {
				ERR("p1 corruption detected\n");
				failures++;
			}
			if (!CHECK_PATTERN(p2, 200, 0xBB)) {
				ERR("p2 corruption detected\n");
				failures++;
			}
			if (!CHECK_PATTERN(p3, 300, 0xCC)) {
				ERR("p3 corruption detected\n");
				failures++;
			}
		}

		/* Test free of last allocation */
		free(p3);
		void *p4 = malloc(100);
		if (p4 != p3) {
			ERR("Free didn't reclaim last allocation\n");
			failures++;
		}

		/* Test free of non-last (should be no-op) */
		free(p1);  // Should do nothing
		void *p5 = malloc(50);
		if (p5 == p1) {
			ERR("Free incorrectly freed non-last allocation\n");
			failures++;
		}
		free(p5);
	}

	/* Test 2: Calloc zeros memory */
	INF("Test 2: Calloc zeros memory...\n");
	{
		/* Test various sizes to ensure calloc always zeros */
		size_t test_sizes[][2] = {
			{10, 20},   // 200 bytes
			{1, 100},   // 100 bytes
			{50, 8},    // 400 bytes
			{7, 13},    // 91 bytes
		};

		for (size_t i = 0; i < sizeof(test_sizes) / sizeof(test_sizes[0]); i++) {
			size_t n = test_sizes[i][0];
			size_t size = test_sizes[i][1];
			size_t total = n * size;

			uint8_t *buf = calloc(n, size);
			if (!buf) {
				ERR("Calloc(%zu, %zu) failed\n", n, size);
				failures++;
				continue;
			}

			if (!CHECK_ZERO(buf, total)) {
				ERR("Calloc(%zu, %zu) didn't zero all %zu bytes\n", n, size, total);
				failures++;
			}
			free(buf);
		}
	}

	/* Test 3: Realloc grow/shrink */
	INF("Test 3: Realloc grow/shrink...\n");
	{
		uint8_t *buf = malloc(100);
		if (!buf) {
			ERR("Initial malloc failed\n");
			failures++;
		} else {
			/* Fill with sequential pattern */
			for (int i = 0; i < 100; i++)
				buf[i] = i & 0xFF;

			/* Grow to 200 */
			uint8_t *buf2 = realloc(buf, 200);
			if (buf2 != buf) {
				ERR("Realloc of last allocation moved it\n");
				failures++;
			} else {
				/* Verify ALL original data preserved */
				int corrupted = 0;
				for (int i = 0; i < 100; i++) {
					if (buf2[i] != (i & 0xFF)) {
						ERR("ealloc corrupted byte at offset %d\n", i);
						corrupted = 1;
						break;
					}
				}
				if (corrupted)
					failures++;
			}

			/* Shrink to 50, note that this will actually shrink to 56 since
			 * it'll align it up to 8 bytes (word size, the alignment boundary). */
			uint8_t *buf3 = realloc(buf2, 50);
			if (buf3 != buf2) {
				ERR("Realloc shrink moved allocation or failed\n");
				failures++;
			}

			/* Verify first 50 bytes still intact */
			for (int i = 0; i < 50; i++) {
				if (buf3[i] != (i & 0xFF)) {
					ERR("Realloc shrink corrupted byte %d\n", i);
					failures++;
					break;
				}
			}

			/* Grow back to 100 - the previously shrunk area may contain old data.
			 * The allocator doesn't clear freed/shrunk memory for performance. */
			uint8_t *buf4 = realloc(buf3, 100);
			if (buf4 != buf3) {
				ERR("Realloc re-grow moved allocation or failed\n");
				failures++;
			}

			/* Verify first 50 bytes are still intact after re-grow */
			for (int i = 0; i < 50; i++) {
				if (buf4[i] != (i & 0xFF)) {
					ERR("Realloc re-grow corrupted byte %d\n", i);
					failures++;
					break;
				}
			}
			free(buf4);
		}
	}

	/* Test 4: Edge cases */
	INF("Test 4: Edge cases...\n");
	{
		/* malloc(0) should return NULL */
		if (malloc(0) != NULL) {
			ERR("malloc(0) didn't return NULL\n");
			failures++;
		}

		/* free(NULL) should be safe */
		free(NULL);  // Should not crash

		/* realloc(NULL, size) should work like malloc */
		void *p = realloc(NULL, 100);
		if (!p) {
			ERR("realloc(NULL, 100) failed\n");
			failures++;
		}

		/* realloc(ptr, 0) should free and return NULL */
		void *p2 = realloc(p, 0);
		if (p2 != NULL) {
			ERR("realloc(ptr, 0) didn't return NULL\n");
			failures++;
		}
	}

	/* Test 5: Overflow protection */
	INF("Test 5: Overflow protection...\n");
	{
		/* Calloc overflow - should fail */
		void *p = calloc(SIZE_MAX/2, 3);
		if (p != NULL) {
			ERR("calloc overflow not detected\n");
			failures++;
			free(p);
		}

		/* reallocarray overflow on new allocation - should fail */
		p = reallocarray(NULL, SIZE_MAX/2, 3);
		if (p != NULL) {
			ERR("reallocarray(NULL) overflow not detected\n");
			failures++;
			free(p);
		}

		/* reallocarray overflow on existing allocation - should fail and preserve original */
		uint8_t *buf = malloc(100);
		if (!buf) {
			ERR("malloc for overflow test failed\n");
			failures++;
		} else {
			/* Fill with pattern to verify preservation */
			FILL_PATTERN(buf, 100, 0xDE);

			/* Try to overflow - should fail */
			uint8_t *new_buf = reallocarray(buf, SIZE_MAX/2, 3);
			if (new_buf != NULL) {
				ERR("reallocarray(existing) overflow not detected\n");
				failures++;
				free(new_buf);
			} else {
				/* Original buffer should still be valid and untouched */
				if (!CHECK_PATTERN(buf, 100, 0xDE)) {
					ERR("reallocarray overflow corrupted original buffer\n");
					failures++;
				}
				free(buf);
			}
		}
	}

	/* Test 6: Alignment */
	INF("Test 6: Alignment...\n");
	{
		/* Test various sizes for alignment */
		for (int size = 1; size <= 17; size++) {
			void *p = malloc(size);
			if (!p) {
				ERR("malloc(%d) failed\n", size);
				failures++;
			} else if ((uintptr_t)p % __SIZEOF_POINTER__ != 0) {
				ERR("malloc(%d) not aligned: %p\n", size, p);
				failures++;
			}
			free(p);
		}
	}

	/* Test 7: Pattern test with random sizes */
	INF("Test 7: Random allocation patterns...\n");
	{
		for (int iter = 0; iter < 20; iter++) {
			size_t size = 1 + ((uint16_t) rand() % 512);
			uint8_t pattern = ((uint8_t) rand() & 0xFF);

			/* Allocate and fill with pattern */
			uint8_t *buf = malloc(size);
			if (!buf) {
				ERR("malloc(%zu) failed\n", size);
				failures++;
				continue;
			}
			for (size_t i = 0; i < size; i++)
				buf[i] = pattern;

			/* Verify pattern */
			int corrupted = 0;
			for (size_t i = 0; i < size; i++) {
				if (buf[i] != pattern) {
					ERR("Corruption at byte %zu/%zu\n", i, size);
					corrupted = 1;
					break;
				}
			}
			if (corrupted)
				failures++;

			/* Try to grow it */
			size_t new_size = size + ((uint8_t) rand() % 256);
			uint8_t *new_buf = realloc(buf, new_size);
			if (new_buf == buf) {
				/* Successful in-place realloc, verify old data */
				for (size_t i = 0; i < size; i++) {
					if (new_buf[i] != pattern) {
						ERR("Realloc corrupted data\n");
						failures++;
						break;
					}
				}
				free(new_buf);
			} else if (new_buf == NULL) {
				/* Reallocation failed, free original buffer instead */
				ERR("realloc failed (size: %li)\n", new_size);
				free(buf);
			} else {
				/* This is unexpected */
				ERR("realloc returned a new allocation !\n");
				failures++;
			}
		}
	}

	/* Test 8: Alternating alloc/free pattern */
	INF("Test 8: Alternating alloc/free...\n");
	{
		void *last = NULL;
		for (int i = 0; i < 50; i++) {
			if (i % 2 == 0) {
				/* Allocate */
				size_t size = 10 + ((uint16_t)rand() % 512);
				last = malloc(size);
				if (!last) {
					ERR("Allocation %d failed\n", i);
					failures++;
				}
			} else {
				/* Free if it's the last allocation */
				free(last);
				void *test = malloc(10);
				if (test != last) {
					ERR("Free didn't work properly\n");
					failures++;
				}
				free(test);
			}
		}
	}

	/* Test 9: Reallocarray with existing pointer */
	INF("Test 9: Reallocarray with existing pointer...\n");
	{
		/* Allocate initial array with reallocarray */
		uint8_t *arr = reallocarray(NULL, 10, 10);  // 100 bytes
		if (!arr) {
			ERR("Initial reallocarray failed\n");
			failures++;
		} else {
			/* Verify it's zeroed (like calloc) */
			if (!CHECK_ZERO(arr, 100)) {
				ERR("reallocarray(NULL) didn't zero memory\n");
				failures++;
			}

			/* Fill with pattern */
			for (int i = 0; i < 100; i++)
				arr[i] = i & 0xFF;

			/* Grow the array */
			uint8_t *arr2 = reallocarray(arr, 20, 10);  // 200 bytes
			if (arr2 != arr) {
				ERR("reallocarray moved last allocation\n");
				failures++;
			} else {
				/* Verify original data preserved */
				int corrupted = 0;
				for (int i = 0; i < 100; i++) {
					if (arr2[i] != (i & 0xFF)) {
						ERR("reallocarray corrupted byte %d\n", i);
						corrupted = 1;
						break;
					}
				}
				if (corrupted)
					failures++;
			}

			/* Shrink the array */
			uint8_t *arr3 = reallocarray(arr2, 5, 10);  // 50 bytes
			if (arr3 != arr2) {
				ERR("reallocarray shrink moved allocation\n");
				failures++;
			}

			/* Verify first 50 bytes intact */
			for (int i = 0; i < 50; i++) {
				if (arr3[i] != (i & 0xFF)) {
					ERR("reallocarray shrink corrupted byte %d\n", i);
					failures++;
					break;
				}
			}

			/* Free with reallocarray(ptr, 0, size) - should free */
			uint8_t *arr4 = reallocarray(arr3, 0, 10);
			if (arr4 != NULL) {
				ERR("reallocarray(ptr, 0, size) didn't free\n");
				failures++;
				free(arr4);
			}
		}
	}

	INF("=== Allocator Test Results: %s (%d failures) ===\n",
		failures == 0 ? "PASS" : "FAIL", failures);

	#undef FILL_PATTERN
	#undef CHECK_PATTERN
	#undef CHECK_ZERO

	INF("Press a key to continue...\n");
	return failures;
}

REGISTER_YALIBC_TEST("Memory allocator tests", test_allocator);
