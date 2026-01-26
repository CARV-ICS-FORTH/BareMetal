#ifndef _CSR_H
#define _CSR_H
#include <platform/utils/bitfield.h>

/*
 * RISC-V Control and Status Registers (CSRs)
 *
 * Based on RISC-V Privileged Architecture Specification and ratified extensions.
 * Official specifications: https://riscv.org/specifications/
 *
 * Privileged Specification Version History:
 *
 * v1.7-1.10 (2015-2017): Pre-ratification drafts
 *   https://github.com/riscv/riscv-isa-manual/tree/eb86a900f418a5436b8e31abc0563be3cb402a16/release
 *   v1.7:   Early draft - basic M/S/U modes, sptbr for virtual memory
 *   v1.9:   misa @ 0xF10 temporarily, platform interrupt controller
 *   v1.9.1: misa back to 0x301, debug CSRs, HPM scheme, medeleg/mideleg present
 *   v1.10:  sptbr→satp, PMP introduced (pmpcfg/pmpaddr), mucounteren→mcountinhibit @0x320
 *
 * v1.11 (June 2019): First ratified version
 *   Machine and Supervisor ISAs ratified, Hypervisor still draft v0.3
 *   Formalized PMP (optional), mcountinhibit, interrupt priorities specified
 *
 * v1.12 (December 2021): Major update - Hypervisor ratified
 *   Added: mstatush, mconfigptr, mseccfg/mseccfgh, menvcfg/henvcfg/senvcfg (+h variants)
 *   Hypervisor ext v1.0 ratified: all H/VS CSRs, mtval2, mtinst, hstatus, etc.
 *   Ratified: Svnapot, Svpbmt, Svinval extensions
 *   Breaking: MRET/SRET clear mstatus.MPRV when leaving M-mode
 *   Removed: N extension (user-level interrupts)
 *
 * v1.13 (March 2025): Latest ratified version
 *   Incorporated: Smstateen (mstateen0-3), Sstc (stimecmp), Sscofpmf (interrupt 13)
 *   Added: medelegh/hedelegh (RV32), misa.B/V defined, misa.MXL now read-only
 *   Constraint: SXLEN ≥ UXLEN
 *   Extensions: Smctr, Svvptc, Ssqosid, pointer masking, Svrsw60t59b, Zalasr
 */

/*
 * Ratified Extension Specifications (defining CSRs, sorted by ratification year):
 *
 * === 2019 ===
 *
 * Base counters and HPM (Zicntr/Zihpm) v2.0 (December 2019):
 *   User-mode cycle, time, instret, hpmcounter3-31 CSRs
 *   https://github.com/riscv/riscv-isa-manual/releases/tag/Ratified-IMAFDQC
 *
 * === 2021 ===
 *
 * Vector extension (V) v1.0 (November 2021):
 *   Adds vstart, vxsat, vxrm, vcsr, vl, vtype, vlenb CSRs
 *   https://github.com/riscv/riscv-v-spec/releases/tag/v1.0
 *
 * Scalar crypto and entropy source (Zkr, Zk*) v1.0.1 (December 2021):
 *   Adds seed CSR (0x015), mseccfg.USEED/SSEED fields
 *   https://github.com/riscv/riscv-crypto/releases/tag/v1.0.1-scalar
 *
 * Hypervisor extension (H) v1.0 (December 2021):
 *   Part of Priv spec v1.12 - all H/VS CSRs (hstatus, hedeleg, etc.)
 *   https://github.com/riscv/riscv-isa-manual/releases/tag/Priv-v1.12
 *
 * State enable (Smstateen) v1.0 (~2021):
 *   Adds mstateen0-3, hstateen0-3, sstateen0-3 CSRs (+ RV32 *h variants)
 *   https://github.com/riscv/riscv-state-enable/releases/download/v1.0.0/Smstateen.pdf
 *
 * Count overflow and mode-based filtering (Sscofpmf) v1.0 (~2021):
 *   Adds scountovf CSR, mhpmevent* fields, interrupt 13 (LCOF)
 *   https://github.com/riscv/riscv-count-overflow/releases/download/v1.0.0/Sscofpmf.pdf
 *
 * Supervisor timer compare (Sstc) v1.0 (~2021):
 *   Adds stimecmp, vstimecmp CSRs (+ RV32 *h variants)
 *   https://github.com/riscv/riscv-time-compare/releases/download/v1.0.0/Sstc.pdf
 *
 * Enhanced Physical Memory Protection (Smepmp) v1.0 (~2021):
 *   Defines mseccfg/mseccfgh fields (MML, MMWP, RLB)
 *   https://github.com/riscv/riscv-tee/blob/main/Smepmp/Smepmp.pdf
 *
 * Base cache management operations (Zicbom/Zicboz/Zicbop) v1.0 (~2021):
 *   Control via *envcfg.CBIE/CBCFE/CBZE fields
 *   https://github.com/riscv/riscv-CMOs/releases/tag/v1.0.1
 *
 * === 2022-2023 ===
 *
 * Advanced Interrupt Architecture (Smaia/Ssaia) v1.0 (June 2023):
 *   Adds miselect, mireg, mtopi, mtopei, siselect, sireg, stopi, stopei, etc.
 *   https://github.com/riscv/riscv-aia/releases/download/v1.0/riscv-interrupts-v1.0.pdf
 *
 * Pointer masking (Smmpm/Smnpm/Ssnpm) v1.0 (~2023):
 *   Adds PMM fields to mseccfg, menvcfg, senvcfg, henvcfg, hstatus
 *   https://github.com/riscv/riscv-j-extension/releases/tag/v1.0.0
 *
 * Hardware update of PTE A/D bits (Svadu) v1.0 (~2023):
 *   Adds menvcfg.ADUE/henvcfg.ADUE fields
 *   https://github.com/riscvarchive/riscv-svadu/releases/download/v1.0/riscv-svadu.pdf
 *
 * Control Flow Integrity (Zicfilp/Zicfiss) v1.0 (~2023):
 *   Zicfilp: Adds *ELP bits to mstatus, menvcfg/henvcfg/senvcfg.LPE fields
 *   Zicfiss: Adds ssp CSR, menvcfg/henvcfg/senvcfg.SSE fields
 *   https://github.com/riscv/riscv-cfi/releases/tag/v1.0
 *
 * Resumable Non-Maskable Interrupts (Smrnmi) v1.0 (~2023):
 *   Adds mnscratch, mnepc, mncause, mnstatus CSRs
 *   https://github.com/riscv/riscv-isa-manual/blob/main/src/smrnmi.adoc
 *
 * Supervisor counter delegation (Smcdeleg/Ssccfg) v1.0 (~2023):
 *   Adds menvcfg.CDE, scountinhibit, scyclecfg, sinstretcfg CSRs
 *   https://github.com/riscvarchive/riscv-smcdeleg-ssccfg/releases/download/v1.0.0/riscv-smcdeleg-ssccfg-v1.0.0.pdf
 *
 * === 2024-2025 ===
 *
 * Indirect CSR access (Smcsrind/Sscsrind) v1.0 (February 2024):
 *   Generalizes *iselect / *ireg mechanism for non-AIA extensions
 *   https://github.com/riscvarchive/riscv-indirect-csr-access/releases/download/v1.0.0/riscv-indirect-csr-access-v1.0.0.pdf
 *
 * Double trap detection (Smdbltrp/Ssdbltrp) v1.0 (August 2024):
 *   Adds mstatus.MDT, menvcfg.DTE, double trap exception cause (16)
 *   https://github.com/riscv/riscv-double-trap/releases/download/v1.0/riscv-double-trap.pdf
 *
 * Debug specification (Sdext/Sdtrig) v1.0 (February 2025):
 *   Adds tselect, tdata1-3, tinfo, tcontrol, mcontext, scontext, hcontext CSRs
 *   https://github.com/riscv/riscv-debug-spec/releases/tag/v1.0.0
 */


