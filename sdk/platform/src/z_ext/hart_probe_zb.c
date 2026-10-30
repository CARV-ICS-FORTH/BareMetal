/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Bit manipulation extensions: Zba, Zbb, Zbc, Zbs and the Zbk* set that the
 * scalar crypto extensions build on. See platform/include/riscv/z_ext.h for
 * how a block is put together.
 */

#include <platform/riscv/z_ext.h>

/*
 * Sum of all eight Zba instructions run on (EXT_A, EXT_B). Recompute by hand
 * from the instruction semantics if you change the operands.
 *
 * ".option arch" makes the assembler encode the real mnemonics whatever -march
 * the SDK is built with, ".option norvc" keeps every instruction 4 bytes wide.
 * Both outputs are early-clobber: without that the compiler is free to hand us
 * the same register for an output and for EXT_A/EXT_B, which the first
 * instruction would then overwrite.
 */
#define ZBA_EXPECT	0x0fedcbb42a190791ULL

static void
hart_probe_zba(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zba\n\t"
		"sh1add    %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"sh2add    %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"sh3add    %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"add.uw    %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"sh1add.uw %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"sh2add.uw %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"sh3add.uw %[t], %[a], %[b]\n\t"
		"add       %[x], %[x], %[t]\n\t"
		"slli.uw   %[t], %[a], 7\n\t"
		"add       %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBA_EXPECT)
		caps->z_caps |= CAP_ZBA;
} 

/* Same idea, for all 24 Zbb instructions. clz/sext.b/sext.h/zext.h run on
 * EXT_B, the rest on EXT_A, so that every one of them yields a non-zero
 * result with these operands. */
#define ZBB_EXPECT	0xae7732ed4965ff5cULL

static void
hart_probe_zbb(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbb\n\t"
		"andn   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"orn    %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"xnor   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"clz    %[t], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"clzw   %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"ctz    %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"ctzw   %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"cpop   %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"cpopw  %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"max    %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"maxu   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"min    %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"minu   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"sext.b %[t], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"sext.h %[t], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"zext.h %[t], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"rol    %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"ror    %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"rori   %[t], %[a], 7\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"rolw   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"rorw   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"roriw  %[t], %[a], 7\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"orc.b  %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"rev8   %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBB_EXPECT)
		caps->z_caps |= CAP_ZBB;
}

/* Zbc: carry-less multiply, the three halves of the same 128-bit product. */
#define ZBC_EXPECT	0x8ef2f10c6f487b8fULL

static void
hart_probe_zbc(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbc\n\t"
		"clmul  %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"clmulh %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"clmulr %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBC_EXPECT)
		caps->z_caps |= CAP_ZBC;
}

/* Zbs: set, clear, invert and extract a single bit, register and immediate
 * forms. The operands are chosen so that every one of them changes its input:
 * a bit that is already in the wanted state would make a no-op look right. */
#define ZBS_EXPECT	0x05b05b045b05b119ULL

static void
hart_probe_zbs(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbs\n\t"
		"bclr   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"bclri  %[t], %[a], 6\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"bext   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"bexti  %[t], %[a], 6\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"binv   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"binvi  %[t], %[a], 7\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"bset   %[t], %[b], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"bseti  %[t], %[a], 7\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBS_EXPECT)
		caps->z_caps |= CAP_ZBS;
}

/* Zbkb: bit manipulation for crypto. Ten of its fourteen instructions are the
 * same ones Zbb has (andn/orn/xnor, the rotates, rev8), so only the four that
 * are unique to it are probed here. */
#define ZBKB_EXPECT	0x81c4a2e55a611686ULL

static void
hart_probe_zbkb(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbkb\n\t"
		"pack   %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"packh  %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"packw  %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"brev8  %[t], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBKB_EXPECT)
		caps->z_caps |= CAP_ZBKB;
}

/* Zbkc: the carry-less multiplies crypto needs. Zbc is a superset (it adds
 * clmulr), but Zbkc can be implemented on its own, so it gets its own probe. */
#define ZBKC_EXPECT	0x8ef2f10b710af54aULL

static void
hart_probe_zbkc(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbkc\n\t"
		"clmul  %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"clmulh %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBKC_EXPECT)
		caps->z_caps |= CAP_ZBKC;
}

/* Zbkx: crossbar permutations. xperm4 takes its nibble indices from EXT_A
 * itself, since every nibble of EXT_B is either 0 or 15 and the result would
 * be the same nibble repeated. xperm8 keeps EXT_B: its 0xFF bytes are out of
 * range and must come back as zero, which is worth testing. */
#define ZBKX_EXPECT	0xefddbb9976543217ULL

static void
hart_probe_zbkx(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t x = 0, t = 0;

	hart_probe_insn_begin(hs);
	__asm__ volatile (
		".option push\n\t"
		".option norvc\n\t"
		".option arch, +zbkx\n\t"
		"xperm4 %[t], %[a], %[a]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		"xperm8 %[t], %[a], %[b]\n\t"
		"add    %[x], %[x], %[t]\n\t"
		".option pop"
		: [x] "+&r"(x), [t] "+&r"(t)
		: [a] "r"(EXT_A), [b] "r"(EXT_B));

	if (hart_probe_insn_end(hs) && x == ZBKX_EXPECT)
		caps->z_caps |= CAP_ZBKX;
}


/* Entry point for the family, called by hart_probe_unpriv_caps() */
void
hart_probe_zb(struct hart_state *hs)
{
	hart_probe_zba(hs);
	hart_probe_zbb(hs);
	hart_probe_zbc(hs);
	hart_probe_zbs(hs);
	hart_probe_zbkb(hs);
	hart_probe_zbkc(hs);
	hart_probe_zbkx(hs);
}