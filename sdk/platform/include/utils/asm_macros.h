/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ASM_MACROS_H
#define _ASM_MACROS_H

/*
 * Creates a globally visible function with:
 * - Dedicated section for linker garbage collection
 * - Proper alignment for RISC-V
 * - ELF function type metadata
 * - CFI directives for stack unwinding
 */
.macro FUNC_START name
.section .text.\name, "ax", %progbits
.global \name
.balign (__riscv_xlen >> 3)
.type \name, @function
\name:
	.cfi_startproc
.endm

/*
 * Creates a file-local (static) function making it
 * visible only within this file.
 */
.macro STATIC_FUNC_START name
.section .text.\name, "ax", %progbits
.local \name
.balign (__riscv_xlen >> 3)
.type \name, @function
\name:
	.cfi_startproc
.endm

/*
 * Closes the CFI frame and sets the ELF symbol size for proper
 * bounds checking and disassembly of function <name>.
 */
.macro FUNC_END name
	.cfi_endproc
.size \name, .-\name
.endm

#endif /* _ASM_MACROS_H */