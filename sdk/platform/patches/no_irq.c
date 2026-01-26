/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Stub irq functions for applications that don't require external
 * interrupts from devices.
 */

#include <platform/interfaces/irq.h>

int irq_init(void) { return 0; };
void irq_source_enable(uint16_t source_id) { return; }
void irq_source_disable(uint16_t source_id) { return; }
void irq_dispatch(uint16_t eiid) { return; };