/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Instruction blocks, one per extension.
 *
 * Each block is a complete inline asm statement that brackets its instructions
 * with ".option push / .option arch / .option pop", so the assembler encodes the
 * real mnemonics whatever -march the SDK is built with. The same block is used
 * for probing (run once with HS_FLAG_PROBING set, see hart_probe_unpriv.c) and for
 * stress testing (run N times from a timed loop). No probing logic lives here.
 *
 * Blocks are macros so they can be pasted several times in a row without call
 * overhead. Arguments are lvalues/expressions, see each block.
 */
#ifndef _RISCV_Z_EXT_BLOCKS_H
#define _RISCV_Z_EXT_BLOCKS_H

/*
 * With EXT_AUDIT_MARKERS defined every block is bracketed by two HINT nops (addi x0, x0, 1953 /
 * 1954), so an audit tool can tell the block's own instructions from the compiler's code
 * around it. Not defined in a normal build: the blocks then contain nothing but their own code.
 */
#ifdef EXT_AUDIT_MARKERS
#define EXT_AUDIT_B		"addi\tx0, x0, 1953\n\t"
#define EXT_AUDIT_E		"addi\tx0, x0, 1954\n\t"
#else
#define EXT_AUDIT_B
#define EXT_AUDIT_E
#endif
#define EXT_ASM_BEGIN(arch)	".option push\n\t.option norvc\n\t.option arch, " arch "\n\t" EXT_AUDIT_B
#define EXT_ASM_END		EXT_AUDIT_E ".option pop\n\t"

/*
 * XLEN-specific instructions: EXT_RV64("insn") / EXT_RV32("insn") expand to
 * the instruction on that XLEN and to nothing on the other.
 */
#if __riscv_xlen == 64
#define EXT_RV64(s)	s
#define EXT_RV32(s)
#define EXT_XLOAD	"ld"
#define EXT_XSTORE	"sd"
#define EXT_XBYTES	"8"
#define EXT_CASP_F3	"4"	/* amocas.q on a register pair */
#else
#define EXT_RV64(s)
#define EXT_RV32(s)	s
#define EXT_XLOAD	"lw"
#define EXT_XSTORE	"sw"
#define EXT_XBYTES	"4"
#define EXT_CASP_F3	"3"	/* amocas.d on a register pair */
#endif

/*
 * For instructions no assembler we care about knows yet (or that GNU as and
 * LLVM spell differently), and for the Zfinx family, whose encodings are the
 * F/D/Zfh ones with a different register file, the blocks use .insn with the
 * mnemonic in a comment. They don't depend on the assembler's -march at all.
 */
#define EXT_INSN_BEGIN	".option push\n\t.option norvc\n\t" EXT_AUDIT_B
#define EXT_INSN_END	EXT_AUDIT_E ".option pop\n\t"

/*
 * Checked scalar blocks (Zba Zbb Zbc Zbs Zicond Zmmul Zbkb Zbkc Zbkx)
 *
 * These blocks are small dataflows: EXT_BLOCK_Z<EXT>(a, b, x) computes x from the
 * inputs a and b, and every instruction of the extension sits on the path to x,
 * so a wrong result anywhere changes x. Instructions that throw information away
 * (clz, cpop, czero, ...) are folded back into x with a base-ISA add/xor.
 *
 * Each block has a C twin, ext_ref_z<ext>(a, b), built from the very same step
 * list, so the two always run the same steps in the same order. The twin uses
 * only base-ISA C, the per-instruction models (ref_*) are in hart_probe_ref.c.
 *
 * One block, three uses:
 *   probe:  run once with HS_FLAG_PROBING set, a trap means the extension is absent
 *   check:  compare x against ext_ref_z<ext>(a, b), a mismatch means "broken"
 *   stress: loop the block, compute the expected x once outside the timed region
 *
 * Operands: a, b are xu values (see ext_pair() below for a good operand set);
 * x is an xu lvalue. Inside the chains x is the accumulator, t a temporary.
 */

#include <stdint.h>

#if __riscv_xlen == 64
typedef uint64_t xu;
typedef int64_t xs;
#else
typedef uint32_t xu;
typedef int32_t xs;
#endif
#define EXT_XLEN	__riscv_xlen


/**** Step lists ****/

/*
 * One line per instruction: R(dst, "mnemonic", src1, src2, model) is
 * dst = op(src1, src2), U(dst, "mnemonic", src1, model) is dst = op(src1) and
 * I(dst, "mnemonic", src1, imm, model) is dst = op(src1, imm). The registers are
 * x (accumulator), t (temporary), a and b (inputs). The same list is expanded
 * once into assembly and once into C, see EXT_CHAIN_BLOCK / EXT_CHAIN_REF.
 */

/* Zba */
#define ZBA_STEPS_COMMON(R, U, I) \
	R(x, "sh1add",     a, b, ref_sh1add) \
	R(x, "sh2add",     x, b, ref_sh2add) \
	R(x, "sh3add",     x, a, ref_sh3add)
#define ZBA_STEPS_RV64(R, U, I) \
	R(x, "add.uw",     x, b, ref_add_uw) \
	R(x, "sh1add.uw",  x, a, ref_sh1add_uw) \
	R(x, "sh2add.uw",  x, b, ref_sh2add_uw) \
	R(x, "sh3add.uw",  x, a, ref_sh3add_uw) \
	I(x, "slli.uw",    x, 7, ref_slli_uw)

/* Zbb */
#define ZBB_STEPS_COMMON(R, U, I) \
	R(x, "andn",   a, b, ref_andn) \
	R(x, "orn",    x, b, ref_orn) \
	R(x, "xnor",   x, a, ref_xnor) \
	R(t, "max",    x, a, ref_max) \
	R(x, "min",    x, b, ref_min) \
	R(x, "add",    x, t, ref_add) \
	R(t, "maxu",   x, a, ref_maxu) \
	R(x, "minu",   x, b, ref_minu) \
	R(x, "add",    x, t, ref_add) \
	R(x, "rol",    x, b, ref_rol) \
	R(x, "ror",    x, a, ref_ror) \
	I(x, "rori",   x, 13, ref_ror) \
	U(t, "clz",    x, ref_clz) \
	R(x, "add",    x, t, ref_add) \
	U(t, "ctz",    x, ref_ctz) \
	R(x, "add",    x, t, ref_add) \
	U(t, "cpop",   x, ref_cpop) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sext.b", x, ref_sext_b) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sext.h", x, ref_sext_h) \
	R(x, "add",    x, t, ref_add) \
	U(t, "zext.h", x, ref_zext_h) \
	R(x, "add",    x, t, ref_add) \
	U(t, "orc.b",  x, ref_orc_b) \
	R(x, "add",    x, t, ref_add) \
	U(x, "rev8",   x, ref_rev8)
#define ZBB_STEPS_RV64(R, U, I) \
	R(t, "rolw",   x, b, ref_rolw) \
	R(x, "add",    x, t, ref_add) \
	R(t, "rorw",   x, a, ref_rorw) \
	R(x, "add",    x, t, ref_add) \
	I(t, "roriw",  x, 13, ref_rorw) \
	R(x, "add",    x, t, ref_add) \
	U(t, "clzw",   x, ref_clzw) \
	R(x, "add",    x, t, ref_add) \
	U(t, "ctzw",   x, ref_ctzw) \
	R(x, "add",    x, t, ref_add) \
	U(t, "cpopw",  x, ref_cpopw) \
	R(x, "add",    x, t, ref_add)

/* Zbc */
#define ZBC_STEPS_COMMON(R, U, I) \
	R(x, "clmulh", a, b, ref_clmulh) \
	R(x, "clmulr", x, b, ref_clmulr) \
	R(x, "clmul",  x, a, ref_clmul)

/* Zbs */
#define ZBS_STEPS_COMMON(R, U, I) \
	R(x, "bset",   a, b, ref_bset) \
	R(x, "binv",   x, a, ref_binv) \
	R(t, "bext",   x, b, ref_bext) \
	R(x, "add",    x, t, ref_add) \
	R(x, "bclr",   x, x, ref_bclr) \
	I(x, "bseti",  x, 21, ref_bset) \
	I(x, "binvi",  x, 11, ref_binv) \
	I(x, "bclri",  x, 29, ref_bclr) \
	I(t, "bexti",  x, 7, ref_bext) \
	R(x, "add",    x, t, ref_add)

/* Zicond */
#define ZICOND_STEPS_COMMON(R, U, I) \
	R(x, "czero.eqz", a, b, ref_czero_eqz) \
	R(t, "czero.nez", a, b, ref_czero_nez) \
	R(x, "add",       x, t, ref_add) \
	R(t, "czero.eqz", b, a, ref_czero_eqz) \
	R(x, "xor",       x, t, ref_xor) \
	R(t, "czero.nez", b, a, ref_czero_nez) \
	R(x, "add",       x, t, ref_add)

/* Zmmul */
#define ZMMUL_STEPS_COMMON(R, U, I) \
	R(x, "mul",    a, b, ref_mul) \
	R(t, "mulh",   a, b, ref_mulh) \
	R(x, "add",    x, t, ref_add) \
	R(t, "mulhsu", x, b, ref_mulhsu) \
	R(x, "add",    x, t, ref_add) \
	R(t, "mulhu",  x, a, ref_mulhu) \
	R(x, "add",    x, t, ref_add)
#define ZMMUL_STEPS_RV64(R, U, I) \
	R(t, "mulw",   x, b, ref_mulw) \
	R(x, "add",    x, t, ref_add)

/* Zbkb */
#define ZBKB_STEPS_COMMON(R, U, I) \
	R(x, "pack",   a, b, ref_pack) \
	R(t, "packh",  x, b, ref_packh) \
	R(x, "add",    x, t, ref_add) \
	R(x, "andn",   x, b, ref_andn) \
	R(x, "orn",    x, a, ref_orn) \
	R(x, "xnor",   x, b, ref_xnor) \
	R(x, "rol",    x, a, ref_rol) \
	R(x, "ror",    x, b, ref_ror) \
	I(x, "rori",   x, 13, ref_ror) \
	U(x, "rev8",   x, ref_rev8) \
	U(x, "brev8",  x, ref_brev8)
#define ZBKB_STEPS_RV64(R, U, I) \
	R(t, "rolw",   x, a, ref_rolw) \
	R(x, "add",    x, t, ref_add) \
	R(t, "rorw",   x, b, ref_rorw) \
	R(x, "add",    x, t, ref_add) \
	R(t, "packw",  x, a, ref_packw) \
	R(x, "add",    x, t, ref_add) \
	I(t, "roriw",  x, 13, ref_rorw) \
	R(x, "add",    x, t, ref_add)
#define ZBKB_STEPS_RV32(R, U, I) \
	U(x, "zip",    x, ref_zip) \
	R(x, "rol",    x, a, ref_rol) \
	U(x, "unzip",  x, ref_unzip)

/* Zbkc */
#define ZBKC_STEPS_COMMON(R, U, I) \
	R(x, "clmul",  a, b, ref_clmul) \
	R(t, "clmulh", a, b, ref_clmulh) \
	R(x, "add",    x, t, ref_add) \
	R(x, "clmul",  x, a, ref_clmul) \
	R(t, "clmulh", x, b, ref_clmulh) \
	R(x, "add",    x, t, ref_add)

/* Zbkx */
#define ZBKX_STEPS_COMMON(R, U, I) \
	R(x, "xperm4", a, b, ref_xperm4) \
	R(t, "xperm8", a, b, ref_xperm8) \
	R(x, "add",    x, t, ref_add) \
	R(t, "xperm4", b, x, ref_xperm4) \
	R(x, "xor",    x, t, ref_xor) \
	R(t, "xperm8", x, a, ref_xperm8) \
	R(x, "add",    x, t, ref_add)

/* Whole lists: the common steps, then the XLEN specific ones */
#define ZBA_STEPS(R, U, I)	ZBA_STEPS_COMMON(R, U, I) EXT_ON64(ZBA_STEPS_RV64(R, U, I))
#define ZBB_STEPS(R, U, I)	ZBB_STEPS_COMMON(R, U, I) EXT_ON64(ZBB_STEPS_RV64(R, U, I))
#define ZBC_STEPS(R, U, I)	ZBC_STEPS_COMMON(R, U, I)
#define ZBS_STEPS(R, U, I)	ZBS_STEPS_COMMON(R, U, I)
#define ZICOND_STEPS(R, U, I)	ZICOND_STEPS_COMMON(R, U, I)
#define ZMMUL_STEPS(R, U, I)	ZMMUL_STEPS_COMMON(R, U, I) EXT_ON64(ZMMUL_STEPS_RV64(R, U, I))
#define ZBKB_STEPS(R, U, I)	ZBKB_STEPS_COMMON(R, U, I) EXT_ON64(ZBKB_STEPS_RV64(R, U, I)) EXT_ON32(ZBKB_STEPS_RV32(R, U, I))
#define ZBKC_STEPS(R, U, I)	ZBKC_STEPS_COMMON(R, U, I)
#define ZBKX_STEPS(R, U, I)	ZBKX_STEPS_COMMON(R, U, I)

#if __riscv_xlen == 64
#define EXT_ON64(s)	s
#define EXT_ON32(s)
#else
#define EXT_ON64(s)
#define EXT_ON32(s)	s
#endif

/**** Expanders ****/

/*
 * Register names inside a chain. Operands are referenced by symbolic asm names
 * (%[o_x]), so a block can declare whatever outputs and inputs it needs:
 * x accumulator, t temporary, p q u v extra temporaries, a b c d inputs.
 */
#define EXT_R_x	"%[o_x]"
#define EXT_R_t	"%[o_t]"
#define EXT_R_p	"%[o_p]"
#define EXT_R_q	"%[o_q]"
#define EXT_R_u	"%[o_u]"
#define EXT_R_v	"%[o_v]"
#define EXT_R_a	"%[o_a]"
#define EXT_R_b	"%[o_b]"
#define EXT_R_c	"%[o_c]"
#define EXT_R_d	"%[o_d]"

#define EXT_ASM_R(dst, mn, s1, s2, fn)	mn "\t" EXT_R_##dst ", " EXT_R_##s1 ", " EXT_R_##s2 "\n\t"
#define EXT_ASM_U(dst, mn, s1, fn)	mn "\t" EXT_R_##dst ", " EXT_R_##s1 "\n\t"
#define EXT_ASM_I(dst, mn, s1, imm, fn)	mn "\t" EXT_R_##dst ", " EXT_R_##s1 ", " #imm "\n\t"


/* x and t are early-clobber outputs: they're written before a and b are done being read */
#define EXT_CHAIN_BLOCK(arch, steps, a, b, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN(arch) \
			steps(EXT_ASM_R, EXT_ASM_U, EXT_ASM_I) \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) : [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)


