/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2019-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if __riscv_xlen != 64
#error "This only works on RV64 !"
#endif

#ifndef __riscv_zicsr
#error "This code requires Zicsr !"
#endif

#include <platform/riscv/csr.h>
#include <platform/riscv/caps.h>
#include <platform/utils/asm_macros.h>
#include <target_config.h>

/* Special-use s* registers */
#define HART_COUNTER_STATUS_ADDR s0
#define HART_COUNTER_ADDR s1
#define HART_IDX s2

/***********************************\
* Linker variable copies in .rodata *
\***********************************/

/* Check templates/bmbase.ld.tmpl for more infos on this. */
.section .srodata.ldvars
.globl __stack_start, __stack_size_shift, __ram_end, __data_start, __data_end
.align 3
__gp_addr: .dword __global_pointer$
__data_start: .dword ___data_start
__data_start_lma: .dword ___data_start_lma
__data_end: .dword ___data_end
__bss_start: .dword ___bss_start
__bss_end: .dword ___bss_end
__stack_size_shift: .dword ___stack_size_shift
__stack_start: .dword ___stack_start
__ram_end: .dword ___ram_end


/*************\
* Entry point *
\*************/

FUNC_START _start

	/* Tell debuggers to terminate their backtrace.
	 * This is the bottom of the call stack. */
	.cfi_undefined ra

	/* Before doing anything we must initialize the
	 * global pointer, so that gp-relative addressing
	 * (relaxing) can be used. For more infos on that
	 * check out: "All Aboard, Part 3: Linker Relaxation
	 * in the RISC-V Toolchain" by Palmer Dabbelt.
	 *
	 * Although gp_addr is in .rodata there is still a
	 * possibility it's close to gp (e.g. if ram is next
	 * rom) and linker tries to use gp-relative addressing,
	 * and since gp is not yet set, we'll only end up with
	 * the offset in t0. So do this with relaxation disabled.
	 */
	.option push
	.option norelax
		la t0, __gp_addr
		ld gp, 0(t0)
	.option pop

	/*
	 * Disable interrupts and clean any writable
	 * bits in mip just in case.
	 */
	csrw	CSR_MIE, zero
	csrw	CSR_MIP, zero

	/* Cleanup scratch register and trap related info */
	csrw	CSR_MSCRATCH, zero
	csrw	CSR_MEPC, zero
	csrw	CSR_MCAUSE, zero
	csrw	CSR_MTVAL, zero

	/* Initialize mstatus */
	li	t0, CSR_MSTATUS_INIT
	csrw	CSR_MSTATUS, t0

	/* --== Determine hart index ==-- */
	#if (PLAT_MAX_HARTS > 1)
		#if !defined(__riscv_atomic)
			#error "A extension is needed to support multiple harts"
		#endif

		/* The idea here is to have a global hart counter, so that each
		 * hart regardless of its hart id (that may be anything as long
		 * as it's unique in the system), gets mapped to an index from
		 * 0 to PLAT_MAX_HARTS, by atomicaly increasing that counter.
		 *
		 * However since memory is uninitialized we can't just grab a
		 * memory region and start increasing the value stored there
		 * assuming it's zero. We need to initialize the hart counter
		 * and signal that it's ready to be used.
		 *
		 * For this we use the two reserved words at the end of .data
		 * section (those filled with 0xDEADBEEF in the linker script):
		 *
		 * __data_end[0]: counter_status
		 * __data_end[4]: hart_counter
		 *
		 * These won't be copied to RAM during .data relocation (they are
		 * stored after __data_end), leaving that space free for our atomic
		 * operations. */
		la	t0, __data_end
		ld	HART_COUNTER_STATUS_ADDR, 0(t0)
		addi	HART_COUNTER_ADDR, HART_COUNTER_STATUS_ADDR, 4

		/* The hart counter is initialized by the boot hart, that we can
		 * either pick by using a hard-coded hart id (PLAT_BOOT_HART_ID)
		 * or by treating counter_status as a boot lottery, meaning that
		 * whoever manages to write it first becomes the boot hart. Note
		 * that we use two status values for counter_status, one to mark
		 * that the boot hart already took over: 0x7E57AB1E ("testable"),
		 * and one to signal that secondary harts are 0x600D7060 ("good
		 * to go") after boot hart has finished initializing.
		 *
		 * The assumption here is that it's highly unlikely for the value
		 * 0x7E57AB1E or 0x600D7060 to already exist in that specific
		 * memory space, in an otherwise uninitialized ram region.
		 * 
		 * Note: li will sign extend the 32bit word we'll pass, but
		 * amoswap.w/lr.w won't, so if we use a word with the sign bit
		 * set (e.g. 0xB007C0DE that I originaly had), this thing will
		 * fail ! */
		li	t1, 0x7E57AB1E
		li	t2, 0x600D7060
		#if defined(PLAT_BOOT_HART_ID) && (PLAT_BOOT_HART_ID >= 0)
			/* Fixed boot hart ID - check if we're the chosen one */
			csrr	t3, mhartid
			li	t4, PLAT_BOOT_HART_ID
			beq	t3, t4, .Lboot_hart_init
		#else
			.Llottery_retry:
				/* Load counter_status and grab a reservation */
				lr.w	t3, (HART_COUNTER_STATUS_ADDR)

				/* See if counter_status is already initialized */
				beq	t3, t1, .Lwait_for_counter
				beq	t3, t2, .Lcounter_ready

				/* If not try to win the boot lottery by being the
				 * first hart to initialize it */
				sc.w	t4, t1, (HART_COUNTER_STATUS_ADDR)
				bnez	t4, .Llottery_retry
				j	.Lboot_hart_init
		#endif

		/* Secondary / non-boot hart path */
		.Lwait_for_counter:
			/* Poll counter_status for "good to go" signal, indicating
			 * hart_counter is ready for use. */

			/* If we have Zihintpause, give the hart some room to
			 * breathe while polling. Note that we could use Zawrs here
			 * but just checking for it would overcomplicate things, this
			 * shouldn't take long anyway. */
			#if defined(__riscv_zihintpause)
				pause
			#endif
			lw	t4, (HART_COUNTER_STATUS_ADDR)
			bne	t2, t4, .Lwait_for_counter

		.Lcounter_ready:
			/* Counter is ready, atomically get our index */
			li	t0, 1
			amoadd.w t3, t0, (HART_COUNTER_ADDR)
			addi	HART_IDX, t3, 1	/* Add 1 to old value, HART_IDX now has our index */

			/* Sanity check - ensure we haven't exceeded max harts */
			li	t4, PLAT_MAX_HARTS
			bgeu	HART_IDX, t4, .Ldone

			/* With an index at hand, we can proceed with initializing
			 * the hart's stack and hart_state structure. */
			j	.Lstack_init

		/* Boot hart path */
		.Lboot_hart_init:
			/* Boot hart always gets index 0 for the first stack slot */
			mv	HART_IDX, zero
			sw	zero, (HART_COUNTER_ADDR)	/* Init counter */
			sw	t2, (HART_COUNTER_STATUS_ADDR)	/* Signal ready */
			fence	w, w	/* Make both visible */
	#else
		/* Single hart system - no atomics needed, just set index and
		 * last hart index (count) to 0 */
		mv	HART_IDX, zero
		la	t0, __data_end
		ld	t0, 0(t0)
		sw	zero, 4(t0)
	#endif

	/* --== Initialize .data/.bss from boot hart ==-- */
