/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/riscv/caps.h>	/* For CAP_* macros */
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/riscv/hart.h>	/* For hart_state and definitions */
#include <platform/utils/utils.h>	/* For DBG() */
#include <stddef.h>			/* For size_t */
#include <stdbool.h>			/* For bool */
#include <string.h>			/* For memset() */

/*********\
* Helpers *
\*********/

/* Helper to probe for a capability by testing if a CSR exists.
 * Sets the capability flag if the CSR is accessible. */
#define hart_probe_cap_by_csr_existence(csr_addr, caps_field, cap_flag)	\
do {									\
	hs->error = 0;							\
	(void)csr_read(csr_addr);					\
	if (hs->error == 0)						\
		caps_field |= cap_flag;					\
} while (0)

/* Helper to probe for a capability by testing if a single-bit CSR field sticks
 * when written. Sets the capability flag if the field is supported.*/
#define hart_probe_cap_by_csr_bit(csr_addr, field_mask, caps_field, cap_flag)	\
do {										\
	hs->error = 0;								\
	csr_set_bits(csr_addr, field_mask);					\
	if (hs->error == 0) {							\
		uint64_t val = csr_read(csr_addr);				\
		if (val & field_mask) {						\
			caps_field |= cap_flag;					\
			csr_clear_bits(csr_addr, field_mask);			\
		}								\
	}									\
} while (0)

/* Helper to probe for a capability by testing if a multi-bit CSR field sticks
 * when written. Sets the capability flag if the field is supported.
 * test_val should be a non-zero value appropriate for the field (e.g., 2 for PMM fields).*/
#define hart_probe_cap_by_csr_field(csr_addr, field_def, test_val, caps_field, cap_flag)\
do {											\
	hs->error = 0;									\
	uint64_t val = (uint64_t)(test_val) << FIELD_GET_SHIFT(field_def);		\
	csr_set_bits(csr_addr, val);							\
	if (hs->error == 0) {								\
		uint64_t readback = csr_read(csr_addr);					\
		if (readback & FIELD_GET_MASK(field_def)) {				\
			caps_field |= cap_flag;						\
			csr_clear_bits(csr_addr, val);					\
		}									\
	}										\
} while (0)

/*********************\
* M-mode capabilities *
\*********************/

static void
hart_probe_smdbltrp(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	/* Check if MDT sticks, note this will clear MIE */
	hart_probe_cap_by_csr_bit(CSR_MSTATUS, CSR_MSTATUS_MDT, caps->m_caps, CAP_SMDBLTRP);
}

static void
hart_probe_mbe(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MSTATUS, CSR_MSTATUS_MBE, caps->m_caps, CAP_MBE);
}

static void
hart_probe_smmpm(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_field(CSR_MSECCFG, CSR_MSECCFG_PMM, 2, caps->m_caps, CAP_SMMPM);
}

static void
hart_probe_smnpm(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_field(CSR_MENVCFG, CSR_MENVCFG_PMM, 2, caps->m_caps, CAP_SMNPM);
}

static void
hart_probe_smstateen(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MSTATEEN0, caps->m_caps, CAP_SMSTATEEN);
}

static void
hart_probe_smcsrind(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MISELECT, caps->m_caps, CAP_SMCSRIND);
}

static void
hart_probe_smrnmi(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MNSCRATCH, caps->m_caps, CAP_SMRNMI);
}

/* Macros to probe PMP registers at compile time */
#define __check_pmp_csr(__n, __pmp_count, __hs, __testval)		\
do {									\
	__hs->error = 0;						\
	uint64_t __orig = csr_read(CSR_PMPADDR_BASE + __n);		\
	if (__hs->error != 0)						\
		goto __pmp_done;					\
	csr_write(CSR_PMPADDR_BASE + __n, __testval);			\
	uint64_t __test = csr_read(CSR_PMPADDR_BASE + __n);		\
	csr_write(CSR_PMPADDR_BASE + __n, __orig);			\
	if (__test != 0)						\
		__pmp_count = __n + 1;					\
	else								\
		goto __pmp_done;					\
} while (0)

