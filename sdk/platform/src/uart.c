/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_UART_* / PLAT_NO_IRQ */
#include <platform/interfaces/irq.h>	/* For REGISTER_IRQ_SOURCE */
#include <platform/interfaces/uart.h>	/* For uart interface definitions */
#include <platform/riscv/mmio.h>	/* For register access */
#include <platform/utils/utils.h>	/* For DBG */
#include <errno.h>			/* For EAGAIN, EIO */

#if defined(PLAT_UART_BASE) && (PLAT_UART_BASE > 0)

/*
 * This is a simple driver for an 8250/16550A compatible UART, for more
 * infos on its programming interface check out:
 * https://www.lammertbies.nl/comm/info/serial-uart
 */

/******************\
* Register offsets *
\******************/

#define UART_RBR_OFFSET		0x0	/* In:  Receive Buffer Register */
#define UART_THR_OFFSET		0x0	/* Out: Transmitter Holding Register */
#define UART_DLL_OFFSET		0x0	/* Out: Divisor Latch Low */
#define UART_IER_OFFSET		0x1	/* I/O: Interrupt Enable Register */
#define UART_DLM_OFFSET		0x1	/* Out: Divisor Latch High */
#define UART_FCR_OFFSET		0x2	/* Out: FIFO Control Register */
#define UART_IIR_OFFSET		0x2	/* I/O: Interrupt Identification Register */
#define UART_LCR_OFFSET		0x3	/* Out: Line Control Register */
#define UART_MCR_OFFSET		0x4	/* Out: Modem Control Register */
#define UART_LSR_OFFSET		0x5	/* In:  Line Status Register */
#define UART_MSR_OFFSET		0x6	/* In:  Modem Status Register */
#define UART_SCR_OFFSET		0x7	/* I/O: Scratch Register */
#define UART_MDR1_OFFSET	0x8	/* I/O:  Mode Register */

#define UART_LSR_FIFOE		0x80    /* Fifo error */
#define UART_LSR_TEMT		0x40    /* Transmitter empty */
#define UART_LSR_THRE		0x20    /* Transmit-hold-register empty */
#define UART_LSR_BI		0x10    /* Break interrupt indicator */
#define UART_LSR_FE		0x08    /* Frame error indicator */
#define UART_LSR_PE		0x04    /* Parity error indicator */
#define UART_LSR_OE		0x02    /* Overrun error indicator */
#define UART_LSR_DR		0x01    /* Receiver data ready */
#define UART_LSR_BRK_ERROR_BITS	0x1E    /* BI, FE, PE, OE bits */

#define UART_FCR_FIFO_EN	0x1	/* FIFO Enable */
#define	UART_FCR_RX_FIFO_CLEAR	0x2	/* Clear RX FIFO */
#define	UART_FCR_TX_FIFO_CLEAR	0x4	/* Clear TX FIFO */
#define	UART_FCR_DMA_MODE	0x8	/* Enable DMA mode (RXRDY/TXRDY) */
#define	UART_FCR_FIFO_TRIG_LVL	0x60	/* Mask for FIFO Trigger level */

/*********\
* Helpers *
\*********/

static inline void
uart_write(int reg, uint8_t val)
{
	uintptr_t addr = (uintptr_t)(PLAT_UART_BASE + (reg << PLAT_UART_REG_SHIFT));
	#if defined(PLAT_UART_SHIFTED_IO) && (PLAT_UART_REG_SHIFT == 4)
		write64((uint64_t*)addr, val);
	#elif defined(PLAT_UART_SHIFTED_IO) && (PLAT_UART_REG_SHIFT == 2)
		write32((uint32_t*)addr, val);
	#else
		write8((uint8_t*)addr, val);
	#endif
}

