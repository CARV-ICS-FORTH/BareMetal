/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _HART_H
#define _HART_H

#include <stdint.h>			/* For typed integers */
#include <stdatomic.h>			/* For C11 atomic types / accessors */
#include <platform/utils/bitfield.h>	/* For BIT() macro */
#include <stdbool.h>			/* For bool in hart_test_flags() */
#include <stddef.h>			/* For size_t in hart_va_map_range() */
#include <platform/riscv/csr.h>		/* For CSRs and csr ops */
#include <platform/riscv/caps.h>	/* For struct rvcaps */

/*
 * Used for to pass arguments on IPIs
 */
struct next_params {
	uint64_t arg0;
	uint64_t arg1;
	uint64_t mtimer_cycles;
};

/*
 * A hart's local state, stored at the begining of
 * its stack (in boot.S), and pointed by mscratch.
 *
 * Note: This needs to be a multiple of 16bytes
 * so that sp remains 16byte aligned. If its size
 * changes boot.S needs to be updated.
 */
struct hart_state {
	union {
		/* Caps set by early boot code (hart_init_*) */
		uint64_t early_caps;
		/* Pointer to a struct with more complex
		 * capability infos (wip) */
		void* caps;
	};

	/* Physical hart_id as reported by marchid */
	uint64_t hart_id;

	/* Logical hart_id given during reset/wakeup (0 is the boot hart) */
	uint16_t hart_idx;

	/* IRQ target mapping index (see irq.h), -1 if no mapping */
	int16_t irq_map_idx;

	/* Per-hart errno */
	int32_t error;

	/* Includes ipi_mask (see ipi.h) and state
	 * flags (see below). */
	_Atomic(uint32_t) flags;

	/* Reserved for future use */
	uint32_t reserved;

	/* See timer.c */
	uint64_t last_cyclecount_tval;

	/* Used for program's per-hart internal state */
	void* internal;

	/* Used for wakeup with addr IPI */
	uintptr_t next_addr;
	struct next_params *next_params;
} __attribute__((aligned(8)));

enum state_flags {
	HS_FLAG_READY		= BIT(0),
	HS_FLAG_RUNNING		= BIT(1),
	HS_FLAG_SLEEPING	= BIT(2),
	HS_FLAG_CAPS_IS_PTR	= BIT(3)
};

/* Static assert to ensure size */
_Static_assert(sizeof(struct hart_state) == 64, "hart_state must be 64 bytes");

/* Clear macros for bit manipulation */
#define IPI_BITS   16
#define FLAG_BITS  16

extern uint64_t __stack_start;
extern uint64_t __stack_size_shift;
extern uint64_t __data_end;

/* Helpers for interrupt handling */
static inline void hart_allow_interrupts(void)
{
	csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_MIE);
}

static inline void hart_block_interrupts(void)
{
	csr_clear_bits(CSR_MSTATUS, CSR_MSTATUS_MIE);
}

static inline void hart_enable_intr(enum interrupt_bits intr)
{
	#if defined(PLAT_HAS_IMSIC) && !defined(PLAT_BYPASS_IMSIC)
		/* Lowest priority ext interrupt -> highest eiid */
		uint16_t threshold = PLAT_NUM_IRQ_SOURCES + 1;
		switch(intr) {
			#if (PLAT_IMSIC_IPI_EIID > 0)
			case INTR_MACHINE_SOFTWARE_TRIG:
				/* This enables IPIs keeping other interrupts with
				 * less priority disabled. However we'll run this only
				 * once in hart_init before we enable external interrupts
				 * so it's ok. */
				threshold = PLAT_IMSIC_IPI_EIID + 1;
				/* Fallthrough */
			#endif
			case INTR_MACHINE_EXTERNAL:
				/* Enable interrupt delivery (EIDELIVERY @ 0x70) */
				csr_write(CSR_MISELECT, 0x70);
				csr_write(CSR_MIREG, 1);
				/* Set threshold on (EITHRESHOLD @ 0x72) */
				csr_write(CSR_MISELECT, 0x72);
				csr_write(CSR_MIREG, threshold);
				csr_set_bits(CSR_MIE, 1 << INTR_MACHINE_EXTERNAL);
				return;
			default:
				csr_set_bits(CSR_MIE, 1 << intr);
				return;
		}
	#else
		csr_set_bits(CSR_MIE, 1 << intr);
	#endif
}

static inline void hart_disable_intr(enum interrupt_bits intr)
{
	switch(intr) {
		case INTR_MACHINE_SOFTWARE_TRIG:
			/* Never disable IPIs */
			break;
		#if defined(PLAT_HAS_IMSIC) && !defined(PLAT_BYPASS_IMSIC)
		case INTR_MACHINE_EXTERNAL:
			/* Set threshold down to IPI_EIID + 1 so that higher
			 * EIIDs (with lower priority) are masked. If IPI_EEID
			 * is 0 this will still work since we'll set it to 1 and
			 * eithreshold is inclusive (so 1 is also masked). */
			csr_write(CSR_MISELECT, 0x72);
			csr_write(CSR_MIREG, PLAT_IMSIC_IPI_EIID + 1);
			#if (PLAT_IMSIC_IPI_EIID == 0)
				/* Also disable external interrupt delivery
				 * (IPIs served via MSWI) */
				csr_write(CSR_MISELECT, 0x70);
				csr_write(CSR_MIREG, 0);
				/* Fallthrough */
			#else
				break;
			#endif
		#endif
		default:
			csr_clear_bits(CSR_MIE, 1 << intr);
			break;
	}
}

static inline uint64_t hart_get_pending_interrupts(void)
{
	return csr_read(CSR_MIP);
}

