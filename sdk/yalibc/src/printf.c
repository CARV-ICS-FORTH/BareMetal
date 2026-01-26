/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/utils/lock.h>	/* For lock_acquire/release() */
#include <stdint.h>	/* For typed integers */
#include <stdbool.h>	/* For bool */
#include <errno.h>	/* For error codes */
#include <string.h>	/* For strncpy/strnlen etc */
#include <limits.h>	/* For INT_MAX */
#include <stdio.h>

/***********************\
* Printf implementation *
\***********************/

/* Implementation based on C23 spec ISO/IEC 9899:2024
 * draft N3096 Section 7.23.6.1 [The fprintf function] */

#define YALC_DEFAULT_STR_PREC 4095	/* Default precision for %s when none
					 * specified. C11 says "at least 4095"
					 * per conversion, we use exactly that
					 * as our implicit default precision. */

enum format_flags {
	FFLAG_ALT	= 0x01,
	FFLAG_ZERO_PAD	= 0x02,
	FFLAG_LEFT_ADJ	= 0x04,
	FFLAG_NOPSIGN	= 0x08,
	FFLAG_ADDSIGN	= 0x10,
	FFLAG_MASK	= 0x1F
};

enum format_length_flags {
	LFLAG_HALF	= 0x020,
	LFLAG_HALF_HALF	= 0x040,
	LFLAG_LONG	= 0x080,
	LFLAG_LONG_LONG	= 0x100,
	LFLAG_INTMAX	= 0x200,
	LFLAG_SIZET	= 0x400,
	LFLAG_PTRDIFF	= 0x800,
	LFLAG_MASK	= 0xFE0
};

enum format_type_flags {
	TFLAG_INT	= 0x01000,
	TFLAG_DOUBLE	= 0x02000,
	TFLAG_CHAR	= 0x04000,
	TFLAG_STRING	= 0x08000,
	TFLAG_PTR	= 0x10000,
	TFLAG_CTR	= 0x20000,
	TFLAG_MASK	= 0x3F000
};

enum format_status_flags {
	SFLAG_HAS_WIDTH	= 0x0040000,
	SFLAG_HAS_PREC	= 0x0080000,
	SFLAG_UPPERCASE	= 0x0100000,
	SFLAG_IS_SIGNED	= 0x0200000,
	SFLAG_IS_FFORM	= 0x0400000,
	SFLAG_IS_EFORM	= 0x0800000,
	SFLAG_IS_GFORM	= 0x0C00000,
	SFLAG_IS_AFORM	= 0x1000000,
	SFLAG_IS_FAST	= 0x2000000,
	SFLAG_MASK	= 0x3FC0000
};

enum format_transform_flags {
	TRFLAG_REVERSE_PREFIX	= 0x04000000,
	TRFLAG_REVERSE_NUM	= 0x08000000,
	TRFLAG_REVERSE_SUFFIX	= 0x10000000,
	TRFLAG_MASK		= 0x1C000000
};

enum radix {
	RADIX_BIN = 2,
	RADIX_OCT = 8,
	RADIX_DEC = 10,
	RADIX_HEX = 16
};

struct format_info {
	uint32_t flags;
	int width;
	int precision;
	int int_bitlen;
	int num_pad;
	int suffix_pad;
	enum radix radix;
};

struct field_data {
	const char *inbuff;
	size_t inbuff_len;
	const char *prefix;
	size_t prefix_len;
	const char *suffix;
	size_t suffix_len;
	char sign_char;
};

struct output_info {
	char* outbuff;
	size_t outbuff_len;
	size_t chars_out;
};

static atomic_int printf_lock = 0;

/*********\
* Helpers *
\*********/

/* Use these to clean up format_info/field_data without
 * using memset (or letting the compiler use memset when
 * compiling with -Os). */
static inline void
yalc_clear_fi(struct format_info *fi)
{
	fi->flags = 0;
	fi->width = 0;
	fi->precision = 0;
	fi->int_bitlen = 0;
	fi->num_pad = 0;
	fi->suffix_pad = 0;
	fi->radix = RADIX_DEC;
}

static inline void
yalc_clear_fld(struct field_data *fld)
{
	fld->inbuff = NULL;
	fld->inbuff_len = 0;
	fld->prefix = 0;
	fld->prefix_len = 0;
	fld->suffix = NULL;
	fld->suffix_len = 0;
	fld->sign_char = '\0';
}

/***********************\
* Input format handling *
\***********************/

/* Got a format specifier (%), not followed by another % (so caller didn't
 * just want to print out '%'). This has to obey the following structure
 * according to the spec:
 *  %[flags][width][.precision][length modifier]conversion
 * We don't support 'x$' (width from argument x), that's not part of the C spec
 * (it's a mess comming from Single Unix), nor m (strerror) which is a gnu extension.
 * We also don't support n since it's a security nightmare (Android's Bionic also follows
 * the same approach). We treat the L modifier the same for all convertion types (allowed
 * by the spec since for non-doubles it's "undefined behavior").
 */