static inline uint8_t
uart_read(int reg)
{
	uintptr_t addr = (uintptr_t)(PLAT_UART_BASE + (reg << PLAT_UART_REG_SHIFT));
	#if defined(PLAT_UART_SHIFTED_IO) && (PLAT_UART_REG_SHIFT == 4)
		return (uint8_t) read64((uint64_t*)addr);
	#elif defined(PLAT_UART_SHIFTED_IO) && (PLAT_UART_REG_SHIFT == 2)
		return (uint8_t) read32((uint32_t*)addr);
	#else
		return read8((uint8_t*)addr);
	#endif
}

/**************\
* Entry points *
\**************/

void
uart_init(void)
{
	uint8_t uart_divisor = (uint8_t) (PLAT_UART_CLOCK_HZ / (PLAT_UART_BAUD_RATE << 4));

	/* Disable interrupts */
	uart_write(UART_IER_OFFSET, 0);

	/* Enable DLAB */
	uart_write(UART_LCR_OFFSET, 0x80);

	/* Set divisor low/high bytes*/
	/* Example: 50MHz / (115200 << 4) = 27.1... -> 0x1b */
	uart_write(UART_DLL_OFFSET, uart_divisor);

	/* Set Line Control Register:
	 * 8 bits, no parity, one stop bit */
	uart_write(UART_LCR_OFFSET, 0x3);

	/* No modem control DTR/RTS */
	uart_write(UART_MCR_OFFSET, 0x0);

	/* Set scratchpad */
	uart_write(UART_SCR_OFFSET, 0x0);

	/* Enable and clear FIFO */
	uart_write(UART_FCR_OFFSET, 0x07);

	/* Clear any pending RX */
	uart_read(UART_RBR_OFFSET);

	/* Clear any pending errors */
	uart_read(UART_LSR_OFFSET);

	return;
}

int
uart_getc(void)
{
	uint8_t lsr = uart_read(UART_LSR_OFFSET);

	/* Check for errors */
	if (lsr & UART_LSR_BRK_ERROR_BITS)
		return -EIO;

	/* We have data available, read the next character in FIFO */
	if(lsr & UART_LSR_DR)
		return uart_read(UART_RBR_OFFSET);

	/* No data yet, caller should try again */
	return -EAGAIN;
}

void
uart_putc(uint8_t c)
{
	uint8_t lsr = 0;

	/* Wait for transmit register to be flushed */
	do {
		lsr = uart_read(UART_LSR_OFFSET);
	} while(!(lsr & UART_LSR_THRE));

	/* Write byte to the Tx buffer */
	uart_write(UART_THR_OFFSET, c);
	/* If we got a new line, also print a carriage return */
	if (c == '\n')
		uart_write(UART_THR_OFFSET, '\r');

	return;
}

#ifndef PLAT_NO_IRQ
void
uart_enable_irq(void)
{
	uart_write(UART_IER_OFFSET, 1);
}

void
uart_disable_irq(void)
{
	uart_write(UART_IER_OFFSET, 0);
}

static uart_irq_handler_t uart_irq_handler = NULL;

void uart_set_irq_handler(uart_irq_handler_t new_handler)
{
	if (new_handler != NULL) {
		uart_irq_handler = new_handler;
		/* Memory barrier to ensure the write is visible */
		__asm__ volatile("fence rw,rw" ::: "memory");
	}
}

static void
uart_irq_trampoline(uint16_t source_id)
{
	/* Safety check in case someone sets NULL */
	if (uart_irq_handler != NULL)
		uart_irq_handler(source_id);
	else
		DBG("UART interrupt received but no handler installed\n");
}

REGISTER_IRQ_SOURCE(uart0_rx, {
	.source.wire_id = PLAT_UART_IRQ,
	.handler = uart_irq_trampoline,
	.target_hart = 0,
	.priority = IRQ_PRIORITY_HIGH,
	.flags = IRQ_TRIGGER_LEVEL_HIGH,
});
#else

void uart_enable_irq(void) { return; }
void uart_disable_irq(void) { return; }
void uart_set_irq_handler(uart_irq_handler_t new_handler) { return; }

#endif /* PLAT_NO_IRQ */
#endif /* defined(PLAT_UART_BASE) && (PLAT_UART_BASE > 0) */