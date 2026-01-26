/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/interfaces/timer.h>
#include <threads.h>

#define NSECS_IN_SEC 1000000000

int
thrd_sleep(const struct timespec *req, struct timespec *_Nullable rem)
{
	if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec > 999999999)
		return thrd_error;

	uint64_t nsecs = ((uint64_t) req->tv_sec * NSECS_IN_SEC) + req->tv_nsec;
	timer_nanosleep(PLAT_TIMER_RTC, nsecs);	/* Equivalent to TIME_UTC */

	/* Not interruptible, so remaining time is always 0 */
	if (rem) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}

	return thrd_success;
}