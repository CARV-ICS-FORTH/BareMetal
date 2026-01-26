/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if __riscv_xlen != 64
#error "This only works on RV64 !"
#endif

#ifndef __riscv_zicsr
#error "This code requires Zicsr !"
#endif

#include <platform/riscv/csr.h>
#include <platform/riscv/caps.h>
#include <platform/utils/asm_macros.h>
#include <target_config.h>

/***************\
* Helper macros *
\***************/

/*
 * Returns from function if field is not in
 * cached MISA in a1 (passed as function arg).
 */
.macro RET_IF_NOT_IN_MISA field
	li	t0, \field
	and	t0, a1, t0
	bnez	t0, 1f
	ret
1:
.endm

/* Convenience macros for accessing fields on
 * hart_state (expected at a0 as a function arg) */
.macro LOAD_ERRNO reg
	lw	\reg, 20(a0)
.endm

.macro STORE_ERRNO reg
	sw	\reg, 20(a0)
.endm

.macro LOAD_CAPS reg
	ld	\reg, (a0)
.endm

.macro STORE_CAPS reg
	sd	\reg, (a0)
.endm

.macro SET_CAP_ON_SUCCESS cap
	LOAD_ERRNO t0
	bnez	t0, 1f
	LOAD_CAPS t1
	li	t2, \cap
	or	t3, t1, t2
	STORE_CAPS t3
1:
	STORE_ERRNO zero
.endm

/**************\
* Entry points *
\**************/

/*
 * Initializes FPU if F/D extension is present
 * a0: pointer to hart state
 * a1: cached misa
 */
FUNC_START hart_init_fpu
	RET_IF_NOT_IN_MISA CSR_MISA_F | CSR_MISA_D

	/* Enable F in mstatus */
	li	t0, CSR_MSTATUS_F_INIT
	csrs	mstatus, t0

.option push
.option arch, +f
	/* Reset FPU state */
	csrw	fcsr, zero
	fmv.s.x f0, zero
	fmv.s.x f1, zero
	fmv.s.x f2, zero
	fmv.s.x f3, zero
	fmv.s.x f4, zero
	fmv.s.x f5, zero
	fmv.s.x f6, zero
	fmv.s.x f7, zero
	fmv.s.x f8, zero
	fmv.s.x f9, zero
	fmv.s.x f10, zero
	fmv.s.x f11, zero
	fmv.s.x f12, zero
	fmv.s.x f13, zero
	fmv.s.x f14, zero
	fmv.s.x f15, zero
	fmv.s.x f16, zero
	fmv.s.x f17, zero
	fmv.s.x f18, zero
	fmv.s.x f19, zero
	fmv.s.x f20, zero
	fmv.s.x f21, zero
	fmv.s.x f22, zero
	fmv.s.x f23, zero
	fmv.s.x f24, zero
	fmv.s.x f25, zero
	fmv.s.x f26, zero
	fmv.s.x f27, zero
	fmv.s.x f28, zero
	fmv.s.x f29, zero
	fmv.s.x f30, zero
	fmv.s.x f31, zero
.option pop

	/* Since we touched fp state, it's now marked
	 * as dirty, re-set to init. */
	li	t0, CSR_MSTATUS_F_CLEAN
	csrc	mstatus, t0
	ret
FUNC_END hart_init_fpu

/*
 * Initializes VPU if V extension is present
 * a0: pointer to hart state
 * a1: cached misa
 */
FUNC_START hart_init_vpu
	RET_IF_NOT_IN_MISA CSR_MISA_V

	/* Enable VPU on mstatus */
	li	t0, CSR_MSTATUS_V_INIT
	csrs	mstatus, t0

.option push
.option arch, +v
	/* Reset VPU state */
	/* Note: vtype and vl are only writable via vsetvli instruction
	 * not through csrw, vlenb is read-only */
	csrw	vstart, zero
	csrs	vcsr, zero
	vsetvli	t1, x0, e8, m8, ta, ma
	vmv.v.i	v0, 0
	vmv.v.i	v8, 0
	vmv.v.i	v16, 0
	vmv.v.i	v24, 0
.option pop

	/* Since we touched v state, it's now marked
	 * as dirty, re-set to init. */
	li	t0, CSR_MSTATUS_V_CLEAN
	csrc	mstatus, t0
	ret
