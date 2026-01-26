/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2023-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2023-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */
 
/*
 * Generic wrapper functions for acquiring and releasing a
 * spin-lock using C11 atomics.
 */

#ifndef _LOCK_H
#define _LOCK_H

#include <stdatomic.h>		/* For C11 atomic types / accessors */
#include <platform/riscv/csr.h>	/* For pause() */

/*
 * Acquire a spin-lock using a compare-and-swap loop on the provided variable.
 */
static inline void
lock_acquire(atomic_int *lock) {
	int expected = 0;

	/* Use compare and swap using C11 atomic_compare_exchange_weak_explicit()
	 * This tries to set lock to 1 if the expected value is 0, otherwise it sets
	 * expected to the current value and fails. On RISC-V it maps directly to
	 * an lr/sc instruction pair: If lr (load reserved) returns a value that
	 * matches expected, it tries to do an sc (store conditional) to update it,
	 * otherwise it updates expected to the value lr returned and fails.
	 *
	 * The difference between the weak and the strong version is that the weak
	 * version may also fail if lr succeeded (we got the expected value), but
	 * sc failed because the reservation was broken (e.g. due to an interrupt
	 * or some other core/device writing that variable), what the C documentation
	 * mentions as spurious failures. The strong version will instead do a loop
	 * internaly (using the weak version) to handle those spurious failures, and
	 * ensure that if the initial lr got the expected value, the operation will
	 * succeed.
	 *
	 * Since we'll spin anyway while waiting for lock to be available (and
	 * have the expected value of 0), it doesn't make much sense to use the
	 * strong version, and as the C documentation also suggests, we use the
	 * weak one since it results in better performance.
	 */
	while (!atomic_compare_exchange_weak_explicit(lock,
						      &expected,
						      1,
						      memory_order_acquire,
						      memory_order_relaxed)) {
		/* Reset expected to 0 (since it gets updated on failure)
		 * and re-try */
		expected = 0;

		/* Give it some time to breathe using the pause instruction
		 * from the Zihintpause extension, since it's a hint it'll
		 * end up being a nop in case it's not supported by hw */
		pause();
	}
}

/*
 * Releases the spin-lock built around the provided variable
 */
static inline void
lock_release(atomic_int *lock) {
	/* This sets the lock value to 0 and places a memory barrier so that
	 * the write is performed before the function returns (synchronously). */
	atomic_store_explicit(lock, 0, memory_order_release);
}

#endif /* _LOCK_H */