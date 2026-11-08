/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Measurement engine for the extension benchmark tier (see ext_bench.h): the
 * mcountinhibit gate, the N-vs-2N differential, the per-run statistics and the
 * report. Extension-specific kernels live with their extension (e.g. zba.c).
 */

#include "ext_bench.h"
#include "ext_probe.h"			/* For rand64() */
#include <platform/riscv/csr.h>		/* For CSR_* + csr_read/write/*_bits */
#include <platform/utils/utils.h>	/* For INF() */
#include <stdint.h>

/* Each sample times BENCH_ITERS steady-state loop iterations (long enough to dwarf
 * the gate cost). BENCH_SAMPLES of them give the min/avg/max and jitter. */
#define BENCH_ITERS	1024u		/* loop length N (the N-vs-2N differential) */
#define BENCH_SAMPLES	1024u		/* samples per kernel -> min/avg/max/jitter */
#define CYIR		(CSR_MCOUNTINHIBIT_CY | CSR_MCOUNTINHIBIT_IR)

/*
 * mcountinhibit-gated window. Freeze CY/IR, snapshot, re-enable, run the kernel, then
 * freeze again, snapshot, restore. Only CY/IR are touched. csr_* are volatile so the
 * kernel stays ordered between enable and freeze.
 */
struct gate {
	uint64_t sv;	/* saved mcountinhibit */
	uint64_t c0;	/* mcycle snapshot   */
	uint64_t i0;	/* minstret snapshot */
};

static inline struct gate
gate_open(void)
{
	struct gate g;

	g.sv = csr_read(CSR_MCOUNTINHIBIT);
	csr_write(CSR_MCOUNTINHIBIT, g.sv | CYIR);		/* freeze CY/IR */
	g.c0 = csr_read(CSR_MCYCLE);
	g.i0 = csr_read(CSR_MINSTRET);
	csr_write(CSR_MCOUNTINHIBIT, g.sv & ~(uint64_t)CYIR);	/* enable */
	return g;
}

static inline void
gate_close(struct gate g, uint64_t *cyc, uint64_t *ins)
{
	csr_write(CSR_MCOUNTINHIBIT, g.sv | CYIR);		/* freeze */
	*cyc = csr_read(CSR_MCYCLE) - g.c0;
	*ins = csr_read(CSR_MINSTRET) - g.i0;
	csr_write(CSR_MCOUNTINHIBIT, g.sv);			/* restore */
}

bool
bench_available(void)
{
	/* mcountinhibit is WARL: if the CY/IR counters exist, the bits we set stick. */
	uint64_t save = csr_read(CSR_MCOUNTINHIBIT);
	uint64_t rb;

	csr_set_bits(CSR_MCOUNTINHIBIT, CYIR);
	rb = csr_read(CSR_MCOUNTINHIBIT);
	csr_write(CSR_MCOUNTINHIBIT, save);
	return (rb & CYIR) == CYIR;
}

/* a>b ? a-b : 0 -- clamp so a noisy differential can't underflow. */
static uint64_t
sub_clamp(uint64_t a, uint64_t b)
{
	return a > b ? a - b : 0;
}

/* One N-vs-2N differential sample: run the kernel at N and 2N iterations and
 * subtract, cancelling the gate and loop-setup overhead. Returns the 2N result. */
static uint64_t
measure_diff(bench_kernel_fn kern, uint64_t seed, uint64_t k,
	     uint64_t *cyc, uint64_t *ins)
{
	uint64_t c1, i1, c2, i2, r2;
	struct gate g;

	g = gate_open(); kern(seed, k, BENCH_ITERS);         gate_close(g, &c1, &i1);
	g = gate_open(); r2 = kern(seed, k, 2 * BENCH_ITERS); gate_close(g, &c2, &i2);
	*cyc = sub_clamp(c2, c1);
	*ins = sub_clamp(i2, i1);
	return r2;
}

/* Per-implementation timing across runs. */
struct stat {
	uint64_t min_c;
	uint64_t max_c;
	uint64_t sum_c;
	uint64_t ins;		/* retired insns per sample (constant) */
	unsigned runs;
};

static void
stat_init(struct stat *s)
{
	s->min_c = UINT64_MAX;
	s->max_c = 0;
	s->sum_c = 0;
	s->ins = 0;
	s->runs = 0;
}

