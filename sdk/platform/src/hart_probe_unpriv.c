/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Probing of unprivileged extensions: those that only add instructions and
 * are detected by executing them (as opposed to the extensions in
 * hart_probe.c, that are detected through CSRs from the privileged spec).
 * The instructions themselves live in riscv/ext_blocks.h.
 */

#include <platform/riscv/caps.h>	/* For CAP_* macros */
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/riscv/z_ext_blocks.h>	/* Per-extension instruction blocks */
#include <platform/riscv/hart.h>	/* For hart_state and definitions */
#include <platform/utils/utils.h>	/* For DBG() */
#include <stdbool.h>			/* For bool */
#include <stdint.h>

/*****************************************\
* Instruction-only Z* extension probing   *
\*****************************************/

/*
 * Extensions that add instructions but no CSRs can only be detected by
 * executing them. Each probe runs *several* instructions of the extension (an
 * implementation that decodes one of them but not the rest isn't compliant, and
 * we want to notice), with HS_FLAG_PROBING set so that the exception handler
 * skips whatever traps and records ENOSYS in hs->error instead of treating it as
 * fatal. The capability is only reported if none of them trapped.
 *
 * Each block also computes a result from fixed or well-known operands, which
 * is compared against a predefined value (a C twin for the scalar blocks, a
 * constant for the rest, see ext_blocks.h). A probe that ran to completion but
 * got a wrong result is reported as present in z_caps and also flagged in
 * z_broken, so a broken extension still shows up in the capability list.
 *
 * The assembler encodes the instructions (.option arch) so the blocks work
 * whatever -march the SDK is built with, and compressed instructions are
 * disabled except where a block emits them explicitly.
 */

static inline void
hart_probe_insn_begin(struct hart_state *hs)
{
	hs->error = 0;
	hart_set_flags(hs, HS_FLAG_PROBING);
	__asm__ volatile ("" ::: "memory");
}

/* Returns true if every instruction executed */
static inline bool
hart_probe_insn_end(struct hart_state *hs)
{
	__asm__ volatile ("" ::: "memory");
	hart_clear_flags(hs, HS_FLAG_PROBING);
	bool ok = (hs->error == 0);
	hs->error = 0;
	return ok;
}


/*
 * Floating point probes: the FPU must be on (mstatus.FS != Off) or everything
 * traps as illegal, and we must leave no trace on the FP state (flags raised by
 * the blocks, the registers we use, and the FS field itself, which becomes Dirty
 * after the first FP instruction). The FP registers used are declared as
 * clobbered so the compiler saves what it needs.
 */
struct fp_probe_state {
	uint64_t fs;
	uint64_t fcsr;
};

static void
hart_probe_fp_begin(struct fp_probe_state *fps)
{
	fps->fs = csr_read(CSR_MSTATUS) & FIELD_GET_MASK(CSR_MSTATUS_FS);
	csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_F_INIT);	/* Off -> Initial, otherwise no change */
	__asm__ volatile (".option push\n\t.option arch, +f\n\tfrcsr %0\n\t.option pop"
			  : "=r" (fps->fcsr));
}

static void
hart_probe_fp_end(struct fp_probe_state *fps)
{
	__asm__ volatile (".option push\n\t.option arch, +f\n\tfscsr %0\n\t.option pop"
			  :: "r" (fps->fcsr));
	csr_clear_bits(CSR_MSTATUS, FIELD_GET_MASK(CSR_MSTATUS_FS));
	csr_set_bits(CSR_MSTATUS, fps->fs);
}

/*
 * Probe generators. All of them have the same shape: gate on the misa bits the
 * extension depends on, run the block with HS_FLAG_PROBING set (a trap means the
 * extension is absent), then compare the result. The comparison is plain C,
 * outside the probing window.
 */

/* Pair-driven blocks (EXT_BLOCK_Z*(a, b, x) + C twin): Zba Zbb Zbc Zbs Zicond Zmmul
 * Zbkb Zbkc Zbkx Zknh Zksh, and Zimop whose "twin" is a constant. */
