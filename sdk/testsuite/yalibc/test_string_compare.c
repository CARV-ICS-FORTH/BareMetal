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
test_string_compare(void)
{
	int failures = 0;
	int ret = 0;

	ANN("\n---===String Compare Tests===---\n");

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

	/* ===== memcmp ===== */
	INF("\n--- memcmp tests ---\n");

	/* Equal buffers */
	printf("memcmp: equal buffers\n");
	memset(testbuff_a, 0xAA, 256);
	memset(testbuff_b, 0xAA, 256);
	ret = memcmp(testbuff_a, testbuff_b, 256);
	if (ret != 0) {
		ERR("memcmp equal buffers should return 0\n");
		failures++;
	}

	/* a < b */
	printf("memcmp: a < b\n");
	uint8_t cmp_a[8] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
	uint8_t cmp_b[8] = {0x10, 0x20, 0x30, 0x41, 0x50, 0x60, 0x70, 0x80};
	ret = memcmp(cmp_a, cmp_b, 8);
	if (ret >= 0) {
		ERR("memcmp 0x40 < 0x41: got %d, expected negative\n", ret);
		failures++;
	}

	/* a > b */
	printf("memcmp: a > b\n");
	cmp_a[3] = 0x42;
	ret = memcmp(cmp_a, cmp_b, 8);
	if (ret <= 0) {
		ERR("memcmp 0x42 > 0x41: got %d, expected positive\n", ret);
		failures++;
	}

	/* Difference at byte 0 (word-aligned) */
	printf("memcmp: difference at byte 0 (aligned)\n");
	uint8_t aligned_a[16] __attribute__((aligned(8))) = {0};
	uint8_t aligned_b[16] __attribute__((aligned(8))) = {0};
	aligned_a[0] = 0x01;
	aligned_b[0] = 0x02;
	ret = memcmp(aligned_a, aligned_b, 16);
	if (ret >= 0) {
		ERR("memcmp aligned byte 0: got %d, expected negative\n", ret);
		failures++;
	}

	/* Difference at byte 7 (word-aligned) */
	printf("memcmp: difference at byte 7 (aligned)\n");
	memset(aligned_a, 0, 16);
	memset(aligned_b, 0, 16);
	aligned_a[7] = 0xFF;
	aligned_b[7] = 0x00;
	ret = memcmp(aligned_a, aligned_b, 16);
	if (ret <= 0) {
		ERR("memcmp aligned byte 7: got %d, expected positive\n", ret);
		failures++;
	}

	/* Unaligned buffers */
	printf("memcmp: unaligned buffers\n");
	for (int i = 0; i < TESTBUFF_LEN; i++) {
		testbuff_a[i] = (uint8_t)rand();
		testbuff_b[i] = (uint8_t)rand();
	}
	ret = memcmp(testbuff_a, testbuff_b, TESTBUFF_LEN);
	if (ret == 0) {
		ERR("memcmp random buffers should differ\n");
		failures++;
	}

	printf("memcmp: unaligned buffers from offset 1\n");
	ret = memcmp(testbuff_a + 1, testbuff_b + 1, TESTBUFF_LEN - 1);
	if (ret == 0) {
		ERR("memcmp random buffers (offset) should differ\n");
		failures++;
	}

	/* Copy and verify equal */
	printf("memcmp: after copy\n");
	memcpy(testbuff_a, testbuff_b, 256);
	ret = memcmp(testbuff_a, testbuff_b, 256);
	if (ret != 0) {
		ERR("memcmp after copy should be equal\n");
		failures++;
	}

	/* Single byte difference */
	printf("memcmp: single byte difference (position 50)\n");
	testbuff_a[50] = 0xAA;
	testbuff_b[50] = 0xBB;
	ret = memcmp(testbuff_a, testbuff_b, 256);
	if (ret >= 0) {
		ERR("memcmp with byte difference should be negative\n");
		failures++;
	}

	/* Same pointer */
	printf("memcmp: same pointer\n");
	ret = memcmp(testbuff_a, testbuff_a, 256);
	if (ret != 0) {
		ERR("memcmp with same pointer should return 0\n");
		failures++;
	}

	/* NULL pointers */
	printf("memcmp: NULL pointers\n");
	ret = memcmp(NULL, testbuff_b, 10);
	if (ret != 0) {
		ERR("memcmp(NULL, ...) should return 0\n");
		failures++;
	}
	ret = memcmp(testbuff_a, NULL, 10);
	if (ret != 0) {
		ERR("memcmp(..., NULL) should return 0\n");
		failures++;
	}

	/* Zero length */
	printf("memcmp: zero length\n");
	testbuff_a[0] = 0x00;
	testbuff_b[0] = 0xFF;
	ret = memcmp(testbuff_a, testbuff_b, 0);
	if (ret != 0) {
		ERR("memcmp with len=0 should return 0\n");
		failures++;
	}

	/* Large buffer comparison */
	printf("memcmp: large buffer (TESTBUFF_LEN)\n");
	memcpy(testbuff_a, testbuff_b, TESTBUFF_LEN);
	ret = memcmp(testbuff_a, testbuff_b, TESTBUFF_LEN);
	if (ret != 0) {
		ERR("memcmp large equal buffers failed\n");
		failures++;
	}

	/* Difference at end */
	printf("memcmp: difference at end\n");
	testbuff_a[TESTBUFF_LEN - 1] = 0x00;
	testbuff_b[TESTBUFF_LEN - 1] = 0xFF;
	ret = memcmp(testbuff_a, testbuff_b, TESTBUFF_LEN);
	if (ret >= 0) {
		ERR("memcmp with difference at end should be negative\n");
		failures++;
	}

	/* ===== strcmp ===== */
	INF("\n--- strcmp tests ---\n");

	/* Equal strings */
	printf("strcmp: equal strings\n");
	ret = strcmp("hello", "hello");
	if (ret != 0) {
		ERR("strcmp equal strings should return 0\n");
		failures++;
	}

	/* a < b */
	printf("strcmp: a < b\n");
	ret = strcmp("abc", "abd");
	if (ret >= 0) {
		ERR("strcmp 'abc' < 'abd': got %d, expected negative\n", ret);
		failures++;
	}

	/* a > b */
	printf("strcmp: a > b\n");
	ret = strcmp("abd", "abc");
	if (ret <= 0) {
		ERR("strcmp 'abd' > 'abc': got %d, expected positive\n", ret);
		failures++;
	}

	/* Different lengths */
	printf("strcmp: different lengths (s1 shorter)\n");
	ret = strcmp("abc", "abcd");
	if (ret >= 0) {
		ERR("strcmp shorter s1: got %d, expected negative\n", ret);
		failures++;
	}

	printf("strcmp: different lengths (s2 shorter)\n");
	ret = strcmp("abcd", "abc");
	if (ret <= 0) {
		ERR("strcmp shorter s2: got %d, expected positive\n", ret);
		failures++;
	}

	/* Empty strings */
	printf("strcmp: both empty\n");
	ret = strcmp("", "");
	if (ret != 0) {
		ERR("strcmp empty strings should be equal\n");
		failures++;
	}

	printf("strcmp: s1 empty\n");
	ret = strcmp("", "hello");
	if (ret >= 0) {
		ERR("strcmp empty s1: got %d, expected negative\n", ret);
		failures++;
	}

	printf("strcmp: s2 empty\n");
	ret = strcmp("hello", "");
	if (ret <= 0) {
		ERR("strcmp empty s2: got %d, expected positive\n", ret);
		failures++;
	}

	/* NULL pointers */
	printf("strcmp: NULL pointers\n");
	ret = strcmp(NULL, "test");
	if (ret != 0) {
		ERR("strcmp(NULL, ...) should return 0\n");
		failures++;
	}
	ret = strcmp("test", NULL);
	if (ret != 0) {
		ERR("strcmp(..., NULL) should return 0\n");
		failures++;
	}
	ret = strcmp(NULL, NULL);
	if (ret != 0) {
		ERR("strcmp(NULL, NULL) should return 0\n");
		failures++;
	}

	/* Same pointer */
	printf("strcmp: same pointer\n");
	char *same = "test";
	ret = strcmp(same, same);
	if (ret != 0) {
		ERR("strcmp same pointer should return 0\n");
		failures++;
	}

	/* Case sensitivity */
	printf("strcmp: case sensitivity\n");
	ret = strcmp("Hello", "hello");
	if (ret == 0) {
		ERR("strcmp should be case-sensitive\n");
		failures++;
	}

	/* Unsigned char comparison (chars should be treated as unsigned) */
	printf("strcmp: unsigned char comparison\n");
	/* 0x80 (128) as unsigned char should be > 0x7F (127)
	 * If chars are treated as signed, 0x80 would be -128 which is < 127 */
	char str_unsigned_a[2] = {0x7F, '\0'};
	char str_unsigned_b[2] = {0x80, '\0'};
	ret = strcmp(str_unsigned_a, str_unsigned_b);
	if (ret >= 0) {
		ERR("strcmp should treat chars as unsigned (0x7F < 0x80): got %d, expected negative\n", ret);
		failures++;
	}
	/* Another test: 0xFF should be > 0x01 */
	str_unsigned_a[0] = 0x01;
	str_unsigned_b[0] = 0xFF;
	ret = strcmp(str_unsigned_a, str_unsigned_b);
	if (ret >= 0) {
		ERR("strcmp should treat chars as unsigned (0x01 < 0xFF): got %d, expected negative\n", ret);
		failures++;
	}

	/* ===== strncmp ===== */
	INF("\n--- strncmp tests ---\n");

	/* Equal strings */
	printf("strncmp: equal strings\n");
	ret = strncmp("hello", "hello", 10);
	if (ret != 0) {
		ERR("strncmp equal strings should return 0\n");
		failures++;
	}

	/* Prefix match with len limit */
	printf("strncmp: prefix match\n");
	ret = strncmp("hello", "helicopter", 3);
	if (ret != 0) {
		ERR("strncmp prefix match failed\n");
		failures++;
	}

	/* Difference within n */
	printf("strncmp: difference within n\n");
	ret = strncmp("hello", "help", 10);
	if (ret == 0) {
		ERR("strncmp should detect difference\n");
		failures++;
	}

	/* Difference beyond n */
	printf("strncmp: difference beyond n\n");
	ret = strncmp("hello", "help", 3);
	if (ret != 0) {
		ERR("strncmp should ignore difference beyond n\n");
		failures++;
	}

	/* s1 shorter */
	printf("strncmp: s1 shorter\n");
	ret = strncmp("abc", "abcd", 10);
	if (ret >= 0) {
		ERR("strncmp shorter s1: got %d, expected negative\n", ret);
		failures++;
	}

	/* s2 shorter */
	printf("strncmp: s2 shorter\n");
	ret = strncmp("abcd", "abc", 10);
	if (ret <= 0) {
		ERR("strncmp shorter s2: got %d, expected positive\n", ret);
		failures++;
	}

	/* n=0 (always equal) */
	printf("strncmp: n=0\n");
	ret = strncmp("abc", "xyz", 0);
	if (ret != 0) {
		ERR("strncmp with n=0 should return 0\n");
		failures++;
	}

	/* Empty strings */
	printf("strncmp: empty strings\n");
	ret = strncmp("", "", 10);
	if (ret != 0) {
		ERR("strncmp empty strings should be equal\n");
		failures++;
	}

	printf("strncmp: empty vs non-empty\n");
	ret = strncmp("", "a", 10);
	if (ret >= 0) {
		ERR("strncmp empty vs non-empty: got %d, expected negative\n", ret);
		failures++;
	}

	/* NULL pointers */
	printf("strncmp: NULL pointers\n");
	ret = strncmp(NULL, "test", 10);
	if (ret != 0) {
		ERR("strncmp(NULL, ...) should return 0\n");
		failures++;
	}

	/* Same pointer */
	printf("strncmp: same pointer\n");
	ret = strncmp(same, same, 10);
	if (ret != 0) {
		ERR("strncmp same pointer should return 0\n");
		failures++;
	}

	/* Stop at null within n */
	printf("strncmp: stop at null\n");
	ret = strncmp("abc", "abc", 100);
	if (ret != 0) {
		ERR("strncmp should stop at null even with large n\n");
		failures++;
	}

	/* Exact n match */
	printf("strncmp: exact n match\n");
	ret = strncmp("abcXYZ", "abcDEF", 3);
	if (ret != 0) {
		ERR("strncmp should match first 3 chars\n");
		failures++;
	}

	/* Comprehensive comparison test */
	printf("memcmp/strcmp: comprehensive test\n");
	for (int i = 0; i < TESTBUFF_LEN; i++) {
		testbuff_a[i] = (uint8_t)rand();
		testbuff_b[i] = (uint8_t)rand();
	}

	ret = memcmp(testbuff_a, testbuff_b, TESTBUFF_LEN);
	if (ret == 0) {
		ERR("Random buffers should differ\n");
		failures++;
	}

	memcpy(testbuff_a, testbuff_b, 6);
	ret = memcmp(testbuff_a, testbuff_b, 6);
	if (ret != 0) {
		ERR("After copy should be equal\n");
		failures++;
	}

	testbuff_b[4] = testbuff_b[4] + 1;
	ret = memcmp(testbuff_a, testbuff_b, 6);
	if (ret != -1) {
		ERR("Single byte increment should give -1: got %d\n", ret);
		failures++;
	}

	testbuff_a[3] = testbuff_a[3] + 1;
	ret = memcmp(testbuff_a, testbuff_b, 6);
	if (ret != 1) {
		ERR("Earlier byte increment should give 1: got %d\n", ret);
		failures++;
	}

	free(testbuff_a);
	free(testbuff_b);

	INF("=== String Compare Test Results: %s (%d failures) ===\n",
	    failures == 0 ? "PASS" : "FAIL", failures);

	return failures;
}

REGISTER_YALIBC_TEST("String compare tests", test_string_compare);
