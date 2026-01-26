/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>	/* For typed integers */
#include <stdbool.h>	/* For bool */

/*
 * Most ISAs (including RISC-V) use the binary64 representation from
 * IEEE 754 for double precision floating point. Printf always uses
 * doubles (it promotes single precision floating point -binary32-
 * to doubles), so we only need to parse binary64 here. The binary64
 * format is:
 * Sign(1bit) | Exponent(11bits) | Significand(52bits)
 *
 * The value represented by this format is (in base2):
 * (-1)^sign * 2^exponent * (significand)
 *
 * The significand is broken down like this:
 * 2^0 (leading bit) | 2^-1 ... 2^-i (fraction)
 *
 * To allow for more precision, when the leading bit is 1, it's not
 * stored, so the 52bit significand field only contains the fraction
 * part which goes down to 2^-53. When it's 0 we have a sub-normal
 * number (very close to zero), in which case the significand also
 * holds the leading bit, and the fraction part is reduced by a bit
 * so it goes down to 2^-52. This loss of precision also prevents
 * zero underflow when doing calculations.
 *
 * The exponent field is an unsigned 11bit integer, and in order to
 * represent both positive and negative exponent values, the field's
 * value is shifted by 2^10 - 1 (half the maximum numbebr it can represent),
 * also called the "bias". So it can represent values from -1023 (when the
 * exponent field is 0) to +1023 (when the exponent field has its maximum
 * value of 2^11 - 1 = 2047). When the exponent field is zero we have a
 * sub-normal number, and because the leading bit is stored in the
 * significand we need to add one to the exponent's value, so it becomes
 * -1022 (after removing the bias). When the exponent field has its maximum
 * value it's also treated in a special way, it means "infinity" when the
 * fraction part is zero, and N(ot)aN(umber) otherwise. So long story short,
 * the exponent field encodes exponent values from -1022 to +1022, and used
 * to mark the number as sub-normal or Infinity/NaN.
 *
 * So far so good, however when we try to convert this from/to decimal
 * things get ugly. To get a fraction in base b, we divide a number by
 * b in order to move the point to the left by one, so for 0.1 in
 * decimal we divide 1 by 10, in binary to get 0.1 would mean to divide
 * 1 by 2. The general rule is that we can only move the point in a
 * finite number of digits (and have a finite representation of the
 * number), when the prime factors of the divisor are the same as the
 * prime factors of b. If not we can't move the point by one digit
 * (or to put it differently, we can't move the point by a fraction
 * of a digit). So a fraction that has a finite representation on
 * base 2 can have a finite representation on base 10 (since 2 is
 * a prime factor of 10), but the oposite is not true. 1/10 for
 * example in base 2 is 0.000110011... and if we only take the
 * first repetition we end up with 0.09375 in base 10, instead of
 * 0.1. Dealing with this inherent error in binary <-> decimal
 * convertion of fractions is quite messy and there are various
 * approaches to the problem, here are a few of them that I
 * could find:
 *
 * https://dl.acm.org/doi/10.1145/93542.93559
 * https://dl.acm.org/doi/10.1145/249069.231397
 * https://dl.acm.org/doi/10.1145/1806596.1806623
 * https://dl.acm.org/doi/10.1145/3192366.3192369
 * https://drive.google.com/file/d/1luHhyQF9zKlM8yJ1nebU0OgVYhfC6CBN/view
 * https://github.com/jk-jeon/dragonbox
 *
 * Of those I decided to implement Ryu by Ulf Adams, since I liked
 * it better, it seemed more natural to me, and turns out it's also
 * considered the state of the art in terms of accuracy/performance.
 * Its reference implementation is also released under the Apache 2.0
 * license, and is available here:
 * https://github.com/ulfjack/ryu/tree/master
 *
 * This one is cleaned up a bit to follow the paper more closely (using
 * the same variable names for example), and modified a bit to fit the
 * coding style.
 *
 * In order for this to work, we need int128 support from the compiler,
 * GCC supports this for all 64bit architectures so we should be ok. Note
 * that we won't go for arbitrary precision here, we'll use the standard
 * Ryu instead of Ryu printf, and return the shortest representation instead.
 *
 * Although I like Ryu, when there are size/arch constraints where we can't
 * use int128 (e.g. on 32bit targets), or the lookup tables, so an alternative
 * is also provided (WiP).
 */