#define __check_pmp_csr_2(__base, __pmp_count, __hs, __testval)	\
	__check_pmp_csr(__base + 0, __pmp_count, __hs, __testval);	\
	__check_pmp_csr(__base + 1, __pmp_count, __hs, __testval)

#define __check_pmp_csr_4(__base, __pmp_count, __hs, __testval)	\
	__check_pmp_csr_2(__base + 0, __pmp_count, __hs, __testval);	\
	__check_pmp_csr_2(__base + 2, __pmp_count, __hs, __testval)

#define __check_pmp_csr_8(__base, __pmp_count, __hs, __testval)	\
	__check_pmp_csr_4(__base + 0, __pmp_count, __hs, __testval);	\
	__check_pmp_csr_4(__base + 4, __pmp_count, __hs, __testval)

#define __check_pmp_csr_16(__base, __pmp_count, __hs, __testval)	\
	__check_pmp_csr_8(__base + 0, __pmp_count, __hs, __testval);	\
	__check_pmp_csr_8(__base + 8, __pmp_count, __hs, __testval)

#define __check_pmp_csr_32(__base, __pmp_count, __hs, __testval)	\
	__check_pmp_csr_16(__base + 0, __pmp_count, __hs, __testval);	\
	__check_pmp_csr_16(__base + 16, __pmp_count, __hs, __testval)

#define __check_pmp_csr_64(__pmp_count, __hs, __testval)		\
	__check_pmp_csr_32(0, __pmp_count, __hs, __testval);		\
	__check_pmp_csr_32(32, __pmp_count, __hs, __testval)

/* Probe for PMP support and count number of regions */
static void
hart_probe_pmp(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint8_t num_pmp = 0;

	/* First check if PMP is supported by trying to access PMPCFG0 and PMPADDR0 */
	hs->error = 0;
	csr_write(CSR_PMPCFG_BASE, 0);
	if (hs->error != 0) {
		caps->num_pmp_rules = 0;
		return;
	}

	/* Try writing to PMPADDR0 to determine which address bits are supported */
	csr_write(CSR_PMPADDR_BASE, 0xFFFFFFFFFFFFFFFFULL);
	uint64_t testval = csr_read(CSR_PMPADDR_BASE);
	csr_write(CSR_PMPADDR_BASE, 0);

	if (testval == 0) {
		caps->num_pmp_rules = 0;
		return;
	}

	/* Use compile-time macro expansion to probe all 64 possible PMP regions */
	__check_pmp_csr_64(num_pmp, hs, testval);

__pmp_done:
	caps->num_pmp_rules = num_pmp;

	/* This is a best effort check for Smepmp, try setting mseccfg.RLB and
	 * see if it sticks. Note that implementations are allowed to hardcode it
	 * to zero and still implement mseccfg.MML/MMAP */
	csr_set_bits(CSR_MSECCFG, CSR_MSECCFG_RLB);
	testval = csr_read(CSR_MSECCFG);
	if (testval & CSR_MSECCFG_RLB) {
		/* Since we haven't added any rules with L bit set, we can
		 * clear it back without locking it to zero. */
		caps->m_caps |= CAP_SMEPMP;
		csr_clear_bits(CSR_MSECCFG, CSR_MSECCFG_RLB);
	}
}

static void
hart_probe_smctr(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MCTRCTL, caps->m_caps, CAP_SMCTR);
}

static void
hart_probe_smaia(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MTOPEI, caps->m_caps, CAP_SMAIA);
}

/*********************\
* S-mode capabilities *
\*********************/

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
	hart_probe_cap_by_csr_existence(CSR_SCOUNTINHIBIT, caps->s_caps, CAP_SSCCFG);
}

static void
hart_probe_ssaia(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_STOPEI, caps->s_caps, CAP_SSAIA);
}

/*********************\
* U-mode capabilities *
\*********************/

static void
hart_probe_ube(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MSTATUS, CSR_MSTATUS_UBE, caps->r_caps, CAP_UBE);
}

static void
hart_probe_zicntr(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_CYCLE, caps->r_caps, CAP_ZICNTR);
}

