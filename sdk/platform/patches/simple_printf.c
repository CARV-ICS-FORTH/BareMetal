/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Simple printf patch - provides lightweight implementations of printf
 * formatting functions. When linked with the platform/yalibc, these strong
 * symbols override the weak implementations, and LTO eliminates the full
 * (heavier) versions.
 *
 * Use this patch for applications that don't need:
 * - Floating point formatting (%f, %e, %g, %a)
 * - Complex field formatting features
 *
 * Savings: ~2-3KB of code size
 *
 * Usage: Link this .o file with your application
 */

/* Forward declarations for types / simple implementations from printf.c */
struct field_data;
struct format_info;
struct output_info;

extern void yalc_pf_field_out_simple(const struct field_data* restrict fld,
				const struct format_info* restrict fi,
				struct output_info* restrict out);

extern void yalc_putd_stub(double in, struct format_info* restrict fi,
			struct output_info* restrict out);

/* Overrides */
void
yalc_pf_field_out(const struct field_data* restrict fld,
		const struct format_info* restrict fi,
		struct output_info* restrict out)
{
	yalc_pf_field_out_simple(fld, fi, out);
}

void
yalc_putd(double in, struct format_info* restrict fi, struct output_info* restrict out)
{
	yalc_putd_stub(in, fi, out);
}