#define HART_PROBE_CHECKED(fn, cap, block, ref)				\
static void								\
fn(struct hart_state *hs)						\
{									\
	struct rvcaps *caps = hs->caps;					\
	bool wrong = false;						\
									\
	for (unsigned i_ = 0; i_ < EXT_NPAIRS; i_++) {			\
		xu a_, b_, x_ = 0;					\
									\
		ext_pair(i_, &a_, &b_);					\
		hart_probe_insn_begin(hs);				\
		block(a_, b_, x_);					\
		if (!hart_probe_insn_end(hs))				\
			return;		/* trapped, not present */	\
		if (x_ != ref(a_, b_))					\
			wrong = true;					\
	}								\
	caps->z_caps |= (cap);						\
	if (wrong)							\
		caps->z_broken |= (cap);				\
}

/* Atomics: same, with a 16-byte aligned memory area reset before every run */
#define HART_PROBE_AMO(fn, cap, block, ref)				\
static void								\
fn(struct hart_state *hs)						\
{									\
	struct rvcaps *caps = hs->caps;					\
	uint64_t mem[2] __attribute__((aligned(16)));			\
	bool wrong = false;						\
									\
	for (unsigned i_ = 0; i_ < EXT_NPAIRS; i_++) {			\
		xu a_, b_, x_ = 0;					\
									\
		ext_pair(i_, &a_, &b_);					\
		mem[0] = 0x1111111111111111ULL;				\
		mem[1] = 0x2222222222222222ULL;				\
		hart_probe_insn_begin(hs);				\
		block(a_, b_, mem, x_);					\
		if (!hart_probe_insn_end(hs))				\
			return;						\
		if (x_ != ref(a_, b_))					\
			wrong = true;					\
	}								\
	caps->z_caps |= (cap);						\
	if (wrong)							\
		caps->z_broken |= (cap);				\
}

/*
 * Fixed-vector blocks: skip is a condition (on caps) under which the probe must
 * not run, fp says whether the FPU has to be switched on (and its state saved),
 * decl declares the scratch objects the block needs, run is the block call (the
 * result goes to x_) and want the value it must produce.
 */
#define HART_PROBE_FIXED(fn, cap, fp, skip, decl, run, want)		\
static void								\
fn(struct hart_state *hs)						\
{									\
	struct rvcaps *caps = hs->caps;					\
	struct fp_probe_state fps = { 0, 0 };				\
	decl;								\
	xu x_ = 0;							\
	bool ok_;							\
									\
	if (skip)							\
		return;							\
	if (fp)								\
		hart_probe_fp_begin(&fps);				\
	hart_probe_insn_begin(hs);					\
	run;								\
	ok_ = hart_probe_insn_end(hs);					\
	if (fp)								\
		hart_probe_fp_end(&fps);				\
	if (!ok_)							\
		return;							\
	caps->z_caps |= (cap);						\
	if (x_ != (xu)(want))						\
		caps->z_broken |= (cap);				\
}

/**** Bit manipulation, conditional ops, multiply (caps.h bits 5-9, 19) ****/

HART_PROBE_CHECKED(hart_probe_zba, CAP_ZBA, EXT_BLOCK_ZBA, ext_ref_zba)
HART_PROBE_CHECKED(hart_probe_zbb, CAP_ZBB, EXT_BLOCK_ZBB, ext_ref_zbb)
HART_PROBE_CHECKED(hart_probe_zbc, CAP_ZBC, EXT_BLOCK_ZBC, ext_ref_zbc)
HART_PROBE_CHECKED(hart_probe_zbs, CAP_ZBS, EXT_BLOCK_ZBS, ext_ref_zbs)
HART_PROBE_CHECKED(hart_probe_zicond, CAP_ZICOND, EXT_BLOCK_ZICOND, ext_ref_zicond)
HART_PROBE_CHECKED(hart_probe_zmmul, CAP_ZMMUL, EXT_BLOCK_ZMMUL, ext_ref_zmmul)

/**** Atomics and system (caps.h bits 10-12, 15-18) ****/

/* There's no reservation held here so both wrs.* may return right away, the
 * block has no result to check. */