typedef unsigned __int128 uint128_t;
#define EXPONENT_BIAS	1023
#define SIGNIFICAND_BITS 52

static inline uint32_t
max(uint32_t a, uint32_t b) {
	return (a > b) ? a : b;
}

/*
 * Calculate log10(2^p)
 * log10(2^p) = p*log10(2) ≈ p * 0.30102999566 ≈ p * 78913 / 2^18
 * Note this approximation works for 0 <= p <= 1650 and allows us
 * to play with integers instead of floats/doubles, which is much
 * faster. It fits our use case since p after removing bias is
 * between +-1022, and we use -p if negative.
 */
static inline uint32_t
Log10ofPow2(const int32_t p) {
	return (((uint32_t) p) * 78913) >> 18;
}

/*
 * Calculate log10(5^p)
 * log10(5^p) = p*log10(5) ≈ p * 0.69897000433 ≈ p * 732923 / 2^20
 * This approximation works for 0 <= p <= 2620, but as in the case
 * above we are good to go.
 */
static inline uint32_t
Log10ofPow5(const int32_t p) {
	return (((uint32_t) p) * 732923) >> 20;
}

/*
 * Calculate log2(5^p)
 * log2(5^p) = p*log2(5) ≈ p * 2.32192809489 ≈ p * 1217359 / 2^19
 * This approximation works for 0 <= p <= 3529, also good to go.
 */
static inline int32_t
Log2ofPow5(const int32_t p) {
	return (int32_t) ((((uint32_t) p) * 1217359) >> 19);
}

/*
 * Calculate ceil(log2(5^p)
 */
static inline int32_t
CeilLog2ofPow5(const int32_t p) {
	return Log2ofPow5(p) + 1;
}

/*
 * Returns true if val is divisible by 2^p
 */
static inline bool
DivisibleByPow2(const uint64_t val, const uint32_t p)
{
	return (val & ((1ull << p) - 1)) == 0;
}

/*
 * Returns true if val is divisible by 5^p
 * This divides val by 5 (multiplying it with 1/5 since
 * multiplication is faster than division), until it can't
 * fit a 64bit int. If p is smaller than the number of times
 * 5 fits in val, then val is divisible by 5^p. This only
 * works for p up to 27 since for larger p 5^p won't fit
 * uin64_t, however we'll be using this for p <= 22 so
 * we are good to go.
 */
static inline bool
DivisibleByPow5(uint64_t val, const uint32_t p)
{
	/* We want to play with integers, so we are looking for a
	 * number M so that 5 * M = 1 in modulo 2^64, so basicaly
	 * a very large number that will result the multiplication's
	 * result to be larger than 2^64 and wrap arround to 1 when
	 * multiplied by 5. This way when it's multiplied by 10 for
	 * example it'll result 2 etc, which is the same as dividing
	 * 10 (or any number) with 5. M is called the modular
	 * multiplicative inverse. */
	const uint64_t m_inv_5 = 14757395258967641293u;
	/* The largest multiple of 5 in a uint64_t = (2^64) / 5 */
	const uint64_t max_div_5 = 3689348814741910323u;
	uint32_t count = 0;
	while(true) {
		val *= m_inv_5;
		if (val > max_div_5)
			break;
		count++;
	}
	return (count >= p);
}

/* Does a 64x128 bit multiplication between a and b, and shifts the result
 * by j - 64. This is a cool trick, we know that a only has 55 significant
 * bits (the fractional has at most 53 bits + 2 that we use for making sure
 * halfway points remain integers -see below-) so we can skip the 9 topmost
 * bits. The 128bit factor b (value of 5^p or 5^-p) has its 4 topmost bits zero
 * as well so we can also ignore them. The worst case scenario is that we need
 * 55 + 124 = 179bits for the multiplication, however j will be >= 115 (see
 * i below) and 179 - 115 = 64, so the result can fit in a uin64_t. */