#define EXT_BLOCK_ZBA(a, b, x)		EXT_CHAIN_BLOCK("+zba", ZBA_STEPS, a, b, x)
#define EXT_BLOCK_ZBB(a, b, x)		EXT_CHAIN_BLOCK("+zbb", ZBB_STEPS, a, b, x)
#define EXT_BLOCK_ZBC(a, b, x)		EXT_CHAIN_BLOCK("+zbc", ZBC_STEPS, a, b, x)
#define EXT_BLOCK_ZBS(a, b, x)		EXT_CHAIN_BLOCK("+zbs", ZBS_STEPS, a, b, x)
#define EXT_BLOCK_ZICOND(a, b, x)	EXT_CHAIN_BLOCK("+zicond", ZICOND_STEPS, a, b, x)
#define EXT_BLOCK_ZMMUL(a, b, x)	EXT_CHAIN_BLOCK("+zmmul", ZMMUL_STEPS, a, b, x)
#define EXT_BLOCK_ZBKB(a, b, x)		EXT_CHAIN_BLOCK("+zbkb", ZBKB_STEPS, a, b, x)
#define EXT_BLOCK_ZBKC(a, b, x)		EXT_CHAIN_BLOCK("+zbkc", ZBKC_STEPS, a, b, x)
#define EXT_BLOCK_ZBKX(a, b, x)		EXT_CHAIN_BLOCK("+zbkx", ZBKX_STEPS, a, b, x)


/**** Operands ****/

/*
 * Mixed bit patterns, none of them toy values: ascending/descending bytes, sign
 * bit set with the low bit set, alternating nibbles, a sign-extension boundary,
 * a 32-bit all-ones and two random-looking values. Truncated on RV32.
 * ext_pair(i) gives EXT_NPAIRS (a, b) pairs: 8 from the set, then three with a
 * zero operand (for czero, shifts, multiply).
 */
#define EXT_NVALS	8
#define EXT_NPAIRS	(EXT_NVALS + 3)

void ext_pair(unsigned i, xu *a, xu *b);

/*
 * C twins (hart_probe_ref.c). ext_ref_z<ext>(a, b) computes what the matching
 * EXT_BLOCK_Z<EXT>(a, b, ..., x) must leave in x. For the step-list blocks it is
 * generated from the same list as the block, the others are written by hand.
 */
xu ext_ref_zba(xu a, xu b);
xu ext_ref_zbb(xu a, xu b);
xu ext_ref_zbc(xu a, xu b);
xu ext_ref_zbs(xu a, xu b);
xu ext_ref_zicond(xu a, xu b);
xu ext_ref_zmmul(xu a, xu b);
xu ext_ref_zbkb(xu a, xu b);
xu ext_ref_zbkc(xu a, xu b);
xu ext_ref_zbkx(xu a, xu b);
xu ext_ref_zknh(xu a, xu b);
xu ext_ref_zksh(xu a, xu b);
xu ext_ref_zaamo(xu a, xu b);
xu ext_ref_zabha(xu a, xu b);
xu ext_ref_zalrsc(xu a, xu b);
xu ext_ref_zacas(xu a, xu b);
xu ext_ref_zifencei(xu k1, xu k2);

/**** Atomics: Zaamo, Zalrsc, Zabha, Zacas ****/

/*
 * Written with .insn (opcode 0x2f), the mnemonic is in a comment. A block gets
 * two XLEN values a, b and the address mp of a 16-byte aligned memory area
 * (uint64_t mem[2]), and returns a signature x folded from every value the
 * instructions return plus the memory they leave behind, so a wrong old value,
 * a wrong new value or a write outside the access width all change x.
 * ext_ref_z<ext>(a, b) is the C twin. The aq/rl bits are varied between the
 * instructions on purpose, they must not change the result.
 */


/*
 * The nine AMOs, in EXT_AMO_* order. AMO(f3, funct7, source): funct7 =
 * funct5 << 2 | aq << 1 | rl, f3 the width. Sources alternate b, a, b, ...
 */
#define EXT_AMO_SEQ(AMO, f3) \
	AMO(f3, 6,   b)	/* amoswap.aq */ \
	AMO(f3, 1,   a)	/* amoadd.rl */ \
	AMO(f3, 19,  b)	/* amoxor.aqrl */ \
	AMO(f3, 32,  a)	/* amoor */ \
	AMO(f3, 48,  b)	/* amoand */ \
	AMO(f3, 80,  a)	/* amomax */ \
	AMO(f3, 112, b)	/* amomaxu */ \
	AMO(f3, 64,  a)	/* amomin */ \
	AMO(f3, 96,  b)	/* amominu */

#define EXT_AMO_ASM(f3, f7, src) \
	".insn r 0x2f, " #f3 ", " #f7 ", %[o_t], %[o_m], %[o_" #src "]\n\t" \
	"add	%[o_x], %[o_x], %[o_t]\n\t"

/* Zaamo: amo<op>.w and, on RV64, amo<op>.d. Memory is set to a before the .w
 * group and to b before the .d group, and read back after each. */
#define EXT_BLOCK_ZAAMO(a, b, mp, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			EXT_XSTORE "	%[o_a], 0(%[o_m])\n\t" \
			EXT_AMO_SEQ(EXT_AMO_ASM, 2) \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("sd	%[o_b], 0(%[o_m])\n\t") \
			EXT_RV64(EXT_AMO_SEQ(EXT_AMO_ASM, 3)) \
			EXT_RV64("ld	%[o_t], 0(%[o_m])\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_m] "r" (mp) : "memory"); \
	} while (0)


/* Zabha: the same nine AMOs on bytes (f3 = 0) and halfwords (f3 = 1). The
 * whole word is read back each time, so a write outside the byte/halfword shows. */
#define EXT_BLOCK_ZABHA(a, b, mp, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			EXT_XSTORE "	%[o_a], 0(%[o_m])\n\t" \
			EXT_AMO_SEQ(EXT_AMO_ASM, 0) \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_XSTORE "	%[o_b], 0(%[o_m])\n\t" \
			EXT_AMO_SEQ(EXT_AMO_ASM, 1) \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_m] "r" (mp) : "memory"); \
	} while (0)


/*
 * Zalrsc: the canonical fetch-and-add loop (lr, add, sc, retry) for .w and,
 * on RV64, .d. lr/sc sequences may fail spuriously so the loop retries (64
 * times at most, then gives up and the status ends up in the signature). Then
 * an sc without a reservation, which must fail. Per width the signature gets
 * the lr value, the final sc status of the loop (0), 1 for the failing sc, and
 * the memory afterwards. Variants: lr.aq, sc.rl.
 */
#define EXT_LRSC_ASM(f3, lrf7, scf7) \
	"li	%[o_q], 64\n\t" \
	"1:\n\t" \
	".insn r 0x2f, " #f3 ", " #lrf7 ", %[o_t], %[o_m], zero\n\t"		/* lr */ \
	"add	%[o_u], %[o_t], %[o_b]\n\t" \
	".insn r 0x2f, " #f3 ", " #scf7 ", %[o_p], %[o_m], %[o_u]\n\t"		/* sc */ \
	"beqz	%[o_p], 2f\n\t" \
	"addi	%[o_q], %[o_q], -1\n\t" \
	"bnez	%[o_q], 1b\n\t" \
	"2:\n\t" \
	"add	%[o_x], %[o_x], %[o_t]\n\t" \
	"add	%[o_x], %[o_x], %[o_p]\n\t" \
	".insn r 0x2f, " #f3 ", " #scf7 ", %[o_p], %[o_m], %[o_b]\n\t"		/* sc, no reservation */ \
	"snez	%[o_p], %[o_p]\n\t" \
	"add	%[o_x], %[o_x], %[o_p]\n\t"

#define EXT_BLOCK_ZALRSC(a, b, mp, x) \
	do { \
		xu t_, p_, q_, u_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			EXT_XSTORE "	%[o_a], 0(%[o_m])\n\t" \
			EXT_LRSC_ASM(2, 10, 13)		/* lr.w.aq, sc.w.rl */ \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("sd	%[o_b], 0(%[o_m])\n\t") \
			EXT_RV64(EXT_LRSC_ASM(3, 8, 12))	/* lr.d, sc.d */ \
			EXT_RV64("ld	%[o_t], 0(%[o_m])\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_m] "r" (mp) : "memory"); \
	} while (0)


/*
 * Zacas: amocas.w and, on RV64, amocas.d, then the double-width form on
 * register pairs (amocas.q on RV64, amocas.d on RV32, which use a2/a3 as the
 * expected/old pair and a4/a5 as the new pair). Each width does a compare-and-swap
 * that matches (the memory takes the new value) and one that doesn't (the memory
 * keeps its value, rd gets it).
 */
#define EXT_BLOCK_ZACAS(a, b, mp, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			EXT_XSTORE "	%[o_a], 0(%[o_m])\n\t" \
			"mv	%[o_t], %[o_a]\n\t" \
			".insn r 0x2f, 2, 22, %[o_t], %[o_m], %[o_b]\n\t"	/* amocas.w.aq t, b, (m) */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"mv	%[o_t], %[o_a]\n\t" \
			".insn r 0x2f, 2, 21, %[o_t], %[o_m], %[o_a]\n\t"	/* amocas.w.rl t, a, (m) */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("sd	%[o_a], 0(%[o_m])\n\t") \
			EXT_RV64("mv	%[o_t], %[o_a]\n\t") \
			EXT_RV64(".insn r 0x2f, 3, 20, %[o_t], %[o_m], %[o_b]\n\t")	/* amocas.d t, b, (m) */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("mv	%[o_t], %[o_a]\n\t") \
			EXT_RV64(".insn r 0x2f, 3, 23, %[o_t], %[o_m], %[o_a]\n\t")	/* amocas.d.aqrl t, a, (m) */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("ld	%[o_t], 0(%[o_m])\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_XSTORE "	%[o_a], 0(%[o_m])\n\t" \
			EXT_XSTORE "	%[o_b], " EXT_XBYTES "(%[o_m])\n\t" \
			"mv	a2, %[o_a]\n\t" \
			"mv	a3, %[o_b]\n\t" \
			"mv	a4, %[o_b]\n\t" \
			"mv	a5, %[o_a]\n\t" \
			".insn r 0x2f, " EXT_CASP_F3 ", 20, a2, %[o_m], a4\n\t"	/* amocas.q/.d a2, a4, (m) */ \
			"add	%[o_x], %[o_x], a2\n\t" \
			"slli	%[o_t], a3, 1\n\t"	/* weight the halves differently: a swapped pair must not cancel */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"mv	a2, %[o_a]\n\t" \
			"mv	a3, %[o_b]\n\t" \
			".insn r 0x2f, " EXT_CASP_F3 ", 20, a2, %[o_m], a4\n\t" \
			"add	%[o_x], %[o_x], a2\n\t" \
			"slli	%[o_t], a3, 1\n\t"	/* weight the halves differently: a swapped pair must not cancel */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_XLOAD "	%[o_t], 0(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_XLOAD "	%[o_t], " EXT_XBYTES "(%[o_m])\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_m] "r" (mp) \
			: "a2", "a3", "a4", "a5", "memory"); \
	} while (0)


/**** Zawrs, Zifencei, Zicsr, Zimop, Zcmop ****/

/* Zawrs: wrs.nto, wrs.sto. No reservation is held, so both may return at once
 * without stalling. There is no result to check, this only shows they execute. */
#define EXT_BLOCK_ZAWRS() \
	__asm__ volatile ( \
		EXT_ASM_BEGIN("+zawrs") \
		"wrs.nto\n\t" \
		"wrs.sto\n\t" \
		EXT_ASM_END \
		: : : "memory")

/*
 * Zifencei: self-modifying code. Writes "addi a0, zero, k1" and "ret" into the
 * buffer, fence.i, calls it; then rewrites the first word to return k2, fence.i
 * and calls again. Without a working fence.i the old instruction can run, and
 * the signature (k1 + (k2 << 16)) is wrong. k1, k2 are < 0x800, code is a 16-byte
 * aligned uint32_t[2] in memory the hart may execute from. Clobbers a0 and ra.
 */
#define EXT_BLOCK_ZIFENCEI(k1, k2, code, x) \
	do { \
		xu t_, u_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zifencei") \
			"li	%[o_u], 0x8067\n\t"				/* ret = jalr zero, 0(ra) */ \
			"sw	%[o_u], 4(%[o_c])\n\t" \
			"slli	%[o_t], %[o_k1], 20\n\t" \
			"ori	%[o_t], %[o_t], 0x513\n\t"			/* addi a0, zero, k1 */ \
			"sw	%[o_t], 0(%[o_c])\n\t" \
			"fence.i\n\t" \
			"jalr	ra, 0(%[o_c])\n\t" \
			"mv	%[o_x], a0\n\t" \
			"slli	%[o_t], %[o_k2], 20\n\t" \
			"ori	%[o_t], %[o_t], 0x513\n\t"			/* addi a0, zero, k2 */ \
			"sw	%[o_t], 0(%[o_c])\n\t" \
			"fence.i\n\t" \
			"jalr	ra, 0(%[o_c])\n\t" \
			"slli	%[o_t], a0, 16\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_u] "=&r" (u_) \
			: [o_k1] "r" (k1), [o_k2] "r" (k2), [o_c] "r" (code) \
			: "a0", "ra", "memory"); \
	} while (0)


/*
 * Zicsr: every CSR instruction form (csrrw csrrs csrrc csrrwi csrrsi csrrci) on one CSR
 * (mstatus: mscratch can't be used, the trap handler needs it), each with an argument that
 * leaves the CSR unchanged (read, set no bits, clear no bits, write back what was read, set
 * bits that are already set, clear bits that are already clear). The result register is
 * loaded with -1 before every instruction, so an instruction that does not run (or does not
 * write rd) leaves -1 behind, which differs from the value read first. Each result must
 * equal the value read first, and so must the CSR at the end. x is the OR of all the
 * differences: 0 when everything behaved. csrrwi can only write a constant, so it writes 0 and
 * the value is written straight back with csrw (clearing the low mstatus enable bits for
 * one instruction is harmless; nothing can trap or interrupt in between).
 */
#ifndef EXT_ZICSR_CSR
#define EXT_ZICSR_CSR	"mstatus"
#endif
#define EXT_ZICSR_CHK \
	"xor	%[o_u], %[o_u], %[o_t]\n\t" \
	"or	%[o_x], %[o_x], %[o_u]\n\t"
#define EXT_ZICSR_SENT	"li	%[o_u], -1\n\t"
#define EXT_BLOCK_ZICSR(x) \
	do { \
		xu t_, u_, p_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zicsr") \
			"csrr	%[o_t], " EXT_ZICSR_CSR "\n\t" \
			"mv	%[o_x], zero\n\t" \
			EXT_ZICSR_SENT "csrrs	%[o_u], " EXT_ZICSR_CSR ", zero\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrc	%[o_u], " EXT_ZICSR_CSR ", zero\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrw	%[o_u], " EXT_ZICSR_CSR ", %[o_t]\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrs	%[o_u], " EXT_ZICSR_CSR ", %[o_t]\n\t" EXT_ZICSR_CHK \
			"not	%[o_p], %[o_t]\n\t" \
			EXT_ZICSR_SENT "csrrc	%[o_u], " EXT_ZICSR_CSR ", %[o_p]\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrsi	%[o_u], " EXT_ZICSR_CSR ", 0\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrci	%[o_u], " EXT_ZICSR_CSR ", 0\n\t" EXT_ZICSR_CHK \
			EXT_ZICSR_SENT "csrrwi	%[o_u], " EXT_ZICSR_CSR ", 0\n\t" EXT_ZICSR_CHK \
			"csrw	" EXT_ZICSR_CSR ", %[o_t]\n\t" \
			EXT_ZICSR_SENT "csrr	%[o_u], " EXT_ZICSR_CSR "\n\t" EXT_ZICSR_CHK \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_u] "=&r" (u_), [o_p] "=&r" (p_)); \
	} while (0)
#define EXT_EXP_ZICSR	0

/*
 * Zimop: all 32 mop.r.N and 8 mop.rr.N. Without an extension giving them a
 * meaning they write 0 to rd; rd starts as -1 so one that did nothing is caught.
 * x is the OR of all the results: 0 expected. (With Zicfiss active some of them
 * are shadow stack instructions, mop.r.N reads a, mop.rr.N reads a and b.)
 */
#define EXT_BLOCK_ZIMOP(a, b, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 64, %[o_t], %[o_a], x28\n\t"	/* mop.r.0 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 64, %[o_t], %[o_a], x29\n\t"	/* mop.r.1 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 64, %[o_t], %[o_a], x30\n\t"	/* mop.r.2 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 64, %[o_t], %[o_a], x31\n\t"	/* mop.r.3 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 66, %[o_t], %[o_a], x28\n\t"	/* mop.r.4 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 66, %[o_t], %[o_a], x29\n\t"	/* mop.r.5 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 66, %[o_t], %[o_a], x30\n\t"	/* mop.r.6 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 66, %[o_t], %[o_a], x31\n\t"	/* mop.r.7 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 68, %[o_t], %[o_a], x28\n\t"	/* mop.r.8 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 68, %[o_t], %[o_a], x29\n\t"	/* mop.r.9 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 68, %[o_t], %[o_a], x30\n\t"	/* mop.r.10 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 68, %[o_t], %[o_a], x31\n\t"	/* mop.r.11 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 70, %[o_t], %[o_a], x28\n\t"	/* mop.r.12 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 70, %[o_t], %[o_a], x29\n\t"	/* mop.r.13 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 70, %[o_t], %[o_a], x30\n\t"	/* mop.r.14 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 70, %[o_t], %[o_a], x31\n\t"	/* mop.r.15 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 96, %[o_t], %[o_a], x28\n\t"	/* mop.r.16 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 96, %[o_t], %[o_a], x29\n\t"	/* mop.r.17 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 96, %[o_t], %[o_a], x30\n\t"	/* mop.r.18 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 96, %[o_t], %[o_a], x31\n\t"	/* mop.r.19 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 98, %[o_t], %[o_a], x28\n\t"	/* mop.r.20 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 98, %[o_t], %[o_a], x29\n\t"	/* mop.r.21 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 98, %[o_t], %[o_a], x30\n\t"	/* mop.r.22 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 98, %[o_t], %[o_a], x31\n\t"	/* mop.r.23 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 100, %[o_t], %[o_a], x28\n\t"	/* mop.r.24 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 100, %[o_t], %[o_a], x29\n\t"	/* mop.r.25 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 100, %[o_t], %[o_a], x30\n\t"	/* mop.r.26 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 100, %[o_t], %[o_a], x31\n\t"	/* mop.r.27 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 102, %[o_t], %[o_a], x28\n\t"	/* mop.r.28 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 102, %[o_t], %[o_a], x29\n\t"	/* mop.r.29 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 102, %[o_t], %[o_a], x30\n\t"	/* mop.r.30 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 102, %[o_t], %[o_a], x31\n\t"	/* mop.r.31 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 65, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.0 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 67, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.1 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 69, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.2 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 71, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.3 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 97, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.4 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 99, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.5 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 101, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.6 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x73, 4, 103, %[o_t], %[o_a], %[o_b]\n\t"	/* mop.rr.7 */ \
			"or	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) : [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)
#define EXT_EXP_ZIMOP	0

/*
 * Zcmop: c.mop.1, .3 ... .15, emitted as raw halfwords (no assembler knows
 * them yet). They must leave every register alone; ra (x1), t0 (x5) and sp,
 * which Zicfiss' c.sspush/c.sspopchk would use, are compared before and after.
 * x is 0 when nothing changed.
 */
#define EXT_BLOCK_ZCMOP(x) \
	do { \
		xu t_, u_, p_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_t], ra\n\t" \
			"mv	%[o_u], t0\n\t" \
			"mv	%[o_p], sp\n\t" \
			".2byte	0x6081\n\t"	/* c.mop.1 */ \
			".2byte	0x6181\n\t"	/* c.mop.3 */ \
			".2byte	0x6281\n\t"	/* c.mop.5 */ \
			".2byte	0x6381\n\t"	/* c.mop.7 */ \
			".2byte	0x6481\n\t"	/* c.mop.9 */ \
			".2byte	0x6581\n\t"	/* c.mop.11 */ \
			".2byte	0x6681\n\t"	/* c.mop.13 */ \
			".2byte	0x6781\n\t"	/* c.mop.15 */ \
			"xor	%[o_t], %[o_t], ra\n\t" \
			"xor	%[o_u], %[o_u], t0\n\t" \
			"xor	%[o_p], %[o_p], sp\n\t" \
			"or	%[o_t], %[o_t], %[o_u]\n\t" \
			"or	%[o_x], %[o_t], %[o_p]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_u] "=&r" (u_), [o_p] "=&r" (p_)); \
	} while (0)
