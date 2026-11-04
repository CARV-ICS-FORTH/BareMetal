# Milk-V Duo S target

Sophgo **SG2000** / CVITek **CV181x** family. Derived from the board device
tree `cv181x_milkv_duos_sd.dts`.

We run as the FIP **MONITOR** image (replacing OpenSBI's `fw_dynamic`) on the
application C906 core. Our image (ROM+RAM) sits at the **top** of DRAM
(`PLAT_ROM_BASE = 0x9fb80000`, ending at the `0x9fe00000` DRAM top, just below
the little-core `rproc` reserve), leaving the whole bottom of DRAM
(`0x80000000 .. 0x9fb80000`, ~507 MB, `PLAT_SYSRAM_*`) as a clean window for
the application (DMA, NetBoot image staging, ...). The second C906 (little
core) and the 8051 run their own firmware and are not touched by this image.

## What is wired up

Only CLINT, PLIC and the console UART are described in `target_config.h`, plus
the UART interrupt (PLIC source 44, level-high). Ethernet
(`cvitek,ethernet`, a DesignWare GMAC at `0x04070000`, PLIC source 31) has its
base/IRQ noted for later; there is no NIC driver yet.

| Block | Address | Notes |
|-------|---------|-------|
| UART0 (console) | `0x04140000` | `snps,dw-apb-uart`, 25 MHz ref clk, 32-bit/word-strided regs, IRQ 44 |
| PLIC  | `0x70000000` | `riscv,plic0`, 101 sources, max priority 7, M-mode context 0 |
| CLINT | `0x74000000` | `riscv,clint0` — see the MTIMER caveat below |

### DesignWare APB UART

UART0 is a Synopsys DW APB UART, handled in the shared `platform/src/uart.c`
behind `PLAT_UART_DW_APB`. The differences from a plain 8250/16550 that the
driver accounts for:

* **Busy-safe LCR programming** — the DW UART drops writes to the Line
  Control Register while it is mid-transfer (and raises a *busy-detect*
  interrupt instead). `uart_init()` quiesces the controller first (status
  read + soft reset via the SRR register) so the divisor actually gets
  programmed, and the IRQ path swallows busy-detect interrupts.
* **Fractional divisor (DLF)** — with a 25 MHz clock, 115200 baud wants a
  divisor of 13.56; integer-only would be ~4% off, so we also program the
  DLF register (DLL=13, DLF=9) landing within ~0.01%.

The core register map is 16550-compatible, so `uart_getc()`/`uart_putc()` are
unchanged.

### CLINT / timer

This C906 CLINT implements only `msip` (`+0x0`) and `mtimecmp` (`+0x4000`) as
MMIO registers; it does **not** implement `mtime` (`+0xBFF8`) — a load there
faults. So `mtime` is read through the architected read-only `time` CSR
(`rdtime`) instead, which is the SDK default (`PLAT_MTIMER_USE_TIME_CSR`) and
how Linux/OpenSBI read it anyway. On this SoC it is mandatory rather than
merely preferred.

`mtimecmp` is still written over MMIO. The DTS marks the CLINT
`clint,has-no-64bit-mmio` (32-bit-only bus), so the target sets
`PLAT_MTIMER_MMIO_BITS = 32` and `mtimer.h` writes it as two 32-bit halves
(high raised to all-ones first). The `msip`/IPI side is 32-bit already, so
IPIs need nothing special.

## QEMU

There is **no upstream QEMU machine** for the SG2000 / CV181x / Sophgo (checked
against QEMU 11.0.x: the RISC-V machine list has no `sophgo`/`cvitek`/`duo`
board). So there is no `run.sh` for this target — it cannot be reused with the
generic `virt` machine either, since the UART/PLIC/CLINT addresses differ
(that is what the `qemu` target is for). Bring-up happens on real hardware.

## Flashing: `fip_replace_monitor.py`

`fip_replace_monitor.py` rebuilds a boot FIP image by swapping just the MONITOR
(OpenSBI) payload for our bare-metal binary, reusing an existing `fip.bin`. It
needs only the Python standard library — no vendor SDK — so you can
re-spin images quickly from a single reference `fip.bin`.

```sh
# build/bm_testsuite.milkv-duos.bin is produced by `make`
./fip_replace_monitor.py fip.bin ../../../build/bm_testsuite.milkv-duos.bin fip_bm.bin
```

It keeps the signed param1 header, BLCP, BL2 (FSBL), BLCP_2ND (RTOS) and the
compressed LOADER_2ND (u-boot) byte-for-byte, and recomputes only the MONITOR
size/checksum and the param2 layout/checksum. `MONITOR_RUNADDR` defaults to
`0x9fb80000` (= `PLAT_ROM_BASE`, where our image links); override with
`--runaddr` if you relink elsewhere. Write the resulting `fip_bm.bin` where the
original `fip.bin` goes (e.g. the FIP partition / offset of the SD card).
