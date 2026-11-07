/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * U-mode and common Z* extensions (REGISTER_HART_PRIV_Z). These overlap -- most Z*
 * are unprivileged and user-visible -- so they share one group. The base-ISA (MISA)
 * letters are reported by the M-mode "Base ISA" unit; here we cover the CSR-probed
 * ones (counters, cache-block ops, pointer/CFI, entropy).
 */

#include "hart_priv_caps.h"
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/utils/utils.h>	/* For INF() */
#include <test_framework.h>		/* For REGISTER_HART_PRIV_Z + PROBE_* */
#include <stdint.h>

static int
hart_probe_zicntr(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_exists(CSR_CYCLE);
}
REGISTER_HART_PRIV_Z("Zicntr (base counters)", hart_probe_zicntr);

/* Macros to count writable mhpmcounter3-31 at compile time (constant CSR #). */
#define __check_hpm_csr(__n, __count, __hs)				\
do {									\
	__hs->error = 0;						\
	uint64_t __orig = csr_read(CSR_MHPMCOUNTER(__n));		\
	if (__hs->error != 0)						\
		goto __hpm_done;					\
	csr_write(CSR_MHPMCOUNTER(__n), 1ULL);				\
	uint64_t __test = csr_read(CSR_MHPMCOUNTER(__n));		\
	csr_write(CSR_MHPMCOUNTER(__n), __orig);			\
	if (__test == 1ULL)						\
		__count = (__n) - 2;	/* HPM counters start at 3 */	\
	else								\
		goto __hpm_done;					\
} while (0)

/* Count implemented HPM counters (mhpmcounter3-31), like OpenSBI does. */
static uint8_t
hpm_count(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	uint8_t count = 0;

	__check_hpm_csr(3, count, hs);  __check_hpm_csr(4, count, hs);
	__check_hpm_csr(5, count, hs);  __check_hpm_csr(6, count, hs);
	__check_hpm_csr(7, count, hs);  __check_hpm_csr(8, count, hs);
	__check_hpm_csr(9, count, hs);  __check_hpm_csr(10, count, hs);
	__check_hpm_csr(11, count, hs); __check_hpm_csr(12, count, hs);
	__check_hpm_csr(13, count, hs); __check_hpm_csr(14, count, hs);
	__check_hpm_csr(15, count, hs); __check_hpm_csr(16, count, hs);
	__check_hpm_csr(17, count, hs); __check_hpm_csr(18, count, hs);
	__check_hpm_csr(19, count, hs); __check_hpm_csr(20, count, hs);
	__check_hpm_csr(21, count, hs); __check_hpm_csr(22, count, hs);
	__check_hpm_csr(23, count, hs); __check_hpm_csr(24, count, hs);
	__check_hpm_csr(25, count, hs); __check_hpm_csr(26, count, hs);
	__check_hpm_csr(27, count, hs); __check_hpm_csr(28, count, hs);
	__check_hpm_csr(29, count, hs); __check_hpm_csr(30, count, hs);
	__check_hpm_csr(31, count, hs);
__hpm_done:
	return count;
}

static int
hart_probe_zihpm(unsigned flags)
{
	if (hart_probe_csr_exists(CSR_HPMCOUNTER3) < 0)
		return PROBE_ABSENT;
	if (flags & PROBE_INFO)
		INF("\t%u hardware performance counters\n", hpm_count());
	return PROBE_INFO;
}
REGISTER_HART_PRIV_Z("Zihpm (HPM counters)", hart_probe_zihpm);

static int
hart_probe_zicfilp(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MSECCFG, CSR_MSECCFG_MLPE);
}
REGISTER_HART_PRIV_Z("Zicfilp (landing pads)", hart_probe_zicfilp);

static int
hart_probe_zicfiss(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_SSE);
}
REGISTER_HART_PRIV_Z("Zicfiss (shadow stack)", hart_probe_zicfiss);

static int
hart_probe_zicbom(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_field(CSR_MENVCFG, CSR_MENVCFG_CBIE, 1);
}
REGISTER_HART_PRIV_Z("Zicbom (cache block management)", hart_probe_zicbom);

static int
hart_probe_zicboz(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MENVCFG, CSR_MENVCFG_CBZE);
}
REGISTER_HART_PRIV_Z("Zicboz (cache block zero)", hart_probe_zicboz);

static int
hart_probe_zkr(unsigned flags)
{
	(void)flags;
	return hart_probe_csr_bit(CSR_MSECCFG, CSR_MSECCFG_SSEED);
}
REGISTER_HART_PRIV_Z("Zkr (entropy source)", hart_probe_zkr);