/**************************\
* Hart configuration/state *
\**************************/

/* Read-only Machine information CSRs (v1.9.1+) */
#define CSR_MVENDORID	0xF11	/* 32bit JEDEC vendor id (v1.10: changed to JEDEC code) */
#define CSR_MARCHID	0xF12	/* MXLEN bits Architecture ID */
#define CSR_MIMPID	0xF13	/* MXLEN bits Implementation ID */
#define CSR_MHARTID	0xF14	/* MXLEN bits Hardware Thread (hart) ID, unique for each hart */
#define CSR_MCONFIGPTR	0xF15	/* Machine configuration structure pointer (v1.12) */

/* Machine status register, MXLEN bits (v1.7+) */
/* Supervisor status register, SXLEN bits (v1.7+) */
/* Virtual supervisor status register, VSXLEN bits (H v1.0 - v1.12) */
#define CSR_MSTATUS		0x300
#define CSR_SSTATUS		0x100
#define CSR_VSSTATUS		0x200
#define CSR_MSTATUS_WPRI1	BIT(0)		/* Reserved */
#define CSR_MSTATUS_SIE 	BIT(1)		/* Supervisor Interrupt Enable */
#define CSR_MSTATUS_WPRI2	BIT(2)		/* Reserved */
#define CSR_MSTATUS_MIE 	BIT(3)		/* Machine Interrupt Enable */
#define CSR_MSTATUS_WPRI3	BIT(4)		/* Reserved */
#define CSR_MSTATUS_SPIE	BIT(5)		/* S-mode previous interrupt enable */
#define CSR_MSTATUS_UBE 	BIT(6)		/* U-mode is big endian (data only) */
#define CSR_MSTATUS_MPIE	BIT(7)		/* M-mode previous interrupt enable */
#define CSR_MSTATUS_SPP 	BIT(8)		/* S-mode previous privilege mode */
#define CSR_MSTATUS_VS		FIELD(10,9)	/* VPU status */
#define CSR_MSTATUS_MPP 	FIELD(12,11)	/* M-mode previous privilege mode  */
#define CSR_MSTATUS_FS		FIELD(14,13)	/* FPU status */
#define CSR_MSTATUS_XS		FIELD(16,15)	/* VPU/FPU status */
#define CSR_MSTATUS_MPRV	BIT(17) 	/* Modify privilege -run with S-mode MMU enabled- */
#define CSR_MSTATUS_SUM 	BIT(18) 	/* Supervisor user memory access */
#define CSR_MSTATUS_MXR 	BIT(19) 	/* Make executable regions readable (for trap-n-emulate) */
#define CSR_MSTATUS_TVM 	BIT(20) 	/* Trap virtual memory -traps on satp access- */
#define CSR_MSTATUS_TW		BIT(21) 	/* Timeout wait -traps on wfi- */
#define CSR_MSTATUS_TSR 	BIT(22) 	/* Trap on sret */
#define CSR_MSTATUS_SPELP	BIT(23)		/* Landing Pad expected on S-mode (Zicfilp v1.0) */
#define CSR_MSTATUS_SDT		BIT(24)		/* Double trap on S-mode (Ssdbltrp v1.0) */
#define CSR_MSTATUS_WPRI4	FIELD(31,25)	/* Reserved */
#define CSR_MSTATUS_UXL 	FIELD_ULL(33,32)	/* U-mode XLEN RV64 only */
#define CSR_MSTATUS_SXL 	FIELD_ULL(35,34)	/* S-mode XLEN RV64 only */
#define CSR_MSTATUS_SBE 	BIT_ULL(36)	/* S-mode is big endian (data only) */
#define CSR_MSTATUS_MBE 	BIT_ULL(37)	/* M-mode is big endian (data only) */
#define CSR_MSTATUS_GVA 	BIT_ULL(38)	/* Guest virtual address */
#define CSR_MSTATUS_MPV 	BIT_ULL(39)	/* Machine previous virtualization mode */
#define CSR_MSTATUS_WPRI5	BIT_ULL(40)	/* Reserved */
#define CSR_MSTATUS_MELP	BIT_ULL(41)	/* Landing Pad expected on M-mode (Zicfilp v1.0) */
#define CSR_MSTATUS_MDT		BIT_ULL(42)	/* Disable trap on M-mode (Smdbltrp v1.0) */
#define CSR_MSTATUS_WPRI6	FIELD_ULL(62,43)	/* Reserved */
#define CSR_MSTATUS_SD		BIT_ULL(63)	/* Extension dirty bit (indicates FS/VS dirty) */
/* Upper 32bits of mstatus -RV32 only, note there is no sstatush (v1.12) */
#define CSR_MSTATUSH		0x310

