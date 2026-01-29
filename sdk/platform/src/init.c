/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Special handling: irq.h must be included before target_config.h
 * so that _IRQ_H is defined when DEFINE_PLATFORM_INTC_MAP is expanded */
#include <platform/interfaces/irq.h>	/* For irq_init() and _IRQ_H */
#define NEED_HART_INTC_MAP
#include <target_config.h>		/* For PLAT_* constants and DEFINE_PLATFORM_INTC_MAP */
#undef NEED_HART_INTC_MAP
#include <platform/interfaces/ipi.h>	/* For ipi_init() */
#include <platform/interfaces/uart.h>	/* For uart_init() */
#include <platform/riscv/csr.h>		/* For csr_read()/pause() */
#include <platform/riscv/hart.h>	/* For hart_get_count/state() */
#include <platform/utils/utils.h>	/* For console output */

void
platform_init_default(void)
{
	uart_init();
	ANN("BareMetal loader (c) FORTH/CARV 2026\n\r");
	ANN("------------------------------------\n\r");
	DBG("Boot hart_id: %li\n", csr_read(CSR_MHARTID));

	#if (PLAT_MAX_HARTS > 1)
		/* Give some time for secondary harts to reach Lcounter_ready in start.S */
		uint32_t current_count = hart_get_count();
		uint32_t last_count = 0;
		uint32_t stable_checks = 0;
		uint32_t max_stable_checks = PLAT_MAX_HARTS * 10000000;

		/* Wait until we either get PLAT_MAX_HARTS registered or
		 * the counter remains stable for max_stable_checks checks. */
		while (stable_checks < max_stable_checks) {
			current_count = hart_get_count();

			if (current_count == PLAT_MAX_HARTS)
				break;

			if (current_count == last_count)
				stable_checks ++;
			else {
				stable_checks = 0;
				last_count = current_count;
			}

			/* Give boot hart some room to breathe */
			pause();
		}

		/* Verify consistency of hart_state structs */
		for (int i = 0; i < current_count; i++) {
			struct hart_state *hs = hart_get_hstate_by_idx(i);
			if (hs->hart_idx != i) {
				hart_set_count(i + 1);
				current_count = hart_get_count();
				WRN("Hart initialization for idx: %i incomplete, truncated counter to: %i\n",
				    i, current_count);
				break;
			}
		}

		/* Clean up boot counter_status (on __data_end[0])to prevent warm reboot issues
		 * in case our ram retains the "ready" value. */
		atomic_store((uint32_t*)((uintptr_t)__data_end), 0);

		INF("Got %i secondary harts out of %i maximum\n", current_count, PLAT_MAX_HARTS);
	#endif

	#if defined(DEBUG)
		/* Platform interrupt controller mapping */
		extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];

		/* Make sure we only have one mapping per interrupt target (we use hart_idx here but it also covers
		 * ctx_idx / idc_idx since it's a union). Since this is hardcoded in target_config.h only do the check
		 * durring debugging. It can't go wrong due to runtime behavior. */
		for (int i = 0; i < PLAT_MAX_HARTS; i++) {
			uint16_t target_idx = platform_intc_map[i].target.hart_idx;
			int matches = 0;
			for (int j = 0; j < PLAT_MAX_HARTS; j++)
				if (platform_intc_map[j].target.hart_idx == target_idx)
					matches++;
			if (matches != 1)
				ERR("Invalid APLIC target mapping for index %i (matches: %i)\n", target_idx, matches);
		}
	#endif

	DBG("Calling irq_init()...\n");
	irq_init();
}

/* Platform-specific code may override this and use a custom platform_init().
 * To make things simple keep the default implementation around as a separate
 * function, that platform-specific code may use as fallback to avoid code
 * duplication. */
void
__attribute__((weak))
platform_init(void)
{
	platform_init_default();
}