static int
yalc_pf_parse_format(const char* restrict fmt, int fmt_len, struct format_info* restrict fi,
		     va_list* restrict va)
{
	yalc_clear_fi(fi);

	bool width_from_arg = false;
	bool prec_from_arg = false;
	bool has_bitlen = false;
	int i = 0;
	for (i = 1; i < fmt_len; i++) {
		const char cur_char = fmt[i];

		switch (cur_char) {
		/* Flags field */
		case '#':
			fi->flags |= FFLAG_ALT;
			continue;
		case '0':
			/* Zero can also be part of the width/precision
			 * field but only if it's not its first digit. */
			if ((fi->flags & SFLAG_HAS_PREC) && !prec_from_arg) {
				fi->precision *= 10;
			} else if ((fi->flags & SFLAG_HAS_WIDTH) && !width_from_arg) {
				fi->width *= 10;
			} else if (!(fi->flags & FFLAG_ZERO_PAD))
				fi->flags |= FFLAG_ZERO_PAD;
			else
				return -EINVAL;
			continue;
		case '-':
			fi->flags |= FFLAG_LEFT_ADJ;
			continue;
		case ' ':
			fi->flags |= FFLAG_NOPSIGN;
			continue;
		case '+':
			fi->flags |= FFLAG_ADDSIGN;
			continue;
		case '\'':
			/* Just ignore it */
			continue;
		/* Width/precision field */
		case '1' ... '9':
			/* Check first if this is part of w/wf since
			 * length modifiers come after precision/width. */
			if (has_bitlen) {
				fi->int_bitlen *=10;
				fi->int_bitlen += (cur_char - '0');
				if(fi->int_bitlen > (sizeof(uintmax_t) * 8))
					return -EINVAL;
				continue;
			}
			/* Precision field comes after the width field
			 * so if precision field is marked with '.'
			 * these digits here are for precision. */
			if (!(fi->flags & SFLAG_HAS_PREC)) {
				/* Can't have width/precision both as a value
				 * and from an argument (via *). Note that since
				 * a negative precision argument will clear SFLAG_HAS_PREC
				 * we include both width/prec_from arg here. */
				if (width_from_arg || prec_from_arg)
					return -EINVAL;
				fi->flags |= SFLAG_HAS_WIDTH;
				/* Make sure we won't overflow on *10 */
				if (fi->width > (INT_MAX / 10))
					return -EINVAL;
				fi->width *= 10;
				fi->width += (cur_char - '0');
			} else {
				if (prec_from_arg)
					return -EINVAL;
				if (fi->precision > (INT_MAX / 10))
					return -EINVAL;
				fi->precision *= 10;
				fi->precision += (cur_char - '0');
			}
			continue;
		case '.':
			if (!(fi->flags & SFLAG_HAS_PREC))
				fi->flags |= SFLAG_HAS_PREC;
			else
				return -EINVAL;
			continue;
		case '*':
			/* Got an unexpected third * */
			if (width_from_arg && prec_from_arg)
				return -EINVAL;

			/* Since width/prec args come "before the
			 * argument (if any) to be converted" we can
			 * just consume them here before yalc_xprintf
			 * consumes the argument. */
			if (!(fi->flags & SFLAG_HAS_PREC)) {
				fi->flags |= SFLAG_HAS_WIDTH;
				int width_arg = va_arg(*va, int);
				/* "A negative field width argument is taken as a - flag
				 * followed by a positive field width" */
				if (width_arg < 0) {
					fi->flags |= FFLAG_LEFT_ADJ;
					width_arg = -width_arg;
				}
				fi->width = width_arg;
				width_from_arg = true;
			} else {
				int prec_arg = va_arg(*va, int);
				/* "A negative precision argument is taken as if the
				 * precision were omitted" */
				if (prec_arg < 0) {
					fi->flags &= ~SFLAG_HAS_PREC;
					continue;
				}
				fi->flags |= SFLAG_HAS_PREC;
				fi->precision = prec_arg;
				prec_from_arg = true;
			}
			continue;
		case '$':
			/* We don't support this mess.
			 * XXX: compile-time check for this ? */
			return -ENOTSUP;
		/* Length modifier */
		case 'h':
			if (fi->flags & LFLAG_HALF)
				fi->flags |= LFLAG_HALF_HALF;
			else if (fi->flags & LFLAG_HALF_HALF)
				return -EINVAL;
			else
				fi->flags |= LFLAG_HALF;
			continue;
		case 'l':
			if (fi->flags & LFLAG_LONG)
				fi->flags |= LFLAG_LONG_LONG;
			else if (fi->flags & LFLAG_LONG_LONG)
				return -EINVAL;
			else
				fi->flags |= LFLAG_LONG;
			continue;
		case 'L':
			/* LL is not valid */
			if (fi->flags & LFLAG_MASK)
				return -EINVAL;
			else
				fi->flags |= LFLAG_LONG;
			continue;
		case 'j':
			if (fi->flags & LFLAG_MASK)
				return -EINVAL;
			else
				fi->flags |= LFLAG_INTMAX;
			continue;
		case 'z':
			if (fi->flags & LFLAG_MASK)
				return -EINVAL;
			else
				fi->flags |= LFLAG_SIZET;
			continue;
		case 't':
			if (fi->flags & LFLAG_MASK)
				return -EINVAL;
			else
				fi->flags |= LFLAG_PTRDIFF;
			continue;
		case 'w':
			if (has_bitlen || (fi->flags & LFLAG_MASK))
				return -EINVAL;
			has_bitlen = true;
			/* No worries for over-reading here since
			 * fmt is NULL-terminated so worst case
			 * fmt[i + 1] is the NULL terminator. */
			if (fmt[i + 1] == 'f') {
				fi->flags |= SFLAG_IS_FAST;
				i++;
			}
			continue;
		/* Type */
		case 'c':
			/* Wide chars (wchar_t, %lc) not supported. Regular %c works fine
			 * with multibyte encodings (e.g., UTF-8) since it just outputs bytes. */
			if (fi->flags & LFLAG_LONG)
				return -ENOTSUP;
			fi->flags |= TFLAG_CHAR;
			break;
		case 's':
			/* Wide strings (wchar_t*, %ls) not supported. Regular %s works fine
			 * with multibyte encodings (e.g., UTF-8) since it just outputs bytes. */
			if (fi->flags & LFLAG_LONG)
				return -ENOTSUP;
			fi->flags |= TFLAG_STRING;
			break;
		case 'i':
		case 'd':
			fi->flags |= SFLAG_IS_SIGNED;
			/* Fallthrough */
		case 'u':
			fi->flags |= TFLAG_INT;
			break;
		case 'X':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'x':
			fi->flags |= TFLAG_INT;
			fi->radix = RADIX_HEX;
			break;
		case 'B':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'b':
			fi->flags |= TFLAG_INT;
			fi->radix = RADIX_BIN;
			break;
		case 'p':
			/* Same as %#lx
			 * XXX: preprocessor check #x vs #lx ?
			 */
			fi->flags |= FFLAG_ALT;
			fi->flags |= LFLAG_LONG;
			fi->flags |= TFLAG_INT;
			fi->radix = RADIX_HEX;
			break;
		case 'o':
			fi->flags |= TFLAG_INT;
			fi->radix = RADIX_OCT;
			break;
		case 'E':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'e':
			fi->flags |= (SFLAG_IS_EFORM | TFLAG_DOUBLE);
			break;
		case 'F':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'f':
			fi->flags |= TFLAG_DOUBLE;
			break;
		case 'G':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'g':
			fi->flags |= (SFLAG_IS_GFORM | TFLAG_DOUBLE);
			break;
		case 'A':
			fi->flags |= SFLAG_UPPERCASE;
			/* Fallthrough */
		case 'a':
			fi->flags |= (SFLAG_IS_AFORM | TFLAG_DOUBLE);
			fi->radix = RADIX_HEX;
			break;
		/* Also not supported:
		 * n (output of printed characters to arg)
		 * m (glibc extension for strerror())
		 */
		case 'n':
		case 'm':
			return -ENOTSUP;
		default:
			continue;
		}

		/* If we are here we should have a type, verify just in case */
		if ((fi->flags & TFLAG_MASK) == 0)
			return -EINVAL;
		break;
	}

	/* Only one type can be specified */
	uint32_t type = fi->flags & TFLAG_MASK;
	if (type & (type -1))
		return -EINVAL;

	/* Only one of half/long flags can be specified */
	if ((fi->flags & LFLAG_HALF) && (fi->flags & LFLAG_LONG))
		return -EINVAL;

	/* Zero flag is ignored on integers when precision is set */
	if ((fi->flags & TFLAG_INT) && (fi->flags & FFLAG_ZERO_PAD) && (fi->flags & SFLAG_HAS_PREC))
		fi->flags &= ~FFLAG_ZERO_PAD;

	/* Zero flag is ignored if - flag is set*/
	if ((fi->flags & FFLAG_ZERO_PAD) && (fi->flags & FFLAG_LEFT_ADJ))
		fi->flags &= ~FFLAG_ZERO_PAD;

	return i;
}

