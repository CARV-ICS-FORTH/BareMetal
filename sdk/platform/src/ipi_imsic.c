/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_HAS_MSWI/PLAT_MSWI_BASE */
#include <platform/interfaces/ipi.h>	/* For IPI interface definitions */
#include <platform/interfaces/irq.h>	/* For struct irq_target_mapping */
#include <platform/riscv/csr.h>		/* For csr_read/write() */
#include <platform/riscv/hart.h>	/* For hart state and macros (includes stdatomic.h) */
#include <platform/riscv/mmio.h>	/* For mmio access to remote hstate/MSIP */

#if defined(PLAT_HAS_IMSIC) && (PLAT_IMSIC_IPI_EIID > 0)

#define _REGBASE PLAT_IMSIC_BASE
#include <platform/utils/register.h>	/* For register access macros */

/* Platform interrupt controller mapping */
extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];

/* We assume a memory layout as specified in chapter 3.6 of the AIA spec.
 * That is all M-mode interrupt files stacked together separately for S/HS/VS-mode,
 * with no gaps between them. If a target comes up with gaps between M-mode interrupt
 * files, or some other weird non-standard layout (that would probably also break other
 * sw out there) we can revisit this.
 *
 * So for now the base of each hart's M-mode interrupt file would be:
 * PLAT_IMSIC_BASE + hart_index << 12 (4KB aligned base address, 4KB stride).
 */
#define SETEIPNUM_LE(_hart_idx)	REG32_STRIDE(0x0, _hart_idx, 0x1000)

/* Add ipi_type to target hart's ipi_mask, and trigger the interrupt */
void
ipi_send(struct hart_state* target_hs, enum ipi_type type)
{
	hart_set_ipi(target_hs, (uint16_t) type);
	uint16_t imsic_hart_idx = platform_intc_map[target_hs->irq_map_idx].target.hart_idx;
	write32(SETEIPNUM_LE(imsic_hart_idx), PLAT_IMSIC_IPI_EIID);
}

/* Same for self-ipis */
void
ipi_self(enum ipi_type type)
{
	struct hart_state *hs = hart_get_hstate_self();
	hart_set_ipi(hs, (uint16_t) type);
	uint16_t imsic_hart_idx = platform_intc_map[hs->irq_map_idx].target.hart_idx;
	write32(SETEIPNUM_LE(imsic_hart_idx), PLAT_IMSIC_IPI_EIID);
}

/* Clear ipi from self (called by the IPI trap handler) */
void
ipi_clear(void) { return; }

#endif /* defined(PLAT_HAS_IMSIC) && (PLAT_IMSIC_IPI_EIID > 0) */