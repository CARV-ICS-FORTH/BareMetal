/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TIME_H
#define _TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __STDC_VERSION_TIME_H__ 202311L

#include <stddef.h>	/* For NULL/size_t */
#include <stdint.h>	/* For uint32_t */

#define CLOCKS_PER_SEC 1000000000

typedef size_t clock_t;
typedef size_t time_t;

struct timespec {
	time_t	tv_sec;		/* Seconds */
	uint32_t tv_nsec;	/* Nanoseconds [0, 999'999'999]
				 * Note: C23 allows this to be smaller than before, since
				 * we can fit 1mil in 32bits leave this as a uint32_t */
};

enum time_bases {
	TIME_UTC = 1,
	TIME_MONOTONIC = 2,
	TIME_ACTIVE = 3,
};

clock_t clock(void);
int timespec_get(struct timespec *ts, int base);
int timespec_getres(struct timespec *ts, int base);

/* POSIX timers extension - clock functions and nanosleep
 * Requires _POSIX_C_SOURCE >= 199309L (POSIX.1b - Real-time extensions)
 */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 199309L

typedef int clockid_t;

enum clock_ids {
	CLOCK_REALTIME = 1,
	CLOCK_MONOTONIC = 2,
	CLOCK_PROCESS_CPUTIME_ID = 3,
};

int clock_gettime(clockid_t clockid, struct timespec *tp);
int clock_getres(clockid_t clockid, struct timespec *_Nullable res);
int nanosleep(const struct timespec *req, struct timespec *_Nullable rem);

#endif

#ifdef __cplusplus
}
#endif
#endif /* _TIME_H */