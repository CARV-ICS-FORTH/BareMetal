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

#endif /* !defined(PLAT_NO_IPI) */
