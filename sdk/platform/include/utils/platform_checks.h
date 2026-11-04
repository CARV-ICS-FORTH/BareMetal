/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef _PLATFORM_H
#ifndef _PLATFORM_CHECKS_H
#define _PLATFORM_CHECKS_H

/*************\
* CORE CHECKS *
\*************/
/* We at least need the hart's frequency for the cyclecount timer */
#if !defined(PLAT_HART_FREQ) || (PLAT_HART_FREQ == 0)
	#error "PLAT_HART_FREQ not defined"
#endif

/*
 * Note: ACLINT is backwards compatible with CLINT, basically
 * CLINT is MSWI + MTIMER, so we treat it like a specific
 * configuration of ACLINT.
 */

/* Checks for MTIMER (if we have one) */
#if defined(PLAT_MTIMER_FREQ) && (PLAT_MTIMER_FREQ > 0)
	/* CLINT: MTIMER/MTIMECMP are part of CLINT */
	#if defined(PLAT_CLINT_BASE) && (PLAT_CLINT_BASE > 0)
		#if defined(PLAT_MTIME_BASE)
			#undef PLAT_MTIME_BASE
		#endif
		#define	PLAT_MTIME_BASE	(PLAT_CLINT_BASE + 0xBFF8)
		#if defined(PLAT_MTIMECMP_BASE)
			#undef PLAT_MTIMECMP_BASE
		#endif
		#define PLAT_MTIMECMP_BASE (PLAT_CLINT_BASE + 0x4000)
	#endif

	/* This now covers both CLINT/ACLINT */
	#if !defined(PLAT_MTIME_BASE) || (PLAT_MTIME_BASE == 0) || \
	    !defined(PLAT_MTIMECMP_BASE) || (PLAT_MTIMECMP_BASE == 0)
			#error "MTIMER registers not set"
	#else
		#define PLAT_HAS_MTIMER
	#endif

	/* Width (in bits) of the MMIO path to the 64-bit mtime/mtimecmp
	 * registers. Some MTIMER implementations only decode <=32-bit MMIO
	 * (device tree "clint,has-no-64bit-mmio"); set PLAT_MTIMER_MMIO_BITS to
	 * 32 there so mtimer.h accesses the two halves separately. Defaults to a
	 * native 64-bit access. */
	#if !defined(PLAT_MTIMER_MMIO_BITS)
		#define PLAT_MTIMER_MMIO_BITS 64
	#elif (PLAT_MTIMER_MMIO_BITS != 32) && (PLAT_MTIMER_MMIO_BITS != 64)
		#error "PLAT_MTIMER_MMIO_BITS must be 32 or 64"
	#endif

	/* Read mtime from the 'time' CSR instead of MMIO (see mtimer.h). On by
	 * default: it is the standard, mode-agnostic way to read wall-clock time
	 * and works even where the mtime MMIO register is absent. A target may
	 * set it to 0 to force the MMIO read path. */
	#if !defined(PLAT_MTIMER_USE_TIME_CSR)
		#define PLAT_MTIMER_USE_TIME_CSR 1
	#elif (PLAT_MTIMER_USE_TIME_CSR != 0) && (PLAT_MTIMER_USE_TIME_CSR != 1)
		#error "PLAT_MTIMER_USE_TIME_CSR must be 0 or 1"
	#endif
#else
	#define PLAT_NO_MTIMER
#endif

/* Check for MSWI */
/* If we have a CLINT it contains an MSWI */
#if defined(PLAT_CLINT_BASE) && (PLAT_CLINT_BASE > 0)
	#if defined(PLAT_MSWI_BASE)
		#undef PLAT_MSWI_BASE
	#endif
	#define PLAT_MSWI_BASE	PLAT_CLINT_BASE
#endif
#if !defined(PLAT_MSWI_BASE) || (PLAT_MSWI_BASE == 0)
	#define PLAT_NO_MSWI
#else
	#define PLAT_HAS_MSWI
#endif

/* Check for IMSIC */
#if defined(PLAT_IMSIC_BASE) && (PLAT_IMSIC_BASE > 0)
	#define PLAT_HAS_IMSIC
#else
	#define PLAT_NO_IMSIC
#endif

/* No MSWI nor IMSIC -> no IPIs */
#if defined(PLAT_NO_MSWI) && defined(PLAT_NO_IMSIC)
	#define PLAT_NO_IPI
#endif

/*
 * Note: We only support one platform interrupt controller
 * type to keep things simple.
 */
#if defined(PLAT_PLIC_BASE) && (PLAT_PLIC_BASE > 0)
	#define PLAT_HAS_PLIC
	#if !defined(PLIC_MAX_PRIORITY) || (defined(PLIC_MAX_PRIORITY) && (PLIC_MAX_PRIORITY == 0))
		#define PLIC_MAX_PRIORITY 1
	#endif
#endif

#if defined(PLAT_APLIC_BASE) && (PLAT_APLIC_BASE > 0)
	#define PLAT_HAS_APLIC
#endif

#if defined(PLAT_HAS_PLIC) && defined(PLAT_HAS_APLIC)
	#error "Only one of PLAT_PLIC/APLIC_BASE must be set"
#endif

