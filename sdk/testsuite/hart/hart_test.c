/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Extension stress test.
 *
 * Runs the instruction block of one Z extension (see ext_blocks.h) HART_TEST_ITERATIONS
 * times in a loop on a hart of your choice and prints the cycles, the retired
 * instructions and the IPC the hart achieved. The same blocks are used by the
 * capability probe (hart_probe_unpriv.c), so what is stressed here is exactly what
 * was probed.
 *
 * For every extension the test does:
 *
 *  1. Check: run the block N times, outside the timed region, and compare the
 *     result of every single pass against the expected one. A hart that gets it
 *     right once but wrong under repetition shows up as a MISMATCH.
 *  2. Timed run: warm up, then run the block N times between two reads of
 *     mcycle/minstret on the hart itself.
 *  3. Calibration: the same loop with an empty body is timed and subtracted, so
 *     the loop counter and branch don't count as part of the extension.
 *
 * The target hart is woken up with hart_wakeup_with_addr(), runs the above with
 * interrupts masked and reports back through a per-hart job slot. Only extensions
 * the target hart reported as present (and not broken) in its own capabilities are
 * offered. One job runs at a time.
 *
 * Not included: Zawrs, since wrs.nto may legally wait until an interrupt arrives and
 * the loop runs with interrupts masked.
 */

#include <target_config.h>		/* For PLAT_MAX_HARTS / PLAT_NO_IPI */
#include <stdint.h>			/* For typed ints */
#include <stdbool.h>			/* For bool */
#include <stdatomic.h>			/* For C11 atomics */
#include <stdio.h>			/* For getchar(), EOF */
#include <platform/utils/utils.h>	/* For ANN/INF/ERR */
#include <platform/utils/bitfield.h>	/* For FIELD_GET_MASK() */
#include <platform/riscv/csr.h>		/* For CSRs, pause() */
#include <platform/riscv/caps.h>	/* For CAP_* macros */
#include <platform/riscv/hart.h>	/* For hart_state, hart_wakeup_with_addr() */
#include <platform/riscv/z_ext_blocks.h>	/* For the instruction blocks */
#include <test_framework.h>		/* For test registration macros */

#ifndef HART_TEST_ITERATIONS
#define HART_TEST_ITERATIONS	100000u
#endif
#define HART_TEST_WARMUP	100u

/* How long hart 0 waits for the target (spins) before calling it a hang */
#define HART_TEST_TIMEOUT	2000000000ULL

/**************\
* DESCRIPTORS  *
\**************/

/* Units that must be switched on in mstatus on the hart running the block */
enum stress_unit {
	UNIT_NONE	= 0,
	UNIT_FP		= BIT(0),
	UNIT_VEC	= BIT(1)
};

struct stress_ext {
	const char *name;
	uint64_t cap;			/* z_caps bit that must be set, and not in z_broken */
	uint64_t need_z;		/* Other z_caps bits the block needs */
	uint32_t need_r;		/* misa bits the block needs */
	unsigned units;			/* enum stress_unit */
	xu (*run)(unsigned n);		/* Runs the block n times, returns the last result */
	unsigned (*chk)(unsigned n, xu want);	/* Same, returns the number of wrong results */
	xu (*want)(void);		/* The result every pass must give */
};

/*
 * Each block gets two loops: run() for the timed region, with nothing but the
 * block in the body, and chk() that compares every pass. decl declares what the
 * block needs (buffers, operands), pre initializes it, call is the block.
 */
#define DEFINE_LOOPS(sym, decl, pre, call)				\
static xu								\
run_##sym(unsigned n)							\
{									\
	decl;								\
	xu x_ = 0;							\
									\
	pre;								\
	for (unsigned i_ = 0; i_ < n; i_++) {				\
		call;							\
	}								\
	return x_;							\
}									\
static unsigned								\
chk_##sym(unsigned n, xu want)						\
{									\
	decl;								\
	xu x_ = 0;							\
	unsigned bad_ = 0;						\
									\
	pre;								\
	for (unsigned i_ = 0; i_ < n; i_++) {				\
		call;							\
		bad_ += (x_ != want);					\
	}								\
	return bad_;							\
}

#define WANT_CONST(sym, expr)						\
static xu want_##sym(void) { return (xu)(expr); }