FUNC_END hart_init_vpu

/*
 * Initialize Counters on M-mode
 * a0: pointer to hart_state
 */
FUNC_START hart_init_counters
	STORE_ERRNO zero

	/* Note that this isn't Zicntr/Zihpm, those
	 * are for U-mode not M-mode, here we are
	 * dealing with counter setup on M-mode and
	 * the CSRs here are mandatory.
	 * 
	 * More infos on that here:
	 * https://github.com/riscv/riscv-isa-manual/issues/560
	 */

	/* First inhibit all counters, note that we may get
	 * an error here if we are on a hart implementing
	 * priv. spec < 1.11 (the first ratified version),
	 * in which mcountinhibit was introduced. */
	li	t0, CSR_MCOUNTINHIBIT_INIT
	csrw	CSR_MCOUNTINHIBIT, t0
	SET_CAP_ON_SUCCESS	CAP_MCOUNTINHIBIT

	/* Set all hpcounters to "no event", these are
	 * WARL so we are not checking anything here,
	 * just making sure we have an "empty" initial
	 * configuration. These were introduced in priv.
	 * spec 1.9.1 so I assume everyone implements them
	 * as expected. */
	csrw	CSR_MHPMEVENT(3), zero
	csrw	CSR_MHPMEVENT(4), zero
	csrw	CSR_MHPMEVENT(5), zero
	csrw	CSR_MHPMEVENT(6), zero
	csrw	CSR_MHPMEVENT(7), zero
	csrw	CSR_MHPMEVENT(8), zero
	csrw	CSR_MHPMEVENT(9), zero
	csrw	CSR_MHPMEVENT(10), zero
	csrw	CSR_MHPMEVENT(11), zero
	csrw	CSR_MHPMEVENT(12), zero
	csrw	CSR_MHPMEVENT(13), zero
	csrw	CSR_MHPMEVENT(14), zero
	csrw	CSR_MHPMEVENT(15), zero
	csrw	CSR_MHPMEVENT(16), zero
	csrw	CSR_MHPMEVENT(17), zero
	csrw	CSR_MHPMEVENT(18), zero
	csrw	CSR_MHPMEVENT(19), zero
	csrw	CSR_MHPMEVENT(20), zero
	csrw	CSR_MHPMEVENT(21), zero
	csrw	CSR_MHPMEVENT(22), zero
	csrw	CSR_MHPMEVENT(23), zero
	csrw	CSR_MHPMEVENT(24), zero
	csrw	CSR_MHPMEVENT(25), zero
	csrw	CSR_MHPMEVENT(26), zero
	csrw	CSR_MHPMEVENT(27), zero
	csrw	CSR_MHPMEVENT(28), zero
	csrw	CSR_MHPMEVENT(29), zero
	csrw	CSR_MHPMEVENT(30), zero
	csrw	CSR_MHPMEVENT(31), zero

	/* Disable per-mode filtering for mcycle/minstret.
	 * That's consistent with zeroing the inhibit bits
	 * om mhpmevent above, introduced by Sscofpmf.
	 * Note that these CSRs are optional, part of the
	 * Smcntrpmf extension. Also note that this extension
	 * may be implemented regardless of the existence of
	 * sub-M modes or Zicntr (TODO: clarify that in the
	 * spec). */
	csrw	CSR_MCYCLECFG, zero
	csrw	CSR_MINSTRETCFG, zero
	SET_CAP_ON_SUCCESS	CAP_SMCNTRPMF

	ret
FUNC_END hart_init_counters

FUNC_START hart_init_sdtrig
	STORE_ERRNO zero

	/* Clear hardware triggers (check out
	 * debug spec) */

	/* Start from -1 so that the loop starts from 0 */
	li	t0, ~0
	2:
		addi	t0, t0, 1
		csrw	tselect, t0	/* Select next trigger */
		csrw	tdata1, zero	/* Set trigger type to zero */
		csrr	t1, tselect	/* Read back tselect and verify the trigger
					 * we wrote was a valid one, if not we are done. */
		beq	t0, t1, 2b
	SET_CAP_ON_SUCCESS CAP_SDTRIG
	ret
FUNC_END hart_init_sdtrig