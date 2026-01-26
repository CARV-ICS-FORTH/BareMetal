/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <platform/interfaces/rng.h>
#include <platform/riscv/mtimer.h>
#include <platform/riscv/csr.h>
#include <platform/riscv/hart.h>
#include <platform/utils/lock.h>

/* Murmurhash3-inspired mixer
 * (Mix13 from https://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
 * also used in Java's SplitMix64), acts as a diffusion layer for mixing/spreading
 * input bits. */
static inline uint64_t avalanche_mix(uint64_t h)
{
	h ^= h >> 30;
	h *= 0xbf58476d1ce4e5b9ULL;
	h ^= h >> 27;
	h *= 0x94d049bb133111ebULL;
	h ^= h >> 31;
	return h;
}

/* This acts as a simple accumulator */
static atomic_int rng_lock = 0;
static uint64_t rng_state = 0;

/*
 * Gather 32 bits of entropy from available hardware sources
 *
 * Sources used (XORed together):
 * 1. MCYCLE - Always available in M-mode
 * 2. MINSTRET - Always available in M-mode
 * 3. mtime - If PLAT_MTIMER_FREQ is defined and non-zero
 * 4. CSR_SEED - If Zkr extension present (checks hs->error for availability)
 *
 * Note: CSR_SEED returns entropy in bits [15:0] with status in bits [31:30]:
 *   00 = BIST  (ignore, running Built-In Self Test)
 *   01 = WAIT  (temporary, try again)
 *   10 = ES16  (valid, 16 bits of entropy)
 *   11 = DEAD  (permanent failure, ignore)
 */
uint32_t rng_get_seed(void)
{
	volatile uint64_t seed = 0;

	/* Always available: cycle and instruction counters (use low 16 bits) */
	seed = csr_read(CSR_MCYCLE);
	seed ^= csr_read(CSR_MINSTRET);
	#ifndef PLAT_NO_MTIMER
		/* XOR in mtime if available */
		seed ^= mtimer_get_num_ticks();
	#endif
	/* Try to get hardware entropy from CSR_SEED (Zkr extension) */
	volatile uint32_t seed_val = 0;
	seed_val = (uint32_t)csr_read(CSR_SEED);
	uint32_t opstat = seed_val >> 30;
	/* ES16 (10) = valid entropy */
	if (opstat == 2)
		seed ^= (uint64_t)(seed_val & 0xFFFF);

	/* Update global state with lock protection for multi-hart safety */
	lock_acquire(&rng_lock);
	rng_state ^= avalanche_mix(seed);
	uint32_t result = (uint32_t)rng_state;
	lock_release(&rng_lock);

	return result;
}
