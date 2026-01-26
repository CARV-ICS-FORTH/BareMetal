/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_* constants */
#include <platform/interfaces/irq.h>	/* For IRQ interface definitions */
#include <platform/riscv/hart.h>	/* For hart state and operations */
#include <platform/riscv/mmio.h>	/* For PLIC register access */
#include <platform/utils/utils.h>	/* For console output */

#include <errno.h>			/* For error constants */
#include <stdbool.h>			/* For bool type */

#if defined(PLAT_HAS_PLIC)

extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];

/*
 * The PLIC spec:
 * https://github.com/riscv/riscv-plic-spec/releases/tag/1.0.0
 */

/*******************\
* PLIC register map *
\*******************/
#define _REGBASE PLAT_PLIC_BASE
#include <platform/utils/register.h>	/* For register access macros */

#define PRIORITY_REG(_src)	REG32_ARRAY(0x0, _src)

#define PENDING_REG(_src)	REG32_BITMAP(0x1000, _src)

#define ENABLE_REG(_ctx, _src)	REG32_BITMAP(0x2000 + (0x80 * (_ctx)), _src)

#define PRIORITY_THR_REG(_ctx)	REG32_STRIDE(0x200000, _ctx, 0x1000)

#define CLAIM_REG(_ctx)		REG32_STRIDE(0x200004, _ctx, 0x1000)

/*********\
* Helpers *
\*********/

static uint8_t
plic_map_priority(enum irq_priority priority)
{
	switch (priority) {
	case IRQ_PRIORITY_DISABLED:
		return 0;
	case IRQ_PRIORITY_LOW:
		return 1;
	case IRQ_PRIORITY_MEDIUM:
		return 1 + (PLIC_MAX_PRIORITY / 2);
	case IRQ_PRIORITY_HIGH:
		return PLIC_MAX_PRIORITY;
	default:
		return 0;
	}
}

/**************\
* Entry points *
\**************/

/* Validate IRQ mappings and initialize PLIC  */
int __attribute__((weak))
irq_init(void)
{
	/* 
	 * Initialize PLIC to clean state
	 * Disable all interrupts by setting priority to 0
	 * Source 0 is reserved, start from 1
	 */
	for (int i = 1; i <= PLAT_NUM_IRQ_SOURCES; i++)
		write32(PRIORITY_REG(i), 0);

	/*
	 * Clear the enable bits for all sources in all mapped contexts,
	 * and initialize priority threshold to PLIC_MAX_PRIORITY so that
	 * no interrupts are triggered until we lower the threshold for them.
	 */
	for (int i = 0; i < PLAT_MAX_HARTS; i++) {
		uint16_t ctx = platform_intc_map[i].target.ctx_idx;
		for (int j = 0; j < ((PLAT_NUM_IRQ_SOURCES + 31) / 32); j++)
			write32(ENABLE_REG(ctx, j * 32), 0);
		write32(PRIORITY_THR_REG(ctx), PLIC_MAX_PRIORITY);
	}

	return 0;
}

/* Enable interrupt delivery for source_id */
void __attribute__((weak))
irq_source_enable(uint16_t source_id)
{
	DBG("Enabling interrupt for source: %i\n", source_id);

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL)
		return;

	int ctx = irq_get_target_idx_for_hart(irq_sm->target_hart);
	if (ctx < 0) {
		ERR("Attempted to enable interrupt for unmapped target (source: %i, target: %i)\n", source_id, irq_sm->target_hart);
		return;
	}
	DBG("Interrupt source %i mapped to ctx %i\n", source_id, ctx);

	/* Enable interrupt and make sure priority for
	 * the context is lower than intr priority. */
	uint32_t val = read32(ENABLE_REG(ctx, source_id));
	val |= (1 << (source_id & 31));
	write32(ENABLE_REG(ctx, source_id), val);
	write32(PRIORITY_REG(source_id), plic_map_priority(irq_sm->priority));
	write32(PRIORITY_THR_REG(ctx), 0);
}

/* Disable interrupt delivery for source_id */
void __attribute__((weak))
irq_source_disable(uint16_t source_id)
{
	DBG("Disabling interrupt for source: %i\n", source_id);

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL)
		return;

	int ctx = irq_get_target_idx_for_hart(irq_sm->target_hart);
	if (ctx < 0) {
		ERR("Attempted to disable interrupt for unmapped target (source: %i, target: %i)\n", source_id, irq_sm->target_hart);
		return;
	}
	DBG("Interrupt source %i mapped to ctx %i\n", source_id, ctx);

	/* Disable interrupt for that source, and set its priority to 0 to disable it */
	uint32_t val = read32(ENABLE_REG(ctx, source_id));
	val &= ~(1 << (source_id & 31));
	write32(ENABLE_REG(ctx, source_id), val);
	write32(PRIORITY_REG(source_id), 0);
}

/*
 * Main interrupt dispatch handler - called from trap handler
 * This is called when an external interrupt is pending
 */
void __attribute__((weak))
irq_dispatch(uint16_t)
{
	struct hart_state* hs = hart_get_hstate_self();
	if (hs->irq_map_idx < 0) {
		ERR("Got interrupt on uninitialized hart (id: %li, idx: %i)\n", hs->hart_id, hs->hart_idx);
		return;
	}

	uint16_t ctx = platform_intc_map[hs->irq_map_idx].target.ctx_idx;
	/* 
	 * Claim the interrupt from PLIC
	 * This returns the highest priority pending interrupt source
	 * and atomically clears the corresponding pending bit
	 */
	uint32_t source_id = read32(CLAIM_REG(ctx));

	if (source_id == 0) {
		WRN("Got spurious interrupt from PLIC !\n");
		goto complete;
	}

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL) {
		ERR("Got interrupt from unmapped source: %i\n", source_id);
		goto complete;
	}

	DBG("Calling handler for interrupt source: %i\n", source_id);

	/* Got mapping, call the associated interrupt handler */
	irq_sm->handler((uint16_t) source_id);

 complete:
	/*  Notify PLIC we are done handling that interrupt */
	write32(CLAIM_REG(ctx), source_id);
	return;
}

#endif /* defined(PLAT_HAS_PLIC) */