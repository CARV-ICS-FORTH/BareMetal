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

static int
test_string_fill(void)
{
	int failures = 0;

	ANN("\n---===String Fill Tests===---\n");

	/* Test buffer */
	uint8_t testbuff[512];

	/* ===== memset ===== */
	INF("\n--- memset tests ---\n");

	/* Basic fill */
	printf("memset: fill with 0xAA\n");
	memset(testbuff, 0xAA, 256);
	for (int i = 0; i < 256; i++) {
		if (testbuff[i] != 0xAA) {
			ERR("memset fill failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Zero fill */
	printf("memset: zero fill\n");
	memset(testbuff, 0, 256);
	for (int i = 0; i < 256; i++) {
		if (testbuff[i] != 0) {
			ERR("memset zero failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Unaligned start */
	printf("memset: unaligned start\n");
	memset(testbuff, 0xFF, 256);
	memset(testbuff + 3, 0x55, 100);
	if (testbuff[2] != 0xFF || testbuff[3] != 0x55 ||
	    testbuff[102] != 0x55 || testbuff[103] != 0xFF) {
		ERR("memset unaligned boundary error\n");
		failures++;
	}

	/* Small buffer (< 2*WORD_SIZE) */
	printf("memset: small buffer (< 2*WORD_SIZE)\n");
	memset(testbuff, 0xCC, 8);
	for (int i = 0; i < 8; i++) {
		if (testbuff[i] != 0xCC) {
			ERR("memset small buffer failed at %d\n", i);
			failures++;
			break;
		}
	}

	/* Single byte */
	printf("memset: single byte\n");
	memset(testbuff, 0xFF, 1);
	if (testbuff[0] != 0xFF) {
		ERR("memset single byte failed\n");
		failures++;
	}

	/* Large buffer */
	printf("memset: large buffer (512 bytes)\n");
	memset(testbuff, 0x12, 512);
	for (int i = 0; i < 512; i++) {
		if (testbuff[i] != 0x12) {
			ERR("memset large buffer failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Different patterns */
	printf("memset: various fill values\n");
	uint8_t patterns[] = {0x00, 0xFF, 0xAA, 0x55, 0x01, 0xFE};
	for (size_t p = 0; p < sizeof(patterns); p++) {
		memset(testbuff, patterns[p], 64);
		for (int i = 0; i < 64; i++) {
			if (testbuff[i] != patterns[p]) {
				ERR("memset pattern 0x%02x failed at byte %d\n",
				    patterns[p], i);
				failures++;
				break;
			}
		}
	}

	/* Return value check */
	printf("memset: return value\n");
	void *ret = memset(testbuff, 0x77, 10);
	if (ret != testbuff) {
		ERR("memset should return dst pointer\n");
		failures++;
	}

	/* NULL pointer (defensive check) */
	printf("memset: NULL pointer\n");
	ret = memset(NULL, 0, 10);
	if (ret != NULL) {
		ERR("memset(NULL) should return NULL\n");
		failures++;
	}

	/* Zero length */
	printf("memset: zero length\n");
	memset(testbuff, 0xFF, 10);
	memset(testbuff, 0xAA, 0);
	if (testbuff[0] != 0xFF) {
		ERR("memset with len=0 should not modify buffer\n");
		failures++;
	}

	/* ===== memset_explicit ===== */
	INF("\n--- memset_explicit tests ---\n");

	/* Basic functionality */
	printf("memset_explicit: basic fill\n");
	memset_explicit(testbuff, 0x99, 128);
	for (int i = 0; i < 128; i++) {
		if (testbuff[i] != 0x99) {
			ERR("memset_explicit failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Zero fill (security-sensitive use case) */
	printf("memset_explicit: zero fill (clearing secrets)\n");
	memset(testbuff, 0xDE, 256);
	memset_explicit(testbuff, 0, 256);
	for (int i = 0; i < 256; i++) {
		if (testbuff[i] != 0) {
			ERR("memset_explicit zero fill failed at byte %d\n", i);
			failures++;
			break;
		}
	}

	/* Return value */
	printf("memset_explicit: return value\n");
	ret = memset_explicit(testbuff, 0x42, 10);
	if (ret != testbuff) {
		ERR("memset_explicit should return dst pointer\n");
		failures++;
	}

	INF("=== String Fill Test Results: %s (%d failures) ===\n",
	    failures == 0 ? "PASS" : "FAIL", failures);

	return failures;
}

REGISTER_YALIBC_TEST("String fill tests", test_string_fill);
