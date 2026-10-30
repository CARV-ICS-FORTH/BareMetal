/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <target_config.h>		/* For PLAT_* constants */
#include <platform/interfaces/ipi.h>	/* For ipi_clear/self/send() */
#include <platform/interfaces/irq.h>	/* For irq_dispatch() */
#include <platform/riscv/csr.h>		/* For CSR numbers and ops */
#include <platform/riscv/hart.h>	/* For hart_state and definitions */
#include <platform/riscv/mtimer.h>	/* For mtimer_disarm() */
#include <platform/utils/utils.h>	/* For console output */
#include <platform/riscv/caps.h>	/* For CAP_* macros */
#include <platform/interfaces/rng.h>	/* For rng_get_seed() */
#include <platform/utils/lock.h>	/* For lock_acquire/release() */
#include <platform/utils/attribute_macros.h>	/* For __trap_handler etc. */
#include <platform/riscv/pmp.h>		/* For hart_init_pmp() */

#include <errno.h>			/* For errno and error constants */

/*********\
* HELPERS *
\*********/

static void
hart_on_wakeup(struct hart_state *hs)
{
	/* Note: no side effects (e.g. hart_clear_ipi_mask()) inside DBG(),
	 * its arguments are not evaluated on non-DEBUG builds. The IPI
	 * mask is consumed by hart_on_mswtrig() anyway (or is already cleared
	 * if we ended up here via hart_init). */
	DBG("HART %i UP: hart_id (from mhartid): %li, ipi_mask 0x%x, flags: 0x%x, irq_map_idx: %i, error: %i, mstatus: %lx, mtvec: %lx\n",
	    hs->hart_idx,
	    hs->hart_id,
	    hart_get_ipi_mask(hs),
	    hart_get_flags(hs),
	    hs->irq_map_idx,
	    hs->error,
	    csr_read(CSR_MSTATUS),
	    csr_read(CSR_MTVEC));

	hs->error = 0;

	/* Trigger a re-seeding of rng state so that
	 * the timing/order of hart registration influences
	 * rng state (hoping to gather some entropy bits out
	 * of this). */
	(void)rng_get_seed();
}

/*--== IPI argument passing ==--*/

/*
 * This whole cluster is IPI-only, so it's compiled out on PLAT_NO_IPI builds
 * (a no-op hart_release_ipi_params() is provided below so the always-present
 * dispatch code still links). Together with the guarded senders further down,
 * that keeps the buffer, the wait helper and the senders out of single-core /
 * no-IPI images entirely.
 */
#ifndef PLAT_NO_IPI

/*
 * Shared parameters buffer for IPIs that carry arguments (wakeup with
 * address / EIID configuration). Senders are serialized via wakeup_lock,
 * and params_pending counts the target harts that haven't copied the
 * buffer's contents over yet. Senders wait for params_pending to drop
 * back to zero before reusing the buffer, and targets decrement it once
 * they are done with it. This way nobody waits for its own request to
 * complete, the cost of polling is deferred to the next args-carrying
 * IPI instead (that will only spin if it comes in before the previous
 * one was handled). Note that applications may still point a hart's
 * next_params to their own buffer and manage its lifetime themselves,
 * we only track our own buffer here.
 */
static atomic_int wakeup_lock = 0;
static atomic_int params_pending = 0;
static struct next_params ipi_params = {0};

/* Wait until the shared params buffer is free to (re)use, in case the
 * target(s) of a previous args-carrying IPI haven't consumed it yet.
 * Normally this doesn't spin at all. The wait is bounded so that a
 * target that can't handle its IPI (e.g. it's running with interrupts
 * disabled) won't block us forever, if we hit that we complain and
 * reclaim the buffer (best effort, the system is misbehaving already). */
static void
hart_wait_params_free(void)
{
	uint32_t spins = 10000000;
	while (atomic_load_explicit(&params_pending, memory_order_acquire) && --spins)
		pause();
	if (!spins) {
		ERR("Args from previous IPI not consumed (pending: %i), reclaiming\n",
		    atomic_load_explicit(&params_pending, memory_order_relaxed));
		atomic_store_explicit(&params_pending, 0, memory_order_relaxed);
	}
}

/* A target consuming an args-carrying IPI signals it's done with the shared
 * buffer by decrementing params_pending (see hart_wait_params_free). Only our
 * own buffer is tracked; application-owned buffers are left to the app. */
