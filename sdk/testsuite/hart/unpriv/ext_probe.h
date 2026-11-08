/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Building blocks for differentially probing/fuzzing an unprivileged ISA extension.
 * EXT_INSN defines a per-instruction "unit": the extension op (inline asm, enabled for
 * the fragment via .option arch) checked against a base-ISA reference expression.
 *
 * An extension's probe function (see zba.c) runs the units once under HS_FLAG_PROBING
 * to detect presence (an absent op is stepped over, not trapped) and over random
 * operands to fuzz. Only the register operands vary -- immediates can't, .text is ROM.
 * Performance is timed separately over whole kernels via bench_pair() (ext_bench.h).
 */

#ifndef EXT_PROBE_H
#define EXT_PROBE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>			/* For rand() behind rand64() */

/* Run one extension instruction and its base-ISA reference on the same operands,
 * returning true iff they agree. */
typedef bool (*ext_unit_fn)(uint64_t a, uint64_t b);

struct ext_unit {
	const char *name;		/* instruction mnemonic */
	ext_unit_fn fn;
};

/* 64-bit value from the libc PRNG. rand() yields 31 bits (RAND_MAX == INT_MAX), so
 * OR three disjoint slices to exercise every bit -- including the MSBs a naive
 * two-call shift leaves stuck at 0 (bit 31 matters for the .uw ops). Deterministic,
 * so failures reproduce. */
static inline uint64_t
rand64(void)
{
	return ((uint64_t)rand() << 33) | ((uint64_t)rand() << 2) | (rand() & 3);
}

/*
 * Define a per-instruction unit. `ext_op` is the instruction as inline asm producing
 * %[r] from %[a]/%[b]. `arch` enables it for the block (e.g. ".option arch, +zba\n")
 * and norvc keeps it 4 bytes so a presence probe steps over it cleanly. `ref_expr` is
 * its base-ISA reference: any C expression over a,b -- a one-liner, a ({ ...; v; })
 * statement-expression, or a call to a `static uint64_t f(uint64_t, uint64_t)` helper.
 */
#define EXT_INSN(fn, arch, ext_op, ref_expr)				\
static bool fn(uint64_t a, uint64_t b)					\
{									\
	uint64_t e;							\
	asm volatile(".option push\n.option norvc\n" arch		\
		     ext_op						\
		     ".option pop\n"					\
		     : [r] "=&r"(e) : [a] "r"(a), [b] "r"(b));		\
	return e == (ref_expr);						\
}

#endif /* EXT_PROBE_H */