static void
hart_probe_zawrs(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	hart_probe_insn_begin(hs);
	EXT_BLOCK_ZAWRS();
	if (hart_probe_insn_end(hs))
		caps->z_caps |= CAP_ZAWRS;
}

/* amocas.q (RV64) needs even/odd register pairs and is not part of the block */
HART_PROBE_AMO(hart_probe_zacas, CAP_ZACAS, EXT_BLOCK_ZACAS, ext_ref_zacas)
/* Zabha's amocas.b/h are not included, they also need Zacas */
HART_PROBE_AMO(hart_probe_zabha, CAP_ZABHA, EXT_BLOCK_ZABHA, ext_ref_zabha)
HART_PROBE_AMO(hart_probe_zaamo, CAP_ZAAMO, EXT_BLOCK_ZAAMO, ext_ref_zaamo)
HART_PROBE_AMO(hart_probe_zalrsc, CAP_ZALRSC, EXT_BLOCK_ZALRSC, ext_ref_zalrsc)

/* Every CSR instruction form on one CSR (see ext_blocks.h), result must be 0 */
HART_PROBE_FIXED(hart_probe_zicsr, CAP_ZICSR, 0, 0, (void)0,
		 EXT_BLOCK_ZICSR(x_), EXT_EXP_ZICSR)

/* Self-modifying code: needs a buffer the hart can execute from. Static, so it
 * lives in .data and not on the stack. */
static uint32_t zifencei_code[4] __attribute__((aligned(16)));
HART_PROBE_FIXED(hart_probe_zifencei, CAP_ZIFENCEI, 0, 0, (void)0,
		 EXT_BLOCK_ZIFENCEI(0x2a5, 0x5c3, zifencei_code, x_),
		 ext_ref_zifencei(0x2a5, 0x5c3))

/**** Half-precision, Zfa, BF16 (caps.h bits 13-14, 24, 42) ****/

/* Zfh and Zfhmin require F. Zfhmin is implied by Zfh, so a Zfh core reports both. */
HART_PROBE_FIXED(hart_probe_zfh, CAP_ZFH, 1, !(caps->r_caps & CAP_F),
		 uint16_t half_[4] __attribute__((aligned(8))),
		 EXT_BLOCK_ZFH(EXT_IN_ZFH_A, EXT_IN_ZFH_B, half_, x_), EXT_EXP_ZFH)
HART_PROBE_FIXED(hart_probe_zfhmin, CAP_ZFHMIN, 1, !(caps->r_caps & CAP_F),
		 uint16_t half_[4] __attribute__((aligned(8))),
		 EXT_BLOCK_ZFHMIN(EXT_IN_ZFHMIN_A, EXT_IN_ZFHMIN_B, half_, x_), EXT_EXP_ZFHMIN)

/* Zfbfmin requires F */
HART_PROBE_FIXED(hart_probe_zfbfmin, CAP_ZFBFMIN, 1, !(caps->r_caps & CAP_F), (void)0,
		 EXT_BLOCK_ZFBFMIN(EXT_IN_ZFBFMIN_A, EXT_IN_ZFBFMIN_B, x_), EXT_EXP_ZFBFMIN)

/* Zfa requires F, and its double-precision forms need D: the single-precision
 * block is always run, the double one only on a core that has D. */
static void
hart_probe_zfa(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	struct fp_probe_state fps;
	xu xs_ = 0, xd_ = 0;
	bool ok, have_d = !!(caps->r_caps & CAP_D);

	if (!(caps->r_caps & CAP_F))
		return;

	hart_probe_fp_begin(&fps);
	hart_probe_insn_begin(hs);
	EXT_BLOCK_ZFA_S(EXT_IN_ZFA_S_A, EXT_IN_ZFA_S_B, xs_);
	ok = hart_probe_insn_end(hs);
	if (ok && have_d) {
		hart_probe_insn_begin(hs);
		EXT_BLOCK_ZFA_D(EXT_IN_ZFA_D_A, EXT_IN_ZFA_D_B, xd_);
		ok = hart_probe_insn_end(hs);
	}
	hart_probe_fp_end(&fps);

	if (!ok)
		return;
	caps->z_caps |= CAP_ZFA;
	if (xs_ != (xu)EXT_EXP_ZFA_S || (have_d && xd_ != (xu)EXT_EXP_ZFA_D))
		caps->z_broken |= CAP_ZFA;
}

