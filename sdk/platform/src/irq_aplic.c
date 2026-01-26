/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_* constants */
#include <platform/interfaces/irq.h>	/* For IRQ interface definitions */
#include <platform/riscv/hart.h>	/* For hart state and operations */
#include <platform/riscv/mmio.h>	/* For APLIC register access */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/utils/bitfield.h>	/* For BIT/FIELD macros */

#include <errno.h>			/* For error constants */
#include <stdbool.h>			/* For bool type */

#if defined(PLAT_HAS_APLIC)

/* Determine APLIC delivery mode */
#if defined(PLAT_HAS_IMSIC) && (PLAT_APLIC_FORCE_DIRECT == 0)
	/* MSI mode */
	#define APLIC_USES_IMSIC 1
#else
	/* Direct mode */
	#define APLIC_USES_IMSIC 0
#endif

/* Platform interrupt controller mapping */
extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];

/*
 * The AIA spec:
 * https://github.com/riscv/riscv-aia
 */

/******************************************************************\
* APLIC Register Layout (RISC-V AIA Specification v1.0, Chapter 4) *
\******************************************************************/
#define _REGBASE PLAT_APLIC_BASE
#include <platform/utils/register.h>	/* For register access macros */

/* Domain Configuration Register */
#define DOMAINCFG		REG32(0x0)
#define DOMAINCFG_BE		BIT(0)	/* Big Endian */
#define DOMAINCFG_DM		BIT(2)	/* Delivery Mode: 0=Direct, 1=MSI */
#define DOMAINCFG_IE		BIT(8)	/* Interrupt Enable */

/* Source Configuration - array of 1024 registers at 4-byte stride */
#define SOURCECFG(_src)		REG32_ARRAY(0x0004, (_src) - 1)
#define SOURCECFG_CHILD_ID	FIELD(9,0)	/* Child domain id to delegate this source to */
#define SOURCECFG_D		BIT(10)		/* Delegate to child domain */
/* Below fields are only present when D is 0 */
#define SOURCECFG_SM		FIELD(2,0)	/* Source Mode*/
enum source_modes {
	SM_INACTIVE = 0,	/* Source inactive/ignored */
	SM_DETACHED = 1,	/* Wire detached but still active (via setip/setipnum)*/
	SM_EDGE_RISE = 4,	/* Edge-triggered on rise */
	SM_EDGE_FALL = 5,	/* Edge-triggered on fall */
	SM_LEVEL_HIGH = 6,	/* Level-triggered, active-high */
	SM_LEVEL_LOW = 7	/* Level-triggered, active-low */
};

/* MSI configuration (WiP) - Writeable only by the root domain */
#define MMSIADDRCFG		REG32(0x1BC0)
#define MMSIADDRCFG_LBPPN	FIELD(31,0)	/* Low base PPN */
#define MMSIADDRCFGH		REG32(0x1BC4)
#define MMSIADDRCFGH_HBPPN	FIELD(11,0)	/* High base PPN */
#define MMSIADDRCFGH_LHXW	FIELD(15,12)	/* Low hart index width */
#define MMSIADDRCFGH_HHXW	FIELD(18,16)	/* High hart index width */
#define MMSIADDRCFGH_LHXS	FIELD(22,20)	/* Low hart index shift */
#define MMSIADDRCFGH_HHXS	FIELD(28,24)	/* High hart index shift */
#define MMSIADDRCFGH_L		BIT(31)		/* Lock bit for {M,S}MSIADDRCFG/H */

#define SMSIADDRCFG		REG32(0x1BC8)
#define SMSIADDRCFG_LBPPN	FIELD(31,0)	/* Low base PPN */
#define SMSIADDRCFGH		REG32(0x1BCC)
#define SMSIADDRCFGH_HBPPN	FIELD(11,0)	/* High base PPN */
#define SMSIADDRCFGH_LHXS	FIELD(22,20)	/* Low hart index shift */

/* Interrupt Pending Control (using individual set/clear registers) */
/* 32 register group (32 * 32 = 1024), bit 0 unused (source 0 reserved) */
#define SETIP(_src)		REG32_BITMAP(0x1C00, _src)
#define CLRIP(_src)		REG32_BITMAP(0x1D00, _src)

