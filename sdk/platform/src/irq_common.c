/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <target_config.h>		/* For PLAT_* constants */
#include <platform/riscv/hart.h>	/* For hart state and operations */
#include <platform/interfaces/irq.h>	/* For IRQ interface definitions */
#include <platform/utils/utils.h>	/* For console output */

/*********\
* Helpers *
\*********/

/* Platform interrupt controller mapping */
extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];

/* Find source mapping for the given source_id */
const struct irq_source_mapping *
irq_get_srcmap(uint16_t source_id)
{
	/* Declare as const - these point to read-only data in .rodata section
	 * This both prevents compiler optimization issues and is semantically correct */
	extern const struct irq_source_mapping __irq_sources_start;
	extern const struct irq_source_mapping __irq_sources_end;
	const struct irq_source_mapping *first = &__irq_sources_start;
	const struct irq_source_mapping *last = &__irq_sources_end;
	DBG("Num sources: %li\n", last - first);
	for (const struct irq_source_mapping *current = first; current < last; current++) {
		DBG("Got mapping with wire_id: %i\n", current->source.wire_id);
		if (current->source.wire_id == source_id) {
			DBG("Got mapping for source: %i, target_hart: %i\n", source_id, current->target_hart);
			return current;
		}
	}

	ERR("Couldn't find IRQ source mapping for id: %i\n", source_id);
	return NULL;
}

/* Get target index (IDC index or hart index) for target_hart */
int
irq_get_target_idx_for_hart(uint16_t target_hart)
{
	struct hart_state* hs = hart_get_hstate_by_idx(target_hart);
	if (hs->irq_map_idx < 0)
		return -1;
	/* Works also for ctx_idx and idc_idx since they're the same union field */
	return platform_intc_map[hs->irq_map_idx].target.hart_idx;
}