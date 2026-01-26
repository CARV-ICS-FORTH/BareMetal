/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_* constants */

/* Only compile this test if APLIC is present */
#if defined(PLAT_HAS_APLIC)

#include <stdint.h>			/* For uintptr_t */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/mmio.h>	/* For read32/write32 */
#include <platform/riscv/hart.h>	/* For hart interrupt control */
#include <platform/interfaces/irq.h>	/* For irq_init, irq_source_enable */
#include <platform/interfaces/timer.h>	/* For timer_get_num_ticks */
#include <test_framework.h>		/* For test registration macros */

/* Define register base for register.h macros */
#define _REGBASE PLAT_APLIC_BASE
#include <platform/utils/register.h>

/* APLIC register offsets */
#define APLIC_DOMAINCFG		REG32(0x0000)
#define APLIC_SOURCECFG(src)	REG32_ARRAY(0x0004, (src) - 1)
#define APLIC_SETIP(src)	REG32_BITMAP(0x1C00, src)
#define APLIC_IN_CLRIP(src)	REG32_BITMAP(0x1D00, src)
#define APLIC_CLRIPNUM		REG32(0x1DDC)
#define APLIC_SETIENUM		REG32(0x1EDC)
#define APLIC_SETIE(src)	REG32_BITMAP(0x1E00, src)
#define APLIC_SETIPNUM_LE	REG32(0x2000)
#define APLIC_GENMSI		REG32(0x3000)

/* Use a high-numbered source as our test/detached source to avoid conflicts */
#define TEST_SOURCE_ID		(PLAT_NUM_IRQ_SOURCES - 1)

/* Track whether interrupt was delivered */
static volatile uint8_t interrupt_received = 0;
static volatile uint16_t received_source = 0;

/* Interrupt handler for test source */
static void
aplic_test_handler(uint16_t source)
{
	interrupt_received = 1;
	received_source = source;
}

/* Register the interrupt source as detached (SM_DETACHED = 1)
 * Target it to hart index 0 (boot hart) */
REGISTER_IRQ_SOURCE(aplic_test, {
	.source.wire_id = TEST_SOURCE_ID,
	.handler = aplic_test_handler,
	.target_hart = 0,
	.priority = IRQ_PRIORITY_HIGH,
	.flags = IRQ_TRIGGER_DETACHED,
});

