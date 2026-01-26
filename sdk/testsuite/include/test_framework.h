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

typedef int (*test_func_t)(void);

struct test_entry {
	const char *description;
	test_func_t test_fn;
} __attribute__((packed));

#define YALIBC_TEST_SECTION __attribute__((section("__tests_yalibc"), used, aligned(16)))
#define PLATFORM_TEST_SECTION __attribute__((section("__tests_platform"), used, aligned(16)))

#define REGISTER_YALIBC_TEST(desc, func) \
	static const struct test_entry __test_yalibc_##func YALIBC_TEST_SECTION = { \
		.description = desc, \
		.test_fn = func \
	}

#define REGISTER_PLATFORM_TEST(desc, func) \
	static const struct test_entry __test_platform_##func PLATFORM_TEST_SECTION = { \
		.description = desc, \
		.test_fn = func \
	}

extern struct test_entry __start_rodata_tests_yalibc[];
extern struct test_entry __stop_rodata_tests_yalibc[];
extern struct test_entry __start_rodata_tests_platform[];
extern struct test_entry __stop_rodata_tests_platform[];

#endif
