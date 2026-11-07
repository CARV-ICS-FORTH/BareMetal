/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_HART_FREQ / PLAT_NO_MTIMER */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/interfaces/timer.h>	/* For timer_get_nsecs(), PLAT_TIMER_* */
#include <platform/riscv/mtimer.h>	/* For mtimer_get_num_ticks()/freq() */
#include <platform/riscv/hart.h>	/* For hart_get_counter(), HC_CYCLES */
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
	long sec_diff;
	long nsec_diff;

	if (!a || !b || !res)
		return;

	/*
	 * Calculate the difference in seconds and nanoseconds separately.
	 * We need to be careful about the order of operations if res
	 * overlaps with a or b (in-place operation).
	 */
	/* Cast to signed 64-bit before subtracting. tv_nsec is uint32_t, so a
	 * bare a->tv_nsec - b->tv_nsec is evaluated in unsigned int: a sub-second
	 * borrow (end tv_nsec < start tv_nsec when the interval crosses a 1s
	 * boundary) would wrap to ~+2^32 instead of going negative, defeating the
	 * borrow handling below and inflating the result by exactly 2^32 ns. */
	sec_diff = (long)a->tv_sec - (long)b->tv_sec;
	nsec_diff = (long)a->tv_nsec - (long)b->tv_nsec;

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

/*
 * Nanosleep mechanism test.
 *
 * Sleep a fixed interval and measure it with CLOCK_MONOTONIC - the same
 * MTIMER that drives the sleep. That makes this a test of the sleep
 * *mechanism* (arm mtimecmp -> wfi -> timer IRQ -> wake -> disarm), not of
 * the core-clock frequency: it is independent of PLAT_HART_FREQ and free of
 * the "cycle counter during wfi" artifact. It catches real bugs - a timer
 * IRQ that never fires, a broken wfi loop, early return, a stuck/re-arming
 * timer, or conversion overflow. Real-time *accuracy* is the crystal's job
 * and is cross-checked separately below.
 */
static int
test_nanosleep(unsigned flags)
{
	if (!flags)
		return PROBE_INTERACTIVE;

	ANN("\n---=== Nanosleep Mechanism Test ===---\n");
	const struct timespec ts_delay = { .tv_nsec = 200 * 1000 * 1000 }; /* 200ms */
	const long min_nsec = 180 * 1000 * 1000;	/* 200ms -10% */
	const long max_nsec = 220 * 1000 * 1000;	/* 200ms +10% */

	int failures = 0;
	long min_observed = LONG_MAX, max_observed = 0, total_nsec = 0;

	INF("Sleeping 200ms x 20, measured via CLOCK_MONOTONIC (tolerance 180-220ms)\n");

	for (int i = 0; i < 20; i++) {
		struct timespec ts_start, ts_end, ts_diff;
		clock_gettime(CLOCK_MONOTONIC, &ts_start);
		nanosleep(&ts_delay, NULL);
		clock_gettime(CLOCK_MONOTONIC, &ts_end);
		timespecsub(&ts_end, &ts_start, &ts_diff);

		long elapsed_ns = ts_diff.tv_sec * NSEC_PER_SEC + ts_diff.tv_nsec;
		if (elapsed_ns < min_observed) min_observed = elapsed_ns;
		if (elapsed_ns > max_observed) max_observed = elapsed_ns;
		total_nsec += elapsed_ns;

		if (elapsed_ns < min_nsec || elapsed_ns > max_nsec) {
			failures++;
			if (failures <= 3)
				INF("  [FAIL] iteration %d: %.3fms (out of range)\n",
				    i, elapsed_ns / 1000000.0);
		}
	}

	INF("  min %.3fms, max %.3fms, avg %.3fms, failures %d/20\n",
	    min_observed / 1000000.0, max_observed / 1000000.0,
	    (total_nsec / 20) / 1000000.0, failures);

	int ret = 0;
	if (failures <= 1) {
		INF("=== Nanosleep mechanism: PASS ===\n");
	} else {
		WRN("=== Nanosleep mechanism: FAIL ===\n");
		ret = 1;
	}
	INF("Press a key to continue...\n");
	return ret ? PROBE_FAILED : 0;
}
REGISTER_PLATFORM("Nanosleep mechanism test", test_nanosleep);

/*
 * Clock cross-check: MTIMER (mtime) vs the cycle counter (mcycle).
 *
 * The two monotonic sources must agree on how long an interval is, so code
 * can switch between them safely (e.g. a build that disables MTIMER and falls
 * back to the cycle counter). They sit in different clock domains - mtime is
 * a fixed, DVFS-independent crystal/divider; mcycle tracks the (DVFS-variable)
 * core clock - so the only way to relate them is to measure one against the
 * other. We busy-wait (never wfi) so the core stays at full clock and mcycle
 * is a clean reference for the whole run.
 *
 * Note: a single measurement is just the ratio f_cycle/f_time; it cannot by
 * itself say which configured frequency is wrong - that needs a trusted
 * reference. mtime is that reference (fixed, DVFS-independent), so any
 * mismatch is attributed to mcycle: either a wrong PLAT_HART_FREQ, or DVFS
 * having moved the core off its expected operating point. Running several
 * windows also tells us if the cycle counter is *stable* (steady ratio) or
 * being scaled underneath us (varying ratio).
 */