/*****************\
* Output handling *
\*****************/

/* Output a character either to a buffer or to stdout
 * (our console, we don't have multiple streams/descriptors here
 * so no fprintf). Note that according to the spec {v}s{n}printf
 * doesn't stop processing when the buffer is over, it just keeps
 * going and returns the number of characters that would be needed
 * for the processed message (allowing someone to do a call with
 * output len = 0 to determine how much memory should be allocated
 * for the output). This isn't much different than printing the
 * character to stdout and counting printed characters, in both
 * cases we move on to the next character, so to avoid
 * duplication/complexity define a function that processes a
 * character in both cases. */
static void
yalc_pf_char_out(char in, struct output_info* out)
{
	if (!out->outbuff) {
		putchar(in);
	} else if (out->outbuff_len > 0) {
		if (out->chars_out < out->outbuff_len - 1)
			out->outbuff[out->chars_out] = in;
		/* Always null-terminate output */
		else if (out->chars_out == out->outbuff_len - 1)
			out->outbuff[out->chars_out] = '\0';
	}
	out->chars_out++;
}

static void
yalc_pf_chars_out(const char* restrict in, size_t in_len, bool reverse,
		  struct output_info* restrict out)
{
	if (reverse)
		for (size_t i = in_len; i-- > 0; )
			yalc_pf_char_out(in[i], out);
	else
		for (size_t i = 0; i < in_len; i++)
			yalc_pf_char_out(in[i], out);
}

static inline void
yalc_pf_pad(size_t pad_len, char pad, struct output_info* out)
{
	for (size_t i = 0; i < pad_len; i++)
		yalc_pf_char_out(pad, out);
}

/* Write a char buffer containing a number to output, using the
 * requested formatting parameters (field width/prefix etc). */
void __attribute__((weak))
yalc_pf_field_out(const struct field_data* restrict fld,
			const struct format_info* restrict fi,
			struct output_info* restrict out)
{
	int prebuf_pad = 0;
	int postbuf_pad = 0;
	int width_pad = 0;
	if (fi->num_pad) {
		if (fi->num_pad < 0)
			prebuf_pad = -fi->num_pad;
		else
			postbuf_pad = fi->num_pad;
	}

	const size_t field_width = fld->prefix_len +
				   prebuf_pad + fld->inbuff_len + postbuf_pad +
				   fld->suffix_len + fi->suffix_pad + ((fld->sign_char != '\0') ? 1 : 0);

	if (fi->width > field_width)
		width_pad = fi->width - field_width;

	/* Extract padding flags (bits 1-2) */
	const int pad_flags = (fi->flags >> 1) & 3;

	switch (pad_flags) {
	case 0:  /* Neither flag - normal right padding with spaces */
		/* pad(spaces)|prefix|num|suffix */
		yalc_pf_pad(width_pad, ' ', out);
		yalc_pf_chars_out(fld->prefix, fld->prefix_len, fi->flags & TRFLAG_REVERSE_PREFIX, out);
		yalc_pf_pad(prebuf_pad, '0', out);
		yalc_pf_chars_out(fld->inbuff, fld->inbuff_len, fi->flags & TRFLAG_REVERSE_NUM, out);
		yalc_pf_pad(postbuf_pad, '0', out);
		yalc_pf_chars_out(fld->suffix, fld->suffix_len, fi->flags & TRFLAG_REVERSE_SUFFIX, out);
		yalc_pf_pad(fi->suffix_pad, '0', out);
		break;
	case 1:  /* FFLAG_ZERO_PAD only */
		/* For doubles:
		 * sign|pad(zeroes)|prefix|num|suffix
		 * For ints:
		 * prefix|pad(zeros)|num|suffix */
		if (fi->flags & TFLAG_DOUBLE) {
			if (fld->sign_char != '\0')
				yalc_pf_char_out(fld->sign_char, out);
			yalc_pf_pad(width_pad, '0', out);
			yalc_pf_chars_out(fld->prefix, fld->prefix_len, fi->flags & TRFLAG_REVERSE_PREFIX, out);
		} else {
			yalc_pf_chars_out(fld->prefix, fld->prefix_len, fi->flags & TRFLAG_REVERSE_PREFIX, out);
			yalc_pf_pad(width_pad, '0', out);
		}
		yalc_pf_chars_out(fld->inbuff, fld->inbuff_len, fi->flags & TRFLAG_REVERSE_NUM, out);
		yalc_pf_pad(postbuf_pad, '0', out);
		yalc_pf_chars_out(fld->suffix, fld->suffix_len, fi->flags & TRFLAG_REVERSE_SUFFIX, out);
		yalc_pf_pad(fi->suffix_pad, '0', out);
		break;
	case 2:  /* FFLAG_LEFT_ADJ only */
	case 3:  /* Both flags - LEFT_ADJ overrides ZERO_PAD */
		/* prefix|num|suffix|pad(spaces) */
		yalc_pf_chars_out(fld->prefix, fld->prefix_len, fi->flags & TRFLAG_REVERSE_PREFIX, out);
		yalc_pf_pad(prebuf_pad, '0', out);
		yalc_pf_chars_out(fld->inbuff, fld->inbuff_len, fi->flags & TRFLAG_REVERSE_NUM, out);
		yalc_pf_pad(postbuf_pad, '0', out);
		yalc_pf_chars_out(fld->suffix, fld->suffix_len, fi->flags & TRFLAG_REVERSE_SUFFIX, out);
		yalc_pf_pad(fi->suffix_pad, '0', out);
		yalc_pf_pad(width_pad, ' ', out);
		break;
	}

}

void
yalc_pf_field_out_simple(const struct field_data* restrict fld,
			const struct format_info* restrict fi,
			struct output_info* restrict out)
{
	int prebuf_pad = 0;
	int postbuf_pad = 0;
	int width_pad = 0;
	if (fi->num_pad) {
		if (fi->num_pad < 0)
			prebuf_pad = -fi->num_pad;
		else
			postbuf_pad = fi->num_pad;
	}

