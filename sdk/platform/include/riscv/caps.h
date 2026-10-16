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

#include <platform/utils/bitfield.h>

/*
 * M-mode capabilities / extensions
 */
#define CAP_MCOUNTINHIBIT	BIT(0)	/* For pre 1.11 implementations */
#define CAP_SMDBLTRP	BIT(1)
#define CAP_MBE		BIT(2)
#define CAP_SMMPM	BIT(3)
#define CAP_SMNPM	BIT(4)
#define CAP_SMSTATEEN	BIT(5)
#define CAP_SMCSRIND	BIT(6)
#define CAP_SMEPMP	BIT(7)	/* Best effort check based on mseccfg.RLB */
#define CAP_SMCNTRPMF	BIT(8)
#define CAP_SMRNMI	BIT(9)
#define CAP_SMCTR	BIT(10)
#define CAP_SDTRIG	BIT(11)
#define CAP_SMAIA	BIT(12)

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

#define CAP_ZBA		BIT_ULL(5)		 /* Zba(part of B) */
#define CAP_ZBB		BIT_ULL(6)		 /* Zbb (part of B) */
#define CAP_ZBC		BIT_ULL(7)		 /* Zbc -> Bitmanip for crypto */
#define CAP_ZBS		BIT_ULL(8)		 /* Zbs (part of B) */
#define CAP_ZICOND	BIT_ULL(9)		 /* Zicond -> Conditional integer ops */
#define CAP_ZAWRS	BIT_ULL(10)		 /* Zawrs -> Wait on reservation set (extends A) */
#define CAP_ZACAS	BIT_ULL(11)		 /* Zacas -> Atomic comparen and swap (extends A) */
#define CAP_ZABHA	BIT_ULL(12)		 /* Zabha -> Half word / byte atomics (extends A) */
#define CAP_ZFH		BIT_ULL(13)		 /* Zfh -> Half-precision floating point */
#define CAP_ZFA		BIT_ULL(14)

#define CAP_ZICSR	BIT_ULL(15)      /* Zicsr (used to be part of I) */
#define CAP_ZIFENCEI	BIT_ULL(16)  /* Zifencei (used to be part of I) */
#define CAP_ZAAMO	BIT_ULL(17)		 /* Zaamo (part of A) */
#define CAP_ZALRSC	BIT_ULL(18)	 	 /* Zalrsc (part of A) */
#define CAP_ZMMUL	BIT_ULL(19)		 /* Zmmul (part of M -M without div, for embedded-) */

#define CAP_ZFINX	BIT_ULL(20)		 /* Zi*nx -> float/double/half-double in x* regs */
#define CAP_ZDINX	BIT_ULL(21)		 /* Zi*nx -> float/double/half-double in x* regs */
#define CAP_ZHINX	BIT_ULL(22)		 /* Zi*nx -> float/double/half-double in x* regs */
#define CAP_ZHINXMIN	BIT_ULL(23)	 /* Zi*nx -> float/double/half-double in x* regs */
#define CAP_ZFHMIN	BIT_ULL(24)		 /* Zfhmin -> Half-precision floating point */
 
#define CAP_ZBKB	BIT_ULL(25)		 /* Zk* -> Scalar crypto */
#define CAP_ZBKC	BIT_ULL(26)		 /* Zk* -> Scalar crypto */
#define CAP_ZBKX	BIT_ULL(27)		 /* Zk* -> Scalar crypto */
#define CAP_ZKND	BIT_ULL(28)		 /* Zk* -> Scalar crypto */
#define CAP_ZKNE	BIT_ULL(29)	 	 /* Zk* -> Scalar crypto */
#define CAP_ZKNH	BIT_ULL(30)		 /* Zk* -> Scalar crypto */
#define CAP_ZKSED	BIT_ULL(31)		 /* Zk* -> Scalar crypto */
#define CAP_ZKSH	BIT_ULL(32)		 /* Zk* -> Scalar crypto */

#define CAP_ZCA		BIT_ULL(33)		 /* Zca/b/d/e/f/mp/mt -> Code size reduction */
#define CAP_ZCB		BIT_ULL(34)
#define CAP_ZCD		BIT_ULL(35)
#define CAP_ZCE		BIT_ULL(36)
#define CAP_ZCF		BIT_ULL(37)		 /* RV32 only */
#define CAP_ZCMP	BIT_ULL(38)
#define CAP_ZCMT	BIT_ULL(39)

 
#define CAP_ZIMOP	BIT_ULL(40)		 /* Zimop -> Maybe ops (used for CFI etc) */
#define CAP_ZCMOP	BIT_ULL(41)		 /* Zcmop -> Maybe ops (used for CFI etc) */

 
#define CAP_ZFBFMIN	BIT_ULL(42)		/* Zfbf* / Zvbf* -> BF16 */

#define CAP_ZVE32X	BIT_ULL(43)		 /* Zve* -> Vector for embedded */
#define CAP_ZVE32F	BIT_ULL(44)		 /* Zve* -> Vector for embedded */
#define CAP_ZVE64X	BIT_ULL(45)		 /* Zve* -> Vector for embedded */
#define CAP_ZVE64F	BIT_ULL(46)		 /* Zve* -> Vector for embedded */
#define CAP_ZVE64D	BIT_ULL(47)		 /* Zve* -> Vector for embedded */

#define CAP_ZVBB	BIT_ULL(48)		 /* Zvbb -> Bitmanip for vectors (extends V) */
#define CAP_ZVBC	BIT_ULL(49)		 /* Zvbc -> Bitmanip for vectors (extends V) */

#define CAP_ZVFH	BIT_ULL(50)		 /* Zvfh -> Half-precision fp for vectors (extends V) */
#define CAP_ZVFHMIN	BIT_ULL(51)		 /* Zvfhmin -> Half-precision fp for vectors (extends V) */
#define CAP_ZVFBFMIN	BIT_ULL(52)	 /* Zfbf* / Zvbf* -> BF16 */
#define CAP_ZVFBFWMA	BIT_ULL(53)	 /* Zfbf* / Zvbf* -> BF16 */
 
#define CAP_ZILSD	BIT_ULL(54)		 /* RV32 only */ /* Zilsd -> Load/store pair (for RV32) */
#define CAP_ZCLSD	BIT_ULL(55)		 /* RV32 only */ /* Zclsd -> Load/store pair (for RV32) */
 
/*
 * Capabilities / extensions that don't fit elsewhere
 */

/* From misa */
#define CAP_A		BIT(0)
#define CAP_B		BIT(1)
#define CAP_C		BIT(2)
#define CAP_D		BIT(3)
#define CAP_E		BIT(4)
#define CAP_F		BIT(5)
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
 * - Zihintpause -> Pause hint
 * - Zkt -> Constant-time guarantees for crypto
 * - Zicbop -> cache prefetch hints
 * - Ztso -> Total Store Ordering memory model
 * - Zihintntl -> Non-temporal locality hint (can't detect this)
 * - Svade -> Default behavior, mutex with Svadu
 * - Svvptc -> Auto-sfence.vma
 */

#ifndef __ASSEMBLER__
#include <stdint.h>

struct rvcaps {
	uint32_t m_caps;
	uint32_t s_caps;
	uint32_t r_caps;
	uint8_t num_hpmcounters;
	uint8_t num_pmp_rules;
	uint8_t num_asid_bits;
	uint8_t vlenb_shift;	/* vlenb = 1 << vlenb_shift, range [4-13] */
	uint64_t z_caps;
	uint64_t z_broken;
};

#endif

#endif /* _CAPS_H */