#define CSR_MSTATUS_PRESERVE_MASK (CSR_MSTATUS_WPRI1 | CSR_MSTATUS_WPRI2 | CSR_MSTATUS_WPRI3 | \
				   CSR_MSTATUS_WPRI4 | CSR_MSTATUS_WPRI5 | CSR_MSTATUS_WPRI6)
/*
 * Disable M/S interrupts, FPU/VPU, big endian support, virtualization etc,
 * only set MPP to M-mode (3), and UXL/SXL to 64bits (2)
 */
#define CSR_MSTATUS_INIT 	FIELD_PREP_ULL(CSR_MSTATUS_MPP, 3) | \
				FIELD_PREP_ULL(CSR_MSTATUS_UXL, 2) | \
				FIELD_PREP_ULL(CSR_MSTATUS_SXL, 2)

#ifndef __ASSEMBLER__
enum priv_modes {
	PRIV_MODE_U = 0,
	PRIV_MODE_S = 1,
	PRIV_MODE_M = 3,
};

enum ext_status {
	EXT_STATUS_OFF = 0,
	EXT_STATUS_INIT = 1,
	EXT_STATUS_CLEAN = 2,
	EXT_STATUS_DIRTY = 3
};
#endif

#define CSR_MSTATUS_F_INIT	FIELD_PREP(CSR_MSTATUS_FS, 1)
#define CSR_MSTATUS_F_CLEAN	FIELD_PREP(CSR_MSTATUS_FS, 2)
#define CSR_MSTATUS_V_INIT	FIELD_PREP(CSR_MSTATUS_VS, 1)
#define CSR_MSTATUS_V_CLEAN	FIELD_PREP(CSR_MSTATUS_VS, 2)

/* Machine ISA register, MXLEN bits, WARL (v1.7+, addr changes: v1.9@0xF10, v1.9.1@0x301) */
#define CSR_MISA		0x301
#define CSR_MISA_A		BIT(0)	/* Atomics (Zaamo, Zalrsc) */
#define CSR_MISA_B		BIT(1)	/* Bitmanip (Zba + Zbb + Zbs) - defined v1.13 */
#define CSR_MISA_C		BIT(2)	/* Compressed instructions */
#define CSR_MISA_D		BIT(3)	/* Double-precision floating point */
#define CSR_MISA_E		BIT(4)	/* RV32E base ISA (should be ~I) */
#define CSR_MISA_F		BIT(5)	/* Single-precision floating point */
#define CSR_MISA_G		BIT(6)	/* Reserved -> Generic set (IMAFD) */
#define CSR_MISA_H		BIT(7)	/* Hypervisor extension - v1.12+ */
#define CSR_MISA_I		BIT(8)	/* RV32/64/128 Integer Base ISA */
#define CSR_MISA_J		BIT(9)	/* Reserved (open for JIT acceleration) */
#define CSR_MISA_K		BIT(10)	/* Reserved (used to be Crypto extensions) */
#define CSR_MISA_L		BIT(11)	/* Reserved (open for decimal floating point) */
#define CSR_MISA_M		BIT(12)	/* Integer multiply/divide */
#define CSR_MISA_N		BIT(13)	/* Reserved (N ext removed in v1.12) */
#define CSR_MISA_O		BIT(14)	/* Reserved */
#define CSR_MISA_P		BIT(15)	/* Reserved for packed SIMD */
#define CSR_MISA_Q		BIT(16)	/* Quad-precision floating point */
#define CSR_MISA_R		BIT(17)	/* Reserved */
#define CSR_MISA_S		BIT(18)	/* Supervisor mode */
#define CSR_MISA_T		BIT(19)	/* Reserved */
#define CSR_MISA_U		BIT(20)	/* User mode */
#define CSR_MISA_V		BIT(21)	/* Vector extension - defined v1.12 */
#define CSR_MISA_W		BIT(22)	/* Reserved */
#define CSR_MISA_X		BIT(23)	/* Non-standard/vendor extensions present */
#define CSR_MISA_Y		BIT(24)	/* Reserved */
#define CSR_MISA_Z		BIT(25)	/* Reserved */
#define CSR_MISA_MXL_SHIFT_32	30	/* Machine XLEN on RV32 */
#define CSR_MISA_MXL_SHIFT_64	62	/* Machine XLEN on RV64 */
#define CSR_MISA_MXL_SHIFT_128	126	/* Machine XLEN on RV128 */

#define CSR_MISA_RESERVED_MASK (CSR_MISA_G | CSR_MISA_J | CSR_MISA_K | CSR_MISA_L | \
				CSR_MISA_N | CSR_MISA_O | CSR_MISA_P | CSR_MISA_R | \
				CSR_MISA_T | CSR_MISA_W | CSR_MISA_Y | CSR_MISA_Z)
#ifndef __ASSEMBLER__
enum xlen_modes {
	XLEN_32 = 1,
	XLEN_64 = 2,
	XLEN_128 = 3
};
#endif

/* Machine environment configuration, 64bits (v1.12) */
/* Supervisor environment configuration, SXLEN bits (v1.12) */
/* Hypervisor environment configuration, HSXLEN bits (v1.12) */
#define CSR_MENVCFG		0x30A
#define CSR_SENVCFG		0x10A
#define CSR_HENVCFG		0x60A
#define CSR_MENVCFG_FIOM	BIT(0)			/* Fence of I/O implies memory */
#define CSR_MENVCFG_WPRI1	BIT(1)			/* Reserved*/
#define CSR_MENVCFG_LPE		BIT(2)			/* Landing pads enabled on mode-1 (Zicfilp v1.0) */
#define CSR_MENVCFG_SSE		BIT(3)			/* Shadow stack enabled on mode-1 (Zicfiss v1.0) */
#define CSR_MENVCFG_CBIE	FIELD(5,4)		/* Cache block invalidate (Zicbom v1.0) */
#define CSR_MENVCFG_CBCFE	BIT(6)			/* Cache block clean and flush (Zicbom v1.0) */
#define CSR_MENVCFG_CBZE	BIT(7)			/* Cache block zeroing (Zicboz v1.0) */
#define CSR_MENVCFG_WPRI2	FIELD(31,8)		/* Reserved */
#define CSR_MENVCFG_PMM 	FIELD_ULL(33,32)	/* Pointer Masking Mode (Smnpm v1.0) */
#define CSR_MENVCFG_WPRI3	FIELD_ULL(58,34)	/* Reserved, last field for senvcfg */
#define CSR_MENVCFG_DTE		BIT_ULL(59)		/* Double trap enable (Ssdbltrp v1.0) */
#define CSR_MENVCFG_CDE 	BIT_ULL(60)		/* Counter delegation enable (Smcdeleg v1.0) */
#define CSR_MENVCFG_ADUE	BIT_ULL(61)		/* Hardware update of PTE A/D bits for HS (Svadu v1.0) */
#define CSR_MENVCFG_PBMTE	BIT_ULL(62)		/* Svpbmt (Page-based memory types) enable (v1.12) */
#define CSR_MENVCFG_STCE	BIT_ULL(63)		/* Sstc (Supervisor Timer Access) enable (v1.13) */
/* Upper 32bits of menvcfg/henvcfg -RV32 only (v1.12) */
#define CSR_MENVCFGH		0x31A
#define CSR_HENVCFGH		0x61A

