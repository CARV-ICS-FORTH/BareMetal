/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_HAS_MSWI/PLAT_MSWI_BASE */
#include <platform/interfaces/ipi.h>	/* For IPI interface definitions */
#include <platform/riscv/csr.h>		/* For csr_read/write() */
#include <platform/riscv/hart.h>	/* For hart state and macros (includes stdatomic.h) */
#include <platform/riscv/mmio.h>	/* For mmio access to remote hstate/MSIP */

#if defined(PLAT_HAS_MSWI) && (PLAT_IMSIC_IPI_EIID == 0)

#define _REGBASE PLAT_MSWI_BASE
#include <platform/utils/register.h>	/* For register access macros */

#define MSIP_BASE(_hartid)	REG32_ARRAY(0x0, _hartid)

/* Add ipi_type to target hart's ipi_mask, and trigger the interrupt */
void
ipi_send(struct hart_state* target_hs, enum ipi_type type)
{
	hart_set_ipi(target_hs, (uint16_t) type);
	write32(MSIP_BASE(target_hs->hart_id), 1);
}

/* Same for self-ipis */
void
ipi_self(enum ipi_type type)
{
	struct hart_state *hs = hart_get_hstate_self();
	hart_set_ipi(hs, (uint16_t) type);
	write32(MSIP_BASE(hs->hart_id), 1);
}

/* Clear ipi from self (called by the IPI trap handler) */
void
ipi_clear(void)
{
	uint64_t hart_id = csr_read(CSR_MHARTID);
	write32(MSIP_BASE(hart_id), 0);
}

#endif /* defined(PLAT_HAS_MSWI) && !(PLAT_IMSIC_IPI_EIID > 0) */