	const int field_width = fld->prefix_len +
				prebuf_pad + fld->inbuff_len + postbuf_pad +
				fld->suffix_len + fi->suffix_pad + ((fld->sign_char != '\0') ? 1 : 0);

	if (fi->width > field_width)
		width_pad = fi->width - field_width;

	yalc_pf_pad(width_pad, ' ', out);
	if (fld->sign_char != '\0')
		yalc_pf_char_out(fld->sign_char, out);
	yalc_pf_chars_out(fld->prefix, fld->prefix_len, fi->flags & TRFLAG_REVERSE_PREFIX, out);
	yalc_pf_pad(prebuf_pad, '0', out);
	yalc_pf_chars_out(fld->inbuff, fld->inbuff_len, fi->flags & TRFLAG_REVERSE_NUM, out);
	yalc_pf_pad(postbuf_pad, '0', out);
	yalc_pf_chars_out(fld->suffix, fld->suffix_len, fi->flags & TRFLAG_REVERSE_SUFFIX, out);
	yalc_pf_pad(fi->suffix_pad, '0', out);
	return;
}

/*******************\
* Integer to string *
\*******************/

/* The longest representation is the binary one for %b/B*/
#define INTBUFF_LEN	64

static int
yalc_itora(char* restrict num_buff, uintmax_t val, struct format_info* restrict fi)
{
	int digit = 0;
	int i = 0;
	const char dec_base = '0';
	const char hex_base = (fi->flags & SFLAG_UPPERCASE) ? 'A' : 'a';

	/* Write val in num_buff backwards, we do it like this
	 * so that the compiler can optimize div/mod with a
	 * constant. */
	switch (fi->radix) {
		case RADIX_DEC:
			do {
				digit = val % 10;
				num_buff[i++] = dec_base + digit;
				val /= 10;
			} while (val);
			break;
		case RADIX_HEX:
			do {
				digit = val % 16;
				if (digit < 10)
					num_buff[i++] = dec_base + digit;
				else
					num_buff[i++] = hex_base + digit - 10;
				val /= 16;
			} while (val);
			break;
		case RADIX_OCT:
			do {
				digit = val % 8;
				num_buff[i++] = dec_base + digit;
				val /= 8;
			} while (val);
			break;
		case RADIX_BIN:
			do {
				digit = val % 2;
				num_buff[i++] = dec_base + digit;
				val /= 2;
			} while (val);
			break;
		default:
			break;
	}

	fi->flags |= TRFLAG_REVERSE_NUM;
	return i;
}

static void
yalc_putll(uintmax_t val, struct format_info* restrict fi, struct output_info* restrict out)
{
	char num_buff[INTBUFF_LEN];
	char prefix[2];		/* 0 for alt o, 0x for alt x, sign for signed ints */
	size_t prefix_len = 0;

	/* Do we need to put a prefix ? */
	switch (fi->radix) {
	case RADIX_DEC:
		if (fi->flags & SFLAG_IS_SIGNED) {
			bool neg =  false;
			intmax_t sval = (intmax_t)val;
			if (sval < 0) {
				neg = true;
				/* Two's complement safe negation to handle INTMAX_MIN */
				val = (uintmax_t)(-(sval + 1)) + 1;
			}
			if (neg)
				prefix[prefix_len++] = '-';
			else if (fi->flags & FFLAG_ADDSIGN)
				prefix[prefix_len++] = '+';
			else if (fi->flags & FFLAG_NOPSIGN)
				prefix[prefix_len++] = ' ';
		}
		break;
	case RADIX_HEX:
		if (fi->flags & FFLAG_ALT && val != 0) {
			prefix[prefix_len++] = '0';
			prefix[prefix_len++] = (fi->flags & SFLAG_UPPERCASE) ? 'X' : 'x';
		}
		break;
	case RADIX_BIN:
		if (fi->flags & FFLAG_ALT && val != 0) {
			prefix[prefix_len++] = '0';
			prefix[prefix_len++] = (fi->flags & SFLAG_UPPERCASE) ? 'B' : 'b';
		}
		break;
	default:
		break;
	}

	size_t len = yalc_itora(num_buff, val, fi);

	/* "For o conversion, it increases the precision, if and only if necessary,
	 * to force the first digit of the result to be a zero (if the value and
	 * precision are both 0, a single 0 is printed)." */
	if (fi->radix == RADIX_OCT && (fi->flags & FFLAG_ALT)) {
		if (((num_buff[len - 1] != '0')) &&
		    (fi->precision <= len)) {
			fi->flags |= SFLAG_HAS_PREC;
			fi->precision = len + 1;
		} else if (!fi->precision && val == 0)
			fi->precision = 1;
	}

	/* Handle precision */
	if (fi->flags & SFLAG_HAS_PREC) {
		if (fi->precision > len)
			fi->num_pad = -(fi->precision - len);
		else if (!fi->precision && val == 0)
			len = 0;
	}

	struct field_data fld = {
		.inbuff = num_buff,
		.inbuff_len = len,
		.prefix = prefix,
		.prefix_len = prefix_len,
		.suffix = NULL,
		.suffix_len = 0,
		.sign_char = '\0'
	};
	return yalc_pf_field_out(&fld, fi, out);
}

/******************\
* Double to string *
\******************/

/* For the full story check out ryu.c */

#define EXPONENT_BITS	11
#define EXPONENT_MASK	((1U << EXPONENT_BITS) - 1)
#define SIGNIFICAND_BITS 52
#define SIGNIFICAND_MASK ((1ULL << SIGNIFICAND_BITS) - 1)
static const char *nan_str[2] = {"nan", "NAN"};
static const char *inf_str[2] = {"inf", "INF"};

/* Ryu d2d implementation (check out ryu.c) */
extern void yalc_double_to_decimal(uint32_t exponent_biased, uint64_t significand,
				   int32_t* exponent10, uint64_t* fraction10);
extern uint64_t yalc_round_to_digits(uint64_t value, int current_digits, int desired_digits);

static size_t
yalc_count_digits(uint64_t val, int radix)
{
	size_t len = 1;
	switch(radix) {
		case RADIX_HEX:
			/* Each hex digit takes 4 bits */
			while (val >>= 4)
				len++;
			break;
		default:
		{
			uint64_t c = 10;
			/* Use multiplication instead of division, the shifts
			 * are probably what the compiler would do anyway to
			 * multiply by 10 (8 + 2). */
			while (c <= val) {
				len++;
				c = (c << 3) + (c << 1);
			}
			break;
		}
	}
	return len;
}