/* Machine state enable, 64bits, WARL (Smstateen v1.0, incorporated v1.13) */
/* Supervisor/hypervisor state enable, SXLEN bits (Ssstateen v1.0, incorporated v1.13) */
#define CSR_MSTATEEN0		0x30C
#define CSR_SSTATEEN0		0x10C
#define CSR_HSTATEEN0		0x60C
#define CSR_MSTATEEN0_C 	BIT(0)			/* Access to custom state (non-standard extensions) */
#define CSR_MSTATEEN0_FCSR	BIT(1)			/* Access to FCSR when Zf/dinx is used (~misa.F) */
#define CSR_MSTATEEN0_JCT	BIT(2)			/* Access to JVT (Zcmt) */
#define CSR_MSTATEEN0_WPRI1	FIELD_ULL(55,3) 	/* Reserved */
#define CSR_MSTATEEN0_PIP13	BIT_ULL(56)		/* Access to hedelegh as per Priv. spec 1.13 */
#define CSR_MSTATEEN0_CONTEXT	BIT_ULL(57)		/* Access to scontext/hcontext (Debug spec chapter 5.7.7/9) */
#define CSR_MSTATEEN0_IMSIC	BIT_ULL(58)		/* Access to IMSIC state (stopei/vstopei) (AIA chapter 2.5) */
#define CSR_MSTATEEN0_AIA	BIT_ULL(59)		/* Access to remaining AIA state (AIA chapter 2.5) */
#define CSR_MSTATEEN0_CSRIND	BIT_ULL(60)		/* HS/VS access to s/vsiselect, s/vsireg* (Sscsrind, AIA chapter 2.5)*/
#define CSR_MSTATEEN0_WPRI2	BIT_ULL(61)		/* Reserved */
#define CSR_MSTATEEN0_ENVCFG	BIT_ULL(62)		/* HS/VS access to h/senvcfg */
#define CSR_MSTATEEN0_SEO	BIT_ULL(63)		/* HS/VS access to h/sstateen0 */
#define CSR_MSTATEEN1		0x30D
#define CSR_SSTATEEN1		0x10D
#define CSR_HSTATEEN1		0x60D
#define CSR_MSTATEEN2		0x30E
#define CSR_SSTATEEN2		0x10E
#define CSR_HSTATEEN2		0x60E
#define CSR_MSTATEEN3		0x30F
#define CSR_SSTATEEN3		0x10F
#define CSR_HSTATEEN3		0x60F
/* Upper 32bits of m/hstateenX -RV32 only */
#define CSR_MSTATEEN0H		0x31C
#define CSR_HSTATEEN0H		0x61C
#define CSR_MSTATEEN1H		0x31D
#define CSR_HSTATEEN1H		0x61D
#define CSR_MSTATEEN2H		0x31E
#define CSR_HSTATEEN2H		0x61E
#define CSR_MSTATEEN3H		0x31F
#define CSR_HSTATEEN3H		0x61F

/* Machine security configuration, 64bits (v1.12) */
#define CSR_MSECCFG		0x747
#define CSR_MSECCFG_MML 	BIT(0)			/* Machine mode lockdown (Smepmp v1.0) */
#define CSR_MSECCFG_MMWP	BIT(1)			/* Machine mode whitelist policy (Smepmp v1.0) */
#define CSR_MSECCFG_RLB 	BIT(2)			/* Rule Lock Bypass (Smepmp v1.0) */
#define CSR_MSECCFG_WPRI1	FIELD(7,3)		/* Reserved*/
#define CSR_MSECCFG_USEED	BIT(8)			/* RNG seeding available to U mode (Zkr v1.0.1) */
#define CSR_MSECCFG_SSEED	BIT(9)			/* RNG seeding available to S mode (Zkr v1.0.1) */
#define CSR_MSECCFG_MLPE	BIT(10)			/* Landing pads enabled for M-mode (Zicfilp v1.0) */
#define CSR_MSECCFG_WPRI2	FIELD(31,11)		/* Reserved */
#define CSR_MSECCFG_PMM 	FIELD_ULL(33,32)	/* Pointer Masking Mode (Smmpm v1.0) */
#define CSR_MSECCFG_WPRI3	FIELD_ULL(63,34)	/* Reserved */
/* Upper 32bits of mseccfg -RV32 only (v1.12) */
#define CSR_MSECCFGH		0x757

/* Machine Physical Memory Protection (PMP) (v1.10+, optional) */
#define CSR_PMPCFG_BASE 	0x3A0			/* 16 pmpcfgN registers (N=0-15) */
#define CSR_PMPADDR_BASE	0x3B0			/* 64 pmpaddrN registers (N=0-63) */

/* Indirect CSR (Smaia/Ssaia v1.0, Smcsrind/Sscsrind v1.0) */
#define CSR_MISELECT		0x350		/* Indirect register select */
#define CSR_MIREG		0x351		/* Aliases to selected indirect CSR */
#define CSR_MIREG2		0x352
#define CSR_MIREG3		0x353
#define CSR_MIREG4		0x355		/* Note: 0x354 is miph */
#define CSR_MIREG5		0x356
#define CSR_MIREG6		0x357

#define CSR_SISELECT		0x150
#define CSR_SIREG2		0x152
#define CSR_SIREG3		0x153
#define CSR_SIREG4		0x155
#define CSR_SIREG5		0x156
#define CSR_SIREG6		0x157