static int
test_aplic_pending(void)
{
	ANN("\n---=== APLIC Interrupt Pending Test ===---\n");

	INF("Testing APLIC interrupt pending bit control (source %u)\n", TEST_SOURCE_ID);
	INF("This test verifies APLIC can properly set/clear interrupt pending bits when triggered via SETIPNUM_LE\n");

	INF("Step 0: Check APLIC's configuration for root domain\n");
	uint32_t domaincfg = read32(APLIC_DOMAINCFG);
	uint8_t is_msi_mode = !!(domaincfg & (1<<2));
	INF("  DOMAINCFG = 0x%08x (IE=%u, DM=%u, BE=%u)\n",
	    domaincfg, !!(domaincfg & (1<<8)), is_msi_mode, !!(domaincfg & (1<<0)));
	INF("  Detected mode: %s\n", is_msi_mode ? "MSI (forwards to IMSIC)" : "Direct (IDC delivery)");

	INF("Step 1: Manually enable test source\n");
	/* Set source mode to detached (SM=1) */
	write32(APLIC_SOURCECFG(TEST_SOURCE_ID), 0x00000001);
	uint32_t sourcecfg_rb = read32(APLIC_SOURCECFG(TEST_SOURCE_ID));
	INF("  SOURCECFG[%u] = 0x%08x (SM=%u)\n", TEST_SOURCE_ID, sourcecfg_rb, sourcecfg_rb & 0x7);

	/* In MSI mode, we must NOT enable the source, otherwise the pending bit
	 * gets cleared immediately when the MSI is sent. In direct mode, we can
	 * enable it since IDC delivery is disabled for that source.*/
	if (!is_msi_mode) {
		/* Enable the interrupt source via SETIENUM (direct mode only) */
		write32(APLIC_SETIENUM, TEST_SOURCE_ID);
		INF("  Wrote SETIENUM with source_id=%u (direct mode)\n", TEST_SOURCE_ID);
	} else {
		INF("  Skipping SETIENUM (MSI mode - would auto-clear pending bit)\n");
	}

	/* Calculate which bit position within the register
	 * Per APLIC spec: source i maps to bit (i mod 32) in register setie[i/32]
	 * Source ID 0 is invalid/reserved and bit 0 is read-only 0.
	 * The REG32_BITMAP macro handles register index calculation. */
	uint32_t bit_pos = TEST_SOURCE_ID % 32;  /* Which bit in that register */

	/* Verify the enable bit state */
	uint32_t setie_val = read32(APLIC_SETIE(TEST_SOURCE_ID));
	uint8_t is_enabled = !!(setie_val & (1 << bit_pos));
	INF("  SETIE = 0x%08x (bit %u %s)\n", setie_val, bit_pos,
	    is_enabled ? "SET" : "CLEAR");

	if (is_msi_mode && is_enabled) {
		WRN("  Warning: Enable bit should be clear in MSI mode test\n");
	} else if (!is_msi_mode && !is_enabled) {
		WRN("  Warning: Enable bit should be set in direct mode test\n");
	}

	INF("\nStep 2: Trigger interrupt via SETIPNUM_LE register\n");
	INF("  Writing %u to SETIPNUM_LE\n", TEST_SOURCE_ID);
	write32(APLIC_SETIPNUM_LE, TEST_SOURCE_ID);

	INF("\nStep 3: Check if interrupt is pending in APLIC\n");

	uint32_t setip_val = read32(APLIC_SETIP(TEST_SOURCE_ID));
	uint32_t in_clrip_val = read32(APLIC_IN_CLRIP(TEST_SOURCE_ID));
	uint8_t is_pending = !!(setip_val & (1 << bit_pos));
	INF("  SETIP = 0x%08x (bit %u %s)\n", setip_val, bit_pos,
	    is_pending ? "SET" : "CLEAR");
	INF("  IN_CLRIP = 0x%08x (rectified input state)\n", in_clrip_val);

	/* In direct mode: pending bit should be set (IDC delivery disabled)
	 * In MSI mode with enable=0: pending bit should be set (no MSI sent)
	 * In MSI mode with enable=1: pending bit cleared after MSI sent */
	if (!is_msi_mode) {
		/* Direct mode: expect pending bit to be set */
		if (!is_pending) {
			ERR("  -> FAIL: Interrupt NOT pending in direct mode (bit %u not set)\n", bit_pos);
			INF("\nPress a key to continue...\n");
			return -1;
		}
		INF("  -> SUCCESS: Interrupt IS pending in direct mode (bit %u set)\n", bit_pos);
	} else {
		/* MSI mode: pending bit should be set since we didn't enable the source */
		if (!is_pending) {
			ERR("  -> FAIL: Interrupt NOT pending in MSI mode (bit %u not set)\n", bit_pos);
			ERR("         Expected: pending bit set because source not enabled\n");
			INF("\nPress a key to continue...\n");
			return -1;
		}
		INF("  -> SUCCESS: Interrupt IS pending in MSI mode (bit %u set)\n", bit_pos);
		INF("         (No MSI sent because source was not enabled)\n");
	}

	INF("\nStep 4: Clear the pending interrupt via CLRIPNUM\n");
	write32(APLIC_CLRIPNUM, TEST_SOURCE_ID);
	INF("  Wrote CLRIPNUM with source_id=%u\n", TEST_SOURCE_ID);

	/* Verify it's cleared */
	setip_val = read32(APLIC_SETIP(TEST_SOURCE_ID));
	INF("  SETIP after clear = 0x%08x\n", setip_val);

	if (setip_val & (1 << bit_pos)) {
		ERR("  -> FAIL: Interrupt still pending after CLRIPNUM (bit %u still set)\n", bit_pos);
		INF("\nPress a key to continue...\n");
		return -1;
	}

	INF("  -> SUCCESS: Interrupt successfully cleared\n");
	INF("\n=== TEST PASSED ===\n");
	if (!is_msi_mode) {
		INF("APLIC direct mode: Can set/clear interrupt pending bits correctly\n");
		INF("(IDC delivery was disabled to observe SETIP state)\n");
	} else {
		INF("APLIC MSI mode: Can set/clear interrupt pending bits correctly\n");
		INF("(Source was not enabled to prevent auto-clear after MSI)\n");
	}
	INF("\nPress a key to continue...\n");

	return 0;
}

REGISTER_PLATFORM_TEST("APLIC interrupt pending test", test_aplic_pending);

