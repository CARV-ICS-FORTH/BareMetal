/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _UART_H
#define _UART_H

#include <stdint.h>	/* For typed integers */

void uart_init(void);
int uart_getc(void);
void uart_putc(unsigned char c);
void uart_enable_irq(void);
void uart_disable_irq(void);

typedef void (*uart_irq_handler_t)(uint16_t source_id);
void uart_set_irq_handler(uart_irq_handler_t new_handler);

#endif /* _UART_H */