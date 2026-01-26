/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/csr.h>		/* For pause() */
#include <platform/interfaces/ipi.h>	/* For ipi_self/send() */
#include <test_framework.h>		/* For test registration macros */

static int
test_ipi(void)
{
	ANN("\n---=== IPI Test ===---\n");
	struct hart_state* this_hs = hart_get_hstate_self();
	int num_harts = hart_get_count();
	if (num_harts == 1) {
		WRN("Only one hart came up, won't send IPI to self\n");
		INF("Press a key to continue...\n");
		return 1;
	}

	INF("Choose a hart to wake up...\n");
	int i = 0;
	for (i = 0; i < num_harts - 1; i++)
		INF("%i, ", i);
	INF("%i\n", i);

	while (1 == 1) {
		INF("Input: ");
		int num = 0;
		int input = 0;
		while (input != '\r' && input != ' ') {
			input = getchar();
			pause();
			if (input > '9' || input < '0')
				continue;
			num = num * 10 + (input - '0');
			putchar(input);
		}
		putchar('\n');

		DBG("Got hart_idx: %i\n", num);
		if (num == this_hs->hart_idx) {
			INF("Sending IPI to self\n");
			ipi_self(IPI_WAKEUP);
			break;
		}
		if (num < num_harts) {
			struct hart_state* hs = hart_get_hstate_by_idx(num);
			INF("Waking up hart %i\n", num);
			ipi_send(hs, IPI_WAKEUP);
			break;
		}
		ERR("Invalid hart index: %i, retry...\n", num);
	}

	INF("Press a key to continue...\n");
	return 0;
}

REGISTER_PLATFORM_TEST("IPI (Inter-Processor Interrupt) test", test_ipi);