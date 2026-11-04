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
#define	PLAT_HART_FREQ		10000000

/*---=== Memory Layout ===---*/
#define KB	1024
#define MB	KB * 1024
#define GB	MB * 1024

/* Note: Use PLAT_SYSRAM_BASE/SIZE to mark the
 * DRAM region, e.g. in case program wants to do DMA
 * transfers. */

#define PLAT_ROM_BASE		0x20000000
#define PLAT_ROM_SIZE		256 * KB
#define PLAT_RAM_BASE		0x80000000
#define	PLAT_RAM_SIZE		2 * MB
#define	PLAT_STACK_SIZE		8 * KB

/* Optional: a fixed window for DMA descriptors/buffers, for any device whose
 * DMA engine can reach DRAM but not the program RAM above (e.g. when RAM lives
 * in boot SRAM). Drivers place their DMA state here instead of on the heap.
 * Leave both undefined to take DMA memory off the heap instead, which is only
 * valid when the RAM region is itself DMA-reachable. When set, PLAT_DMA_BASE
 * must be cache-line (64B) aligned and [PLAT_DMA_BASE, +PLAT_DMA_SIZE) must lie
 * inside the PLAT_SYSRAM_BASE/SIZE DRAM window without overlapping PLAT_RAM;
 * platform_checks.h enforces all of that. Put it at the top of DRAM so it stays
 * clear of firmware images loaded from the bottom. */
//#define PLAT_DMA_SIZE		64 * KB
//#define PLAT_DMA_BASE		(PLAT_SYSRAM_BASE + PLAT_SYSRAM_SIZE - PLAT_DMA_SIZE)

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

/* Width (bits) of the MMIO path to the 64-bit mtime/mtimecmp registers,
 * either 64 (default) or 32. Set to 32 on implementations whose MMIO bus
 * only decodes 32-bit accesses (device tree "clint,has-no-64bit-mmio"),
 * where a 64-bit access to these registers faults; mtimer.h then
 * reads/writes them as two 32-bit halves. */
//#define PLAT_MTIMER_MMIO_BITS	64

/* Read mtime from the architected read-only 'time' CSR (0xC01, the Zicntr
 * shadow of the memory-mapped mtime) instead of via MMIO. 1 (default) is the
 * standard, mode-agnostic way to read wall-clock time; set to 0 to force the
 * MMIO read path instead. mtimecmp is unaffected (always MMIO, per the width
 * above). */
//#define PLAT_MTIMER_USE_TIME_CSR 1

/* MTIMER frequency in Hz, set to 0 to disable */
#define	PLAT_MTIMER_FREQ	25000000

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
#define PLAT_APLIC_BASE		0
#define PLIC_MAX_PRIORITY	7
#define PLAT_NUM_IRQ_SOURCES	53

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

/* Never emit the WFI instruction (fall back to a pause()/spin). Use on
 * cores where WFI traps, is unimplemented in a harmful way, or otherwise
 * misbehaves instead of simply acting as a NOP. */
//#define PLAT_NO_WFI

/* Workaround for a hardware bug where WFI advances mepc by 8-12 bytes
 * instead of 4, so execution resumes past the following instruction(s) and
 * hangs/misbehaves. Pads the WFI with extra NOPs to absorb the overshoot. */
//#define PLAT_QUIRK_WFI_EPC

/* The hart sets misa.V but does NOT implement the ratified RVV 1.0 (e.g.
 * the T-Head C906, which has the incompatible 0.7.1 draft vector: a
 * different CSR map - no vstart/vcsr - and different instruction
 * encodings). With this set, hart_init_vpu() skips the RVV 1.0 reset
 * sequence and leaves the vector unit off (mstatus.VS = 0); otherwise that
 * sequence would hit an illegal-instruction trap (the vsetvli is an OP-V,
 * non-SYSTEM opcode, so it is not skippable) and hang the hart. */
//#define PLAT_QUIRK_NON_COMPLIANT_RVV

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

#endif /* _PLATFORM_H */