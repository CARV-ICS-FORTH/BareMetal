/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _PLATFORM_H
#define _PLATFORM_H

/******\
* CORE *
\******/

/*---=== HARTs ===---*/
#define PLAT_MAX_HARTS		4

/* Assumes all harts have the same frequency
 * used for cycle counter - based timer. */
/* Note: QEMU is not cycle-accurate, this is an
 * estimate, and will probably be host-dependent. */
#define	PLAT_HART_FREQ		1000000000


/*---=== Memory Layout ===---*/
#define KB	1024
#define MB	KB * 1024
#define GB	MB * 1024

/* Use the last RAM_SIZE bytes of DRAM */
#define	PLAT_SYSRAM_BASE	0x80000000
#define	PLAT_SYSRAM_SIZE	2 * GB

#define PLAT_ROM_BASE		0x20000000
#define PLAT_ROM_SIZE		512 * KB
#define	PLAT_RAM_SIZE		2 * MB
#define PLAT_RAM_BASE		(PLAT_SYSRAM_BASE + PLAT_SYSRAM_SIZE - PLAT_RAM_SIZE)
#define	PLAT_STACK_SIZE		8 * KB

#if defined(LDSCRIPT)
___rom = PLAT_ROM_BASE;
___rom_size = PLAT_ROM_SIZE;
___ram = PLAT_RAM_BASE;
___ram_size = PLAT_RAM_SIZE;
___stack_size = PLAT_STACK_SIZE;
___num_harts = PLAT_MAX_HARTS;
#endif


/********************\
* INTERRUPT HANDLING *
\********************/

/*---=== TIMER ===---*/

/* Base address for a SiFive CLINT compatible device
 * set to 0 if no CLINT is present */
#define PLAT_CLINT_BASE		0x2000000

/* MTIMER frequency in Hz, set to 0 to disable */
/* See note on PLAT_HART_FREQ, this is inaccurate too. */
#define	PLAT_MTIMER_FREQ	9000000

/* In case of ACLINT, define MTIME/MTIMECMP separately */
#define PLAT_MTIME_BASE		0
#define PLAT_MTIMECMP_BASE	0


/*---=== IPIs ===---*/

/* Base addres for Machine-level Software Interrupt Device (MSWI)
 * in case of ACLINT, leave 0 for CLINT since it matches CLINT_BASE. */
#define PLAT_MSWI_BASE		0


/*---=== External Interrupts ===---*/

/* Base for PLIC or APLIC (only one should be set to non-zero) */
#define PLAT_PLIC_BASE		0xc000000
#define PLIC_MAX_PRIORITY	7
#define PLAT_APLIC_BASE		0
#define PLAT_NUM_IRQ_SOURCES	95

/* Base for M-mode IMSIC interrupt file for hart index 0 (set to 0 if not present)
 * Used with APLIC in MSI mode for AIA support, and IPIs over IMSIC */
#define PLAT_IMSIC_BASE		0


/**************\
* CORE OPTIONS *
\**************/
/* Hardcode the hart_id of the boot hart,
 * set it to -1 to use the boot lottery instead. */
#define PLAT_BOOT_HART_ID	-1

/* Set to 1 to use vectored traps on mtvec, 0 for
 * direct (single trap handler with dispatch table). */
#define PLAT_HART_VECTORED_TRAPS 1

/* Set to 1 to be used for sending IPIs via IMSIC,
 * leave it 0 to use MSWI instead. */
#define PLAT_IMSIC_IPI_EIID 0

/* Set to 1 to force APLIC direct mode, bypassing IMSIC */
#define PLAT_APLIC_FORCE_DIRECT 0

/* Quirks */
//#define PLAT_NO_WFI
//#define PLAT_QUIRK_WFI_EPC

/* Core platform part done, run checks before defining the
 * platform hart irq map, so that we don't define it
 * if PLAT_NO_IRQ is set. */
#include <platform/utils/platform_checks.h>

/* Make sure we only define this once, using NEED_HART_IRQ_MAP on init.c */
#if defined(_IRQ_H) && !defined(PLAT_NO_IRQ) && defined(NEED_HART_INTC_MAP)
DEFINE_PLATFORM_INTC_MAP({
	{ .hart_id = 0, .target = { .ctx_idx = 0 } },  /* Hart 0 -> Context 0 (M-mode) */
	{ .hart_id = 1, .target = { .ctx_idx = 2 } },  /* Hart 1 -> Context 2 (M-mode) */
	{ .hart_id = 2, .target = { .ctx_idx = 4 } },  /* Hart 2 -> Context 4 (M-mode) */
	{ .hart_id = 3, .target = { .ctx_idx = 6 } },  /* Hart 3 -> Context 6 (M-mode) */
});
#endif

/*************\
* PERIPHERALS *
\*************/

/*---=== UART ===---*/
#define PLAT_UART_BASE		0x10000000
#define PLAT_UART_CLOCK_HZ	3686400
#define PLAT_UART_BAUD_RATE	115200
#define PLAT_UART_REG_SHIFT	0
#define	PLAT_UART_SHIFTED_IO	0
#define PLAT_UART_IRQ		10

/*---=== VIRTIO Net ===---*/
#define PLAT_VIRTIO_NET_BASE_ADDR 0x10008000	/* QEMU virt default */

#endif /* _PLATFORM_H */