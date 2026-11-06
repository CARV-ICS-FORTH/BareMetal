/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Privileged hart-level probe engine + U-mode / Z* detection + the cached
 * accessor used by the menu.
 *
 * M-mode and S-mode (incl. virtual memory) detection live in hart_probe_mmode.c
 * and hart_probe_smode.c; the name tables, printers and menu entries live in
 * hart_priv_probes.c. The engine runs once, lazily, via priv_get_caps() (its
 * only consumer is the report/menu layer).
 */

#include "hart_priv_caps.h"
#include <target_config.h>		/* For PLAT_QUIRK_* (vlenb) */
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/utils/utils.h>	/* For ANN()/INF() */
#include <stddef.h>			/* For size_t */
#include <stdbool.h>			/* For bool */
#include <stdint.h>			/* For typed ints */
#include <string.h>			/* For memset() */

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
 * vlenb = 1 << vlenb_shift, where vlenb_shift is in range [4-13];
 * vlenb_shift == 0 means "no / unknown VLEN". */
static void
hart_probe_vlenb(struct hart_state *hs)
{
	struct rvcaps *caps = hs->caps;

	/* VLENB (CSR 0xc22) is a ratified-RVV register. A core that advertises
	 * misa.V but implements a non-compliant vector (e.g. the T-Head 0.7.1
	 * draft) does not have it: the read would trap, get skipped, and leave
	 * a garbage VLEN. Mark VLEN unknown (shift 0) and don't probe. */
#if defined(PLAT_QUIRK_NON_COMPLIANT_RVV)
	caps->vlenb_shift = 0;
	return;
#else
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
#endif
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


/**************\
* Probe engine *
\**************/

static void
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

	/* Seed m_caps with the caps early boot already detected for free (and more
	 * reliably, by exercising the CSRs): MCOUNTINHIBIT, SMCNTRPMF, SDTRIG and
	 * SMEPMP. Those are not re-probed; the rest are probed on top. */
	caps->m_caps = (uint32_t) saved_early_caps;
	hart_probe_mmode(hs);

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
			hart_probe_smode(hs);

			/* Check for H extension */
			if (misa & CSR_MISA_H)
				caps->s_caps |= CAP_H;
		}
	}

	/* Restore early_caps */
	hs->early_caps = saved_early_caps;
}

/* priv_get_caps() probes *all* privileged caps on its first call and returns the
 * cached result on every call after that. The probe is one interdependent pass
 * with side effects (PMP/MMU config, page-table alloc/free) and its results are
 * static hardware facts, so it must run at most once -- lazily, on first use,
 * never eagerly in main(). The report/menu layer calls this and renders slices
 * of the cached result. */
static struct rvcaps g_priv_caps;
static bool g_priv_caps_probed;

struct rvcaps *
priv_get_caps(void)
{
	if (!g_priv_caps_probed) {
		ANN("\n---=== Probing RISC-V Capabilities ===---\n");
		INF("Running capability detection...\n");
		hart_probe_priv_caps(&g_priv_caps);
		g_priv_caps_probed = true;
	}
	return &g_priv_caps;
}