static void
hart_release_ipi_params(struct next_params *params)
{
	if (params == &ipi_params)
		atomic_fetch_sub_explicit(&params_pending, 1, memory_order_release);
}

#else	/* PLAT_NO_IPI */

/* Without IPIs nothing ever sets hs->next_params, so hart_jump_with_args()
 * never calls this with a real buffer; the no-op just lets the dispatch code
 * link without pulling in the IPI param bookkeeping above. */
static void hart_release_ipi_params(struct next_params *params) { (void)params; }

#endif	/* PLAT_NO_IPI */

/*--== Hart dispatch ==--*/

/* noinline is required: hart_dispatch() moves sp with inline asm that the
 * compiler can't see, so this must NOT be inlined into it - otherwise its
 * spill slots would be addressed off the (now redirected) sp and land inside
 * the hart_state at the top of the stack. */
static void __attribute__((noreturn, noinline))
hart_jump_with_args(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	uintptr_t next_addr = hs->next_addr;
	uint64_t arg0 = csr_read(CSR_MHARTID);
	uint64_t arg1 = 0;
	uint64_t mtimer_cycles = 0;

	if (next_addr == 0) {
		ERR("Nowhere to jump to...\n");
		hart_hang();
	}

	/* Copy the arguments over and mark them as consumed right away,
	 * so that the shared params buffer can be reused while we wait
	 * for the rendezvous point below (otherwise a timed wakeup would
	 * keep the buffer busy -and the next sender spinning- for the
	 * whole sleep). Only track our shared buffer, applications may
	 * point us to their own. */
	struct next_params *params = hs->next_params;
	if (params != NULL) {
		arg0 = params->arg0;
		arg1 = params->arg1;
		mtimer_cycles = params->mtimer_cycles;
		hs->next_params = NULL;
		hart_release_ipi_params(params);
	}

	DBG("Will jump to 0x%lx", next_addr);
	if (params != NULL)
		DBG(" with arg0:0x%lx, arg1:0x%lx", arg0, arg1);
	#ifndef PLAT_NO_MTIMER
	if (mtimer_cycles) {
		DBG(" at %li", mtimer_cycles);
		hart_set_flags(hs, HS_FLAG_SLEEPING);
		mtimer_arm_at(mtimer_cycles);
		mtimer_enable_irq();
		while (hart_test_flags(hs, HS_FLAG_SLEEPING)) {
			wfi();
		}
		mtimer_disable_irq();
	}
	#else
		(void)mtimer_cycles;
	#endif
	DBG("\n");

	hart_set_flags(hs, HS_FLAG_RUNNING);

	/* Ensure all pending memory operations complete */
	__asm__ __volatile__("fence.i");
	__asm__ __volatile__("fence");

	/* Jump to payload with arguments. entry() may return (e.g. a program
	 * or thread that finishes), so it's deliberately NOT marked noreturn.
	 * We can be reached by a direct call (boot hart, from hart_init) or via
	 * mret from the IPI handler (secondaries, with no valid return address),
	 * so we must not rely on returning to our caller. */
	void (*entry)(uint64_t, uint64_t) = (void *)next_addr;
	entry(arg0, arg1);

	/* Payload returned: drop RUNNING (back to READY) and park until the next
	 * IPI_WAKEUP_WITH_ADDR re-dispatches us. This function never returns. */
	hart_clear_flags(hs, HS_FLAG_RUNNING);
	while (1 == 1)
		wfi();
}

/*
 * (Re-)dispatch entry point. We reach hart_jump_with_args() from the top on
 * every wakeup (direct call from hart_init, or mret from the IPI handler)
 * without unwinding the previous invocation, so reset sp to this hart's base
 * first (mscratch, set by start.S, sits right at the hart_state) - otherwise
 * each re-dispatch would waste a stack frame. The call below never returns.
 */
static void __attribute__((noreturn))
hart_dispatch(void)
{
	__asm__ __volatile__("csrr sp, mscratch" ::: "memory");
	hart_jump_with_args();
}

/***************\
* TRAP HANDLING *
\***************/

void __trap_handler
hart_default_trap_handler(void)
{
	uint64_t mcause = csr_read(CSR_MCAUSE);
	uint64_t mtval = csr_read(CSR_MTVAL);
	uintptr_t mepc = csr_read(CSR_MEPC);

	ERR("Unhandled %s:\n\tmcause: %lx\n\tmepc: 0x%lx\n\tmtval: 0x%lx\n",
	    (mcause & CSR_MCAUSE_INTR) ? "interrupt" : "exception", mcause, mtval, mepc);

	mepc = (uintptr_t)hart_hang;
	csr_write(CSR_MEPC, mepc);
	return;
}

