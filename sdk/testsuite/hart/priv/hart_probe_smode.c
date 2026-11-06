/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * S-mode privileged capability probing, including the virtual-memory extensions.
 *
 * The Sv* / NAPOT / SVINVAL detection is functional: it builds a real page table
 * and accesses VA 0 via MPRV, so it reports what actually translates (and probes
 * ASID width as a side effect) rather than guessing from a CSR bit.
 */

#include "hart_priv_caps.h"
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/utils/utils.h>	/* For DBG() */
#include <stddef.h>			/* For size_t */
#include <stdint.h>			/* For typed ints */
#include <stdbool.h>			/* For bool */

static void
hart_probe_ssdbltrp(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_DTE, caps->s_caps, CAP_SSDBLTRP);
}

static void
hart_probe_sbe(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MSTATUS, CSR_MSTATUS_SBE, caps->s_caps, CAP_SBE);
}

static void
hart_probe_svpbmt(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	/* TODO: Check without MENVCFG */
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_PBMTE, caps->s_caps, CAP_SVPBMT);
}

static void
hart_probe_svadu(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_ADUE, caps->s_caps, CAP_SVADU);
}

static void
hart_probe_smcdeleg(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_CDE, caps->s_caps, CAP_SMCDELEG);
}

static void
hart_probe_sstc(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_STIMECMP, caps->s_caps, CAP_SSTC);
}

static void
hart_probe_ssnpm(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_field(CSR_SENVCFG, CSR_MENVCFG_PMM, 2, caps->s_caps, CAP_SSNPM);
}

static void
hart_probe_ssstateen(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_SSTATEEN0, caps->s_caps, CAP_SSSTATEEN);
}

static void
hart_probe_sscsrind(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_SISELECT, caps->s_caps, CAP_SSCSRIND);
}

static void
hart_probe_ssqosid(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_SRMCFG, caps->s_caps, CAP_SSQOSID);
}

static void
hart_probe_sscofpmf(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_SCOUNTOVF, caps->s_caps, CAP_SSCOFPMF);
}

static void
hart_probe_ssctr(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_SCTRCTL, caps->s_caps, CAP_SSCTR);
}

static void
hart_probe_ssccfg(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	/* scountinhibit is accessible only while menvcfg.CDE=1 (Smcdeleg enabled);
	 * with CDE=0 the access traps and Ssccfg "appears not implemented". Enable
	 * CDE around the probe and restore it. If Smcdeleg is absent CDE is read-only
	 * zero, so this correctly detects nothing. */
	csr_set_bits(CSR_MENVCFG, CSR_MENVCFG_CDE);
	hart_probe_cap_by_csr_existence(CSR_SCOUNTINHIBIT, caps->s_caps, CAP_SSCCFG);
	csr_clear_bits(CSR_MENVCFG, CSR_MENVCFG_CDE);
}

static void
hart_probe_ssaia(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	/* stopi (AIA-core), not stopei (IMSIC-only): detect Ssaia with or without an
	 * S-mode IMSIC interrupt file. */
	hart_probe_cap_by_csr_existence(CSR_STOPI, caps->s_caps, CAP_SSAIA);
}

/**************************\
* Virtual Memory extensions *
\**************************/

/* External symbols from linker/start.S */
extern const uintptr_t __data_start;
extern const uintptr_t __ram_end;

/* Helper to test MMU mode by creating a mapping and trying to access VA 0.
 * Also probes for ASID bits on first call if caps->num_asid_bits is 0. */
