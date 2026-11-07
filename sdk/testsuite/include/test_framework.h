/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stddef.h>			/* For size_t */

/*
 * The test suite is a flat set of "units": an extension, a privileged feature, a
 * platform test or a libc test. Each unit is one probe function, run in a mix of
 * modes selected by `flags`:
 *   PROBE_BENCH       - run the payload repeatedly and measure/report.
 *   PROBE_INFO        - print the unit's details (counters, PMP granule, ASID ...).
 *   PROBE_INTERACTIVE - run the payload once, showing its output (a plain test).
 *   PROBE_FUZZ        - run open-ended until an error or the user presses 'q'.
 *   PROBE_COMPACT     - modifier for PROBE_BENCH: one compact line (group benchmark).
 *
 * With no flags the function is a pure probe (run once; present and working?). It
 * returns which operations it supports, so the menu offers them conditionally:
 *   >= 0   present, OR-ed with any of PROBE_BENCH / PROBE_INFO / PROBE_INTERACTIVE /
 *          PROBE_FUZZ it supports (the capability bits reuse the flag values);
 *   PROBE_ABSENT (-1)   not present;
 *   PROBE_FAILED (-2)   the probe / benchmark / fuzz failed.
 * With any flag set it returns only 0 (ok) or PROBE_FAILED.
 */
enum probe_flags {
	PROBE_BENCH		= 1,
	PROBE_INFO		= 2,
	PROBE_INTERACTIVE	= 4,
	PROBE_FUZZ		= 8,
	PROBE_COMPACT		= 16,
};

#define PROBE_ABSENT	(-1)
#define PROBE_FAILED	(-2)

typedef int (*probe_fn)(unsigned flags);

/*
 * One per unit, gathered by the linker into a per-group array (test_sections.ld)
 * walked by index / counted as (stop - start), so the array stride must equal
 * sizeof(struct unit_probe); the 16-byte alignment keeps sizeof a multiple of 16
 * (matching the ALIGN(16) before each __stop) and the members naturally aligned
 * under -mstrict-align. The symbol is named after the probe fn (named after the
 * unit) so SORT_BY_NAME orders the group -- a new unit is just a new REGISTER_*
 * line (ideally its own .c file), no caller changes.
 */
struct unit_probe {
	const char *name;
	probe_fn probe;
} __attribute__((aligned(16)));

#define __REGISTER_UNIT(sect, fn, unit_name) \
	static const struct unit_probe __unit_##fn \
		__attribute__((section(sect "." #fn), used)) = { \
			.name = (unit_name), \
			.probe = (fn) \
		}

#define REGISTER_HART_PRIV_M(name, fn)	__REGISTER_UNIT("__units_hart_priv_m", fn, name)
#define REGISTER_HART_PRIV_S(name, fn)	__REGISTER_UNIT("__units_hart_priv_s", fn, name)
/* U-mode and common Z* overlap (Z* is unprivileged, user-visible), so they share
 * one group/file (hart_priv_zexts.c). */
#define REGISTER_HART_PRIV_Z(name, fn)	__REGISTER_UNIT("__units_hart_priv_z", fn, name)
#define REGISTER_HART_UNPRIV(name, fn)	__REGISTER_UNIT("__units_hart_unpriv", fn, name)
#define REGISTER_PLATFORM(name, fn)	__REGISTER_UNIT("__units_platform", fn, name)
#define REGISTER_YALIBC(name, fn)	__REGISTER_UNIT("__units_yalibc", fn, name)

#define __UNITS_GROUP(grp) \
	extern const struct unit_probe __start_units_##grp[], __stop_units_##grp[]
__UNITS_GROUP(hart_priv_m);
__UNITS_GROUP(hart_priv_s);
__UNITS_GROUP(hart_priv_z);
__UNITS_GROUP(hart_unpriv);
__UNITS_GROUP(platform);
__UNITS_GROUP(yalibc);
#undef __UNITS_GROUP

/*
 * Interactive menu for one group: probe every unit once, list the present ones with
 * a continuous index (printing details for those that report PROBE_INFO), then --
 * conditional on the group's capabilities -- let the user benchmark all, or
 * run/benchmark/fuzz a chosen unit, or go back.
 */
void run_group_menu(const char *title, const struct unit_probe *units, size_t count);

#endif /* TEST_FRAMEWORK_H */
