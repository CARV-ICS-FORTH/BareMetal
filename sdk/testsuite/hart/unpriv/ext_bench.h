/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Benchmark harness for the unprivileged extension tier (companion to ext_probe.h).
 *
 * Where ext_probe measures correctness one instruction at a time, this times whole
 * register-only kernels. Each kernel is written twice -- once with the extension, once
 * as a base-ISA reimplementation of the same computation -- as a single asm-volatile
 * loop:
 *
 *	static uint64_t kern(uint64_t acc, uint64_t k, uint64_t n)
 *	{ asm volatile(... "<op> ..\n addi n,n,-1\n bnez n,1b\n" ...); return acc; }
 *
 * Both go to bench_pair(), which times them (see ext_bench.c for how), reports
 * min/avg/max IPC, cycle jitter and the base-vs-extension speedup, and cross-checks
 * that the twins still agree. The measured path is registers only, so what we time is
 * the extension's arithmetic. Only the register operands vary (immediates are ROM).
 * See hart/unpriv/zba.c for worked kernels.
 */

#ifndef EXT_BENCH_H
#define EXT_BENCH_H

#include <stdint.h>
#include <stdbool.h>

/* A benchmark kernel: apply its operation to `acc` (with register addend `k`) `n`
 * times and return the result. The ext and base twins must compute the same thing.
 * The engine picks `n`. */
typedef uint64_t (*bench_kernel_fn)(uint64_t acc, uint64_t k, uint64_t n);

/* Is cycle/instret gating usable here (mcountinhibit implemented)? */
bool bench_available(void);

/*
 * Time an extension kernel against its base-ISA twin (identical random seeds each
 * sample, results cross-checked). `compact` (the "run all" sweep) prints a single
 * avg-IPC/jitter/speedup line, otherwise the full min/avg/max report.
 */
void bench_pair(const char *name, bench_kernel_fn ext, bench_kernel_fn base,
		uint64_t k, bool compact);

#endif /* EXT_BENCH_H */