#define EXT_EXP_ZCMOP	0

/* ===== Floating point: Zfh Zfhmin Zfinx Zdinx Zhinx Zhinxmin =====
 * Inputs EXT_IN_<EXT>_A/_B are fixed; the block result must equal EXT_EXP_<EXT>.
 * Zfinx-family blocks use .insn (integer registers) so any assembler works. */
#define EXT_IN_ZFH_A	0x4780ULL
#define EXT_IN_ZFH_B	0x4100ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZFH	0x40f32210ULL
#else
#define EXT_EXP_ZFH	0x40f29402ULL
#endif
#define EXT_IN_ZFHMIN_A	0x4780ULL
#define EXT_IN_ZFHMIN_B	0x40f19000ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZFHMIN	0x81e20f18ULL
#else
#define EXT_EXP_ZFHMIN	0x81e20f18ULL
#endif
#define EXT_IN_ZFINX_A	0x40f00000ULL
#define EXT_IN_ZFINX_B	0x40200000ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZFINX	0x212560060ULL
#else
#define EXT_EXP_ZFINX	0x90960052ULL
#endif
#define EXT_IN_ZDINX_A	0x401e000000000000ULL
#define EXT_IN_ZDINX_B	0x4004000000000000ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZDINX	0x4268c00040f00060ULL
#endif
#define EXT_IN_ZHINX_A	0x4780ULL
#define EXT_IN_ZHINX_B	0x4100ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZHINX	0x40f2da90ULL
#else
#define EXT_EXP_ZHINX	0x40f24c82ULL
#endif
#define EXT_IN_ZHINXMIN_A	0x4780ULL
#define EXT_IN_ZHINXMIN_B	0x40f19000ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZHINXMIN	0x81e1c78cULL
#else
#define EXT_EXP_ZHINXMIN	0x81e1c78cULL
#endif

#define EXT_BLOCK_ZFH(a, b, hp, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zfh") \
			"mv	%[o_x], zero\n\t" \
			"fmv.h.x	ft0, %[o_a]\n\t" \
			"fmv.h.x	ft1, %[o_b]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fadd.h	ft2, ft0, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fsub.h	ft2, ft0, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fmul.h	ft2, ft0, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fdiv.h	ft2, ft0, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft3, %[o_t]\n\t" \
			"fmul.h	ft3, ft1, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft3\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fsqrt.h	ft2, ft3, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft4, %[o_t]\n\t" \
			"fsgnjn.h	ft4, ft1, ft0\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft4\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fmin.h	ft2, ft0, ft4\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fmax.h	ft2, ft0, ft4\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fsgnj.h	ft2, ft0, ft4\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fsgnjx.h	ft2, ft0, ft4\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fsgnjn.h	ft2, ft0, ft4\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fmadd.h	ft2, ft0, ft1, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fmsub.h	ft2, ft0, ft1, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fnmsub.h	ft2, ft0, ft1, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fnmadd.h	ft2, ft0, ft1, ft1, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"feq.h	%[o_t], ft0, ft0\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"flt.h	%[o_t], ft1, ft0\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"fle.h	%[o_t], ft0, ft1\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"fclass.h	%[o_t], ft4\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"fclass.h	%[o_t], ft0\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"fcvt.w.h	%[o_t], ft0, rtz\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			"fcvt.wu.h	%[o_t], ft0, rtz\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64("fcvt.l.h	%[o_t], ft0, rtz\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64("fcvt.lu.h	%[o_t], ft0, rtz\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fcvt.w.h	%[o_t], ft0, rtz\n\t" \
			"fcvt.h.w	ft2, %[o_t], rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft2, %[o_t]\n\t" \
			"fcvt.w.h	%[o_t], ft0, rtz\n\t" \
			"fcvt.h.wu	ft2, %[o_t], rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft2\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("li	%[o_t], 0x5a5a5a5a\n\t") \
			EXT_RV64("fmv.h.x	ft2, %[o_t]\n\t") \
			EXT_RV64("fcvt.w.h	%[o_t], ft0, rtz\n\t") \
			EXT_RV64("fcvt.h.l	ft2, %[o_t], rne\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64("fmv.x.h	%[o_t], ft2\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("li	%[o_t], 0x5a5a5a5a\n\t") \
			EXT_RV64("fmv.h.x	ft2, %[o_t]\n\t") \
			EXT_RV64("fcvt.w.h	%[o_t], ft0, rtz\n\t") \
			EXT_RV64("fcvt.h.lu	ft2, %[o_t], rne\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64("fmv.x.h	%[o_t], ft2\n\t") \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x	ft3, %[o_t]\n\t" \
			"fcvt.s.h	ft3, ft0\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.w	%[o_t], ft3\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft4, %[o_t]\n\t" \
			"fcvt.h.s	ft4, ft3, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft4\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a\n\t" \
			"sh	%[o_t], 0(%[o_h])\n\t" \
			"fsh	ft0, 0(%[o_h])\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft5, %[o_t]\n\t" \
			"flh	ft5, 0(%[o_h])\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft5\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_h] "r" (hp) \
			: "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "memory"); \
	} while (0)

#define EXT_BLOCK_ZFHMIN(a, b, hp, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zfhmin") \
			"mv	%[o_x], zero\n\t" \
			"fmv.h.x	ft0, %[o_a]\n\t" \
			"fmv.w.x	ft2, %[o_b]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x	ft1, %[o_t]\n\t" \
			"fcvt.s.h	ft1, ft0\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.w	%[o_t], ft1\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x	ft2, %[o_t]\n\t" \
			"fmv.w.x	ft2, %[o_b]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft3, %[o_t]\n\t" \
			"fcvt.h.s	ft3, ft2, rne\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft3\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x	ft4, %[o_t]\n\t" \
			"fcvt.s.h	ft4, ft3\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.w	%[o_t], ft4\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], 0x5a5a\n\t" \
			"sh	%[o_t], 0(%[o_h])\n\t" \
			"fsh	ft3, 0(%[o_h])\n\t" \
			"li	%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.h.x	ft5, %[o_t]\n\t" \
			"flh	ft5, 0(%[o_h])\n\t" \
			"li	%[o_t], -1\n\t" \
			"fmv.x.h	%[o_t], ft5\n\t" \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b), [o_h] "r" (hp) \
			: "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "memory"); \
	} while (0)

#define EXT_BLOCK_ZFINX(a, b, x) \
	do { \
		xu t_, p_, q_, u_, v_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0, %[o_p], %[o_a], %[o_b]\n\t"	/* fadd.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 4, %[o_p], %[o_a], %[o_b]\n\t"	/* fsub.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 8, %[o_p], %[o_a], %[o_b]\n\t"	/* fmul.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 12, %[o_p], %[o_a], %[o_b]\n\t"	/* fdiv.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 8, %[o_q], %[o_b], %[o_b]\n\t"	/* fmul.s */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 44, %[o_p], %[o_q], x0\n\t"	/* fsqrt.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 16, %[o_u], %[o_b], %[o_a]\n\t"	/* fsgnjn.s */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 20, %[o_p], %[o_a], %[o_u]\n\t"	/* fmin.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 20, %[o_p], %[o_a], %[o_u]\n\t"	/* fmax.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 16, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnj.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 2, 16, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjx.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 16, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjn.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x43, 0, 0, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmadd.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x47, 0, 0, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmsub.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4b, 0, 0, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmsub.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4f, 0, 0, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmadd.s */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 2, 80, %[o_t], %[o_a], %[o_a]\n\t"	/* feq.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 80, %[o_t], %[o_b], %[o_a]\n\t"	/* flt.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 0, 80, %[o_t], %[o_a], %[o_b]\n\t"	/* fle.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 112, %[o_t], %[o_u], x0\n\t"	/* fclass.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 112, %[o_t], %[o_a], x0\n\t"	/* fclass.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 96, %[o_t], %[o_a], x0\n\t"	/* fcvt.w.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 96, %[o_t], %[o_a], x1\n\t"	/* fcvt.wu.s */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 96, %[o_t], %[o_a], x2\n\t")	/* fcvt.l.s */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 96, %[o_t], %[o_a], x3\n\t")	/* fcvt.lu.s */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 104, %[o_p], %[o_t], x0\n\t"	/* fcvt.s.w */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 104, %[o_p], %[o_t], x1\n\t"	/* fcvt.s.wu */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 104, %[o_p], %[o_t], x2\n\t")	/* fcvt.s.l */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 104, %[o_p], %[o_t], x3\n\t")	/* fcvt.s.lu */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_) \
			: [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)

