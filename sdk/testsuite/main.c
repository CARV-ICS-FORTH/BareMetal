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
#include <stddef.h>			/* For size_t */
#include <stdio.h>			/* For getchar(), EOF */

/*
 * A group is one array of entries (one linker section). An entry is a probe
 * (detect & report, no verdict) or a test (asserts, pass/fail) -- see
 * test_framework.h. A category groups one or more related groups together;
 * categories with a single group skip straight to their list, while multi-group
 * categories (e.g. Hart -> priv/unpriv) show a sub-menu. Every list offers
 * running an individual entry or a cumulative run-all summary.
 */
struct test_group {
	const char *name;
	const struct test_entry *start;
	const struct test_entry *stop;
};

struct test_category {
	const char *name;
	const struct test_group *groups;
	size_t num_groups;
};

static const struct test_group hart_groups[] = {
	{ "Privileged",   __start_rodata_menu_hart_priv,   __stop_rodata_menu_hart_priv },
	{ "Unprivileged", __start_rodata_menu_hart_unpriv, __stop_rodata_menu_hart_unpriv },
};

static const struct test_group platform_groups[] = {
	{ "Platform tests", __start_rodata_menu_platform, __stop_rodata_menu_platform },
};

static const struct test_group yalibc_groups[] = {
	{ "YaLibC tests", __start_rodata_menu_yalibc, __stop_rodata_menu_yalibc },
};

static const struct test_category categories[] = {
	{ "Hart Tests",     hart_groups,     sizeof(hart_groups) / sizeof(hart_groups[0]) },
	{ "Platform Tests", platform_groups, sizeof(platform_groups) / sizeof(platform_groups[0]) },
	{ "YaLibC Tests",   yalibc_groups,   sizeof(yalibc_groups) / sizeof(yalibc_groups[0]) },
};

/* Run every entry in a group back-to-back: tests get a pass/fail verdict and
 * feed the failure tally, probes just report their output. */
static int
summarize_group(const struct test_group *g)
{
	size_t num_entries = g->stop - g->start;
	int total_failures = 0;
	unsigned tests = 0, probes = 0;

	ANN("\n---=== %s: Run all ===---\n", g->name);
	for (size_t i = 0; i < num_entries; i++) {
		const struct test_entry *e = &g->start[i];
		int result = e->test_fn();

		/* Probes just report (output printed by the entry itself); only
		 * tests carry a pass/fail verdict and feed the failure tally. */
		if (menu_entry_is_probe(e)) {
			probes++;
			continue;
		}
		tests++;
		total_failures += result;
		if (result == 0)
			INF("[PASS] %s\n", menu_entry_name(e));
		else
			INF("[FAIL] %s (%d errors)\n", menu_entry_name(e), result);
	}
	INF("\n%s summary: %u tests, %d failures", g->name, tests, total_failures);
	if (probes)
		INF(" (%u probes)", probes);
	INF("\n");
	return total_failures;
}

/* Interactive list for a single group: run one test, run all, or go back. */
static int
run_group(const struct test_group *g)
{
	size_t num_entries = g->stop - g->start;
	int total_failures = 0;

	while (1) {
		ANN("\n---=== %s ===---\n", g->name);
		INF("Select:\n");
		for (size_t i = 0; i < num_entries; i++)
			INF("\t%zu -> %s\n", i + 1, menu_entry_name(&g->start[i]));
		INF("\ta -> Run all (summary)\n");
		INF("\t0 -> Back\n");

		int input = getchar();
		if (input == EOF) {
			pause();
			continue;
		}

		if (input == '0')
			return total_failures;

		if (input == 'a' || input == 'A') {
			total_failures += summarize_group(g);
			pause();
			continue;
		}

		int entry_num = input - '1';
		if (entry_num >= 0 && (size_t)entry_num < num_entries) {
			const struct test_entry *e = &g->start[entry_num];
			int result = e->test_fn();

			/* Probes just report; only tests carry a verdict. */
			if (!menu_entry_is_probe(e)) {
				if (result != 0) {
					total_failures += result;
					INF("\nTest failed with %d errors\n", result);
				}
				INF("\nTotal failures so far: %i\n", total_failures);
			}
		} else {
			INF("Invalid selection. Please try again.\n");
		}

		pause();
	}
}

/* A category: single-group ones go straight to the test list; multi-group ones
 * show a sub-menu of groups plus a cumulative run-all across every group. */
static int
run_category(const struct test_category *c)
{
	if (c->num_groups == 1)
		return run_group(&c->groups[0]);

	int total_failures = 0;

	while (1) {
		ANN("\n---=== %s ===---\n", c->name);
		INF("Select a group:\n");
		for (size_t i = 0; i < c->num_groups; i++)
			INF("\t%zu -> %s\n", i + 1, c->groups[i].name);
		INF("\t%zu -> Run all (summary)\n", c->num_groups + 1);
		INF("\t0 -> Back\n");

		int input = getchar();
		if (input == EOF) {
			pause();
			continue;
		}

		if (input == '0')
			return total_failures;

		size_t sel = (size_t)(input - '1');
		if (input >= '1' && sel < c->num_groups) {
			total_failures += run_group(&c->groups[sel]);
		} else if (input >= '1' && sel == c->num_groups) {
			for (size_t i = 0; i < c->num_groups; i++)
				total_failures += summarize_group(&c->groups[i]);
			INF("\n%s total failures: %i\n", c->name, total_failures);
			pause();
		} else {
			INF("Invalid selection. Please try again.\n");
			pause();
		}
	}
}

int __attribute__((noreturn))
main(void)
{
	int total_failures = 0;
	size_t num_categories = sizeof(categories) / sizeof(categories[0]);
	DBG("At main\n");

	while (1) {
		struct hart_state *hs = hart_get_hstate_self();
		ANN("\n---=== TestSuite Menu ===---\n");
		INF("Welcome from hart: %i, hart_id: %li\n", hs->hart_idx, hs->hart_id);
		INF("Select test category:\n");
		for (size_t i = 0; i < num_categories; i++)
			INF("\t%zu -> %s\n", i + 1, categories[i].name);

		int input = getchar();
		if (input == EOF) {
			pause();
			continue;
		}

		size_t sel = (size_t)(input - '1');
		if (input >= '1' && sel < num_categories) {
			total_failures += run_category(&categories[sel]);
			INF("\nTotal failures across all tests: %i\n", total_failures);
		} else {
			INF("Invalid selection. Please try again.\n");
		}

		pause();
	}
}
