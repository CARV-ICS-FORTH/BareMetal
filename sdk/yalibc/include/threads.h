/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _THREADS_H
#define _THREADS_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <time.h>	/* For struct timespec */

/* We don't really support threads here, just thrd_sleep() */

/* Enumeration constants returned by functions
 * declared in threads.h (we just care about those
 * returned by thrd_sleep). */
enum {
	thrd_success = 0,
	thrd_error = -1
};

int thrd_sleep(const struct timespec *req, struct timespec *_Nullable rem);

#ifdef __cplusplus
}
#endif
#endif /* _THREADS_H */