#if __riscv_xlen == 64
#define EXT_BLOCK_ZDINX(a, b, x) \
	do { \
		xu t_, p_, q_, u_, v_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 1, %[o_p], %[o_a], %[o_b]\n\t"	/* fadd.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 5, %[o_p], %[o_a], %[o_b]\n\t"	/* fsub.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 9, %[o_p], %[o_a], %[o_b]\n\t"	/* fmul.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 13, %[o_p], %[o_a], %[o_b]\n\t"	/* fdiv.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 9, %[o_q], %[o_b], %[o_b]\n\t"	/* fmul.d */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 45, %[o_p], %[o_q], x0\n\t"	/* fsqrt.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 17, %[o_u], %[o_b], %[o_a]\n\t"	/* fsgnjn.d */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 21, %[o_p], %[o_a], %[o_u]\n\t"	/* fmin.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 21, %[o_p], %[o_a], %[o_u]\n\t"	/* fmax.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 17, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnj.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 2, 17, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjx.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 17, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjn.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x43, 0, 1, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmadd.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x47, 0, 1, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmsub.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4b, 0, 1, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmsub.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4f, 0, 1, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmadd.d */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 2, 81, %[o_t], %[o_a], %[o_a]\n\t"	/* feq.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 81, %[o_t], %[o_b], %[o_a]\n\t"	/* flt.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 0, 81, %[o_t], %[o_a], %[o_b]\n\t"	/* fle.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 113, %[o_t], %[o_u], x0\n\t"	/* fclass.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 113, %[o_t], %[o_a], x0\n\t"	/* fclass.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 97, %[o_t], %[o_a], x0\n\t"	/* fcvt.w.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 97, %[o_t], %[o_a], x1\n\t"	/* fcvt.wu.d */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 97, %[o_t], %[o_a], x2\n\t")	/* fcvt.l.d */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 97, %[o_t], %[o_a], x3\n\t")	/* fcvt.lu.d */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 105, %[o_p], %[o_t], x0\n\t"	/* fcvt.d.w */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 105, %[o_p], %[o_t], x1\n\t"	/* fcvt.d.wu */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 105, %[o_p], %[o_t], x2\n\t")	/* fcvt.d.l */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 105, %[o_p], %[o_t], x3\n\t")	/* fcvt.d.lu */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 32, %[o_q], %[o_a], x1\n\t"	/* fcvt.s.d */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 33, %[o_u], %[o_q], x0\n\t"	/* fcvt.d.s */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_) \
			: [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)
#endif

#define EXT_BLOCK_ZHINX(a, b, x) \
	do { \
		xu t_, p_, q_, u_, v_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 2, %[o_p], %[o_a], %[o_b]\n\t"	/* fadd.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 6, %[o_p], %[o_a], %[o_b]\n\t"	/* fsub.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 10, %[o_p], %[o_a], %[o_b]\n\t"	/* fmul.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 14, %[o_p], %[o_a], %[o_b]\n\t"	/* fdiv.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 10, %[o_q], %[o_b], %[o_b]\n\t"	/* fmul.h */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 46, %[o_p], %[o_q], x0\n\t"	/* fsqrt.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 18, %[o_u], %[o_b], %[o_a]\n\t"	/* fsgnjn.h */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 22, %[o_p], %[o_a], %[o_u]\n\t"	/* fmin.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 22, %[o_p], %[o_a], %[o_u]\n\t"	/* fmax.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 18, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnj.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 2, 18, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjx.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 18, %[o_p], %[o_a], %[o_u]\n\t"	/* fsgnjn.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x43, 0, 2, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmadd.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x47, 0, 2, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fmsub.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4b, 0, 2, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmsub.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r4 0x4f, 0, 2, %[o_p], %[o_a], %[o_b], %[o_b]\n\t"	/* fnmadd.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 2, 82, %[o_t], %[o_a], %[o_a]\n\t"	/* feq.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 82, %[o_t], %[o_b], %[o_a]\n\t"	/* flt.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 0, 82, %[o_t], %[o_a], %[o_b]\n\t"	/* fle.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 114, %[o_t], %[o_u], x0\n\t"	/* fclass.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 114, %[o_t], %[o_a], x0\n\t"	/* fclass.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 98, %[o_t], %[o_a], x0\n\t"	/* fcvt.w.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			"li	%[o_t], -1\n\t" \
			".insn r 0x53, 1, 98, %[o_t], %[o_a], x1\n\t"	/* fcvt.wu.h */ \
			"add	%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 98, %[o_t], %[o_a], x2\n\t")	/* fcvt.l.h */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_RV64("li	%[o_t], -1\n\t") \
			EXT_RV64(".insn r 0x53, 1, 98, %[o_t], %[o_a], x3\n\t")	/* fcvt.lu.h */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_t]\n\t") \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 106, %[o_p], %[o_t], x0\n\t"	/* fcvt.h.w */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 106, %[o_p], %[o_t], x1\n\t"	/* fcvt.h.wu */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 106, %[o_p], %[o_t], x2\n\t")	/* fcvt.h.l */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			EXT_RV64("li	%[o_p], 0x5a5a5a5a\n\t") \
			EXT_RV64(".insn r 0x53, 0, 106, %[o_p], %[o_t], x3\n\t")	/* fcvt.h.lu */ \
			EXT_RV64("add	%[o_x], %[o_x], %[o_p]\n\t") \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 32, %[o_q], %[o_a], x2\n\t"	/* fcvt.s.h */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 34, %[o_u], %[o_q], x0\n\t"	/* fcvt.h.s */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_) \
			: [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)

#define EXT_BLOCK_ZHINXMIN(a, b, x) \
	do { \
		xu t_, p_, q_, u_, v_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"li	%[o_p], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 32, %[o_p], %[o_a], x2\n\t"	/* fcvt.s.h */ \
			"add	%[o_x], %[o_x], %[o_p]\n\t" \
			"li	%[o_q], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 34, %[o_q], %[o_b], x0\n\t"	/* fcvt.h.s */ \
			"add	%[o_x], %[o_x], %[o_q]\n\t" \
			"li	%[o_u], 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 32, %[o_u], %[o_q], x2\n\t"	/* fcvt.s.h */ \
			"add	%[o_x], %[o_x], %[o_u]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_) \
			: [o_a] "r" (a), [o_b] "r" (b)); \
	} while (0)

/* ===== Zfa (single/double), Zfbfmin =====
 * Inputs: a/b are single-precision bit patterns (7.5 and 2.5; Zfbfmin: 7.5 and the bf16 tie 0x40F18000).
 * Zfbfmin uses .insn (fcvt.bf16.s / fcvt.s.bf16 are not in older assemblers); FS must be on. */
#define EXT_IN_ZFA_S_A	0x40F00000ULL
#define EXT_IN_ZFA_S_B	0x40200000ULL
#define EXT_IN_ZFA_D_A	0x40F00000ULL
#define EXT_IN_ZFA_D_B	0x40200000ULL
#define EXT_IN_ZFBFMIN_A	0x40F00000ULL
#define EXT_IN_ZFBFMIN_B	0x40F18000ULL
#if __riscv_xlen == 64
#define EXT_EXP_ZFA_S	0x2c0f00001ULL
#else
#define EXT_EXP_ZFA_S	0xc0f00001ULL
#endif
#if __riscv_xlen == 64
#define EXT_EXP_ZFA_D	0x240f00008ULL
#else
#define EXT_EXP_ZFA_D	0x81100008ULL
#endif
#if __riscv_xlen == 64
#define EXT_EXP_ZFBFMIN	0x81dec2d3ULL
#else
#define EXT_EXP_ZFBFMIN	0x81dec2d3ULL
#endif

#define EXT_BLOCK_ZFA_S(a, b, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zfa") \
			"mv	%[o_x], zero\n\t" \
			"fmv.w.x	ft0, %[o_a]\n\t" \
			"fmv.w.x	ft1, %[o_b]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft2, %[o_t]\n\t" \
			"fli.s	ft2, 1.0\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft2\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft3, %[o_t]\n\t" \
			"fli.s	ft3, 0.5\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft3\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft4, %[o_t]\n\t" \
			"fli.s	ft4, 2.5\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft4\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft5, %[o_t]\n\t" \
			"fli.s	ft5, 3.0\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"fminm.s	ft6, ft0, ft1\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"fmaxm.s	ft6, ft0, ft1\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"fround.s	ft6, ft0, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"froundnx.s	ft6, ft1, rtz\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"fround.s	ft6, ft1, rup\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft7, %[o_t]\n\t" \
			"fli.s	ft7, nan\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fmv.w.x\tft6, %[o_t]\n\t" \
			"fminm.s	ft6, ft2, ft7\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft6\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fleq.s	%[o_t], ft1, ft0\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fltq.s	%[o_t], ft0, ft1\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fleq.s	%[o_t], ft7, ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b) \
			: "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7"); \
	} while (0)

#define EXT_BLOCK_ZFA_D(a, b, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zfa, +d") \
			"mv	%[o_x], zero\n\t" \
			"fmv.w.x	ft0, %[o_a]\n\t" \
			"fmv.w.x	ft1, %[o_b]\n\t" \
			"fcvt.d.s	ft0, ft0\n\t" \
			"fcvt.d.s	ft1, ft1\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft2, %[o_t]\n\t" \
			"fli.d	ft2, 1.0\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft2, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft3, %[o_t]\n\t" \
			"fli.d	ft3, 0.5\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft3, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft4, %[o_t]\n\t" \
			"fli.d	ft4, 2.5\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft4, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft5, %[o_t]\n\t" \
			"fli.d	ft5, 3.0\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft5, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fminm.d	ft6, ft0, ft1\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fmaxm.d	ft6, ft0, ft1\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fround.d	ft6, ft0, rne\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"froundnx.d	ft6, ft1, rtz\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fround.d	ft6, ft1, rup\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fli.d	ft7, nan\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fminm.d	ft6, ft2, ft7\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft7, %[o_t]\n\t" \
			"fcvt.s.d	ft7, ft6, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fleq.d	%[o_t], ft1, ft0\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fltq.d	%[o_t], ft0, ft1\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fleq.d	%[o_t], ft7, ft7\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fcvtmod.w.d	%[o_t], ft0, rtz\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			"fli.d	ft6, 65536.0\n\t" \
			"fmul.d	ft6, ft6, ft6, rne\n\t" \
			"fmul.d	ft6, ft6, ft0, rne\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fcvtmod.w.d	%[o_t], ft6, rtz\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x5a5a5a5a\n\t" \
			"fcvt.d.w\tft6, %[o_t]\n\t" \
			EXT_RV32("fmvp.d.x	ft6, %[o_a], %[o_b]\n\t") \
			"li\t%[o_t], -1\n\t" \
			EXT_RV32("fmvh.x.d\t%[o_t], ft6\n\t") \
			EXT_RV32("add\t%[o_x], %[o_x], %[o_t]\n\t") \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b) \
			: "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7"); \
	} while (0)