/* Blocks computed from two operands, checked against the C twin */
#define WANT_PAIR(sym, ref)						\
static xu want_##sym(void) { xu a, b; ext_pair(0, &a, &b); return ref(a, b); }

#define PAIR_LOOPS(sym, BLOCK)						\
	DEFINE_LOOPS(sym, xu a_; xu b_, ext_pair(0, &a_, &b_), BLOCK(a_, b_, x_))

#define AMO_LOOPS(sym, BLOCK)						\
	DEFINE_LOOPS(sym, uint64_t mem_[2] __attribute__((aligned(16))); xu a_; xu b_, \
		     ext_pair(0, &a_, &b_); mem_[0] = 0x1111111111111111ULL; \
		     mem_[1] = 0x2222222222222222ULL, BLOCK(a_, b_, mem_, x_))

/* Base-ISA pair extensions and the scalar crypto ones with a C twin */
PAIR_LOOPS(zba, EXT_BLOCK_ZBA)		WANT_PAIR(zba, ext_ref_zba)
PAIR_LOOPS(zbb, EXT_BLOCK_ZBB)		WANT_PAIR(zbb, ext_ref_zbb)
PAIR_LOOPS(zbc, EXT_BLOCK_ZBC)		WANT_PAIR(zbc, ext_ref_zbc)
PAIR_LOOPS(zbs, EXT_BLOCK_ZBS)		WANT_PAIR(zbs, ext_ref_zbs)
PAIR_LOOPS(zicond, EXT_BLOCK_ZICOND)	WANT_PAIR(zicond, ext_ref_zicond)
PAIR_LOOPS(zmmul, EXT_BLOCK_ZMMUL)	WANT_PAIR(zmmul, ext_ref_zmmul)
PAIR_LOOPS(zbkb, EXT_BLOCK_ZBKB)	WANT_PAIR(zbkb, ext_ref_zbkb)
PAIR_LOOPS(zbkc, EXT_BLOCK_ZBKC)	WANT_PAIR(zbkc, ext_ref_zbkc)
PAIR_LOOPS(zbkx, EXT_BLOCK_ZBKX)	WANT_PAIR(zbkx, ext_ref_zbkx)
PAIR_LOOPS(zknh, EXT_BLOCK_ZKNH)	WANT_PAIR(zknh, ext_ref_zknh)
PAIR_LOOPS(zksh, EXT_BLOCK_ZKSH)	WANT_PAIR(zksh, ext_ref_zksh)
PAIR_LOOPS(zimop, EXT_BLOCK_ZIMOP)	WANT_CONST(zimop, EXT_EXP_ZIMOP)

/* Atomics */
AMO_LOOPS(zaamo, EXT_BLOCK_ZAAMO)	WANT_PAIR(zaamo, ext_ref_zaamo)
AMO_LOOPS(zabha, EXT_BLOCK_ZABHA)	WANT_PAIR(zabha, ext_ref_zabha)
AMO_LOOPS(zalrsc, EXT_BLOCK_ZALRSC)	WANT_PAIR(zalrsc, ext_ref_zalrsc)
AMO_LOOPS(zacas, EXT_BLOCK_ZACAS)	WANT_PAIR(zacas, ext_ref_zacas)

/* System */
DEFINE_LOOPS(zicsr, (void)0, (void)0, EXT_BLOCK_ZICSR(x_))
WANT_CONST(zicsr, EXT_EXP_ZICSR)

/* Self-modifying code needs a buffer the hart can execute from, in .data */
static uint32_t zifencei_code[4] __attribute__((aligned(16)));
DEFINE_LOOPS(zifencei, (void)0, (void)0, EXT_BLOCK_ZIFENCEI(0x2a5, 0x5c3, zifencei_code, x_))
WANT_CONST(zifencei, ext_ref_zifencei(0x2a5, 0x5c3))

/* Scalar crypto with fixed test vectors (the blocks ignore their operands) */
DEFINE_LOOPS(zknd, (void)0, (void)0, EXT_BLOCK_ZKND(0, 0, x_))	WANT_CONST(zknd, EXT_EXP_ZKND)
DEFINE_LOOPS(zkne, (void)0, (void)0, EXT_BLOCK_ZKNE(0, 0, x_))	WANT_CONST(zkne, EXT_EXP_ZKNE)
DEFINE_LOOPS(zksed, (void)0, (void)0, EXT_BLOCK_ZKSED(0, 0, x_))	WANT_CONST(zksed, EXT_EXP_ZKSED)

