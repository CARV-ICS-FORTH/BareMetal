/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * S-mode privileged units (REGISTER_HART_PRIV_S). Most are plain CSR probes. The
 * virtual-memory extensions are collapsed into one "Sv*" unit whose PROBE_INFO
 * reports everything it can detect functionally -- paging modes, ASID width,
 * Svnapot, Svinval -- plus the CSR-detectable Svadu/Svpbmt. (A full MMU benchmark
 * will come later as PROBE_BENCH.)
 */

#include "hart_priv_caps.h"
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/utils/utils.h>	/* For INF() */
#include <test_framework.h>		/* For REGISTER_HART_PRIV_S + PROBE_* */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

static int
hart_probe_ssdbltrp(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_DTE);
}
REGISTER_HART_PRIV_S("Ssdbltrp (double trap)", hart_probe_ssdbltrp);

static int
hart_probe_smcdeleg(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_CDE);
}
REGISTER_HART_PRIV_S("Smcdeleg (counter delegation)", hart_probe_smcdeleg);

static int
hart_probe_sstc(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_STIMECMP);
}
REGISTER_HART_PRIV_S("Sstc (timer compare)", hart_probe_sstc);

static int
hart_probe_ssnpm(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_field(CSR_SENVCFG, CSR_MENVCFG_PMM, 2);
}
REGISTER_HART_PRIV_S("Ssnpm (pointer masking)", hart_probe_ssnpm);

static int
hart_probe_ssstateen(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_SSTATEEN0);
}
REGISTER_HART_PRIV_S("Ssstateen (state enable)", hart_probe_ssstateen);

static int
hart_probe_sscsrind(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_SISELECT);
}
REGISTER_HART_PRIV_S("Sscsrind (indirect CSR access)", hart_probe_sscsrind);

static int
hart_probe_ssqosid(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_SRMCFG);
}
REGISTER_HART_PRIV_S("Ssqosid (QoS identifiers)", hart_probe_ssqosid);

static int
hart_probe_sscofpmf(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_SCOUNTOVF);
}
REGISTER_HART_PRIV_S("Sscofpmf (counter overflow)", hart_probe_sscofpmf);

static int
hart_probe_ssctr(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_SCTRCTL);
}
REGISTER_HART_PRIV_S("Ssctr (control transfer records)", hart_probe_ssctr);

/* scountinhibit is reachable only while menvcfg.CDE=1 (Smcdeleg enabled); enable CDE
 * around the probe and restore it (read-only zero if Smcdeleg is absent). */
static int
hart_probe_ssccfg(unsigned flags)
{
	(void)flags;
	csr_set_bits(CSR_MENVCFG, CSR_MENVCFG_CDE);
	int r = hart_probe_csr_exists(CSR_SCOUNTINHIBIT);
	csr_clear_bits(CSR_MENVCFG, CSR_MENVCFG_CDE);
	return r;
}
REGISTER_HART_PRIV_S("Ssccfg (counter configuration)", hart_probe_ssccfg);

static int
hart_probe_ssaia(unsigned flags)
{
	(void)flags;
	/* stopi (AIA core), not stopei (IMSIC-only): detect with or without an IMSIC. */
	return hart_probe_csr_exists(CSR_STOPI);
}
REGISTER_HART_PRIV_S("Ssaia (advanced interrupt architecture)", hart_probe_ssaia);

/**************************\
* Virtual memory (one unit) *
\**************************/

extern const uintptr_t __data_start;
extern const uintptr_t __ram_end;

/*
 * Functionally test a paging mode: build a mapping for VA 0 and read it back via
 * MPRV (U-mode translation); returns true if it translates. When `asid_out` is
 * non-NULL, measures the ASID width while the page table is live. Restores satp.
 */