/**** FP in integer registers (caps.h bits 20-23) ****/

/*
 * Zfinx and friends reuse the F/D/Zfh encodings with the integer register
 * file, and they can't be implemented together with F. On a core that has F
 * the same bits would execute as ordinary F instructions and we'd report a
 * false positive, so these probes only run when misa.F is clear (misa.D for
 * Zdinx). Their blocks are encoded with .insn.
 */
HART_PROBE_FIXED(hart_probe_zfinx, CAP_ZFINX, 1, caps->r_caps & CAP_F, (void)0,
		 EXT_BLOCK_ZFINX(EXT_IN_ZFINX_A, EXT_IN_ZFINX_B, x_), EXT_EXP_ZFINX)
#if __riscv_xlen == 64
#define ZDINX_A	EXT_IN_ZDINX_A
#define ZDINX_B	EXT_IN_ZDINX_B
#else
#define ZDINX_A	0	/* the RV32 block works on register pairs with fixed values */
#define ZDINX_B	0
#endif
HART_PROBE_FIXED(hart_probe_zdinx, CAP_ZDINX, 1, caps->r_caps & (CAP_F | CAP_D), (void)0,
		 EXT_BLOCK_ZDINX(ZDINX_A, ZDINX_B, x_), EXT_EXP_ZDINX)
HART_PROBE_FIXED(hart_probe_zhinx, CAP_ZHINX, 1, caps->r_caps & CAP_F, (void)0,
		 EXT_BLOCK_ZHINX(EXT_IN_ZHINX_A, EXT_IN_ZHINX_B, x_), EXT_EXP_ZHINX)
HART_PROBE_FIXED(hart_probe_zhinxmin, CAP_ZHINXMIN, 1, caps->r_caps & CAP_F, (void)0,
		 EXT_BLOCK_ZHINXMIN(EXT_IN_ZHINXMIN_A, EXT_IN_ZHINXMIN_B, x_), EXT_EXP_ZHINXMIN)

/**** Scalar crypto (caps.h bits 25-32) ****/

HART_PROBE_CHECKED(hart_probe_zbkb, CAP_ZBKB, EXT_BLOCK_ZBKB, ext_ref_zbkb)
HART_PROBE_CHECKED(hart_probe_zbkc, CAP_ZBKC, EXT_BLOCK_ZBKC, ext_ref_zbkc)
HART_PROBE_CHECKED(hart_probe_zbkx, CAP_ZBKX, EXT_BLOCK_ZBKX, ext_ref_zbkx)
HART_PROBE_CHECKED(hart_probe_zknh, CAP_ZKNH, EXT_BLOCK_ZKNH, ext_ref_zknh)
HART_PROBE_CHECKED(hart_probe_zksh, CAP_ZKSH, EXT_BLOCK_ZKSH, ext_ref_zksh)

/* Zknd, Zkne and Zksed run a complete AES-128 / SM4 operation on a published
 * test vector (the block ignores a and b). */
HART_PROBE_FIXED(hart_probe_zknd, CAP_ZKND, 0, 0, (void)0, EXT_BLOCK_ZKND(0, 0, x_), EXT_EXP_ZKND)
HART_PROBE_FIXED(hart_probe_zkne, CAP_ZKNE, 0, 0, (void)0, EXT_BLOCK_ZKNE(0, 0, x_), EXT_EXP_ZKNE)
HART_PROBE_FIXED(hart_probe_zksed, CAP_ZKSED, 0, 0, (void)0, EXT_BLOCK_ZKSED(0, 0, x_), EXT_EXP_ZKSED)

/**** Compressed (caps.h bits 33-39) ****/

