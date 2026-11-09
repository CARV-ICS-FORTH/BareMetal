/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _MTIMER_H
#define _MTIMER_H
#include <target_config.h>	/* For PLAT_NO_MTIMER, PLAT_MTIME/CMP_BASE */

#ifndef PLAT_NO_MTIMER
/*
 * The RISC-V Privilege spec. defines two 64bit memory mapped registers
 * (mtime and mtimecmp), to provide a basic M-mode timer facility. There
 * is also a U-mode time{h} CSR that's a shadow copy of mtime, and may be
 * implemented in hw (where the hart does an mmio access to mtime), or
 * in sw through trap-and-emulate. The same mtime register is expected to
 * be shared among multiple harts (and possibly among all harts), using a
 * global wall-clock (so different mtime registers may have different
 * values but they increment at the same frequency and can be synced
 * through software) and mtimecmp is defined per-hart.
 *
 * The official RISC-V specification of such a device is defined in
 * the ACLINT specification and it's called MTIMER:
 * https://github.com/riscv/riscv-aclint/blob/main/riscv-aclint.adoc
 *
 * Note that MTIMER is backwards compatible with SiFive's CLINT so the
 * same code can be used for both (CLINT was the de-facto standard
 * before the official ACLINT specification got ratified).
 *
 * In ase of multiple MTIMER instances, or some other facility that
 * provides mtime/mtimecmp, platforms may override MTIME_BASE and
 * MTIMECMP_BASE(_hartid) macros, and implement an init function in
 * platform_init, if needed.
 */
#include <stdint.h>			/* For typed integers */
#include <platform/riscv/mmio.h>	/* For register access */
#include <platform/riscv/csr.h>		/* For csr_read/set_bits/clear_bits() */

/* MTIMER uses direct addresses since MTIME and MTIMECMP can be at different bases */
#define MTIME_BASE		((uint64_t*)((uintptr_t)(PLAT_MTIME_BASE)))
#define MTIMECMP_ADDR(_hartid)	((uint64_t*)((uintptr_t)(PLAT_MTIMECMP_BASE + ((_hartid) * 8))))

static inline uint_fast32_t
mtimer_get_freq()
{
	return (uint_fast32_t) PLAT_MTIMER_FREQ;
}

/*
 * mtime/mtimecmp are 64-bit registers. On implementations whose MMIO bus
 * only decodes 32-bit accesses (PLAT_MTIMER_MMIO_BITS == 32) a 64-bit load/
 * store faults, so they must be accessed as two 32-bit halves - the way an
 * RV32 hart always would. The helpers below hide that behind the configured
 * access width.
 *
 * PLAT_MTIMER_USE_TIME_CSR: read mtime from the architected read-only 'time'
 * CSR (0xC01, the Zicntr shadow of the memory-mapped mtime) instead of via
 * MMIO. This is the standard, mode-agnostic way to read wall-clock time (one
 * instruction, no bus access). mtimecmp is unaffected - it has no CSR form and
 * is always written over MMIO, governed by PLAT_MTIMER_MMIO_BITS.
 */
static inline uint64_t
mtimer_get_num_ticks() {
#if (PLAT_MTIMER_USE_TIME_CSR)
	return csr_read(CSR_TIME);
#elif (PLAT_MTIMER_MMIO_BITS == 32)
	/* Read the halves separately, retrying if the low half rolled over
	 * into the high half between the two reads. */
	volatile uint32_t *mt = (volatile uint32_t *)MTIME_BASE;
	uint32_t hi, lo;
	do {
		hi = read32(&mt[1]);
		lo = read32(&mt[0]);
	} while (hi != read32(&mt[1]));
	return ((uint64_t)hi << 32) | lo;
#else
	return read64(MTIME_BASE);
#endif
}

/* Program this hart's mtimecmp, honouring the configured MMIO width. */
static inline void
mtimer_set_mtimecmp(uint64_t hart_id, uint64_t value)
{
#if (PLAT_MTIMER_MMIO_BITS == 32)
	volatile uint32_t *cmp = (volatile uint32_t *)MTIMECMP_ADDR(hart_id);
	/* Raise the high half to all-ones first so the intermediate value is
	 * never earlier than either the old or the new target, avoiding a
	 * spurious timer interrupt while the two halves are inconsistent. */
	write32(&cmp[1], 0xFFFFFFFFU);
	write32(&cmp[0], (uint32_t)value);
	write32(&cmp[1], (uint32_t)(value >> 32));
#else
	write64(MTIMECMP_ADDR(hart_id), value);
#endif
}

static inline void
mtimer_enable_irq(void)
{
	csr_set_bits(CSR_MIE, (1 << INTR_MACHINE_TIMER));
}

static inline void
mtimer_disable_irq(void)
{
	csr_clear_bits(CSR_MIE, (1 << INTR_MACHINE_TIMER));
}

static inline void
mtimer_arm_after_ticks(uint64_t cycles)
{
	uint64_t hart_id = csr_read(CSR_MHARTID);
	uint64_t current_count = mtimer_get_num_ticks();
	mtimer_set_mtimecmp(hart_id, current_count + cycles);
}

/* Arm mtimecmp with an ABSOLUTE mtime value. Don't pass it a duration: mtime
 * isn't guaranteed to start from 0 (e.g. the C906 CLINT's free-running
 * counter), use mtimer_arm_after_ticks() for a delay from now. */
static inline void
mtimer_arm_at(uint64_t cycles)
{
	uint64_t hart_id = csr_read(CSR_MHARTID);
	mtimer_set_mtimecmp(hart_id, cycles);
}

static inline void
mtimer_disarm(void) {
	uint64_t hart_id = csr_read(CSR_MHARTID);
	mtimer_set_mtimecmp(hart_id, (uint64_t) -1);
}

#endif /* PLAT_NO_MTIMER */
#endif /* _MTIMER_H */