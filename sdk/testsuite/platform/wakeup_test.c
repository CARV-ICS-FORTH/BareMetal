/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_MAX_HARTS / PLAT_NO_IPI */

/* No IPIs -> no way to wake anyone up */
#if !defined(PLAT_NO_IPI)

#include <stdint.h>			/* For typed ints */
#include <stdatomic.h>			/* For C11 atomics */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/csr.h>		/* For wfi()/pause() */
#include <platform/riscv/hart.h>	/* For hart_wakeup_with_addr() etc */
#include <platform/riscv/mtimer.h>	/* For mtimer_get_num_ticks()/freq() */
#include <test_framework.h>		/* For test registration macros */

/* What each woken hart observed as its arguments */
static volatile uint64_t observed_arg0[PLAT_MAX_HARTS];
static volatile uint64_t observed_arg1[PLAT_MAX_HARTS];
static atomic_int harts_done = 0;

static void
wakeup_test_entry(uint64_t arg0, uint64_t arg1)
{
	struct hart_state *hs = hart_get_hstate_self();
	observed_arg0[hs->hart_idx] = arg0;
	observed_arg1[hs->hart_idx] = arg1;
	atomic_fetch_add_explicit(&harts_done, 1, memory_order_release);

	/* Park until the next wakeup */
	while (1 == 1) {
		wfi();
	}
}

static int
test_wakeup_args(unsigned flags)
{
	int failures = 0;

	if (!flags)
		return PROBE_INTERACTIVE;

	ANN("\n---=== Hart Wakeup-with-args Test ===---\n");

	int num_harts = hart_get_count();
	if (num_harts == 1) {
		WRN("Only one hart came up, nobody to wake up\n");
		INF("Press a key to continue...\n");
		return 0;
	}

	struct hart_state *this_hs = hart_get_hstate_self();
	atomic_store_explicit(&harts_done, 0, memory_order_relaxed);
	for (int i = 0; i < num_harts; i++) {
		observed_arg0[i] = 0;
		observed_arg1[i] = 0;
	}

	/* Wake up each secondary hart with its own arguments, back to
	 * back without waiting in between. Each hart must observe the
	 * arguments that were meant for it (this used to race: all
	 * in-flight wakeups shared a single args buffer with no
	 * serialization, so a hart could read the args of a wakeup
	 * that was sent after its own). */
	INF("Waking up %i secondary harts with per-hart args\n", num_harts - 1);
	int expected = 0;
	for (int i = 0; i < num_harts; i++) {
		if (i == this_hs->hart_idx)
			continue;
		hart_wakeup_with_addr(i, (uintptr_t)wakeup_test_entry,
				      0x1000 + i, 0x2000 + i, 0);
		expected++;
	}

	/* Wait for all of them to check in (bounded) */
	uint32_t spins = 100000000;
	while ((atomic_load_explicit(&harts_done, memory_order_acquire) < expected) && --spins)
		pause();

	if (!spins) {
		ERR("Timed out waiting for harts to check in (%i/%i)\n",
		    atomic_load_explicit(&harts_done, memory_order_relaxed), expected);
		failures++;
	}

	for (int i = 0; i < num_harts; i++) {
		if (i == this_hs->hart_idx)
			continue;
		INF("hart_idx %i: arg0: 0x%lx, arg1: 0x%lx\n",
		    i, observed_arg0[i], observed_arg1[i]);
		if (observed_arg0[i] != (uint64_t)(0x1000 + i) ||
		    observed_arg1[i] != (uint64_t)(0x2000 + i)) {
			ERR("hart_idx %i got wrong args (expected 0x%x/0x%x)\n",
			    i, 0x1000 + i, 0x2000 + i);
			failures++;
		}
	}

	if (!failures)
		INF("All harts got their own arguments\n");

	INF("Press a key to continue...\n");
	return failures ? PROBE_FAILED : 0;
}
REGISTER_PLATFORM("Hart wakeup with args test", test_wakeup_args);

#ifndef PLAT_NO_MTIMER
/* mtime tick the timed-wakeup target observed when its entry finally ran */
static volatile uint64_t observed_wakeup_tick = 0;
static atomic_int timed_hart_done = 0;