/* Floating point */
#define HALF_DECL	uint16_t half_[4] __attribute__((aligned(8)))
DEFINE_LOOPS(zfh, HALF_DECL, (void)0, EXT_BLOCK_ZFH(EXT_IN_ZFH_A, EXT_IN_ZFH_B, half_, x_))
WANT_CONST(zfh, EXT_EXP_ZFH)
DEFINE_LOOPS(zfhmin, HALF_DECL, (void)0, EXT_BLOCK_ZFHMIN(EXT_IN_ZFHMIN_A, EXT_IN_ZFHMIN_B, half_, x_))
WANT_CONST(zfhmin, EXT_EXP_ZFHMIN)
DEFINE_LOOPS(zfbfmin, (void)0, (void)0, EXT_BLOCK_ZFBFMIN(EXT_IN_ZFBFMIN_A, EXT_IN_ZFBFMIN_B, x_))
WANT_CONST(zfbfmin, EXT_EXP_ZFBFMIN)
DEFINE_LOOPS(zfa_s, (void)0, (void)0, EXT_BLOCK_ZFA_S(EXT_IN_ZFA_S_A, EXT_IN_ZFA_S_B, x_))
WANT_CONST(zfa_s, EXT_EXP_ZFA_S)
DEFINE_LOOPS(zfa_d, (void)0, (void)0, EXT_BLOCK_ZFA_D(EXT_IN_ZFA_D_A, EXT_IN_ZFA_D_B, x_))
WANT_CONST(zfa_d, EXT_EXP_ZFA_D)

/* FP in integer registers */
DEFINE_LOOPS(zfinx, (void)0, (void)0, EXT_BLOCK_ZFINX(EXT_IN_ZFINX_A, EXT_IN_ZFINX_B, x_))
WANT_CONST(zfinx, EXT_EXP_ZFINX)
#if __riscv_xlen == 64
#define ZDINX_A	EXT_IN_ZDINX_A
#define ZDINX_B	EXT_IN_ZDINX_B
#else
#define ZDINX_A	0	/* the RV32 block works on register pairs with fixed values */
#define ZDINX_B	0
#endif
DEFINE_LOOPS(zdinx, (void)0, (void)0, EXT_BLOCK_ZDINX(ZDINX_A, ZDINX_B, x_))
WANT_CONST(zdinx, EXT_EXP_ZDINX)
DEFINE_LOOPS(zhinx, (void)0, (void)0, EXT_BLOCK_ZHINX(EXT_IN_ZHINX_A, EXT_IN_ZHINX_B, x_))
WANT_CONST(zhinx, EXT_EXP_ZHINX)
DEFINE_LOOPS(zhinxmin, (void)0, (void)0, EXT_BLOCK_ZHINXMIN(EXT_IN_ZHINXMIN_A, EXT_IN_ZHINXMIN_B, x_))
WANT_CONST(zhinxmin, EXT_EXP_ZHINXMIN)

/* Compressed */
#define MEM32_DECL	uint8_t mem_[32] __attribute__((aligned(16)))
DEFINE_LOOPS(zca, MEM32_DECL, (void)0, EXT_BLOCK_ZCA(mem_, x_))	WANT_CONST(zca, EXT_EXP_ZCA)
DEFINE_LOOPS(zcb, MEM32_DECL, (void)0, EXT_BLOCK_ZCB(mem_, x_))	WANT_CONST(zcb, EXT_EXP_ZCB)
DEFINE_LOOPS(zcbx, MEM32_DECL, (void)0, EXT_BLOCK_ZCBX(mem_, x_))	WANT_CONST(zcbx, EXT_EXP_ZCBX)
DEFINE_LOOPS(zcd, MEM32_DECL, (void)0, EXT_BLOCK_ZCD(mem_, x_))	WANT_CONST(zcd, EXT_EXP_ZCD)
DEFINE_LOOPS(zcmp, MEM32_DECL, (void)0, EXT_BLOCK_ZCMP(mem_, x_))	WANT_CONST(zcmp, EXT_EXP_ZCMP)
/* The jvt table needs 64-byte alignment */
DEFINE_LOOPS(zcmt, uint8_t mem_[320] __attribute__((aligned(64))), (void)0, EXT_BLOCK_ZCMT(mem_, x_))
WANT_CONST(zcmt, EXT_EXP_ZCMT)
DEFINE_LOOPS(zcmop, (void)0, (void)0, EXT_BLOCK_ZCMOP(x_))	WANT_CONST(zcmop, EXT_EXP_ZCMOP)
#if __riscv_xlen == 32
DEFINE_LOOPS(zcf, MEM32_DECL, (void)0, EXT_BLOCK_ZCF(mem_, x_))	WANT_CONST(zcf, EXT_EXP_ZCF)
DEFINE_LOOPS(zilsd, MEM32_DECL, (void)0, EXT_BLOCK_ZILSD(mem_, x_))	WANT_CONST(zilsd, EXT_EXP_ZILSD)
DEFINE_LOOPS(zclsd, MEM32_DECL, (void)0, EXT_BLOCK_ZCLSD(mem_, x_))	WANT_CONST(zclsd, EXT_EXP_ZCLSD)
#endif