/* Software Triggered Interrupts */

void __empty_trap_handler hart_handle_supervisor_swtrig(void);
void __empty_trap_handler hart_handle_guest_swtrig(void);

#if defined(PLAT_HAS_IMSIC) && (!defined(PLAT_BYPASS_IMSIC) || (PLAT_IMSIC_IPI_EIID > 0))
static void
hart_set_imsic_eiid_status(struct hart_state *hs, uint16_t eiid, bool enable)
{
		/* Gate the trace on HS_FLAG_RUNNING: this is also called from
		 * hart_init_intr() on the self-init path (hart not RUNNING yet,
		 * console may still be down) where we must stay silent; at
		 * runtime (IPI ENABLE/DISABLE_EIID, hart RUNNING) it prints. */
		if (hart_test_flags(hs, HS_FLAG_RUNNING))
			DBG("%s eeid %hu on hart_idx: %i\n",
			    enable ? "Enabling" : "Disabling", eiid, hs->hart_idx);
		/* Enable/disable this interrupt identity in IMSIC (via indirect CSRs)
		 * EIE registers start at iselect 0xC0, each register covers 32
		 * identities, but on 64bit we read two registers together so
		 * odd registers are invalid (e.g. eie1 -> 0xC1 doesn't exist in
		 * RV64). */
		uint32_t eie_reg = 0xC0 + (eiid / 64);
		eie_reg += (eie_reg & 1);
		uint32_t eie_bit = eiid % 64;

		/* Access via indirect CSRs (miselect/mireg) */
		csr_write(CSR_MISELECT, eie_reg);
		uint64_t eie_val = csr_read(CSR_MIREG);
		if (enable)
			eie_val |= (1ULL << eie_bit);
		else
			eie_val &= ~(1ULL << eie_bit);
		csr_write(CSR_MIREG, eie_val);
}
#endif

void __weak_handler
hart_on_mswtrig(struct hart_state *hs)
{
	ipi_clear();
	/* Consume the IPI mask. If we leave stale bits behind they'll
	 * be misinterpreted on the next IPI (e.g. a plain IPI_WAKEUP
	 * after an IPI_WAKEUP_WITH_ADDR would jump to the stale
	 * next_addr all over again). */
	uint16_t ipi_mask = hart_clear_ipi_mask(hs);
	DBG("Got IPI on hart %i, id: %li, mask: 0x%x\n", hs->hart_idx, hs->hart_id, ipi_mask);
	if (ipi_mask & IPI_WAKEUP_WITH_ADDR) {
		hart_on_wakeup(hs);
		/* Set MEPC to our trampoline function */
		csr_write(CSR_MEPC, (uintptr_t)hart_dispatch);
	} else
	#if defined(PLAT_HAS_IMSIC) && !defined(PLAT_BYPASS_IMSIC)
		if (ipi_mask & (IPI_ENABLE_EIID|IPI_DISABLE_EIID)) {
			struct next_params *params = hs->next_params;
			if (params != NULL) {
				uint16_t eiid = (uint16_t) params->arg0;
				bool enable = (bool) params->arg1;
				/* Mark args as consumed (see hart_jump_with_args) */
				hs->next_params = NULL;
				hart_release_ipi_params(params);
				hart_set_imsic_eiid_status(hs, eiid, enable);
			} else
				ERR("Got EIID IPI without params !\n");
		} else
	#endif
	if (ipi_mask & IPI_WAKEUP)
		hart_on_wakeup(hs);
	return;
}

#if (PLAT_IMSIC_IPI_EIID > 0) || defined(PLAT_NO_IPI)
	/* IPIs arrive via the external interrupt handler (IMSIC), or not at all
	 * (PLAT_NO_IPI) - either way there's no MSWI to service here. With this
	 * empty handler installed nothing references hart_on_mswtrig(), so it and
	 * the sender-only machinery get dropped at link time (--gc-sections). */
	void __empty_trap_handler hart_handle_machine_swtrig(void);
#else
void __trap_handler
hart_handle_machine_swtrig(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	hart_on_mswtrig(hs);
	return;
}
#endif

/* Timer interrupt */

void __empty_trap_handler hart_handle_supervisor_timer(void);
void __empty_trap_handler hart_handle_guest_timer(void);

void __weak_handler
hart_on_mtimer(struct hart_state *hs) {
	WRN("Got timer interrupt out of sleep !\n");
}

