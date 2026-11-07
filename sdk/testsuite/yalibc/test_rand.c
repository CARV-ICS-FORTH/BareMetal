/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>			/* For typed ints */
#include <platform/utils/utils.h>	/* For ANN/INF/ERR */
#include <stdlib.h>			/* For rand/srand */
#include <test_framework.h>		/* For test registration macros */

#define SEQ_LEN		8
#define NUM_DRAWS	10000

static int
test_rand(unsigned flags)
{
	if (!flags)		/* probe: present, run on demand */
		return PROBE_INTERACTIVE;

	int failures = 0;

	ANN("\n---===Rand tests===---\n");

	/* Determinism: the same seed must produce the same sequence */
	printf("rand: deterministic sequence for a given seed\n");
	int seq_a[SEQ_LEN];
	srand(12345);
	for (int i = 0; i < SEQ_LEN; i++)
		seq_a[i] = rand();
	srand(12345);
	for (int i = 0; i < SEQ_LEN; i++) {
		int val = rand();
		if (seq_a[i] != val) {
			ERR("sequences differ at draw %i (%i vs %i)\n",
			    i, seq_a[i], val);
			failures++;
			break;
		}
	}

	/* Different seeds should produce different sequences */
	printf("rand: different seeds -> different sequences\n");
	srand(54321);
	int diffs = 0;
	for (int i = 0; i < SEQ_LEN; i++) {
		if (rand() != seq_a[i])
			diffs++;
	}
	if (!diffs) {
		ERR("got the same sequence from a different seed\n");
		failures++;
	}

	/* Range: [0, RAND_MAX] means non-negative, and with enough draws
	 * we should see values on both halves of the range. */
	printf("rand: range and rough distribution (x%i)\n", NUM_DRAWS);
	int high = 0;
	int low = 0;
	for (int i = 0; i < NUM_DRAWS; i++) {
		int val = rand();
		if (val < 0) {
			ERR("rand() returned negative value: %i\n", val);
			failures++;
			break;
		}
		if (val > RAND_MAX / 2)
			high++;
		else
			low++;
	}
	if (!high || !low) {
		ERR("suspicious distribution (low: %i, high: %i)\n", low, high);
		failures++;
	}

	INF("Press a key to continue...\n");
	return failures ? PROBE_FAILED : 0;
}

REGISTER_YALIBC("Rand tests", test_rand);
