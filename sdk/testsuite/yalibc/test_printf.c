/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>			/* For typed ints */
#include <stdio.h>			/* For printf */
#include <platform/utils/utils.h>	/* For ANN/INF/ERR */
#include <limits.h>			/* For INT_MAX */
#include <test_framework.h>		/* For test registration macros */

static void
test_putd(double val) {
	printf("Default behavior\n");
	printf("f form: %f\ne-form: %e\ng-form: %g\na-form: %a\n", val, val, val, val);
	printf("With presision 2\n");
	printf("f form: %.2f\ne-form: %.2e\ng-form: %.2g\na-form: %.2a\n", val, val, val, val);
	printf("With precision 324 (from argument) and forced sign\n");
	int prec = 324;
	printf("f form: %+.*f\ne-form: %+.*e\ng-form: %+.*g\na-form: %+.*a\n", prec, val, prec, val, prec, val, prec, val);
	printf("With precision 0 and space sign\n");
	printf("f form: % .0f\ne-form: % .0e\ng-form: % .0g\na-form: % .0a\n", val, val, val, val);
	printf("With alt flag and uppercase\n");
	printf("f form: %#F\ne-form: %#E\ng-form: %#G\na-form: %#A\n", val, val, val, val);
	printf("With alt flag and precision 0\n");
	printf("f form: %#.0f\ne-form: %#.0e\ng-form: %#.0g\na-form: %#.0a\n", val, val, val, val);
}

static void
test_putll(long long int itest)
{
#define CAST_UNSIGNED_ALL(val) \
	(unsigned int)(val), \
	(unsigned long)(val), \
	(unsigned long long)(val), \
	(unsigned short)(val), \
	(unsigned char)(val)

#define CAST_UNSIGNED_ALL_WITH_PREC(val) \
	prec, (unsigned int)(val), \
	prec, (unsigned long)(val), \
	prec, (unsigned long long)(val), \
	prec, (unsigned short)(val), \
	prec, (unsigned char)(val)

#define CAST_SIGNED_ALL(val) \
	(int)(val), \
	(long)(val), \
	(long long)(val), \
	(short)(val), \
	(signed char)(val)

#define CAST_SIGNED_ALL_WITH_PREC(val) \
	prec, (int)(val), \
	prec, (long)(val), \
	prec, (long long)(val), \
	prec, (short)(val), \
	prec, (signed char)(val)

	printf("Default behavior\n");
	printf("i: %+i, li: % li, lld: %+lld, hd: % hd, hhi:%hhi\n", CAST_SIGNED_ALL(itest));
	printf("u: %u, lu: %lu, llu: %llu, hu: %hu, hhu:%hhu\n", CAST_UNSIGNED_ALL(itest));
	printf("x: %x, lX: %lX, llx: %llx, hx: %hx, hhx:%hhx\n", CAST_UNSIGNED_ALL(itest));
	printf("o: %o, lo: %lo, llo: %llo, ho: %ho, hho:%hho\n", CAST_UNSIGNED_ALL(itest));
	printf("#o: %#o, #lo: %#lo, #llx: %#llX, #hx: %#hx, #hho:%#hho\n", CAST_UNSIGNED_ALL(itest));
	printf("With precision 2\n");
	printf("i: %+.2i, li: % .2li, lld: %+.2lld, hd: % .2hd, hhi:%.2hhi\n", CAST_SIGNED_ALL(itest));
	printf("u: %.2u, lu: %.2lu, llu: %.2llu, hu: %.2hu, hhu:%.2hhu\n", CAST_UNSIGNED_ALL(itest));
	printf("x: %.2x, lX: %.2lX, llx: %.2llx, hx: %.2hx, hhx:%.2hhx\n", CAST_UNSIGNED_ALL(itest));
	printf("o: %.2o, lo: %.2lo, llo: %.2llo, ho: %.2ho, hho:%.2hho\n", CAST_UNSIGNED_ALL(itest));
	printf("#o: %#.2o, #lo: %#.2lo, #llx: %#.2llX, #hx: %#.2hx, #hho:%#.2hho\n", CAST_UNSIGNED_ALL(itest));
	printf("With precision (from argument) 20\n");
	int prec = 20;
	printf("i: %+.*i, li: % .*li, lld: %+.*lld, hd: % .*hd, hhi:%.*hhi\n", CAST_SIGNED_ALL_WITH_PREC(itest));
	printf("u: %.*u, lu: %.*lu, llu: %.*llu, hu: %.*hu, hhu:%.*hhu\n", CAST_UNSIGNED_ALL_WITH_PREC(itest));
	printf("x: %.*x, lX: %.*lX, llx: %.*llx, hx: %.*hx, hhx:%.*hhx\n", CAST_UNSIGNED_ALL_WITH_PREC(itest));
	printf("o: %.*o, lo: %.*lo, llo: %.*llo, ho: %.*ho, hho:%.*hho\n", CAST_UNSIGNED_ALL_WITH_PREC(itest));
	printf("#o: %#.*o, #lo: %#.*lo, #llx: %#.*llX, #hx: %#.*hx, #hho:%#.*hho\n", CAST_UNSIGNED_ALL_WITH_PREC(itest));

#undef CAST_UNSIGNED_ALL
#undef CAST_UNSIGNED_ALL_WITH_PREC
#undef CAST_SIGNED_ALL
#undef CAST_SIGNED_ALL_WITH_PREC
}

