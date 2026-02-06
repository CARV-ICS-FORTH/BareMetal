/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ASSERT_H
#define _ASSERT_H
#include <features.h>

#define __STDC_VERSION_ASSERT_H__ 202311L

/* assert.h - Diagnostics
 *
 * This header can be included multiple times with different NDEBUG settings.
 * Each inclusion redefines assert() based on whether NDEBUG is defined.
 *
 * Per C standard (C17 7.2, C23 7.2):
 * - If NDEBUG is defined, assert expands to ((void)0)
 * - Otherwise, it prints diagnostic info to stderr and calls abort()
 *
 * Note on static_assert:
 * - C11/C17: _Static_assert is a keyword; static_assert is a macro (defined here)
 * - C23: static_assert is a keyword (no macro needed)
 * - You can use _Static_assert directly without including this header in C11+
 */

#undef assert

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else

#include <stdio.h>
#include <stdlib.h>

/* Standard-compliant assert macro
 * The standard requires: print to stderr, then call abort()
 */
#define assert(expr)	((expr) ? (void)0 : \
			(printf("Assertion failed: %s, function %s, file %s, line %d.\n", \
				  #expr, __func__, __FILE__, __LINE__), abort()))
#endif /* NDEBUG */

/* static_assert macro for C11/C17
 * In C23, static_assert is already a keyword, so we only define it for C11/C17.
 * In C++, static_assert has been a keyword since C++11, so don't redefine it. */
#if __STDC_VERSION__ >= 201112L && __STDC_VERSION__ < 202311L && !defined(__cplusplus)
#define static_assert _Static_assert
#endif
#endif /* _ASSERT_H */