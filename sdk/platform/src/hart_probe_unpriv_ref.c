/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Reference models for the extension blocks (ext_blocks.h).
 *
 * Everything here is plain base-ISA C. The probes (hart_probe_unpriv.c) run an
 * extension's block, then compare the result against the matching ext_ref_z<ext>()
 * computed here, outside the probing window, so a bug in a model can never be
 * confused with a trap.
 */

#include <platform/riscv/z_ext_blocks.h>
#include <stdint.h>

/**** Reference models (base-ISA C, no builtins) ****/

#define EXT_REF_FN2(n, e)	static inline xu ref_##n(xu a, xu b) { (void)b; return (e); }
#define EXT_REF_FN1(n, e)	static inline xu ref_##n(xu a) { return (e); }

static inline xu ref_sext32(xu a) { return (xu)(xs)(int32_t)(uint32_t)a; }
static inline xu ref_shadd(xu a, xu b, unsigned n) { return (a << n) + b; }
static inline xu ref_clz_n(uint64_t a, unsigned n)
{
	unsigned c = 0;
	for (int i = n - 1; i >= 0 && !((a >> i) & 1); i--)
		c++;
	return c;
}
static inline xu ref_ctz_n(uint64_t a, unsigned n)
{
	unsigned c = 0;
	for (unsigned i = 0; i < n && !((a >> i) & 1); i++)
		c++;
	return c;
}
static inline xu ref_cpop_n(uint64_t a, unsigned n)
{
	unsigned c = 0;
	for (unsigned i = 0; i < n; i++)
		c += (a >> i) & 1;
	return c;
}

/* base ISA, used to fold results back into the chain */
EXT_REF_FN2(add, a + b)
EXT_REF_FN2(xor, a ^ b)

/* Zba */
static inline xu ref_sh1add(xu a, xu b) { return ref_shadd(a, b, 1); }
static inline xu ref_sh2add(xu a, xu b) { return ref_shadd(a, b, 2); }
static inline xu ref_sh3add(xu a, xu b) { return ref_shadd(a, b, 3); }
#if __riscv_xlen == 64
static inline xu ref_add_uw(xu a, xu b) { return (a & 0xffffffffu) + b; }
static inline xu ref_sh1add_uw(xu a, xu b) { return ref_shadd(a & 0xffffffffu, b, 1); }
static inline xu ref_sh2add_uw(xu a, xu b) { return ref_shadd(a & 0xffffffffu, b, 2); }
static inline xu ref_sh3add_uw(xu a, xu b) { return ref_shadd(a & 0xffffffffu, b, 3); }
static inline xu ref_slli_uw(xu a, xu n) { return (a & 0xffffffffu) << n; }
#endif