static inline uint64_t
MulShift64(const uint64_t a, const uint64_t* const b, const int32_t j)
{
	const uint128_t c0 = ((uint128_t) a) * b[0];
	const uint128_t c2 = ((uint128_t) a) * b[1];
	return (uint64_t) (((c0 >> 64) + c2) >> (j - 64));
}

#define POW5_INV_BITCOUNT 125
#define POW5_BITCOUNT 125

/* Note the code for generating the tables below is here:
 * https://github.com/ulfjack/ryu/blob/master/src/main/java/info/adams/ryu/analysis/PrintDoubleLookupTable.java
 */

/* Precomputed values of 5^p, with 0 <= p < 27, since 5^26 is the largest
 * power of 5 that fits a uint64_t, this table has 26 entries */
#define POW5_TABLE_SIZE 26
static const uint64_t POW5_TABLE[POW5_TABLE_SIZE] = {
	1ull, 5ull, 25ull, 125ull, 625ull, 3125ull, 15625ull, 78125ull, 390625ull,
	1953125ull, 9765625ull, 48828125ull, 244140625ull, 1220703125ull, 6103515625ull,
	30517578125ull, 152587890625ull, 762939453125ull, 3814697265625ull,
	19073486328125ull, 95367431640625ull, 476837158203125ull,
	2384185791015625ull, 11920928955078125ull, 59604644775390625ull,
	298023223876953125ull //, 1490116119384765625ull
};


/* Here we want to calculate 5^p / 2^(ceil(log2(5^p)) - POW5_BITCOUNT), where p is
 * -exponent10 for negative exponents, and ceil(log2(5^p)) is the length of 5^p in
 * bits. Since -exponent10 can go up to 324, we have 324 possible entries but instead
 * of storing them all, in order to save space, we save one for every 26 entries and
 * use the precomputed 5^p table above and POW5_OFFSETS table below, to fill the rest. */
static const uint64_t POW5_SPLIT2[13][2] = {
	{		     0u, 1152921504606846976u },
	{		     0u, 1490116119384765625u },
	{  1032610780636961552u, 1925929944387235853u },
	{  7910200175544436838u, 1244603055572228341u },
	{ 16941905809032713930u, 1608611746708759036u },
	{ 13024893955298202172u, 2079081953128979843u },
	{  6607496772837067824u, 1343575221513417750u },
	{ 17332926989895652603u, 1736530273035216783u },
	{ 13037379183483547984u, 2244412773384604712u },
	{  1605989338741628675u, 1450417759929778918u },
	{  9630225068416591280u, 1874621017369538693u },
	{   665883850346957067u, 1211445438634777304u },
	{ 14931890668723713708u, 1565756531257009982u }
};

static const uint32_t POW5_OFFSETS[21] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x40000000, 0x59695995,
	0x55545555, 0x56555515, 0x41150504, 0x40555410, 0x44555145, 0x44504540,
	0x45555550, 0x40004000, 0x96440440, 0x55565565, 0x54454045, 0x40154151,
	0x55559155, 0x51405555, 0x00000105
};

static inline void
Pow5Inv2k(const uint32_t p, uint64_t* const result)
{
	const uint32_t base = p / POW5_TABLE_SIZE;
	const uint32_t base2 = base * POW5_TABLE_SIZE;
	const uint32_t offset = p - base2; // p % POW_TABLE_SIZE
	const uint64_t* const mul = POW5_SPLIT2[base]; // 5^base2
	if (offset == 0) {
		result[0] = mul[0];
		result[1] = mul[1];
		return;
	}
	const uint64_t m = POW5_TABLE[offset]; // 5^offset
	// 5^offset * 5^base2 = 5^(p - base2 + base2) = 5^p
	const uint128_t b0 = ((uint128_t) m) * mul[0];
	const uint128_t b2 = ((uint128_t) m) * mul[1];
	const uint32_t delta = CeilLog2ofPow5(p) - CeilLog2ofPow5(base2);
	const uint128_t shiftedSum = (b0 >> delta) + (b2 << (64 - delta)) +
				     ((POW5_OFFSETS[p / 16] >> ((p % 16) << 1)) & 3);
	result[0] = (uint64_t) shiftedSum;
	result[1] = (uint64_t) (shiftedSum >> 64);
}

