/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/interfaces/timer.h>
#include <time.h>


#define NSECS_IN_SEC 1000000000

/**********************\
* C STANDARD FUNCTIONS *
\**********************/

/* C89 clock()
 * Returns the implementation’s best approximation of the active
 * processing time associated with the program execution since the
 * beginning of an implementation-defined era related only to the
 * program invocation. */
clock_t
clock(void)
{
	return timer_get_num_ticks(PLAT_TIMER_CYCLES);
}

/* C23 timespec_get()
 * Sets the interval pointed to by ts to hold the current calendar
 * time based on the specified time base. */
int
timespec_get(struct timespec *ts, int base)
{
	if (ts == NULL)
		return -1;

	uint64_t nsecs = timer_get_nsecs(base);
	if (nsecs == 0)
		return -1;

	ts->tv_sec = nsecs / NSECS_IN_SEC;
	ts->tv_nsec = nsecs % NSECS_IN_SEC;

	return base;
}

/* C23 timespec_getres()
 * Returns the resolution of the time provided by the timespec_get
 * function for base in the timespec structure pointed to by ts. */
int
timespec_getres(struct timespec *ts, int base)
{
	if (ts == NULL)
		return -1;

	const struct timespec* res = timer_get_resolution(base);
	if (res == NULL)
		return -1;

	ts->tv_sec = res->tv_sec;
	ts->tv_nsec = res->tv_nsec;

	return base;
}

/******************\
* POSIX EXTENSIONS *
\******************/

#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 199309L

/* These are more commonly used functions defined by POSIX since
 * 1993. Implement them on top of their C11/23 counterparts, now that
 * we finaly have standard C counterparts (it only took 20-30 years :P).
 *
 * Note: clock ids are 1:1 mapped to time bases and also 1:1 mapped
 * to platform timers, so we can just wrap the C functions without
 * worying about mapping clock ids to bases, just assume that they
 * have the same meaning.
 */

/* clock_gettime() - Get time from specified clock */
int
clock_gettime(clockid_t clockid, struct timespec *tp)
{
	int ret = timespec_get(tp, clockid);
	return (ret == clockid) ? 0 : -1;
}

/* clock_getres() - Get resolution of specified clock */
int
clock_getres(clockid_t clockid, struct timespec *_Nullable res)
{
	int ret = timespec_getres(res, clockid);
	return (ret == clockid) ? 0 : -1;
}

/* nanosleep() - Sleep for specified time
 * Note: This implementation is not interruptible, so 'rem' is ignored.
 * Also the C11 equivalent is thrd_sleep which is part of threads.h not
 * time.h, so we have to include it here.
 */
#include <threads.h>
int
nanosleep(const struct timespec *req, struct timespec *_Nullable rem)
{
	return thrd_sleep(req, rem);
}

#endif /* _POSIX_C_SOURCE */