#define CSR_VSISELECT		0x250
#define CSR_VSIREG		0x251
#define CSR_VSIREG2		0x252
#define CSR_VSIREG3		0x253
#define CSR_VSIREG4		0x255
#define CSR_VSIREG5		0x256
#define CSR_VSIREG6		0x257

/* Supervisor address translation and protection, SXLEN bits (v1.10: sptbr→satp) */
/* VS address translation and protection (H v1.0 - v1.12) */
/* Hypervisor address translation and protection (H v1.0 - v1.12) */
#define CSR_SATP		0x180
#define CSR_VSATP		0x280
#define CSR_HGATP		0x680
#define CSR_SATP_PPN		FIELD_ULL(43,0) 	/* Physical page number */
#define CSR_SATP_ASID		FIELD_ULL(59,44)	/* Address space Identifier */
#define CSR_SATP_MODE		FIELD_ULL(63,60)	/* Translation mode */

#ifndef __ASSEMBLER__
enum satp_modes {
	SATP_MODE_BARE = 0,
	SATP_MODE_SV39 = 8,
	SATP_MODE_SV48 = 9,
	SATP_MODE_SV57 = 10
};
#endif

/* Supervisor resource management configuration, SXLEN bits (Ssqosid v1.0, v1.13) */
#define CSR_SRMCFG		0x181
#define CSR_SRMCFG_RCID		FIELD(11,0)	/* Resource Control ID (12 bits, WARL) */
#define CSR_SRMCFG_MCID		FIELD(27,16)	/* Monitoring Counter ID (12 bits, WARL) */

/* Hypervisor status, HSXLEN bits (H v1.0 - v1.12) */
#define CSR_HSTATUS		0x600
#define CSR_HSTATUS_WPRI1	FIELD(4,0)		/* Reserved */
#define CSR_HSTATUS_VSBE	BIT(5)			/* VS-mode in big endian */
#define CSR_HSTATUS_GVA		BIT(6)			/* Guest virtual address */
#define CSR_HSTATUS_SPV		BIT(7)			/* Supervisor previous virtualization mode */
#define CSR_HSTATUS_SPVP	BIT(8)			/* Supervisor previous virtuall privilege */
#define CSR_HSTATUS_HU		BIT(9)			/* Hypervisor in U mode */
#define CSR_HSTATUS_WPRI2	FIELD(11,10)		/* Reserved */
#define CSR_HSTATUS_VGEIN	FIELD(17,12)		/* Virtual Guest External Interrupt (AIA)*/
#define CSR_HSTATUS_WPRI3	FIELD(19,18)		/* Reserved */
#define CSR_HSTATUS_VTVM	BIT(20)			/* mstatus.TVM for VS mode */
#define CSR_HSTATUS_VTW		BIT(21)			/* mstatus.TW for VS mode */
#define CSR_HSTATUS_VTSR	BIT(22)			/* mstatus.TSR for VS mode */
#define CSR_HSTATUS_WPRI4	FIELD(31,23)		/* Reserved */
#define CSR_HSTATUS_VSXL	FIELD_ULL(33,32)	/* XLEN for VS mode (WARL) */
#define CSR_HSTATUS_WPRI5	FIELD_ULL(63,34)	/* Reserved */

/************\
* Trap setup *
\************/

/* Machine exception/interrupt delegation, MXLEN bits, WARL (v1.9.1+) */
/* Hypervisor exception/interrupt delegation, HSXLEN bits, WARL (H v1.0 - v1.12) */
#define CSR_MEDELEG		0x302
#define CSR_HEDELEG		0x602
#define CSR_MIDELEG		0x303
#define CSR_HIDELEG		0x603
/* Upper 32 bits of exception delegation -RV32 only (v1.13 draft) */
#define CSR_MEDELEGH		0x312
#define CSR_HEDELEGH		0x612

#ifndef __ASSEMBLER__
enum exception_bits {
	CAUSE_INST_ADDR_MISALIGNED	= 0,
	CAUSE_INST_ACCESS_FAULT		= 1,
	CAUSE_INST_ILLEGAL		= 2,
	CAUSE_BREAKPOINT		= 3,
	CAUSE_LOAD_ADDR_MISALIGNED	= 4,
	CAUSE_LOAD_ACCESS_FAULT 	= 5,
	CAUSE_STORE_ADDR_MISALIGNED	= 6,
	CAUSE_STORE_ACCESS_FAULT	= 7,
	CAUSE_ECALL_FROM_U		= 8,
	CAUSE_ECALL_FROM_S		= 9,
	CAUSE_ECALL_FROM_VS		= 10,	/* When Hypervisor is implemented */
	CAUSE_ECALL_FROM_M		= 11,
	CAUSE_INST_PAGE_FAULT		= 12,
	CAUSE_LOAD_PAGE_FAULT		= 13,
	CAUSE_STORE_PAGE_FAULT		= 15,
	CAUSE_DOUBLE_TRAP		= 16,	/* Smdbltrp/Ssdbltrp v1.0 */
	CAUSE_SOFTWARE_CHECK		= 18,	/* Zicfiss/Zicfilp v1.0 - CFI violations */
	CAUSE_HARDWARE_ERROR		= 19,	/* v1.13 - hardware integrity failures */
	CAUSE_INST_GUEST_PAGE_FAULT	= 20,	/* H v1.0 */
	CAUSE_LOAD_GUEST_PAGE_FAULT	= 21,	/* H v1.0 */
	CAUSE_VIRTUAL_INSTRUCTION	= 22,	/* H v1.0 */
	CAUSE_STORE_GUEST_PAGE_FAULT	= 23,	/* H v1.0 */
};

enum interrupt_bits {
	INTR_SUPERVISOR_SOFTWARE_TRIG	= 1,
	INTR_GUEST_SOFTWARE_TRIG	= 2,	/* H v1.0 - on hvip/hip/hie */
	INTR_MACHINE_SOFTWARE_TRIG	= 3,
	INTR_SUPERVISOR_TIMER		= 5,
	INTR_GUEST_TIMER		= 6,	/* H v1.0 - on hvip/hip/hie */
	INTR_MACHINE_TIMER		= 7,
	INTR_SUPERVISOR_EXTERNAL	= 9,
	INTR_GUEST_EXTERNAL		= 10,	/* H v1.0 - on hvip/hip/hie */
	INTR_MACHINE_EXTERNAL		= 11,
	INTR_HYPERVISOR_EXTERNAL	= 12,	/* H v1.0 - on hip/hie */
	INTR_LOCAL_COUNT_OVERFLOW	= 13	/* Sscofpmf v1.0 - allocated in v1.13 */
};
#endif

