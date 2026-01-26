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
#include <string.h>			/* For mem*, str* etc */
#include <test_framework.h>		/* For test registration macros */

#define TESTBUFF_LEN 2048

static int
test_string_copy(void)
{
	int failures = 0;
	int ret = 0;

	ANN("\n---===String Copy Tests===---\n");

	/* Allocate test buffers */
	uint8_t *testbuff_a = malloc(TESTBUFF_LEN);
	if (testbuff_a == NULL) {
		ERR("Could not allocate testbuff_a\n");
		return -1;
	}

	uint8_t *testbuff_b = malloc(TESTBUFF_LEN);
	if (testbuff_b == NULL) {
		ERR("Could not allocate testbuff_b\n");
		free(testbuff_a);
		return -1;
	}

	/* ===== memcpy ===== */
	INF("\n--- memcpy tests ---\n");

	/* Basic copy */
	printf("memcpy: basic copy\n");
	memset(testbuff_a, 0, TESTBUFF_LEN);
	memset(testbuff_b, 0xAA, TESTBUFF_LEN);
	memcpy(testbuff_a, testbuff_b, 64);
	ret = memcmp(testbuff_a, testbuff_b, 64);
	if (ret != 0) {
		ERR("memcpy basic copy failed\n");
		failures++;
	}

	/* Aligned copy */
	printf("memcpy: aligned copy (256 bytes)\n");
	for (int i = 0; i < 256; i++)
		testbuff_b[i] = i & 0xFF;
	memcpy(testbuff_a, testbuff_b, 256);
	ret = memcmp(testbuff_a, testbuff_b, 256);
	if (ret != 0) {
		ERR("memcpy aligned copy failed\n");
		failures++;
	}

	/* Unaligned copy */
	printf("memcpy: unaligned copy\n");
	memcpy(testbuff_a + 1, testbuff_b + 1, 256);
	ret = memcmp(testbuff_a + 1, testbuff_b + 1, 256);
	if (ret != 0) {
		ERR("memcpy unaligned copy failed\n");
		failures++;
	}

	/* Small copy */
	printf("memcpy: small copy (< 2*WORD_SIZE)\n");
	memcpy(testbuff_a, testbuff_b, 6);
	ret = memcmp(testbuff_a, testbuff_b, 6);
	if (ret != 0) {
		ERR("memcpy small copy failed\n");
		failures++;
	}

	/* Large copy */
	printf("memcpy: large copy (TESTBUFF_LEN)\n");
	for (int i = 0; i < TESTBUFF_LEN; i++)
		testbuff_b[i] = (uint8_t)rand();
	memcpy(testbuff_a, testbuff_b, TESTBUFF_LEN);
	ret = memcmp(testbuff_a, testbuff_b, TESTBUFF_LEN);
	if (ret != 0) {
		ERR("memcpy large copy failed\n");
		failures++;
	}

	/* Return value */
	printf("memcpy: return value\n");
	void *retptr = memcpy(testbuff_a, testbuff_b, 10);
	if (retptr != testbuff_a) {
		ERR("memcpy should return dst pointer\n");
		failures++;
	}

	/* NULL src/dst */
	printf("memcpy: NULL pointers\n");
	retptr = memcpy(NULL, testbuff_b, 10);
	if (retptr != NULL) {
		ERR("memcpy(NULL, ...) should return NULL\n");
		failures++;
	}
	retptr = memcpy(testbuff_a, NULL, 10);
	if (retptr != testbuff_a) {
		ERR("memcpy(..., NULL, ...) should return dst\n");
		failures++;
	}

	/* Zero length */
	printf("memcpy: zero length\n");
	memset(testbuff_a, 0xFF, 10);
	memcpy(testbuff_a, testbuff_b, 0);
	if (testbuff_a[0] != 0xFF) {
		ERR("memcpy with len=0 should not modify buffer\n");
		failures++;
	}

	/* ===== memmove ===== */
	INF("\n--- memmove tests ---\n");

	/* Non-overlapping forward */
	printf("memmove: non-overlapping forward\n");
	memset(testbuff_a, 0, TESTBUFF_LEN);
	memset(testbuff_a, 0xAA, 100);
	memmove(testbuff_a + 200, testbuff_a, 100);
	for (int i = 0; i < 100; i++) {
		if (testbuff_a[200 + i] != 0xAA) {
			ERR("memmove non-overlapping failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Overlapping forward (dst > src) */
	printf("memmove: overlapping forward\n");
	memset(testbuff_a, 0, TESTBUFF_LEN);
	for (int i = 0; i < 100; i++)
		testbuff_a[i] = i & 0xFF;
	memmove(testbuff_a + 50, testbuff_a, 100);
	for (int i = 0; i < 100; i++) {
		if (testbuff_a[50 + i] != (i & 0xFF)) {
			ERR("memmove overlapping forward failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Overlapping backward (dst < src) */
	printf("memmove: overlapping backward\n");
	memset(testbuff_a, 0, TESTBUFF_LEN);
	for (int i = 0; i < 100; i++)
		testbuff_a[100 + i] = i & 0xFF;
	memmove(testbuff_a + 50, testbuff_a + 100, 100);
	for (int i = 0; i < 100; i++) {
		if (testbuff_a[50 + i] != (i & 0xFF)) {
			ERR("memmove overlapping backward failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Stress test overlapping regions */
	printf("memmove: stress test overlapping (x100)\n");
	#define OVERLAP_TEST_SIZE (TESTBUFF_LEN / 2)
	uint8_t *overlap_buf = testbuff_a;
	uint8_t *verify_buf = testbuff_a + OVERLAP_TEST_SIZE;

	for (int i = 0; i < 100; i++) {
		memset(overlap_buf, 0xFF, OVERLAP_TEST_SIZE);

		uint16_t copy_len = 1 + (uint8_t)rand();
		uint16_t max_offset = OVERLAP_TEST_SIZE - copy_len;
		uint16_t src_offset = (uint16_t)rand() % max_offset;
		uint16_t dst_offset;

		int overlap_type = (uint8_t)rand() % 5;
		switch (overlap_type) {
			case 0:
				dst_offset = src_offset + ((uint16_t)rand() % copy_len);
				break;
			case 1:
				if (src_offset >= copy_len)
					dst_offset = src_offset - ((uint16_t)rand() % copy_len);
				else
					dst_offset = 0;
				break;
			case 2:
				dst_offset = src_offset;
				break;
			case 3:
				dst_offset = src_offset + copy_len - 1;
				break;
			case 4:
				if (src_offset >= (copy_len + 1))
					dst_offset = src_offset - copy_len + 1;
				else
					dst_offset = 0;
				break;
		}

		if (dst_offset + copy_len > OVERLAP_TEST_SIZE)
			dst_offset = OVERLAP_TEST_SIZE - copy_len;

		memcpy(verify_buf, overlap_buf, OVERLAP_TEST_SIZE);
		if (dst_offset > src_offset) {
			for (int j = copy_len - 1; j >= 0; j--)
				verify_buf[dst_offset + j] = verify_buf[src_offset + j];
		} else {
			for (int j = 0; j < copy_len; j++)
				verify_buf[dst_offset + j] = verify_buf[src_offset + j];
		}

		memmove(overlap_buf + dst_offset, overlap_buf + src_offset, copy_len);

		int cmp_result = memcmp(overlap_buf, verify_buf, OVERLAP_TEST_SIZE);
		if (cmp_result != 0) {
			ERR("memmove stress test %d failed\n", i + 1);
			failures++;
		}
	}

	/* Same source and destination */
	printf("memmove: src == dst\n");
	memset(testbuff_a, 0xBB, 10);
	memmove(testbuff_a, testbuff_a, 10);
	if (testbuff_a[0] != 0xBB) {
		ERR("memmove with src==dst should be no-op\n");
		failures++;
	}

	/* ===== memccpy ===== */
	INF("\n--- memccpy tests ---\n");

	/* Basic copy with stop char */
	printf("memccpy: basic with stop char\n");
	memset(testbuff_a, 0, 100);
	memcpy(testbuff_b, "hello world", 12);
	retptr = memccpy(testbuff_a, testbuff_b, ' ', 12);
	if (!retptr || memcmp(testbuff_a, "hello ", 6) != 0) {
		ERR("memccpy basic copy failed\n");
		failures++;
	}
	if (retptr != testbuff_a + 6) {
		ERR("memccpy should return dst + bytes_copied\n");
		failures++;
	}

	/* Stop char not found */
	printf("memccpy: stop char not found\n");
	memset(testbuff_a, 0, 100);
	retptr = memccpy(testbuff_a, "hello", 'x', 5);
	if (retptr != NULL) {
		ERR("memccpy should return NULL when char not found\n");
		failures++;
	}
	if (memcmp(testbuff_a, "hello", 5) != 0) {
		ERR("memccpy should still copy all bytes when char not found\n");
		failures++;
	}

	/* Stop char at first position */
	printf("memccpy: stop char at first position\n");
	memset(testbuff_a, 0, 100);
	memcpy(testbuff_b, "xhello", 7);
	retptr = memccpy(testbuff_a, testbuff_b, 'x', 10);
	if (!retptr || retptr != testbuff_a + 1) {
		ERR("memccpy with char at first position failed\n");
		failures++;
	}

	/* ===== strcpy ===== */
	INF("\n--- strcpy tests ---\n");

	/* Basic copy */
	printf("strcpy: basic copy\n");
	char strbuf[64];
	char *sret = strcpy(strbuf, "hello");
	if (strcmp(strbuf, "hello") != 0) {
		ERR("strcpy basic copy failed\n");
		failures++;
	}
	if (sret != strbuf) {
		ERR("strcpy should return dst pointer\n");
		failures++;
	}

	/* Empty string */
	printf("strcpy: empty string\n");
	strcpy(strbuf, "");
	if (strbuf[0] != '\0') {
		ERR("strcpy empty string failed\n");
		failures++;
	}

	/* Overwrite longer string */
	printf("strcpy: overwrite longer string\n");
	strcpy(strbuf, "longlongstring");
	strcpy(strbuf, "short");
	if (strcmp(strbuf, "short") != 0) {
		ERR("strcpy overwrite failed\n");
		failures++;
	}

	/* ===== strncpy ===== */
	INF("\n--- strncpy tests ---\n");

	/* Basic copy */
	printf("strncpy: basic copy\n");
	memset(strbuf, 'X', sizeof(strbuf));
	strncpy(strbuf, "hello", sizeof(strbuf));
	if (strncmp(strbuf, "hello", sizeof(strbuf)) != 0) {
		ERR("strncpy basic copy failed\n");
		failures++;
	}

	/* Null padding */
	printf("strncpy: null padding\n");
	int pad_ok = 1;
	for (int i = 5; i < sizeof(strbuf); i++) {
		if (strbuf[i] != '\0') {
			pad_ok = 0;
			break;
		}
	}
	if (!pad_ok) {
		ERR("strncpy should pad with nulls\n");
		failures++;
	}

	/* Truncation (no null terminator) */
	printf("strncpy: truncation\n");
	memset(strbuf, 'X', sizeof(strbuf));
	strncpy(strbuf, "hello world", 5);
	if (memcmp(strbuf, "hello", 5) != 0 || strbuf[5] != 'X') {
		ERR("strncpy truncation failed\n");
		failures++;
	}

	/* Exact fit */
	printf("strncpy: exact fit\n");
	memset(strbuf, 'X', sizeof(strbuf));
	strncpy(strbuf, "hello", 5);
	if (memcmp(strbuf, "hello", 5) != 0 || strbuf[5] != 'X') {
		ERR("strncpy exact fit should not null-terminate\n");
		failures++;
	}

	/* n=0 */
	printf("strncpy: n=0\n");
	memset(strbuf, 'X', sizeof(strbuf));
	strncpy(strbuf, "hello", 0);
	if (strbuf[0] != 'X') {
		ERR("strncpy with n=0 should not modify buffer\n");
		failures++;
	}

	/* ===== strcat ===== */
	INF("\n--- strcat tests ---\n");

	/* Basic concatenation */
	printf("strcat: basic concatenation\n");
	strcpy(strbuf, "hello");
	strcat(strbuf, " world");
	if (strcmp(strbuf, "hello world") != 0) {
		ERR("strcat basic concatenation failed\n");
		failures++;
	}

	/* Append to empty string */
	printf("strcat: append to empty string\n");
	strcpy(strbuf, "");
	strcat(strbuf, "hello");
	if (strcmp(strbuf, "hello") != 0) {
		ERR("strcat to empty string failed\n");
		failures++;
	}

	/* Append empty string */
	printf("strcat: append empty string\n");
	strcpy(strbuf, "hello");
	strcat(strbuf, "");
	if (strcmp(strbuf, "hello") != 0) {
		ERR("strcat empty string should not modify\n");
		failures++;
	}

	/* Multiple concatenations */
	printf("strcat: multiple concatenations\n");
	strcpy(strbuf, "a");
	strcat(strbuf, "b");
	strcat(strbuf, "c");
	if (strcmp(strbuf, "abc") != 0) {
		ERR("strcat multiple concatenations failed\n");
		failures++;
	}

	/* ===== strncat ===== */
	INF("\n--- strncat tests ---\n");

	/* Basic concatenation */
	printf("strncat: basic concatenation\n");
	strcpy(strbuf, "hello");
	strncat(strbuf, " world", 6);
	if (strcmp(strbuf, "hello world") != 0) {
		ERR("strncat basic concatenation failed\n");
		failures++;
	}

	/* Truncation */
	printf("strncat: truncation\n");
	strcpy(strbuf, "hello");
	strncat(strbuf, " world", 3);
	if (strcmp(strbuf, "hello wo") != 0) {
		ERR("strncat truncation failed\n");
		failures++;
	}

	/* n=0 */
	printf("strncat: n=0\n");
	strcpy(strbuf, "hello");
	strncat(strbuf, " world", 0);
	if (strcmp(strbuf, "hello") != 0) {
		ERR("strncat with n=0 should not modify\n");
		failures++;
	}

	/* Null termination */
	printf("strncat: null termination\n");
	strcpy(strbuf, "hello");
	strncat(strbuf, "world", 10);
	if (strbuf[10] != '\0') {
		ERR("strncat should always null-terminate\n");
		failures++;
	}

	/* Random copy stress test */
	printf("memcpy/memmove: random copy stress test (x100)\n");
	for (int i = 0; i < 100; i++) {
		unsigned int direction = rand() % 2;
		uint16_t src_offset = (uint16_t)rand() % (TESTBUFF_LEN - 512);
		uint16_t dst_offset = (uint16_t)rand() % (TESTBUFF_LEN - 512);
		uint16_t copy_len = 1 + ((uint16_t)rand() % 512);

		for (int j = 0; j < copy_len; j++) {
			if (direction == 0) {
				testbuff_a[src_offset + j] = rand() & 0xFF;
				testbuff_b[dst_offset + j] = rand() & 0xFF;
			} else {
				testbuff_b[src_offset + j] = rand() & 0xFF;
				testbuff_a[dst_offset + j] = rand() & 0xFF;
			}
		}

		if (direction == 0) {
			memmove(testbuff_b + dst_offset, testbuff_a + src_offset, copy_len);
		} else {
			memmove(testbuff_a + dst_offset, testbuff_b + src_offset, copy_len);
		}

		int cmp_result;
		if (direction == 0) {
			cmp_result = memcmp(testbuff_a + src_offset, testbuff_b + dst_offset, copy_len);
		} else {
			cmp_result = memcmp(testbuff_b + src_offset, testbuff_a + dst_offset, copy_len);
		}

		if (cmp_result != 0) {
			ERR("Random copy test %d failed\n", i + 1);
			failures++;
		}
	}

	free(testbuff_a);
	free(testbuff_b);

	INF("=== String Copy Test Results: %s (%d failures) ===\n",
	    failures == 0 ? "PASS" : "FAIL", failures);

	return failures;
}

REGISTER_YALIBC_TEST("String copy tests", test_string_copy);