/* Same but by source id */
#define SETIPNUM		REG32(0x1CDC)
#define CLRIPNUM		REG32(0x1DDC)

/* Interrupt Enable Control (using individual set/clear registers) */
#define SETIE(_src)		REG32_BITMAP(0x1E00, _src)
#define CLRIE(_src)		REG32_BITMAP(0x1F00, _src)

#define SETIENUM		REG32(0x1EDC)
#define CLRIENUM		REG32(0x1FDC)

/* SETIPNUM_BE/LE, same as SETIPNUM but with fixed endianess, used as MSI write ports */
#define SETIPNUM_LE		REG32(0x2000)
#define SETIPNUM_BE		REG32(0x2004)

/* Generate MSI for APLIC/IMSIC synchronization (see Section 4.9.3) */
#define GENMSI			REG32(0x3000)
#define GENMSI_EIID		FIELD(10,0)	/* External Interrupt Identity (on the hart size) */
#define GENMSI_BUSY		BIT(12)		/* Busy indicator */
#define GENMSI_HIDX		FIELD(31,18)	/* Hart index */

/* Target registers - array of 1024 registers at 4-byte stride
 * Controls routing for each source */
#define TARGET(_src)		REG32_ARRAY(0x3004, (_src) - 1)
/* When source is configured for direct delivery */
#define TARGET_IPRIO		FIELD(7,0)	/* Interrupt priority */
/* When source is configured for MSI delivery */
#define TARGET_EIID		FIELD(10,0)	/* External Interrupt Identity */
#define TARGET_GIDX		FIELD(17,12)	/* Guest index */
/* For both modes */
#define TARGET_HIDX		FIELD(31,18)	/* Hart index */

/*
 * IDC (Interrupt Delivery Control) - Direct Mode Only
 * One 32-byte block per IDC context
 */
#define IDC_OFFSET		0x4000
#define IDC_STRIDE		32

#define IDC_IDELIVERY(_idc)	REG32_BLOCK(IDC_OFFSET, _idc, IDC_STRIDE, 0x00)	/* Enable interrupt delivery */
#define IDC_IFORCE(_idc)	REG32_BLOCK(IDC_OFFSET, _idc, IDC_STRIDE, 0x04)	/* Force interrupt 0 */
#define IDC_ITHRESHOLD(_idc)	REG32_BLOCK(IDC_OFFSET, _idc, IDC_STRIDE, 0x08)	/* Priority threshold */
#define IDC_TOPI(_idc)		REG32_BLOCK(IDC_OFFSET, _idc, IDC_STRIDE, 0x18)	/* Top pending interrupt (RO) */
#define IDC_CLAIMI(_idc)	REG32_BLOCK(IDC_OFFSET, _idc, IDC_STRIDE, 0x1c)	/* Claim interrupt (atomically claims and clears) */

#define IDC_TOPI_IPRIO		FIELD(7,0)	/* Interrupt priority */
#define IDC_TOPI_ID		FIELD(25,16)	/* Interrupt Identity (source number) */

/*********\
* Helpers *
\*********/

#if !APLIC_USES_IMSIC
/* Priorities are opposite of PLIC, lowest number is highest priority */
static const uint8_t aplic_max_priority = 1;
static uint8_t aplic_min_priority = 1;

/* Map enum irq_priority to APLIC hardware priority value
 * APLIC: 0 = disabled, 1 = highest priority, higher values = lower priority */
static uint8_t
aplic_map_priority(enum irq_priority priority)
{
	switch (priority) {
	case IRQ_PRIORITY_DISABLED:
		return 0;
	case IRQ_PRIORITY_LOW:
		return aplic_min_priority;
	case IRQ_PRIORITY_MEDIUM:
		return aplic_max_priority + ((aplic_min_priority - aplic_max_priority) / 2);
	case IRQ_PRIORITY_HIGH:
		return aplic_max_priority;
	default:
		return 0;
	}
}
#endif

/**************\
* Entry points *
\**************/