/* Machine interrupt enable, MXLEN bits */
/* Supervisor interrupt enable, SXLEN bits */
/* Hypervisor interrupt enable, HSXLEN bits */
/* VS interrupt enable, VSXLEN bits */
#define CSR_MIE			0x304
#define CSR_SIE			0x104
#define CSR_HIE			0x604
#define CSR_VSIE		0x204

/* Hypervisor guest external interrupt enable, HSLEN bits */
#define CSR_HGEIE		0x607

/* Machine trap vector, MXLEN bits, WARL */
/* Supervisor trap vector, SXLEN bits, WARL */
/* VS trap vector, VSXLEN bits, WARL */
#define CSR_MTVEC		0x305
#define CSR_STVEC		0x105
#define CSR_VSTVEC		0x205
#define CSR_MTVEC_MODE_MASK	FIELD(1,0)
#define CSR_MTVEC_BASE		GENMASK_ULL(63,2)

#ifndef __ASSEMBLER__
enum mtvec_modes {
	MTVEC_DIRECT	= 0,
	MTVEC_VECTORED	= 1
};
#endif


/***************\
* Trap handling *
\***************/

/* Machine scratch register, MXLEN bits */
/* Supervisor scratch register, SXLEN bits */
/* VS scratch register, VSXLEN bits */
/* Machine mode NMI scratch register, MXLEN bits (Smrnmi) */
#define CSR_MSCRATCH		0x340
#define CSR_SSCRATCH		0x140
#define CSR_VSSCRATCH		0x240
#define CSR_MNSCRATCH		0x740

/* Machine exception programm counter, MXLEN bits */
/* Supervisor exception program counter, SXLEN bits */
/* VS exception program counter, VSXLEN bits */
/* Machine mode NMI exception program counter, MXLEN bits (Smrnmi) */
#define CSR_MEPC		0x341
#define CSR_SEPC		0x141
#define CSR_VSEPC		0x241
#define CSR_MNEPC		0x741

/* Machine cause register, MXLEN bits */
/* Supervisor cause register, SXLEN bits */
/* VS cause register, VSXLEN bits */
/* Machine mode NMI cause register, MXLEN bits (Smrnmi) */
#define CSR_MCAUSE		0x342
#define CSR_SCAUSE		0x142
#define CSR_VSCAUSE		0x242
#define CSR_MNCAUSE		0x742
#define CSR_MCAUSE_CODE_MASK	GENMASK_ULL(62,0)
#define CSR_MCAUSE_INTR 	BIT_ULL(63)

/* Machine trap value register */
/* Supervisor trap value register */
/* VS trap value register */
#define CSR_MTVAL		0x343
#define CSR_STVAL		0x143
#define CSR_HTVAL		0x643
#define CSR_VSTVAL		0x243

/* Machine interrupt pending register */
/* Supervisor interrupt pending register */
/* VS interrupt pending register */
/* Hypervisor interrupt pending register */
#define CSR_MIP 		0x344
#define CSR_SIP 		0x144
#define CSR_VSIP		0x244
#define CSR_HIP			0x644
#define CSR_HVIP		0x645

/* Hypervisor guest external interrupt pending, GEILEN bits */
#define CSR_HGEIP		0xE12

/* Machine trap instruction register */
/* Hypervisor trap instruction register */
#define CSR_MTINST		0x34A
#define CSR_HTINST		0x64A

/* Machine second trap value register */
#define CSR_MTVAL2		0x34B

/* Machine mode NMI status, MXLEN (Smrnmi) */
#define CSR_MNSTATUS		0x744
#define CSR_MNSTATUS_WPRI1	FIELD(2,0)
#define CSR_MNSTATUS_NMIE	BIT(3)		/* All interrupts (including NMIs) disabled */
#define CSR_MNSTATUS_WPRI2	FIELD(6,4)
#define CSR_MNSTATUS_MPV	BIT(7)		/* Previous virtualization mode */
#define CSR_MNSTATUS_WPRI3	BIT(8)
#define CSR_MNSTATUS_MNPELP	BIT(9)		/* Previous expected landing pad state */
#define CSR_MNSTATUS_WPRI4	BIT(10)
#define CSR_MNSTATUS_MNPP	FIELD(12,11)	/* Previous privilege mode */
#define CSR_MNSTATUS_WPRI5	FIELD_ULL(63,13)


/*********************************\
* Performance counter/timer setup *
\*********************************/

/* Machine counter enable, 32bits (v1.9.1+) */
/* Supervisor counter enable, 32bits (v1.9.1+) */
/* Hypervisor counter enable, 32bits (H v1.0 - v1.12) */
#define CSR_MCOUNTEREN		0x306
#define CSR_SCOUNTEREN		0x106
#define CSR_HCOUNTEREN		0x606
#define CSR_MCOUNTEREN_CY	BIT(0)	/* Cycle counter enable */
#define CSR_MCOUNTEREN_TM	BIT(1)	/* Timer enabe (XXX: bug in spec ?,
					 * https://github.com/riscv/riscv-isa-manual/issues/883) */
#define CSR_MCOUNTEREN_IR	BIT(2)	/* Instructions retired */
#define CSR_MCOUNTEREN_HPM(_n)	(1 << _n)

/* Start with all counters disabled */
#define CSR_MCOUNTEREN_INIT	0

/* Machine counter inhibit, 32bits (v1.11) */
/* Supervisor counter inhibit, 32bits (Smcdeleg v1.0) */
#define CSR_MCOUNTINHIBIT	0x320
#define CSR_SCOUNTINHIBIT	0x120
#define CSR_MCOUNTINHIBIT_CY	BIT(0)	/* Cycle counter */
#define CSR_MCOUNTINHIBIT_IR	BIT(2)	/* Instructions retired */
#define CSR_MCOUNTINHIBIT_HPM(_n)	(1 << _n)