#ifndef PLAT_NO_MTIMER
void __trap_handler
hart_handle_machine_timer(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	if (hart_test_flags(hs, HS_FLAG_SLEEPING))
		hart_clear_flags(hs, HS_FLAG_SLEEPING);
	else
		hart_on_mtimer(hs);
	mtimer_disarm();
	return;
}
#else
void __empty_trap_handler hart_handle_machine_timer(void);
#endif

/* External interrupt */

void __empty_trap_handler hart_handle_supervisor_eintr(void);
void __empty_trap_handler hart_handle_guest_eintr(void);

#ifndef PLAT_NO_IRQ
void __trap_handler
hart_handle_machine_eintr(void)
{
	DBG("Got external interrupt, calling dispatcher\n");
	#if defined(PLAT_HAS_IMSIC) && !defined(PLAT_BYPASS_IMSIC)
		uint64_t mtopei = 0;
		/* Claim top interrupt in a single instruction as
		 * suggested in chapter 3.10 of AIA spec. */
		__asm__ __volatile__("csrrw %0, %1, x0"	\
				     : "=r"(mtopei)	\
				     : "i"(CSR_MTOPEI)	\
				     : "memory");	\
		uint16_t source_id = (uint16_t)mtopei;
		#if (PLAT_IMSIC_IPI_EIID > 0)
			if (source_id == PLAT_IMSIC_IPI_EIID) {
				struct hart_state *hs = hart_get_hstate_self();
				hart_on_mswtrig(hs);
				return;
			}
		#endif
		irq_dispatch(source_id);
	#else
		irq_dispatch(0);
	#endif
	return;
}
#else
void __empty_trap_handler hart_handle_machine_eintr(void);
#endif

void __empty_trap_handler hart_handle_hypervisor_eintr(void);

/* Counter overflow interrupt */

void __empty_trap_handler hart_handle_counter_ovf(void);

/* Exceptions */

void __weak_handler
hart_on_mecall(struct hart_state *hs)
{
	uintptr_t mepc = csr_read(CSR_MEPC);
	WRN("Got ecall from M-mode, pc at: 0x%lx\n", mepc);
	mepc += 4;
	csr_write(CSR_MEPC, mepc);
	return;
}

void __weak_handler
hart_on_breakpoint(struct hart_state *hs)
{
	uintptr_t mepc = csr_read(CSR_MEPC);
	uintptr_t mtval = csr_read(CSR_MTVAL);
	ERR("Breakpoint/abort at 0x%lx, mtval: 0x%lx\n", mepc, mtval);
	/* Default behavior: hang */
	mepc = (uintptr_t)hart_hang;
	csr_write(CSR_MEPC, mepc);
	return;
}

