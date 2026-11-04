/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Milk-V Duo S (Sophgo SG2000 / CVITek CV181x family).
 *
 * Values below come from the board device tree
 * (cv181x_milkv_duos_sd.dts). We boot as the fip "MONITOR" image
 * (replacing OpenSBI's fw_dynamic) on the application C906 core, so our
 * image is loaded at, and runs from, the DRAM base (0x80000000). The
 * second C906 ("c906_1"/little core) and the 8051 run their own firmware
 * and never enter this image.
 *
 * Only the pieces we currently care about are described here: CLINT,
 * PLIC and the two enabled UARTs, plus the UART interrupt wiring. The
 * many CVITek-specific IP blocks in the DTS (VPU, ISP, codecs, ...) are
 * intentionally left out.
 */

#ifndef _PLATFORM_H
#define _PLATFORM_H

/******\
* CORE *
\******/

/*---=== HARTs ===---*/
/* The DTS declares a single RISC-V application cpu (cpu@0). The little
 * C906 that runs the RTOS payload is not part of this image. */
#define PLAT_MAX_HARTS		1

/* CPU cycle-counter (mcycle) frequency, used by the PLAT_TIMER_CYCLES clock.
 * The DTS cpu "clock-frequency" just mirrors the 25MHz timebase (wrong for
 * mcycle), but the cv181x_cooling node lists the real clk_cpu DVFS points -
 * the top one (dev-freqs[0] = 0x32a9f880) is 850MHz, the speed the boot
 * stages leave the big C906 running at. This only affects PLAT_TIMER_CYCLES
 * accuracy; wall-clock time uses the MTIMER below. */
#define	PLAT_HART_FREQ		850000000

/*---=== Memory Layout ===---*/
#define KB	1024
#define MB	KB * 1024
#define GB	MB * 1024

/* DRAM, from memory@80000000 (reg = <0x80000000 0x1fe00000>). The node ends
 * right below the rproc reserve (0x9fe00000+, the little C906's firmware), so
 * this whole window is ours. */
#define PLAT_DRAM_BASE		0x80000000
#define PLAT_DRAM_SIZE		0x1fe00000	/* 510 MB */

#define PLAT_ROM_SIZE		512 * KB
#define	PLAT_RAM_SIZE		2 * MB
#define	PLAT_STACK_SIZE		8 * KB

/* Put our image at the TOP of DRAM as <rom><ram>[DRAM end]: ROM
 * (.text/.rodata + .data load image) first, then RAM (.data/.bss/stack/heap)
 * filling the last bytes up to the end of DRAM. This leaves the whole bottom
 * of DRAM as one clean window for the application (PLAT_SYSRAM_* below). The
 * FSBL loads the MONITOR (us) at PLAT_ROM_BASE and jumps there, so the fip's
 * MONITOR_RUNADDR must match it (fip_replace_monitor.py defaults to this). */
#define PLAT_ROM_BASE		(PLAT_DRAM_BASE + PLAT_DRAM_SIZE - PLAT_RAM_SIZE - PLAT_ROM_SIZE)	/* 0x9fb80000 */
#define PLAT_RAM_BASE		(PLAT_ROM_BASE + PLAT_ROM_SIZE)					/* 0x9fc00000 */

/* Everything below our image is free for the application: DMA buffers,
 * NetBoot image staging, etc. (This reclaims the DTS reserved-memory regions
 * - framebuffer, rpmsg vrings - which only concern the vendor Linux stack we
 * replace; the live little-core firmware sits above us, outside this node.) */
#define	PLAT_SYSRAM_BASE	PLAT_DRAM_BASE					/* 0x80000000 */
#define	PLAT_SYSRAM_SIZE	(PLAT_ROM_BASE - PLAT_DRAM_BASE)		/* ~507 MB */

/* Number of PMP entries this hart implements (0 disables PMP support).
 * The C906 has a PMP, but our image runs from DRAM (not a write-protected
 * ROM), so the W^X story differs from the QEMU targets; leave it disabled
 * until the mapping is validated on real hardware. */
#define PLAT_PMP_REGIONS	0

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

/* SiFive-CLINT-compatible device: clint@74000000. */
#define PLAT_CLINT_BASE		0x74000000

/* The DTS marks this CLINT "clint,has-no-64bit-mmio": its MMIO bus only
 * decodes 32-bit accesses, so mtimecmp must be written as two 32-bit halves
 * (a 64-bit store faults). */
#define PLAT_MTIMER_MMIO_BITS	32

/* This C906 CLINT implements only msip + mtimecmp as MMIO; it does NOT
 * implement the mtime register (a load from CLINT+0xBFF8 faults). mtime is
 * read solely through the architected read-only 'time' CSR, which is the
 * SDK default (PLAT_MTIMER_USE_TIME_CSR) - so nothing to set here; it is
 * mandatory on this SoC rather than merely preferred. mtimecmp is still
 * written over MMIO (see above, the 'time' CSR is read-only). */

/* MTIMER frequency in Hz (cpus/timebase-frequency = 25MHz). */
#define	PLAT_MTIMER_FREQ	25000000

/* MTIME/MTIMECMP are part of the CLINT, derived from PLAT_CLINT_BASE. */
#define PLAT_MTIME_BASE		0
#define PLAT_MTIMECMP_BASE	0


/*---=== IPIs ===---*/

/* MSWI is part of the CLINT, leave 0 so it matches PLAT_CLINT_BASE. */
#define PLAT_MSWI_BASE		0


/*---=== External Interrupts ===---*/

/* PLIC: interrupt-controller@70000000 ("riscv,plic0"),
 * riscv,ndev = 0x65 (101), riscv,max-priority = 7. */
#define PLAT_PLIC_BASE		0x70000000
#define PLAT_APLIC_BASE		0
#define PLIC_MAX_PRIORITY	7
#define PLAT_NUM_IRQ_SOURCES	101

/* No IMSIC on this SoC. */
#define PLAT_IMSIC_BASE		0


/**************\
* CORE OPTIONS *
\**************/
/* Boot hart is picked by the lottery: the application core enters our
 * image but its mhartid is not guaranteed to be 0, so don't hardcode it. */
#define PLAT_BOOT_HART_ID	-1

/* C906 supports vectored mtvec. */
#define PLAT_HART_VECTORED_TRAPS 1

/* Set to 1 to be used for sending IPIs via IMSIC,
 * leave it 0 to use MSWI instead. */
#define PLAT_IMSIC_IPI_EIID 0

/* Set to 1 to force APLIC direct mode, bypassing IMSIC */
#define PLAT_APLIC_FORCE_DIRECT 0

/* Quirks */
//#define PLAT_NO_WFI
//#define PLAT_QUIRK_WFI_EPC

/* The C906 sets misa.V but implements the T-Head 0.7.1 draft vector, not
 * the ratified RVV 1.0: vstart/vcsr are absent and the vset/vector ops use
 * a different encoding. Our RVV 1.0 VPU init (hart_init_vpu) would fault -
 * the csrw/csrr get skipped as unimplemented, but the following vsetvli
 * (an OP-V, non-SYSTEM opcode) is not skippable and hangs the hart. Skip
 * VPU init entirely and leave the vector unit off (mstatus.VS = 0). */
#define PLAT_QUIRK_NON_COMPLIANT_RVV

/* The C906's DMA masters are not coherent with the D-cache, and this core
 * predates ratified Zicbom - it provides T-Head's custom cache ops instead
 * (same vintage as the non-compliant RVV above). Select that CMO backend for
 * the DMA cache-maintenance API (see platform/include/riscv/cache.h). */
#define PLAT_QUIRK_THEAD_CMO

/* Core platform part done, run checks before defining the
 * platform hart irq map, so that we don't define it
 * if PLAT_NO_IRQ is set. */
#include <platform/utils/platform_checks.h>

/* Make sure we only define this once, using NEED_HART_IRQ_MAP on init.c.
 * The C906 PLIC exposes the M-mode external context of the application
 * hart as context 0 (context 1 is its S-mode context, which the DTS wires
 * to Linux). We run in M-mode, so target context 0. */
#if defined(_IRQ_H) && !defined(PLAT_NO_IRQ) && defined(NEED_HART_INTC_MAP)
DEFINE_PLATFORM_INTC_MAP({
	{ .hart_id = 0, .target = { .ctx_idx = 0 } },  /* Hart 0 -> Context 0 (M-mode) */
});
#endif

/*************\
* PERIPHERALS *
\*************/

/*---=== UART ===---*/
/* serial@04140000 ("snps,dw-apb-uart"), the DTS console (stdout-path =
 * serial0). It is a DesignWare APB UART: 32-bit registers (reg-io-width =
 * 4), word-strided (reg-shift = 2), fed by a 25MHz reference clock
 * (clock-frequency = 0x17d7840). PLIC source 0x2c (44), level-high. */
#define PLAT_UART_BASE		0x04140000
#define PLAT_UART_CLOCK_HZ	25000000
#define PLAT_UART_BAUD_RATE	115200
#define PLAT_UART_REG_SHIFT	2
#define	PLAT_UART_SHIFTED_IO	1
#define PLAT_UART_IRQ		44

/* Select the DesignWare APB UART code paths in the shared 8250/16550
 * driver (busy-safe LCR programming, fractional DLF divisor, busy-detect
 * interrupt handling). */
#define PLAT_UART_DW_APB	1

/*---=== Ethernet ===---*/
/* ethernet@4070000 ("cvitek,ethernet"): a Synopsys DesignWare GMAC, the
 * dwmac-1000 generation (version register reads 0x1037 = v3.70a), driven by
 * the generic dwmac3 driver. The MAC's 100M port is wired over RMII to the
 * SoC's internal ePHY (powered up and calibrated in platform_init.c below).
 * PLIC source 0x1f (31), level-high - unused, the driver polls. */
#define PLAT_DWMAC3_BASE_ADDR	0x04070000
#define PLAT_DWMAC3_IRQ		31
/* CSR (application) clock feeding the MAC - eth_csrclk, a fixed 250MHz in the
 * board DTS. The driver derives the MDC/MDIO clock divider from it. */
#define PLAT_DWMAC3_CSR_CLOCK_HZ	250000000

#endif /* _PLATFORM_H */