/* Zbb, Zbkb */
EXT_REF_FN2(andn, a & ~b)
EXT_REF_FN2(orn, a | ~b)
EXT_REF_FN2(xnor, ~(a ^ b))
EXT_REF_FN2(max, (xs)a > (xs)b ? a : b)
EXT_REF_FN2(min, (xs)a < (xs)b ? a : b)
EXT_REF_FN2(maxu, a > b ? a : b)
EXT_REF_FN2(minu, a < b ? a : b)
static inline xu ref_rol(xu a, xu b)
{
	unsigned s = b & (EXT_XLEN - 1);
	return s ? (a << s) | (a >> (EXT_XLEN - s)) : a;
}
static inline xu ref_ror(xu a, xu b)
{
	unsigned s = b & (EXT_XLEN - 1);
	return s ? (a >> s) | (a << (EXT_XLEN - s)) : a;
}
#if __riscv_xlen == 64
static inline xu ref_rolw(xu a, xu b)
{
	uint32_t x = (uint32_t)a;
	unsigned s = b & 31;
	return ref_sext32(s ? (x << s) | (x >> (32 - s)) : x);
}
static inline xu ref_rorw(xu a, xu b)
{
	uint32_t x = (uint32_t)a;
	unsigned s = b & 31;
	return ref_sext32(s ? (x >> s) | (x << (32 - s)) : x);
}
#endif
EXT_REF_FN1(clz, ref_clz_n(a, EXT_XLEN))
EXT_REF_FN1(ctz, ref_ctz_n(a, EXT_XLEN))
EXT_REF_FN1(cpop, ref_cpop_n(a, EXT_XLEN))
#if __riscv_xlen == 64
EXT_REF_FN1(clzw, ref_clz_n((uint32_t)a, 32))
EXT_REF_FN1(ctzw, ref_ctz_n((uint32_t)a, 32))
EXT_REF_FN1(cpopw, ref_cpop_n((uint32_t)a, 32))
#endif
EXT_REF_FN1(sext_b, (xu)(xs)(int8_t)(uint8_t)a)
EXT_REF_FN1(sext_h, (xu)(xs)(int16_t)(uint16_t)a)
EXT_REF_FN1(zext_h, a & 0xffff)
static inline xu ref_orc_b(xu a)
{
	xu r = 0;
	for (unsigned i = 0; i < EXT_XLEN; i += 8)
		if ((a >> i) & 0xff)
			r |= (xu)0xff << i;
	return r;
}
static inline xu ref_rev8(xu a)
{
	xu r = 0;
	for (unsigned i = 0; i < EXT_XLEN; i += 8)
		r |= ((a >> i) & 0xff) << (EXT_XLEN - 8 - i);
	return r;
}
/* Reverse the bits inside each byte */
static inline xu ref_brev8(xu a)
{
	xu r = 0;
	for (unsigned i = 0; i < EXT_XLEN; i++)
		if ((a >> i) & 1)
			r |= (xu)1 << ((i & ~7u) + 7 - (i & 7));
	return r;
}
static inline xu ref_pack(xu a, xu b)
{
	xu m = ((xu)1 << (EXT_XLEN / 2)) - 1;
	return (a & m) | (b << (EXT_XLEN / 2));
}
EXT_REF_FN2(packh, (a & 0xff) | ((b & 0xff) << 8))
#if __riscv_xlen == 64
EXT_REF_FN2(packw, ref_sext32((a & 0xffff) | ((b & 0xffff) << 16)))
#endif
#if __riscv_xlen == 32
/* RV32 only. zip moves the low half to the even bits and the high half to the odd ones */
static inline xu ref_zip(xu a)
{
	xu r = 0;
	for (unsigned i = 0; i < 16; i++)
		r |= (((a >> i) & 1) << (2 * i)) | (((a >> (i + 16)) & 1) << (2 * i + 1));
	return r;
}
static inline xu ref_unzip(xu a)
{
	xu r = 0;
	for (unsigned i = 0; i < 16; i++)
		r |= (((a >> (2 * i)) & 1) << i) | (((a >> (2 * i + 1)) & 1) << (i + 16));
	return r;
}
#endif

/* Zbc, Zbkc: carry-less multiply */
static inline xu ref_clmul(xu a, xu b)
{
	xu r = 0;
	for (unsigned i = 0; i < EXT_XLEN; i++)
		if ((b >> i) & 1)
			r ^= a << i;
	return r;
}
static inline xu ref_clmulh(xu a, xu b)
{
	xu r = 0;
	for (unsigned i = 1; i < EXT_XLEN; i++)
		if ((b >> i) & 1)
			r ^= a >> (EXT_XLEN - i);
	return r;
}
static inline xu ref_clmulr(xu a, xu b)
{
	xu r = 0;
	for (unsigned i = 0; i < EXT_XLEN; i++)
		if ((b >> i) & 1)
			r ^= a >> (EXT_XLEN - i - 1);
	return r;
}

/* Zbs */
EXT_REF_FN2(bclr, a & ~((xu)1 << (b & (EXT_XLEN - 1))))
EXT_REF_FN2(bset, a | ((xu)1 << (b & (EXT_XLEN - 1))))
EXT_REF_FN2(binv, a ^ ((xu)1 << (b & (EXT_XLEN - 1))))
EXT_REF_FN2(bext, (a >> (b & (EXT_XLEN - 1))) & 1)

/* Zicond */
EXT_REF_FN2(czero_eqz, b == 0 ? 0 : a)
EXT_REF_FN2(czero_nez, b != 0 ? 0 : a)