void __trap_handler
hart_exception_handler(void) {
	uint64_t mcause = csr_read(CSR_MCAUSE);
	uintptr_t mtval = csr_read(CSR_MTVAL);
	uintptr_t mepc = csr_read(CSR_MEPC);
	uint64_t mstatus = csr_read(CSR_MSTATUS);
	struct hart_state *hs = hart_get_hstate_self();

	if (mcause & CSR_MCAUSE_INTR) {
		ERR("Exception handler called for interrupt ! mcause: 0x%lx !\n", mcause);
		goto hang;
	}

	switch(mcause) {
		case CAUSE_INST_ILLEGAL:

			/* While probing for unprivileged extensions an illegal
			 * instruction is the expected outcome, not an error: the
			 * probe runs the extension's instructions and we report
			 * back that they are not implemented. Probe blocks are
			 * assembled with .option norvc, so every instruction they
			 * contain is 4 bytes and skipping below is always right. */
			if (hart_test_flags(hs, HS_FLAG_PROBING_UNPRIV)) {
				hs->error = ENOSYS;
				goto skip;
			}


			/* We only handle access to unimplemented SYSTEM instructions
			 * here and report the rest. */

			/* MTVAL should contain the illegal instruction but
			 * this is an optional feature so we may need to grab
			 * the instruction ourselves.*/
			uint32_t ill_inst = 0;
			if (mtval == 0) {
				/* Make sure we can read the executable region,
				 * set MXR bit on MSTATUS */
				csr_set_bits(CSR_MSTATUS, CSR_MSTATUS_MXR);
				ill_inst = *((uint32_t*)((uintptr_t)mepc));
				csr_clear_bits(CSR_MSTATUS, CSR_MSTATUS_MXR);
			} else
				ill_inst = mtval;
			/* SYSTEM opcode */
			if ((ill_inst & 0x7F) == 0x73) {
				/* Funct3 is != 0 (CSR* instruction) */
				if ((ill_inst >> 12) & 0x7) {
					uint16_t ill_csr = 0;
					ill_csr = (ill_inst >> 20)  & 0xFFF;
					DBG("Access to unimplemented CSR: 0x%x, pc at: 0x%lx\n", ill_csr, mepc);
				} else
					DBG("Unimplemented SYSTEM instruction: %#.8x\n", ill_inst);
				hs->error = ENOSYS;
				goto skip;
			}
			ERR("Illegal instruction at 0x%lx, mtval: 0x%lx\n", mepc, mtval);
			break;
		case CAUSE_INST_ACCESS_FAULT:
		case CAUSE_LOAD_ACCESS_FAULT:
		case CAUSE_STORE_ACCESS_FAULT:
			ERR("Access fault (%u) at 0x%lx, pc at 0x%lx, mstatus: 0x%.16lx\n",
			    mcause, mtval, mepc, mstatus);
			break;
		case CAUSE_INST_ADDR_MISALIGNED:
		case CAUSE_LOAD_ADDR_MISALIGNED:
		case CAUSE_STORE_ADDR_MISALIGNED:
			ERR("Misaligned access (%u) at 0x%lx, pc at: 0x%lx\n",
			    mcause, mepc, mtval);
			break;
		case CAUSE_ECALL_FROM_M:
			hart_on_mecall(hs);
			return;
		case CAUSE_HARDWARE_ERROR:
			ERR("Got hardware error ! mtval: 0x%lx, pc at: 0x%lx\n", mtval, mepc);
			break;
		/* Page faults - used during MMU capability probing on M-mode */
		case CAUSE_INST_PAGE_FAULT:
		case CAUSE_LOAD_PAGE_FAULT:
		case CAUSE_STORE_PAGE_FAULT:
			DBG("Page fault (%s) at VA: 0x%lx, pc at: 0x%lx\n",
			    (mcause == CAUSE_INST_PAGE_FAULT) ? "instruction" :
			    (mcause == CAUSE_LOAD_PAGE_FAULT) ? "load" : "store",
			    mtval, mepc);
			hs->error = ENOMEM;
			goto skip;
		case CAUSE_BREAKPOINT:
			hart_on_breakpoint(hs);
			return;
		/* TODO: Do something in case of a double trap on M-mode */
		case CAUSE_DOUBLE_TRAP:
		/* Note: CFI on M-mode is WiP spec-wise */
		case CAUSE_SOFTWARE_CHECK:
		/* Those should be handled by lower priv. levels */
		case CAUSE_ECALL_FROM_U:
		case CAUSE_ECALL_FROM_S:
		case CAUSE_ECALL_FROM_VS:
		case CAUSE_INST_GUEST_PAGE_FAULT:
		case CAUSE_LOAD_GUEST_PAGE_FAULT:
		case CAUSE_VIRTUAL_INSTRUCTION:
		case CAUSE_STORE_GUEST_PAGE_FAULT:
		default:
			ERR("Unhandled exception %li at 0x%lx, mtval: 0x%lx\n", mcause, mepc, mtval);
			break;
	};

 hang:
	mepc = (uintptr_t)hart_hang;
	csr_write(CSR_MEPC, mepc);
	return;
 skip:
	mepc += 4;
	csr_write(CSR_MEPC, mepc);
	return;
}

#if (PLAT_HART_VECTORED_TRAPS == 1)
/*
 * RISC-V M-mode trap vector table
 * Hardware jumps to (base + 4*cause) for interrupts, base for exceptions, so it's
 * not really a table rather than a set of call sites which is why it belongs to
 * .text and not e.g. .rodata.
 */