/* Vector */
#define VBUF_DECL	uint8_t buf_[256] __attribute__((aligned(16)))
#define VEC_LOOPS(sym, BLOCK, exp)					\
	DEFINE_LOOPS(sym, VBUF_DECL, (void)0, BLOCK(buf_, x_))		\
	WANT_CONST(sym, exp)
VEC_LOOPS(zve32x, EXT_BLOCK_ZVE32X, EXT_EXP_ZVE32X)
VEC_LOOPS(zve32f, EXT_BLOCK_ZVE32F, EXT_EXP_ZVE32F)
VEC_LOOPS(zve64x, EXT_BLOCK_ZVE64X, EXT_EXP_ZVE64X)
VEC_LOOPS(zve64d, EXT_BLOCK_ZVE64D, EXT_EXP_ZVE64D)
VEC_LOOPS(zvbb, EXT_BLOCK_ZVBB, EXT_EXP_ZVBB)
VEC_LOOPS(zvbc, EXT_BLOCK_ZVBC, EXT_EXP_ZVBC)
VEC_LOOPS(zvfh, EXT_BLOCK_ZVFH, EXT_EXP_ZVFH)
VEC_LOOPS(zvfhmin, EXT_BLOCK_ZVFHMIN, EXT_EXP_ZVFHMIN)
VEC_LOOPS(zvfbfmin, EXT_BLOCK_ZVFBFMIN, EXT_EXP_ZVFBFMIN)
VEC_LOOPS(zvfbfwma, EXT_BLOCK_ZVFBFWMA, EXT_EXP_ZVFBFWMA)