/* Same as above but for 2^(floor(log2(5^p)) + POW5_INV_BITCOUNT) / 5^p + 1,
 * this is used for positive exponents so exponent10 goes up to 308. */
static const uint64_t POW5_INV_SPLIT2[15][2] = {
	{		     1u, 2305843009213693952u },
	{  5955668970331000884u, 1784059615882449851u },
	{  8982663654677661702u, 1380349269358112757u },
	{  7286864317269821294u, 2135987035920910082u },
	{  7005857020398200553u, 1652639921975621497u },
	{ 17965325103354776697u, 1278668206209430417u },
	{  8928596168509315048u, 1978643211784836272u },
	{ 10075671573058298858u, 1530901034580419511u },
	{   597001226353042382u, 1184477304306571148u },
	{  1527430471115325346u, 1832889850782397517u },
	{ 12533209867169019542u, 1418129833677084982u },
	{  5577825024675947042u, 2194449627517475473u },
	{ 11006974540203867551u, 1697873161311732311u },
	{ 10313493231639821582u, 1313665730009899186u },
	{ 12701016819766672773u, 2032799256770390445u }
};

static const uint32_t POW5_INV_OFFSETS[19] = {
	0x54544554, 0x04055545, 0x10041000, 0x00400414, 0x40010000, 0x41155555,
	0x00000454, 0x00010044, 0x40000000, 0x44000041, 0x50454450, 0x55550054,
	0x51655554, 0x40004000, 0x01000001, 0x00010500, 0x51515411, 0x05555554,
	0x00000000
};

static inline void
Pow2kInvPow5(const uint32_t p, uint64_t* const result)
{
	const uint32_t base = (p + POW5_TABLE_SIZE - 1) / POW5_TABLE_SIZE;
	const uint32_t base2 = base * POW5_TABLE_SIZE;
	const uint32_t offset = base2 - p; // p % POW_TABLE_SIZE
	const uint64_t* const mul = POW5_INV_SPLIT2[base]; // 1/5^base2
	if (offset == 0) {
		result[0] = mul[0];
		result[1] = mul[1];
		return;
	}
	const uint64_t m = POW5_TABLE[offset]; // 5^offset
	const uint128_t b0 = ((uint128_t) m) * (mul[0] - 1);
	// 1/5^base2 * 5^offset = 1/5^(base2-offset) = 1/5^i
	const uint128_t b2 = ((uint128_t) m) * mul[1];
	const uint32_t delta = CeilLog2ofPow5(base2) - CeilLog2ofPow5(p);
	const uint128_t shiftedSum = ((b0 >> delta) + (b2 << (64 - delta))) + 1 +
				     ((POW5_INV_OFFSETS[p / 16] >> ((p % 16) << 1)) & 3);
	result[0] = (uint64_t) shiftedSum;
	result[1] = (uint64_t) (shiftedSum >> 64);
}