__asm__(
	".section .text.tvec_table, \"ax\", @progbits\n"
	".align 3\n"
	".local hart_trap_vector_table\n" 
	".type hart_trap_vector_table, @object\n"
	"hart_trap_vector_table:\n"

	/* Entry 0: All exceptions go to base address */
	".org hart_trap_vector_table + 0*4\n"
	"jal   zero, hart_exception_handler\n"

	/* Entry 1: Supervisor software interrupt */
	".org hart_trap_vector_table + 1*4\n"
	"jal   zero, hart_handle_supervisor_swtrig\n"

	/* Entry 2: Reserved */
	".org hart_trap_vector_table + 2*4\n"
	"jal   zero, hart_default_trap_handler\n"

	/* Entry 3: Machine software interrupt */
	".org hart_trap_vector_table + 3*4\n"
	"jal   zero, hart_handle_machine_swtrig\n"

	/* Entry 4: Reserved */
	".org hart_trap_vector_table + 4*4\n"
	"jal   zero, hart_default_trap_handler\n"

	/* Entry 5: Supervisor timer interrupt */
	".org hart_trap_vector_table + 5*4\n"
	"jal   zero, hart_handle_supervisor_timer\n"

	/* Entry 6: Guest timer interrupt (hypervisor extension) */
	".org hart_trap_vector_table + 6*4\n"
	"jal   zero, hart_handle_guest_timer\n"

	/* Entry 7: Machine timer interrupt */
	".org hart_trap_vector_table + 7*4\n"
	"jal   zero, hart_handle_machine_timer\n"

	/* Entry 8: Reserved */
	".org hart_trap_vector_table + 8*4\n"
	"jal   zero, hart_default_trap_handler\n"

	/* Entry 9: Supervisor external interrupt */
	".org hart_trap_vector_table + 9*4\n"
	"jal   zero, hart_handle_supervisor_eintr\n"

	/* Entry 10: Guest external interrupt (hypervisor extension) */
	".org hart_trap_vector_table + 10*4\n"
	"jal   zero, hart_handle_guest_eintr\n"

	/* Entry 11: Machine external interrupt */
	".org hart_trap_vector_table + 11*4\n"
	"jal   zero, hart_handle_machine_eintr\n"

	/* Entry 12: Hypervisor external interrupt */
	".org hart_trap_vector_table + 12*4\n"
	"jal   zero, hart_handle_hypervisor_eintr\n"

	/* Entry 13: Local counter overflow (Sscofpmf extension) */
	".org hart_trap_vector_table + 13*4\n"
	"jal   zero, hart_handle_counter_ovf\n"

	/* Entry 14: Reserved */
	".org hart_trap_vector_table + 14*4\n"
	"jal   zero, hart_default_trap_handler\n"

	/* Entry 15: Reserved */
	".org hart_trap_vector_table + 15*4\n"
	"jal   zero, hart_default_trap_handler\n"

	/* Platform-specific interrupts (16+) could be added here */
	".size hart_trap_vector_table, . - hart_trap_vector_table\n"
);
#else
void __direct_trap_handler __attribute__((optimize("O2")))
hart_direct_trap_handler(void)
{
	uint64_t mcause = csr_read(CSR_MCAUSE);

	if (!(mcause & CSR_MCAUSE_INTR))
		return hart_exception_handler();

	mcause &= CSR_MCAUSE_CODE_MASK;
	switch (mcause) {
	case INTR_MACHINE_SOFTWARE_TRIG:
		hart_handle_machine_swtrig();
		break;
	case INTR_MACHINE_TIMER:
		hart_handle_machine_timer();
		break;
	case INTR_MACHINE_EXTERNAL:
		hart_handle_machine_eintr();
		break;
	default:
		ERR("Got unhandled exception, cause: %lx\n", mcause);
		uint64_t mepc = (uintptr_t)hart_hang;
		csr_write(CSR_MEPC, mepc);
		break;
	};
	return;
}
#endif


/**************\
* ENTRY POINTS *
\**************/

void __attribute__((noreturn))
hart_hang(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	ERR("Hart %i (id: %li) hang...\n\r", hs->hart_idx, hs->hart_id);
	hart_block_interrupts();
	while (1 == 1) {
		wfi();
	}
}

void
hart_configure_imsic_eiid(uint16_t hart_idx, uint16_t eiid, bool enable)
{
	#if defined(PLAT_HAS_IMSIC) && !defined(PLAT_BYPASS_IMSIC)
		if (hart_idx >= hart_get_count()) {
			ERR("Tried to configure EIID on unregistered hart_idx: %i\n", hart_idx);
			return;
		}

		struct hart_state *hs = hart_get_hstate_by_idx(hart_idx);

		lock_acquire(&wakeup_lock);
		hart_wait_params_free();
		ipi_params.arg0 = (uint64_t) eiid;
		ipi_params.arg1 = (uint64_t) enable;
		ipi_params.mtimer_cycles = 0;
		hs->next_params = &ipi_params;
		atomic_fetch_add_explicit(&params_pending, 1, memory_order_relaxed);
		if (enable)
			ipi_send(hs, IPI_ENABLE_EIID);
		else
			ipi_send(hs, IPI_DISABLE_EIID);
		lock_release(&wakeup_lock);
	#endif
}

