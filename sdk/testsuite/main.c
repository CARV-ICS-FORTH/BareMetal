/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <test_framework.h>		/* For test registration */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/hart.h>	/* For hart_get_hstate_self() */
#include <platform/riscv/csr.h>		/* For pause() */
#include <stdio.h>			/* For getchar(), EOF */
#include <errno.h>			/* For EAGAIN */

static void
print_category_menu(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	ANN("\n---=== TestSuite Menu ===---\n");
	INF("Welcome from hart: %i, hart_id: %li\n", hs->hart_idx, hs->hart_id);
	INF("Select test category:\n");
	INF("\t1 -> YaLibC tests\n");
	INF("\t2 -> Platform tests\n");
}

static void
print_test_menu(struct test_entry *tests_start, struct test_entry *tests_end, const char *category)
{
	size_t num_tests = tests_end - tests_start;

	ANN("\n---=== %s ===---\n", category);
	INF("Select a test:\n");

	for (size_t i = 0; i < num_tests; i++) {
		INF("\t%zu -> %s\n", i + 1, tests_start[i].description);
	}
	INF("\t0 -> Back to category menu\n");
}

static int
run_test_category(struct test_entry *tests_start, struct test_entry *tests_end, const char *category)
{
	size_t num_tests = tests_end - tests_start;
	int total_failures = 0;

	while (1) {
		print_test_menu(tests_start, tests_end, category);

		int input = getchar();
		if (input == EOF) {
			pause();
			continue;
		}

		if (input == '0') {
			return total_failures;
		}

		int test_num = input - '1';
		if (test_num >= 0 && test_num < num_tests) {
			int result = tests_start[test_num].test_fn();
			if (result != 0) {
				total_failures += result;
				INF("\nTest failed with %d errors\n", result);
			}
			INF("\nTotal failures so far: %i\n", total_failures);
		} else {
			INF("Invalid selection. Please try again.\n");
		}

		pause();
	}
}

void
main(void)
{
	int total_failures = 0;
	DBG("At main\n");

	while (1) {
		print_category_menu();

		int input = getchar();
		if (input == EOF) {
			pause();
			continue;
		}

		switch (input) {
			case '1':
				total_failures += run_test_category(
					__start_rodata_tests_yalibc,
					__stop_rodata_tests_yalibc,
					"YaLibC Tests"
				);
				INF("\nTotal failures across all tests: %i\n", total_failures);
				break;
			case '2':
				total_failures += run_test_category(
					__start_rodata_tests_platform,
					__stop_rodata_tests_platform,
					"Platform Tests"
				);
				INF("\nTotal failures across all tests: %i\n", total_failures);
				break;
			case EOF:
				break;
			default:
				INF("Invalid selection. Please try again.\n");
				break;
		}
		pause();
	}
	INF("\n---===DONE===---\n");
}