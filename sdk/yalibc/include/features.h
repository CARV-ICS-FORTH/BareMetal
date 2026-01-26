/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FEATURES_H
#define _FEATURES_H

/* YALibC Feature Test Macros
 *
 * This header handles feature test macros for POSIX and other extensions.
 * It should be included at the top of each public header that provides
 * POSIX or extension functions.
 */

/* If nothing is defined and not in strict ANSI mode, default to POSIX.1-2008 */
#if !defined(_POSIX_C_SOURCE) && !defined(__STRICT_ANSI__)
#define _POSIX_C_SOURCE 200809L
#endif

/* Nullability qualifiers: currently only supported by Clang.
 * For GCC and other compilers, these are defined as empty. */
#if !(defined(__clang__) && __has_feature(nullability))
#define	_Nonnull
#define	_Nullable
#define	_Null_unspecified
#endif

/* noreturn compatibility: C23 uses [[noreturn]], older standards use _Noreturn.
 * Define _Noreturn for pre-C23 compilers to map to the attribute syntax. */
#if __STDC_VERSION__ < 202311L && !defined(__cplusplus)
#define _Noreturn [[noreturn]]
#endif

#define __STDC_NO_COMPLEX__ 1

#endif /* _FEATURES_H */
