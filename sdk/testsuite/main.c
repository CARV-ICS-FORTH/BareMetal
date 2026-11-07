/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <test_framework.h>		/* For unit registration + run_group_menu() */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/hart.h>	/* For hart_get_hstate_self() */
#include <platform/riscv/csr.h>		/* For pause() */
#include <stddef.h>			/* For size_t */
#include <stdint.h>			/* For uint64_t */
#include <stdio.h>			/* For getchar(), EOF */

#define GROUP_COUNT(grp)	((size_t)(__stop_units_##grp - __start_units_##grp))

/* Array index of the Nth (1-based) set bit in `mask`, or -1 if there are fewer.
 * Maps a continuous display number back to its sparse unit_probe array slot. */
static int
nth_present(uint64_t mask, unsigned n)
{
	for (int i = 0; i < 64; i++)
		if ((mask & (1ull << i)) && --n == 0)
			return i;
	return -1;
}

/* Read a decimal number from the console: skip leading non-digits (e.g. a stray
 * newline), then read digits, echoing them (the serial line is in raw mode, so the
 * terminal doesn't echo). Returns the value, or -1 if none was entered. */
static int
read_index(void)
{
	int c, val;

	while ((c = getchar()) != EOF && (c < '0' || c > '9'))
		;
	if (c == EOF)
		return -1;
	putchar(c);
	for (val = c - '0'; (c = getchar()) >= '0' && c <= '9'; ) {
		putchar(c);
		val = val * 10 + (c - '0');
	}
	putchar('\n');
	return val;
}

/* Prompt for a displayed unit number and run `op` (one of PROBE_INTERACTIVE /
 * PROBE_BENCH / PROBE_FUZZ) on it, if it supports it. */
static void
run_one(const struct unit_probe *units, uint64_t present, unsigned op)
{
	INF("Unit #: ");
	int n = read_index();
	int idx = (n > 0) ? nth_present(present, (unsigned)n) : -1;

	if (idx < 0) {
		INF("No such unit.\n");
		return;
	}
	if (!(units[idx].probe(0) & op)) {
		INF("%s does not support that.\n", units[idx].name);
		return;
	}
	if (units[idx].probe(op) == PROBE_FAILED)
		INF("\n%s: FAILED\n", units[idx].name);
}

void
run_group_menu(const char *title, const struct unit_probe *units, size_t count)
{
	while (1) {
		uint64_t present = 0;
		unsigned any = 0, shown = 0;

		ANN("\n---=== %s ===---\n", title);
		for (size_t i = 0; i < count && i < 64; i++) {
			int caps = units[i].probe(0);

			if (caps < 0)
				continue;		/* absent / failed: skip */
			present |= 1ull << i;
			any |= (unsigned)caps;
			INF("  %2u) %s\n", ++shown, units[i].name);
			if (caps & PROBE_INFO)
				units[i].probe(PROBE_INFO);	/* indented detail */
		}
		if (!shown)
			INF("  (nothing present)\n");

		INF("\nSelect:\n");
		if (any & PROBE_INTERACTIVE)
			INF("\tr -> run a test\n");
		if (any & PROBE_BENCH) {
			INF("\tb -> benchmark one\n");
			INF("\ta -> benchmark all\n");
		}
		if (any & PROBE_FUZZ)
			INF("\tf -> fuzz one\n");
		INF("\t0 -> back\n");

		int sel = getchar();
		if (sel == EOF) {
			pause();
			continue;
		}
		switch (sel) {
		case '0':
			return;
		case 'r':
		case 'R':
			if (any & PROBE_INTERACTIVE)
				run_one(units, present, PROBE_INTERACTIVE);
			break;
		case 'b':
		case 'B':
			if (any & PROBE_BENCH)
				run_one(units, present, PROBE_BENCH);
			break;
		case 'f':
		case 'F':
			if (any & PROBE_FUZZ)
				run_one(units, present, PROBE_FUZZ);
			break;
		case 'a':
		case 'A':
			if (any & PROBE_BENCH)
				for (size_t i = 0; i < count && i < 64; i++)
					if ((present & (1ull << i)) &&
					    (units[i].probe(0) & PROBE_BENCH))
						units[i].probe(PROBE_BENCH | PROBE_COMPACT);
			break;
		default:
			INF("Invalid selection.\n");
			break;
		}
		pause();
	}
}

/* Privileged sub-menu: the M/S/U/Z groups. */
static void
menu_privileged(void)
{
	while (1) {
		ANN("\n---=== Privileged extensions ===---\n");
		INF("\t1 -> M-mode\n"
		    "\t2 -> S-mode\n"
		    "\t3 -> U-mode & Z* (common)\n"
		    "\t0 -> back\n");

		int sel = getchar();
		if (sel == EOF) {
			pause();
			continue;
		}
		switch (sel) {
		case '1':
			run_group_menu("M-mode extensions", __start_units_hart_priv_m,
				       GROUP_COUNT(hart_priv_m));
			break;
		case '2':
			run_group_menu("S-mode extensions", __start_units_hart_priv_s,
				       GROUP_COUNT(hart_priv_s));
			break;
		case '3':
			run_group_menu("U-mode & Z* extensions", __start_units_hart_priv_z,
				       GROUP_COUNT(hart_priv_z));
			break;
		case '0':
			return;
		default:
			INF("Invalid selection.\n");
			pause();
			break;
		}
	}
}

/* Hart probing sub-menu: privileged and unprivileged. */
static void
menu_hart(void)
{
	while (1) {
		ANN("\n---=== Hart probing ===---\n");
		INF("\t1 -> Privileged extensions\n"
		    "\t2 -> Unprivileged extensions\n"
		    "\t0 -> back\n");

		int sel = getchar();
		if (sel == EOF) {
			pause();
			continue;
		}
		switch (sel) {
		case '1':
			menu_privileged();
			break;
		case '2':
			run_group_menu("Unprivileged extensions", __start_units_hart_unpriv,
				       GROUP_COUNT(hart_unpriv));
			break;
		case '0':
			return;
		default:
			INF("Invalid selection.\n");
			pause();
			break;
		}
	}
}

int __attribute__((noreturn))
main(void)
{
	DBG("At main\n");

	while (1) {
		struct hart_state *hs = hart_get_hstate_self();

		ANN("\n---=== TestSuite Menu ===---\n");
		INF("Welcome from hart: %i, hart_id: %li\n", hs->hart_idx, hs->hart_id);
		INF("\t1 -> Hart probing\n"
		    "\t2 -> Platform probing\n"
		    "\t3 -> YaLibC tests\n");

		int sel = getchar();
		if (sel == EOF) {
			pause();
			continue;
		}
		switch (sel) {
		case '1':
			menu_hart();
			break;
		case '2':
			run_group_menu("Platform probing", __start_units_platform,
				       GROUP_COUNT(platform));
			break;
		case '3':
			run_group_menu("YaLibC tests", __start_units_yalibc,
				       GROUP_COUNT(yalibc));
			break;
		default:
			INF("Invalid selection.\n");
			break;
		}
		pause();
	}
}
