/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _STDIO_H
#define _STDIO_H
#ifdef __cplusplus
extern "C" {
#endif

#define __STDC_VERSION_STDIO_H__ 202311L

#include <features.h>
#include <stdarg.h>	/* For va_* stuff */
#include <stddef.h>	/* For NULL/size_t */

/* Length of [-]NaN string
 * "If the implementation only uses the [-]NAN style,
 * then _PRINTF_NAN_LEN_MAX would have the value 4" */
#define _PRINTF_NAN_LEN_MAX 4

/* No file / stream operations or file descriptors etc,
 * there is no OS here to handle such operations. We'll only
 * provide those functions that assume stdout by default, which
 * in our case will be the platform's uart. */

/* We still need this for getchar */
#define EOF -1

/* Note: *printf doesn't support wide chars, decimal floats, nor %n.
 * Wide chars and decimal floats are both conditional features
 * (__STDC_ISO_10646__, __STDC_IEC_60559_DFP__) so omitting them is
 * compliant. The %n conversion specifier is omitted due to security
 * concerns (it's a common attack vector), though this technically
 * makes the implementation non-conforming to the base standard. */
int printf(const char * restrict format, ...);
int sprintf(char * restrict s, const char * restrict format, ...);
int snprintf(char * restrict s, size_t n, const char * restrict format, ...);
int vprintf(const char* restrict fmt, va_list va);
int vsprintf(char* restrict outbuff, const char* restrict fmt, va_list va);
int vsnprintf(char* restrict outbuff, size_t outbuff_len, const char* restrict fmt, va_list va);
/* No scanf for now */
int getchar(void);
int putchar(int c);
int puts(const char *s);

#ifdef __cplusplus
}
#endif
#endif /* _STDIO_H */