static void
timed_wakeup_entry(uint64_t arg0, uint64_t arg1)
{
	(void)arg0;
	(void)arg1;
	/* We're dispatched only after the requested delay elapsed. mtime is the
	 * shared wall clock, so this reads in the same domain as the waker's t0
	 * below (see hart_wakeup_with_addr's mtimer_cycles contract). */
	observed_wakeup_tick = mtimer_get_num_ticks();
	atomic_store_explicit(&timed_hart_done, 1, memory_order_release);

	/* Park until the next wakeup */
	while (1 == 1) {
		wfi();
	}
}

/*
 * Timed wakeup test: wake a secondary hart with a non-zero mtimer_cycles and
 * verify it's dispatched ~that many mtime ticks later. This exercises the
 * mtimer_cycles path (mtimer_arm_after_ticks -> wfi -> timer IRQ -> jump) that
 * the args test above never touches (it always passes 0). It catches both
 * failure modes of a broken delay: a hang (target never runs -> we time out)
 * and a deadline armed in the wrong units/domain (fires immediately -> the
 * observed delay collapses to ~0 and falls below the lower bound).
 */
static int
test_wakeup_timed(unsigned flags)
{
	if (!flags)
		return PROBE_INTERACTIVE;

	ANN("\n---=== Hart Timed-Wakeup Test ===---\n");

	int num_harts = hart_get_count();
	if (num_harts == 1) {
		WRN("Only one hart came up, nobody to wake up\n");
		INF("Press a key to continue...\n");
		return 0;
	}

	struct hart_state *this_hs = hart_get_hstate_self();
	int target = -1;
	for (int i = 0; i < num_harts; i++) {
		if (i != this_hs->hart_idx) {
			target = i;
			break;
		}
	}

	/* Delay of ~100ms expressed in mtime ticks, and a generous tolerance:
	 * the observed delay is always >= the request (IPI delivery and the
	 * target's own mtime read at arm time only add to it), so the lower
	 * bound just needs to be comfortably above 0 to reject an immediate
	 * fire, and the upper bound absorbs scheduling slop. */
	const uint64_t freq = mtimer_get_freq();
	const uint64_t delay_ticks = freq / 10;			/* 100ms */
	const uint64_t min_ticks = delay_ticks - (delay_ticks / 10);	/* 90% */
	const uint64_t max_ticks = delay_ticks * 2;			/* 200% */

	atomic_store_explicit(&timed_hart_done, 0, memory_order_relaxed);
	observed_wakeup_tick = 0;

	INF("Waking hart_idx %i after %lu ticks (~100ms @ %lu Hz)\n",
	    target, delay_ticks, freq);

	uint64_t t0 = mtimer_get_num_ticks();
	hart_wakeup_with_addr(target, (uintptr_t)timed_wakeup_entry, 0, 0, delay_ticks);

	/* Bounded wait for the target to run its entry (after the delay). The
	 * spin budget is well above the ~100ms delay; if we exhaust it the
	 * target is stuck in its sleep loop. */
	uint32_t spins = 1000000000;
	while (!atomic_load_explicit(&timed_hart_done, memory_order_acquire) && --spins)
		pause();

	if (!spins) {
		ERR("Timed wakeup never fired (target stuck in sleep?)\n");
		INF("Press a key to continue...\n");
		return PROBE_FAILED;
	}

	uint64_t elapsed = observed_wakeup_tick - t0;
	INF("Observed delay: %lu ticks (expected %lu..%lu)\n",
	    elapsed, min_ticks, max_ticks);

	int failures = 0;
	if (elapsed < min_ticks || elapsed > max_ticks) {
		ERR("Timed wakeup out of range\n");
		failures++;
	} else {
		INF("Timed wakeup within tolerance\n");
	}

	INF("Press a key to continue...\n");
	return failures ? PROBE_FAILED : 0;
}
REGISTER_PLATFORM("Hart timed wakeup test", test_wakeup_timed);
#endif /* !defined(PLAT_NO_MTIMER) */

#endif /* !defined(PLAT_NO_IPI) */