/* Start with all counters except cycle/instret inhibited */
#define CSR_MCOUNTINHIBIT_INIT	(0xFFFFFFFF & ~(CSR_MCOUNTINHIBIT_CY | CSR_MCOUNTINHIBIT_IR))

/* Machine mode event selector (v1.9.1: basic, Sscofpmf v1.0: added mode filtering & OF bit) */
#define CSR_MHPMEVENT(_n)	(CSR_MCOUNTINHIBIT + _n)
#define CSR_MHPMEVEHTH(_n)	(0x720 + _n)	/* Higher 32bits, RV32 only */
#define CSR_MHPMEVENT_VUINH	BIT(58)	/* Inhibited on VU-mode (Sscofpmf v1.0) */
#define CSR_MHPMEVENT_VSINH	BIT(59)	/* Inhibited on VS-mode (Sscofpmf v1.0) */
#define CSR_MHPMEVENT_UINH	BIT(60)	/* Inhibited on U-mode (Sscofpmf v1.0) */
#define CSR_MHPMEVENT_SINH	BIT(61)	/* Inhibited on S-mode (Sscofpmf v1.0) */
#define CSR_MHPMEVENT_MINH	BIT(62)	/* Inhibited on M-mode (Sscofpmf v1.0) */
#define CSR_MHPMEVENT_OF	BIT(63)	/* Overflow status (Sscofpmf v1.0) */

/* Cycle/instret counter filtering (Smcntrpmf v1.0, same fields as MHPMEVENT_*INH) */
#define CSR_MCYCLECFG		0x321
#define CSR_MINSTRETCFG		0x322

/* Supervisor counter configuration (Smcdeleg/Ssccfg v1.0) */
/* Note: scountinhibit already defined above @ 0x120 */
#define CSR_SCYCLECFG		0x121	/* Supervisor cycle counter config */
#define CSR_SINSTRETCFG		0x122	/* Supervisor instret counter config */
/* Supervisor HPM event selectors (shpmeventN @ 0x123-0x13F, N=3-31) */
#define CSR_SHPMEVENT(_n)	(0x120 + _n)	/* N=3-31 */

/* Supervisor counter overflow, 32bits (Sscofpmf v1.0) */
#define CSR_SCOUNTOVF		0xDA0

/* Delta for VS/VU-mode timer, 64bits (H v1.0 - v1.12) */
#define CSR_HTIMEDELTA		0x605
#define CSR_HTIMEDELTAH		0x615

/* S/VS-mode time compare, 64bits (Sstc v1.0, incorporated v1.13) */
#define CSR_STIMECMP		0x14D
#define CSR_STIMECMPH		0x15D
#define CSR_VSTIMECMP		0x24D
#define CSR_VSTIMECMPH		0x25D

/*******************************\
* Performance Counters / timers *
\*******************************/

#ifndef __ASSEMBLER__
enum hart_counters {
	HC_CYCLES	= 0,
	HC_TIME		= 1,
	HC_INSTRET	= 2
};
#endif

/* Machine-mode counters (v1.9.1+) */
#define CSR_MCYCLE			0xB00
#define CSR_MINSTRET			0xB02
#define CSR_MHPMCOUNTER(_n)		(CSR_MCYCLE + _n)
/* Upper 32bits -RV32 only */
#define CSR_MCYCLEH			0xB80
#define CSR_MINSTRETH			0xB82
#define CSR_MHPCOUNTERH(_n)		(CSR_MCYCLEH + _n)

/* User-mode counters (Zicntr v2.0 for cycle/time/instret, Zihpm v2.0 for hpmcounterN) */
#define CSR_CYCLE			0xC00
#define CSR_TIME			0xC01
#define CSR_INSTRET			0xC02
#define CSR_HPMCOUNTER3			0xC03
#define CSR_HPMCOUNTER(_n)		(CSR_CYCLE + _n)	/* N=3-31 */
/* Upper 32bits -RV32 only */
#define CSR_CYCLEH			0xC80
#define CSR_TIMEH			0xC81
#define CSR_INSTRETH			0xC82
#define CSR_HPMCOUNTER3H		0xC83
#define CSR_HPMCOUNTERH(_n)		(CSR_CYCLEH + _n)	/* N=3-31 */

/**********************************\
* Control Transfer Records (Smctr) *
\**********************************/

/* Machine Control Transfer Records (Smctr v1.0, v1.13) */
#define CSR_MCTRCTL		0x34E	/* Machine CTR control */

/* Supervisor Control Transfer Records (Ssctr v1.0, v1.13) */
#define CSR_SCTRCTL		0x14E	/* Supervisor CTR control */
#define CSR_SCTRSTATUS		0x14F	/* Supervisor CTR status (WRPTR, FROZEN) */
#define CSR_SCTRDEPTH		0x15F	/* Supervisor CTR depth (16/32/64/128/256) */

/* Virtual Supervisor Control Transfer Records (Ssctr v1.0 + H, v1.13) */
#define CSR_VSCTRCTL		0x24E	/* Virtual supervisor CTR control */

/* CTR entry registers accessed indirectly via siselect (Sscsrind/Smcsrind)
 * siselect range 0x200-0x2FF for logical entries 0-255:
 *   sireg   -> ctrsource (source PC with valid bit)
 *   sireg2  -> ctrtarget (target PC with optional mispred bit)
 *   sireg3  -> ctrdata (transfer type and cycle count metadata)
 */

/***************************\
* Vector extension (V v1.0) *
\***************************/

/* Vector configuration CSRs (user-mode, November 2021) */
#define CSR_VSTART		0x008	/* Vector start position (URW) */
#define CSR_VXSAT		0x009	/* Fixed-point saturate flag (URW) */
#define CSR_VXRM		0x00A	/* Fixed-point rounding mode (URW) */
#define CSR_VCSR		0x00F	/* Vector control and status (URW, combines vxrm+vxsat) */
#define CSR_VL			0xC20	/* Vector length (URO, read-only) */
#define CSR_VTYPE		0xC21	/* Vector data type register (URO, read-only) */
#define CSR_VLENB		0xC22	/* VLEN/8 in bytes (URO, read-only, constant) */


/***************************************\
* Crypto and Control Flow Integrity CSRs *
\***************************************/

/* Entropy source (Zkr v1.0.1, December 2021) */
#define CSR_SEED		0x015	/* Physical entropy bits for RNG seeding (URO, 16 bits) */
/* Access controlled via mseccfg.USEED (U-mode) and mseccfg.SSEED (S-mode) */