/*
 * Compressed instructions are only executed on a hart with misa.C set. Without
 * C a compressed instruction traps, and the handler can't step over it (the
 * hart would fetch from a misaligned address and trap again forever). The price
 * is that a hart with Zca but no C bit isn't detected.
 *
 * Zcmp and Zcmt share encodings with c.fsdsp from Zcd, so on a hart that has
 * Zcd they would run as stores to the stack instead of trapping. They are only
 * probed when Zcd was not found. Likewise Zclsd shares c.ld/c.sd with c.flw/
 * c.fsw (Zcf), and is only probed when Zcf was not found.
 */
#define NO_C	(!(caps->r_caps & CAP_C))

HART_PROBE_FIXED(hart_probe_zca, CAP_ZCA, 0, NO_C,
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZCA(mem_, x_), EXT_EXP_ZCA)
/*
 * Zcb: the core block, plus the instructions that need a second extension (c.zext.h, c.sext.b,
 * c.sext.h with Zbb; c.mul with Zmmul; c.zext.w with Zba on RV64) when those are known to be
 * present (their probes run earlier, they are lower caps bits).
 */
static void
hart_probe_zcb(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint8_t mem_[32] __attribute__((aligned(16)));
	xu x_ = 0, y_ = 0;
	bool more_ = (caps->z_caps & CAP_ZBB) && (caps->z_caps & CAP_ZMMUL);
	bool ok_;

#if __riscv_xlen == 64
	more_ = more_ && (caps->z_caps & CAP_ZBA);
#endif
	if (NO_C)
		return;
	hart_probe_insn_begin(hs);
	EXT_BLOCK_ZCB(mem_, x_);
	if (more_)
		EXT_BLOCK_ZCBX(mem_, y_);
	ok_ = hart_probe_insn_end(hs);
	if (!ok_)
		return;
	caps->z_caps |= CAP_ZCB;
	if (x_ != (xu)EXT_EXP_ZCB || (more_ && y_ != (xu)EXT_EXP_ZCBX))
		caps->z_broken |= CAP_ZCB;
}
HART_PROBE_FIXED(hart_probe_zcd, CAP_ZCD, 1, NO_C || !(caps->r_caps & CAP_D),
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZCD(mem_, x_), EXT_EXP_ZCD)
#if __riscv_xlen == 32
HART_PROBE_FIXED(hart_probe_zcf, CAP_ZCF, 1, NO_C || !(caps->r_caps & CAP_F),
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZCF(mem_, x_), EXT_EXP_ZCF)
#endif
HART_PROBE_FIXED(hart_probe_zcmp, CAP_ZCMP, 0, NO_C || (caps->z_caps & CAP_ZCD),
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZCMP(mem_, x_), EXT_EXP_ZCMP)
/* jvt table (32 entries for cm.jt plus the cm.jalt entries we use) needs 64-byte alignment */
HART_PROBE_FIXED(hart_probe_zcmt, CAP_ZCMT, 0, NO_C || (caps->z_caps & CAP_ZCD),
		 uint8_t mem_[320] __attribute__((aligned(64))), EXT_BLOCK_ZCMT(mem_, x_), EXT_EXP_ZCMT)

/* Zce is a bundle: Zca, Zcb, Zcmp, Zcmt, plus Zcf on RV32 when F is present.
 * (Zcd is not part of it, it can't coexist with Zcmt.) Call this after the
 * members have been probed. */
static void
hart_probe_zce(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t need = CAP_ZCA | CAP_ZCB | CAP_ZCMP | CAP_ZCMT;

#if __riscv_xlen == 32
	if (caps->r_caps & CAP_F)
		need |= CAP_ZCF;
#endif
	if ((caps->z_caps & need) == need) {
		caps->z_caps |= CAP_ZCE;
		if (caps->z_broken & need)
			caps->z_broken |= CAP_ZCE;
	}
}

/**** May-be-operations (caps.h bits 40-41) ****/

/* All mop.r.N / mop.rr.N must write 0 to rd (unless Zicfiss is active) */
#define ZIMOP_REF(a, b)	((xu)EXT_EXP_ZIMOP)
HART_PROBE_CHECKED(hart_probe_zimop, CAP_ZIMOP, EXT_BLOCK_ZIMOP, ZIMOP_REF)
HART_PROBE_FIXED(hart_probe_zcmop, CAP_ZCMOP, 0, NO_C, (void)0, EXT_BLOCK_ZCMOP(x_), EXT_EXP_ZCMOP)

