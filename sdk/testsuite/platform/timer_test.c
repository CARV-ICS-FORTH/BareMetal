/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/utils/utils.h>	/* For console output */
#include <test_framework.h>		/* For test registration macros */

#include <time.h>	/* For struct timespec, CLOCK_*, nanosleep etc */
#include <limits.h>	/* For LONG_MAX */

/* Nanoseconds per second constant */
#define NSEC_PER_SEC 1000000000L

/**
 * timespecsub - Subtract two timespec structures (a - b = res)
 * Note: The result pointer can be the same as either input pointer,
 * allowing in-place operations like timespecsub(&ts, &delta, &ts).
 */
static void
timespecsub(struct timespec *a, struct timespec *b, struct timespec *res)
{
	time_t sec_diff;
	long nsec_diff;

	if (!a || !b || !res)
		return;

	/*
	 * Calculate the difference in seconds and nanoseconds separately.
	 * We need to be careful about the order of operations if res
	 * overlaps with a or b (in-place operation).
	 */
	sec_diff = a->tv_sec - b->tv_sec;
	nsec_diff = a->tv_nsec - b->tv_nsec;

	/*
	 * Handle nanosecond underflow.
	 * If we have a negative nanosecond difference, we need to
	 * borrow one second and adjust the nanoseconds accordingly.
	 *
	 * Example: 5.200000000 - 3.500000000
	 *   sec_diff = 2, nsec_diff = -300000000
	 *   After adjustment: 1.700000000 (borrow 1 sec = 1000000000 nsec)
	 */
	if (nsec_diff < 0) {
		sec_diff--;	/* Borrow one second */
		nsec_diff += NSEC_PER_SEC;	/* Add it to nanoseconds */
	}

	/*
	 * Store the result.
	 * Note: This works even if res == a or res == b because we've
	 * already computed both differences before modifying anything.
	 */
	res->tv_sec = sec_diff;
	res->tv_nsec = nsec_diff;

	/*
	 * Paranoid check: Ensure nanoseconds are in valid range.
	 */
	if (res->tv_nsec >= NSEC_PER_SEC) {
		res->tv_sec += res->tv_nsec / NSEC_PER_SEC;
		res->tv_nsec = res->tv_nsec % NSEC_PER_SEC;
	}
	/* Handle potential negative results consistently */
	else if (res->tv_nsec < 0 && res->tv_sec > 0) {
		res->tv_sec--;
		res->tv_nsec += NSEC_PER_SEC;
	}
}

static int
test_timer(void)
{
	ANN("\n---=== Timer Tests ===---\n");
	struct timespec ts_start = {0};
	struct timespec ts_end = {0};
	struct timespec ts_diff = {0};
	const struct timespec ts_delay = { .tv_nsec = 200 * 1000 * 1000}; // 200msec
	int ret = 0;

	/* Define acceptable range: 200ms ± 10% = 180-220ms */
	const long min_nsec = 180 * 1000 * 1000;  // 180ms
	const long max_nsec = 220 * 1000 * 1000;  // 220ms

	int failures = 0;
	long min_observed = LONG_MAX;
	long max_observed = 0;
	long total_nsec = 0;

	INF("Testing nanosleep with 200ms delay (tolerance: 180-220ms)\n");
	INF("Running 100 iterations...\n");

	for (int i = 0; i < 100; i++) {
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_start);
		nanosleep(&ts_delay, NULL);
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts_end);
		timespecsub(&ts_end, &ts_start, &ts_diff);

		/* Convert to total nanoseconds for easier comparison */
		long elapsed_ns = ts_diff.tv_sec * NSEC_PER_SEC + ts_diff.tv_nsec;

		/* Track min/max/average */
		if (elapsed_ns < min_observed) min_observed = elapsed_ns;
		if (elapsed_ns > max_observed) max_observed = elapsed_ns;
		total_nsec += elapsed_ns;

		/* Check if within acceptable range */
		if (elapsed_ns < min_nsec || elapsed_ns > max_nsec) {
			failures++;
			/* Only print first few failures to avoid spam */
			if (failures <= 3) {
				INF("  [FAIL] Iteration %d: %.3fms (out of range)\n", 
				       i, elapsed_ns / 1000000.0);
			}
		}

		/* Optional: print progress every 25 iterations */
		if ((i + 1) % 25 == 0) {
			INF("  Progress: %d/100 (failures so far: %d)\n", i + 1, failures);
		}
	}

	/* Print summary */
	INF("\nResults:\n");
	INF("  Min delay: %.3fms\n", min_observed / 1000000.0);
	INF("  Max delay: %.3fms\n", max_observed / 1000000.0);
	INF("  Avg delay: %.3fms\n", (total_nsec / 100) / 1000000.0);
	INF("  Failures: %d/100\n", failures);

	/* Consider test passed if >95% of iterations are within range */
	if (failures <= 5) {
		INF("=== Timer test: PASS (≤5%% failures acceptable) ===\n");
	} else {
		WRN("=== Timer test: FAIL (too many out-of-range delays) ===\n");
		ret = 1;
	}
	INF("Press a key to continue...\n");
	return ret;
}

REGISTER_PLATFORM_TEST("Timer nanosleep test", test_timer);