/* Zmmul */
EXT_REF_FN2(mul, a * b)
#if __riscv_xlen == 64
EXT_REF_FN2(mulw, ref_sext32((uint32_t)a * (uint32_t)b))
#endif
#if __riscv_xlen == 64
EXT_REF_FN2(mulh, (xu)(((__int128)(xs)a * (__int128)(xs)b) >> 64))
EXT_REF_FN2(mulhsu, (xu)(((__int128)(xs)a * (__int128)(unsigned __int128)b) >> 64))
EXT_REF_FN2(mulhu, (xu)(((unsigned __int128)a * (unsigned __int128)b) >> 64))
#else
EXT_REF_FN2(mulh, (xu)(((int64_t)(xs)a * (int64_t)(xs)b) >> 32))
EXT_REF_FN2(mulhsu, (xu)(((int64_t)(xs)a * (int64_t)(uint64_t)b) >> 32))
EXT_REF_FN2(mulhu, (xu)(((uint64_t)a * (uint64_t)b) >> 32))
#endif

/* Zbkx: a is the lookup table, b holds the indices; an out of range index reads as 0 */
static inline xu ref_xperm_n(xu a, xu b, unsigned w)
{
	xu r = 0, m = ((xu)1 << w) - 1;
	unsigned n = EXT_XLEN / w;
	for (unsigned i = 0; i < n; i++) {
		xu idx = (b >> (i * w)) & m;
		if (idx < n)
			r |= ((a >> (idx * w)) & m) << (i * w);
	}
	return r;
}
static inline xu ref_xperm4(xu a, xu b) { return ref_xperm_n(a, b, 4); }
static inline xu ref_xperm8(xu a, xu b) { return ref_xperm_n(a, b, 8); }

/* Reference models for the SHA-2 / SM3 instructions */
static inline uint32_t ref_ror32(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
static inline uint32_t ref_rol32(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }
static inline xu ref_sha256sig0(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(ref_ror32(v, 7) ^ ref_ror32(v, 18) ^ (v >> 3)); }
static inline xu ref_sha256sig1(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(ref_ror32(v, 17) ^ ref_ror32(v, 19) ^ (v >> 10)); }
static inline xu ref_sha256sum0(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(ref_ror32(v, 2) ^ ref_ror32(v, 13) ^ ref_ror32(v, 22)); }
static inline xu ref_sha256sum1(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(ref_ror32(v, 6) ^ ref_ror32(v, 11) ^ ref_ror32(v, 25)); }
static inline xu ref_sm3p0(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(v ^ ref_rol32(v, 9) ^ ref_rol32(v, 17)); }
static inline xu ref_sm3p1(xu a) { uint32_t v = (uint32_t)a; return ref_sext32(v ^ ref_rol32(v, 15) ^ ref_rol32(v, 23)); }
#if __riscv_xlen == 64
static inline uint64_t ref_ror64(uint64_t v, unsigned n) { return (v >> n) | (v << (64 - n)); }
static inline xu ref_sha512sig0(xu a) { return ref_ror64(a, 1) ^ ref_ror64(a, 8) ^ (a >> 7); }
static inline xu ref_sha512sig1(xu a) { return ref_ror64(a, 19) ^ ref_ror64(a, 61) ^ (a >> 6); }
static inline xu ref_sha512sum0(xu a) { return ref_ror64(a, 28) ^ ref_ror64(a, 34) ^ ref_ror64(a, 39); }
static inline xu ref_sha512sum1(xu a) { return ref_ror64(a, 14) ^ ref_ror64(a, 18) ^ ref_ror64(a, 41); }
#else
/* RV32: the 64-bit functions are split into low/high word instructions */
static inline xu ref_sha512sig0l(xu a, xu b) { return (a >> 1) ^ (a >> 7) ^ (a >> 8) ^ (b << 31) ^ (b << 25) ^ (b << 24); }
static inline xu ref_sha512sig0h(xu a, xu b) { return (a >> 1) ^ (a >> 7) ^ (a >> 8) ^ (b << 31) ^ (b << 24); }
static inline xu ref_sha512sig1l(xu a, xu b) { return (a << 3) ^ (a >> 6) ^ (a >> 19) ^ (b >> 29) ^ (b << 26) ^ (b << 13); }
static inline xu ref_sha512sig1h(xu a, xu b) { return (a << 3) ^ (a >> 6) ^ (a >> 19) ^ (b >> 29) ^ (b << 13); }
static inline xu ref_sha512sum0r(xu a, xu b) { return (a << 25) ^ (a << 30) ^ (a >> 28) ^ (b >> 7) ^ (b >> 2) ^ (b << 4); }
static inline xu ref_sha512sum1r(xu a, xu b) { return (a << 23) ^ (a >> 14) ^ (a >> 18) ^ (b >> 9) ^ (b << 18) ^ (b << 14); }
#endif

/* C side of the step lists, see ext_blocks.h */
#define EXT_C_R(dst, mn, s1, s2, fn)	dst = fn(s1, s2);
#define EXT_C_U(dst, mn, s1, fn)	dst = fn(s1);
#define EXT_C_I(dst, mn, s1, imm, fn)	dst = fn(s1, imm);

#define EXT_CHAIN_REF(name, steps) \
	xu name(xu a, xu b) \
	{ \
		xu x = 0, t = 0; \
		steps(EXT_C_R, EXT_C_U, EXT_C_I) \
		(void)t; \
		return x; \
	}

/* Atomics: reference models of the memory contents and returned values */
static inline xu ref_sext_n(xu v, unsigned bits)
{
	return bits >= EXT_XLEN ? v : (xu)((xs)(v << (EXT_XLEN - bits)) >> (EXT_XLEN - bits));
}
static inline uint64_t ref_mask_n(unsigned bits)
{
	return bits >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << bits) - 1);
}

