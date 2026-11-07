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
 * CSRs anyway, so recording them costs almost nothing. They can't be re-probed
 * reliably after boot (e.g. Smepmp's mseccfg.RLB, once our own PMP rule is locked),
 * so the test-suite's M-mode units read them back from here instead:
 *   - CAP_MCOUNTINHIBIT, CAP_SMCNTRPMF : hart_init_counters() (hart.S)
 *   - CAP_SDTRIG                       : hart_init_sdtrig()   (hart.S)
 *   - CAP_SMEPMP                       : hart_init_pmp()      (pmp.c)
 *
 * They occupy the low bits [0..3] of early_caps, grouped and contiguous so the
 * upper bits stay free for future early-detected flags. Assembler-safe (BIT()
 * macros only, no C declarations).
 */
#define CAP_MCOUNTINHIBIT	BIT(0)	/* For pre 1.11 implementations */
#define CAP_SMCNTRPMF		BIT(1)
#define CAP_SDTRIG		BIT(2)
#define CAP_SMEPMP		BIT(3)	/* Best effort check based on mseccfg.RLB */

#endif /* _EARLY_CAPS_H */