static bool
mmu_mode_works(uint64_t mode, bool napot, uint8_t *asid_out)
{
	struct hart_state *hs = hart_get_hstate_self();
	uintptr_t phys;
	size_t map_size, zero_size = 0;
	size_t align = napot ? 65536 : 4096;

	hs->error = 0;

	/* MPRV reads need U-mode PMP access; open RAM via a TOR rule if none exists. */
	uint64_t pmpcfg0 = csr_read(CSR_PMPCFG_BASE);
	if (hs->error == 0 && pmpcfg0 == 0) {
		csr_write(CSR_PMPADDR_BASE, ((uintptr_t)__ram_end) >> 2);
		csr_write(CSR_PMPCFG_BASE, 0x0F);	/* RWX, TOR */
	}

	phys = ((uintptr_t)__data_start + align - 1) & ~(align - 1);
	if (phys >= (uintptr_t)__ram_end)
		return false;

	map_size = align;
	if (hart_va_map_range(phys, &map_size, mode, napot) != 0)
		return false;

	volatile uint64_t test_val;
	uint64_t old_mstatus = csr_read(CSR_MSTATUS);
	uint64_t new_mstatus = (old_mstatus & ~FIELD_GET_MASK(CSR_MSTATUS_MPP)) |
			       FIELD_PREP(CSR_MSTATUS_MPP, PRIV_MODE_U) | CSR_MSTATUS_MPRV;
	asm volatile(
		"csrw mstatus, %1\n"
		"ld   %0, 0(zero)\n"
		"csrw mstatus, %2\n"
		: "=&r"(test_val) : "r"(new_mstatus), "r"(old_mstatus) : "memory");

	if (asid_out) {
		uint64_t satp = csr_read(CSR_SATP);
		csr_write(CSR_SATP, satp | FIELD_PREP_ULL(CSR_SATP_ASID, 0xFFFF));
		uint16_t mask = FIELD_GET_ULL(CSR_SATP_ASID, csr_read(CSR_SATP));
		csr_write(CSR_SATP, satp);
		uint8_t bits = 0;
		for (; mask; mask >>= 1)
			bits += mask & 1;
		*asid_out = bits;
	}

	hart_va_map_range(0, &zero_size, 0, false);	/* tear down satp + page table */
	return hs->error == 0;
}

/* Does sinval.vma execute (Svinval)? It is a SYSTEM insn legal in M-mode; the trap
 * handler flags it via hs->error (ENOSYS) when unimplemented. Svinval isn't in the
 * SDK's rv64gc baseline, so enable it just for this instruction via .option arch. */
static bool
svinval_works(void)
{
	struct hart_state *hs = hart_get_hstate_self();

	hs->error = 0;
	asm volatile(".option push\n.option arch, +svinval\n"
		     "sinval.vma x0, x0\n"
		     ".option pop\n" ::: "memory");
	bool ok = (hs->error == 0);
	hs->error = 0;
	return ok;
}

static int
hart_probe_sv(unsigned flags)
{
	uint8_t asid = 0;

	/* Presence: bare-only harts can't enable Sv39. */
	if (!mmu_mode_works(SATP_MODE_SV39, false, NULL))
		return PROBE_ABSENT;

	if (flags & PROBE_INFO) {
		INF("\tPaging modes: Sv39");
		if (mmu_mode_works(SATP_MODE_SV48, false, NULL))
			INF(" Sv48");
		if (mmu_mode_works(SATP_MODE_SV57, false, NULL))
			INF(" Sv57");
		INF("\n");
		if (mmu_mode_works(SATP_MODE_SV39, false, &asid))
			INF("\tASID bits: %u\n", asid);
		if (mmu_mode_works(SATP_MODE_SV39, true, NULL))
			INF("\tSvnapot (naturally aligned power-of-2)\n");
		if (svinval_works())
			INF("\tSvinval (fine-grained TLB invalidation)\n");
		if (hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_ADUE) == 0)
			INF("\tSvadu (hardware A/D updates)\n");
		if (hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_PBMTE) == 0)
			INF("\tSvpbmt (page-based memory types)\n");
	}

	return PROBE_INFO;
}
REGISTER_HART_PRIV_S("Sv* (virtual memory)", hart_probe_sv);
