/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _CAPS_H
#define _CAPS_H

/* M-mode caps detected for free by early boot (bits [0..3] of m_caps) */
#include <platform/riscv/early_caps.h>

/*
 * M-mode capabilities probed by the test suite.
 *
 * These continue the m_caps word after the early-boot caps in early_caps.h
 * (bits [0..3]), so they start at bit 4 and must not collide with those.
 */
#define CAP_SMDBLTRP	BIT(4)
#define CAP_MBE		BIT(5)
#define CAP_SMMPM	BIT(6)
#define CAP_SMNPM	BIT(7)
#define CAP_SMSTATEEN	BIT(8)
#define CAP_SMCSRIND	BIT(9)
#define CAP_SMRNMI	BIT(10)
#define CAP_SMCTR	BIT(11)
#define CAP_SMAIA	BIT(12)
/* Not a named ISA extension: the AIA's IMSIC (MSI controller) hardware
 * component, detected via its M-mode CSR interface (mtopei). */
#define CAP_IMSIC	BIT(13)

/*
 * S-mode capabilities / extensions
 */
#define CAP_S		BIT(0)
#define CAP_H		BIT(1)
#define CAP_SSDBLTRP	BIT(2)
#define CAP_SBE		BIT(3)
#define CAP_SVPBMT	BIT(4)
#define CAP_SMCDELEG	BIT(5)
#define CAP_SSTC	BIT(6)
#define CAP_SSNPM	BIT(7)
#define CAP_SSSTATEEN	BIT(8)
#define CAP_SSCSRIND	BIT(9)
#define CAP_SV32	BIT(10)	/* RV32 only */
#define CAP_SV39	BIT(11)
#define CAP_SV48	BIT(12)
#define CAP_SV57	BIT(13)
#define CAP_SVNAPOT	BIT(14)
#define CAP_SVINVAL	BIT(15)
#define CAP_SVADU	BIT(16)
#define CAP_SSQOSID	BIT(17)
#define CAP_SSCOFPMF	BIT(18)
#define CAP_SSCTR	BIT(19)
#define CAP_SSCCFG	BIT(20)
#define CAP_SSAIA	BIT(21)

/*
 * Common caps / exts across privilege modes
 */
#define CAP_ZICFILP	BIT_ULL(0)
#define CAP_ZICFISS	BIT_ULL(1)
#define CAP_ZICBOM	BIT_ULL(2)
#define CAP_ZICBOZ	BIT_ULL(3)
#define CAP_ZKR		BIT_ULL(4)

/*
 * Capabilities / extensions that don't fit elsewhere
 */

/* From misa */
#define CAP_A		BIT(0)
#define CAP_B		BIT(1)
#define CAP_C		BIT(2)
#define CAP_D		BIT(3)
#define CAP_F		BIT(4)
#define CAP_M		BIT(6)
#define CAP_Q		BIT(7)
#define CAP_U		BIT(8)
#define CAP_V		BIT(9)
#define CAP_X		BIT(10)

/* U-mode extensions */
#define CAP_UBE		BIT(11)
#define CAP_ZICNTR	BIT(12)
#define CAP_ZIHPM	BIT(13)

/* Placeholder for the rest to keep track...
 *
 * Sub-extensions:
 * Zicsr, Zifencei (used to be part of I)
 * Zaamo, Zalrsc (part of A)
 * Zba/b/s (part of B)
 * Zmmul (part of M -M without div, for embedded-)
 *
 * Other extensions:
 * - Zihintpause -> Pause hint
 * Zifh/fhmin -> Half-precision floating point
 * Zi*nx -> float/double/half-double in x* regs
 * Zk* -> Scalar crypto (already tracking Zkr)
 * - Zkt -> Constant-time guarantees for crypto
 * Zbc, Zbk* -> Bitmanip for crypto
 * Zve* -> Vector for embedded
 * - Zicbop -> cache prefetch hints
 * Zvbb/c -> Bitmanip for vectors (extends V)
 * Zawrs -> Wait on reservation set (extends A)
 * - Ztso -> Total Store Ordering memory model
 * RV3264E -> Alternative base integer sets (mutex to RV32/64I)
 * Zca/b/d/e/f/mp/mt -> Code size reduction
 * Zihintntl -> Non-temporal locality hint (can't detect this)
 * Zvfh/fhmin -> Half-precision fp for vectors (extends V)
 * Zacas -> Atomic comparen and swap (extends A)
 * Zicond -> Conditional integer ops
 * - Svade -> Default behavior, mutex with Svadu
 * Zimop/Zicmop -> Maybe ops (used for CFI etc)
 * Zahba -> Half word / byte atomics (extends A)
 * Zfbf* / Zvbf* -> BF16
 * - Svvptc -> Auto-sfence.vma
 * Zilsd/Zclsd -> Load/store pair (for RV32)
 */

#ifndef __ASSEMBLER__
#include <stdint.h>

struct rvcaps {
	uint32_t m_caps;
	uint32_t s_caps;
	uint32_t r_caps;
	uint8_t num_hpmcounters;
	uint8_t num_pmp_rules;
	uint8_t pmp_granule_shift;	/* log2(granule bytes): 2 => 4-byte (NA4), 0 => no PMP */
	uint8_t num_triggers;		/* Sdtrig hardware triggers */
	uint8_t num_asid_bits;
	uint8_t vlenb_shift;	/* vlenb = 1 << vlenb_shift, range [4-13] */
	uint64_t z_caps;
};

#endif

#endif /* _CAPS_H */
