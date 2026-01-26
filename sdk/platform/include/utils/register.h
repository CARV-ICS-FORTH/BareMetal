/*
 * SPDX-FileType: SOURCE
 *
 * SPDX-FileCopyrightText: 2026 Nick Kossifidis <mick@ics.forth.gr>
 * SPDX-FileCopyrightText: 2026 ICS/FORTH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Memory-mapped register access helper macros
 *
 * These macros provide clean, type-safe ways to declare and access
 * memory-mapped hardware registers. They handle the common patterns
 * found in RISC-V platform code (PLIC, APLIC, ACLINT, etc.).
 */

#ifndef _REGISTER_H
#define _REGISTER_H

#include <stdint.h>

/*
 * Register base address check
 *
 * Drivers MUST define _REGBASE before using these macros.
 * All register addresses will be relative to _REGBASE.
 *
 * Usage:
 *   #define _REGBASE  PLAT_APLIC_BASE
 *   #define MY_REG    REG32(0x0004)     // Expands to PLAT_APLIC_BASE + 0x0004
 */
#ifndef _REGBASE
#error "_REGBASE must be defined before using register macros"
#endif

/*
 * Basic register pointer macros
 *
 * Addresses are automatically offset by _REGBASE.
 */
#define REG32(offset) \
	((uint32_t*)((uintptr_t)((_REGBASE) + (offset))))

#define REG64(offset) \
	((uint64_t*)((uintptr_t)((_REGBASE) + (offset))))

/*
 * Register array macros
 *
 * For arrays of registers with fixed stride based on register size.
 * Common for per-hart or per-source register banks.
 *
 * Usage:
 *   #define MTIMECMP(hart_id)  REG64_ARRAY(MTIMECMP_BASE, hart_id)
 *   #define PRIORITY(src_id)   REG32_ARRAY(PRIORITY_BASE, src_id)
 */
#define REG32_ARRAY(offset, idx) \
	((uint32_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * 4))))

#define REG64_ARRAY(offset, idx) \
	((uint64_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * 8))))

/*
 * Register array with custom stride
 *
 * For register arrays where the stride is not the register size.
 * Common in interrupt controllers with context-based register banks.
 *
 * Usage:
 *   #define PRIORITY_THR_REG(ctx)  REG32_STRIDE(PRIORITY_THR_BASE, ctx, 0x1000)
 */
#define REG32_STRIDE(offset, idx, stride) \
	((uint32_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * (stride)))))

#define REG64_STRIDE(offset, idx, stride) \
	((uint64_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * (stride)))))

/*
 * Register block with index and offset
 *
 * For register blocks where each index has multiple registers at fixed offsets.
 * Common pattern: base + (idx * block_size) + offset
 *
 * Usage:
 *   #define IDC_REG(idc, off)  REG32_BLOCK(IDC_BASE, idc, 32, off)
 *   // Accesses: IDC_BASE + (idc * 32) + off
 */
#define REG32_BLOCK(offset, idx, block_size, reg_offset) \
	((uint32_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * (block_size)) + (reg_offset))))

#define REG64_BLOCK(offset, idx, block_size, reg_offset) \
	((uint64_t*)((uintptr_t)((_REGBASE) + (offset) + ((idx) * (block_size)) + (reg_offset))))

/*
 * Bitmap-style register access
 *
 * For registers that represent bitmaps where each bit corresponds to
 * an index (e.g., interrupt sources, enables, pending bits).
 * Groups 32 bits per 32-bit register.
 *
 * The bit_pos parameter is the logical bit position (0-1023+), and
 * the macro calculates which register in the array contains that bit.
 *
 * Formula: register_index = bit_pos / 32 = bit_pos >> 5
 *          register_offset = register_index * 4 bytes
 *
 * Usage:
 *   #define PENDING_REG(src)  REG32_BITMAP(PENDING_BASE, src)
 *   #define ENABLE_REG(src)   REG32_BITMAP(ENABLE_BASE, src)
 *
 *   Then access with:
 *   uint32_t val = read32(PENDING_REG(45));  // Gets register for bit 45
 *   val |= (1 << (45 & 31));                 // Sets bit 45 within that register
 */
#define REG32_BITMAP(offset, bit_pos) \
	((uint32_t*)((uintptr_t)((_REGBASE) + (offset) + (((bit_pos) >> 5) * 4))))

/*
 * Same as REG32_BITMAP but for 64-bit registers (groups of 64 bits)
 * Less common but included for completeness.
 */
#define REG64_BITMAP(offset, bit_pos) \
	((uint64_t*)((uintptr_t)((_REGBASE) + (offset) + (((bit_pos) >> 6) * 8))))

#endif /* _REGISTER_H */