enum { EXT_AMO_SWAP, EXT_AMO_ADD, EXT_AMO_XOR, EXT_AMO_OR, EXT_AMO_AND,
       EXT_AMO_MAX, EXT_AMO_MAXU, EXT_AMO_MIN, EXT_AMO_MINU };

/* New value (masked to the access width) of an AMO on (old, src) */
static inline uint64_t ref_amo_new(unsigned op, unsigned bits, uint64_t old, uint64_t src)
{
	uint64_t m = ref_mask_n(bits), sg = (uint64_t)1 << (bits - 1);

	old &= m;
	src &= m;
	/* (v ^ sign bit) - sign bit sign-extends v from `bits` bits */
	int64_t so = (int64_t)((old ^ sg) - sg);
	int64_t ss = (int64_t)((src ^ sg) - sg);

	switch (op) {
	case EXT_AMO_SWAP: return src;
	case EXT_AMO_ADD: return (old + src) & m;
	case EXT_AMO_XOR: return old ^ src;
	case EXT_AMO_OR: return old | src;
	case EXT_AMO_AND: return old & src;
	case EXT_AMO_MAX: return so > ss ? old : src;
	case EXT_AMO_MAXU: return old > src ? old : src;
	case EXT_AMO_MIN: return so < ss ? old : src;
	default: return old < src ? old : src;
	}
}

/* Applies an AMO to the low `bits` of *mem (little endian), returns what rd gets */
static inline xu ref_amo(unsigned bits, uint64_t *mem, unsigned op, xu src)
{
	uint64_t m = ref_mask_n(bits), old = *mem & m;

	*mem = (*mem & ~m) | ref_amo_new(op, bits, old, src);
	return ref_sext_n((xu)old, bits);
}

xu ext_ref_zaamo(xu a, xu b)
{
	uint64_t m_ = a;
	xu x = 0;

	for (unsigned i_ = 0; i_ < 9; i_++)
		x += ref_amo(32, &m_, i_, (i_ & 1) ? a : b);
	x += (xu)m_;
#if __riscv_xlen == 64
	m_ = b;
	for (unsigned i_ = 0; i_ < 9; i_++)
		x += ref_amo(64, &m_, i_, (i_ & 1) ? a : b);
	x += (xu)m_;
#endif
	return x;
}

xu ext_ref_zabha(xu a, xu b)
{
	uint64_t m_ = a;
	xu x = 0;

	for (unsigned i_ = 0; i_ < 9; i_++)
		x += ref_amo(8, &m_, i_, (i_ & 1) ? a : b);
	x += (xu)m_;
	m_ = b;
	for (unsigned i_ = 0; i_ < 9; i_++)
		x += ref_amo(16, &m_, i_, (i_ & 1) ? a : b);
	x += (xu)m_;
	return x;
}