#ifndef PLAT_NO_IPI

void
hart_wakeup_with_addr(uint16_t hart_idx, uintptr_t jump_addr, uint64_t arg0,
		      uint64_t arg1, uint64_t mtimer_cycles)
{
	if (hart_idx >= hart_get_count()) {
		ERR("Tried to wake up unregistered hart_idx: %i\n", hart_idx);
		return;
	}

	struct hart_state *hs = hart_get_hstate_by_idx(hart_idx);

	lock_acquire(&wakeup_lock);
	hart_wait_params_free();
	ipi_params.arg0 = arg0;
	ipi_params.arg1 = arg1;
	ipi_params.mtimer_cycles = mtimer_cycles;
	hs->next_addr = jump_addr;
	hs->next_params = &ipi_params;
	atomic_fetch_add_explicit(&params_pending, 1, memory_order_relaxed);
	ipi_send(hs, IPI_WAKEUP_WITH_ADDR);
	lock_release(&wakeup_lock);
}

void
hart_wakeup_all_with_addr(uintptr_t jump_addr, uint64_t arg0, uint64_t arg1,
			  uint64_t mtimer_cycles)
{
	int num_harts = hart_get_count();
	struct hart_state *this_hs = hart_get_hstate_self();

	lock_acquire(&wakeup_lock);
	hart_wait_params_free();
	ipi_params.arg0 = arg0;
	ipi_params.arg1 = arg1;
	ipi_params.mtimer_cycles = mtimer_cycles;

	/* All targets share the same arguments (that's the point of
	 * this function), mark them all as pending upfront so that
	 * the buffer stays busy until the last one has copied them. */
	atomic_fetch_add_explicit(&params_pending, num_harts,
				  memory_order_relaxed);

	/* First send IPI to all other harts */
	for (int i = 0; i < num_harts; i++) {
		struct hart_state *hs = hart_get_hstate_by_idx(i);
		if (hs == this_hs)
			continue;
		hs->next_addr = jump_addr;
		hs->next_params = &ipi_params;
		ipi_send(hs, IPI_WAKEUP_WITH_ADDR);
	}

	/* Finaly to self. Note that we release the lock first: once
	 * the IPI comes in we'll jump away and never return here,
	 * whoever needs the buffer next will wait on params_pending
	 * instead. */
	this_hs->next_addr = jump_addr;
	this_hs->next_params = &ipi_params;
	lock_release(&wakeup_lock);
	ipi_self(IPI_WAKEUP_WITH_ADDR);
}

#else	/* PLAT_NO_IPI: no harts to wake, keep the API as no-ops */

void
hart_wakeup_with_addr(uint16_t hart_idx, uintptr_t jump_addr, uint64_t arg0,
		      uint64_t arg1, uint64_t mtimer_cycles)
{
	(void)hart_idx; (void)jump_addr; (void)arg0; (void)arg1; (void)mtimer_cycles;
}

void
hart_wakeup_all_with_addr(uintptr_t jump_addr, uint64_t arg0, uint64_t arg1,
			  uint64_t mtimer_cycles)
{
	(void)jump_addr; (void)arg0; (void)arg1; (void)mtimer_cycles;
}

#endif	/* PLAT_NO_IPI */

/***********\
* HART INIT *
\***********/

/*
 * Everything here runs on the per-hart self-init path. Secondary harts
 * (hart_idx > 0) reach it straight out of start.S while the boot hart may
 * still be bringing up the shared environment, so .data/.bss and the console
 * (UART, set up by platform_init()) are not guaranteed to be ready yet.
 * Code here must therefore stick to hart-local state (stack + CSRs + .rodata)
 * and must not print or take shared locks. Anything worth reporting is stashed
 * in hs->error and printed later by hart_on_wakeup(), once the hart is
 * dispatched and the console is up.
 */
