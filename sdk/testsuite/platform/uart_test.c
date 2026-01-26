/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_UART_IRQ */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/interfaces/uart.h>	/* For uart_enable_irq() */
#include <platform/riscv/csr.h>		/* For wfi() */
#include <platform/interfaces/irq.h>	/* For REGISTER_IRQ_SOURCE, irq_source_enable() */
#include <platform/riscv/hart.h>	/* For hart_*_interrupts(), hart_*_irq(), hart_*_flags() */
#include <test_framework.h>		/* For test registration macros */

#include <stdio.h>			/* For putchar/getchar */
#include <stdint.h>			/* For typed ints */

static int
uart_loopback_poll(void)
{
	ANN("\n---=== UART Loopback Test (Polling mode) ===---\n");
	INF("Press Q to exit...\n");
	while(1 == 1){
		int ret = getchar();
		if(ret != EOF)
			putchar(ret);
		if (((char) ret) == 'Q') {
			INF("\nExiting uart loopback test (poll mode)...\n");
			break;
		}
	}

	INF("Press a key to continue...\n");
	return 0;
}

static void
uart_irq_test_handler(uint16_t source_id)
{
	struct hart_state *hs = hart_get_hstate_self();
	/* Use non-blocking uart_getc() in IRQ handler, not blocking getchar() */
	int ret = uart_getc();
	if(ret > 0)
		putchar(ret);
	if (((char) ret) == 'Q') {
		INF("\nExiting uart loopback test (irq mode)...\n");
		hart_clear_flags(hs, HS_FLAG_RUNNING);
	}
}
 
static int
uart_loopback_irq(void)
{
	ANN("\n---=== UART Loopback Test (IRQ mode) ===---\n");
	INF("Press Q to exit...\n");
	uart_set_irq_handler(&uart_irq_test_handler);
	hart_enable_intr(INTR_MACHINE_EXTERNAL);
	irq_source_enable(PLAT_UART_IRQ);
	uart_enable_irq();
	struct hart_state *hs = hart_get_hstate_self();
	hart_set_flags(hs, HS_FLAG_RUNNING);
	while(hart_test_flags(hs, HS_FLAG_RUNNING)) {
		wfi();
	}
	uart_disable_irq();
	irq_source_disable(PLAT_UART_IRQ);
	hart_disable_intr(INTR_MACHINE_EXTERNAL);

	INF("Press a key to continue...\n");
	return 0;
}

REGISTER_PLATFORM_TEST("UART loopback test (polling mode)", uart_loopback_poll);
REGISTER_PLATFORM_TEST("UART loopback test (IRQ mode)", uart_loopback_irq);