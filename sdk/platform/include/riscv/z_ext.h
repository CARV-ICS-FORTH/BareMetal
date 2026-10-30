/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Shared bits for the unprivileged extension probes in platform/src/z_ext/.
 *
 * Those extensions have no CSR to look at, so we run their instructions and
 * see what happens. While HS_FLAG_PROBING_UNPRIV is set the trap handler in
 * hart.c turns an illegal instruction into hs->error = ENOSYS and resumes at
 * the next one, instead of reporting it and hanging.
 *
 * A probe runs each of its extension's instructions once on (EXT_A, EXT_B) and
 * adds the results together, so one comparison at the end covers the whole
 * extension. Every block goes between hart_probe_insn_begin() and
 * hart_probe_insn_end(), and must be assembled with ".option norvc": the
 * handler resumes at mepc + 4, so a compressed instruction would leave us in
 * the middle of the next one.
 */

#ifndef _Z_EXT_H
#define _Z_EXT_H

#include <platform/riscv/caps.h>	/* For CAP_* macros and struct rvcaps */
#include <platform/riscv/hart.h>	/* For hart_state and the flag helpers */
#include <stdbool.h>			/* For bool */
#include <stdint.h>			/* For typed ints */

/*
 * Operands shared by every block. They are picked so that no instruction
 * produces zero: a missing instruction is caught by the trap, but one that
 * decodes as something else would otherwise be able to hide behind a zero
 * term. EXT_A is negative as a signed value and large as an unsigned one, so
 * min/max can't be confused with minu/maxu, and all eight of its bytes are
 * non-zero for orc.b.
 */
#define EXT_A	0x8123456712345678ULL
#define EXT_B	0x00000000FFFFFFFFULL

static inline void
hart_probe_insn_begin(struct hart_state *hs)
{
	hs->error = 0;
	hart_set_flags(hs, HS_FLAG_PROBING_UNPRIV);
	__asm__ volatile ("" ::: "memory");
}

/* True if every instruction in the block executed */
static inline bool
hart_probe_insn_end(struct hart_state *hs)
{
	__asm__ volatile ("" ::: "memory");
	hart_clear_flags(hs, HS_FLAG_PROBING_UNPRIV);
	bool ok = (hs->error == 0);
	hs->error = 0;
	return ok;
}

/*
 * One entry point per instruction family, called by hart_probe_unpriv_caps().
 * Each one keeps its per-extension probes static and does whatever setup the
 * whole family needs (turning the FPU on, saving fcsr, ...) once.
 */
void hart_probe_zb(struct hart_state *hs);	/* z_ext/zb.c: bit manipulation */

#endif /* _Z_EXT_H */