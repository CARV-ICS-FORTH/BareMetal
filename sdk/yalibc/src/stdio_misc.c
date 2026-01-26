/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/interfaces/uart.h>	/* For uart_putc/getc() */
#include <errno.h>			/* For EAGAIN */
#include <stdio.h>

int
putchar(int c)
{
	uart_putc((char) c);
	return (int) c;
}

int
getchar(void)
{
	int ret;

	/* Poll until data is available or error occurs */
	do {
		ret = uart_getc();
	} while (ret == -EAGAIN);

	/* Return EOF on error */
	if (ret < 0)
		return EOF;

	return ret;
}

int
puts(const char *s)
{
	while (*s)
		putchar(*s++);
	putchar('\n');
	return 0;
}
