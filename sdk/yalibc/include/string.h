/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _STRING_H
#define _STRING_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* C23 Section 4 (Conformance) requires for a freestanding implementation to support
 * any program where "the features specified in the header <string.h> are used, except
 * the following functions: strdup, strndup, strcoll, strxfrm, strerror;"
 *
 * So in order to be a compliant C23 freestanding implementation we need to support
 * all the rest. */

#define __STDC_VERSION_STRING_H__ 202311L

#include <stddef.h>  /* For size_t and NULL */

void *memcpy(void * restrict s1, const void * restrict s2, size_t n);
void *memccpy(void * restrict s1, const void * restrict s2, int c, size_t n);
void *memmove(void *s1, const void *s2, size_t n);
char *strcpy(char * restrict s1, const char * restrict s2);
char *strncpy(char * restrict s1, const char * restrict s2, size_t n);
char *strcat(char * restrict s1, const char * restrict s2);
char *strncat(char * restrict s1, const char * restrict s2, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strchr(const char *s, int c);
size_t strcspn(const char *s1, const char *s2);
char *strpbrk(const char *s1, const char *s2);
char *strrchr(const char *s, int c);
size_t strspn(const char *s1, const char *s2);
char *strstr(const char *s1, const char *s2);
char *strtok(char * restrict s1, const char * restrict s2);
void *memset(void *s, int c, size_t n);
void *memset_explicit(void *s, int c, size_t n);
size_t strlen(const char *s);

/* strnlen() exists on C23 but as part of Annex K, also include
 * it via POSIX which is the most common use case. */

 /* C23 Annex K - Bounds-checking interfaces */
#if defined(__STDC_WANT_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__ == 1
size_t strnlen_s(const char *s, size_t maxsize);
#endif

/* POSIX extension - strnlen */
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
size_t strnlen(const char *s, size_t maxsize);
#endif

#ifdef __cplusplus
}
#endif
#endif /* _STRING_H */