void
yalc_double_to_decimal(uint32_t exponent_biased, uint64_t significand,
		       int32_t* exponent10, uint64_t* fraction10)
{
	int32_t exponent2 = 0;
	uint64_t fraction2 = 0;
	/* Halfway points in base2 */
	uint64_t u = 0;
	uint64_t v = 0;
	uint64_t w = 0;
	bool u_not_smallest = true;
	/* Halfway points in decimal */
	uint64_t a = 0;
	uint64_t b = 0;
	uint64_t c = 0;
	bool Za = false;
	bool Zb = false;
	int32_t removed_digits = 0;
	uint8_t last_removed_digit = 0;
	bool frac_is_even = false;

	if (exponent_biased != 0) {
		exponent2 = exponent_biased - EXPONENT_BIAS;
		/* Add the leading bit */
		fraction2 = significand | (1ULL << SIGNIFICAND_BITS);
	} else {
		/* Sub-normal number, +1 because the
		 * leading bit is already stored */
		exponent2 = (0 -EXPONENT_BIAS + 1);
		fraction2 = significand;
	}

	frac_is_even = ((fraction2 & 1) == 0);

	/* Let's see if we got an integer from 1 - 2^53, that we can extract
	 * directly from fraction2. */
	if (exponent2 > 0 && exponent2 < SIGNIFICAND_BITS) {
		const uint64_t frac_mask = ((1ULL << (SIGNIFICAND_BITS - exponent2)) - 1);
		const uint64_t tmp_fraction = fraction2 & frac_mask;
		if (!tmp_fraction) {
			*fraction10 = fraction2 >> (SIGNIFICAND_BITS - exponent2);
			*exponent10 = 0;
			/* Move any trailing 0s to the exponent */
			while (true) {
				const uint64_t fr10_div10 = (*fraction10) / 10;
				const uint32_t fr10_mod10 = (*fraction10) % 10;
				if (fr10_mod10 != 0)
					break;
				*fraction10 = fr10_div10;
				(*exponent10)++;
			}
			return;
		}
	}

	/* Step 2: Calculate halfway points, defining the range of valid outputs
	 *
	 * We'll treat fracion part as an integer, which is equivalent to
	 * having multiplied the number by 2^52, so we need to subtract 52
	 * from the exponent. This also takes care of sub-normal numbers
	 * since we've already added one to the exponent. In order for the
	 * halfway points to be integers as well, we'll need two more bits
	 * in the fraction part, so we'll multiply it by 2^2, and remove 2
	 * from the exponent. */
	exponent2 -= (SIGNIFICAND_BITS + 2);
	fraction2 *= 4;
	/* Smaller halfway point, handle the case where it's the smallest
	 * possible number. */
	u_not_smallest = (significand != 0 || exponent_biased <= 1);
	u = fraction2 - ((u_not_smallest) ? 2 : 1);
	v = fraction2;
	w = fraction2 + 2;

	/* Step 3: Convert halfway points / exponent to decimal */
	if (exponent2 >= 0) {
		/* (a_q, b_q, c_q) = ((u,v, w) * ((2^k/5^q) + 1)/2^(−exponent2 + q + k) */
		const uint32_t q = max(0, Log10ofPow2(exponent2) - 1);
		*exponent10 = (int32_t) q;
		const int32_t p = *exponent10;
		const int32_t k = POW5_INV_BITCOUNT + Log2ofPow5(p);
		const int32_t i = -exponent2 + (int32_t) q + k;
		uint64_t p2kip5[2];	// 128bit, shifted
		Pow2kInvPow5(p, p2kip5);
		a = MulShift64(u, p2kip5, i);
		b = MulShift64(v, p2kip5, i);
		c = MulShift64(w, p2kip5, i);
		/* Check for trailing zeroes (see sec. 3.3) */
		if (q <= 22) {
			const uint32_t v_mod5 = ((uint32_t) v) - 5 * ((uint32_t) (v / 5));
			if (!v_mod5) {
				Zb = DivisibleByPow5(v, q);
			} else if (frac_is_even) {
				Za = DivisibleByPow5(u, q);
			} else {
				c -= DivisibleByPow5(w, q);
			}
		}
	} else {
		/* (a_q, b_q, c_q) = ((u,v, w) * (5^(-exponent2 - q)/2^k)/2^(q - k) */
		const uint32_t q = max(0, Log10ofPow5(-exponent2) - 1);
		*exponent10 = (int32_t) q + exponent2;
		const int32_t p = -(*exponent10);
		const uint32_t k = CeilLog2ofPow5(p) - POW5_BITCOUNT;
		const int32_t i = (int32_t) q - k;
		uint64_t p5i2k[2];
		Pow5Inv2k(p, p5i2k);
		a = MulShift64(u, p5i2k, i);
		b = MulShift64(v, p5i2k, i);
		c = MulShift64(w, p5i2k, i);
		if (q <= 1) {
			Zb = true;
			if (frac_is_even) {
				Za = (u_not_smallest ? 1 : 0);
			} else {
				--c;
			}
		} else if (q <= 63) {
			Zb = DivisibleByPow2(v, q);
		}
	}

	/* Step 4: Find the shortest decimal representation */
	if (Za || Zb) {
		while (true) {
			const uint64_t a_div10 = (a / 10);
			const uint64_t c_div10 = (c / 10);
			if (c_div10 <= a_div10)
				break;
			const uint32_t a_mod10 = (a % 10);
			const uint64_t b_div10 = (b / 10);
			const uint32_t b_mod10 = (b % 10);
			Za &= (a_mod10 == 0);
			Zb &= (last_removed_digit == 0);
			last_removed_digit = (uint8_t) b_mod10;
			a = a_div10;
			b = b_div10;
			c = c_div10;
			removed_digits++;
		}
		if (Za) {
			while (true) {
				const uint64_t a_div10 = (a / 10);
				const uint32_t a_mod10 = (a % 10);
				if (a_mod10)
					break;
				const uint64_t b_div10 = (b / 10);
				const uint64_t c_div10 = (c / 10);
				const uint32_t b_mod10 = (b % 10);
				Zb &= (last_removed_digit == 0);
				last_removed_digit = (uint8_t) b_mod10;
				a = a_div10;
				b = b_div10;
				c = c_div10;
				removed_digits++;
			}
		}
		if (Zb && (last_removed_digit == 5) && ((b & 2) == 0)) {
			last_removed_digit = 4;
		}
		*fraction10 = b + ((b == a && (!frac_is_even || !Za)) || last_removed_digit >= 5);
	} else {
		bool round_up = false;
		const uint64_t a_div100 = (a / 100);
		const uint64_t c_div100 = (c / 100);
		if (a_div100 < c_div100) {
			const uint64_t b_div100 = (b / 100);
			const uint32_t b_mod100 = (b % 100);
			round_up = b_mod100 >= 50;
			a = a_div100;
			b = b_div100;
			c = c_div100;
			removed_digits += 2;
		}
		while (true) {
			const uint64_t a_div10 = (a / 10);
			const uint64_t c_div10 = (c / 10);
			if (c_div10 <= a_div10)
				break;
			const uint64_t b_div10 = (b / 10);
			const uint32_t b_mod10 = (b % 10);
			round_up = b_mod10 >= 5;
			a = a_div10;
			b = b_div10;
			c = c_div10;
			removed_digits++;
		}
		*fraction10 = b + ((b == a) || round_up);
	}
	*exponent10 += removed_digits;
}

/* We compute powers of 10 using Ryu's power-of-5 table since 10^n = 5^n × 2^n.
 * This saves 160 bytes of .rodata at the cost of one bit-shift operation. */
#define POW10(n) (POW5_TABLE[n] << (n))

uint64_t
yalc_round_to_digits(uint64_t value, int current_digits, int desired_digits)
{
	if (desired_digits >= current_digits || desired_digits >= 17)
		return value;

	const int digits_to_remove = current_digits - desired_digits;
	uint64_t divisor = POW10(digits_to_remove);
	uint64_t quotient = value / divisor;
	uint64_t remainder = value % divisor;
	uint64_t half = divisor / 2;

	/* Round half to even: 4.5 -> 4, 5.6 -> 6
	 * aka banker's rounding, as required by IEEE 754 */
	if (remainder > half)
		quotient++;
	else if (remainder == half && (quotient & 1))
		quotient++;

	return quotient;
}