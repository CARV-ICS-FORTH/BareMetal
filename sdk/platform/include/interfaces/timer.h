/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TIMER_H
#define _TIMER_H

#include <stdint.h>	/* For typed integers */
#include <time.h>	/* For struct timespec */

/* Common API for both platform and cycle count - based timers.
 * Used by yalibc/time.c
 *
 * This interface is independent of both C23 and POSIX - it uses
 * platform-specific timer IDs that map to C23 time bases and
 * POSIX clock IDs at the yalibc layer.
 */

typedef int timerid_t;

enum timer_ids {
	PLAT_TIMER_RTC	= 1,	/* Maps to TIME_UTC / CLOCK_REALTIME */
	PLAT_TIMER_MTIMER = 2,	/* Maps to TIME_MONOTONIC / CLOCK_MONOTONIC */
	PLAT_TIMER_CYCLES = 3,	/* Maps to TIME_ACTIVE / CLOCK_PROCESS_CPUTIME_ID */
};

const struct timespec* timer_get_resolution(timerid_t timer);
uint64_t timer_get_nsecs(timerid_t timer);
uint64_t timer_get_num_ticks(timerid_t timer);
uint64_t timer_nsecs_to_cycles(timerid_t timer, uint64_t nsecs);
void timer_nanosleep(timerid_t timer, uint64_t nsecs);

#endif  /* _TIMER_H */