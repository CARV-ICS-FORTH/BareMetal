/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Zba (address generation) -- worked example of an unprivileged-extension unit.
 *
 * Zba fuses "base + index*scale": shNadd = (rs1 << N) + rs2 for array indexing with
 * scale 2/4/8, and the .uw variants zero-extend rs1[31:0] first for an unsigned
 * 32-bit index on RV64 (array[(u32)i]). Each instruction gets an EXT_INSN unit (the
 * op vs its base-ISA reference). The one zba() function drives them off `flags`:
 * probe presence, fuzz differentially, or run the IPC benchmark.
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

#define ZBA	".option arch, +zba\n"

/* slli.uw takes an immediate shift -- one representative amount for the example
 * (a fuller sweep would add a few more). Kept in sync between asm and C. */
#define SLLI_UW_SHAMT	7

EXT_INSN(zba_sh1add,    ZBA, "sh1add    %[r], %[a], %[b]\n", (a << 1) + b)
EXT_INSN(zba_sh2add,    ZBA, "sh2add    %[r], %[a], %[b]\n", (a << 2) + b)
EXT_INSN(zba_sh3add,    ZBA, "sh3add    %[r], %[a], %[b]\n", (a << 3) + b)
EXT_INSN(zba_add_uw,    ZBA, "add.uw    %[r], %[a], %[b]\n", (uint64_t)(uint32_t)a + b)
EXT_INSN(zba_sh1add_uw, ZBA, "sh1add.uw %[r], %[a], %[b]\n", ((uint64_t)(uint32_t)a << 1) + b)
EXT_INSN(zba_sh2add_uw, ZBA, "sh2add.uw %[r], %[a], %[b]\n", ((uint64_t)(uint32_t)a << 2) + b)
EXT_INSN(zba_sh3add_uw, ZBA, "sh3add.uw %[r], %[a], %[b]\n", ((uint64_t)(uint32_t)a << 3) + b)
EXT_INSN(zba_slli_uw,   ZBA, "slli.uw   %[r], %[a], 7\n",    (uint64_t)(uint32_t)a << SLLI_UW_SHAMT)

static const struct ext_unit zba_units[] = {
	{ "sh1add",    zba_sh1add    },
	{ "sh2add",    zba_sh2add    },
	{ "sh3add",    zba_sh3add    },
	{ "add.uw",    zba_add_uw    },
	{ "sh1add.uw", zba_sh1add_uw },
	{ "sh2add.uw", zba_sh2add_uw },
	{ "sh3add.uw", zba_sh3add_uw },
	{ "slli.uw",   zba_slli_uw   },
};

/*
 * Benchmark kernels: one register-only loop chains all eight Zba instructions per
 * iteration (a synthetic address-generation mix), written once with Zba and once as a
 * base-ISA shift/add twin computing the same acc. Dependent chain (each op consumes the
 * previous result) so nothing folds. The base twin uses t0 as scratch, and norvc keeps
 * every op 4 bytes for a fair comparison.
 */
static uint64_t
kern_zba_ext(uint64_t acc, uint64_t k, uint64_t n)
{
	asm volatile(".option push\n.option norvc\n" ZBA
		     "1:\n"
		     "sh1add    %0, %0, %2\n"
		     "sh2add    %0, %0, %2\n"
		     "sh3add    %0, %0, %2\n"
		     "add.uw    %0, %0, %2\n"
		     "sh1add.uw %0, %0, %2\n"
		     "sh2add.uw %0, %0, %2\n"
		     "sh3add.uw %0, %0, %2\n"
		     "slli.uw   %0, %0, 1\n"
		     "addi      %1, %1, -1\n"
		     "bnez      %1, 1b\n"
		     ".option pop\n"
		     : "+r"(acc), "+r"(n) : "r"(k) : "memory");
	return acc;
}

static uint64_t
kern_zba_base(uint64_t acc, uint64_t k, uint64_t n)
{
	asm volatile(".option push\n.option norvc\n"
		     "1:\n"
		     "slli t0, %0, 1\n  add  %0, t0, %2\n"	/* sh1add       */
		     "slli t0, %0, 2\n  add  %0, t0, %2\n"	/* sh2add       */
		     "slli t0, %0, 3\n  add  %0, t0, %2\n"	/* sh3add       */
		     "slli t0, %0, 32\n srli t0, t0, 32\n add %0, t0, %2\n" /* add.uw    */
		     "slli t0, %0, 32\n srli t0, t0, 31\n add %0, t0, %2\n" /* sh1add.uw */
		     "slli t0, %0, 32\n srli t0, t0, 30\n add %0, t0, %2\n" /* sh2add.uw */
		     "slli t0, %0, 32\n srli t0, t0, 29\n add %0, t0, %2\n" /* sh3add.uw */
		     "slli t0, %0, 32\n srli %0, t0, 31\n"	/* slli.uw %0,%0,1 */
		     "addi %1, %1, -1\n"
		     "bnez %1, 1b\n"
		     ".option pop\n"
		     : "+r"(acc), "+r"(n) : "r"(k) : "t0", "memory");
	return acc;
}

/* The one Zba unit: probe the instructions, then act on `flags` (test_framework.h). */
static int
zba(unsigned flags)
{
	const char *name = "Zba (address generation)";
	struct hart_state *hs = hart_get_hstate_self();
	size_t present = 0;

	/* Probe: run each op once under HS_FLAG_PROBING, so an absent instruction is
	 * stepped over (hs->error) instead of trapping for real. Value-independent. */
	for (size_t i = 0; i < ARRAY_SIZE(zba_units); i++) {
		hs->error = 0;
		hart_set_flags(hs, HS_FLAG_PROBING);
		(void)zba_units[i].fn(rand64(), rand64());
		hart_clear_flags(hs, HS_FLAG_PROBING);
		present += (hs->error == 0);
	}
	if (present == 0)
		return PROBE_ABSENT;
	if (present < ARRAY_SIZE(zba_units))
		return PROBE_FAILED;		/* partial: fuzz/bench would trap for real */

	/* Pure probe: report the ops we support (benchmark needs mcountinhibit). */
	if (!(flags & (PROBE_FUZZ | PROBE_BENCH)))
		return PROBE_FUZZ | (bench_available() ? PROBE_BENCH : 0);

	if (flags & PROBE_FUZZ) {
		uint64_t rounds = 0;
		int mism = 0;

		INF("Fuzzing %s (differential vs base-ISA), press 'q' to stop...\n", name);
		while (1) {
			uint64_t a = rand64(), b = rand64();

			for (size_t i = 0; i < ARRAY_SIZE(zba_units); i++)
				if (!zba_units[i].fn(a, b) && ++mism <= 8)
					INF("  MISMATCH: %s (a=%#lx b=%#lx)\n",
					    zba_units[i].name, a, b);
			if ((++rounds & 0xFFFFu) == 0) {
				int c = uart_getc();	/* non-blocking */
				if (c == 'q' || c == 'Q')
					break;
			}
		}
		INF("Fuzz done: %lu rounds, %d mismatches\n", rounds, mism);
		return mism ? PROBE_FAILED : 0;
	}

	/* PROBE_BENCH */
	if (!bench_available()) {
		INF("Zba benchmark needs mcountinhibit -- unavailable.\n");
		return 0;
	}
	bench_pair("Zba addr-gen chain", kern_zba_ext, kern_zba_base, 3,
		   flags & PROBE_COMPACT);
	return 0;
}
REGISTER_HART_UNPRIV("Zba (address generation)", zba);