static int
test_aplic_delivery(void)
{
	ANN("\n---=== APLIC Interrupt Delivery Test ===---\n");

	INF("Testing APLIC interrupt delivery to hart (source %u)\n", TEST_SOURCE_ID);
	INF("This test verifies APLIC can deliver interrupts through the full interrupt path\n");

	INF("Step 0: Check APLIC mode\n");
	uint32_t domaincfg = read32(APLIC_DOMAINCFG);
	uint8_t is_msi_mode = !!(domaincfg & (1<<2));
	INF("  Mode: %s\n", is_msi_mode ? "MSI (forwards to IMSIC)" : "Direct (IDC delivery)");

	INF("\nStep 1: Reset handler state\n");
	interrupt_received = 0;
	received_source = 0;
	INF("  Handler flag cleared\n");

	INF("\nStep 2: Enable external interrupts\n");
	struct hart_state *hs = hart_get_hstate_self();
	INF("  Current hart: hart_id=%llu, hart_idx=%u\n", hs->hart_id, hs->hart_idx);
	hart_enable_intr(INTR_MACHINE_EXTERNAL);
	INF("  Enabled machine external interrupts (mie.MEIE=1)\n");

#if defined(PLAT_IMSIC_BASE) && (PLAT_IMSIC_BASE > 0)
	if (is_msi_mode) {
		/* Verify IMSIC configuration via indirect CSRs, note that this will work
		 * as long as the test runs on hart idx 0 (same as the target_hart of TEST_SOURCE_ID) */
		csr_write(CSR_MISELECT, 0x70);
		uint32_t eidelivery = csr_read(CSR_MIREG);
		csr_write(CSR_MISELECT, 0x72);
		uint32_t eithreshold = csr_read(CSR_MIREG);
		INF("  IMSIC: EIDELIVERY=0x%x, EITHRESHOLD=0x%x\n", eidelivery, eithreshold);
	}
#endif

	INF("\nStep 3: Enable test source via IRQ interface\n");
	INF("  Source %u registered with target_hart=%u (from REGISTER_IRQ_SOURCE)\n",
	    TEST_SOURCE_ID, 0);
	irq_source_enable(TEST_SOURCE_ID);
	INF("  Enabled source %u\n", TEST_SOURCE_ID);

#if defined(PLAT_IMSIC_BASE) && (PLAT_IMSIC_BASE > 0)
	if (is_msi_mode) {
		/* Verify EIE bit is set for this interrupt via indirect CSRs */
		uint32_t eie_reg = 0xC0 + (TEST_SOURCE_ID / 64);
		eie_reg += (eie_reg & 1);
		uint32_t eie_bit = TEST_SOURCE_ID % 64;
		csr_write(CSR_MISELECT, eie_reg);
		uint64_t eie_val = csr_read(CSR_MIREG);
		INF("  IMSIC: EIE via iselect 0x%x = 0x%llx (bit %u %s)\n",
		    eie_reg, eie_val, eie_bit,
		    (eie_val & (1ULL << eie_bit)) ? "SET" : "CLEAR");
	}
#endif

	INF("\nStep 4: Trigger interrupt via SETIPNUM_LE\n");
	INF("  Writing %u to SETIPNUM_LE\n", TEST_SOURCE_ID);

	uint64_t start_cycles = timer_get_num_ticks(PLAT_TIMER_CYCLES);
	write32(APLIC_SETIPNUM_LE, TEST_SOURCE_ID);

	/* Wait for interrupt delivery using wfi */
	INF("  Waiting for interrupt delivery using wfi\n");
	for (int i = 0; i < 10 && !interrupt_received; i++) {
		wfi();
		/* In MSI mode, trigger GENMSI to ensure MSI write completes */
		if (is_msi_mode && !interrupt_received)
			write32(APLIC_GENMSI, 0);
	}

	uint64_t end_cycles = timer_get_num_ticks(PLAT_TIMER_CYCLES);
	uint64_t elapsed_cycles = end_cycles - start_cycles;

	INF("\nStep 5: Verify interrupt was delivered\n");
	if (!interrupt_received) {
		ERR("  -> FAIL: Interrupt was NOT delivered to handler\n");
		INF("         Handler flag: %u\n", interrupt_received);
		INF("         Cycles elapsed: %llu\n", elapsed_cycles);

		/* Cleanup */
		irq_source_disable(TEST_SOURCE_ID);
		hart_disable_intr(INTR_MACHINE_EXTERNAL);

		INF("\nPress a key to continue...\n");
		return -1;
	}

	INF("  -> SUCCESS: Interrupt delivered to handler!\n");
	INF("         Handler called with source_id=%u (expected %u)\n",
	    received_source, TEST_SOURCE_ID);
	INF("         Interrupt latency: %llu cycles\n", elapsed_cycles);

	if (received_source != TEST_SOURCE_ID) {
		ERR("  -> FAIL: Source ID mismatch (got %u, expected %u)\n",
		    received_source, TEST_SOURCE_ID);

		/* Cleanup */
		irq_source_disable(TEST_SOURCE_ID);
		hart_disable_intr(INTR_MACHINE_EXTERNAL);

		INF("\nPress a key to continue...\n");
		return -1;
	}

	INF("\nStep 6: Cleanup - disable test source\n");
	irq_source_disable(TEST_SOURCE_ID);
	hart_disable_intr(INTR_MACHINE_EXTERNAL);
	INF("  Disabled test source and external interrupt bit\n");

	INF("\n=== TEST PASSED ===\n");
	INF("APLIC successfully delivered interrupt through full interrupt path\n");
	INF("\nPress a key to continue...\n");

	return 0;
}

REGISTER_PLATFORM_TEST("APLIC interrupt delivery test", test_aplic_delivery);

#endif /* defined(PLAT_HAS_APLIC) */