/**** Vector subsets (caps.h bits 43-53) ****/

/*
 * The vector unit needs mstatus.VS on, and the FP forms also FS. The blocks
 * start with their own vsetivli so they don't depend on the previous vtype/vl.
 * We only restore VS afterwards: the vector registers and vl/vtype are left
 * with whatever the probe wrote, nothing in the SDK uses them (it's built
 * without V) and a vsetvli is always needed before using them anyway.
 */
struct vec_probe_state {
	struct fp_probe_state fp;
	uint64_t vs;
	bool fp_on;
};

static void
hart_probe_vec_begin(struct vec_probe_state *vps, bool need_fp)
{
	vps->vs = csr_read(CSR_MSTATUS) & FIELD_GET_MASK(CSR_MSTATUS_VS);
	csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_V_INIT);	/* Off -> Initial, otherwise no change */
	vps->fp_on = need_fp;
	if (need_fp)
		hart_probe_fp_begin(&vps->fp);
}

static void
hart_probe_vec_end(struct vec_probe_state *vps)
{
	if (vps->fp_on)
		hart_probe_fp_end(&vps->fp);
	csr_clear_bits(CSR_MSTATUS, FIELD_GET_MASK(CSR_MSTATUS_VS));
	csr_set_bits(CSR_MSTATUS, vps->vs);
}

/* Generates a vector probe: req is the misa bits (CAP_F, CAP_D...) the subset
 * depends on and need_fp whether FS must be on. buf is the 256 byte scratch
 * area the blocks use for their operands and results. */
#define HART_PROBE_VEC(fn, cap, req, need_fp, block, want)		\
static void								\
fn(struct hart_state *hs)						\
{									\
	struct rvcaps *caps = hs->caps;					\
	struct vec_probe_state vps;					\
	uint8_t buf[256] __attribute__((aligned(16)));			\
	xu x_ = 0;							\
	bool ok;							\
									\
	if ((caps->r_caps & (req)) != (req))				\
		return;							\
									\
	hart_probe_vec_begin(&vps, need_fp);				\
	hart_probe_insn_begin(hs);					\
	block(buf, x_);							\
	ok = hart_probe_insn_end(hs);					\
	hart_probe_vec_end(&vps);					\
	if (!ok)							\
		return;							\
	caps->z_caps |= (cap);						\
	if (x_ != (xu)(want))						\
		caps->z_broken |= (cap);				\
}

HART_PROBE_VEC(hart_probe_zve32x, CAP_ZVE32X, 0, false, EXT_BLOCK_ZVE32X, EXT_EXP_ZVE32X)
HART_PROBE_VEC(hart_probe_zve32f, CAP_ZVE32F, CAP_F, true, EXT_BLOCK_ZVE32F, EXT_EXP_ZVE32F)
HART_PROBE_VEC(hart_probe_zve64x, CAP_ZVE64X, 0, false, EXT_BLOCK_ZVE64X, EXT_EXP_ZVE64X)
HART_PROBE_VEC(hart_probe_zve64d, CAP_ZVE64D, CAP_D, true, EXT_BLOCK_ZVE64D, EXT_EXP_ZVE64D)
HART_PROBE_VEC(hart_probe_zvbb, CAP_ZVBB, 0, false, EXT_BLOCK_ZVBB, EXT_EXP_ZVBB)
HART_PROBE_VEC(hart_probe_zvbc, CAP_ZVBC, 0, false, EXT_BLOCK_ZVBC, EXT_EXP_ZVBC)
HART_PROBE_VEC(hart_probe_zvfh, CAP_ZVFH, CAP_F, true, EXT_BLOCK_ZVFH, EXT_EXP_ZVFH)
HART_PROBE_VEC(hart_probe_zvfhmin, CAP_ZVFHMIN, CAP_F, true, EXT_BLOCK_ZVFHMIN, EXT_EXP_ZVFHMIN)
HART_PROBE_VEC(hart_probe_zvfbfmin, CAP_ZVFBFMIN, CAP_F, true, EXT_BLOCK_ZVFBFMIN, EXT_EXP_ZVFBFMIN)
HART_PROBE_VEC(hart_probe_zvfbfwma, CAP_ZVFBFWMA, CAP_F, true, EXT_BLOCK_ZVFBFWMA, EXT_EXP_ZVFBFWMA)