/* Helpers for profile counters */
static inline void hart_enable_counter(enum hart_counters cntr)
{
	csr_set_bits(CSR_MCOUNTEREN, BIT(cntr));
	csr_clear_bits(CSR_MCOUNTINHIBIT, BIT(cntr));
}

static inline void hart_disable_counter(enum hart_counters cntr)
{
	csr_clear_bits(CSR_MCOUNTEREN, BIT(cntr));
	csr_set_bits(CSR_MCOUNTINHIBIT, BIT(cntr));
}

static inline uint64_t hart_get_counter(enum hart_counters cntr)
{
	return csr_read(CSR_MHPMCOUNTER(cntr));
}

static inline void hart_reset_counter(enum hart_counters cntr)
{
	csr_write(CSR_MCYCLE + cntr, 0);
}

/* Grabs the current hart's hart_state from mscratch csr */
static inline struct hart_state* hart_get_hstate_self(void)
{
	return (struct hart_state*)((uintptr_t)csr_read(CSR_MSCRATCH));
}

/* Grabs a hart's hart_state from the begining of its stack, based on its index */
static inline struct hart_state* hart_get_hstate_by_idx(uint32_t hart_idx)
{
	uintptr_t stack_start = (uintptr_t)(__stack_start - (hart_idx << __stack_size_shift));
	return (struct hart_state*)((uintptr_t)(stack_start - sizeof(struct hart_state)));
}

/* Returns the hart counter (last idx + 1) */
static inline uint16_t hart_get_count(void)
{
	uintptr_t counter_addr = __data_end + 4;
	uint32_t current_count = atomic_load_explicit((uint32_t*)counter_addr, memory_order_acquire);
	return  (uint16_t)(current_count + 1);
}

/* Sets the hart counter (stores last idx = count - 1), should only be used on init.c */
static inline void hart_set_count(uint16_t count)
{
	uintptr_t counter_addr = __data_end + 4;
	/* Store count - 1 since we store last index, not count */
	uint32_t last_idx = (count > 0) ? (uint32_t)(count - 1) : 0;
	atomic_store_explicit((uint32_t*)counter_addr, last_idx, memory_order_release);
}

/* Check if hart_state has any IPIs pending */
static inline bool hart_has_ipis(struct hart_state *hs)
{
	uint32_t current = atomic_load_explicit(&hs->flags, memory_order_acquire);
	return (current & ((1U << IPI_BITS) - 1)) != 0;
}

static inline uint16_t hart_get_ipi_mask(struct hart_state *hs)
{
	uint32_t current = atomic_load_explicit(&hs->flags, memory_order_acquire);
	return (uint16_t)(current & ((1U << IPI_BITS) - 1));
}

/* Set an IPI mask on hart_state */
static inline void hart_set_ipi(struct hart_state *hs, uint16_t ipi_mask) {
	atomic_fetch_or_explicit(&hs->flags, ipi_mask, memory_order_release);
}

/* Clear IPI mask from hart_state */
static inline uint16_t hart_clear_ipi_mask(struct hart_state *hs) {
	uint32_t mask = ((1U << FLAG_BITS) - 1) << IPI_BITS;  /* Preserve flags */
	uint32_t old = atomic_fetch_and(&hs->flags, mask);
	return old & ((1U << IPI_BITS) - 1);
}

/* Get all flags (upper 16 bits) from hart_state */
static inline uint16_t hart_get_flags(struct hart_state *hs)
{
	uint32_t current = atomic_load_explicit(&hs->flags, memory_order_acquire);
	return (current >> IPI_BITS) & ((1U << FLAG_BITS) - 1);
}

/* Set one or more flag bits (upper 16 bits) on hart_state */
static inline void hart_set_flags(struct hart_state *hs, uint16_t flags)
{
	atomic_fetch_or_explicit(&hs->flags, 
				(uint32_t)flags << IPI_BITS,
				memory_order_release);
}

/* Clear one or more flag bits (upper 16 bits) on hart_state */
static inline void hart_clear_flags(struct hart_state *hs, uint16_t flags)
{
	atomic_fetch_and_explicit(&hs->flags,
				~((uint32_t)flags << IPI_BITS),
				memory_order_release);
}

/* Test if specific flag bits are set on hart_state */
static inline bool hart_test_flags(struct hart_state *hs, uint16_t flags)
{
	uint32_t current = atomic_load_explicit(&hs->flags, memory_order_acquire);
	uint16_t current_flags = (current >> IPI_BITS) & ((1U << FLAG_BITS) - 1);
	return (current_flags & flags) == flags;
}

/* Weak handlers for applications to override */
void hart_on_mswtrig(struct hart_state *hs);
void hart_on_mtimer(struct hart_state *hs);
void hart_on_mecall(struct hart_state *hs);

/* Extended init functions in hart.S called by hart_init */
void hart_init_fpu(struct hart_state *hs, uint64_t misa);
void hart_init_vpu(struct hart_state *hs, uint64_t misa);
void hart_init_counters(struct hart_state *hs);
void hart_init_sdtrig(struct hart_state *hs);

/* VA mapping functions in hart_va.c */
int hart_va_map_range(uintptr_t phys_addr, size_t *size, uint64_t mode, bool napot);

/* Hart probing facility in hart_probe.c */
void hart_probe_priv_caps(struct rvcaps *caps);

/* Entry points on hart.c */
void hart_init(void);
void hart_configure_imsic_eiid(uint16_t hart_idx, uint16_t eiid, bool enable);
void hart_wakeup_with_addr(uint16_t hart_idx, uintptr_t jump_addr, uint64_t arg0, 
			   uint64_t arg1, uint64_t mtimer_cycles);
void hart_wakeup_all_with_addr(uintptr_t jump_addr, uint64_t arg0, uint64_t arg1,
			  uint64_t mtimer_cycles);
void hart_hang(void);

#endif /* _HART_H */