/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _RNG_H
#define _RNG_H

#include <stdint.h>

/*
 * RNG seed gathering interface
 *
 * Collects entropy from available hardware sources:
 * - mcycle: Cycle counter (timing-based entropy)
 * - mtime: Machine timer (if MTIMER present)
 * - CSR_SEED: Hardware entropy source (Zkr extension, if present)
 *
 * Returns a 32-bit seed value (compatible with CSR_SEED format).
 * Suitable for seeding PRNGs - call multiple times for more entropy.
 */
uint32_t rng_get_seed(void);

#endif /* _RNG_H */