static void
hart_probe_zihpm(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_HPMCOUNTER3, caps->r_caps, CAP_ZIHPM);
}

/* Macros to count HPM counters at compile time (like PMP probing) */
#define __check_hpm_csr(__n, __hpm_count, __hs)				\
do {										\
	__hs->error = 0;							\
	uint64_t __orig = csr_read(CSR_MHPMCOUNTER(__n));			\
	if (__hs->error != 0)							\
		goto __hpm_done;						\
	csr_write(CSR_MHPMCOUNTER(__n), 1ULL);					\
	uint64_t __test = csr_read(CSR_MHPMCOUNTER(__n));			\
	csr_write(CSR_MHPMCOUNTER(__n), __orig);				\
	if (__test == 1ULL)							\
		__hpm_count = (__n) - 2;  /* HPM counters start at 3, so count = n-2 */ \
	else									\
		goto __hpm_done;						\
} while (0)

#define __check_hpm_csr_2(__c, __h) \
	__check_hpm_csr(3, __c, __h); __check_hpm_csr(4, __c, __h)

#define __check_hpm_csr_4(__c, __h) \
	__check_hpm_csr_2(__c, __h); __check_hpm_csr(5, __c, __h); __check_hpm_csr(6, __c, __h)

#define __check_hpm_csr_8(__c, __h) \
	__check_hpm_csr_4(__c, __h); __check_hpm_csr(7, __c, __h); __check_hpm_csr(8, __c, __h); \
	__check_hpm_csr(9, __c, __h); __check_hpm_csr(10, __c, __h)

#define __check_hpm_csr_16(__c, __h) \
	__check_hpm_csr_8(__c, __h); __check_hpm_csr(11, __c, __h); __check_hpm_csr(12, __c, __h); \
	__check_hpm_csr(13, __c, __h); __check_hpm_csr(14, __c, __h); __check_hpm_csr(15, __c, __h); \
	__check_hpm_csr(16, __c, __h); __check_hpm_csr(17, __c, __h); __check_hpm_csr(18, __c, __h)

#define __check_hpm_csr_29(__c, __h) \
	__check_hpm_csr_16(__c, __h); __check_hpm_csr(19, __c, __h); __check_hpm_csr(20, __c, __h); \
	__check_hpm_csr(21, __c, __h); __check_hpm_csr(22, __c, __h); __check_hpm_csr(23, __c, __h); \
	__check_hpm_csr(24, __c, __h); __check_hpm_csr(25, __c, __h); __check_hpm_csr(26, __c, __h); \
	__check_hpm_csr(27, __c, __h); __check_hpm_csr(28, __c, __h); __check_hpm_csr(29, __c, __h); \
	__check_hpm_csr(30, __c, __h); __check_hpm_csr(31, __c, __h)

/* Count the number of implemented HPM counters by testing which
 * mhpmcounter3-31 CSRs are writable. Following OpenSBI's approach. */
static void
hart_count_hpm(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint8_t count = 0;

	/* Use compile-time macro expansion to probe all 29 possible HPM counters */
	__check_hpm_csr_29(count, hs);

__hpm_done:
	caps->num_hpmcounters = count;
}

/* Get VLEN in bytes and store as a shift value to save space.
 * vlenb = 1 << vlenb_shift, where vlenb_shift is in range [4-13]. */
static void
hart_probe_vlenb(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	uint64_t vlenb = csr_read(CSR_VLENB);

	/* VLEN must be a power of 2, so vlenb must also be a power of 2.
	 * Store the shift value (4-13 for valid VLEN 128-65536 bits).
	 * Find the position of the single set bit. (TODO: uze clz if
	 * compiled with zbb) */
	uint8_t shift = 0;
	while (vlenb > 1) {
		vlenb >>= 1;
		shift++;
	}
	caps->vlenb_shift = shift;
}


/********************************************\
* Common capabilities across privilege modes *
\********************************************/

static void
hart_probe_zicfilp(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MSECCFG, CSR_MSECCFG_MLPE, caps->z_caps, CAP_ZICFILP);
}