#define EXT_BLOCK_ZFBFMIN(a, b, x) \
	do { \
		xu t_; \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"mv	%[o_x], zero\n\t" \
			"fmv.w.x	ft0, %[o_a]\n\t" \
			"fmv.w.x	ft1, %[o_b]\n\t" \
			".insn r 0x53, 0, 0x22, x2, x0, x8\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft2\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			".insn r 0x53, 0, 0x20, x3, x2, x6\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft3\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			".insn r 0x53, 0, 0x22, x2, x1, x8\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft2\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			".insn r 0x53, 1, 0x22, x2, x1, x8\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft2\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			".insn r 0x53, 0, 0x20, x3, x2, x6\n\t" \
			"li\t%[o_t], -1\n\t" \
			"fmv.x.w\t%[o_t], ft3\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_) \
			: [o_a] "r" (a), [o_b] "r" (b) \
			: "ft0", "ft1", "ft2", "ft3"); \
	} while (0)

/* ===== Zdinx on RV32: 64-bit values live in even/odd register pairs =====
 * a0:a1 = 7.5, a2:a3 = 2.5; results go to the pairs a4:a5, a6:a7, t3:t4, t5:t6 (single results to one register).
 * Every Zdinx instruction (fadd fsub fmul fdiv fsqrt fmadd fmsub fnmsub fnmadd fsgnj fsgnjn fsgnjx fmin fmax
 * feq flt fle fclass fcvt.w.d fcvt.wu.d fcvt.d.w fcvt.d.wu fcvt.s.d fcvt.d.s) runs once; each destination is
 * loaded with a pattern first and each result is folded as low + 2 * high. The inputs are constants; a, b are
 * accepted and ignored. x must equal EXT_EXP_ZDINX. Encoded with .insn (rm = rne, rtz for the conversions to int). */
#if __riscv_xlen == 32
#define EXT_BLOCK_ZDINX(a, b, x) \
	do { \
		xu t_, u_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_INSN_BEGIN \
			"li\ta0, 0x0\n\t" \
			"li\ta1, 0x401e0000\n\t" \
			"li\ta2, 0x0\n\t" \
			"li\ta3, 0x40040000\n\t" \
			"mv\t%[o_x], zero\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x01, a4, a0, a2\n\t"	/* fadd.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x05, a4, a0, a2\n\t"	/* fsub.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x09, a4, a0, a2\n\t"	/* fmul.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x0d, a4, a0, a2\n\t"	/* fdiv.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta6, 0x5a5a5a5a\n\t" \
			"li\ta7, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x09, a6, a2, a2\n\t"	/* fmul.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a6\n\t" \
			"slli\t%[o_u], a7, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x2d, a4, a6, x0\n\t"	/* fsqrt.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\tt3, 0x5a5a5a5a\n\t" \
			"li\tt4, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 0x11, t3, a2, a0\n\t"	/* fsgnjn.d (register pairs) */ \
			"add\t%[o_x], %[o_x], t3\n\t" \
			"slli\t%[o_u], t4, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x15, a4, a0, t3\n\t"	/* fmin.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 0x15, a4, a0, t3\n\t"	/* fmax.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x11, a4, a0, t3\n\t"	/* fsgnj.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 2, 0x11, a4, a0, t3\n\t"	/* fsgnjx.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 1, 0x11, a4, a0, t3\n\t"	/* fsgnjn.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r4 0x43, 0, 1, a4, a0, a2, a2\n\t"	/* fmadd.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r4 0x47, 0, 1, a4, a0, a2, a2\n\t"	/* fmsub.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r4 0x4b, 0, 1, a4, a0, a2, a2\n\t"	/* fnmsub.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r4 0x4f, 0, 1, a4, a0, a2, a2\n\t"	/* fnmadd.d (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 2, 0x51, %[o_t], a0, a0\n\t"	/* feq.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 1, 0x51, %[o_t], a2, a0\n\t"	/* flt.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 0, 0x51, %[o_t], a0, a2\n\t"	/* fle.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 1, 0x71, %[o_t], t3, x0\n\t"	/* fclass.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 1, 0x71, %[o_t], a0, x0\n\t"	/* fclass.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 1, 0x61, %[o_t], a0, x0\n\t"	/* fcvt.w.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 1, 0x61, %[o_t], a0, x1\n\t"	/* fcvt.wu.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			"li\t%[o_t], 0xfffffffb\n\t" \
			".insn r 0x53, 0, 0x69, a4, %[o_t], x0\n\t"	/* fcvt.d.w (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			"li\t%[o_t], 0x80000003\n\t" \
			".insn r 0x53, 0, 0x69, a4, %[o_t], x1\n\t"	/* fcvt.d.wu (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			"li\t%[o_t], -1\n\t" \
			".insn r 0x53, 0, 0x20, %[o_t], a0, x1\n\t"	/* fcvt.s.d (register pairs) */ \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\ta4, 0x5a5a5a5a\n\t" \
			"li\ta5, 0x5a5a5a5a\n\t" \
			".insn r 0x53, 0, 0x21, a4, %[o_t], x0\n\t"	/* fcvt.d.s (register pairs) */ \
			"add\t%[o_x], %[o_x], a4\n\t" \
			"slli\t%[o_u], a5, 1\n\t" \
			"add\t%[o_x], %[o_x], %[o_u]\n\t" \
			EXT_INSN_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_u] "=&r" (u_) : : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "t3", "t4", "t5", "t6"); \
	} while (0)
#define EXT_EXP_ZDINX	0xc9298052ULL
#endif

/* ===== Scalar crypto: Zknh Zksh (step-list chains, C twins) ===== */


#define ZKNH_STEPS_COMMON(R, U, I) \
	U(x, "sha256sig0", a, ref_sha256sig0) \
	U(t, "sha256sig1", b, ref_sha256sig1) \
	R(x, "xor",    x, t, ref_xor) \
	U(t, "sha256sum0", x, ref_sha256sum0) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sha256sum1", x, ref_sha256sum1) \
	R(x, "xor",    x, t, ref_xor)
#define ZKNH_STEPS_RV64(R, U, I) \
	U(t, "sha512sig0", x, ref_sha512sig0) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sha512sig1", b, ref_sha512sig1) \
	R(x, "xor",    x, t, ref_xor) \
	U(t, "sha512sum0", x, ref_sha512sum0) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sha512sum1", a, ref_sha512sum1) \
	R(x, "xor",    x, t, ref_xor)
#define ZKNH_STEPS_RV32(R, U, I) \
	R(t, "sha512sig0l", x, b, ref_sha512sig0l) \
	R(x, "add",    x, t, ref_add) \
	R(t, "sha512sig0h", b, x, ref_sha512sig0h) \
	R(x, "xor",    x, t, ref_xor) \
	R(t, "sha512sig1l", a, x, ref_sha512sig1l) \
	R(x, "add",    x, t, ref_add) \
	R(t, "sha512sig1h", x, a, ref_sha512sig1h) \
	R(x, "xor",    x, t, ref_xor) \
	R(t, "sha512sum0r", x, b, ref_sha512sum0r) \
	R(x, "add",    x, t, ref_add) \
	R(t, "sha512sum0r", b, x, ref_sha512sum0r) \
	R(x, "xor",    x, t, ref_xor) \
	R(t, "sha512sum1r", a, x, ref_sha512sum1r) \
	R(x, "add",    x, t, ref_add) \
	R(t, "sha512sum1r", x, b, ref_sha512sum1r) \
	R(x, "xor",    x, t, ref_xor)
#define ZKNH_STEPS(R, U, I)	ZKNH_STEPS_COMMON(R, U, I) EXT_ON64(ZKNH_STEPS_RV64(R, U, I)) EXT_ON32(ZKNH_STEPS_RV32(R, U, I))

#define ZKSH_STEPS(R, U, I) \
	U(x, "sm3p0",  a, ref_sm3p0) \
	U(t, "sm3p1",  b, ref_sm3p1) \
	R(x, "xor",    x, t, ref_xor) \
	U(t, "sm3p0",  x, ref_sm3p0) \
	R(x, "add",    x, t, ref_add) \
	U(t, "sm3p1",  x, ref_sm3p1) \
	R(x, "xor",    x, t, ref_xor) \
	U(t, "sm3p0",  b, ref_sm3p0) \
	R(x, "add",    x, t, ref_add)

#define EXT_BLOCK_ZKNH(a, b, x)	EXT_CHAIN_BLOCK("+zknh", ZKNH_STEPS, a, b, x)
#define EXT_BLOCK_ZKSH(a, b, x)	EXT_CHAIN_BLOCK("+zksh", ZKSH_STEPS, a, b, x)

/*
 * Zknd, Zkne, Zksed: complete AES-128 / SM4 runs on the published test vectors
 * (FIPS-197 C.1, GB/T 32907 appendix A.1), folded into one word. The a and b
 * arguments are accepted for symmetry with the other blocks and ignored. The result must equal
 * EXT_EXP_<EXT> (a constant computed from the published ciphertext/plaintext).
 * Zknd needs a 176 byte scratch area, it lives on the stack inside the block.
 */
#if __riscv_xlen == 64
#define EXT_BLOCK_ZKNE_RV64(a, b, x) \
	do { \
		xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zkne") \
			"li\t%[o_p], 0x706050403020100\n\t" \
			"li\t%[o_q], 0xf0e0d0c0b0a0908\n\t" \
			"li\t%[o_u], 0x7766554433221100\n\t" \
			"li\t%[o_v], 0xffeeddccbbaa9988\n\t" \
			"xor\t%[o_u], %[o_u], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_v], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 0\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 1\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 2\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 3\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 4\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 5\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 6\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 7\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 8\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64esm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64esm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 9\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"aes64es\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64es\t%[o_t], %[o_v], %[o_u]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"add\t%[o_x], %[o_u], %[o_v]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_) \
			:  \
			: ); \
	} while (0)