static int
test_printf(void)
{
	ANN("\n---===PRINTF TEST===---\n");

	INF("\n---INTS---\n");
	printf("Note: that i/lld are forced sign (+), and li/hd with forced space (' ')\n");
	printf("Input is 0x123456789ABCDEF0ULL\n");
	int64_t itest = 0x123456789ABCDEF0ULL;
	test_putll(itest);

	printf("\nInput is -1234567\n");
	itest = -1234567;
	test_putll(itest);

	printf("\nInput is 1234567\n");
	itest = 1234567;
	test_putll(itest);

	printf("\nInput is -123\n");
	itest = -123;
	test_putll(itest);

	printf("\nInput is 123\n");
	itest = 123;
	test_putll(itest);

	printf("\nSpecial cases\n");
	itest = 0;
	printf("0 with precision 1->5: u: %.1u, li: %.2li, #llx: %#.3llx, hx: %.4hx, hho: %.5hho\n",
					(unsigned int) itest, (long) itest, (unsigned long long) itest,
					(unsigned short) itest, (unsigned char) itest);
	printf("0 with 0 precision: u: %.0u, li: %.0li, #llx: %#.0llx, hx: %.0hx, hho: %.0hho\n",
					(unsigned int) itest, (long) itest, (unsigned long long) itest,
					(unsigned short) itest, (unsigned char) itest);
	printf("p: %p\n", &itest);
	printf("INT_MAX:    %d\n", INT_MAX);
	printf("INT_MIN:    %d\n", INT_MIN);
	printf("LLONG_MAX:  %lld\n", LLONG_MAX);
	printf("LLONG_MIN:  %lld\n", LLONG_MIN);
	printf("INTMAX_MIN: %jd\n", INTMAX_MIN);
	printf("UINT_MAX:   %u\n", UINT_MAX);
	printf("ULLONG_MAX: %llu\n", ULLONG_MAX);
	printf("UINTMAX_MAX: %ju\n", UINTMAX_MAX);

	/* Hex to verify no truncation */
	printf("ULLONG_MAX hex: %llx\n", ULLONG_MAX);

	union {
		double d;
		uint64_t i;
	} test;

	INF("\n---DOUBLES---\n");
	printf("Input is: 0.0\n");
	test.d = 0.0L;
	test_putd(test.d);

	printf("\nInput is: -0.0\n");
	test.d = -0.0L;
	test_putd(test.d);

	printf("\nInput is: +inf\n");
	test.i = 0x7FF0000000000000ULL;
	test_putd(test.d);

	printf("\nInput is: -inf\n");
	test.i = 0xFFF0000000000000ULL;
	test_putd(test.d);

	printf("\nInput is: nan (quiet)\n");
	test.i = 0x7FF8000000000000ULL;
	test_putd(test.d);

	printf("\nInput is: nan (signaling)\n");
	test.i = 0x7FF0000000000001ULL;
	test_putd(test.d);

	printf("\nInput is: -nan\n");
	test.i = 0xFFF8000000000000ULL;
	test_putd(test.d);

	printf("\nInput is: 123.456789\n");
	test.d = 123.456789;
	test_putd(test.d);

	printf("\nInput is: 0.999999\n");
	test.d = 0.999999;
	test_putd(test.d);

	printf("\nInput is: -1.99999\n");
	test.d = -1.99999;
	test_putd(test.d);

	printf("\nInput is: 9.99999\n");
	test.d = 9.99999;
	test_putd(test.d);

	printf("\nInput is: -10.99999\n");
	test.d = -10.99999;
	test_putd(test.d);

	printf("\nInput is: 1\n");
	test.i = 0x3FF0000000000000UL;
	test_putd(test.d);

	printf("\nInput is: -2\n");
	test.i = 0xC000000000000000UL;
	test_putd(test.d);

	printf("\nInput is: 123400.0\n");
	test.d = 123400.0L;
	test_putd(test.d);

	printf("\nInput is: 1.00023456\n");
	test.d = 1.00023456L;
	test_putd(test.d);

	printf("\nInput is: smallest number > 1\n");
	test.i = 0x3FF0000000000001UL;
	test_putd(test.d);

	printf("\nInput is: 242312142346.1432567\n");
	test.d = 242312142346.1432567L;
	test_putd(test.d);

	printf("\nInput is: 0.123400E-20L\n");
	test.d = 0.123400E-20L;
	test_putd(test.d);

	printf("\nInput is: 0.0123456\n");
	test.d = 0.0123456L;
	test_putd(test.d);

	printf("\nInput is: max normal positive double\n");
	test.i = 0x7FEFFFFFFFFFFFFFULL;
	test_putd(test.d);

	printf("\nInput is: min normal positive double\n");
	test.i = 0x0010000000000000ULL;
	test_putd(test.d);

	printf("\nInput is: max sub-normal positive double\n");
	test.i = 0x000FFFFFFFFFFFFFULL;
	test_putd(test.d);

	printf("\nInput is: min sub-normal positive double\n");
	test.i = 0x0000000000000001ULL;
	test_putd(test.d);

	printf("\nInput is: 1/3\n");
	test.d = 1.0L/3.0L;
	test_putd(test.d);

	printf("\nInput is: ~pi\n");
	test.i = 0x400921FB54442D18ULL;
	test_putd(test.d);

	INF("\n---Ryu corner cases---\n");
	printf("Input is: 5.764607523034235E39\n");
	test.i = 0x4830F0CF064DD592ULL;
	test_putd(test.d);

	printf("\nInput is: 1.152921504606847E40\n");
	test.i = 0x4840F0CF064DD592ULL;
	test_putd(test.d);

	printf("\nInput is: 2.305843009213694E40\n");
	test.i = 0x4850F0CF064DD592ULL;
	test_putd(test.d);

	INF("\n---FORMATED OUTPUT---\n");
	printf("Int input is 123\n");
	itest = 123;
	printf("Double input is 123.456789\n");
	test.d = 123.456789;
	printf("In the following tests its default, - flag. 0 flag for i/e/f convertions\n");
	printf("Test1: Right/left adjust, fixed width, default precision\n");
	printf("|123456789012345|\n");
	printf("|%+15i|\n", (int) itest);
	printf("|%-15i|\n", (int) itest);
	printf("|% 015i|\n", (int) itest);
	printf("|%+15e|\n", test.d);
	printf("|%-15e|\n", test.d);
	printf("|% 015e|\n", test.d);
	printf("|% 15f|\n", test.d);
	printf("|%-15f|\n", test.d);
	printf("|%+015f|\n", test.d);
	printf("Test2: Dynamic prec, fixed width, both from args\n");
	printf("|12345678901234567890|\n");
	int width = 20;
	int prec = 4;
	while(prec <= 20) {
		printf("|%*.*i|\n", width, prec, (int) itest);
		prec += 4;
	}
	prec = 20;
	while(prec > 0) {
		printf("|%-*.*i|\n", width, prec, (int) itest);
		prec -= 4;
	}
	prec = 4;
	while(prec <= 12) {
		printf("|%*.*e|\n", width, prec, test.d);
		prec += 4;
	}
	prec = 12;
	while(prec > 0) {
		printf("|%-*.*e|\n", width, prec, test.d);
		prec -= 4;
	}
	prec = 4;
	while(prec <= 12) {
		printf("|%*.*f|\n", width, prec, test.d);
		prec += 4;
	}
	prec = 12;
	while(prec > 0) {
		printf("|%-*.*f|\n", width, prec, test.d);
		prec -= 4;
	}

	INF("\n---STRING/CHAR---\n");
	printf("Example from C11 7.21.6.1 with normal (non-wide) characters\n");
	static char str[] = " X Yabc Z W";
	printf("|1234567890123|\n");
	printf("|%13s|\n", str);
	printf("|%-13.9s|\n", str);
	printf("|%13.10s|\n", str);
	printf("|%13.11s|\n", str);
	printf("|%13.15s|\n", &str[2]);
	printf("|%13c|\n", str[5]);

	INF("Press a key to continue...\n");
	return 0;
}

REGISTER_YALIBC_TEST("Printf tests", test_printf);