static void
hart_probe_zicbom(struct hart_state *hs)
{
	/* TODO: Check without MENVCFG */
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_field(CSR_MENVCFG, CSR_MENVCFG_CBIE, 1, caps->z_caps, CAP_ZICBOM);
}

static void
hart_probe_zicboz(struct hart_state *hs)
{
	/* TODO: Check without MENVCFG */
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_CBZE, caps->z_caps, CAP_ZICBOZ);
}

static void
hart_probe_zicfiss(struct hart_state *hs)
{
	/* Note: sub-M modes only for now */
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MENVCFG, CSR_MENVCFG_SSE, caps->z_caps, CAP_ZICFISS);
}

static void
hart_probe_zkr(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_bit(CSR_MSECCFG, CSR_MSECCFG_SSEED, caps->z_caps, CAP_ZKR);
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


/**************\
* Entry points *
\**************/

void
hart_probe_priv_caps(struct rvcaps *caps)
{
	memset(caps, 0, sizeof(struct rvcaps));

	/* Instead of passing rvcaps as provided
	 * by the user, pass it via hs->caps so that
	 * we can also retrieve hs for free and save
	 * a few function arguments and extra code
	 * along the way. We'll keep a copy of
	 * hs->early_caps and restore it when done. */
	struct hart_state *hs = hart_get_hstate_self();
	uint64_t saved_early_caps = hs->early_caps;
	hs->caps = caps;

	uint64_t misa = csr_read(CSR_MISA);

	/* Probe MISA bits */
	if (misa & CSR_MISA_A)
		caps->r_caps |= CAP_A;
	if (misa & CSR_MISA_B)
		caps->r_caps |= CAP_B;
	if (misa & CSR_MISA_C)
		caps->r_caps |= CAP_C;
	if (misa & CSR_MISA_D)
		caps->r_caps |= CAP_D;
	if (misa & CSR_MISA_F)
		caps->r_caps |= CAP_F;
	if (misa & CSR_MISA_M)
		caps->r_caps |= CAP_M;
	if (misa & CSR_MISA_Q)
		caps->r_caps |= CAP_Q;
	if (misa & CSR_MISA_U)
		caps->r_caps |= CAP_U;
	if (misa & CSR_MISA_V) {
		caps->r_caps |= CAP_V;
		hart_probe_vlenb(hs);
	}
	if (misa & CSR_MISA_X)
		caps->r_caps |= CAP_X;

	/* Probe M-mode caps, on top of existing
	 * early_caps */
	caps->m_caps = (uint32_t) saved_early_caps;
	hart_probe_smdbltrp(hs);
	hart_probe_mbe(hs);
	hart_probe_smmpm(hs);
	hart_probe_smnpm(hs);
	hart_probe_smstateen(hs);
	hart_probe_smcsrind(hs);
	hart_probe_smrnmi(hs);
	hart_probe_smctr(hs);
	hart_probe_pmp(hs);
	hart_probe_smaia(hs);

	/* Probe Z* extensions with CSRs mentioned
	 * in the priv. spec. */
	hart_probe_zicfilp(hs);
	hart_probe_zicbom(hs);
	hart_probe_zicboz(hs);
	hart_probe_zicfiss(hs);
	hart_probe_zkr(hs);

	if (misa & CSR_MISA_U) {
		hart_probe_ube(hs);
		hart_probe_zicntr(hs);
		hart_probe_zihpm(hs);
		hart_count_hpm(hs);

		if (misa & CSR_MISA_S) {
			caps->s_caps |= CAP_S;
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

			/* Probe virtual memory extensions */
			hart_probe_sv39(hs);

			/* If Sv39 exists, check for NAPOT and higher modes */
			if (caps->s_caps & CAP_SV39) {
				hart_probe_sv39_napot(hs);
				hart_probe_sv48(hs);
				hart_probe_sv57(hs);
			}

			/* Check for H extension */
			if (misa & CSR_MISA_H)
				caps->s_caps |= CAP_H;
		}
	}

	/* Restore early_caps */
	hs->early_caps = saved_early_caps;
}