/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The idea here is to allow disabling atomics for single-hart scenarios, where we either
 * don't have atomics in the hw, or we want to test the hw without them, without having to
 * put ifdefs all over the place. Instead we disable the a extension from the compiler's
 * -march parameter, in which case the compiler will attempt to implement stdatomic.h
 * functions via libatomic, which isn't available since we tell the compiler to not link
 * against anything else (nostdlib/ffreestanding). This works to our advantage since we
 * can provide here stub implementations for libatomic functions for the linker to pick up.
 * We can also use this together with compiler's .option arch feature to conditionaly enable
 * atomics and control what's emmited (e.g. if the hw only has a subset implemented), or use
 * custom atomics. */

#include <target_config.h>		/* For PLAT_MAX_HARTS */
#include <stdbool.h>			/* For bool in __atomic_compare_exchange_4 */
#include <stdint.h>			/* For typed integers */

#if ((PLAT_MAX_HARTS == 1) && !defined(__riscv_atomic)) || defined(CUSTOM_ATOMICS)

/* 32-bit atomics (most common) */

uint32_t
__atomic_load_4(const volatile void *ptr, int memorder)
{
	(void)memorder;
	/* Note: this is a compiler barrier, not a hw barrier.
	 * It prevents the compiler from reordering memory operations
	 * across this point, but generates no actual instructions. */
	__asm__ __volatile__("" ::: "memory");
	uint32_t val = *(const volatile uint32_t *)ptr;
	__asm__ __volatile__("" ::: "memory");
	return val;
}

void
__atomic_store_4(volatile void *ptr, uint32_t val, int memorder)
{
	(void)memorder;
	__asm__ __volatile__("" ::: "memory");
	*(volatile uint32_t *)ptr = val;
	__asm__ __volatile__("" ::: "memory");
}

uint32_t
__atomic_fetch_or_4(volatile void *ptr, uint32_t val, int memorder)
{
	(void)memorder;
	__asm__ __volatile__("" ::: "memory");
	uint32_t old = *(volatile uint32_t *)ptr;
	*(volatile uint32_t *)ptr = old | val;
	__asm__ __volatile__("" ::: "memory");
	return old;
}

uint32_t
__atomic_fetch_and_4(volatile void *ptr, uint32_t val, int memorder)
{
	(void)memorder;
	__asm__ __volatile__("" ::: "memory");
	uint32_t old = *(volatile uint32_t *)ptr;
	*(volatile uint32_t *)ptr = old & val;
	__asm__ __volatile__("" ::: "memory");
	return old;
}

bool
__atomic_compare_exchange_4(volatile void *ptr, void *expected,
			   uint32_t desired, bool weak,
			   int success_memorder, int failure_memorder)
{
	(void)weak;
	(void)success_memorder;
	(void)failure_memorder;
	
	__asm__ __volatile__("" ::: "memory");
	
	uint32_t old = *(volatile uint32_t *)ptr;
	uint32_t exp = *(uint32_t *)expected;
	
	if (old == exp) {
		*(volatile uint32_t *)ptr = desired;
		__asm__ __volatile__("" ::: "memory");
		return true;
	}
	
	*(uint32_t *)expected = old;
	__asm__ __volatile__("" ::: "memory");
	return false;
}

#endif