#define EXT(nm, capbit, units, sym)	\
	{ nm, capbit, 0, 0, units, run_##sym, chk_##sym, want_##sym }
#define EXT_NEED(nm, capbit, needz, needr, units, sym)	\
	{ nm, capbit, needz, needr, units, run_##sym, chk_##sym, want_##sym }

#if __riscv_xlen == 64
#define ZCBX_NEED	(CAP_ZCB | CAP_ZBB | CAP_ZMMUL | CAP_ZBA)
#else
#define ZCBX_NEED	(CAP_ZCB | CAP_ZBB | CAP_ZMMUL)
#endif

static const struct stress_ext stress_exts[] = {
	EXT("Zba",	CAP_ZBA,	UNIT_NONE, zba),
	EXT("Zbb",	CAP_ZBB,	UNIT_NONE, zbb),
	EXT("Zbc",	CAP_ZBC,	UNIT_NONE, zbc),
	EXT("Zbs",	CAP_ZBS,	UNIT_NONE, zbs),
	EXT("Zicond",	CAP_ZICOND,	UNIT_NONE, zicond),
	EXT("Zmmul",	CAP_ZMMUL,	UNIT_NONE, zmmul),
	EXT("Zbkb",	CAP_ZBKB,	UNIT_NONE, zbkb),
	EXT("Zbkc",	CAP_ZBKC,	UNIT_NONE, zbkc),
	EXT("Zbkx",	CAP_ZBKX,	UNIT_NONE, zbkx),
	EXT("Zknd",	CAP_ZKND,	UNIT_NONE, zknd),
	EXT("Zkne",	CAP_ZKNE,	UNIT_NONE, zkne),
	EXT("Zknh",	CAP_ZKNH,	UNIT_NONE, zknh),
	EXT("Zksed",	CAP_ZKSED,	UNIT_NONE, zksed),
	EXT("Zksh",	CAP_ZKSH,	UNIT_NONE, zksh),
	EXT("Zimop",	CAP_ZIMOP,	UNIT_NONE, zimop),
	EXT("Zaamo",	CAP_ZAAMO,	UNIT_NONE, zaamo),
	EXT("Zalrsc",	CAP_ZALRSC,	UNIT_NONE, zalrsc),
	EXT("Zabha",	CAP_ZABHA,	UNIT_NONE, zabha),
	EXT("Zacas",	CAP_ZACAS,	UNIT_NONE, zacas),
	EXT("Zicsr",	CAP_ZICSR,	UNIT_NONE, zicsr),
	EXT("Zifencei",	CAP_ZIFENCEI,	UNIT_NONE, zifencei),
	EXT("Zfh",	CAP_ZFH,	UNIT_FP, zfh),
	EXT("Zfhmin",	CAP_ZFHMIN,	UNIT_FP, zfhmin),
	EXT("Zfbfmin",	CAP_ZFBFMIN,	UNIT_FP, zfbfmin),
	EXT_NEED("Zfa (single)", CAP_ZFA, 0, CAP_F, UNIT_FP, zfa_s),
	EXT_NEED("Zfa (double)", CAP_ZFA, 0, CAP_D, UNIT_FP, zfa_d),
	EXT("Zfinx",	CAP_ZFINX,	UNIT_FP, zfinx),
	EXT("Zdinx",	CAP_ZDINX,	UNIT_FP, zdinx),
	EXT("Zhinx",	CAP_ZHINX,	UNIT_FP, zhinx),
	EXT("Zhinxmin",	CAP_ZHINXMIN,	UNIT_FP, zhinxmin),
	EXT("Zca",	CAP_ZCA,	UNIT_NONE, zca),
	EXT("Zcb",	CAP_ZCB,	UNIT_NONE, zcb),
	EXT_NEED("Zcb (Zbb/Zmmul/Zba forms)", CAP_ZCB, ZCBX_NEED, 0, UNIT_NONE, zcbx),
	EXT("Zcd",	CAP_ZCD,	UNIT_FP, zcd),
	EXT("Zcmp",	CAP_ZCMP,	UNIT_NONE, zcmp),
	EXT("Zcmt",	CAP_ZCMT,	UNIT_NONE, zcmt),
	EXT("Zcmop",	CAP_ZCMOP,	UNIT_NONE, zcmop),
#if __riscv_xlen == 32
	EXT("Zcf",	CAP_ZCF,	UNIT_FP, zcf),
	EXT("Zilsd",	CAP_ZILSD,	UNIT_NONE, zilsd),
	EXT("Zclsd",	CAP_ZCLSD,	UNIT_NONE, zclsd),
#endif
	EXT("Zve32x",	CAP_ZVE32X,	UNIT_VEC, zve32x),
	EXT("Zve32f",	CAP_ZVE32F,	UNIT_VEC | UNIT_FP, zve32f),
	EXT("Zve64x",	CAP_ZVE64X,	UNIT_VEC, zve64x),
	EXT("Zve64d",	CAP_ZVE64D,	UNIT_VEC | UNIT_FP, zve64d),
	EXT("Zvbb",	CAP_ZVBB,	UNIT_VEC, zvbb),
	EXT("Zvbc",	CAP_ZVBC,	UNIT_VEC, zvbc),
	EXT("Zvfh",	CAP_ZVFH,	UNIT_VEC | UNIT_FP, zvfh),
	EXT("Zvfhmin",	CAP_ZVFHMIN,	UNIT_VEC | UNIT_FP, zvfhmin),
	EXT("Zvfbfmin",	CAP_ZVFBFMIN,	UNIT_VEC | UNIT_FP, zvfbfmin),
	EXT("Zvfbfwma",	CAP_ZVFBFWMA,	UNIT_VEC | UNIT_FP, zvfbfwma),
};
#define NUM_EXTS	(sizeof(stress_exts) / sizeof(stress_exts[0]))

/* The empty loop, called the same way as the blocks' loops */
static xu
run_empty(unsigned n)
{
	xu x = 0;

	for (unsigned i = 0; i < n; i++)
		__asm__ volatile ("" : "+r" (x));
	return x;
}

/************\
* ON TARGET  *
\************/

/* One job slot per hart: hart 0 fills in the request and wakes the target up,
 * the target fills in the result and sets done last (release). */
struct stress_job {
	/* Request */
	unsigned ext_idx;
	unsigned iters;
	/* Result */
	uint64_t cycles;
	uint64_t insns;
	uint64_t cal_cycles;
	uint64_t cal_insns;
	unsigned bad;
	atomic_uint done;
};
static struct stress_job jobs[PLAT_MAX_HARTS];

/* 64-bit counters, on RV32 they are split in two CSRs */
static inline uint64_t
read_cycles(void)
{
#if __riscv_xlen == 64
	return csr_read(CSR_MCYCLE);
#else
	unsigned long hi, lo;

	do {
		hi = csr_read(CSR_MCYCLEH);
		lo = csr_read(CSR_MCYCLE);
	} while (hi != csr_read(CSR_MCYCLEH));
	return ((uint64_t)hi << 32) | lo;
#endif
}

static inline uint64_t
read_instret(void)
{
#if __riscv_xlen == 64
	return csr_read(CSR_MINSTRET);
#else
	unsigned long hi, lo;

	do {
		hi = csr_read(CSR_MINSTRETH);
		lo = csr_read(CSR_MINSTRET);
	} while (hi != csr_read(CSR_MINSTRETH));
	return ((uint64_t)hi << 32) | lo;
#endif
}

/* Switches on the FP / vector units for the duration of a run and puts mstatus
 * (and fcsr) back as it found them. */
struct unit_state {
	unsigned long mstatus;
	unsigned long fcsr;
	unsigned units;
};

static void
unit_enter(struct unit_state *us, unsigned units)
{
	us->units = units;
	us->mstatus = csr_read(CSR_MSTATUS);
	if (units & UNIT_FP) {
		csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_F_INIT);	/* Off -> Initial, otherwise no change */
		__asm__ volatile (".option push\n\t.option arch, +f\n\tfrcsr %0\n\t.option pop"
				  : "=r" (us->fcsr));
	}
	if (units & UNIT_VEC)
		csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_V_INIT);
}

