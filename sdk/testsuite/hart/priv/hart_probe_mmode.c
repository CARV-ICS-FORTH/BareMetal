/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * M-mode privileged units (REGISTER_HART_PRIV_M). Each is a live probe: it pokes an
 * M-mode CSR (the trap handler flags unsupported accesses via hs->error) and returns
 * its presence/caps. MCOUNTINHIBIT/SMCNTRPMF/SDTRIG/SMEPMP can't be re-probed after
 * boot, so those units read the caps early boot stashed in hart_state.early_caps.
 */

#include "hart_priv_caps.h"
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/riscv/early_caps.h>	/* For CAP_* stashed by early boot */
#include <platform/utils/utils.h>	/* For INF() */
#include <test_framework.h>		/* For REGISTER_HART_PRIV_M + PROBE_* */
#include <stdint.h>

/* Report a cap early boot already detected into hart_state.early_caps. */
static int
early_cap(uint64_t bit)
{
	return (hart_get_hstate_self()->early_caps & bit) ? 0 : PROBE_ABSENT;
}

/*
 * Base ISA (MISA), privilege modes and endianness -- a hart-wide overview. Named to
 * sort first in the M-mode list ("0misa" < "probe_*"). Always present; its detail
 * prints under PROBE_INFO.
 */
static int
hart_probe_0misa(unsigned flags)
{
	if (!(flags & PROBE_INFO))
		return PROBE_INFO;

	uint64_t misa = csr_read(CSR_MISA);
	uint64_t mstatus = csr_read(CSR_MSTATUS);

	INF("\tMISA:");
	for (int i = 0; i < 26; i++)
		if (misa & (1ull << i))
			INF(" %c", 'A' + i);
	INF("\n");
	INF("\tModes: M%s%s\n",
	    (misa & (1ull << ('S' - 'A'))) ? " S" : "",
	    (misa & (1ull << ('U' - 'A'))) ? " U" : "");
	INF("\tM-mode endianness: %s\n",
	    (mstatus & CSR_MSTATUS_MBE) ? "big" : "little");
	return 0;
}
REGISTER_HART_PRIV_M("Base ISA (MISA), modes & endianness", hart_probe_0misa);

static int
hart_probe_mcountinhibit(unsigned flags)
{
	(void)flags;
	return early_cap(CAP_MCOUNTINHIBIT);
}
REGISTER_HART_PRIV_M("mcountinhibit (counter inhibit)", hart_probe_mcountinhibit);

static int
hart_probe_smcntrpmf(unsigned flags)
{
	(void)flags;
	return early_cap(CAP_SMCNTRPMF);
}
REGISTER_HART_PRIV_M("Smcntrpmf (counter privilege mode filtering)", hart_probe_smcntrpmf);

static int
hart_probe_smepmp(unsigned flags)
{
	(void)flags;
	return early_cap(CAP_SMEPMP);
}
REGISTER_HART_PRIV_M("Smepmp (enhanced PMP)", hart_probe_smepmp);

/* Count Sdtrig triggers: tselect is WARL, so walk indices until a write stops
 * sticking. Non-destructive (only tselect is touched). */
static unsigned
count_triggers(void)
{
	unsigned n = 0;

	while (n < 255) {			/* defensive bound; real counts are tiny */
		csr_write(CSR_TSELECT, n);
		if (csr_read(CSR_TSELECT) != n)
			break;
		n++;
	}
	return n;
}

static int
hart_probe_sdtrig(unsigned flags)
{
	if (early_cap(CAP_SDTRIG) < 0)
		return PROBE_ABSENT;
	if (flags & PROBE_INFO)
		INF("\t%u triggers\n", count_triggers());
	return PROBE_INFO;
}
REGISTER_HART_PRIV_M("Sdtrig (debug triggers)", hart_probe_sdtrig);

static int
hart_probe_smdbltrp(unsigned flags)
{
	(void)flags;			/* MDT sticking also clears MIE -- harmless here */
	return hart_probe_csr_bit(CSR_MSTATUS, CSR_MSTATUS_MDT);
}
REGISTER_HART_PRIV_M("Smdbltrp (double trap)", hart_probe_smdbltrp);

static int
hart_probe_smmpm(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_field(CSR_MSECCFG, CSR_MSECCFG_PMM, 2);
}
REGISTER_HART_PRIV_M("Smmpm (pointer masking)", hart_probe_smmpm);

static int
hart_probe_smnpm(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_field(CSR_MENVCFG, CSR_MENVCFG_PMM, 2);
}
REGISTER_HART_PRIV_M("Smnpm (pointer masking next mode)", hart_probe_smnpm);

