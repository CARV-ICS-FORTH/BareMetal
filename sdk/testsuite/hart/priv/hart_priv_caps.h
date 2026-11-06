/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Shared internals for the privileged hart probes, split across:
 *   hart_probe_mmode.c  - M-mode detection       (hart_probe_mmode)
 *   hart_probe_smode.c  - S-mode + VM detection  (hart_probe_smode)
 *   hart_priv_caps.c    - U/Z detection + engine (hart_probe_priv_caps) + accessor
 *   hart_priv_probes.c  - name tables, printers, and the menu entries
 */

#ifndef HART_PRIV_CAPS_H
#define HART_PRIV_CAPS_H

#include <platform/riscv/hart.h>	/* For struct hart_state + CSR ops */
#include <caps.h>			/* For struct rvcaps, CAP_* (+ FIELD_* via bitfield) */

/*********\
* Helpers *
\*********/

/* The probe helpers assume a `struct hart_state *hs` is in scope (the probe
 * functions take it as their argument); they key off hs->error, which the
 * platform trap handler sets when a CSR access is unsupported. */

/* Probe for a capability by testing if a CSR exists.
 * Sets the capability flag if the CSR is accessible. */
#define hart_probe_cap_by_csr_existence(csr_addr, caps_field, cap_flag)	\
do {									\
	hs->error = 0;							\
	(void)csr_read(csr_addr);					\
	if (hs->error == 0)						\
		caps_field |= cap_flag;					\
} while (0)

/* Probe for a capability by testing if a single-bit CSR field sticks
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

/* Probe for a capability by testing if a multi-bit CSR field sticks when
 * written. Sets the capability flag if the field is supported. test_val should
 * be a non-zero value appropriate for the field (e.g., 2 for PMM fields).*/
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

/*************\
* Entry points *
\*************/

/* Per-privilege-level detection. Each runs its level's probes into hs->caps
 * (the engine points hs->caps at the rvcaps struct before calling). */
void hart_probe_mmode(struct hart_state *hs);
void hart_probe_smode(struct hart_state *hs);

/* Probe all privileged caps once (lazily, cached) and return the result.
 * Defined in hart_priv_caps.c; used by the report/menu layer. */
struct rvcaps *priv_get_caps(void);

#endif /* HART_PRIV_CAPS_H */