void __attribute__((weak))
yalc_putd(double in, struct format_info* restrict fi, struct output_info* restrict out)
{
	char num_buff[INTBUFF_LEN] = {0};
	char prefix_buff[5] = {0};	/* Sign, 0x for a form, and d. when needed */
	char suffix_buff[6] = {0};	/* For a/e form "[e/p][+/-]XXXX"
					 * (-324 < x < +308 for e, -(1022 + 52) < x < 1023 for a) */
	size_t len = 0;
	size_t prefix_len = 0;
	size_t suffix_len = 0;

	/* This will allow us to override num/prefix/suffix
	 * later on in specific cases for simplicity. */
	const char* num = num_buff;
	char* prefix = prefix_buff;
	char* suffix = suffix_buff;

	/* Parse the binary format */
	union {
		double d;
		uint64_t i;
	} inval = {in};
	const bool sign = ((inval.i & (1ULL << 63)) != 0);
	const uint32_t exponent_biased = (uint32_t) ((inval.i >> SIGNIFICAND_BITS) & EXPONENT_MASK);
	uint64_t significand = (inval.i & SIGNIFICAND_MASK);
	const int is_uppercase = (fi->flags & SFLAG_UPPERCASE) ? 1 : 0;

	/* Set default precision to 6 (or 13 for a form) if not set
	 * Note that in case of a form 13 = (SIGNIFICAND_BITS + 3) / 4
	 * so that "precision is sufficient for an exact representation
	 * of the value". */
	if (!(fi->flags & SFLAG_HAS_PREC))
		fi->precision = (fi->flags & SFLAG_IS_AFORM) ? 13 : 6;

	/* Maximum precision for doubles:
	 *
	 * IEEE 754 doubles have ~17 significant decimal digits (15-17 roundtrip via Ryu),
	 * with exponents from -324 to +308. For hex (a-form): 13 hex digits in the
	 * significand, exponents from -1074 to +1023. Any precision beyond this just
	 * produces zero padding — the extra digits carry no information.
	 *
	 * We cap at 4096 to prevent abuse (e.g., requesting gigabytes of zeros) while
	 * meeting C11 spec's "at least 4095" per conversion minimum. */
	if (fi->precision > 4096)
		fi->precision = 4096;

	/* In case of FFLAG_ZERO_PAD since we include part of the number in the
	 * prefix we can't also use it for the sign otherwise we'll get 000+ddd.dd...
	 * instead of +000ddd.dd..., so use sign_char to pass to yalc_pf_field_out */
	char sign_char = '\0';
	if (sign)
		sign_char = '-';
	else if (fi->flags & FFLAG_ADDSIGN)
		sign_char = '+';
	else if (fi->flags & FFLAG_NOPSIGN)
		sign_char = ' ';

	if (!(fi->flags & FFLAG_ZERO_PAD) && sign_char != '\0') {
		prefix[prefix_len++] = sign_char;
		sign_char = '\0';
	}

	/* Is NaN or Infinity ? */
	if (exponent_biased == EXPONENT_MASK) {
		fi->precision = 0;
		/* With fraction -> NaN */
		if (significand) {
			num = nan_str[is_uppercase];
		/* No fraction -> Infinity */
		} else {
			num = inf_str[is_uppercase];
		}
		len = 3;
		goto done;
	} else if (exponent_biased == 0) {
		/* No fraction -> Zero */
		if (!significand) {
			/* For a form 0x */
			if (fi->flags & SFLAG_IS_AFORM) {
				prefix[prefix_len++] = '0';
				prefix[prefix_len++] = is_uppercase ? 'X' : 'x';
			}
			/* a/e/f-form zero (0.00...0) but not for g-form unless
			 * the alt flag is specified. */
			prefix[prefix_len++] = '0';
			if (((fi->flags & SFLAG_IS_GFORM) != SFLAG_IS_GFORM) ||
			    (fi->flags & FFLAG_ALT)) {
				if (fi->precision) {
					prefix[prefix_len++] = '.';
					fi->num_pad = fi->precision;
				}
			}
			/* e-form zero (0.00...0 e+00)
			 * Note: C11 has a space before e, POSIX doesn't.
			 * I'm going with C11. Note: the check for SFLAG_IS_EFORM
			 * is there go cover g-form, since on g-form 0 is
			 * displayed as in f-form (without +e00). */
			if ((fi->flags & SFLAG_IS_GFORM) == SFLAG_IS_EFORM) {
				suffix[suffix_len++] = is_uppercase ? 'E' : 'e';
				suffix[suffix_len++] = '+';
				suffix[suffix_len++] = '0';
				suffix[suffix_len++] = '0';
			} else
			/* a-form zero (0x0... p+0) */
			if (fi->flags & SFLAG_IS_AFORM) {
				suffix[suffix_len++] = is_uppercase ? 'P' : 'p';
				suffix[suffix_len++] = '+';
				suffix[suffix_len++] = '0';
			}
			goto done;
		}
	}


	/* Extract the 2-bit form field (bits 22-24) */
	const int form = (fi->flags >> 22) & 7;
	int frac_digits = 0;
	int int_digits = 0;
	int decimal_digits = 0;
	int32_t exponent10 = 0;
	uint64_t fraction10 = 0;
	int32_t exp_val = 0;
	/* No need to switch to decimal for a-form */
	if (form < 4) {
		yalc_double_to_decimal(exponent_biased, significand, &exponent10, &fraction10);
		frac_digits = yalc_count_digits(fraction10, fi->radix);
	}

	bool add_trailing_zeros = true;
	switch (form) {
	case 4: /* SFLAG_IS_AFORM */
		/* In order to deal with normal and subnormal numbers in the
		 * same way, we can normalize the subnormal such as the leading
		 * bit is always 1. This is allowed by the spec since it mandates
		 * that it "is nonzero if the argument is a normalized floating-point
		 * number and is otherwise unspecified", so unspecified could also be 1. */
		if (exponent_biased == 0) {
			/* Subnormal number -> normalize it */
			exp_val = -1022;  /* Fixed exponent for subnormals */
			while (!(significand & (1ULL << SIGNIFICAND_BITS))) {
				significand <<= 1;
				exp_val--;
			}
		} else {
			/* Normalized number: remove bias and restore the hidden bit */
			exp_val = (int)exponent_biased - 1023;
			significand |= 1ULL << SIGNIFICAND_BITS;
		}

		/* Round the full 53bit signifficant if needed */
		if (fi->precision < 13) {
			const int bits_to_keep = fi->precision * 4;
			const int bits_to_remove = 52 - bits_to_keep;

			if (bits_to_remove > 0) {
				const uint64_t msb = 1ULL << bits_to_remove;
				const uint64_t round_bit = 1ULL << (bits_to_remove - 1);
				const uint64_t sticky_mask = round_bit - 1;

				/* Ties-to-Even Rounding Logic */
				if ((significand & round_bit) &&
				    ((significand & sticky_mask) || (significand & msb))) {
					significand += msb;

					/* Check for carry into the 54th bit (index 53) */
					if (significand & (1ULL << (SIGNIFICAND_BITS + 1))) {
						significand >>= 1;
						exp_val++;
					}
				}
				/* Clear the bits we rounded away */
				significand &= ~(msb - 1);
			}
		}

		/* Leading_digit will be 1, because we forced it to be 1, it would be
		 * at the top of num_buff, grab it an move it in the prefix. */
		len = yalc_itora(num_buff, significand, fi);
		prefix[prefix_len++] = '0';
		prefix[prefix_len++] = is_uppercase ? 'X' : 'x';
		prefix[prefix_len++] = num_buff[--len];
		if (fi->precision || (fi->flags & FFLAG_ALT))
			prefix[prefix_len++] = '.';

		/* Do we need to zero-pad ? */
		if (fi->precision > len)
			fi->num_pad = fi->precision - len;

		/* Do we need to truncate ? */
		if (len > fi->precision) {
			num += (len - fi->precision);
			len = fi->precision;
		}

		/* We'll print [p/P][+/-]XXXX reversed in suffix, starting with XXXX */
		fi->radix = 10;
		if (exp_val < 0)
			suffix_len = yalc_itora(suffix, -exp_val, fi);
		else
			suffix_len = yalc_itora(suffix, exp_val, fi);
		suffix[suffix_len++] = (exp_val < 0) ? '-' : '+';
		suffix[suffix_len++] = is_uppercase ? 'P' : 'p';
		fi->flags |= TRFLAG_REVERSE_SUFFIX;
		break;
	case 3: /* SFLAG_IS_GFORM (0xC00000 = both E/F flags) */
		/* If precision is zero, force it to 1 */
		if (!fi->precision)
			fi->precision = 1;

		if (!(fi->flags & FFLAG_ALT)) {
			add_trailing_zeros = false;
		}

		/* This is a slightly non-compliant behavior to avoid unnecessary complexity:
		 * The C spec says that we should check the exponent value of the e/E convention and
		 * the precision, to decide if we are going to use e/E of f/F. However in the corner
		 * case of rounding 9.9... -> 10.0... the exponent will depend on the precision since
		 * it'll get increased by 1. To avoid doing most of e/E convention twice just for that
		 * rare use case, do the check as if the exponent value was independent from precision.
		 */

		/* actual_value = fraction10 × 10^exponent10
		 *		= (first_digit.remaining × 10^(frac_digits - 1)) × 10^exponent10
		 *		= first_digit.remaining × 10^(exponent10 + frac_digits - 1) */
		exp_val = exponent10 + frac_digits - 1;
		if (fi->precision > exp_val && exp_val >= -4) {
			fi->precision = fi->precision - (exp_val + 1);
			goto f_form;
		}

		fi->precision = fi->precision - 1;
		/* Fallthrough */
	case 2: /* SFLAG_IS_EFORM (0x800000) */
		/* Single case: [+-]d.ddd... [e/E][+/-]XXX */
		int_digits = 1;
		decimal_digits = frac_digits - int_digits;

		/* Round the fractional part to match precision */
		if (fi->precision < decimal_digits) {
			fraction10 = yalc_round_to_digits(fraction10, decimal_digits, fi->precision);
			const int skipped_digits = decimal_digits - fi->precision;
			const int rounded_decimals = yalc_count_digits(fraction10, 10);
			/* Handle the case of 9.9 -> 10.0 */
			if (rounded_decimals > (frac_digits - skipped_digits)) {
				fraction10 /= 10;
				exponent10++;
			}
		/* Just pad with zeroes until we write out precision digits */
		} else if (add_trailing_zeros)
			fi->num_pad = fi->precision - decimal_digits;

		/* Put fraction10 in num_buff, reversed, and get the length
		 * of its representation. */
		len = yalc_itora(num_buff, fraction10, fi);

		/* Move integer part (the last digit on num_buff - the first of fraction10) to prefix */
		prefix[prefix_len++] = num_buff[--len];

		/* We only print decimal-point if we have a positive precision or
		 * the alt flag forces us to (even if there are no digits
		 * following). */
		if ((len > 0 || fi->num_pad) || (fi->flags & FFLAG_ALT))
			prefix[prefix_len++] = '.';

		/* We'll print [e/E][+/-]XXX reversed in suffix, starting with XXX */
		exp_val = exponent10 + decimal_digits;
		if (exp_val < 0)
			suffix_len = yalc_itora(suffix, -exp_val, fi);
		else
			suffix_len = yalc_itora(suffix, exp_val, fi);
		/* Exponent part needs to be at least 2 digits */
		if (suffix_len < 2)
			suffix[suffix_len++] = '0';
		suffix[suffix_len++] = (exp_val < 0) ? '-' : '+';
		suffix[suffix_len++] = is_uppercase ? 'E' : 'e';
		fi->flags |= TRFLAG_REVERSE_SUFFIX;
		break;
	default: /* SFLAG_IS_FFORM only (0x400000) implied by default */
	f_form:
		int_digits = frac_digits + exponent10;

		/* Case 1: exponent10 ≥ 0
		 * 	int_digits = frac_digits + exponent10 ≥ frac_digits
		 * 	Output: <all fraction10 digits><exponent10 trailing zeros>
		 * 	Example: fraction10=123, frac_digits=3, exp10=2 → "12300" */
		if (int_digits >= frac_digits) {
			len = yalc_itora(num_buff, fraction10, fi);
			/* This is an integer followed by exponent10 zeroes */
			fi->num_pad = exponent10;
			if ((add_trailing_zeros && fi->precision) || (fi->flags & FFLAG_ALT)) {
				suffix[suffix_len++] = '.';
				fi->suffix_pad = fi->precision;
			}
		}
		/* Case 2: exponent10 ≤ -frac_digits
		 *	int_digits = frac_digits + exponent10 ≤ 0
		 *	Output: 0.<(-exponent10 - frac_digits) leading zeros><all fraction10 digits>
		 *	Example: fraction10=123, frac_digits=3, exp10=-5 → "0.00123" */
		else if (int_digits <= 0) {
			const int leading_zeroes = (-exponent10 - frac_digits);
			/* Carry handles 0.99.. -> 1.00... */
			int carry = 0;
			decimal_digits = leading_zeroes + frac_digits;
			/* Precision only covers leading zeros (e.g., %.2f of 0.00123) */
			if (fi->precision <= leading_zeroes) {
				len = 0;
				if (add_trailing_zeros)
					fi->num_pad = -fi->precision;
				else
					fi->precision = 0;	 // To also handle decimal-point below
			/* Precision covers the whole thing, no need for rounding, just
			 * zero-pad if needed. */
			} else if (fi->precision >= decimal_digits) {
				len = yalc_itora(num_buff, fraction10, fi);
				fi->num_pad = -leading_zeroes;
				if (add_trailing_zeros)
					fi->suffix_pad = fi->precision - decimal_digits;
			/* Precision includes significant digits but not all of them,
			 * so we need to round up to precision decimal digits. */
			} else {
				int remaining_digits = fi->precision - leading_zeroes;
				fraction10 = yalc_round_to_digits(fraction10, frac_digits, remaining_digits);
				len = yalc_itora(num_buff, fraction10, fi);
				if (len > remaining_digits) {
					carry = 1;
					len = 0;
					if (add_trailing_zeros)
						fi->suffix_pad = remaining_digits;
					else
						fi->precision = 0;
				}
				fi->num_pad = -leading_zeroes;
			}
			prefix[prefix_len++] = '0' + (uint8_t) carry;
			if (fi->precision || (fi->flags & FFLAG_ALT))
				prefix[prefix_len++] = '.';
		}
		/* Case 3: -frac_digits < exponent10 < 0
		 *	int_digits = frac_digits + exponent10, where 0 < int_digits < frac_digits
		 *	Output: <first int_digits of fraction10>.<remaining digits>
		 *	Example: fraction10=12345, frac_digits=5, exp10=-2 → "123.45" */
		else if (int_digits < frac_digits) {
			/* Note: we'll override prefix/suffix pointers to point to the integer/fractional
			 * part inside num_buff, and num will point to the decimal-point stored in prefix_buff[0],
			 * so that we re-use existing output function without extra code for the dot. */
			decimal_digits = frac_digits - int_digits;
			/* Round the fractional part to match precision, carry will just flow to
			 * the integer part. */
			if (fi->precision < decimal_digits) {
				fraction10 = yalc_round_to_digits(fraction10, decimal_digits, fi->precision);
				suffix_len = fi->precision;
			/* Just pad with zeroes until we write out precision digits */
			} else {
				suffix_len = decimal_digits;
			}

			len = yalc_itora(num_buff, fraction10, fi);
			suffix = num_buff;

			/* Add sign on top of num_buff, note that even with 20 digits + the 1 during
			 * rounding we still have one byte left so no worries here. */
			if (prefix_len)
				num_buff[len++] = prefix[0];
			prefix = &num_buff[suffix_len];
			prefix_len = (len - suffix_len);

			/* Trim any zeroes from suffix, they'll be added to suffix_pad based on precision
			 * so that we can remove trailing zeroes by only using suffix_pad */
			const int untrimed_suffix_len = suffix_len;
			for (int i = 0; i < untrimed_suffix_len; i++) {
				if (num_buff[i] == '0')
					suffix_len--;
				else
					break;
			}

			if ((add_trailing_zeros && untrimed_suffix_len) || suffix_len || (fi->flags & FFLAG_ALT)) {
				prefix_buff[0] = '.';
				num = prefix_buff;
				len = 1;
				if (add_trailing_zeros)
					fi->suffix_pad = fi->precision - suffix_len;
			} else
				len = 0;
			fi->flags |= TRFLAG_REVERSE_PREFIX | TRFLAG_REVERSE_SUFFIX;
		}
		break;
	}

 done:
	struct field_data fld = {
		.inbuff = num,
		.inbuff_len = len,
		.prefix = prefix,
		.prefix_len = prefix_len,
		.suffix = suffix,
		.suffix_len = suffix_len,
		.sign_char = sign_char
	};
	return yalc_pf_field_out(&fld, fi, out);
}

