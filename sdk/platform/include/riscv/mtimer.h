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

static inline uint64_t
mtimer_get_num_ticks() {
	return read64(MTIME_BASE);
}

static inline void
mtimer_reset_num_ticks()
{
	write64(MTIME_BASE, 0);
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
	write64(MTIMECMP_ADDR(hart_id), current_count + cycles);
}

static inline void
mtimer_arm_at(uint64_t cycles)
{
	uint64_t hart_id = csr_read(CSR_MHARTID);
	write64(MTIMECMP_ADDR(hart_id), cycles);
}

static inline void
mtimer_disarm(void) {
	uint64_t hart_id = csr_read(CSR_MHARTID);
	write64(MTIMECMP_ADDR(hart_id), (uint64_t) -1);
}

#endif /* PLAT_NO_MTIMER */
#endif /* _MTIMER_H */