static int
test_clock_crosscheck(unsigned flags)
{
	if (!flags)
		return PROBE_INTERACTIVE;

	ANN("\n---=== Clock Cross-check (MTIMER vs cycle counter) ===---\n");
#ifdef PLAT_NO_MTIMER
	WRN("No MTIMER present; the cycle counter is the only time source - "
	    "nothing to cross-check\n");
	INF("Press a key to continue...\n");
	return 0;
#else
	const uint32_t mtfreq = mtimer_get_freq();
	const uint64_t cfg_cpu = (uint64_t)PLAT_HART_FREQ;
	const int nwin = 5;
	uint64_t win = mtfreq / 50;		/* ~20ms of mtime ticks per window */
	if (!win)
		win = 1;

	hart_enable_counter(HC_CYCLES);

	INF("Reference: MTIMER %lu Hz (fixed); %d windows of ~%lums\n",
	    (unsigned long)mtfreq, nwin, (unsigned long)(win * 1000 / mtfreq));

	uint64_t fmin = (uint64_t)-1, fmax = 0, fsum = 0;
	uint64_t mono_ns_total = 0, cpu_ns_total = 0;

	for (int i = 0; i < nwin; i++) {
		/* Align to an mtime edge, then latch both counters. */
		uint64_t t0 = mtimer_get_num_ticks(), t;
		do { t = mtimer_get_num_ticks(); } while (t == t0);

		uint64_t c_start = hart_get_counter(HC_CYCLES);
		uint64_t mono_start = timer_get_nsecs(PLAT_TIMER_MTIMER);
		uint64_t cpu_start = timer_get_nsecs(PLAT_TIMER_CYCLES);

		uint64_t tend = t + win, t1;
		do { t1 = mtimer_get_num_ticks(); } while (t1 < tend);

		uint64_t c_end = hart_get_counter(HC_CYCLES);
		uint64_t mono_end = timer_get_nsecs(PLAT_TIMER_MTIMER);
		uint64_t cpu_end = timer_get_nsecs(PLAT_TIMER_CYCLES);

		/* Cycle-counter frequency this window, timed by the fixed mtime. */
		uint64_t fcyc = ((c_end - c_start) * (uint64_t)mtfreq) / (t1 - t);
		if (fcyc < fmin) fmin = fcyc;
		if (fcyc > fmax) fmax = fcyc;
		fsum += fcyc;

		mono_ns_total += mono_end - mono_start;
		cpu_ns_total  += cpu_end - cpu_start;

		INF("  window %d: cycle freq %lu Hz (%.3f MHz)\n",
		    i, (unsigned long)fcyc, fcyc / 1e6);
	}

	uint64_t favg = fsum / nwin;
	double spread = favg ? 100.0 * (double)(fmax - fmin) / (double)favg : 0.0;

	/* --- 1) Stability of the cycle counter --- */
	INF("\nCycle counter: avg %lu Hz (%.3f MHz), window spread %.2f%%\n",
	    (unsigned long)favg, favg / 1e6, spread);
	if (spread > 1.0)
		WRN("  -> not stable across windows (DVFS / frequency scaling active?)\n");
	else
		INF("  -> stable across windows\n");

	/* --- 2) Cross-check the configured frequencies against the measured
	 * ratio. Trusting mtime, a deviation is the cycle counter's. The second
	 * line is the reciprocal view the measurement equally supports. --- */
	double cpu_dev = 100.0 * ((double)favg - (double)cfg_cpu) / (double)cfg_cpu;
	uint64_t ftime_implied = favg ?
		(uint64_t)(((double)cfg_cpu * (double)mtfreq) / (double)favg) : 0;
	double time_dev = 100.0 * ((double)ftime_implied - (double)mtfreq) / (double)mtfreq;

	INF("PLAT_HART_FREQ  : cfg %lu Hz, measured %lu Hz (%.2f%%)\n",
	    (unsigned long)cfg_cpu, (unsigned long)favg, cpu_dev);
	INF("PLAT_MTIMER_FREQ: cfg %lu Hz, implied  %lu Hz (%.2f%%)\n",
	    (unsigned long)mtfreq, (unsigned long)ftime_implied, time_dev);

	/* --- 3) Agreement via the public clock API: do CLOCK_MONOTONIC and
	 * CLOCK_PROCESS_CPUTIME_ID report the same elapsed time? This is the
	 * property that lets software use them interchangeably. --- */
	double agree_dev = mono_ns_total ?
		100.0 * ((double)cpu_ns_total - (double)mono_ns_total) /
		(double)mono_ns_total : 0.0;
	INF("Agreement: MONOTONIC %lums vs CPUTIME %lums (%.2f%%)\n",
	    (unsigned long)(mono_ns_total / 1000000),
	    (unsigned long)(cpu_ns_total / 1000000), agree_dev);

	int ret = 0;
	double aerr = agree_dev < 0 ? -agree_dev : agree_dev;
	if (aerr <= 2.0) {
		INF("=== Clocks agree within 2%% - safe to interchange: PASS ===\n");
	} else {
		WRN("=== Clocks disagree (>2%%); assuming the fixed mtime, "
		    "check PLAT_HART_FREQ / DVFS: FAIL ===\n");
		ret = 1;
	}
	INF("Press a key to continue...\n");
	return ret ? PROBE_FAILED : 0;
#endif
}
REGISTER_PLATFORM("Clock cross-check (timer vs cycles)", test_clock_crosscheck);