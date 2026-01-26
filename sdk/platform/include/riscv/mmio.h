#ifndef _MMIO_H
#define _MMIO_H

#include <stdint.h>	/* For typed integers */

/* Barriers */

#define __io_ar()	__asm__ __volatile__ ("fence i,r" : : : "memory")
#define __io_bw()	__asm__ __volatile__ ("fence w,o" : : : "memory")

/* Read functions */

static inline uint8_t
read8(const volatile void *addr)
{
	uint8_t val = 0;
	asm volatile("lb %0, 0(%1)" : "=r" (val) : "r" (addr));
	__io_ar();
	return val;
}

static inline uint16_t
read16(const volatile void *addr)
{
	uint16_t val = 0;
	asm volatile("lh %0, 0(%1)" : "=r" (val) : "r" (addr));
	__io_ar();
	return val;
}

static inline uint32_t
read32(const volatile void *addr)
{
	uint32_t val = 0;
	asm volatile("lw %0, 0(%1)" : "=r" (val) : "r" (addr));
	__io_ar();
	return val;
}

static inline uint64_t
read64(const volatile void *addr)
{
	uint64_t val = 0;
	asm volatile("ld %0, 0(%1)" : "=r" (val) : "r" (addr));
	__io_ar();
	return val;
}


/* Write functions */

static inline void
write8(volatile void *addr, uint8_t val)
{
	__io_bw();
	asm volatile("sb %0, 0(%1)" : : "r" (val), "r" (addr));
}

static inline void
write16(volatile void *addr, uint16_t val)
{
	__io_bw();
	asm volatile("sh %0, 0(%1)" : : "r" (val), "r" (addr));
}

static inline void
write32(volatile void *addr, uint32_t val)
{
	__io_bw();
	asm volatile("sw %0, 0(%1)" : : "r" (val), "r" (addr));
}

static inline void
write64(volatile void *addr, uint64_t val)
{
	__io_bw();
	asm volatile("sd %0, 0(%1)" : : "r" (val), "r" (addr));
}

#endif	/* _MMIO_H */