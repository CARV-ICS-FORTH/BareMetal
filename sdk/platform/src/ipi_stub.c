/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/interfaces/ipi.h>
#include <target_config.h>

#ifdef PLAT_NO_IPI

void ipi_init(void) { return; }
void ipi_send(struct hart_state* target_hstate, enum ipi_type type) { return; }
void ipi_self(enum ipi_type type) { return; }
void ipi_clear(void) { return; }

#endif