/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * M-mode privileged capability probing.
 *
 * Detects M-mode extensions by poking at M-mode CSRs (the platform trap handler
 * flags unsupported accesses via hs->error) and counts PMP entries/granularity
 * and Sdtrig triggers. MCOUNTINHIBIT/SMCNTRPMF/SDTRIG/SMEPMP are not probed here:
 * early boot detects them for free into hart_state.early_caps, and the engine
 * seeds m_caps from there (see hart_priv_caps.c).
 */

#include "hart_priv_caps.h"
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <stdint.h>			/* For typed ints */
#include <stdbool.h>			/* For bool */

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

	/* PMP granularity (priv. spec 3.7): with pmpcfg0=0 (set above), the all-ones
	 * write to pmpaddr0 reads back with its least-significant set bit at index G,
	 * and the granularity is 2^(G+2) bytes. G==0 (4-byte granule) means NA4 is
	 * supported. We reuse 'testval' read-only here, so the count below is
	 * unaffected. (TODO: use ctz if compiled with Zbb.) */
	uint8_t g = 0;
	while (!((testval >> g) & 1))
		g++;
	caps->pmp_granule_shift = g + 2;

	/* Use compile-time macro expansion to probe all 64 possible PMP regions */
	__check_pmp_csr_64(num_pmp, hs, testval);

__pmp_done:
	caps->num_pmp_rules = num_pmp;

	/* Smepmp (CAP_SMEPMP) is detected for free during hart_init_pmp() and
	 * arrives via early_caps (already copied into m_caps). We don't re-check
	 * mseccfg.RLB here: by now our own locked PMP rule is installed, so RLB
	 * can't go 0->1 and the test would yield a false negative. */
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
	/* Probe mtopi (AIA-core), not mtopei (IMSIC-only), so Smaia is detected on
	 * harts with a wired APLIC but no IMSIC too. */
	hart_probe_cap_by_csr_existence(CSR_MTOPI, caps->m_caps, CAP_SMAIA);
}

/* IMSIC is not a separate ISA extension -- it's the AIA's MSI-controller
 * hardware component. Its presence shows up as the IMSIC CSR interface (mtopei,
 * the M-mode interrupt file), which exists only when an IMSIC is present,
 * whereas mtopi (Smaia, above) exists regardless. So this is a distinct,
 * finer-grained capability. */
static void
hart_probe_imsic(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;
	hart_probe_cap_by_csr_existence(CSR_MTOPEI, caps->m_caps, CAP_IMSIC);
}

/* Count Sdtrig hardware triggers. tselect is WARL: writing an out-of-range index
 * reads back a different value, so walk indices until the write stops sticking.
 * Non-destructive -- only tselect is touched, never the trigger data CSRs. The
 * Sdtrig cap itself comes from early_caps (hart_init_sdtrig); we only quantify
 * it here, alongside the other "how many" probes (PMP entries, HPM counters). */
static void
hart_count_triggers(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	if (!(caps->m_caps & CAP_SDTRIG))
		return;

	uint32_t count = 0;
	while (count < 255) {		/* defensive bound; real counts are tiny */
		csr_write(CSR_TSELECT, count);
		if (csr_read(CSR_TSELECT) != count)
			break;
		count++;
	}
	caps->num_triggers = (uint8_t)count;
}

void
hart_probe_mmode(struct hart_state *hs)
{
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
	hart_probe_imsic(hs);
	hart_count_triggers(hs);
}