/* Validate IRQ mappings and initialize APLIC */
int __attribute__((weak))
irq_init(void)
{
	DBG("APLIC irq_init starting (mode=%s)\n", APLIC_USES_IMSIC ? "IMSIC" : "direct");

	/*
	 * Initialize APLIC to clean state
	 * First, disable the domain by clearing domaincfg
	 */
	write32(DOMAINCFG, 0);

	/*
	 * Configure all interrupt sources to inactive state, and
	 * disable delegation (D = 0).
	 */
	DBG("Configuring %d interrupt sources...\n", PLAT_NUM_IRQ_SOURCES);
	for (int i = 1; i <= PLAT_NUM_IRQ_SOURCES; i++)
		write32(SOURCECFG(i), FIELD_PREP(SOURCECFG_SM, SM_INACTIVE));

	/*
	 * Disable all interrupt sources using CLRIE registers.
	 * (better 32 writes than 1024 through CLRIENUM)
	 */
	for (int i = 0; i < 32; i++)
		write32(CLRIE(i * 32), 0xFFFFFFFF);

	/*
	 * Initialize delivery mode specific configuration
	 */
	#if APLIC_USES_IMSIC
		/* MSI delivery mode: Configure MMSIADDRCFG registers */
		const uint64_t imsic_base = PLAT_IMSIC_BASE;

		/* Read current values */
		uint32_t mmsiaddrcfg = read32(MMSIADDRCFG);
		uint32_t mmsiaddrcfgh = read32(MMSIADDRCFGH);
		bool locked = mmsiaddrcfgh & MMSIADDRCFGH_L;

		/* If non-zero, decode and log current configuration */
		uint64_t cur_base = 0;
		if (mmsiaddrcfg != 0) {
			uint32_t cur_lbppn = FIELD_GET(MMSIADDRCFG_LBPPN, mmsiaddrcfg);
			uint32_t cur_hbppn = FIELD_GET(MMSIADDRCFGH_HBPPN, mmsiaddrcfgh);
			cur_base = ((uint64_t)cur_hbppn << 44) | ((uint64_t)cur_lbppn << 12);
			DBG("Current msiaddr: 0x%lx\n", cur_base);
		}

		if (cur_base > 0) {
			if(cur_base != imsic_base) {
				WRN("APLIC is configured with a different imsic base address than PLAT_IMSIC_BASE: 0x%lx\n", cur_base);
				if(locked) {
					ERR("MMSIADDRCFG is locked, cannot initialize APLIC !\n");
					return -EINVAL;
				}
			} else {
				DBG("APLIC is preconfigured with the correct imsic base, will not update MMSIADDRCFG\n");
				goto skip_mmiaddrcfg;
			}
		}

		if (locked) {
			ERR("MMSIADDRCFG is locked, cannot configure!\n");
			return -EINVAL;
		}

		/* Address calculation for hart_idx:
		 *   low_bits  = hart_idx & ((1 << LHXW) - 1)  // Index within group
		 *   high_bits = (hart_idx >> LHXW) & ((1 << HHXW) - 1)  // Group number
		 *   addr = base_addr | (low_bits << (LHXS + 12)) | (high_bits << (HHXS + 12))
		 *
		 * Unless APLIC is already programmed on reset, we assume the same memory layout
		 * we do on ipi_imsic.c.
		 *
		 * We use:
		 *   LHXW = 12 (12-bit low hart index, supports 4096 harts in one group)
		 *   LHXS = 0  (low bits placed at bit 12, giving 0x1000 stride)
		 *   HHXW = 0  (no group bits needed for small systems)
		 *   HHXS = 0  (not used)
		 *
		 * This gives: addr = base_addr | (hart_idx << 12)
		 */
		const uint32_t expected_lbppn = (uint32_t)(imsic_base >> 12);
		const uint32_t expected_hbppn = (uint32_t)(imsic_base >> 44);
		const uint32_t expected_mmsiaddrcfg = FIELD_PREP(MMSIADDRCFG_LBPPN, expected_lbppn);
		const uint32_t expected_mmsiaddrcfgh = FIELD_PREP(MMSIADDRCFGH_HBPPN, expected_hbppn) |
							FIELD_PREP(MMSIADDRCFGH_LHXW, 12) |
						 	FIELD_PREP(MMSIADDRCFGH_LHXS, 0);

		/* Write and lock */
		write32(MMSIADDRCFG, expected_mmsiaddrcfg);
		write32(MMSIADDRCFGH, expected_mmsiaddrcfgh | MMSIADDRCFGH_L);

		/* Read back to verify */
		uint32_t rb_cfg = read32(MMSIADDRCFG);
		uint32_t rb_cfgh = read32(MMSIADDRCFGH);

		if (rb_cfg != expected_mmsiaddrcfg ||
		    (rb_cfgh & ~MMSIADDRCFGH_L) != expected_mmsiaddrcfgh ||
		    !(rb_cfgh & MMSIADDRCFGH_L)) {
			ERR("Failed to configure MMSIADDRCFG!\n");
			ERR("  Wrote:     CFG=0x%08x, CFGH=0x%08x\n",
			    expected_mmsiaddrcfg, expected_mmsiaddrcfgh | MMSIADDRCFGH_L);
			ERR("  Read back: CFG=0x%08x, CFGH=0x%08x\n", rb_cfg, rb_cfgh);
			return -EINVAL;
		}

		DBG("MMSIADDRCFG configured: base=0x%lx, stride=0x1000, locked\n", imsic_base);

 skip_mmiaddrcfg:
		 /* Now enable the domain */
		write32(DOMAINCFG, DOMAINCFG_IE | DOMAINCFG_DM);
	#else
		/* Determine IPRIOLEN by enabling source 1 and probing target[1].iprio */
		write32(SOURCECFG(1), FIELD_PREP(SOURCECFG_SM, SM_DETACHED));
		const uint32_t target_val = FIELD_PREP(TARGET_IPRIO, 0xFF) |
					    FIELD_PREP(TARGET_HIDX, 0);
		write32(TARGET(1), target_val);
		uint32_t target_val_rb = read32(TARGET(1));
		aplic_min_priority = FIELD_GET(TARGET_IPRIO, target_val_rb);
		DBG("Got minimum priority: %i\n", aplic_min_priority);
		if (!aplic_min_priority)
			aplic_min_priority = aplic_max_priority;
		write32(TARGET(1), 0);
		write32(SOURCECFG(1), FIELD_PREP(SOURCECFG_SM, SM_INACTIVE));

		/* Direct delivery mode: Initialize all IDC contexts */
		DBG("Initializing %d IDC contexts...\n", PLAT_MAX_HARTS);
		for (int i = 0; i < PLAT_MAX_HARTS; i++) {
			uint16_t idc = platform_intc_map[i].target.idc_idx;
			DBG("  IDC %d: delivery=0, threshold=%d\n", idc, aplic_min_priority);
			write32(IDC_IDELIVERY(idc), 0);
			write32(IDC_ITHRESHOLD(idc), aplic_min_priority);
		}

		/* Enable the domain in direct delivery mode
		 * IE=1 (enable), DM=0 (direct mode), BE=0 (little endian) */
		DBG("Enabling APLIC domain in direct mode\n");
		write32(DOMAINCFG, DOMAINCFG_IE);
	#endif
	return 0;
}

