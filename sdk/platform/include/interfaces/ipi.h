/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _IPI_H
#define _IPI_H

#include <stdint.h>			/* For typed integers */
#include <platform/utils/bitfield.h>	/* For BIT() macro */
#include <platform/riscv/hart.h>	/* For hart_state */

/* Keep this up to 15 so that it can be used
 * as part of a 16bit mask. */
enum ipi_type {
	IPI_WAKEUP		= BIT(0),
	IPI_WAKEUP_WITH_ADDR	= BIT(1),
	IPI_ENABLE_EIID		= BIT(2),
	IPI_DISABLE_EIID	= BIT(3),
	IPI_MAX			= BIT(15)
};

void ipi_send(struct hart_state* target_hstate, enum ipi_type type);
void ipi_self(enum ipi_type type);
void ipi_clear(void);

#endif /* _IPI_H */