static void
hart_init_intr(struct hart_state *hs)
{
	#if defined(PLAT_HAS_IMSIC) && defined(PLAT_BYPASS_IMSIC)
		/* Make sure EIDELIVERY @0x70 is set to 0x40000000 on reset
		 * to bypass IMSIC, otherwise IMSIC bypass is not supported ! */
		csr_write(CSR_MISELECT, 0x70);
		uint64_t eidelivery = csr_read(CSR_MIREG);
		if (eidelivery != 0x40000000)
			hs->error = ENOTSUP;
	#endif
	hart_allow_interrupts();
	hart_enable_intr(INTR_MACHINE_SOFTWARE_TRIG);
	#if (PLAT_IMSIC_IPI_EIID > 0)
		hart_set_imsic_eiid_status(hs, PLAT_IMSIC_IPI_EIID, 1);
	#endif

	/* For each hart we have the following infos:
	 * a) hart_id from mhartid, an XLEN id that can be anything as long as it's unique in the system, a physical hart id
	 * b) hart_idx, an index we assigned to this hart based on the order it came up, it's the logical hart id under our control
	 * c) An interrupt target id, which is a way to tell the interrupt controller "send this to hart X on mode Y". For PLIC this
	 * is called a context and describes both X and Y, so it'll e.g. be 0 -> hart0,M-mode, 1-> hart0,S-mode. For APLIC it describes
	 * X, and Y is controlled via APLIC's delegation (so everything is sent to Y = M-mode and APLIC can delegate it to Y = S-mode
	 * if needed per interrupt source) and is called interrupt domain context (IDC). For IMSIC this describes X, Y is encoded in
	 * IMSIC's base address (the Y mode's interrupt file), and it's called hart index.
	 *
	 * In struct irq_target_mapping (irq.h)  we have a mapping between hart_id and that interrupt target id (that has a different name
	 * depending on the interrupt controller but we use a union so it doesn't matter which one we choose), and we use hs->irq_map_idx
	 * so that we don't loop over the platform_intc_map all the time to get it.
	 */
	extern const volatile struct irq_target_mapping platform_intc_map[PLAT_MAX_HARTS];
	hs->irq_map_idx = -1;
	int matches = 0;
	for (int i = 0; i < PLAT_MAX_HARTS; i++) {
		if (platform_intc_map[i].hart_id == hs->hart_id) {
			hs->irq_map_idx = i;
			matches++;
		}
	}
	#if defined(DEBUG)
		/* Make sure there is only one mapping for our hart_id. Since this is hardcoded in target_config.h only do the check
		 * durring debugging. It can't go wrong due to runtime behavior. */
		if (matches != 1) {
			hs->irq_map_idx = -1;
			/* DEBUG-only: stash the match count in the per-hart errno
			 * so the wakeup banner (hart_on_wakeup) can report where
			 * the platform_intc_map is wrong: 0 => no mapping for this
			 * hart_id, >1 => duplicate mappings. This deliberately
			 * overloads errno; it's cleared right after being printed. */
			hs->error = matches;
		}
	#endif
}

/* Main without arguments as per C spec */
extern int main(void);

extern void platform_init(void);

/* Note: We arrive here with stack initialized, but .data/.bss may not be
 * ready yet on secondary harts (hs->hart_idx > 0), so stick to hart-local
 * variables. */
void __attribute__((noreturn))
hart_init(void)
{
	struct hart_state *hs = hart_get_hstate_self();
	#if (PLAT_HART_VECTORED_TRAPS == 1)
		/* Reference the vector table symbol - only visible in this function */
		extern char hart_trap_vector_table[];
		uintptr_t mtvec_val = (uintptr_t)hart_trap_vector_table | 1;
	#else
		uintptr_t mtvec_val = (uintptr_t)hart_direct_trap_handler & ~1;
	#endif
	csr_write(CSR_MTVEC, mtvec_val);

	uint64_t misa = csr_read(CSR_MISA);
	hart_init_fpu(hs, misa);
	hart_init_vpu(hs, misa);

	/* Set up this hart's physical memory protection (no-op if PMP is
	 * unavailable/disabled for this target). */
	hart_init_pmp();

	hart_init_counters(hs);
	hart_init_sdtrig(hs);
	hart_init_intr(hs);
	hart_set_flags(hs, HS_FLAG_READY);

	/* Good to go, if this is the boot hart go with the flow
	 * and call platform_init before jumping to main() through
	 * hart_jump_with_addrs. */
	if (hs->hart_idx == 0) {
		platform_init();
		hs->next_addr = (uintptr_t)main;
		hart_on_wakeup(hs);
		hart_dispatch();
	} else {
		/* If this is a secondary hart wait for an IPI, note that jumping
		 * to a function is handled by the IPI handler. */
		while(1 == 1) {
			wfi();
		}
	}

	/* Both branches above are non-returning; hart_hang() is just a safety
	 * net in case that ever changes. */
	hart_hang();
}