void
yalc_putd_stub(double in, struct format_info* restrict fi, struct output_info* restrict out) {
	struct field_data fld;
	yalc_clear_fld(&fld);
	fld.inbuff = "(n/a)";
	fld.inbuff_len = 5;
	return yalc_pf_field_out(&fld, fi, out);
}

/********************************\
* Internal printf implementation *
\********************************/

/*
 * The idea here is to unify vprintf/vsnprintf so that we don't duplicate code,
 * instead we use output_info and let yalibc_pf_char{s}_out do the rest.
 */

static int
yalc_xprintf(struct output_info* restrict out, const char* restrict fmt, va_list* va)
{
	/* Use strlen to remain compliant with the spec, this is unsafe
	 * but in most cases fmt will be a string litteral so it's going to
	 * be NULL-terminated. */
	size_t fmt_len = strlen(fmt);

	for (size_t i = 0; i < fmt_len; i++) {
		uint32_t type = 0;
		int ret = 0;
		char cur_char = fmt[i];

		/* Normal text (also handles the null terminator) */
		if (cur_char != '%') {
			yalc_pf_char_out(cur_char, out);
			continue;
		}

		/* Got a %, we need at least one character afterwards */
		if (i == (fmt_len -1))
			break;

		/* Is % followed by another % ? */
		if (fmt[i + 1] == '%') {
			yalc_pf_char_out('%', out);
			i++;
			continue;
		}

		/* Check for the format string and parse it */
		struct format_info fi;
		ret = yalc_pf_parse_format(fmt + i, fmt_len - i, &fi, va);
		if (ret < 0)
			return ret;
		i += ret;

		/* Fetch arguments and print them out */
		type = fi.flags & TFLAG_MASK;
		switch (type) {
		case TFLAG_INT: {
			/* Integer argument extraction and strict C compliance:
			 *
			 * The C standard (§7.16.1.1) requires va_arg's type to match what was actually
			 * passed, with one exception: "one type is a signed integer type, the other
			 * type is the corresponding unsigned integer type, and the value is representable
			 * in both types."
			 *
			 * The "representable in both types" clause is the tricky part. For negative
			 * values like -42, this technically doesn't hold: -42 isn't representable as
			 * an unsigned int in the mathematical sense, making va_arg(va, unsigned int)
			 * for a signed argument technically UB.
			 *
			 * With C23's mandate of two's complement representation, you might argue that
			 * the bit patterns are identical (0xFFFFFFD6 for -42 as both int and unsigned),
			 * so the value "is" representable. However, since we promote everything to
			 * {u}intmax_t, we also need to take care of zero/sign extension from smaller
			 * types. To avoid casting all the time I use a union here, the zero/sign extension
			 * happens when storing the value, but then we just pass umax to yalc_putll
			 * and let that figure out how to interpret the value via SFLAG_IS_SIGNED. Since
			 * no further zero/sign extensions will be used there (it only works with {u}intmax_t),
			 * that's safe. */
			union {
				intmax_t  smax;
				uintmax_t umax;
			} intval = {0};

			bool is_signed = (fi.flags & SFLAG_IS_SIGNED);

			/* Determine integer's bitlen */
			if (!fi.int_bitlen) {
				/* Note: signed and unsigned types have the same size according
				 * to §6.2.5 paragraph 8, that's true also for fast types. So it's
				 * ok to use the unsigned variants here for sizeof. */
				if (fi.flags & LFLAG_INTMAX)
					fi.int_bitlen = sizeof(uintmax_t);
				else if (fi.flags & LFLAG_LONG_LONG)
					fi.int_bitlen = sizeof(unsigned long long);
				else if (fi.flags & LFLAG_LONG)
					fi.int_bitlen = sizeof(unsigned long);
				else if (fi.flags & LFLAG_SIZET)
					fi.int_bitlen = sizeof(size_t);
				else if (fi.flags & LFLAG_PTRDIFF)
					fi.int_bitlen = sizeof(ptrdiff_t);
				else if (fi.flags & LFLAG_HALF)
					fi.int_bitlen = sizeof(unsigned short int);
				else if (fi.flags & LFLAG_HALF_HALF)
					fi.int_bitlen = sizeof(unsigned char);
				else
					fi.int_bitlen = sizeof(unsigned int);
				fi.int_bitlen *= 8;
			} else if (fi.flags & SFLAG_IS_FAST) {
				switch(fi.int_bitlen) {
					case 8:
						fi.int_bitlen = sizeof(uint_fast8_t);
						break;
					case 16:
						fi.int_bitlen = sizeof(uint_fast16_t);
						break;
					case 32:
						fi.int_bitlen = sizeof(uint_fast32_t);
						break;
					case 64:
						fi.int_bitlen = sizeof(uint_fast64_t);
						break;
					default:
						return -EINVAL;
				}
				fi.int_bitlen *= 8;
			}

			/* Consume and sign/zero extend / mask as needed */
			switch(fi.int_bitlen) {
				case 8:
					if (is_signed)	intval.smax = (int8_t)va_arg(*va, int);
					else		intval.umax = (uint8_t)va_arg(*va, unsigned int);
					break;
				case 16:
					if (is_signed)	intval.smax = (int16_t)va_arg(*va, int);
					else		intval.umax = (uint16_t)va_arg(*va, unsigned int);
					break;
				case 32:
					if (is_signed)	intval.smax = va_arg(*va, int32_t);
					else		intval.umax = va_arg(*va, uint32_t);
					break;
				case 64:
					if (is_signed)	intval.smax = va_arg(*va, int64_t);
					else		intval.umax = va_arg(*va, uint64_t);
					break;
				default:
					return -EINVAL;
			}

			/* Pass unsigned representation to formatter */
			yalc_putll(intval.umax, &fi, out);
			break;
		}
		case TFLAG_DOUBLE: {
			/* We don't support quad precision (long doubles) but
			 * we should consume it so that we don't leave garbage
			 * in the stack. However, casting long double to double
			 * pulls in soft-float library functions (__trunctfdf2)
			 * which add ~600 bytes to the binary. Instead, we consume
			 * the argument but print "(n/a)" for long doubles. */
			if (fi.flags & LFLAG_LONG) {
				/* Consume the long double argument to maintain stack alignment */
				(void) va_arg(*va, long double);
				/* Print placeholder instead of attempting conversion */
				struct field_data fld;
				yalc_clear_fld(&fld);
				fld.inbuff = "(n/a)";
				fld.inbuff_len = 5;
				yalc_pf_field_out(&fld, &fi, out);
			} else {
				double dval = va_arg(*va, double);
				yalc_putd(dval, &fi, out);
			}
			break;
		}
		/* Note: We don't support wide chars (wchar_t, via %lc/%ls).
		 * This is acceptable since wide char support is conditional on __STDC_ISO_10646__.
		 * Regular %c/%s work fine with multibyte encodings like UTF-8. */
		case TFLAG_CHAR: {
			char charval = (char) va_arg(*va, int);
			struct field_data fld;
			yalc_clear_fld(&fld);
			fld.inbuff = &charval;
			fld.inbuff_len = 1;
			yalc_pf_field_out(&fld, &fi, out);
			break;
		}
		case TFLAG_STRING: {
			char* strval = va_arg(*va, char*);
			struct field_data fld;
			yalc_clear_fld(&fld);
			if (!strval) {
				fld.inbuff = "(null)";
				fld.inbuff_len = 6;
				yalc_pf_field_out(&fld, &fi, out);
			} else {
				fld.inbuff = strval;
				fld.inbuff_len = 6;
				if (fi.flags & SFLAG_HAS_PREC && fi.precision >= 0)
					fld.inbuff_len = strnlen(strval, fi.precision);
				else
					fld.inbuff_len = strnlen(strval, YALC_DEFAULT_STR_PREC);
				yalc_pf_field_out(&fld, &fi, out);
			}
			break;
		}
		default:
			continue;
		}
	}

	return out->chars_out;
}

