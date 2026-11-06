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

#include <stdbool.h>			/* For bool in the helpers below */

typedef int (*test_func_t)(void);

struct test_entry {
	/* First char is a one-byte kind marker (MENU_MARK_*); the human-readable
	 * text starts at description[1]. Menu code skips the marker when printing
	 * and checks it to decide whether to render a pass/fail verdict. */
	const char *description;
	test_func_t test_fn;
} __attribute__((packed));

/*
 * An entry is either a PROBE or a TEST:
 *   - PROBE: detect & report a capability. Absence is a valid result, so there
 *     is no pass/fail -- the menu just runs it and shows its output.
 *   - TEST: asserts something and returns a failure count, rendered pass/fail.
 *
 * Rather than spend a struct field on this, the kind is encoded as a one-char
 * marker prefixed to the description by the register macro. For menu ordering,
 * entries are placed in a per-group input subsection named after the registered
 * symbol, and symbols are named <...>probe_* / <...>test_* so SORT_BY_NAME (in
 * test_sections.ld) naturally lists a group's probes before its tests.
 */
#define MENU_MARK_PROBE	"\x01"
#define MENU_MARK_TEST	"\x02"

#define __REGISTER_ENTRY(sect, func, mark, desc) \
	static const struct test_entry __menu_##func \
		__attribute__((section(sect "." #func), used, aligned(16))) = { \
			.description = mark desc, \
			.test_fn = (func) \
		}

#define REGISTER_YALIBC_TEST(desc, func)	__REGISTER_ENTRY("__menu_yalibc", func, MENU_MARK_TEST, desc)
#define REGISTER_PLATFORM_TEST(desc, func)	__REGISTER_ENTRY("__menu_platform", func, MENU_MARK_TEST, desc)
#define REGISTER_HART_PRIV_PROBE(desc, func)	__REGISTER_ENTRY("__menu_hart_priv", func, MENU_MARK_PROBE, desc)
#define REGISTER_HART_PRIV_TEST(desc, func)	__REGISTER_ENTRY("__menu_hart_priv", func, MENU_MARK_TEST, desc)
#define REGISTER_HART_UNPRIV_PROBE(desc, func)	__REGISTER_ENTRY("__menu_hart_unpriv", func, MENU_MARK_PROBE, desc)
#define REGISTER_HART_UNPRIV_TEST(desc, func)	__REGISTER_ENTRY("__menu_hart_unpriv", func, MENU_MARK_TEST, desc)

/* Skip the kind marker to get the printable description. */
static inline const char *
menu_entry_name(const struct test_entry *e)
{
	return &e->description[1];
}

/* A probe has no pass/fail verdict; a test does. */
static inline bool
menu_entry_is_probe(const struct test_entry *e)
{
	return e->description[0] == MENU_MARK_PROBE[0];
}

extern struct test_entry __start_rodata_menu_yalibc[];
extern struct test_entry __stop_rodata_menu_yalibc[];
extern struct test_entry __start_rodata_menu_platform[];
extern struct test_entry __stop_rodata_menu_platform[];
extern struct test_entry __start_rodata_menu_hart_priv[];
extern struct test_entry __stop_rodata_menu_hart_priv[];
extern struct test_entry __start_rodata_menu_hart_unpriv[];
extern struct test_entry __stop_rodata_menu_hart_unpriv[];

#endif