#if defined(PLAT_HAS_PLIC) || defined(PLAT_HAS_APLIC)
	#if !defined(PLAT_NUM_IRQ_SOURCES) || (PLAT_NUM_IRQ_SOURCES == 0)
		#error "Invalid PLAT_NUM_IRQ_SOURCES for PLIC/APLIC"
	#endif
#endif

/* No PLIC or APLIC -> no IRQs
 * (for now, until we add IMSIC support) */
#if !defined(PLAT_PLIC_BASE) && !defined(PLAT_APLIC_BASE)
	#ifndef PLAT_NO_IRQ
		#define PLAT_NO_IRQ
	#endif
#endif

/*********************\
* CORE OPTIONS CHECKS *
\*********************/

#ifndef PLAT_HART_VECTORED_TRAPS
	#define PLAT_HART_VECTORED_TRAPS 1
#endif

#if !defined(PLAT_BOOT_HART_ID)
	#define PLAT_BOOT_HART_ID -1
#endif

#if defined(PLAT_IMSIC_IPI_EIID) && (PLAT_IMSIC_IPI_EIID > 0) && !defined(PLAT_HAS_IMSIC)
	#warning "PLAT_IMSIC_IPI_EIID was set but there is no IMSIC"
	#undef PLAT_IMSIC_IPI_EIID
	#define PLAT_IMSIC_IPI_EIID 0
#elif !defined(PLAT_IMSIC_IPI_EIID)
	#define PLAT_IMSIC_IPI_EIID 0
#elif (PLAT_IMSIC_IPI_EIID > 1)
	#undef PLAT_IMSIC_IPI_EIID
	#define PLAT_IMSIC_IPI_EIID 1	/* Always use EIID 1 since it has the higher priority */
#endif

#if defined(PLAT_APLIC_FORCE_DIRECT) && (PLAT_APLIC_FORCE_DIRECT > 0) && \
    !(defined(PLAT_HAS_APLIC) && defined(PLAT_HAS_IMSIC))
	#warning "PLAT_APLIC_FORCE_DIRECT defined but there is no APLIC + IMSIC"
	#undef PLAT_APLIC_FORCE_DIRECT
	#define PLAT_APLIC_FORCE_DIRECT 0
#endif

#if defined(PLAT_HAS_IMSIC) && ((PLAT_APLIC_FORCE_DIRECT > 0) || defined(PLAT_HAS_PLIC))
	#define PLAT_BYPASS_IMSIC
#endif

/*******************\
* PERIPHERAL CHECKS *
\*******************/

#if defined(PLAT_UART_BASE) && (PLAT_UART_BASE > 0)
	#define _UART_DIVISOR ((PLAT_UART_CLOCK_HZ) / ((PLAT_UART_BAUD_RATE) << 4))
	#if (_UART_DIVISOR > 65535)
		#error "UART divisor overflow: PLAT_UART_CLOCK_HZ / (PLAT_UART_BAUD_RATE * 16) must be <= 65535"
	#elif (_UART_DIVISOR < 1)
		#error "UART divisor is zero: PLAT_UART_CLOCK_HZ too low for PLAT_UART_BAUD_RATE"
	#endif
#endif

/****************\
* DMA REGION CHECKS *
\****************/

/* PLAT_DMA_BASE/SIZE optionally carve a fixed DMA descriptor/buffer window out
 * of DRAM, for devices whose DMA engine can only reach DRAM while the program
 * RAM lives elsewhere (e.g. boot SRAM). When a target defines it, validate the
 * window here once, for every driver that uses it: it must be cache-line
 * aligned, fit inside the DRAM/SYSRAM window and stay clear of the program RAM
 * region. Per-device checks (does my state fit PLAT_DMA_SIZE, is it within the
 * device's own DMA address range) belong in the driver. */
#if defined(PLAT_DMA_BASE)
	#if !defined(PLAT_DMA_SIZE) || (PLAT_DMA_SIZE == 0)
		#error "PLAT_DMA_BASE needs a non-zero PLAT_DMA_SIZE"
	#endif
	#if !defined(PLAT_SYSRAM_BASE) || !defined(PLAT_SYSRAM_SIZE)
		#error "PLAT_DMA_BASE needs PLAT_SYSRAM_BASE/SIZE (the DMA-capable DRAM window)"
	#endif
	#if (PLAT_DMA_BASE % 64) != 0
		#error "PLAT_DMA_BASE must be 64-byte (cache-line) aligned"
	#endif
	#if (PLAT_DMA_BASE < PLAT_SYSRAM_BASE) || \
	    ((PLAT_DMA_BASE + PLAT_DMA_SIZE) > (PLAT_SYSRAM_BASE + PLAT_SYSRAM_SIZE))
		#error "PLAT_DMA region must lie within the DRAM/SYSRAM window"
	#endif
	#if defined(PLAT_RAM_BASE) && defined(PLAT_RAM_SIZE) && \
	    ((PLAT_DMA_BASE) < (PLAT_RAM_BASE + PLAT_RAM_SIZE)) && \
	    ((PLAT_DMA_BASE + PLAT_DMA_SIZE) > (PLAT_RAM_BASE))
		#error "PLAT_DMA region must not overlap the program RAM region"
	#endif
#endif

#endif /* _PLATFORM_CHECKS_H */
#endif /* _PLATFORM_H */