/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 /*
 * Bitfield manipulation macros compatible with assembler inclusion
 * Inspired by Linux kernel's linux/bitfield.h
 */

#ifndef _BITFIELD_H
#define _BITFIELD_H
 
/* 
 * Detect if this is being included by the assembler
 * Assemblers typically define __ASSEMBLER__ or AS_FEATURE
 */
#if defined(__ASSEMBLER__) || defined(AS_FEATURE)
	/* Assembler-compatible definitions (no type suffixes) */
	#define _UL(x)  (x)
	#define _ULL(x) (x)
#else
	/* C code type suffixes */
	#define _UL(x)  ((unsigned long)(x))
	#define _ULL(x) ((unsigned long long)(x))
#endif


/* Basic bit manipulation macros */
#define BIT(nr)		(_UL(1) << (nr))
#define BIT_ULL(nr)	(_ULL(1) << (nr))

/* Architecture-specific bit size definitions */
#define __BITS_PER_LONG (__SIZEOF_POINTER__ * 8)
#define __BITS_PER_LONG_LONG 64

/* To make things simpler and avoid retrieving the shift
 * bits from the mask like linux does, which is incompatible
 * with the assembler since it requires __builtin_ffsll,
 * and can't be done in the preprocessor, we encode both the
 * mask and the shift bits in the same value. Since we want
 * to be able to place a field anywhere in the register, we
 * use a 64bit value, where the top 6bits (0 - 63)
 * hold the shift bits, and the bottom 58bits hold the mask.
 * The limitation introduced by this approach is that for fields
 * longer than 58bits we need extra macros, but that scenario is
 * rare and such fields are usually reserved anyway. */
#define FIELD_SHIFT_BITS  6
#define FIELD_MASK_BITS   (64 - FIELD_SHIFT_BITS)
#define FIELD_SHIFT_MASK  ((_ULL(0x3F)) << FIELD_MASK_BITS)
 
 /* Linux kernel-style GENMASK implementation */
#define GENMASK(h, l) \
	(((_UL(~0)) - (_UL(1) << (l)) + 1) & \
	(_UL(~0) >> (__BITS_PER_LONG - 1 - (h))))

#define GENMASK_ULL(h, l) \
	(((_ULL(~0)) - (_ULL(1) << (l)) + 1) & \
	(_ULL(~0) >> (__BITS_PER_LONG_LONG - 1 - (h))))

/* Define a field given high and low bits (32-bit registers, but using 64-bit encoding) */
#define FIELD(h, l) \
	(((_ULL(l)) << FIELD_MASK_BITS) | \
	((GENMASK(h, l) >> (l)) & ~FIELD_SHIFT_MASK))

/* Define a field given high and low bits (64-bit version) */
#define FIELD_ULL(h, l) \
	(((_ULL(l)) << FIELD_MASK_BITS) | \
	((GENMASK_ULL(h, l) >> (l)) & ~FIELD_SHIFT_MASK))

/* Extract components from an encoded field */
#define FIELD_GET_SHIFT(field)  ((field) >> FIELD_MASK_BITS)
#define FIELD_GET_MASK(field)   (((field) & ~FIELD_SHIFT_MASK) << FIELD_GET_SHIFT(field))

/* Field manipulation macros for 32-bit fields - mask to 32 bits */
#define FIELD_PREP(field, val) \
	_UL((_UL(val) << FIELD_GET_SHIFT(field)) & FIELD_GET_MASK(field))

#define FIELD_GET(field, reg) \
	_UL((_UL(reg) & FIELD_GET_MASK(field)) >> FIELD_GET_SHIFT(field))

#define FIELD_INSERT(reg, field, val) \
	_UL(_UL((reg) & ~(FIELD_GET_MASK(field))) | FIELD_PREP(field, val))

/* Field manipulation macros for 64-bit fields */
#define FIELD_PREP_ULL(field, val) \
	_ULL((_ULL(val) << FIELD_GET_SHIFT(field)) & FIELD_GET_MASK(field))

#define FIELD_GET_ULL(field, reg) \
	_ULL((_ULL(reg) & FIELD_GET_MASK(field)) >> FIELD_GET_SHIFT(field))

#define FIELD_INSERT_ULL(reg, field, val) \
	_ULL((_ULL(reg) & ~(FIELD_GET_MASK(field))) | FIELD_PREP(field, val))

#endif /* _BITFIELD_H */