/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Presentation layer for the privileged hart probes: capability-name tables, the
 * section printers, and the per-category menu entries.
 *
 * These are probes, not tests (see test_framework.h): each entry just pulls the
 * (cached) result from priv_get_caps() -- the one-shot probe in hart_priv_caps.c
 * -- and renders its slice. The split mirrors the rvcaps fields (m_caps, s_caps,
 * r_caps, z_caps + the counts). "Run all" reuses the single cached probe.
 */

#include "hart_priv_caps.h"		/* For priv_get_caps(), struct rvcaps, CAP_* */
#include <platform/utils/utils.h>	/* For ANN()/INF() */
#include <test_framework.h>		/* For REGISTER_HART_PRIV_PROBE */
#include <stddef.h>			/* For size_t */
#include <stdint.h>			/* For typed ints */
#include <stdbool.h>			/* For bool */

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

struct cap_to_str {
	uint32_t cap;
	const char* name;
};

struct cap_to_str_64 {
	uint64_t cap;
	const char* name;
};

/* M-mode capabilities */
static const struct cap_to_str m_cap_names[] = {
	{CAP_MCOUNTINHIBIT, "Mcountinhibit (Priv. spec. 1.11)"},
	{CAP_SMDBLTRP, "Smdbltrp (double trap)"},
	{CAP_SMMPM, "Smmpm (pointer masking)"},
	{CAP_SMNPM, "Smnpm (pointer masking next mode)"},
	{CAP_SMSTATEEN, "Smstateen (state enable)"},
	{CAP_SMCSRIND, "Smcsrind (indirect CSR access)"},
	{CAP_SMEPMP, "Smepmp (enhanced PMP)"},
	{CAP_SMCNTRPMF, "Smcntrpmf (counter privilege mode filtering)"},
	{CAP_SMRNMI, "Smrnmi (resumable NMI)"},
	{CAP_SMCTR, "Smctr (control transfer records)"},
	{CAP_SDTRIG, "Sdtrig (debug triggers)"},
	{CAP_SMAIA, "Smaia (advanced interrupt architecture)"},
	{CAP_IMSIC, "IMSIC (incoming MSI controller)"}
};

/* S-mode capabilities */
static const struct cap_to_str s_cap_names[] = {
	{CAP_SSDBLTRP, "Ssdbltrp (double trap)"},
	{CAP_SVPBMT, "Svpbmt (page-based memory types)"},
	{CAP_SMCDELEG, "Smcdeleg (counter delegation)"},
	{CAP_SSTC, "Sstc (timer compare)"},
	{CAP_SSNPM, "Ssnpm (pointer masking)"},
	{CAP_SSSTATEEN, "Ssstateen (state enable)"},
	{CAP_SSCSRIND, "Sscsrind (indirect CSR access)"},
	{CAP_SV32, "Sv32 (virtual memory)"},
	{CAP_SV39, "Sv39 (virtual memory)"},
	{CAP_SV48, "Sv48 (virtual memory)"},
	{CAP_SV57, "Sv57 (virtual memory)"},
	{CAP_SVNAPOT, "Svnapot (naturally aligned power-of-2)"},
	{CAP_SVINVAL, "Svinval (fine-grained TLB invalidation)"},
	{CAP_SVADU, "Svadu (hardware A/D bit updates)"},
	{CAP_SSQOSID, "Ssqosid (QoS identifiers)"},
	{CAP_SSCOFPMF, "Sscofpmf (counter overflow)"},
	{CAP_SSCTR, "Ssctr (control transfer records)"},
	{CAP_SSCCFG, "Ssccfg (counter configuration)"},
	{CAP_SSAIA, "Ssaia (advanced interrupt architecture)"}
};

/* MISA Extensions */
static const struct cap_to_str misa_ext_names[] = {
	{CAP_A, "A (atomics)"},
	{CAP_B, "B (bit manipulation)"},
	{CAP_C, "C (compressed)"},
	{CAP_D, "D (double-precision FP)"},
	{CAP_F, "F (single-precision FP)"},
	{CAP_M, "M (multiply/divide)"},
	{CAP_Q, "Q (quad-precision FP)"},
	{CAP_V, "V (vector)"},
	{CAP_X, "X (non-standard extensions)"}
};

/* U-mode extensions */
static const struct cap_to_str u_ext_names[] = {
	{CAP_ZICNTR, "Zicntr (base counters)"},
	{CAP_ZIHPM, "Zihpm (HPM counters)"}
};