/* Zve64f is Zve64x + Zve32f, there's no instruction that only it adds. Call
 * this after both have been probed. */
static void
hart_probe_zve64f(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t need = CAP_ZVE64X | CAP_ZVE32F;

	if ((caps->z_caps & need) == need) {
		caps->z_caps |= CAP_ZVE64F;
		if (caps->z_broken & need)
			caps->z_broken |= CAP_ZVE64F;
	}
}

#if __riscv_xlen == 32
/**** Load/store pairs (caps.h bits 54-55, RV32 only) ****/

HART_PROBE_FIXED(hart_probe_zilsd, CAP_ZILSD, 0, 0,
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZILSD(mem_, x_), EXT_EXP_ZILSD)
/* Zclsd can't be implemented with Zcf (they share encodings): see above */
HART_PROBE_FIXED(hart_probe_zclsd, CAP_ZCLSD, 0, NO_C || (caps->z_caps & CAP_ZCF),
		 uint8_t mem_[32] __attribute__((aligned(16))), EXT_BLOCK_ZCLSD(mem_, x_), EXT_EXP_ZCLSD)
#endif


/* Entry point, called from hart_probe_priv_caps() in hart_probe.c.
 * Sets the matching bits on hs->caps->z_caps, in the same order as caps.h.
 * Needs r_caps (misa) to be filled already since the FP, vector and
 * Zfinx probes depend on F/D. */
void
hart_probe_unpriv_caps(struct hart_state *hs)
{
	hart_probe_zba(hs);
	hart_probe_zbb(hs);
	hart_probe_zbc(hs);
	hart_probe_zbs(hs);
	hart_probe_zicond(hs);
	hart_probe_zawrs(hs);
	hart_probe_zacas(hs);
	hart_probe_zabha(hs);
	hart_probe_zfh(hs);
	hart_probe_zfa(hs);

	hart_probe_zicsr(hs);
	hart_probe_zifencei(hs);
	hart_probe_zaamo(hs);
	hart_probe_zalrsc(hs);
	hart_probe_zmmul(hs);

	hart_probe_zfinx(hs);
	hart_probe_zdinx(hs);
	hart_probe_zhinx(hs);
	hart_probe_zhinxmin(hs);
	hart_probe_zfhmin(hs);

	hart_probe_zbkb(hs);
	hart_probe_zbkc(hs);
	hart_probe_zbkx(hs);
	hart_probe_zknd(hs);
	hart_probe_zkne(hs);
	hart_probe_zknh(hs);
	hart_probe_zksed(hs);
	hart_probe_zksh(hs);

	hart_probe_zca(hs);
	hart_probe_zcb(hs);
	hart_probe_zcd(hs);
#if __riscv_xlen == 32
	hart_probe_zcf(hs);
#endif
	hart_probe_zcmp(hs);
	hart_probe_zcmt(hs);
	hart_probe_zce(hs);		

	hart_probe_zimop(hs);
	hart_probe_zcmop(hs);
	hart_probe_zfbfmin(hs);

	hart_probe_zve32x(hs);
	hart_probe_zve32f(hs);
	hart_probe_zve64x(hs);
	hart_probe_zve64f(hs);		
	hart_probe_zve64d(hs);
	hart_probe_zvbb(hs);
	hart_probe_zvbc(hs);
	hart_probe_zvfh(hs);
	hart_probe_zvfhmin(hs);
	hart_probe_zvfbfmin(hs);
	hart_probe_zvfbfwma(hs);

#if __riscv_xlen == 32
	hart_probe_zilsd(hs);
	hart_probe_zclsd(hs);
#endif
}