static void
unit_leave(struct unit_state *us)
{
	if (us->units & UNIT_FP) {
		__asm__ volatile (".option push\n\t.option arch, +f\n\tfscsr %0\n\t.option pop"
				  :: "r" (us->fcsr));
		csr_clear_bits(CSR_MSTATUS, FIELD_GET_MASK(CSR_MSTATUS_FS));
		csr_set_bits(CSR_MSTATUS, us->mstatus & FIELD_GET_MASK(CSR_MSTATUS_FS));
	}
	if (us->units & UNIT_VEC) {
		csr_clear_bits(CSR_MSTATUS, FIELD_GET_MASK(CSR_MSTATUS_VS));
		csr_set_bits(CSR_MSTATUS, us->mstatus & FIELD_GET_MASK(CSR_MSTATUS_VS));
	}
}

/* Times n runs of fn between two counter reads */
static void
time_loop(xu (*volatile fn)(unsigned), unsigned n, uint64_t *cycles, uint64_t *insns)
{
	uint64_t c0, i0, c1, i1;

	fn(HART_TEST_WARMUP);
	c0 = read_cycles();
	i0 = read_instret();
	fn(n);
	i1 = read_instret();
	c1 = read_cycles();
	*cycles = c1 - c0;
	*insns = i1 - i0;
}

/*
 * The SDK has no persistent per-hart capability record (hs->caps only points at
 * the caller's struct while hart_probe_priv_caps() runs), and probing always
 * looks at the hart it runs on. So each hart is probed once, on itself, the first
 * time it is selected, and the result is kept here.
 */
static struct rvcaps caps_tbl[PLAT_MAX_HARTS];
static atomic_uint caps_done[PLAT_MAX_HARTS];

static void
caps_entry(uint64_t arg0, uint64_t arg1)
{
	struct hart_state *hs = hart_get_hstate_self();

	(void)arg0;
	(void)arg1;
	hart_probe_priv_caps(&caps_tbl[hs->hart_idx]);
	atomic_store_explicit(&caps_done[hs->hart_idx], 1, memory_order_release);
}

