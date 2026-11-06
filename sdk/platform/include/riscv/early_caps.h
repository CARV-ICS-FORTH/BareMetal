/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _EARLY_CAPS_H
#define _EARLY_CAPS_H

#include <platform/utils/bitfield.h>

/*
 * M-mode capabilities detected "for free" by early boot code and stashed in
 * hart_state.early_caps.
 *
 * These are a byproduct of mandatory init steps that have to touch the relevant
 * CSRs anyway, so recording them costs almost nothing and saves the test-suite
 * probe from re-detecting them:
 *   - CAP_MCOUNTINHIBIT, CAP_SMCNTRPMF : hart_init_counters() (hart.S)
 *   - CAP_SDTRIG                       : hart_init_sdtrig()   (hart.S)
 *   - CAP_SMEPMP                       : hart_init_pmp()      (pmp.c)
 *
 * They occupy the low bits [0..3] of the m_caps word; the remaining (probe-only)
 * M-mode caps are defined in the test suite's caps.h, which includes this header,
 * starting at bit 4. Kept grouped and contiguous so the upper early_caps bits
 * stay free for future early-detected flags. Assembler-safe (BIT() macros only,
 * no C declarations).
 */
#define CAP_MCOUNTINHIBIT	BIT(0)	/* For pre 1.11 implementations */
#define CAP_SMCNTRPMF		BIT(1)
#define CAP_SDTRIG		BIT(2)
#define CAP_SMEPMP		BIT(3)	/* Best effort check based on mseccfg.RLB */

#endif /* _EARLY_CAPS_H */
