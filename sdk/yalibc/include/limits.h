/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _LIMITS_H
#define _LIMITS_H

#define __STDC_VERSION_LIMITS_H__ 202311L

/* Just use GCC/Clang constants */
#define CHAR_BIT __CHAR_BIT__

#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN 0
#define CHAR_MAX UCHAR_MAX
#else
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#endif

/* Signed/unsigned char */
#define SCHAR_MIN (-__SCHAR_MAX__-1)
#define SCHAR_MAX __SCHAR_MAX__
#define UCHAR_MAX (__SCHAR_MAX__*2+1)

/* Widths (C23) */
#define CHAR_WIDTH __CHAR_BIT__
#define SCHAR_WIDTH __CHAR_BIT__
#define UCHAR_WIDTH __CHAR_BIT__

/* Short */
#define SHRT_MIN (-__SHRT_MAX__-1)
#define SHRT_MAX __SHRT_MAX__
#define USHRT_MAX (__SHRT_MAX__*2+1)
#define SHRT_WIDTH __SHRT_WIDTH__
#define USHRT_WIDTH __SHRT_WIDTH__

/* Int */
#define INT_MIN (-__INT_MAX__-1)
#define INT_MAX __INT_MAX__
#define UINT_MAX (__INT_MAX__*2U+1U)
#define INT_WIDTH __INT_WIDTH__
#define UINT_WIDTH __INT_WIDTH__

/* Long */
#define LONG_MIN (-__LONG_MAX__-1L)
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__*2UL+1UL)
#define LONG_WIDTH __LONG_WIDTH__
#define ULONG_WIDTH __LONG_WIDTH__

/* Long long */
#define LLONG_MIN (-__LONG_LONG_MAX__-1LL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (__LONG_LONG_MAX__*2ULL+1ULL)
#define LLONG_WIDTH __LONG_LONG_WIDTH__
#define ULLONG_WIDTH __LONG_LONG_WIDTH__

/* C23 additions */
#define BITINT_MAXWIDTH ULLONG_WIDTH
#define BOOL_WIDTH 1
#define BOOL_MAX 1
#define MB_LEN_MAX 4

#endif /* _LIMITS_H */