/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/riscv/hart.h>

/* Returns a pointer to the current hart's errno field.
 * errno is per-hart (thread-local) since each hart has its
 * own hart_state structure. */
int *__errno_location(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	return &hs->error;
}
