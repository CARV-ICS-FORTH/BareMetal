/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Shared CSR-probe helpers for the privileged units (hart_probe_mmode.c,
 * hart_probe_smode.c, hart_priv_zexts.c). Each unit is a single
 * `int probe(unsigned flags)` registered with REGISTER_HART_PRIV_{M,S,Z}; most just
 * return one of these helpers.
 *
 * The helpers fetch the current hart_state themselves and key off hs->error, which
 * the platform trap handler sets on an unsupported CSR access. Each evaluates to 0
 * (present) or PROBE_ABSENT. They are macros, not functions: RISC-V encodes the CSR
 * as a 12-bit immediate, so the CSR argument must be a compile-time constant.
 */

#ifndef HART_PRIV_CAPS_H
#define HART_PRIV_CAPS_H

#include <platform/riscv/hart.h>	/* For hart_state + csr ops + hart_get_hstate_self */
#include <platform/utils/bitfield.h>	/* For FIELD_GET_SHIFT/MASK */
#include <test_framework.h>		/* For PROBE_ABSENT */
#include <stdint.h>

/* Present iff the CSR is accessible. */
#define hart_probe_csr_exists(csr)					\
({									\
	struct hart_state *_hs = hart_get_hstate_self();		\
	_hs->error = 0;							\
	(void)csr_read(csr);						\
	_hs->error ? PROBE_ABSENT : 0;					\
})

/* Present iff a single-bit CSR field sticks when written (then restored). */
#define hart_probe_csr_bit(csr, mask)					\
({									\
	struct hart_state *_hs = hart_get_hstate_self();		\
	int _r = PROBE_ABSENT;						\
	_hs->error = 0;							\
	csr_set_bits((csr), (mask));					\
	if (!_hs->error && (csr_read(csr) & (mask))) {			\
		csr_clear_bits((csr), (mask));				\
		_r = 0;							\
	}								\
	_r;								\
})

/* Present iff a multi-bit CSR field accepts `test_val` (then restored). */
#define hart_probe_csr_field(csr, field_def, test_val)			\
({									\
	struct hart_state *_hs = hart_get_hstate_self();		\
	int _r = PROBE_ABSENT;						\
	uint64_t _v = (uint64_t)(test_val) << FIELD_GET_SHIFT(field_def);\
	_hs->error = 0;							\
	csr_set_bits((csr), _v);					\
	if (!_hs->error && (csr_read(csr) & FIELD_GET_MASK(field_def))) {\
		csr_clear_bits((csr), _v);				\
		_r = 0;							\
	}								\
	_r;								\
})

#endif /* HART_PRIV_CAPS_H */