/* Waits until *flag is set, false on timeout */
static bool
wait_flag(atomic_uint *flag)
{
	uint64_t spins = HART_TEST_TIMEOUT;

	while (!atomic_load_explicit(flag, memory_order_acquire) && --spins)
		pause();
	return spins != 0;
}

/* Capabilities of a hart, probing it first if needed. NULL if that fails. */
static const struct rvcaps *
get_caps(struct hart_state *self, struct hart_state *target)
{
	uint16_t idx = target->hart_idx;

	if (atomic_load_explicit(&caps_done[idx], memory_order_acquire))
		return &caps_tbl[idx];

	if (target == self) {
		caps_entry(0, 0);
		return &caps_tbl[idx];
	}
#if defined(PLAT_NO_IPI)
	return NULL;
#else
	hart_wakeup_with_addr(idx, (uintptr_t)caps_entry, 0, 0, 0);
	return wait_flag(&caps_done[idx]) ? &caps_tbl[idx] : NULL;
#endif
}

/* Runs on the target hart, entered through hart_wakeup_with_addr() (or called
 * directly when the target is the calling hart). Returns when done, the wakeup
 * code then parks the hart until the next job. */
static void
stress_entry(uint64_t ext_idx, uint64_t iters)
{
	struct hart_state *hs = hart_get_hstate_self();
	const struct rvcaps *caps = &caps_tbl[hs->hart_idx];
	struct stress_job *job = &jobs[hs->hart_idx];
	const struct stress_ext *e = &stress_exts[ext_idx];
	bool have_inhibit = !!(caps->m_caps & CAP_MCOUNTINHIBIT);
	unsigned long mie = csr_read(CSR_MSTATUS) & CSR_MSTATUS_MIE;
	unsigned long inhibit = 0;
	struct unit_state us;

	/* No interrupts while we measure */
	hart_block_interrupts();
	unit_enter(&us, e->units);

	/* Counters are inhibited at boot, let cycle and instret run */
	if (have_inhibit) {
		inhibit = csr_read(CSR_MCOUNTINHIBIT);
		csr_clear_bits(CSR_MCOUNTINHIBIT, BIT(HC_CYCLES) | BIT(HC_INSTRET));
	}

	job->bad = e->chk((unsigned)iters, e->want());
	time_loop(e->run, (unsigned)iters, &job->cycles, &job->insns);
	time_loop(run_empty, (unsigned)iters, &job->cal_cycles, &job->cal_insns);

	if (have_inhibit)
		csr_write(CSR_MCOUNTINHIBIT, inhibit);
	unit_leave(&us);
	if (mie)
		hart_allow_interrupts();

	atomic_store_explicit(&job->done, 1, memory_order_release);
}

/*********\
* UI      *
\*********/