#define EXT_EXP_ZKNE	0x8aca2fdb59989241ULL
#else
#define EXT_BLOCK_ZKNE_RV32(a, b, x) \
	do { \
		xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; xu y_; xu z_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zkne") \
			"li\t%[o_u], 0x33221100\n\t" \
			"li\t%[o_z], 0x3020100\n\t" \
			"xor\t%[o_u], %[o_u], %[o_z]\n\t" \
			"li\t%[o_v], 0x77665544\n\t" \
			"li\t%[o_z], 0x7060504\n\t" \
			"xor\t%[o_v], %[o_v], %[o_z]\n\t" \
			"li\t%[o_w], 0xbbaa9988\n\t" \
			"li\t%[o_z], 0xb0a0908\n\t" \
			"xor\t%[o_w], %[o_w], %[o_z]\n\t" \
			"li\t%[o_y], 0xffeeddcc\n\t" \
			"li\t%[o_z], 0xf0e0d0c\n\t" \
			"xor\t%[o_y], %[o_y], %[o_z]\n\t" \
			"li\t%[o_x], 0xfd74aad6\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0xfa72afd2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xf178a6da\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xfe76abd6\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0xbcf92b6\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0xf1bd3d64\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xc59bbe\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xfeb33068\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x4e74ffb6\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0xbfc9c2d2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xbf0c596c\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0x41bf6904\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0xbcf7f747\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0x33e3595\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xbc326cf9\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xfd8d05fd\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0xe8a3aa3c\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0xeb9d9fa9\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0x57aff350\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xaa22f6ad\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x7d0f395e\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0x9692a6f7\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xc13d55a7\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0x6b1fa30a\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x1a70f914\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0x8ce25fe3\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0x4ddf0a44\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0x26c0a94e\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x35874347\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0xb9651ca4\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0xf4ba16e0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xd27abfae\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0xd1329954\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esmi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0x685785f0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esmi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0x9ced9310\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esmi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0x4e972cbe\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esmi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x7f1d1113\n\t" \
			"aes32esi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32esi\t%[o_x], %[o_x], %[o_v], 1\n\t" \
			"aes32esi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32esi\t%[o_x], %[o_x], %[o_y], 3\n\t" \
			"li\t%[o_t], 0x174a94e3\n\t" \
			"aes32esi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32esi\t%[o_t], %[o_t], %[o_w], 1\n\t" \
			"aes32esi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32esi\t%[o_t], %[o_t], %[o_u], 3\n\t" \
			"li\t%[o_p], 0x8ba707f3\n\t" \
			"aes32esi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32esi\t%[o_p], %[o_p], %[o_y], 1\n\t" \
			"aes32esi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32esi\t%[o_p], %[o_p], %[o_v], 3\n\t" \
			"li\t%[o_q], 0xc5302b4d\n\t" \
			"aes32esi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32esi\t%[o_q], %[o_q], %[o_u], 1\n\t" \
			"aes32esi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32esi\t%[o_q], %[o_q], %[o_w], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"add\t%[o_x], %[o_u], %[o_v]\n\t" \
			"add\t%[o_x], %[o_x], %[o_w]\n\t" \
			"add\t%[o_x], %[o_x], %[o_y]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_), [o_y] "=&r" (y_), [o_z] "=&r" (z_) \
			:  \
			: ); \
	} while (0)
#define EXT_EXP_ZKNE	0xe462c21bULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZKNE(a, b, x)	EXT_BLOCK_ZKNE_RV64(a, b, x)
#else
#define EXT_BLOCK_ZKNE(a, b, x)	EXT_BLOCK_ZKNE_RV32(a, b, x)
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZKND_RV64(a, b, x) \
	do { \
		uint64_t rk_[22]; xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zknd") \
			"li\t%[o_p], 0x706050403020100\n\t" \
			"li\t%[o_q], 0xf0e0d0c0b0a0908\n\t" \
			"sd\t%[o_p], 0(%[o_rk])\n\t" \
			"sd\t%[o_q], 8(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 0\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 16(%[o_rk])\n\t" \
			"sd\t%[o_q], 24(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 1\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 32(%[o_rk])\n\t" \
			"sd\t%[o_q], 40(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 2\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 48(%[o_rk])\n\t" \
			"sd\t%[o_q], 56(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 3\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 64(%[o_rk])\n\t" \
			"sd\t%[o_q], 72(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 4\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 80(%[o_rk])\n\t" \
			"sd\t%[o_q], 88(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 5\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 96(%[o_rk])\n\t" \
			"sd\t%[o_q], 104(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 6\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 112(%[o_rk])\n\t" \
			"sd\t%[o_q], 120(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 7\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 128(%[o_rk])\n\t" \
			"sd\t%[o_q], 136(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 8\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 144(%[o_rk])\n\t" \
			"sd\t%[o_q], 152(%[o_rk])\n\t" \
			"aes64ks1i\t%[o_w], %[o_q], 9\n\t" \
			"aes64ks2\t%[o_p], %[o_w], %[o_p]\n\t" \
			"aes64ks2\t%[o_q], %[o_p], %[o_q]\n\t" \
			"sd\t%[o_p], 160(%[o_rk])\n\t" \
			"sd\t%[o_q], 168(%[o_rk])\n\t" \
			"li\t%[o_u], 0x30047b6ad8e0c469\n\t" \
			"li\t%[o_v], 0x5ac5b47080b7cdd8\n\t" \
			"xor\t%[o_u], %[o_u], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_v], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 144(%[o_rk])\n\t" \
			"ld\t%[o_q], 152(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 128(%[o_rk])\n\t" \
			"ld\t%[o_q], 136(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 112(%[o_rk])\n\t" \
			"ld\t%[o_q], 120(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 96(%[o_rk])\n\t" \
			"ld\t%[o_q], 104(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 80(%[o_rk])\n\t" \
			"ld\t%[o_q], 88(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 64(%[o_rk])\n\t" \
			"ld\t%[o_q], 72(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 48(%[o_rk])\n\t" \
			"ld\t%[o_q], 56(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 32(%[o_rk])\n\t" \
			"ld\t%[o_q], 40(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64dsm\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64dsm\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 16(%[o_rk])\n\t" \
			"ld\t%[o_q], 24(%[o_rk])\n\t" \
			"aes64im\t%[o_p], %[o_p]\n\t" \
			"aes64im\t%[o_q], %[o_q]\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"aes64ds\t%[o_x], %[o_u], %[o_v]\n\t" \
			"aes64ds\t%[o_t], %[o_v], %[o_u]\n\t" \
			"ld\t%[o_p], 0(%[o_rk])\n\t" \
			"ld\t%[o_q], 8(%[o_rk])\n\t" \
			"xor\t%[o_u], %[o_x], %[o_p]\n\t" \
			"xor\t%[o_v], %[o_t], %[o_q]\n\t" \
			"add\t%[o_x], %[o_u], %[o_v]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_) \
			: [o_rk] "r" (rk_) \
			: "memory"); \
	} while (0)
#define EXT_EXP_ZKND	0x77553310eeccaa88ULL
#else
#define EXT_BLOCK_ZKND_RV32(a, b, x) \
	do { \
		xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; xu y_; xu z_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zknd") \
			"li\t%[o_u], 0xd8e0c469\n\t" \
			"li\t%[o_z], 0x7f1d1113\n\t" \
			"xor\t%[o_u], %[o_u], %[o_z]\n\t" \
			"li\t%[o_v], 0x30047b6a\n\t" \
			"li\t%[o_z], 0x174a94e3\n\t" \
			"xor\t%[o_v], %[o_v], %[o_z]\n\t" \
			"li\t%[o_w], 0x80b7cdd8\n\t" \
			"li\t%[o_z], 0x8ba707f3\n\t" \
			"xor\t%[o_w], %[o_w], %[o_z]\n\t" \
			"li\t%[o_y], 0x5ac5b470\n\t" \
			"li\t%[o_z], 0xc5302b4d\n\t" \
			"xor\t%[o_y], %[o_y], %[o_z]\n\t" \
			"li\t%[o_x], 0xbe29aa13\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0xf6af8f9c\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x80f570f7\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0x3bff700\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x63a46213\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0x4886258f\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x765aff6b\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0x834a87f7\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x74fc828d\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0x2b22479c\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x3edcdae4\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xf510789c\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x8d09e372\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0x5fdec511\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x15fe9d78\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xcbcca278\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x2710c42e\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0xd2d72663\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x4a205869\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xde323f00\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x4f5a2a8\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0xf5c7e24d\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x98f77e0a\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0x94126769\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x91e3c6c7\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0xf13240e5\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x6d309c47\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xce51963\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x9902dba0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0x60d18622\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0x9c02dca2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0x61d58524\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0xf0df568c\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsmi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0xf9d35d82\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsmi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0xfcd35a80\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsmi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xfdd75986\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsmi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"li\t%[o_x], 0x3020100\n\t" \
			"aes32dsi\t%[o_x], %[o_x], %[o_u], 0\n\t" \
			"aes32dsi\t%[o_x], %[o_x], %[o_y], 1\n\t" \
			"aes32dsi\t%[o_x], %[o_x], %[o_w], 2\n\t" \
			"aes32dsi\t%[o_x], %[o_x], %[o_v], 3\n\t" \
			"li\t%[o_t], 0x7060504\n\t" \
			"aes32dsi\t%[o_t], %[o_t], %[o_v], 0\n\t" \
			"aes32dsi\t%[o_t], %[o_t], %[o_u], 1\n\t" \
			"aes32dsi\t%[o_t], %[o_t], %[o_y], 2\n\t" \
			"aes32dsi\t%[o_t], %[o_t], %[o_w], 3\n\t" \
			"li\t%[o_p], 0xb0a0908\n\t" \
			"aes32dsi\t%[o_p], %[o_p], %[o_w], 0\n\t" \
			"aes32dsi\t%[o_p], %[o_p], %[o_v], 1\n\t" \
			"aes32dsi\t%[o_p], %[o_p], %[o_u], 2\n\t" \
			"aes32dsi\t%[o_p], %[o_p], %[o_y], 3\n\t" \
			"li\t%[o_q], 0xf0e0d0c\n\t" \
			"aes32dsi\t%[o_q], %[o_q], %[o_y], 0\n\t" \
			"aes32dsi\t%[o_q], %[o_q], %[o_w], 1\n\t" \
			"aes32dsi\t%[o_q], %[o_q], %[o_v], 2\n\t" \
			"aes32dsi\t%[o_q], %[o_q], %[o_u], 3\n\t" \
			"mv\t%[o_u], %[o_x]\n\t" \
			"mv\t%[o_v], %[o_t]\n\t" \
			"mv\t%[o_w], %[o_p]\n\t" \
			"mv\t%[o_y], %[o_q]\n\t" \
			"add\t%[o_x], %[o_u], %[o_v]\n\t" \
			"add\t%[o_x], %[o_x], %[o_w]\n\t" \
			"add\t%[o_x], %[o_x], %[o_y]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_), [o_y] "=&r" (y_), [o_z] "=&r" (z_) \
			:  \
			: ); \
	} while (0)
#define EXT_EXP_ZKND	0x6621dd98ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZKND(a, b, x)	EXT_BLOCK_ZKND_RV64(a, b, x)
#else
#define EXT_BLOCK_ZKND(a, b, x)	EXT_BLOCK_ZKND_RV32(a, b, x)
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZKSED_RV64(a, b, x) \
	do { \
		xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; xu y_; xu z_; xu e2_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zksed") \
			"li\t%[o_p], 0xa1ff92a2\n\t" \
			"li\t%[o_x], 0x67452301\n\t" \
			"li\t%[o_q], 0xbffe01df\n\t" \
			"li\t%[o_t], 0xefcdab89\n\t" \
			"li\t%[o_u], 0xf2ba199\n\t" \
			"li\t%[o_w], 0x98badcfe\n\t" \
			"li\t%[o_v], 0xcc1024c4\n\t" \
			"li\t%[o_y], 0x10325476\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x150e0700\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x312a231c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x4d463f38\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x69625b54\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x857e7770\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xa19a938c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xbdb6afa8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0xd9d2cbc4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xf5eee7e0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x110a03fc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x2d261f18\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x49423b34\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x655e5750\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x817a736c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x9d968f88\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0xb9b2aba4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xd5cec7c0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xf1eae3dc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xd06fff8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x29221b14\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x453e3730\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x615a534c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x7d766f68\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x99928b84\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xb5aea7a0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xd1cac3bc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xede6dfd8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x902fbf4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x251e1710\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x413a332c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x5d564f48\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x79726b64\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"add\t%[o_x], %[o_x], %[o_w]\n\t" \
			"add\t%[o_x], %[o_x], %[o_y]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_), [o_y] "=&r" (y_), [o_z] "=&r" (z_), [o_e2] "=&r" (e2_) \
			:  \
			: ); \
	} while (0)
#define EXT_EXP_ZKSED	0x129a14713ULL
#else
#define EXT_BLOCK_ZKSED_RV32(a, b, x) \
	do { \
		xu t_; xu p_; xu q_; xu u_; xu v_; xu w_; xu y_; xu z_; xu e2_; \
		(void)(a); (void)(b); \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zksed") \
			"li\t%[o_p], 0xa1ff92a2\n\t" \
			"li\t%[o_x], 0x67452301\n\t" \
			"li\t%[o_q], 0xbffe01df\n\t" \
			"li\t%[o_t], 0xefcdab89\n\t" \
			"li\t%[o_u], 0xf2ba199\n\t" \
			"li\t%[o_w], 0x98badcfe\n\t" \
			"li\t%[o_v], 0xcc1024c4\n\t" \
			"li\t%[o_y], 0x10325476\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x150e0700\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x312a231c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x4d463f38\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x69625b54\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x857e7770\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xa19a938c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xbdb6afa8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0xd9d2cbc4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xf5eee7e0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x110a03fc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x2d261f18\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x49423b34\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x655e5750\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x817a736c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x9d968f88\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0xb9b2aba4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xd5cec7c0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xf1eae3dc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xd06fff8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x29221b14\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x453e3730\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x615a534c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x7d766f68\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x99928b84\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0xb5aea7a0\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0xd1cac3bc\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0xede6dfd8\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x902fbf4\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_q], %[o_u]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"li\t%[o_e2], 0x251e1710\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 0\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 1\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 2\n\t" \
			"sm4ks\t%[o_p], %[o_p], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_t], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 0\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 1\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 2\n\t" \
			"sm4ed\t%[o_x], %[o_x], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_u], %[o_v]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_p]\n\t" \
			"li\t%[o_e2], 0x413a332c\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 0\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 1\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 2\n\t" \
			"sm4ks\t%[o_q], %[o_q], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_w], %[o_y]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 0\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 1\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 2\n\t" \
			"sm4ed\t%[o_t], %[o_t], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_v], %[o_p]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_q]\n\t" \
			"li\t%[o_e2], 0x5d564f48\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 0\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 1\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 2\n\t" \
			"sm4ks\t%[o_u], %[o_u], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_y], %[o_x]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 0\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 1\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 2\n\t" \
			"sm4ed\t%[o_w], %[o_w], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_p], %[o_q]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_u]\n\t" \
			"li\t%[o_e2], 0x79726b64\n\t" \
			"xor\t%[o_z], %[o_z], %[o_e2]\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 0\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 1\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 2\n\t" \
			"sm4ks\t%[o_v], %[o_v], %[o_z], 3\n\t" \
			"xor\t%[o_z], %[o_x], %[o_t]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_w]\n\t" \
			"xor\t%[o_z], %[o_z], %[o_v]\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 0\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 1\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 2\n\t" \
			"sm4ed\t%[o_y], %[o_y], %[o_z], 3\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"add\t%[o_x], %[o_x], %[o_w]\n\t" \
			"add\t%[o_x], %[o_x], %[o_y]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_), [o_u] "=&r" (u_), [o_v] "=&r" (v_), [o_w] "=&r" (w_), [o_y] "=&r" (y_), [o_z] "=&r" (z_), [o_e2] "=&r" (e2_) \
			:  \
			: ); \
	} while (0)
#define EXT_EXP_ZKSED	0x29a14713ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZKSED(a, b, x)	EXT_BLOCK_ZKSED_RV64(a, b, x)
#else
#define EXT_BLOCK_ZKSED(a, b, x)	EXT_BLOCK_ZKSED_RV32(a, b, x)
#endif

/* ===== Compressed and register-pair blocks: Zca Zcb Zcd Zcf Zcmp Zcmt Zilsd Zclsd =====
 *
 * Signature: EXT_BLOCK_Z<EXT>(mem, x). mem is a pointer to a scratch buffer, x gets
 * the result, which must equal EXT_EXP_Z<EXT> (a constant worked out by hand-checked
 * model of the sequence). Every block starts from constants and uses only registers
 * it declares clobbered, so there are no inputs besides mem.
 *   mem: 16-byte aligned, 32 bytes at least. Zcmt: 64-byte aligned, 320 bytes (jump table).
 * Compressed instructions only accept x8-x15, so the blocks use fixed registers.
 * `.option norvc` only stops the assembler from compressing the regular
 * instructions, explicit c.* mnemonics are still assembled.
 * Run these only when Zca is present: on a core without C the first compressed
 * instruction would trap forever (the handler can't tell its length).
 * The sp-relative forms (c.addi16sp, c.lwsp/c.swsp, c.ldsp/c.sdsp, c.fldsp/c.fsdsp, c.flwsp/c.fswsp)
 * lower sp by 32 with c.addi16sp first and use the 32 bytes below the live stack as scratch (a
 * trap handler's frame goes below sp as well), then raise it again. c.ebreak is the one Zca
 * instruction no block can run: it raises a breakpoint exception and has no result to check.
 * Every instruction gets its operands loaded fresh and its destination loaded with a value that
 * differs from the expected result, so an instruction that is skipped changes the folded value.
 * Zcb: the core block needs nothing else; the instructions that need a second extension
 * (c.zext.h, c.sext.b, c.sext.h: Zbb; c.mul: Zmmul; c.zext.w: Zba on RV64) are in EXT_BLOCK_ZCBX,
 * run only when those extensions are present (expected value EXT_EXP_ZCBX).
 */
#ifdef __clang__
#define EXT_LBRACE	"%{"
#define EXT_RBRACE	"%}"
#else
#define EXT_LBRACE	"{"
#define EXT_RBRACE	"}"
#endif
#define EXT_CMP(op, list, adj)	op "\t" EXT_LBRACE list EXT_RBRACE ", " #adj "\n\t"

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCA_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x7777\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 5\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x7777\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lui\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 7\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta1, 0x5a5a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.mv\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1234\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.add\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sub\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.and\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.or\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.xor\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x8123456789abcdef\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.slli\ta0, 5\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x8123456789abcdef\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.srli\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x8123456789abcdef\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.srai\ta0, 2\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.andi\ta0, 21\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.nop\n\t" ".option pop\n\t" \
			"mv\ta4, sp\n\t" \
			"li\ta1, 0x99\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi4spn\ta1, sp, 8\n\t" ".option pop\n\t" \
			"sub\ta1, a1, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x7fffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addiw\ta0, 1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x10\n\t" \
			"li\ta1, 0x20\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.subw\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x7ffffff0\n\t" \
			"li\ta1, 0x20\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addw\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"sw\tzero, 0(a2)\n\t" \
			"li\ta0, 0x80000001\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sw\ta0, 0(a2)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lw\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 4(a2)\n\t" \
			"li\ta0, 0x12345678\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sw\ta0, 4(a2)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lw\ta1, 4(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sd\tzero, 8(a2)\n\t" \
			"li\ta0, 0x123456789abcdef0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sd\ta0, 8(a2)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.ld\ta1, 8(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x0\n\t" \
			"li\ta1, 0x7\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta1, 0\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.beqz\ta1, 1f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 31\n\t" ".option pop\n\t" \
			"1:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.bnez\ta1, 2f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 1\n\t" ".option pop\n\t" \
			"2:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta1, 0x0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta1, 1\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.bnez\ta1, 3f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 30\n\t" ".option pop\n\t" \
			"3:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.beqz\ta1, 4f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 2\n\t" ".option pop\n\t" \
			"4:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.j\t5f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 29\n\t" ".option pop\n\t" \
			"5:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\tra, 0x1234\n\t" \
			"lla\ta4, 6f\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.jalr\ta4\n\t" ".option pop\n\t" \
			"6:\n\t" \
			"sub\ta1, ra, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lla\ta4, 7f\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.jr\ta4\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 28\n\t" ".option pop\n\t" \
			"7:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"mv\ta4, sp\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 4(sp)\n\t" \
			"li\ta0, 0x80000001\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.swsp\ta0, 4(sp)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lwsp\ta1, 4(sp)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sd\tzero, 8(sp)\n\t" \
			"li\ta0, 0x123456789abcdef0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sdsp\ta0, 8(sp)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.ldsp\ta1, 8(sp)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "ra", "memory")
#define EXT_BLOCK_ZCA(mem, x)	EXT_BLOCK_ZCA_RV64(mem, x)
#define EXT_EXP_ZCA	0x393e93e77f051da0ULL
#else
#define EXT_BLOCK_ZCA_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x7777\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 5\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x7777\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lui\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 7\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta1, 0x5a5a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.mv\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1234\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.add\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sub\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.and\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.or\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xf3c5a96\n\t" \
			"li\ta1, 0xff0c3a\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.xor\ta0, a1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x81234567\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.slli\ta0, 5\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x81234567\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.srli\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0x81234567\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.srai\ta0, 2\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta0, 0xffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.andi\ta0, 21\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.nop\n\t" ".option pop\n\t" \
			"mv\ta4, sp\n\t" \
			"li\ta1, 0x99\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi4spn\ta1, sp, 8\n\t" ".option pop\n\t" \
			"sub\ta1, a1, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 0(a2)\n\t" \
			"li\ta0, 0x80000001\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sw\ta0, 0(a2)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lw\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 4(a2)\n\t" \
			"li\ta0, 0x12345678\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.sw\ta0, 4(a2)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lw\ta1, 4(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x0\n\t" \
			"li\ta1, 0x7\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta1, 0\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.beqz\ta1, 1f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 31\n\t" ".option pop\n\t" \
			"1:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.bnez\ta1, 2f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 1\n\t" ".option pop\n\t" \
			"2:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\ta1, 0x0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta1, 1\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.bnez\ta1, 3f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 30\n\t" ".option pop\n\t" \
			"3:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.beqz\ta1, 4f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 2\n\t" ".option pop\n\t" \
			"4:\n\t" \
			"add\ta3, a3, a0\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.j\t5f\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 29\n\t" ".option pop\n\t" \
			"5:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\tra, 0x1234\n\t" \
			"lla\ta4, 6f\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.jalr\ta4\n\t" ".option pop\n\t" \
			"6:\n\t" \
			"sub\ta1, ra, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lla\ta4, 7f\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.jr\ta4\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 28\n\t" ".option pop\n\t" \
			"7:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"li\tra, 0x1234\n\t" \
			"lla\ta4, 8f\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.jal\t8f\n\t" ".option pop\n\t" \
			"8:\n\t" \
			"sub\ta1, ra, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\ta4, sp\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 4(sp)\n\t" \
			"li\ta0, 0x80000001\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.swsp\ta0, 4(sp)\n\t" ".option pop\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.lwsp\ta1, 4(sp)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "ra", "memory")
#define EXT_BLOCK_ZCA(mem, x)	EXT_BLOCK_ZCA_RV32(mem, x)
#define EXT_EXP_ZCA	0x55471b8dULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCB_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x87654321\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"sw\ta0, 4(a2)\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 1(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 3(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lhu\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffffffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lh\ta1, 2(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x1badf00d\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.sb\ta0, 1(a2)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.sh\ta0, 2(a2)\n\t" ".option pop\n\t" \
			"lw\ta1, 0(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1ff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.zext.b\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1234\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.not\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "memory")
#define EXT_BLOCK_ZCB(mem, x)	EXT_BLOCK_ZCB_RV64(mem, x)
#define EXT_EXP_ZCB	0xfffffffff00cc75cULL
#else
#define EXT_BLOCK_ZCB_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x87654321\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"sw\ta0, 4(a2)\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 1(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lbu\ta1, 3(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lhu\ta1, 0(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.lh\ta1, 2(a2)\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x1badf00d\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.sb\ta0, 1(a2)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.sh\ta0, 2(a2)\n\t" ".option pop\n\t" \
			"lw\ta1, 0(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1ff\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.zext.b\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1234\n\t" \
			".option push\n\t.option arch, +zca, +zcb\n\t" "c.not\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "memory")
#define EXT_BLOCK_ZCB(mem, x)	EXT_BLOCK_ZCB_RV32(mem, x)
#define EXT_EXP_ZCB	0xf00cc75cULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCBX_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta1, 0x1ff80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul, +zba\n\t" "c.zext.h\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul, +zba\n\t" "c.sext.b\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1ff80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul, +zba\n\t" "c.sext.h\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x1234567\n\t" \
			"li\ta1, 0x89abcd\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul, +zba\n\t" "c.mul\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x123456789\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul, +zba\n\t" "c.zext.w\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "memory")
#define EXT_BLOCK_ZCBX(mem, x)	EXT_BLOCK_ZCBX_RV64(mem, x)
#define EXT_EXP_ZCBX	0x9ca3bfffc684ULL
#else
#define EXT_BLOCK_ZCBX_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta1, 0x1ff80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul\n\t" "c.zext.h\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul\n\t" "c.sext.b\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta1, 0x1ff80\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul\n\t" "c.sext.h\ta1\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x1234567\n\t" \
			"li\ta1, 0x89abcd\n\t" \
			".option push\n\t.option arch, +zca, +zcb, +zbb, +zmmul\n\t" "c.mul\ta1, a0\n\t" ".option pop\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "memory")
#define EXT_BLOCK_ZCBX(mem, x)	EXT_BLOCK_ZCBX_RV32(mem, x)
#define EXT_EXP_ZCBX	0x9cba5efbULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCD_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x40f0000012345678\n\t" \
			"sd\ta0, 0(a2)\n\t" \
			"sd\tzero, 8(a2)\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fld\tfa0, 0(a2)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fsd\tfa0, 8(a2)\n\t" ".option pop\n\t" \
			"ld\ta1, 8(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"fcvt.d.w\tfa1, zero\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sd\tzero, 8(sp)\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fsdsp\tfa0, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fldsp\tfa1, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"sd\tzero, 8(a2)\n\t" \
			"fsd\tfa1, 8(a2)\n\t" \
			"ld\ta1, 8(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "fa0", "fa1", "memory")
#define EXT_BLOCK_ZCD(mem, x)	EXT_BLOCK_ZCD_RV64(mem, x)
#define EXT_EXP_ZCD	0x81e000002468acf0ULL
#else
#define EXT_BLOCK_ZCD_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x12345678\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"li\ta0, 0x40f00000\n\t" \
			"sw\ta0, 4(a2)\n\t" \
			"sw\tzero, 8(a2)\n\t" \
			"sw\tzero, 12(a2)\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fld\tfa0, 0(a2)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fsd\tfa0, 8(a2)\n\t" ".option pop\n\t" \
			"lw\ta1, 8(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lw\ta1, 12(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"fcvt.d.w\tfa1, zero\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sw\tzero, 8(sp)\n\t" \
			"sw\tzero, 12(sp)\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fsdsp\tfa0, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.fldsp\tfa1, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcd\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"sw\tzero, 8(a2)\n\t" \
			"sw\tzero, 12(a2)\n\t" \
			"fsd\tfa1, 8(a2)\n\t" \
			"lw\ta1, 8(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lw\ta1, 12(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "fa0", "fa1", "memory")
#define EXT_BLOCK_ZCD(mem, x)	EXT_BLOCK_ZCD_RV32(mem, x)
#define EXT_EXP_ZCD	0xa648acf0ULL
#endif

#if __riscv_xlen == 32
#define EXT_BLOCK_ZCF_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x40f12345\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"sw\tzero, 4(a2)\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.flw\tfa0, 0(a2)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.fsw\tfa0, 4(a2)\n\t" ".option pop\n\t" \
			"lw\ta1, 4(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"fcvt.s.w\tfa1, zero\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sw\tzero, 8(sp)\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.fswsp\tfa0, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.flwsp\tfa1, 8(sp)\n\t" ".option pop\n\t" \
			".option push\n\t.option arch, +zcf\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"sw\tzero, 4(a2)\n\t" \
			"fsw\tfa1, 4(a2)\n\t" \
			"lw\ta1, 4(a2)\n\t" \
			"add\ta3, a3, a1\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "fa0", "fa1", "memory")
#define EXT_BLOCK_ZCF(mem, x)	EXT_BLOCK_ZCF_RV32(mem, x)
#define EXT_EXP_ZCF	0x81e2468aULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCMP_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"mv\ta4, sp\n\t" \
			"mv\ta5, s0\n\t" \
			"li\ts0, 0x11\n\t" \
			"li\ts1, 0x22\n\t" \
			"li\tra, 0x33\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0-s1", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			"li\ts1, 0x0\n\t" \
			"li\tra, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.pop", "ra, s0-s1", 32) ".option pop\n\t" \
			"add\ta3, a3, s0\n\t" \
			"add\ta3, a3, s1\n\t" \
			"add\ta3, a3, ra\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x77\n\t" \
			"lla\tra, 1f\n\t" \
			"li\ts0, 0x44\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.popretz", "ra, s0", 32) ".option pop\n\t" \
			"1:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"add\ta3, a3, s0\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lla\tra, 2f\n\t" \
			"li\ts0, 0x55\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.popret", "ra, s0", 32) ".option pop\n\t" \
			"2:\n\t" \
			"add\ta3, a3, s0\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ts2, 0x66\n\t" \
			"li\ts3, 0x77\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" "cm.mva01s\ts2, s3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x88\n\t" \
			"li\ta1, 0x99\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" "cm.mvsa01\ts2, s3\n\t" ".option pop\n\t" \
			"add\ta3, a3, s2\n\t" \
			"add\ta3, a3, s3\n\t" \
			"mv\ts0, a5\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "ra", "s0", "s1", "s2", "s3", "memory")
#define EXT_BLOCK_ZCMP(mem, x)	EXT_BLOCK_ZCMP_RV64(mem, x)
#define EXT_EXP_ZCMP	0x2fdULL
#else
#define EXT_BLOCK_ZCMP_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"mv\ta4, sp\n\t" \
			"mv\ta5, s0\n\t" \
			"li\ts0, 0x11\n\t" \
			"li\ts1, 0x22\n\t" \
			"li\tra, 0x33\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0-s1", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			"li\ts1, 0x0\n\t" \
			"li\tra, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.pop", "ra, s0-s1", 32) ".option pop\n\t" \
			"add\ta3, a3, s0\n\t" \
			"add\ta3, a3, s1\n\t" \
			"add\ta3, a3, ra\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x77\n\t" \
			"lla\tra, 1f\n\t" \
			"li\ts0, 0x44\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.popretz", "ra, s0", 32) ".option pop\n\t" \
			"1:\n\t" \
			"add\ta3, a3, a0\n\t" \
			"add\ta3, a3, s0\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"lla\tra, 2f\n\t" \
			"li\ts0, 0x55\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.push", "ra, s0", -32) ".option pop\n\t" \
			"li\ts0, 0x0\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" EXT_CMP("cm.popret", "ra, s0", 32) ".option pop\n\t" \
			"2:\n\t" \
			"add\ta3, a3, s0\n\t" \
			"sub\ta1, sp, a4\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ts2, 0x66\n\t" \
			"li\ts3, 0x77\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" "cm.mva01s\ts2, s3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"add\ta3, a3, a1\n\t" \
			"li\ta0, 0x88\n\t" \
			"li\ta1, 0x99\n\t" \
			".option push\n\t.option arch, +zca, +zcmp\n\t" "cm.mvsa01\ts2, s3\n\t" ".option pop\n\t" \
			"add\ta3, a3, s2\n\t" \
			"add\ta3, a3, s3\n\t" \
			"mv\ts0, a5\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "ra", "s0", "s1", "s2", "s3", "memory")
#define EXT_BLOCK_ZCMP(mem, x)	EXT_BLOCK_ZCMP_RV32(mem, x)
#define EXT_EXP_ZCMP	0x2fdULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZCMT_RV64(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"lla\ta5, 1f\n\t" \
			"sd\ta5, 0(a2)\n\t" \
			"lla\ta5, 2f\n\t" \
			"sd\ta5, 256(a2)\n\t" \
			"csrrw\ta4, 0x017, a2\n\t" \
			"li\ta0, 0x0\n\t" \
			".2byte\t0xa002\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 27\n\t" ".option pop\n\t" \
			"1:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"lla\ta5, 3f\n\t" \
			".2byte\t0xa082\n\t" \
			"3:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 26\n\t" ".option pop\n\t" \
			"2:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 4\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"sub\ta1, ra, a5\n\t" \
			"add\ta3, a3, a1\n\t" \
			"csrw\t0x017, a4\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "ra", "memory")
#define EXT_BLOCK_ZCMT(mem, x)	EXT_BLOCK_ZCMT_RV64(mem, x)
#define EXT_EXP_ZCMT	0xaULL
#else
#define EXT_BLOCK_ZCMT_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"lla\ta5, 1f\n\t" \
			"sw\ta5, 0(a2)\n\t" \
			"lla\ta5, 2f\n\t" \
			"sw\ta5, 128(a2)\n\t" \
			"csrrw\ta4, 0x017, a2\n\t" \
			"li\ta0, 0x0\n\t" \
			".2byte\t0xa002\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 27\n\t" ".option pop\n\t" \
			"1:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 3\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"lla\ta5, 3f\n\t" \
			".2byte\t0xa082\n\t" \
			"3:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.li\ta0, 26\n\t" ".option pop\n\t" \
			"2:\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi\ta0, 4\n\t" ".option pop\n\t" \
			"add\ta3, a3, a0\n\t" \
			"sub\ta1, ra, a5\n\t" \
			"add\ta3, a3, a1\n\t" \
			"csrw\t0x017, a4\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "ra", "memory")
#define EXT_BLOCK_ZCMT(mem, x)	EXT_BLOCK_ZCMT_RV32(mem, x)
#define EXT_EXP_ZCMT	0xaULL
#endif

#if __riscv_xlen == 32
#define EXT_BLOCK_ZILSD_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0x89abcdef\n\t" \
			"li\ta1, 0x1234567\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"sw\ta1, 4(a2)\n\t" \
			".insn i 0x03, 3, a4, 0(a2)\t/* ld a4, 0(a2) */\n\t" \
			"add\ta3, a3, a4\n\t" \
			"add\ta3, a3, a5\n\t" \
			".insn s 0x23, 3, a4, 8(a2)\t/* sd a4, 8(a2) */\n\t" \
			"lw\ta0, 8(a2)\n\t" \
			"add\ta3, a3, a0\n\t" \
			"lw\ta0, 12(a2)\n\t" \
			"add\ta3, a3, a0\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "memory")
#define EXT_BLOCK_ZILSD(mem, x)	EXT_BLOCK_ZILSD_RV32(mem, x)
#define EXT_EXP_ZILSD	0x159e26acULL
#endif

#if __riscv_xlen == 32
#define EXT_BLOCK_ZCLSD_RV32(mem, x) \
	__asm__ volatile ( \
		EXT_INSN_BEGIN \
			"mv\ta2, %[o_m]\n\t" \
			"mv\ta3, zero\n\t" \
			"li\ta0, 0xfedcba98\n\t" \
			"li\ta1, 0x76543210\n\t" \
			"sw\ta0, 0(a2)\n\t" \
			"sw\ta1, 4(a2)\n\t" \
			"li\ta0, 0xffffffff\n\t" \
			"li\ta1, 0xffffffff\n\t" \
			".2byte\t0x6208\t# c.ld a0, 0(a2)\n\t" \
			"add\ta3, a3, a0\n\t" \
			"add\ta3, a3, a1\n\t" \
			"sw\tzero, 8(a2)\n\t" \
			"sw\tzero, 12(a2)\n\t" \
			".2byte\t0xe608\t# c.sd a0, 8(a2)\n\t" \
			"lw\ta4, 8(a2)\n\t" \
			"add\ta3, a3, a4\n\t" \
			"lw\ta4, 12(a2)\n\t" \
			"add\ta3, a3, a4\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, -32\n\t" ".option pop\n\t" \
			"sw\tzero, 8(sp)\n\t" \
			"sw\tzero, 12(sp)\n\t" \
			".2byte\t0xe42a\t# c.sdsp a0, 8(sp)\n\t" \
			"li\ta4, 0xffffffff\n\t" \
			"li\ta5, 0xffffffff\n\t" \
			".2byte\t0x6722\t# c.ldsp a4, 8(sp)\n\t" \
			"add\ta3, a3, a4\n\t" \
			"add\ta3, a3, a5\n\t" \
			".option push\n\t.option arch, +zca\n\t" "c.addi16sp\tsp, 32\n\t" ".option pop\n\t" \
			"mv\t%[o_x], a3\n\t" \
		EXT_INSN_END \
		: [o_x] "=&r" (x) : [o_m] "r" (mem) : "a0", "a1", "a2", "a3", "a4", "a5", "memory")
#define EXT_BLOCK_ZCLSD(mem, x)	EXT_BLOCK_ZCLSD_RV32(mem, x)
#define EXT_EXP_ZCLSD	0x5f92c5f8ULL
#endif

/* ===== Vector subsets: Zve32x Zve32f Zve64x Zve64d Zvbb Zvbc Zvfh Zvfhmin Zvfbfmin Zvfbfwma =====
 *
 * Signature: EXT_BLOCK_Z<EXT>(buf, x). buf is a pointer to a 16-byte aligned scratch buffer of
 * at least 256 bytes, x gets the result, which must equal EXT_EXP_Z<EXT>.
 *
 * Each block loads its operand vectors from constants, runs a representative set of the
 * extension's instructions, stores every result and adds the stored words into x. The expected
 * value comes from an independent Python model of the same sequence (division by zero and
 * overflow cases included). It is not every instruction of the extension: the integer
 * blocks cover arithmetic, shifts, multiply/divide, min/max, mask compare + merge,
 * gather, slides and reduction; the FP blocks cover arithmetic, sqrt, fused multiply-add,
 * min/max, sign injection, conversions and compare + merge.
 *
 * vl is 4 (2 for 64-bit elements) with LMUL=8 (4 where a widening op needs the double
 * group), which is valid even at the smallest VLEN a Zve* core may have (32 bits).
 * mstatus.VS must be on, FS too for the FP blocks. v0 and v8..v31 are used; the
 * vector registers aren't declared clobbered since the SDK is built without V.
 * The blocks that narrow to a FP format save and restore frm and run with rne.
 * Zvfbfmin and Zvfbfwma are encoded with .insn (no assembler support needed).
 */
#if __riscv_xlen == 64
#define EXT_BLOCK_ZVE32X_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32x") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x12345678\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xfffffff0\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x4\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vand.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vxor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsll.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsrl.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsra.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulhu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vminu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmaxu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdivu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vremu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrem.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vadd.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrsub.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vadd.vi\tv24, v8, -3\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.i\tv0, -1\n\t" \
			"vmseq.vv\tv0, v8, v16\n\t" \
			"vmerge.vim\tv24, v16, 5, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"vrsub.vi\tv24, v24, 3\n\t" \
			"vrgather.vv\tv0, v8, v24\n\t" \
			"vadd.vv\tv24, v0, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1down.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1up.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vredsum.vs\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE32X(buf, x)	EXT_BLOCK_ZVE32X_RV64(buf, x)
#define EXT_EXP_ZVE32X	0x29fed22d6fd9489cULL
#else
#define EXT_BLOCK_ZVE32X_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32x") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x12345678\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xfffffff0\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x4\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vand.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vxor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsll.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsrl.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsra.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulhu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vminu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmaxu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdivu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vremu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrem.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vadd.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrsub.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vadd.vi\tv24, v8, -3\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.i\tv0, -1\n\t" \
			"vmseq.vv\tv0, v8, v16\n\t" \
			"vmerge.vim\tv24, v16, 5, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"vrsub.vi\tv24, v24, 3\n\t" \
			"vrgather.vv\tv0, v8, v24\n\t" \
			"vadd.vv\tv24, v0, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1down.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1up.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vredsum.vs\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE32X(buf, x)	EXT_BLOCK_ZVE32X_RV32(buf, x)
#define EXT_EXP_ZVE32X	0x6fd9489cULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVE64X_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64x") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vand.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vxor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsll.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsrl.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsra.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulhu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vminu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmaxu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdivu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vremu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrem.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vadd.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrsub.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vadd.vi\tv24, v8, -3\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.i\tv0, -1\n\t" \
			"vmseq.vv\tv0, v8, v16\n\t" \
			"vmerge.vim\tv24, v16, 5, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"vrsub.vi\tv24, v24, 1\n\t" \
			"vrgather.vv\tv0, v8, v24\n\t" \
			"vadd.vv\tv24, v0, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1down.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1up.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vredsum.vs\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE64X(buf, x)	EXT_BLOCK_ZVE64X_RV64(buf, x)
#define EXT_EXP_ZVE64X	0xae8aa592505213baULL
#else
#define EXT_BLOCK_ZVE64X_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64x") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0xffffffff\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x7\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vand.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vxor.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsll.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsrl.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsra.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmulhu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vminu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmaxu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdivu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vremu.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrem.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vadd.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrsub.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vadd.vi\tv24, v8, -3\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.i\tv0, -1\n\t" \
			"vmseq.vv\tv0, v8, v16\n\t" \
			"vmerge.vim\tv24, v16, 5, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vid.v\tv24\n\t" \
			"vrsub.vi\tv24, v24, 1\n\t" \
			"vrgather.vv\tv0, v8, v24\n\t" \
			"vadd.vv\tv24, v0, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1down.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vslide1up.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vredsum.vs\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE64X(buf, x)	EXT_BLOCK_ZVE64X_RV32(buf, x)
#define EXT_EXP_ZVE64X	0x505213baULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVE32F_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x40f00000\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0200000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x40c80000\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x42c80000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40200000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f000000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x40800000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE32F(buf, x)	EXT_BLOCK_ZVE32F_RV64(buf, x)
#define EXT_EXP_ZVE32F	0xa2701612ac04f2d7ULL
#else
#define EXT_BLOCK_ZVE32F_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x40f00000\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0200000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x40c80000\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x42c80000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40200000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f000000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x40800000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE32F(buf, x)	EXT_BLOCK_ZVE32F_RV32(buf, x)
#define EXT_EXP_ZVE32F	0xac04f2d7ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVE64D_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64d") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x401e0000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0040000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40040000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE64D(buf, x)	EXT_BLOCK_ZVE64D_RV64(buf, x)
#define EXT_EXP_ZVE64D	0x62929dc6f6f03d3aULL
#else
#define EXT_BLOCK_ZVE64D_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64d") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x401e0000\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0040000\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40040000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVE64D(buf, x)	EXT_BLOCK_ZVE64D_RV32(buf, x)
#define EXT_EXP_ZVE64D	0xf6f03d3aULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVFH_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zvfh") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e16, m8, ta, ma\n\t" \
			"li\t%[o_t], 0xc1004780\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x56404640\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40004100\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x44003800\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle16.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFH(buf, x)	EXT_BLOCK_ZVFH_RV64(buf, x)
#define EXT_EXP_ZVFH	0xda1ade2d5c913918ULL
#else
#define EXT_BLOCK_ZVFH_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zvfh") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e16, m8, ta, ma\n\t" \
			"li\t%[o_t], 0xc1004780\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x56404640\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40004100\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x44003800\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle16.v\tv16, (%[o_p])\n\t" \
			"vfadd.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsub.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfdiv.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmin.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfmax.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsgnjn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfsqrt.v\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmv.v.v\tv24, v8\n\t" \
			"vfmacc.vv\tv24, v16, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.rtz.x.f.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfcvt.f.x.v\tv16, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vmflt.vv\tv0, v8, v16\n\t" \
			"vmerge.vvm\tv24, v8, v16, v0\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFH(buf, x)	EXT_BLOCK_ZVFH_RV32(buf, x)
#define EXT_EXP_ZVFH	0x5c913918ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVBB_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32x, +zvbb") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x12345670\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000002\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0xf0ff80\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x10\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x3\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x1f\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x28\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x5\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vandn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vbrev.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vbrev8.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrev8.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclz.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vctz.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vcpop.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrol.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vi\tv24, v8, 5\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 7\n\t" \
			"vrol.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vandn.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0x80011234\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xffff00ff\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0xf0003\n\t" \
			"sw\t%[o_t], 96(%[o_b])\n\t" \
			"li\t%[o_t], 0x10014\n\t" \
			"sw\t%[o_t], 100(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 96\n\t" \
			"vle16.v\tv12, (%[o_p])\n\t" \
			"vwsll.vv\tv16, v8, v12\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vwsll.vi\tv16, v8, 9\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vwsll.vx\tv16, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVBB(buf, x)	EXT_BLOCK_ZVBB_RV64(buf, x)
#define EXT_EXP_ZVBB	0x91ae1e38f917939ULL
#else
#define EXT_BLOCK_ZVBB_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32x, +zvbb") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e32, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x12345670\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000002\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0xf0ff80\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0x10\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle32.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x3\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x1f\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x28\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x5\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			"vandn.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vbrev.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vbrev8.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrev8.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclz.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vctz.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vcpop.v\tv24, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vrol.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vi\tv24, v8, 5\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 7\n\t" \
			"vrol.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vror.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vandn.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0x80011234\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0xffff00ff\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0xf0003\n\t" \
			"sw\t%[o_t], 96(%[o_b])\n\t" \
			"li\t%[o_t], 0x10014\n\t" \
			"sw\t%[o_t], 100(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 96\n\t" \
			"vle16.v\tv12, (%[o_p])\n\t" \
			"vwsll.vv\tv16, v8, v12\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vwsll.vi\tv16, v8, 9\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vwsll.vx\tv16, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVBB(buf, x)	EXT_BLOCK_ZVBB_RV32(buf, x)
#define EXT_EXP_ZVBB	0x8f917939ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVBC_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64x, +zvbc") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x89abcdef\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x1234567\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x76543210\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0xfedcba98\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x1\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0xdeadbeef\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vclmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vclmul.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclmulh.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVBC(buf, x)	EXT_BLOCK_ZVBC_RV64(buf, x)
#define EXT_EXP_ZVBC	0x62fc02a3b0ba0e52ULL
#else
#define EXT_BLOCK_ZVBC_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve64x, +zvbc") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 2, e64, m8, ta, ma\n\t" \
			"li\t%[o_t], 0x89abcdef\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x1234567\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"li\t%[o_t], 0x76543210\n\t" \
			"sw\t%[o_t], 8(%[o_b])\n\t" \
			"li\t%[o_t], 0xfedcba98\n\t" \
			"sw\t%[o_t], 12(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle64.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x1\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x80000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0xdeadbeef\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x0\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle64.v\tv16, (%[o_p])\n\t" \
			"vclmul.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclmulh.vv\tv24, v8, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0x5a5a\n\t" \
			"vclmul.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vclmulh.vx\tv24, v8, %[o_q]\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse64.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVBC(buf, x)	EXT_BLOCK_ZVBC_RV32(buf, x)
#define EXT_EXP_ZVBC	0xb0ba0e52ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVFHMIN_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zvfhmin") \
			"mv\t%[o_x], zero\n\t" \
			"fsrmi\t%[o_q], 0\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc1004780\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x64003100\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"vfwcvt.f.f.v\tv16, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfncvt.f.f.w\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"fsrm\tzero, %[o_q]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFHMIN(buf, x)	EXT_BLOCK_ZVFHMIN_RV64(buf, x)
#define EXT_EXP_ZVFHMIN	0x93194ddab96880ULL
#else
#define EXT_BLOCK_ZVFHMIN_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zvfhmin") \
			"mv\t%[o_x], zero\n\t" \
			"fsrmi\t%[o_q], 0\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc1004780\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x64003100\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"vfwcvt.f.f.v\tv16, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"vfncvt.f.f.w\tv24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"fsrm\tzero, %[o_q]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFHMIN(buf, x)	EXT_BLOCK_ZVFHMIN_RV32(buf, x)
#define EXT_EXP_ZVFHMIN	0xdab96880ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVFBFMIN_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"fsrmi\t%[o_q], 0\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc02040f0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x44803e20\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"vsetivli\tzero, 8, e16, m4, ta, ma\n\t" \
			"vmv.v.i\tv16, -1\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			".insn r 0x57, 1, 0x25, x16, x13, x8\t# vfwcvtbf16.f.f.v v16, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x40f18000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40f18001\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f800000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0490fdb\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			".insn r 0x57, 1, 0x25, x24, x29, x16\t# vfncvtbf16.f.f.w v24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"fsrm\tzero, %[o_q]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFBFMIN(buf, x)	EXT_BLOCK_ZVFBFMIN_RV64(buf, x)
#define EXT_EXP_ZVFBFMIN	0x93195db5339eb2ULL
#else
#define EXT_BLOCK_ZVFBFMIN_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"fsrmi\t%[o_q], 0\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc02040f0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x44803e20\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"vsetivli\tzero, 8, e16, m4, ta, ma\n\t" \
			"vmv.v.i\tv16, -1\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			".insn r 0x57, 1, 0x25, x16, x13, x8\t# vfwcvtbf16.f.f.v v16, v8\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_t], 0x40f18000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40f18001\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f800000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0490fdb\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			".insn r 0x57, 1, 0x25, x24, x29, x16\t# vfncvtbf16.f.f.w v24, v16\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse16.v\tv24, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"fsrm\tzero, %[o_q]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFBFMIN(buf, x)	EXT_BLOCK_ZVFBFMIN_RV32(buf, x)
#define EXT_EXP_ZVFBFMIN	0xb5339eb2ULL
#endif

#if __riscv_xlen == 64
#define EXT_BLOCK_ZVFBFWMA_RV64(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc02040f0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x40403e20\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40804000\n\t" \
			"sw\t%[o_t], 64(%[o_b])\n\t" \
			"li\t%[o_t], 0xbfc04100\n\t" \
			"sw\t%[o_t], 68(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 64\n\t" \
			"vle16.v\tv24, (%[o_p])\n\t" \
			"li\t%[o_t], 0x3f800000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0400000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f000000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			".insn r 0x57, 1, 0x77, x16, x8, x24\t# vfwmaccbf16.vv v16, v8, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0xffff4000\n\t" \
			"fmv.w.x\tft0, %[o_q]\n\t" \
			".insn r 0x57, 5, 0x77, x16, x0, x24\t# vfwmaccbf16.vf v16, ft0, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "ft0", "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFBFWMA(buf, x)	EXT_BLOCK_ZVFBFWMA_RV64(buf, x)
#define EXT_EXP_ZVFBFWMA	0x766755274b440000ULL
#else
#define EXT_BLOCK_ZVFBFWMA_RV32(buf, x) \
	do { \
		xu t_, p_, q_; \
		__asm__ volatile ( \
			EXT_ASM_BEGIN("+zve32f") \
			"mv\t%[o_x], zero\n\t" \
			"vsetivli\tzero, 4, e16, m4, ta, ma\n\t" \
			"li\t%[o_t], 0xc02040f0\n\t" \
			"sw\t%[o_t], 0(%[o_b])\n\t" \
			"li\t%[o_t], 0x40403e20\n\t" \
			"sw\t%[o_t], 4(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 0\n\t" \
			"vle16.v\tv8, (%[o_p])\n\t" \
			"li\t%[o_t], 0x40804000\n\t" \
			"sw\t%[o_t], 64(%[o_b])\n\t" \
			"li\t%[o_t], 0xbfc04100\n\t" \
			"sw\t%[o_t], 68(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 64\n\t" \
			"vle16.v\tv24, (%[o_p])\n\t" \
			"li\t%[o_t], 0x3f800000\n\t" \
			"sw\t%[o_t], 32(%[o_b])\n\t" \
			"li\t%[o_t], 0x40000000\n\t" \
			"sw\t%[o_t], 36(%[o_b])\n\t" \
			"li\t%[o_t], 0xc0400000\n\t" \
			"sw\t%[o_t], 40(%[o_b])\n\t" \
			"li\t%[o_t], 0x3f000000\n\t" \
			"sw\t%[o_t], 44(%[o_b])\n\t" \
			"addi\t%[o_p], %[o_b], 32\n\t" \
			"vle32.v\tv16, (%[o_p])\n\t" \
			".insn r 0x57, 1, 0x77, x16, x8, x24\t# vfwmaccbf16.vv v16, v8, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"li\t%[o_q], 0xffff4000\n\t" \
			"fmv.w.x\tft0, %[o_q]\n\t" \
			".insn r 0x57, 5, 0x77, x16, x0, x24\t# vfwmaccbf16.vf v16, ft0, v24\n\t" \
			"addi\t%[o_p], %[o_b], 128\n\t" \
			"vse32.v\tv16, (%[o_p])\n\t" \
			"lw\t%[o_t], 0(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 4(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 8(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			"lw\t%[o_t], 12(%[o_p])\n\t" \
			"add\t%[o_t], %[o_t], %[o_x]\n\t" \
			"slli\t%[o_x], %[o_x], 5\n\t" \
			"add\t%[o_x], %[o_x], %[o_t]\n\t" \
			EXT_ASM_END \
			: [o_x] "=&r" (x), [o_t] "=&r" (t_), [o_p] "=&r" (p_), [o_q] "=&r" (q_) \
			: [o_b] "r" (buf) \
			: "ft0", "memory"); \
	} while (0)
#define EXT_BLOCK_ZVFBFWMA(buf, x)	EXT_BLOCK_ZVFBFWMA_RV32(buf, x)
#define EXT_EXP_ZVFBFWMA	0x4b440000ULL
#endif

#endif /* _RISCV_Z_EXT_BLOCKS_H */