/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ATTRIBUTE_MACROS_H
#define _ATTRIBUTE_MACROS_H

#include <target_config.h>	/* For PLAT_HART_VECTORED_TRAPS */

/*
 * Pseudo-keywords for readability when declaring trap handlers.
 *
 * In case of vectored traps, each trap handler is standalone and should contain
 * the whole intro/exit +mret sequence required. All stub handlers are declared
 * as aliases of the default trap handler for clarity. In case a single handler is
 * used for all traps, trap handlers are declared as static inline so that they
 * become part of the direct trap handler. Weak handlers are functions called by
 * trap handlers that applications can override. Since trap handlers are not
 * called from other functions, we need to declare them as used otherwise LTO
 * and / or --gc-sections may throw them away.
 *
 * 
 * Vectored trap handlers and the direct trap handler are deliberately NOT
 * static. The trap vector table (hart_trap_vector_table in hart.c) is
 * file-scope asm that refers to them by symbol name, which the compiler does
 * not see as a reference. With LTO, a static function may be renamed or placed
 * in a different partition than that asm, leaving the name undefined at link
 * time ("undefined reference to hart_exception_handler"). Keeping them global
 * guarantees the names exist, and "used" still keeps them from being discarded.
 * Do not add static back without checking an LTO build of the testsuite.
 */
 
#define __weak_handler	__attribute__((weak))
#if (PLAT_HART_VECTORED_TRAPS == 1)
	#define __trap_handler __attribute__((used, interrupt("machine"), optimize("align-functions=8"), section(".text.trap_handlers")))
#else
	#define __trap_handler	static inline
	#define __direct_trap_handler __attribute__((used, interrupt("machine"), optimize("align-functions=8")))
#endif
#define __empty_trap_handler	__trap_handler __attribute__((alias("hart_default_trap_handler")))

#endif /* _ATTRIBUTE_MACROS_H */