xu ext_ref_zalrsc(xu a, xu b)
{
	uint64_t m_ = a;
	xu x = 0;

	/* .w: lr gets the old value (sign extended), the sc stores old + b */
	x += ref_sext_n((xu)m_, 32);
	m_ = (m_ & ~ref_mask_n(32)) | ((xu)(ref_sext_n((xu)m_, 32) + b) & ref_mask_n(32));
	x += 0;			/* status of the successful sc */
	x += 1;			/* the sc with no reservation fails */
	x += (xu)m_;
#if __riscv_xlen == 64
	m_ = b;
	x += (xu)m_;		/* lr.d */
	m_ = m_ + b;
	x += 0;
	x += 1;
	x += (xu)m_;
#endif
	return x;
}

xu ext_ref_zacas(xu a, xu b)
{
	uint64_t m0 = a, m1 = 0;
	uint64_t w = ref_mask_n(32);
	xu x = 0, old;

	/* amocas.w: the compare uses the low 32 bits */
	old = ref_sext_n((xu)m0, 32);
	if ((m0 & w) == ((uint64_t)a & w))
		m0 = (m0 & ~w) | ((uint64_t)b & w);
	x += old;
	old = ref_sext_n((xu)m0, 32);
	if ((m0 & w) == ((uint64_t)a & w))
		m0 = (m0 & ~w) | ((uint64_t)a & w);
	x += old;
	x += (xu)m0;
#if __riscv_xlen == 64
	m0 = a;
	old = (xu)m0;
	if (m0 == a)
		m0 = b;
	x += old;
	old = (xu)m0;
	if (m0 == a)
		m0 = a;
	x += old;
	x += (xu)m0;
#endif
	/* pair: memory (a, b), expected (a, b), new (b, a) */
	m0 = a;
	m1 = b;
	{
		xu o0 = (xu)m0, o1 = (xu)m1;

		if (o0 == a && o1 == b) {
			m0 = b;
			m1 = a;
		}
		x += o0 + 2 * o1;
		o0 = (xu)m0;
		o1 = (xu)m1;
		if (o0 == a && o1 == b) {
			m0 = b;
			m1 = a;
		}
		x += o0 + 2 * o1;
	}
	x += (xu)m0;
	x += (xu)m1;
	return x;
}

xu ext_ref_zifencei(xu k1, xu k2)
{
	return k1 + (k2 << 16);
}

/* Step-list chains: one C twin per extension, same list as the matching block */
EXT_CHAIN_REF(ext_ref_zba, ZBA_STEPS)
EXT_CHAIN_REF(ext_ref_zbb, ZBB_STEPS)
EXT_CHAIN_REF(ext_ref_zbc, ZBC_STEPS)
EXT_CHAIN_REF(ext_ref_zbs, ZBS_STEPS)
EXT_CHAIN_REF(ext_ref_zicond, ZICOND_STEPS)
EXT_CHAIN_REF(ext_ref_zmmul, ZMMUL_STEPS)
EXT_CHAIN_REF(ext_ref_zbkb, ZBKB_STEPS)
EXT_CHAIN_REF(ext_ref_zbkc, ZBKC_STEPS)
EXT_CHAIN_REF(ext_ref_zbkx, ZBKX_STEPS)
EXT_CHAIN_REF(ext_ref_zknh, ZKNH_STEPS)
EXT_CHAIN_REF(ext_ref_zksh, ZKSH_STEPS)

/* Operands */
static const uint64_t ext_vals[] = {
	0x0123456789abcdefULL, 0xfedcba9876543210ULL, 0x8000000000000001ULL,
	0xa5a53c3cf0f00f0fULL, 0x7fffffff80000000ULL, 0xdeadbeefcafef00dULL,
	0x00000000ffffffffULL, 0x13579bdf2468ace0ULL,
};

void
ext_pair(unsigned i, xu *a, xu *b)
{
	if (i < EXT_NVALS) {
		*a = (xu)ext_vals[i];
		*b = (xu)ext_vals[(i + 3) % EXT_NVALS];
	} else if (i == EXT_NVALS) {
		*a = (xu)ext_vals[0]; *b = 0;
	} else if (i == EXT_NVALS + 1) {
		*a = (xu)ext_vals[1]; *b = 0;
	} else {
		*a = 0; *b = (xu)ext_vals[1];
	}
}