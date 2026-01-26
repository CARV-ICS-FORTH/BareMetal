/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>	/* For printf */

/************************************\
* PRINTF WRAPPERS FOR CONSOLE OUTPUT *
\************************************/

/* ANSI color codes */
#if !defined(NO_ANSI_COLORS)
	#define NORMAL	"\x1B[0m"
	#define	BRIGHT	"\x1B[1m"
	#define	DIM	"\x1B[2m"
	#define RED	"\x1B[31m"
	#define GREEN	"\x1B[32m"
	#define YELLOW	"\x1B[33m"
	#define BLUE	"\x1B[34m"
	#define MAGENTA	"\x1B[35m"
	#define CYAN	"\x1B[36m"
	#define WHITE	"\x1B[37m"
#else
	#define NORMAL	""
	#define	BRIGHT	""
	#define	DIM	""
	#define RED	""
	#define GREEN	""
	#define YELLOW	""
	#define BLUE	""
	#define MAGENTA	""
	#define CYAN	""
	#define WHITE	""
#endif

#if defined(DEBUG)
	#define DBG(fmt, ...)	printf(MAGENTA fmt NORMAL, ##__VA_ARGS__)
#else
	/* Instead of defining an empty DBG, this will still check
	*  printf() syntax/arguments even if it's not called. */
	#define DBG(fmt, ...)	do { if (0) printf(fmt, ##__VA_ARGS__); } while(0)
#endif

#if defined(NET_DEBUG)
	#define DBG_NET(fmt, ...)	printf(BLUE fmt NORMAL, ##__VA_ARGS__)
#else
	/* Instead of defining an empty DBG, this will still check
	*  printf() syntax/arguments even if it's not called. */
	#define DBG_NET(fmt, ...)	do { if (0) printf(fmt, ##__VA_ARGS__); } while(0)
#endif

#define INF(fmt, ...)	printf(CYAN fmt NORMAL, ##__VA_ARGS__)
#define ANN(fmt, ...)	printf(GREEN fmt NORMAL, ##__VA_ARGS__)
#define WRN(fmt, ...)	printf(BRIGHT YELLOW "Warning: " fmt NORMAL, ##__VA_ARGS__)
#define ERR(fmt, ...)	printf(BRIGHT RED "Error: " fmt NORMAL, ##__VA_ARGS__)

#endif /* _UTILS_H */