/* Enable interrupt delivery for source_id */
void __attribute__((weak))
irq_source_enable(uint16_t source_id)
{
	DBG("Enabling interrupt for source: %i\n", source_id);

	if (source_id == 0 || source_id > PLAT_NUM_IRQ_SOURCES) {
		ERR("Invalid source ID: %i (valid range: 1-%i)\n", source_id, PLAT_NUM_IRQ_SOURCES);
		return;
	}

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL)
		return;

	int target_idx = irq_get_target_idx_for_hart(irq_sm->target_hart);
	if (target_idx < 0) {
		ERR("Attempted to enable interrupt for unmapped target (source: %i, target: %i)\n",
		    source_id, irq_sm->target_hart);
		return;
	}

	/*
	 * Configure the interrupt source based on trigger mode flags
	 */
	uint32_t source_mode = irq_sm->flags;
	write32(SOURCECFG(source_id), FIELD_PREP(SOURCECFG_SM, source_mode));
	DBG("Wrote SOURCECFG[%i] with SM=%i\n", source_id, source_mode);

	/* Read back to confirm */
	uint32_t sourcecfg_rb = read32(SOURCECFG(source_id));
	DBG("SOURCECFG[%i] readback = 0x%08x (SM=%i)\n", source_id, sourcecfg_rb, sourcecfg_rb & 0x7);

	#if APLIC_USES_IMSIC
		/*
		 * MSI delivery mode: Set target register with hart index and EEID
		 * target_idx is the APLIC hart index from the platform mapping
		 */
		DBG("Interrupt source %i mapped to hart index %i\n", source_id, target_idx);
		uint32_t target_val = FIELD_PREP(TARGET_EIID, source_id) |
				      FIELD_PREP(TARGET_HIDX, target_idx);
		write32(TARGET(source_id), target_val);

		/* Also enable the eeid on target hart's IMSIC */
		hart_configure_imsic_eiid(irq_sm->target_hart, source_id, 1);
	#else
		/*
		 * Direct delivery mode: Set target register with IDC index and priority
		 * target_idx is the IDC index from the platform mapping
		 * Note: APLIC priorities are reversed from PLIC (lower = higher priority)
		 */
		DBG("Interrupt source %i mapped to IDC %i\n", source_id, target_idx);
		uint32_t target_val = FIELD_PREP(TARGET_IPRIO, aplic_map_priority(irq_sm->priority)) |
				      FIELD_PREP(TARGET_HIDX, target_idx);
		write32(TARGET(source_id), target_val);

		/* Enable interrupt delivery for this IDC if not already enabled */
		write32(IDC_IDELIVERY(target_idx), 1);
	#endif

	/* Enable the interrupt source using SETIENUM register */
	DBG("Writing SETIENUM with source_id=%i\n", source_id);
	write32(SETIENUM, source_id);
	DBG("SETIENUM write completed\n");
}