.Lcopy_data:
	/* Relocate data section if needed */
	la	t0, __data_start
	ld	t1, 0(t0)
	la	t2, __data_start_lma
	ld	t3, 0(t2)
	la	t4, __data_end
	ld	t5, 0(t4)
	beq	t1, t3, .Linit_bss
	beq	t1, t5, .Linit_bss

1:
	ld	t0, 0(t3)
	addi	t3, t3, 8
	sd	t0, 0(t1)
	addi	t1, t1, 8
	blt	t1, t5, 1b

.Linit_bss:
	/* Zero-out the BSS segment if needed */
	la	t1, __bss_start
	ld	t2, 0(t1)
	la	t3, __bss_end
	ld	t4, 0(t3)
	beq	t2, t4, .Lstack_init
1:
	sd	zero, 0(t2)
	addi	t2, t2, 8
	blt	t2, t4, 1b

	/* --== Initialize per-hart stack/state ==-- */
.Lstack_init:
	/* Initialize stack pointer
	 * Offset from stack_start = (hart_idx << stack_size_shift) */
	la	t0, __stack_size_shift
	ld	t1, 0(t0)
	sll	t2, HART_IDX, t1
	la	t3, __stack_start
	ld	t5, 0(t3)
	sub	sp, t5, t2

	/* Make sure we didn't run out of stack, this should be handled
	 * by the linker script, but just in case we messed up, do another
	 * check here on where our stack will end, to be on the safe side. */
	li	t0, 1
	sll	t2, t0, t1	/* Size of one stack slot */
	sub	t3, sp, t2	/* t3 = end of our stack */
	la	t0, __bss_end
	ld	t1, 0(t0)
	blt	t3, t1, .Ldone	/* Stack out of bounds (below __bss_end) */

	/* Make space for hart_state and set mscratch to point there */
	addi	sp, sp, -64
	csrw	CSR_MSCRATCH, sp

	/* Initialize hart_state */
	sd	zero, 0(sp)
	/* Store hartID/idx */
	csrr	t0, mhartid
	sd	t0, 8(sp)
	sh	HART_IDX, 16(sp)
	/* Clear the rest */
	sh	zero, 18(sp)
	sw	zero, 20(sp)
	sd	zero, 24(sp)
	sd	zero, 32(sp)
	sd	zero, 40(sp)
	sd	zero, 48(sp)
	sd	zero, 56(sp)

	#if (PLAT_MAX_HARTS > 1)
		/* Ensure writes are visible to others */
		fence   w, w 
	#endif

	/* --== Switch to C environment ==-- */

	/* Zero-out everything except ra, sp and gp */
	li	x4, 0
	li	x5, 0
	li	x6, 0
	li	x7, 0
	li	x8, 0
	li	x9, 0
	li	x10, 0
	li	x11, 0
	li	x12, 0
	li	x13, 0
	li	x14, 0
	li	x15, 0
	li	x16, 0
	li	x17, 0
	li	x18, 0
	li	x19, 0
	li	x20, 0
	li	x21, 0
	li	x22, 0
	li	x23, 0
	li	x24, 0
	li	x25, 0
	li	x26, 0
	li	x27, 0
	li	x28, 0
	li	x29, 0
	li	x30, 0
	li	x31, 0

	/* C environment is ready, we are good to go */
	call	hart_init

.Ldone:
	csrw	mie, zero
	wfi
	#if defined(__riscv_zihintpause)
		pause
	#endif
	j	.Ldone
FUNC_END _start