static int
hart_probe_smstateen(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MSTATEEN0);
}
REGISTER_HART_PRIV_M("Smstateen (state enable)", hart_probe_smstateen);

static int
hart_probe_smcsrind(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MISELECT);
}
REGISTER_HART_PRIV_M("Smcsrind (indirect CSR access)", hart_probe_smcsrind);

static int
hart_probe_smrnmi(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MNSCRATCH);
}
REGISTER_HART_PRIV_M("Smrnmi (resumable NMI)", hart_probe_smrnmi);

static int
hart_probe_smctr(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MCTRCTL);
}
REGISTER_HART_PRIV_M("Smctr (control transfer records)", hart_probe_smctr);

/* Macros to probe the 64 possible PMP regions at compile time (constant CSR #). */
#define __check_pmp_csr(__n, __count, __hs, __testval)			\
do {									\
	__hs->error = 0;						\
	uint64_t __orig = csr_read(CSR_PMPADDR_BASE + __n);		\
	if (__hs->error != 0)						\
		goto __pmp_done;					\
	csr_write(CSR_PMPADDR_BASE + __n, __testval);			\
	uint64_t __test = csr_read(CSR_PMPADDR_BASE + __n);		\
	csr_write(CSR_PMPADDR_BASE + __n, __orig);			\
	if (__test != 0)						\
		__count = __n + 1;					\
	else								\
		goto __pmp_done;					\
} while (0)

#define __check_pmp_csr_2(__b, __c, __h, __t)	__check_pmp_csr(__b + 0, __c, __h, __t); __check_pmp_csr(__b + 1, __c, __h, __t)
#define __check_pmp_csr_4(__b, __c, __h, __t)	__check_pmp_csr_2(__b + 0, __c, __h, __t); __check_pmp_csr_2(__b + 2, __c, __h, __t)
#define __check_pmp_csr_8(__b, __c, __h, __t)	__check_pmp_csr_4(__b + 0, __c, __h, __t); __check_pmp_csr_4(__b + 4, __c, __h, __t)
#define __check_pmp_csr_16(__b, __c, __h, __t)	__check_pmp_csr_8(__b + 0, __c, __h, __t); __check_pmp_csr_8(__b + 8, __c, __h, __t)
#define __check_pmp_csr_32(__b, __c, __h, __t)	__check_pmp_csr_16(__b + 0, __c, __h, __t); __check_pmp_csr_16(__b + 16, __c, __h, __t)
#define __check_pmp_csr_64(__c, __h, __t)	__check_pmp_csr_32(0, __c, __h, __t); __check_pmp_csr_32(32, __c, __h, __t)

/* Detect PMP and count regions + granule. Returns true if present. */
static bool
pmp_detect(uint8_t *num, uint8_t *granule_shift)
{
	struct hart_state *hs = hart_get_hstate_self();
	uint8_t count = 0, g = 0;

	hs->error = 0;
	csr_write(CSR_PMPCFG_BASE, 0);
	if (hs->error != 0)
		return false;

	csr_write(CSR_PMPADDR_BASE, 0xFFFFFFFFFFFFFFFFULL);
	uint64_t testval = csr_read(CSR_PMPADDR_BASE);
	csr_write(CSR_PMPADDR_BASE, 0);
	if (testval == 0)
		return false;

	/* Granularity (priv spec 3.7): lowest set bit of the all-ones readback is G,
	 * granule is 2^(G+2) bytes (G==0 => 4-byte, NA4 supported). */
	while (!((testval >> g) & 1))
		g++;
	*granule_shift = g + 2;

	__check_pmp_csr_64(count, hs, testval);
__pmp_done:
	*num = count;
	return true;
}

static int
hart_probe_pmp(unsigned flags)
{
	uint8_t num, granule_shift;

	if (!pmp_detect(&num, &granule_shift))
		return PROBE_ABSENT;
	if (flags & PROBE_INFO)
		INF("\t%u regions, %lu-byte granule\n", num, 1ul << granule_shift);
	return PROBE_INFO;
}
REGISTER_HART_PRIV_M("PMP (physical memory protection)", hart_probe_pmp);

/* Probe mtopi (AIA core), present with a wired APLIC even without an IMSIC. */
static int
hart_probe_smaia(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MTOPI);
}
REGISTER_HART_PRIV_M("Smaia (advanced interrupt architecture)", hart_probe_smaia);

/* mtopei (the M-mode interrupt file) exists only when an IMSIC is present. */
static int
hart_probe_imsic(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_MTOPEI);
}
REGISTER_HART_PRIV_M("IMSIC (incoming MSI controller)", hart_probe_imsic);
