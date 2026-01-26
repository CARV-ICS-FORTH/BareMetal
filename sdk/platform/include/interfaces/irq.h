/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _IRQ_H
#define _IRQ_H

#include <stdint.h>	/* For typed integers */

/* Interrupt handler function type, implemented by
 * the driver, one for each interrupt mapping. */
typedef void (*irq_handler_t)(uint16_t source);

/* Priority levels */
enum irq_priority {
	IRQ_PRIORITY_DISABLED = 0,
	IRQ_PRIORITY_LOW = 1,
	IRQ_PRIORITY_MEDIUM = 2,
	IRQ_PRIORITY_HIGH = 3,
	IRQ_PRIORITY_MAX = IRQ_PRIORITY_HIGH,
};

/* Interrupt trigger modes (for flags field, used on APLIC, matching APLIC's source modes) */
enum irq_trigger_mode {
	IRQ_TRIGGER_DEFAULT = 0,	/* Use controller default (typically level-high) */
	IRQ_TRIGGER_DETACHED = 1,	/* Wire detached but still active (via setip/setipnum)*/
	IRQ_TRIGGER_EDGE_RISE = 4,	/* Edge-triggered, rising edge */
	IRQ_TRIGGER_EDGE_FALL = 5,	/* Edge-triggered, falling edge */
	IRQ_TRIGGER_LEVEL_HIGH = 6,	/* Level-triggered, active-high */
	IRQ_TRIGGER_LEVEL_LOW = 7,	/* Level-triggered, active-low */
};

/* 
 * Static interrupt mapping structure between interrupt
 * sources and handler functions. Drivers populate this
 * at compile time. We pack this to ensure consistent
 * layout in memory.
 *
 * Note eiid 1 is reserved for PLAT_IMSIC_IPI_EIID
 */
struct irq_source_mapping {
	/* Hardware source identification */
	union {
		uint16_t wire_id;	/* PLIC/APLIC wire number */  
		uint16_t eiid;		/* IMSIC interrupt identity (future) */
	} source;

	/* Configuration */
	uint8_t priority;	/* Interrupt priority */
	uint8_t flags;		/* Edge/Level, Rising/Falling */
	uint16_t target_hart;	/* Which hart (by index, see hart.h) should handle this
				 * (leave it 0 for boot hart). */
	uint16_t reserved;	/* For padding so that handler is dword aligned */

	irq_handler_t handler;	/* Function to call on interrupt */
} __attribute__((packed));

/* 
 * Macro for drivers to register their interrupts
 * This places the mapping in a special .rodata section
 * 
 * Usage in driver code:
 *	REGISTER_IRQ_SOURCE(uart0_rx, {
 *		.source.wire_id = 10,
 *		.handler = uart_rx_handler,
 *		.target_hart = 0,
 *		.priority = IRQ_PRIORITY_HIGH,
 *		.flags = 0,
 *	});
 */
#define REGISTER_IRQ_SOURCE(name, ...) \
	static const volatile struct irq_source_mapping \
	__attribute__((used, section(".rodata.irq_sources"), aligned(16))) \
	__irq_source_##name = __VA_ARGS__

/*
 * Static interrupt mapping structure between the interrupt
 * controller and interrupt targets/harts. In target_config.h an
 * array of those structures is defined for all harts in the
 * system. Note that's only for M-mode interrupts, so only one
 * entry per hart_id is supported. */
struct irq_target_mapping {
	uint64_t hart_id;
	union {
		uint64_t ctx_idx;	/* Maps to a PLIC context offset */
		uint64_t idc_idx;	/* Maps to an APLIC IDC context (direct delivery) */
		uint64_t hart_idx;	/* Maps to an APLIC/IMSIC hart index (MSI delivery mode) */
	} target;
} __attribute__((packed));


/* 
 * Define the platform's hart-to-interrupt-controller mapping table
 * Always creates 'platform_intc_map' array with PLAT_MAX_HARTS entries
 * 
 * Usage in target_config.h for PLIC:
 * 
 * DEFINE_PLATFORM_INTC_MAP({
 *	{ .hart_id = 0, .target = { .ctx_idx = 0 } },
 *	{ .hart_id = 1, .target = { .ctx_idx = 2 } },
 *	{ .hart_id = 2, .target = { .ctx_idx = 4 } },
 *	{ .hart_id = 3, .target = { .ctx_idx = 6 } }
 * });
 */
#define DEFINE_PLATFORM_INTC_MAP(...) \
	const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS] \
	__attribute__((section(".rodata.irq_targets"), aligned(16))) = __VA_ARGS__


/* Common helpers in irq_common.c */
const struct irq_source_mapping * irq_get_srcmap(uint16_t source_id);
int irq_get_target_idx_for_hart(uint16_t target_hart);


/* Initialize interrupt subsystem */
int irq_init(void);

/* Enable/disable specific wire */
void irq_source_enable(uint16_t source_id);
void irq_source_disable(uint16_t source_id);

/* Main interrupt handler - called from trap handler
 * Note: eiid is only used when IMSIC is present to
 * avoid re-reading mtopei. */
void irq_dispatch(uint16_t eiid);

#endif /* _IRQ_H */