/* Disable interrupt delivery for source_id */
void __attribute__((weak))
irq_source_disable(uint16_t source_id)
{
	DBG("Disabling interrupt for source: %i\n", source_id);

	if (source_id == 0 || source_id > PLAT_NUM_IRQ_SOURCES) {
		ERR("Invalid source ID: %i (valid range: 1-%i)\n", source_id, PLAT_NUM_IRQ_SOURCES);
		return;
	}

	/* Disable the interrupt source using CLRIENUM */
	write32(CLRIENUM, source_id);

	/* Set source configuration to inactive */
	write32(SOURCECFG(source_id), FIELD_PREP(SOURCECFG_SM, SM_INACTIVE));

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL)
		return;

	#if !APLIC_USES_IMSIC
		/* In direct mode, also clear the target priority */
		int target_idx = irq_get_target_idx_for_hart(irq_sm->target_hart);
		if (target_idx < 0)
			return;

		uint32_t target_val = FIELD_PREP(TARGET_IPRIO, aplic_max_priority) |
				      FIELD_PREP(TARGET_HIDX, target_idx);
		write32(TARGET(source_id), target_val);
	#else
		/* Also disable the eeid on target hart's IMSIC */
		hart_configure_imsic_eiid(irq_sm->target_hart, source_id, 0);
	#endif
}

/*
 * Main interrupt dispatch handler - called from trap handler
 * This is called when an external interrupt is pending
 *
 * APLIC direct mode uses CLAIMI register to atomically claim
 * and clear pending interrupts. Unlike PLIC, there's no separate
 * completion step - the interrupt is completed when claimed.
 */
void __attribute__((weak))
irq_dispatch(uint16_t eiid)
{
	struct hart_state* hs = hart_get_hstate_self();
	if (hs->irq_map_idx < 0) {
		ERR("Got interrupt on uninitialized hart (id: %li, idx: %i)\n", hs->hart_id, hs->hart_idx);
		return;
	}

	#if !APLIC_USES_IMSIC
		uint16_t idc = platform_intc_map[hs->irq_map_idx].target.idc_idx;
		/*
		 * Claim the interrupt from APLIC
		 * This returns the highest priority pending interrupt source
		 * and atomically clears the corresponding pending bit
		 */
		uint32_t claimi = read32(IDC_CLAIMI(idc));
		uint16_t source_id = (uint16_t)FIELD_GET(IDC_TOPI_ID, claimi);
	#else
		uint16_t source_id = eiid;
	#endif

	DBG("Claimed interrupt source: %i\n", source_id);

	const struct irq_source_mapping *irq_sm = irq_get_srcmap(source_id);
	if (irq_sm == NULL) {
		ERR("Got interrupt from unmapped source: %i\n", source_id);
		return;
	}

	DBG("Calling handler for interrupt source: %i\n", source_id);

	/* Got mapping, call the associated interrupt handler */
	irq_sm->handler((uint16_t) source_id);
	return;
}

#endif /* defined(PLAT_HAS_APLIC) */
