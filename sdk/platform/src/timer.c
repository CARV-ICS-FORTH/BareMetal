/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_* constants */
#include <platform/interfaces/timer.h>	/* The public API used by yalibc */
#include <platform/riscv/csr.h>		/* For wfi() */
#include <platform/riscv/hart.h>	/* For hart_get_hstate_self(), hart_*_counter() */
#include <platform/riscv/mtimer.h>	/* For mtimer_*() functions */
#include <platform/utils/utils.h>	/* For console output */

/*********\
* HELPERS *
\*********/

#define NSECS_IN_SEC 1000000000UL

/* Note: We assume all harts run at the same frequency
 * so we don't need to have different timer parameters
 * per part. Hence we only have two sets of parameters,
 * one for the platform-level timer (mtimer), and one
 * for the timer based on each hart's cycle counter. */
struct timer_spec {
	struct timespec res;
	uint32_t clock_freq;
	uint32_t mult_c2ns;
	uint32_t shift_c2ns;
	uint32_t mult_ns2c;
	uint32_t shift_ns2c;
	uint32_t mult_c2c;
	uint32_t shift_c2c;
};

static struct timer_spec cyclecount_timer = {0};

#ifndef PLAT_NO_MTIMER
static struct timer_spec platform_timer = {0};
/* We track the last measurement of each hart's cyclecount_timer
 * in the hart's hart_state, and here we track the last count of
 * the (global) platform_timer. */
static uint64_t last_platform_tval = 0;
#endif

/* Initialize a timer parameters for the given clock frequency,
 * and/or return the cached version. Note: We could init params
 * on e.g. platform_init or hart_init, but this way if a program
 * never uses a timer, this function will also be optimized-out
 * since it'll never get called. */
static const struct timer_spec *
timer_get_spec(timerid_t timerid)
{
	struct timer_spec *timer = NULL;
	uint32_t clock_freq = 0;
	switch (timerid) {
	case PLAT_TIMER_RTC:
	case PLAT_TIMER_MTIMER:
		#ifndef PLAT_NO_MTIMER
			timer = &platform_timer;
			clock_freq = PLAT_MTIMER_FREQ;
			break;
		#endif
		/* Fallthrough */
	case PLAT_TIMER_CYCLES:
		timer = &cyclecount_timer;
		clock_freq = PLAT_HART_FREQ;
		break;
	default:
		return NULL;
	}

	/* Already initialized */
	if (timer->clock_freq > 0)
		return timer;

	uint32_t divisor = 0;
	timer->res.tv_sec = 0;
	timer->res.tv_nsec = 0;

	/* Start with nanoseconds and go down to 100msec */
	for (divisor = NSECS_IN_SEC; divisor >= 100; divisor /= 10) {
		if (clock_freq >= divisor) {
			timer->res.tv_nsec = NSECS_IN_SEC / divisor;
			break;
		}
	}

	/* This shouldn't happen, I mean the clock should at least
	 * run at a few KHz or else is kinda useless, if we are here
	 * it's less than 100Hz. */
	if (!timer->res.tv_nsec)
		timer->res.tv_sec = 1;

	/* In order to calculate nsecs from cycles we need to calculate
	 * nsecs = (cycles/clock_freq) * NSECS_IN_SEC, however we don't
	 * want to use floats/doubles, and we also want to avoid division.
	 * So instead of division we'll use shifts and look for a pair of
	 * multiplier/shifter that would allow us to calculate the above
	 * much faster. This comes from Linux's clocks_calc_mult_shift(). */
	timer->clock_freq = clock_freq;
	const uint32_t maxdelay_secs = 10;

	/* Make sure a multiplication between maxdelay_secs and freq won't
	 * overflow a 64bit integer. This assumes that the maximum
	 * interval we'll measure will be up to maxdelay_secs. Larger intervals
	 * may overflow. */
	uint64_t tmp = ((uint64_t) maxdelay_secs * clock_freq) >> 32;
	uint32_t shift_accumulator = 32;
	while (tmp) {
		tmp >>= 1;
		shift_accumulator--;
	}

	uint64_t mult = 0;
	uint32_t shift = 0;
	/* Multiplier / shifter for (NSECS_IN_SEC / clock_freq) */
	for (shift = 32; shift > 0; shift--) {
		mult = ((uint64_t) NSECS_IN_SEC) << shift;
		mult += timer->clock_freq >> 1;
		mult /= timer->clock_freq;
		if ((mult >> shift_accumulator) == 0)
			break;
	}

	timer->mult_c2ns = mult;
	timer->shift_c2ns = shift;

	/* Same as above but for converting nsecs to cycles, so now we want
	 * cycles = (nsecs / NSECS_IN_SEC) * clock_freq. */
	tmp = ((uint64_t) maxdelay_secs * NSECS_IN_SEC) >> 32;
	shift_accumulator = 32;
	while (tmp) {
		tmp >>= 1;
		shift_accumulator--;
	}

	for (shift = 32; shift > 0; shift--) {
		mult = ((uint64_t) timer->clock_freq) << shift;
		mult += NSECS_IN_SEC >> 1;
		mult /= NSECS_IN_SEC;
		if ((mult >> shift_accumulator) == 0)
			break;
	}

	timer->mult_ns2c = mult;
	timer->shift_ns2c = shift;

	/* Same but for nsecs to clock() cycles, so it's
	 * cycles = (nsecs / NSECS_IN_SEC) * CLOCKS_PER_SEC */
	for (shift = 32; shift > 0; shift--) {
		mult = ((uint64_t) CLOCKS_PER_SEC) << shift;
		mult += NSECS_IN_SEC >> 1;
		mult /= NSECS_IN_SEC;
		if ((mult >> shift_accumulator) == 0)
			break;
	}

	timer->mult_c2c = mult;
	timer->shift_c2c = shift;

	return timer;
}

