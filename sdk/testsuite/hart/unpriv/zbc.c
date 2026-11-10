/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Zbc (carry-less multiplication) -- multiplication over GF(2), where partial products
 * are xored rather than added. The three instructions are three windows onto the same
 * 2*XLEN product: clmul the low half, clmulh the high half, clmulr bits
 * 2*XLEN-2:XLEN-1, one bit below clmulh. Each gets an EXT_INSN unit (the op vs a
 * reference transcribed from the spec's Operation block).
 *
 * The benchmarks are CRC-32 under the standard polynomial, in both bit orders. A CRC is
 * the remainder of polynomial division, which is what these multiplies replace. Msb-first takes three 
 * multiplies (fold, Barrett quotient, multiply back); reflected takes two, because reversed order fuses
 * the quotient and the multiply-back into a single clmulr. Four loop instructions
 * against eight on the same polynomial is the clearest statement of why clmulr exists.
 *
 * The one zbc() function drives them off `flags`: probe presence, fuzz differentially,
 * or run the benchmarks.
 */

#include "ext_probe.h"
#include "ext_bench.h"
#include <test_framework.h>		/* For PROBE_* + REGISTER_HART_UNPRIV */
#include <platform/riscv/hart.h>	/* For hart_state + HS_FLAG_PROBING */
#include <platform/interfaces/uart.h>	/* For uart_getc() (non-blocking fuzz poll) */
#include <platform/utils/utils.h>	/* For INF() */
#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(a)	(sizeof(a) / sizeof((a)[0]))

#define ZBC	".option arch, +zbc\n"

/* The standard CRC-32 polynomial without its x^32 term, and the file's only literal.
 * The reciprocal, the reflected polynomial and the reflected reciprocal are derived
 * from it below rather than written out. */
#define CRC32_POLY	0x04C11DB7ULL

/* Reverse the low w bits. Zbb has rev8/brev8, but a Zbc unit should not need another
 * extension to set itself up. */
static uint64_t
revn(uint64_t x, unsigned w)
{
	uint64_t r = 0;

	for (unsigned i = 0; i < w; i++, x >>= 1)
		r = (r << 1) | (x & 1);
	return r;
}

/* floor(x^64 / (x^32 + poly)), the Barrett reciprocal (33 bits). Long division over
 * GF(2): the dividend x^64 is a single one followed by 64 zeros, so shift it into a
 * remainder and subtract the divisor whenever the x^32 term appears, collecting one
 * quotient bit per step. */
static uint64_t
barrett_mu(uint64_t poly)
{
	uint64_t div = ((uint64_t)1 << 32) | poly;
	uint64_t rem = 0, q = 0;

	for (int i = 64; i >= 0; i--) {
		rem = (rem << 1) | (i == 64);
		q <<= 1;
		if (rem >> 32) {
			q |= 1;
			rem ^= div;
		}
	}
	return q;
}

/*
 * Base-ISA references. Clmulh's loop starts at i = 1, which keeps
 * (xlen - i) off a shift by xlen -- RV64 masks 64 down to 0, so an i = 0 term would
 * return rs1 instead of zero. clmulr shifts by (xlen - i - 1) and is in range from
 * i = 0, the same one-bit offset that makes it the reversed form.
 */
static uint64_t
clmul_ref(uint64_t a, uint64_t b)
{
	uint64_t o = 0;

	for (unsigned i = 0; i <= 63; i++)
		if ((b >> i) & 1)
			o ^= a << i;
	return o;
}

static uint64_t
clmulh_ref(uint64_t a, uint64_t b)
{
	uint64_t o = 0;

	for (unsigned i = 1; i <= 63; i++)
		if ((b >> i) & 1)
			o ^= a >> (64 - i);
	return o;
}

static uint64_t
clmulr_ref(uint64_t a, uint64_t b)
{
	uint64_t o = 0;

	for (unsigned i = 0; i <= 63; i++)
		if ((b >> i) & 1)
			o ^= a >> (63 - i);
	return o;
}

EXT_INSN(zbc_clmul,  ZBC, "clmul  %[r], %[a], %[b]\n", clmul_ref(a, b))
EXT_INSN(zbc_clmulh, ZBC, "clmulh %[r], %[a], %[b]\n", clmulh_ref(a, b))
EXT_INSN(zbc_clmulr, ZBC, "clmulr %[r], %[a], %[b]\n", clmulr_ref(a, b))

static const struct ext_unit zbc_units[] = {
	{ "clmul",  zbc_clmul  },
	{ "clmulh", zbc_clmulh },
	{ "clmulr", zbc_clmulr },
};

/*
 * Benchmark kernels: each CRCs a 32-bit word per iteration, the word being the
 * previous CRC, so the chain is dependent and nothing folds. Iterating a CRC on itself
 * is safe because the map is a permutation (the polynomial's constant term is set) and
 * W = 0 is its only fixed point. norvc keeps every op 4 bytes for a fair comparison.
 *
 * k is unused here. bench_kernel_fn supplies it for instructions that need an arbitrary
 * second operand (zba.c uses it as the base address its indexes are added to), and
 * every second operand in a CRC is the polynomial's own. Constants are derived before
 * each loop, which costs nothing measured: bench_pair subtracts an N-iteration run from
 * a 2N one, so anything outside the loop cancels.
 */

/* msb-first: fold by K = x^32 mod P, which is P_low itself since x^32 is congruent to
 * it -- so one register serves both the fold and the multiply-back. MU is 33 bits, so
 * T * MU spans 95 and clmulh's window holds real quotient bits. */
static uint64_t
kern_crc_ext(uint64_t acc, uint64_t k, uint64_t n)
{
	uint64_t p = CRC32_POLY;
	uint64_t mu = barrett_mu(p);

	(void)k;

	asm volatile(".option push\n.option norvc\n" ZBC
		     "slli   %[acc], %[acc], 32\n"	/* seed down to 32 bits   */
		     "srli   %[acc], %[acc], 32\n"
		     "1:\n"
		     "clmul  t1, %[acc], %[p]\n"	/* T = W * K, K == P_low  */
		     "clmulh t2, t1, %[mu]\n"		/* Q = upper half of T*MU */
		     "clmul  t2, t2, %[p]\n"		/* Q * P_low              */
		     "xor    t1, t1, t2\n"
		     "slli   %[acc], t1, 32\n"		/* CRC = the low 32 bits  */
		     "srli   %[acc], %[acc], 32\n"
		     "addi   %[n], %[n], -1\n"
		     "bnez   %[n], 1b\n"
		     ".option pop\n"
		     : [acc] "+r"(acc), [n] "+r"(n)
		     : [p] "r"(p), [mu] "r"(mu)
		     : "t1", "t2", "memory");
	return acc;
}

/* msb-first from the definition: append a zero bit, and subtract the polynomial
 * whenever that pushes a term past x^31. The constant carries x^32 so the xor clears
 * the overflow bit as it goes, which saves masking inside the loop. */
static uint64_t
kern_crc_base(uint64_t acc, uint64_t k, uint64_t n)
{
	uint64_t pf = ((uint64_t)1 << 32) | CRC32_POLY;

	(void)k;

	asm volatile(".option push\n.option norvc\n"
		     "slli %[acc], %[acc], 32\n"	/* seed down to 32 bits    */
		     "srli %[acc], %[acc], 32\n"
		     "1:\n"
		     "mv   t0, %[acc]\n"		/* W = the previous CRC    */
		     "li   t1, 32\n"			/* zero bits left to add   */
		     "2:\n"
		     "slli t0, t0, 1\n"			/* append a zero bit       */
		     "srli t2, t0, 32\n"		/* did a term pass x^31?   */
		     "neg  t2, t2\n"			/* all ones if it did      */
		     "and  t2, t2, %[pf]\n"		/* so, the polynomial      */
		     "xor  t0, t0, t2\n"		/* subtract, clearing x^32 */
		     "addi t1, t1, -1\n"
		     "bnez t1, 2b\n"
		     "mv   %[acc], t0\n"
		     "addi %[n], %[n], -1\n"
		     "bnez %[n], 1b\n"
		     ".option pop\n"
		     : [acc] "+r"(acc), [n] "+r"(n)
		     : [pf] "r"(pf)
		     : "t0", "t1", "t2", "memory");
	return acc;
}

/* Reflected: the same CRC bit-reversed. MU_r is the reciprocal reversed and 
 * parked in the high half -- reversed it reads 0x1F7011641,
 * the constant every clmul CRC implementation carries. The accumulator
 * stays 32-bit here, so only the seed needs narrowing. */
static uint64_t
kern_crcr_ext(uint64_t acc, uint64_t k, uint64_t n)
{
	uint64_t pr = revn(CRC32_POLY, 32);
	uint64_t mur = (revn(barrett_mu(CRC32_POLY), 33) & 0xFFFFFFFFu) << 32;

	(void)k;

	asm volatile(".option push\n.option norvc\n" ZBC
		     "slli   %[acc], %[acc], 32\n"	/* seed down to 32 bits     */
		     "srli   %[acc], %[acc], 32\n"
		     "1:\n"
		     "clmul  t1, %[acc], %[mur]\n"	/* T = W * MU_r             */
		     "clmulr %[acc], t1, %[pr]\n"	/* quotient + multiply back */
		     "addi   %[n], %[n], -1\n"
		     "bnez   %[n], 1b\n"
		     ".option pop\n"
		     : [acc] "+r"(acc), [n] "+r"(n)
		     : [mur] "r"(mur), [pr] "r"(pr)
		     : "t1", "memory");
	return acc;
}

/* Reflected from the definition: the data enters at the low end, so the remainder
 * shifts down and the polynomial comes in whenever the bit leaving the bottom is set. */
static uint64_t
kern_crcr_base(uint64_t acc, uint64_t k, uint64_t n)
{
	uint64_t pr = revn(CRC32_POLY, 32);

	(void)k;

	asm volatile(".option push\n.option norvc\n"
		     "slli %[acc], %[acc], 32\n"	/* seed down to 32 bits   */
		     "srli %[acc], %[acc], 32\n"
		     "1:\n"
		     "mv   t0, %[acc]\n"		/* W = the previous CRC   */
		     "li   t1, 32\n"
		     "2:\n"
		     "andi t2, t0, 1\n"			/* the bit leaving        */
		     "neg  t2, t2\n"			/* all ones if it is set  */
		     "and  t2, t2, %[pr]\n"		/* so, the polynomial     */
		     "srli t0, t0, 1\n"			/* and it shifts down     */
		     "xor  t0, t0, t2\n"
		     "addi t1, t1, -1\n"
		     "bnez t1, 2b\n"
		     "mv   %[acc], t0\n"
		     "addi %[n], %[n], -1\n"
		     "bnez %[n], 1b\n"
		     ".option pop\n"
		     : [acc] "+r"(acc), [n] "+r"(n)
		     : [pr] "r"(pr)
		     : "t0", "t1", "t2", "memory");
	return acc;
}

static int
zbc(unsigned flags)
{
	const char *name = "Zbc (carry-less multiply)";
	struct hart_state *hs = hart_get_hstate_self();
	size_t present = 0;

	/* Run each op once under HS_FLAG_PROBING, so an absent instruction is stepped
	 * over (hs->error) instead of trapping for real. Value-independent. */
	for (size_t i = 0; i < ARRAY_SIZE(zbc_units); i++) {
		hs->error = 0;
		hart_set_flags(hs, HS_FLAG_PROBING);
		(void)zbc_units[i].fn(rand64(), rand64());
		hart_clear_flags(hs, HS_FLAG_PROBING);
		present += (hs->error == 0);
	}
	if (present == 0)
		return PROBE_ABSENT;
	if (present < ARRAY_SIZE(zbc_units))
		return PROBE_FAILED;		/* partial: fuzz/bench would trap for real */

	/* Pure probe: report what we support (the benchmark needs mcountinhibit). */
	if (!(flags & (PROBE_FUZZ | PROBE_BENCH)))
		return PROBE_FUZZ | (bench_available() ? PROBE_BENCH : 0);

	if (flags & PROBE_FUZZ) {
		uint64_t rounds = 0;
		int mism = 0;

		INF("Fuzzing %s (differential vs base-ISA), press 'q' to stop...\n", name);
		while (1) {
			uint64_t a = rand64(), b = rand64();

			for (size_t i = 0; i < ARRAY_SIZE(zbc_units); i++)
				if (!zbc_units[i].fn(a, b) && ++mism <= 8)
					INF("  MISMATCH: %s (a=%#lx b=%#lx)\n",
					    zbc_units[i].name, a, b);
			if ((++rounds & 0xFFFFu) == 0) {
				int c = uart_getc();	/* non-blocking */
				if (c == 'q' || c == 'Q')
					break;
			}
		}
		INF("Fuzz done: %lu rounds, %d mismatches\n", rounds, mism);
		return mism ? PROBE_FAILED : 0;
	}

	if (!bench_available()) {
		INF("Zbc benchmark needs mcountinhibit -- unavailable.\n");
		return 0;
	}
	bench_pair("CRC-32 msb-first (clmul+clmulh)", kern_crc_ext, kern_crc_base,
		   0, flags & PROBE_COMPACT);
	bench_pair("CRC-32 reflected (clmul+clmulr)", kern_crcr_ext, kern_crcr_base,
		   0, flags & PROBE_COMPACT);
	return 0;
}
REGISTER_HART_UNPRIV("Zbc (carry-less multiply)", zbc);