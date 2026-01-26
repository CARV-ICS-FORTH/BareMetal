/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _STDLIB_H
#define _STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif

#define __STDC_VERSION_STDLIB_H__ 202311L

#include <features.h>
#include <stddef.h>	/* For NULL/size_t */

#define EXIT_FAILURE -1
#define EXIT_SUCCESS 0
#define RAND_MAX 0x40000000  /* 2^30, largest power of 2 in positive int range */
#define MB_CUR_MAX 1

int rand(void);
void srand(unsigned int seed);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void *reallocarray(void *ptr, size_t nmemb, size_t size);

_Noreturn void abort(void);

#ifdef __cplusplus
}
#endif
#endif /* _STDLIB_H */