/* Shadow stack pointer (Zicfiss v1.0) */
#define CSR_SSP			0x011	/* Supervisor shadow stack pointer (SRW) */
/* Enabled via *envcfg.SSE (menvcfg/henvcfg/senvcfg) */


/*******************************************************\
* Advanced Interrupt Architecture (Smaia/Ssaia v1.0) *
\*******************************************************/

/* Note: *iselect / *ireg CSRs already defined in Indirect CSR section above */

/* Supervisor-level AIA CSRs (Ssaia v1.0, June 2023) */
#define CSR_STOPEI		0x15C	/* Supervisor top external interrupt (SRO) */
#define CSR_STOPI		0xDB0	/* Supervisor top interrupt (SRO) */

/* Virtual Supervisor-level AIA CSRs (with H extension) */
#define CSR_VSTOPEI		0x25C	/* VS top external interrupt (VRO) */
#define CSR_VSTOPI		0xEB0	/* VS top interrupt (VRO) */

/* Machine-level AIA CSRs (Smaia v1.0, June 2023) */
#define CSR_MTOPEI		0x35C	/* Machine top external interrupt (MRO) */
#define CSR_MTOPI		0xFB0	/* Machine top interrupt (MRO) */
#define CSR_MIPH		0x354	/* Upper 32 bits of mip (RV32 only) */

/* AIA uses the indirect CSR mechanism (*iselect / *ireg) for:
 * - Interrupt priority arrays (IPRIO0-15 @ iselect 0x30-0x3F)
 * - IMSIC registers (EIDELIVERY @ 0x70, EITHRESHOLD @ 0x72,
 *                    EIPx @ 0x80-0xBF, EIEx @ 0xC0-0xFF)
 * See AIA spec chapter 2 for full indirect register map */


/***********************************************\
* Debug/trace registers, shared with Debug Mode *
\***********************************************/

/*
 * Note: References here are from the Debug spec:
 * https://github.com/riscv/riscv-debug-spec
 */

/* Trigger select, chapter 5.6.1 */
#define CSR_TSELECT		0x7A0

/* Trigger data 1, chapter 5.6.2 */
#define CSR_TDATA1		0x7A1
#define CSR_MCONTROL		0x7A1	/* When tdata1 type is 2, chapter 5.6.11 */
#define CSR_MCONTROL6		0x7A1	/* When tdata1 type is 6, chapter 5.6.12 */
#define CSR_ICOUNT		0x7A1	/* When tdata1 type is 3, chapter 5.6.13 */
#define CSR_ITRIGGER		0x7A1	/* When tdata1 type is 4, chapter 5.6.14 */
#define CSR_ETRIGGER		0x7A1	/* When tdata1 type is 5, chapter 5.6.15 */
#define CSR_TMEXTTRIGGER	0x7A1	/* When tdata1 type is 7. chapter 5.6.16 */

/* Trigger data 2, chapter 5.6.3 */
#define CSR_TDATA2		0x7A2

/* Trigger data 3, chapter 5.6.4 */
#define CSR_TDATA3		0x7A3
#define CSR_TEXTRA32		0x7A3	/* When tdata3 type is 2 - 6 and XLEN is 32, chapter 5.6.17 */
#define CSR_TEXTRA64		0x7A3	/* When tdata3 type is 2 - 6 and XLEN is 64, chapter 5.6.18 */

/* Trigger info, chapter 5.6.5 */
#define CSR_TINFO		0x7A4

/* Trigger control, chapter 5.6.6 */
#define CSR_TCONTROL		0x7A5

/* Machine/supervisor/hypervisor context,
 * chapter 5.6.9 (Sdtrig v1.0) */
#define CSR_MCONTEXT		0x7A8
#define CSR_SCONTEXT		0x5A8
#define CSR_HCONTEXT		0x6A8

/*********\
* Helpers *
\*********/

#ifndef __ASSEMBLER__

/* We can't turn those to static inline functions because the
 * CSR numbers are immediates and we can't pass them on as variables. */
#define csr_read(addr)				\
({						\
	volatile unsigned long __v;		\
	__asm__ __volatile__("csrr %0, %1"	\
			     : "=r"(__v)	\
			     : "i"(addr)	\
			     : "memory");	\
	__v;					\
})

#define csr_write(addr, val)				\
({							\
	unsigned long long __v = (unsigned long long)(val);	\
	__asm__ __volatile__("csrw %0, %1"		\
			     :				\
			     : "i"(addr), "rK"(__v)	\
			     : "memory");		\
})

#define csr_set_bits(addr, val)				\
({							\
	unsigned long long __v = (unsigned long long)(val);	\
	__asm__ __volatile__("csrs %0, %1"		\
			     :				\
			     : "i"(addr), "rK"(__v) 	\
			     : "memory");		\
})

#define csr_clear_bits(addr, val)			\
({							\
	unsigned long long __v = (unsigned long long)(val);	\
	__asm__ __volatile__("csrc %0, %1"		\
			     :				\
			     : "i"(addr), "rK"(__v)	\
			     : "memory");		\
})

/* Pause hint - reduces power consumption while spinning */
static inline void
pause(void)
{
	#if defined(__riscv_zihintpause)
		__asm__ __volatile__("pause" ::: "memory");
	#endif
}

/* Note that both wfi/pause are hints, always use this inside
 * a loop since it can be a simple nop. Also note that we can't
 * just include a check for mip & mie, since the trap handler
 * will clean up any pending interrupts before returning e.g.
 * after the wfi instruction (if implemented), and we'll end
 * up in an infinite loop. */
static inline void
wfi(void)
{
	#if !defined(PLAT_NO_WFI)
		/* Try to sleep - might block, might be nop, might
		 * be a bug too which is why there is a macro for it */
		__asm__ __volatile__("wfi" ::: "memory");
	#endif
	#if defined(PLAT_QUIRK_WFI_EPC)
		/* This is a hw bug where on wfi instead of
		 * advancing epc by 4 it advances it by 8-12
		 * which leads to infinite loops/hangs. */
		__asm__ __volatile__("nop;nop;");
	#endif
	/* Hint that we're spinning if wfi was a nop */
	pause();
}
#endif /* __ASSEMBLER__ */
#endif /* _CSR_H */