static void
stat_add(struct stat *s, uint64_t c, uint64_t i)
{
	if (c < s->min_c)
		s->min_c = c;
	if (c > s->max_c)
		s->max_c = c;
	s->sum_c += c;
	s->ins = i;
	s->runs++;
}

/* IPC x1000 as an integer, so the report needs no soft-float. */
static uint64_t
ipc_milli(uint64_t ins, uint64_t cyc)
{
	return cyc ? ins * 1000u / cyc : 0;
}

static void
report_one(const char *tag, const struct stat *s)
{
	uint64_t avg = s->sum_c / s->runs;		/* runs > 0: bench_pair checked */
	uint64_t lo = ipc_milli(s->ins, s->max_c);	/* min IPC @ max cyc */
	uint64_t av = ipc_milli(s->ins, avg);
	uint64_t hi = ipc_milli(s->ins, s->min_c);	/* max IPC @ min cyc */

	INF("    %s: %lu insn  cyc[min/avg/max]=%lu/%lu/%lu  "
	    "IPC[min/avg/max]=%lu.%03lu/%lu.%03lu/%lu.%03lu  jitter=%lu\n",
	    tag, s->ins, s->min_c, avg, s->max_c,
	    lo / 1000, lo % 1000, av / 1000, av % 1000, hi / 1000, hi % 1000,
	    s->max_c - s->min_c);
}

/* IPC x1000 at the average cycle count. */
static uint64_t
avg_ipc_milli(const struct stat *s)
{
	return s->runs ? ipc_milli(s->ins, s->sum_c / s->runs) : 0;
}

/* Speedup x1000: base best cycles / ext best cycles. */
static uint64_t
speedup_milli(const struct stat *ext, const struct stat *base)
{
	return ext->min_c ? base->min_c * 1000u / ext->min_c : 0;
}

void
bench_pair(const char *name, bench_kernel_fn ext, bench_kernel_fn base,
	   uint64_t k, bool compact)
{
	struct stat es, bs;
	uint64_t prev;

	stat_init(&es);
	stat_init(&bs);

	/* IRQs off for the whole kernel: keep interrupt jitter out of the spread. */
	prev = csr_read(CSR_MSTATUS) & CSR_MSTATUS_MIE;
	csr_clear_bits(CSR_MSTATUS, CSR_MSTATUS_MIE);

	for (unsigned r = 0; r < BENCH_SAMPLES; r++) {
		uint64_t seed = rand64();
		uint64_t ec, ei, bc, bi, er, br;

		er = measure_diff(ext,  seed, k, &ec, &ei);
		br = measure_diff(base, seed, k, &bc, &bi);
		/* A zero-cycle differential means the counters didn't advance (e.g.
		 * QEMU's block-granular gating). Drop it rather than poison min. */
		if (ec && bc) {
			stat_add(&es, ec, ei);
			stat_add(&bs, bc, bi);
		}
		if (er != br)
			INF("  MISMATCH %s: ext=%#lx base=%#lx (seed=%#lx)\n",
			    name, er, br, seed);
	}

	if (prev)
		csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_MIE);

	if (!es.runs || !bs.runs) {
		INF("  %s: counters unreliable (no usable samples)\n", name);
		return;
	}

	if (compact) {
		/* One line: avg IPC (ext/base), ext jitter, speedup on best cycles. */
		uint64_t ei = avg_ipc_milli(&es), bi = avg_ipc_milli(&bs);
		uint64_t sp = speedup_milli(&es, &bs);
		INF("  %s: IPC ext %lu.%03lu / base %lu.%03lu, jitter %lu, speedup %lu.%03lu x\n",
		    name, ei / 1000, ei % 1000, bi / 1000, bi % 1000,
		    es.max_c - es.min_c, sp / 1000, sp % 1000);
		return;
	}

	uint64_t sp = speedup_milli(&es, &bs);
	INF("  %s (N-vs-2N differential, N=%u, %u samples, IRQs off)\n",
	    name, BENCH_ITERS, BENCH_SAMPLES);
	report_one("ext ", &es);
	report_one("base", &bs);
	INF("    speedup (base/ext, best cyc): %lu.%03lu x\n", sp / 1000, sp % 1000);
}