static uint64_t __attribute__((noinline))
timer_sample(timerid_t timerid, const struct timer_spec **_Nullable tspec)
{
	const struct timer_spec *timer = NULL;
	uint64_t cycles = 0;
	uint64_t tval = 0;
	switch (timerid) {
		case PLAT_TIMER_RTC:
		case PLAT_TIMER_MTIMER:
			#ifndef PLAT_NO_MTIMER
				timer = timer_get_spec(timerid);
				if (last_platform_tval == 0)
					mtimer_reset_num_ticks();
				cycles = mtimer_get_num_ticks();
				mtimer_reset_num_ticks();
				tval = (cycles * timer->mult_c2ns) >> timer->shift_c2ns;
				tval += last_platform_tval;
				last_platform_tval = tval;
				break;
			#else
				/* Fallthrough */
			#endif
		case PLAT_TIMER_CYCLES:
			timer = timer_get_spec(timerid);
			struct hart_state *hs = hart_get_hstate_self();
			if (hs->last_cyclecount_tval == 0) {
				hart_reset_counter(HC_CYCLES);
				hart_enable_counter(HC_CYCLES);
			}
			cycles = hart_get_counter(HC_CYCLES);
			hart_reset_counter(HC_CYCLES);
			tval = (cycles * timer->mult_c2ns) >> timer->shift_c2ns;
			tval += hs->last_cyclecount_tval;
			hs->last_cyclecount_tval = tval;
			break;
		default:
			ERR("Tried to sample unknown timerid: %i\n", timerid);
			return 0;
	}
	if (tspec)
		*tspec = timer;
	return tval;
}

/**************\
* ENTRY POINTS *
\**************/

const struct timespec*
timer_get_resolution(timerid_t timerid)
{
	const struct timer_spec *timer = timer_get_spec(timerid);
	if (timer)
		return &timer->res;
	else
		return NULL;
}

uint64_t
timer_get_nsecs(timerid_t timerid)
{
	return timer_sample(timerid, NULL);
}

uint64_t
timer_get_num_ticks(timerid_t timerid)
{
	const struct timer_spec *timer = NULL;
	uint64_t nsecs = timer_sample(timerid, &timer);
	if (!timer)
		return 0;
	uint64_t cycles = (nsecs * timer->mult_c2c) >> timer->shift_c2c;
	return cycles;
}

uint64_t
timer_nsecs_to_cycles(timerid_t timerid, uint64_t nsecs)
{
	const struct timer_spec *timer = timer_get_spec(timerid);
	if (!timer)
		return 0;
	return (nsecs * timer->mult_ns2c) >> timer->shift_ns2c;
}

void
timer_nanosleep(timerid_t timerid, uint64_t nsecs)
{
	struct hart_state *hs = hart_get_hstate_self();
	const struct timer_spec *timer = timer_get_spec(timerid);
	if (!timer)
		return;
	uint64_t cycles_to_wait = (nsecs * timer->mult_ns2c) >> timer->shift_ns2c;

	switch (timerid) {
	case PLAT_TIMER_RTC:
	case PLAT_TIMER_MTIMER:
		#ifndef PLAT_NO_MTIMER
			hart_set_flags(hs, HS_FLAG_SLEEPING);
			mtimer_arm_after_ticks(cycles_to_wait);
			mtimer_enable_irq();
			/* Wait for the trap handler to clear the
			 * HS_FLAG_SLEEPING flag and disarm the timer. */
			while (hart_test_flags(hs, HS_FLAG_SLEEPING)) {
				wfi();
			}
			mtimer_disable_irq();
			break;
		#endif
		/* Fallthrough */
	case PLAT_TIMER_CYCLES:
		uint64_t curr_cycles = hart_get_counter(HC_CYCLES);
		uint64_t tot_cycles = curr_cycles + cycles_to_wait;

		hart_set_flags(hs, HS_FLAG_SLEEPING);
		while (tot_cycles > curr_cycles)
			curr_cycles = hart_get_counter(HC_CYCLES);

		hart_clear_flags(hs, HS_FLAG_SLEEPING);
		break;
	default:
		return;
	}
}