/* Z* (common) capabilities */
static const struct cap_to_str_64 z_cap_names[] = {
	{CAP_ZICFILP, "Zicfilp (landing pads)"},
	{CAP_ZICFISS, "Zicfiss (shadow stack)"},
	{CAP_ZICBOM, "Zicbom (cache block management)"},
	{CAP_ZICBOZ, "Zicboz (cache block zero)"},
	{CAP_ZKR, "Zkr (entropy source)"}
};

static void
print_caps_section(const char *section_name, uint32_t caps,
                   const struct cap_to_str *cap_list, size_t num_caps)
{
	size_t i;
	bool found = false;

	for (i = 0; i < num_caps; i++) {
		if (caps & cap_list[i].cap) {
			if (!found) {
				INF("\n%s:\n", section_name);
				found = true;
			}
			INF("  %s\n", cap_list[i].name);
		}
	}

	if (!found)
		INF("\n%s: None\n", section_name);
}

static void
print_caps_section_64(const char *section_name, uint64_t caps,
                      const struct cap_to_str_64 *cap_list, size_t num_caps)
{
	size_t i;
	bool found = false;

	for (i = 0; i < num_caps; i++) {
		if (caps & cap_list[i].cap) {
			if (!found) {
				INF("\n%s:\n", section_name);
				found = true;
			}
			INF("  %s\n", cap_list[i].name);
		}
	}

	if (!found)
		INF("\n%s: None\n", section_name);
}

static void
print_priv_modes(struct rvcaps *caps)
{
	INF("\nPrivilege Modes:\n");
	if (caps->r_caps & CAP_U)
		INF("  U-mode (User)\n");
	if (caps->s_caps & CAP_S)
		INF("  S-mode (Supervisor)\n");
	if (caps->s_caps & CAP_H)
		INF("  H-mode (Hypervisor)\n");
}

static void
print_big_endian_support(struct rvcaps *caps)
{
	bool has_any = false;

	INF("\nBig Endian Support:\n");
	if (caps->r_caps & CAP_UBE) {
		INF("  U-mode\n");
		has_any = true;
	}
	if (caps->s_caps & CAP_SBE) {
		INF("  S-mode\n");
		has_any = true;
	}
	if (caps->m_caps & CAP_MBE) {
		INF("  M-mode\n");
		has_any = true;
	}

	if (!has_any)
		INF("  None\n");
}

static int
priv_probe_modes(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_priv_modes(caps);
	print_big_endian_support(caps);
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("Privilege modes & endianness", priv_probe_modes);

static int
priv_probe_misa(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_caps_section("MISA Extensions", caps->r_caps,
	                   misa_ext_names, ARRAY_SIZE(misa_ext_names));
	if (caps->vlenb_shift > 0) {
		uint16_t vlenb = 1 << caps->vlenb_shift;
		INF("\nVector Length (VLEN): %u bits (%u bytes)\n", vlenb * 8, vlenb);
	}
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("Base ISA (MISA) extensions", priv_probe_misa);

static int
priv_probe_m_caps(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_caps_section("M-mode Extensions", caps->m_caps,
	                   m_cap_names, ARRAY_SIZE(m_cap_names));
	if (caps->num_pmp_rules > 0) {
		INF("\nPMP Rules: %u\n", caps->num_pmp_rules);
		INF("PMP Granularity: %lu bytes%s\n",
		    1UL << caps->pmp_granule_shift,
		    caps->pmp_granule_shift == 2 ? " (NA4 supported)" : "");
	}
	if (caps->num_triggers > 0)
		INF("Debug Triggers: %u\n", caps->num_triggers);
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("M-mode extensions", priv_probe_m_caps);

static int
priv_probe_s_caps(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_caps_section("S-mode Extensions", caps->s_caps,
	                   s_cap_names, ARRAY_SIZE(s_cap_names));
	if (caps->num_asid_bits > 0)
		INF("\nASID bits: %u\n", caps->num_asid_bits);
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("S-mode extensions", priv_probe_s_caps);

static int
priv_probe_u_caps(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_caps_section("U-mode Extensions", caps->r_caps,
	                   u_ext_names, ARRAY_SIZE(u_ext_names));
	if (caps->num_hpmcounters > 0)
		INF("\nHardware Performance Counters: %u\n", caps->num_hpmcounters);
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("U-mode extensions & counters", priv_probe_u_caps);

static int
priv_probe_z_caps(void)
{
	struct rvcaps *caps = priv_get_caps();
	print_caps_section_64("Common Extensions (Z*)", caps->z_caps,
	                      z_cap_names, ARRAY_SIZE(z_cap_names));
	INF("\nPress a key to continue...\n");
	return 0;
}
REGISTER_HART_PRIV_PROBE("Common (Z*) extensions", priv_probe_z_caps);