static bool
hart_test_mmu_mode(struct hart_state *hs, uint64_t mode, bool napot)
{
	struct rvcaps *caps = hs->caps;

	/* Clear error flag */
	hs->error = 0;

	/* Ensure PMP allows U-mode access (needed for MPRV testing) */
	uint64_t pmpcfg0 = csr_read(CSR_PMPCFG_BASE);
	if (hs->error == 0 && pmpcfg0 == 0) {
		/* No PMP rules configured - U-mode would be blocked from all memory.
		 * Configure PMP entry 0 to allow U-mode RWX access to all RAM using TOR mode. */
		csr_write(CSR_PMPADDR_BASE + 0, ((uintptr_t)__ram_end) >> 2);
		csr_write(CSR_PMPCFG_BASE, 0x0F);  /* R=1, W=1, X=1, A=01 (TOR) */
	}

	/* Find a properly aligned physical address in RAM to map */
	uintptr_t phys_addr = __data_start;
	size_t align = napot ? 65536 : 4096;

	/* Align up to page/napot boundary */
	phys_addr = (phys_addr + align - 1) & ~(align - 1);

	/* Make sure it's within RAM bounds */
	if (phys_addr >= __ram_end)
		return false;

	/* Request a mapping (one page) */
	size_t map_size = align;
	int ret = hart_va_map_range(phys_addr, &map_size, mode, napot);
	if (ret != 0) {
		DBG("VA: Failed to create mapping for mode %lu: %d\n", mode, ret);
		return false;
	}

	/* Test for SVINVAL extension support, SVINVAL adds fine-grained TLB
	 * invalidation instructions: sinval.vma, sfence.w.inval, and sfence.inval.ir.
	 * We test by executing sinval.vma x0, x0 (encoding: 0x16000073).
	 * Like sfence.vma, this is an S-mode instruction that can be executed
	 * in M-mode. If not implemented, it will trap with illegal instruction, but
	 * since it's a SYSTEM instruction our trap handler will just ignore it
	 * and set hs->error to ENOSYS. */
	if (!(caps->s_caps & CAP_SVINVAL)) {
		hs->error = 0;
		asm volatile(".word 0x16000073" ::: "memory");  /* sinval.vma x0, x0 */
		if (hs->error == 0) {
			caps->s_caps |= CAP_SVINVAL;
			DBG("VA: SVINVAL extension detected\n");
		}
		/* Clear error so it doesn't interfere with page fault detection below */
		hs->error = 0;
	}

	/* Try to access VA 0 using MPRV (load with U-mode privilege and translation).
	 * Do this in a single asm block to avoid any compiler-generated
	 * loads/stores while MPRV is active. */
	volatile uint64_t test_val;

	/* Prepare new mstatus value: clear MPP, set to U-mode, set MPRV */
	uint64_t old_mstatus = csr_read(CSR_MSTATUS);
	uint64_t mpp_cleared = old_mstatus & ~FIELD_GET_MASK(CSR_MSTATUS_MPP);
	uint64_t mpp_value = FIELD_PREP(CSR_MSTATUS_MPP, PRIV_MODE_U);
	uint64_t new_mstatus = mpp_cleared | mpp_value | CSR_MSTATUS_MPRV;

	asm volatile(
		"csrw mstatus, %1\n"		/* Write new mstatus (enable MPRV with MPP=U) */
		"ld %0, 0(zero)\n"		/* Load from VA 0 */
		"csrw mstatus, %2\n"		/* Restore old mstatus */
		: "=&r"(test_val)
		: "r"(new_mstatus), "r"(old_mstatus)
		: "memory"
	);

	/* Probe ASID bits on first call (when num_asid_bits is still 0). */
	if (caps->num_asid_bits == 0) {
		/* Read current satp (has valid page table and mode) */
		uint64_t current_satp = csr_read(CSR_SATP);

		/* Set all ASID bits to 1 while keeping mode and PPN */
		uint64_t test_satp = current_satp | FIELD_PREP_ULL(CSR_SATP_ASID, 0xFFFF);
		csr_write(CSR_SATP, test_satp);
		uint64_t readback = csr_read(CSR_SATP);

		/* Restore original satp (without ASID bits set) */
		csr_write(CSR_SATP, current_satp);

		/* Count the number of bits that stuck in the ASID field */
		uint16_t asid_mask = FIELD_GET_ULL(CSR_SATP_ASID, readback);
		uint8_t asid_bits = 0;
		while (asid_mask) {
			if (asid_mask & 1)
				asid_bits++;
			asid_mask >>= 1;
		}
		caps->num_asid_bits = asid_bits;
	}

	/* Clear SATP and free page table */
	size_t zero_size = 0;
	hart_va_map_range(0, &zero_size, 0, false);

	/* Check if we got an error (page fault) */
	if (hs->error != 0) {
		DBG("VA: Mode %lu%s test failed with error %d\n",
		    mode, napot ? " + NAPOT" : "", hs->error);
		return false;
	}

	DBG("VA: Mode %lu%s test succeeded\n", mode, napot ? " + NAPOT" : "");
	return true;
}

static void
hart_probe_sv39(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	if (hart_test_mmu_mode(hs, SATP_MODE_SV39, false))
		caps->s_caps |= CAP_SV39;
}

static void
hart_probe_sv39_napot(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	/* Only test if Sv39 is supported */
	if (!(caps->s_caps & CAP_SV39))
		return;

	if (hart_test_mmu_mode(hs, SATP_MODE_SV39, true))
		caps->s_caps |= CAP_SVNAPOT;
}

static void
hart_probe_sv48(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	if (hart_test_mmu_mode(hs, SATP_MODE_SV48, false))
		caps->s_caps |= CAP_SV48;
}

static void
hart_probe_sv57(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	if (hart_test_mmu_mode(hs, SATP_MODE_SV57, false))
		caps->s_caps |= CAP_SV57;
}

void
hart_probe_smode(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	hart_probe_ssdbltrp(hs);
	hart_probe_sbe(hs);
	hart_probe_svpbmt(hs);
	hart_probe_svadu(hs);
	hart_probe_smcdeleg(hs);
	hart_probe_sstc(hs);
	hart_probe_ssnpm(hs);
	hart_probe_ssstateen(hs);
	hart_probe_sscsrind(hs);
	hart_probe_ssqosid(hs);
	hart_probe_sscofpmf(hs);
	hart_probe_ssctr(hs);
	hart_probe_ssccfg(hs);
	hart_probe_ssaia(hs);

	/* Virtual memory: probe Sv39 first, then NAPOT and higher modes if present. */
	hart_probe_sv39(hs);
	if (caps->s_caps & CAP_SV39) {
		hart_probe_sv39_napot(hs);
		hart_probe_sv48(hs);
		hart_probe_sv57(hs);
	}
}
