/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>			/* For typed ints */
#include <target_config.h>		/* For PLAT_RAM_SIZE */
#include <platform/riscv/pmp.h>		/* For PMP_* and PLAT_HAS_PMP */
#include <platform/riscv/csr.h>		/* For csr_write()/CSR_PMP* */
#include <platform/riscv/hart.h>	/* For hart_get_hstate_self() */
#include <platform/riscv/early_caps.h>	/* For CAP_SMEPMP (early_caps) */
#include <platform/utils/utils.h>	/* For DBG()/ERR() */

#ifdef PLAT_HAS_PMP

/* A single NAPOT entry needs a power-of-two size (base alignment is checked at
 * runtime below since PLAT_RAM_BASE would overflow int in a constant expr). */
_Static_assert((PLAT_RAM_SIZE & (PLAT_RAM_SIZE - 1)) == 0,
	       "PLAT_RAM_SIZE must be a power of two for a NAPOT PMP rule");

extern uint64_t __data_start;	/* RAM origin (== PLAT_RAM_BASE), from start.S */

/*
 * Configure PMP for the calling hart.
 *
 * One locked NAPOT rule marks our RAM region [__data_start, +PLAT_RAM_SIZE)
 * (data, bss, stack, heap) as RW and non-executable, enforced on M-mode too
 * (L=1). ROM code/rodata are non-writable via PMA and anything outside our RAM
 * region keeps the default M-mode policy. pmpaddr is written before pmpcfg so
 * the entry only goes live once its address is in place.
 */
void
hart_init_pmp(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	uintptr_t base = (uintptr_t)__data_start;

	/* NAPOT needs a naturally aligned base; it's linker-provided, so this
	 * really guards against a misconfigured PLAT_RAM_BASE. */
	if (base & (PLAT_RAM_SIZE - 1)) {
		ERR("PMP: RAM base 0x%lx not aligned to 0x%x, skipping\n",
		    base, (unsigned)PLAT_RAM_SIZE);
		return;
	}

	/* If Smepmp (ePMP) is implemented, set mseccfg.RLB before locking our
	 * rule so locked rules stay modifiable for a later stage (e.g. an FSBL
	 * we hand off to); RLB can't go 0->1 once any rule is locked. mseccfg
	 * traps when Smepmp is absent -- the illegal-instruction handler swallows
	 * it and sets hs->error. */
	csr_set_bits(CSR_MSECCFG, CSR_MSECCFG_RLB);

	/* Read mseccfg back for the log; if the write above trapped (no Smepmp)
	 * report 0 rather than a misleading garbage read, then clear the error. */
	uint64_t mseccfg = hs->error ? 0 : csr_read(CSR_MSECCFG);
	hs->error = 0;

	/* Record Smepmp as an early cap, best effort: RLB is an Smepmp-specific
	 * field, so if it stuck Smepmp is present. A non-sticking RLB is treated as
	 * absent -- this can miss a present Smepmp if the hart booted with a locked
	 * rule (RLB can't go 0->1 then) or hardwires RLB to 0. mseccfg merely
	 * existing is NOT enough (it is shared with Zkr, pointer masking, Zicfilp).
	 * The test-suite probe trusts this instead of re-checking, since by then our
	 * own locked rule below would skew the RLB test. */
	if (mseccfg & CSR_MSECCFG_RLB)
		hs->early_caps |= CAP_SMEPMP;

	csr_write(CSR_PMPADDR_BASE + 0, PMP_NAPOT_ADDR(base, (uintptr_t)PLAT_RAM_SIZE));
	csr_write(CSR_PMPCFG_BASE, (uint64_t)(PMP_A_NAPOT | PMP_R | PMP_W | PMP_L));

	DBG("PMP: RAM [0x%lx-0x%lx) RW-NX (NAPOT, locked), mseccfg=0x%lx\n",
	    base, base + (uintptr_t)PLAT_RAM_SIZE, mseccfg);
}

#endif /* PLAT_HAS_PMP */