/**************\
* Entry points *
\**************/

int
vsnprintf(char* restrict outbuff, size_t outbuff_len, const char* restrict fmt, va_list va)
{
	struct output_info out = {0};
	out.outbuff = outbuff;
	out.outbuff_len = outbuff_len;
	return yalc_xprintf(&out, fmt, &va);
}

int
snprintf(char* restrict outbuff, size_t outbuff_len, const char* restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	int ret = vsnprintf(outbuff, outbuff_len, fmt, va);
	va_end(va);
	return ret;
}

int
vsprintf(char* restrict outbuff, const char* restrict fmt, va_list va)
{
	return vsnprintf(outbuff, SIZE_MAX, fmt, va);
}

int
sprintf(char* restrict outbuff, const char* restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	int ret = vsnprintf(outbuff, SIZE_MAX, fmt, va);
	va_end(va);
	return ret;
}

int
vprintf(const char* restrict fmt, va_list va)
{
	struct output_info out = {0};
	/* Acquire lock so that we don't have two instances
	 * printing at the same time. */
	lock_acquire(&printf_lock);
	int ret = yalc_xprintf(&out, fmt, &va);
	lock_release(&printf_lock);
	return ret;
}

int
printf(const char* restrict fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	int ret = vprintf(fmt, va);
	va_end(va);
	return ret;
}
