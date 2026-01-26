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
test_string_search(void)
{
	int failures = 0;

	ANN("\n---===String Search Tests===---\n");

	/* Allocate test buffers */
	uint8_t *testbuff_a = malloc(TESTBUFF_LEN);
	if (testbuff_a == NULL) {
		ERR("Could not allocate testbuff_a\n");
		return -1;
	}

	/* ===== memchr ===== */
	INF("\n--- memchr tests ---\n");

	/* Basic search */
	printf("memchr: basic search\n");
	memcpy(testbuff_a, "hello world", 12);
	uint8_t *ptr = (uint8_t *)memchr(testbuff_a, 'w', 12);
	if (!ptr || (ptr - testbuff_a) != 6) {
		ERR("memchr 'w' in 'hello world' failed\n");
		failures++;
	}

	/* Not found */
	printf("memchr: char not found\n");
	ptr = (uint8_t *)memchr(testbuff_a, 'x', 12);
	if (ptr != NULL) {
		ERR("memchr should return NULL when char not found\n");
		failures++;
	}

	/* First occurrence */
	printf("memchr: first occurrence\n");
	memcpy(testbuff_a, "mississippi", 12);
	ptr = (uint8_t *)memchr(testbuff_a, 's', 12);
	if (!ptr || (ptr - testbuff_a) != 2) {
		ERR("memchr should find first 's' at position 2\n");
		failures++;
	}

	/* Null byte search */
	printf("memchr: null byte search\n");
	memcpy(testbuff_a, "hello", 6);
	ptr = (uint8_t *)memchr(testbuff_a, '\0', 6);
	if (!ptr || (ptr - testbuff_a) != 5) {
		ERR("memchr null byte search failed\n");
		failures++;
	}

	/* Search in binary data */
	printf("memchr: binary data (0xFF)\n");
	memset(testbuff_a, 0, 100);
	testbuff_a[50] = 0xFF;
	ptr = (uint8_t *)memchr(testbuff_a, 0xFF, 100);
	if (!ptr || (ptr - testbuff_a) != 50) {
		ERR("memchr binary search failed\n");
		failures++;
	}

	/* NULL pointer */
	printf("memchr: NULL pointer\n");
	ptr = (uint8_t *)memchr(NULL, 'a', 10);
	if (ptr != NULL) {
		ERR("memchr(NULL) should return NULL\n");
		failures++;
	}

	/* Zero length */
	printf("memchr: zero length\n");
	ptr = (uint8_t *)memchr(testbuff_a, 'a', 0);
	if (ptr != NULL) {
		ERR("memchr with len=0 should return NULL\n");
		failures++;
	}

	/* Random position test */
	printf("memchr: random position test (x100)\n");
	memset(testbuff_a, 0, TESTBUFF_LEN);
	for (int i = 0; i < 100; i++) {
		uint16_t random_pos = ((uint16_t)rand()) % TESTBUFF_LEN;
		uint8_t saved_val = testbuff_a[random_pos];
		testbuff_a[random_pos] = 0xFF;
		ptr = (uint8_t *)memchr(testbuff_a, 0xFF, TESTBUFF_LEN);
		if (!ptr || ((ptr - testbuff_a) != random_pos)) {
			ERR("memchr failed for pos: %d\n", random_pos);
			failures++;
		}
		testbuff_a[random_pos] = saved_val;
	}

	/* ===== strchr ===== */
	INF("\n--- strchr tests ---\n");

	/* Basic search */
	printf("strchr: basic search\n");
	char *str = "hello world";
	char *cptr = strchr(str, 'w');
	if (!cptr || cptr != str + 6) {
		ERR("strchr 'w' in 'hello world' failed\n");
		failures++;
	}

	/* Not found */
	printf("strchr: char not found\n");
	cptr = strchr(str, 'x');
	if (cptr != NULL) {
		ERR("strchr should return NULL when char not found\n");
		failures++;
	}

	/* Find null terminator */
	printf("strchr: find null terminator\n");
	cptr = strchr(str, '\0');
	if (!cptr || cptr != str + strlen(str)) {
		ERR("strchr should find null terminator\n");
		failures++;
	}

	/* First occurrence */
	printf("strchr: first occurrence\n");
	str = "mississippi";
	cptr = strchr(str, 's');
	if (!cptr || cptr != str + 2) {
		ERR("strchr should find first 's' at position 2\n");
		failures++;
	}

	/* Empty string */
	printf("strchr: empty string\n");
	cptr = strchr("", 'a');
	if (cptr != NULL) {
		ERR("strchr on empty string should return NULL\n");
		failures++;
	}

	/* Empty string with null search */
	printf("strchr: empty string, null search\n");
	cptr = strchr("", '\0');
	if (!cptr || *cptr != '\0') {
		ERR("strchr should find null in empty string\n");
		failures++;
	}

	/* NULL pointer */
	printf("strchr: NULL pointer\n");
	cptr = strchr(NULL, 'a');
	if (cptr != NULL) {
		ERR("strchr(NULL) should return NULL\n");
		failures++;
	}

	/* ===== strrchr ===== */
	INF("\n--- strrchr tests ---\n");

	/* Basic search */
	printf("strrchr: basic search\n");
	str = "hello world";
	cptr = strrchr(str, 'o');
	if (!cptr || cptr != str + 7) {
		ERR("strrchr 'o' should find last occurrence at position 7\n");
		failures++;
	}

	/* Not found */
	printf("strrchr: char not found\n");
	cptr = strrchr(str, 'x');
	if (cptr != NULL) {
		ERR("strrchr should return NULL when char not found\n");
		failures++;
	}

	/* Find null terminator */
	printf("strrchr: find null terminator\n");
	cptr = strrchr(str, '\0');
	if (!cptr || cptr != str + strlen(str)) {
		ERR("strrchr should find null terminator\n");
		failures++;
	}

	/* Last occurrence */
	printf("strrchr: last occurrence\n");
	str = "mississippi";
	cptr = strrchr(str, 's');
	if (!cptr || cptr != str + 6) {
		ERR("strrchr should find last 's' at position 6\n");
		failures++;
	}

	/* Empty string */
	printf("strrchr: empty string\n");
	cptr = strrchr("", 'a');
	if (cptr != NULL) {
		ERR("strrchr on empty string should return NULL\n");
		failures++;
	}

	/* ===== strlen ===== */
	INF("\n--- strlen tests ---\n");

	/* Basic */
	printf("strlen: basic\n");
	if (strlen("hello") != 5) {
		ERR("strlen(\"hello\") != 5\n");
		failures++;
	}

	/* Empty string */
	printf("strlen: empty string\n");
	if (strlen("") != 0) {
		ERR("strlen(\"\") != 0\n");
		failures++;
	}

	/* NULL */
	printf("strlen: NULL\n");
	if (strlen(NULL) != 0) {
		ERR("strlen(NULL) != 0\n");
		failures++;
	}

	/* Long string */
	printf("strlen: long string\n");
	char longstr[256];
	memset(longstr, 'a', 255);
	longstr[255] = '\0';
	if (strlen(longstr) != 255) {
		ERR("strlen(255 chars) != 255\n");
		failures++;
	}

	/* ===== strnlen ===== */
	INF("\n--- strnlen tests ---\n");

	/* Within bound */
	printf("strnlen: within bound\n");
	if (strnlen("hello", 10) != 5) {
		ERR("strnlen(\"hello\", 10) != 5\n");
		failures++;
	}

	/* Truncated */
	printf("strnlen: truncated\n");
	if (strnlen("hello", 3) != 3) {
		ERR("strnlen(\"hello\", 3) != 3\n");
		failures++;
	}

	/* Exact length */
	printf("strnlen: exact length\n");
	if (strnlen("hello", 5) != 5) {
		ERR("strnlen(\"hello\", 5) != 5\n");
		failures++;
	}

	/* maxlen=0 */
	printf("strnlen: maxlen=0\n");
	if (strnlen("hello", 0) != 0) {
		ERR("strnlen(\"hello\", 0) != 0\n");
		failures++;
	}

	/* Empty string */
	printf("strnlen: empty string\n");
	if (strnlen("", 10) != 0) {
		ERR("strnlen(\"\", 10) != 0\n");
		failures++;
	}

	/* ===== strspn ===== */
	INF("\n--- strspn tests ---\n");

	/* Basic */
	printf("strspn: basic\n");
	if (strspn("abcdef", "abc") != 3) {
		ERR("strspn(\"abcdef\", \"abc\") != 3\n");
		failures++;
	}

	/* No match */
	printf("strspn: no match\n");
	if (strspn("hello", "xyz") != 0) {
		ERR("strspn(\"hello\", \"xyz\") != 0\n");
		failures++;
	}

	/* Full match */
	printf("strspn: full match\n");
	if (strspn("aaa", "a") != 3) {
		ERR("strspn(\"aaa\", \"a\") != 3\n");
		failures++;
	}

	/* Empty accept */
	printf("strspn: empty accept\n");
	if (strspn("hello", "") != 0) {
		ERR("strspn(\"hello\", \"\") != 0\n");
		failures++;
	}

	/* Empty string */
	printf("strspn: empty string\n");
	if (strspn("", "abc") != 0) {
		ERR("strspn(\"\", \"abc\") != 0\n");
		failures++;
	}

	/* Single char accept */
	printf("strspn: single char accept\n");
	if (strspn("aaabbb", "a") != 3) {
		ERR("strspn(\"aaabbb\", \"a\") != 3\n");
		failures++;
	}

	/* ===== strcspn ===== */
	INF("\n--- strcspn tests ---\n");

	/* Basic */
	printf("strcspn: basic\n");
	if (strcspn("abcdef", "de") != 3) {
		ERR("strcspn(\"abcdef\", \"de\") != 3\n");
		failures++;
	}

	/* No match */
	printf("strcspn: no match (full length)\n");
	if (strcspn("hello", "xyz") != 5) {
		ERR("strcspn(\"hello\", \"xyz\") != 5\n");
		failures++;
	}

	/* First char matches */
	printf("strcspn: first char matches\n");
	if (strcspn("hello", "h") != 0) {
		ERR("strcspn(\"hello\", \"h\") != 0\n");
		failures++;
	}

	/* Empty reject */
	printf("strcspn: empty reject\n");
	if (strcspn("hello", "") != 5) {
		ERR("strcspn(\"hello\", \"\") != 5\n");
		failures++;
	}

	/* Empty string */
	printf("strcspn: empty string\n");
	if (strcspn("", "abc") != 0) {
		ERR("strcspn(\"\", \"abc\") != 0\n");
		failures++;
	}

	/* Single char reject */
	printf("strcspn: single char reject\n");
	if (strcspn("aaabbb", "b") != 3) {
		ERR("strcspn(\"aaabbb\", \"b\") != 3\n");
		failures++;
	}

	/* ===== strpbrk ===== */
	INF("\n--- strpbrk tests ---\n");

	/* Basic */
	printf("strpbrk: basic\n");
	cptr = strpbrk("hello world", "ow");
	if (!cptr || *cptr != 'o') {
		ERR("strpbrk(\"hello world\", \"ow\") should find 'o'\n");
		failures++;
	}

	/* Not found */
	printf("strpbrk: not found\n");
	cptr = strpbrk("hello", "xyz");
	if (cptr != NULL) {
		ERR("strpbrk should return NULL when not found\n");
		failures++;
	}

	/* First char */
	printf("strpbrk: first char\n");
	str = "hello";
	cptr = strpbrk(str, "h");
	if (!cptr || cptr != str) {
		ERR("strpbrk should find first char\n");
		failures++;
	}

	/* Empty accept */
	printf("strpbrk: empty accept\n");
	cptr = strpbrk("hello", "");
	if (cptr != NULL) {
		ERR("strpbrk with empty accept should return NULL\n");
		failures++;
	}

	/* Empty string */
	printf("strpbrk: empty string\n");
	cptr = strpbrk("", "abc");
	if (cptr != NULL) {
		ERR("strpbrk on empty string should return NULL\n");
		failures++;
	}

	/* ===== strtok ===== */
	INF("\n--- strtok tests ---\n");

	/* Basic tokenization */
	printf("strtok: basic tokenization\n");
	char tokbuf[64];
	strcpy(tokbuf, "hello,world,test");
	cptr = strtok(tokbuf, ",");
	if (!cptr || strcmp(cptr, "hello") != 0) {
		ERR("strtok first token should be 'hello'\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (!cptr || strcmp(cptr, "world") != 0) {
		ERR("strtok second token should be 'world'\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (!cptr || strcmp(cptr, "test") != 0) {
		ERR("strtok third token should be 'test'\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (cptr != NULL) {
		ERR("strtok should return NULL after last token\n");
		failures++;
	}

	/* Multiple delimiters */
	printf("strtok: multiple delimiters\n");
	strcpy(tokbuf, "  hello  world  ");
	cptr = strtok(tokbuf, " ");
	if (!cptr || strcmp(cptr, "hello") != 0) {
		ERR("strtok should skip leading delimiters\n");
		failures++;
	}
	cptr = strtok(NULL, " ");
	if (!cptr || strcmp(cptr, "world") != 0) {
		ERR("strtok should skip multiple delimiters\n");
		failures++;
	}
	cptr = strtok(NULL, " ");
	if (cptr != NULL) {
		ERR("strtok should return NULL after trailing delimiters\n");
		failures++;
	}

	/* Different delimiter sets */
	printf("strtok: different delimiter sets\n");
	strcpy(tokbuf, "one,two;three");
	cptr = strtok(tokbuf, ",");
	if (!cptr || strcmp(cptr, "one") != 0) {
		ERR("strtok first token failed\n");
		failures++;
	}
	cptr = strtok(NULL, ";");
	if (!cptr || strcmp(cptr, "two") != 0) {
		ERR("strtok with different delimiter failed\n");
		failures++;
	}

	/* Empty string */
	printf("strtok: empty string\n");
	strcpy(tokbuf, "");
	cptr = strtok(tokbuf, ",");
	if (cptr != NULL) {
		ERR("strtok on empty string should return NULL\n");
		failures++;
	}

	/* State reset - new string resets internal state */
	printf("strtok: state reset with new string\n");
	strcpy(tokbuf, "first,second");
	cptr = strtok(tokbuf, ",");
	if (!cptr || strcmp(cptr, "first") != 0) {
		ERR("strtok state reset: first token failed\n");
		failures++;
	}
	/* Now start a new tokenization without finishing the first */
	char tokbuf2[64];
	strcpy(tokbuf2, "new,tokens,here");
	cptr = strtok(tokbuf2, ",");
	if (!cptr || strcmp(cptr, "new") != 0) {
		ERR("strtok state reset: new string failed\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (!cptr || strcmp(cptr, "tokens") != 0) {
		ERR("strtok state reset: second token of new string failed\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (!cptr || strcmp(cptr, "here") != 0) {
		ERR("strtok state reset: third token of new string failed\n");
		failures++;
	}
	cptr = strtok(NULL, ",");
	if (cptr != NULL) {
		ERR("strtok state reset: should return NULL after new string exhausted\n");
		failures++;
	}

	/* Calling with NULL after complete exhaustion */
	printf("strtok: NULL after exhaustion\n");
	cptr = strtok(NULL, ",");
	if (cptr != NULL) {
		ERR("strtok(NULL) after exhaustion should return NULL\n");
		failures++;
	}

	/* ===== strstr ===== */
	INF("\n--- strstr tests ---\n");

	/* Basic search */
	printf("strstr: basic search\n");
	cptr = strstr("hello world", "world");
	if (!cptr || strcmp(cptr, "world") != 0) {
		ERR("strstr(\"hello world\", \"world\") failed\n");
		failures++;
	}

	/* Not found */
	printf("strstr: not found\n");
	cptr = strstr("hello world", "xyz");
	if (cptr != NULL) {
		ERR("strstr should return NULL when not found\n");
		failures++;
	}

	/* Empty needle */
	printf("strstr: empty needle\n");
	str = "hello";
	cptr = strstr(str, "");
	if (cptr != str) {
		ERR("strstr with empty needle should return haystack\n");
		failures++;
	}

	/* Single char needle */
	printf("strstr: single char needle\n");
	cptr = strstr("hello", "e");
	if (!cptr || *cptr != 'e') {
		ERR("strstr single char search failed\n");
		failures++;
	}

	/* Needle at start */
	printf("strstr: needle at start\n");
	str = "hello world";
	cptr = strstr(str, "hello");
	if (!cptr || cptr != str) {
		ERR("strstr needle at start failed\n");
		failures++;
	}

	/* Needle at end */
	printf("strstr: needle at end\n");
	cptr = strstr("hello world", "world");
	if (!cptr || strcmp(cptr, "world") != 0) {
		ERR("strstr needle at end failed\n");
		failures++;
	}

	/* Needle equals haystack */
	printf("strstr: needle equals haystack\n");
	cptr = strstr("test", "test");
	if (!cptr || strcmp(cptr, "test") != 0) {
		ERR("strstr needle equals haystack failed\n");
		failures++;
	}

	/* Needle longer than haystack */
	printf("strstr: needle longer than haystack\n");
	cptr = strstr("hi", "hello");
	if (cptr != NULL) {
		ERR("strstr with needle longer than haystack should return NULL\n");
		failures++;
	}

	/* Partial matches */
	printf("strstr: partial matches\n");
	cptr = strstr("aaaaaab", "aaab");
	if (!cptr || strcmp(cptr, "aaab") != 0) {
		ERR("strstr partial match handling failed\n");
		failures++;
	}

	/* Long needle (trigger two-way algorithm) */
	printf("strstr: long needle\n");
	char haystack[128];
	memset(haystack, 'a', 100);
	strcpy(haystack + 100, "needle");
	haystack[106] = '\0';
	cptr = strstr(haystack, "needle");
	if (!cptr || strcmp(cptr, "needle") != 0) {
		ERR("strstr long needle search failed\n");
		failures++;
	}

	/* Repetitive pattern */
	printf("strstr: repetitive pattern\n");
	cptr = strstr("ababababac", "ababac");
	if (!cptr || strcmp(cptr, "ababac") != 0) {
		ERR("strstr repetitive pattern failed\n");
		failures++;
	}

	/* NULL haystack */
	printf("strstr: NULL haystack\n");
	cptr = strstr(NULL, "test");
	if (cptr != NULL) {
		ERR("strstr(NULL, ...) should return NULL\n");
		failures++;
	}

	/* Stress test with random substrings */
	printf("strstr: stress test with random substrings (x100)\n");
	for (int i = 0; i < 100; i++) {
		/* a) Pick random length for haystack (at least 10 bytes, leave room for needle) */
		uint16_t haystack_len = 10 + ((uint16_t)rand() % (TESTBUFF_LEN / 2));

		/* b) Fill haystack with random bytes (avoid null bytes) */
		for (uint16_t j = 0; j < haystack_len; j++) {
			uint8_t byte;
			do {
				byte = (uint8_t)rand();
			} while (byte == 0);  /* Ensure no null bytes in haystack */
			testbuff_a[j] = byte;
		}
		testbuff_a[haystack_len] = '\0';

		/* c) Pick random length for needle (at least 1 byte, up to remaining space) */
		uint16_t max_needle_len = TESTBUFF_LEN - haystack_len - 1;
		uint16_t needle_len = 1 + ((uint16_t)rand() % max_needle_len);

		/* d) Find random substring inside haystack to use as needle */
		uint16_t substring_pos = (haystack_len > needle_len) ?
			(uint16_t)rand() % (haystack_len - needle_len + 1) : 0;

		/* e) Copy substring to needle position (after haystack) */
		char *needle_ptr = (char *)(testbuff_a + haystack_len + 1);
		memcpy(needle_ptr, testbuff_a + substring_pos, needle_len);
		needle_ptr[needle_len] = '\0';

		/* f) Try to find it using strstr */
		cptr = strstr((char *)testbuff_a, needle_ptr);
		if (!cptr) {
			ERR("strstr stress test %d: failed to find needle (haystack_len=%u, needle_len=%u, expected_pos=%u)\n",
			    i + 1, haystack_len, needle_len, substring_pos);
			failures++;
		} else {
			/* Verify the position is correct (should find first occurrence) */
			uint16_t found_pos = (uint16_t)(cptr - (char *)testbuff_a);
			if (found_pos > substring_pos) {
				ERR("strstr stress test %d: found at wrong position (found=%u, expected<=%u)\n",
				    i + 1, found_pos, substring_pos);
				failures++;
			}
			/* Verify the match is correct */
			if (memcmp(cptr, needle_ptr, needle_len) != 0) {
				ERR("strstr stress test %d: match content mismatch\n", i + 1);
				failures++;
			}
		}
	}

	free(testbuff_a);

	INF("=== String Search Test Results: %s (%d failures) ===\n",
	    failures == 0 ? "PASS" : "FAIL", failures);

	return failures;
}

REGISTER_YALIBC_TEST("String search tests", test_string_search);