/* Selection labels: 0-9, a-z, A-Z */
static const char labels[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

static int
label_to_idx(int c)
{
	for (int i = 0; labels[i]; i++)
		if (labels[i] == c)
			return i;
	return -1;
}

static int
get_key(void)
{
	int c;

	while ((c = getchar()) == EOF)
		pause();
	return c;
}

/* 0 = not present on this hart, 1 = present, -1 = present but failed its check */
static int
ext_status(const struct rvcaps *caps, const struct stress_ext *e)
{
	if (!(caps->z_caps & e->cap) ||
	    (caps->z_caps & e->need_z) != e->need_z ||
	    (caps->r_caps & e->need_r) != e->need_r)
		return 0;
	return (caps->z_broken & e->cap) ? -1 : 1;
}

/* Runs one extension on the target hart, prints the result. Returns the number of failures. */
static int
run_ext(struct hart_state *self, struct hart_state *target, unsigned ext_idx)
{
	const struct stress_ext *e = &stress_exts[ext_idx];
	struct stress_job *job = &jobs[target->hart_idx];

	job->ext_idx = ext_idx;
	job->iters = HART_TEST_ITERATIONS;
	atomic_store_explicit(&job->done, 0, memory_order_relaxed);

	if (target == self) {
		stress_entry(ext_idx, job->iters);
	} else {
#if defined(PLAT_NO_IPI)
		ERR("%s: this target can't wake up other harts\n", e->name);
		return 1;
#else
		hart_wakeup_with_addr(target->hart_idx, (uintptr_t)stress_entry,
				      ext_idx, job->iters, 0);
		if (!wait_flag(&job->done)) {
			ERR("%s: hart %i did not report back\n", e->name, target->hart_idx);
			return 1;
		}
#endif
	}

	uint64_t cycles = job->cycles, insns = job->insns;

	if (!job->cycles || !job->insns) {
		WRN("%-26s counters did not advance (mcycle/minstret not running?)\n", e->name);
	} else {
		/* Take the empty loop out: what's left is the block */
		cycles = cycles > job->cal_cycles ? cycles - job->cal_cycles : 0;
		insns = insns > job->cal_insns ? insns - job->cal_insns : 0;
		if (!cycles || !insns) {
			WRN("%-26s too short to measure against the loop overhead\n", e->name);
		} else {
			/* IPC in thousandths, printed as digits to avoid relying on %0Nd */
			unsigned long long ipc = (unsigned long long)insns * 1000 / cycles;

			INF("%-26s %10llu cycles %10llu insns %5llu insns/iter   IPC %llu.%llu%llu%llu\n",
			    e->name, (unsigned long long)cycles, (unsigned long long)insns,
			    (unsigned long long)(insns / job->iters), ipc / 1000,
			    ipc / 100 % 10, ipc / 10 % 10, ipc % 10);
		}
	}
	if (job->bad) {
		ERR("%-26s MISMATCH: %u of %u passes gave a wrong result\n",
		    e->name, job->bad, job->iters);
		return 1;
	}
	return 0;
}

static int
test_hart_stress(void)
{
	struct hart_state *self = hart_get_hstate_self();
	int num_harts = hart_get_count();
	int failures = 0;

	ANN("\n---=== Hart Extension Stress Test ===---\n");
	INF("%u passes per extension\n", HART_TEST_ITERATIONS);

	INF("Select a hart:\n");
	for (int i = 0; i < num_harts; i++) {
		struct hart_state *hs = hart_get_hstate_by_idx(i);
		INF("\t%i -> hart %i%s%s\n", i, i, i == self->hart_idx ? " (this one)" : "",
		    hart_test_flags(hs, HS_FLAG_READY) ? "" : " (not up)");
	}
	INF("\t-  -> any other key to go back\n");

	int key = get_key();
	int hidx = key - '0';
	if (hidx < 0 || hidx >= num_harts)
		return 0;

	struct hart_state *target = hart_get_hstate_by_idx(hidx);
	if (!hart_test_flags(target, HS_FLAG_READY)) {
		ERR("hart %i is not up\n", hidx);
		return 1;
	}

	INF("Probing hart %i...\n", hidx);
	const struct rvcaps *caps = get_caps(self, target);
	if (!caps) {
		ERR("Could not probe hart %i\n", hidx);
		return 1;
	}

	INF("Extensions found on hart %i:\n", hidx);
	unsigned present = 0;
	for (unsigned i = 0; i < NUM_EXTS && i < sizeof(labels) - 1; i++) {
		int st = ext_status(caps, &stress_exts[i]);

		if (st > 0) {
			INF("\t%c -> %s\n", labels[i], stress_exts[i].name);
			present++;
		} else if (st < 0) {
			INF("\t%c -> %s (broken, not stressed)\n", labels[i], stress_exts[i].name);
		}
	}
	if (!present) {
		WRN("Nothing to stress on hart %i\n", hidx);
		return 0;
	}
	INF("\t* -> all of the above\n");
	INF("\t-  -> any other key to go back\n");

	key = get_key();
	if (key == '*') {
		for (unsigned i = 0; i < NUM_EXTS; i++)
			if (ext_status(caps, &stress_exts[i]) > 0)
				failures += run_ext(self, target, i);
	} else {
		int idx = label_to_idx(key);

		if (idx < 0 || idx >= (int)NUM_EXTS || ext_status(caps, &stress_exts[idx]) <= 0)
			return 0;
		failures += run_ext(self, target, idx);
	}

	if (!failures)
		INF("No mismatches\n");
	INF("Press a key to continue...\n");
	return failures;
}

REGISTER_HART_TEST("Hart extension stress test", test_hart_stress);