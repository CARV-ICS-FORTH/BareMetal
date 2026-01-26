# BareMetal SDK

A lightweight, freestanding SDK for RISC-V bare-metal development with a freestanding C23-compliant standard library (YaLibC) and comprehensive platform support.
For now this only targets RV64 little-endian systems, but in the future we may add support for RV32 as well, and even big endian if needed. Initialy it was used
for writing BootROM images for our RISC-V prototypes, and also platform-level tests for hw validation (a subset of those is part of the testsuite). These days we
also use it for further experimentation, and also for education, to help students familiarize themselves with RISC-V and bare metal programming, without having
to study a full OS (that's why it's full of extensive comments).

## Overview

The BareMetal SDK provides everything needed to build efficient bare-metal applications for RISC-V systems. It features:

- **YaLibC**: A freestanding C23 standard library implementation
- **Platform Layer**: Hardware abstraction for RISC-V peripherals and system features
- **Multi-Target Support**: Build system supporting multiple hardware configurations
- **Integrated Test Suite**: Comprehensive tests for both library functions and platform features

Originally developed in 2019 at CARV/ICS-FORTH, the SDK has evolved to support modern RISC-V extensions. An overview of the RISC-V priv. spec. evolution is
included in csr.h.

### Directory layout

```
sdk/
├── yalibc/              # C23 standard library implementation
│   ├── include/         # Public headers (string.h, stdio.h, stdlib.h, etc.)
│   └── src/             # Library implementation
│
├── platform/            # Platform abstraction layer
│   ├── include/
│   │   ├── interfaces/  # HAL interfaces (uart.h, timer.h, irq.h, ipi.h)
│   │   ├── riscv/       # RISC-V specific (hart.h, csr.h, mtimer.h, caps.h)
│   │   ├── utils/       # Utilities (locks, bitfields, register macros)
│   │   └── templates/   # Linker script templates
│   ├── src/             # Platform implementation
│   └── patches/         # Optional simplified versions (e.g., simple_printf.c)
│
├── targets/             # Target-specific configurations
│   ├── qemu/            # Standard QEMU virt machine
│   ├── qemu-aplic/      # QEMU with APLIC
│   └── qemu-aia/        # QEMU with full AIA (APLIC + IMSIC)
│
├── testsuite/           # Comprehensive test suite
│   ├── yalibc/          # Tests for C library functions
│   ├── platform/        # Tests for platform features
│   └── main.c           # Interactive test menu
│
├── build.mk             # Common build configuration
└── sdk.mk               # Main SDK build system
```

## Features

### YaLibC - C23 Standard Freestanding Library

- **String Operations** (`<string.h>`):
  - All mem*/str* required by C23 for freestanding implementations (earlier revisions only required headers).
  - Optimized with word-at-a-time operations and Two-Way string search algorithm for `strstr`.

- **I/O Functions** (`<stdio.h>`):
  - `printf` family with full format specifier support (including floating-point via Ryu algorithm), note that it doesn't support %n for security reasons.
  - `getchar` for interactive input
  - Note that a simpler/smaller/non-compliant printf implementation is provided as a patch (see platform/patches/simple_printf.c) without support for double or complex formatting.

- **Standard Library** (`<stdlib.h>`):
  - A simple LIFO allocator, built around `realloc` that can free/resize the last allocation and free in the reverse order.
  - It also has a metadata/redzone tail on each allocation to verify consistency and catch some memory corruption bugs.

- **Time Functions** (`<time.h>`/ `<threads.h>`):
  - Good old `clock` from C89 for reading the cycle counter in "clock ticks".
  - `thrd_sleep`from C11 (part of the concurency support library, hence the `thread.h` header) for sleeping
  - `timespec_get`/`timespec_getres` from C23
  - Their POSIX counterparts for simplicity: `clock_gettime`, `clock_getres`, `nanosleep`.
  - Note that TIME_* C ids map to CLOCK_* POSIX ids, and POSIX functions are built on top of the C standard ones (so you can stick with C23 if you want).

Also `platform/utils/lock.h` provides a simple lock mechanism (note that stdatomic.h is also available via the compiler, and there is even a "trick" in `atomic_stubs.c` for implementations
without full atomics support), and `platform/utils/utils.h` can be used for console output with ANSI colors, debug levels etc (you can save space by defining NO_ANSI_COLORS).

### Platform Layer

Hardware abstraction and system initialization:

- **Hart (Hardware Thread) Management**:
  - Multi-hart initialization and control, with support for sparse hart ids.
  - Per-hart state management and TLS (e.g. `errno`)
  - Capability probing for runtime hardware detection

- **Interrupt Handling**:
  - SiFive PLIC (Platform-Level Interrupt Controller) support
  - RISC-V APLIC (Advanced PLIC) support
  - RISC-V AIA (Advanced Interrupt Architecture) with IMSIC support
  - Flexible interrupt routing and priority management

- **Inter-Processor Interrupts (IPI)**:
  - CLINT (Core-Local Interrupt Controller)
  - ACLINT/MSWI (Machine Software Interrupt) support
  - IMSIC-based IPI support
  - Hart wakeup and synchronization

- **Timers**:
  - RISC-V MTIMER (also part of CLINT/ACLINT)
  - RISC-V cycle counter
  - Exposed with a similar API to C23/POSIX
  - Follows the multiplier/shifter approach when converting time units to avoid doubles/division.

- **UART Driver**:
  - 16550-compatible UART support (as required by RVA23)
  - Interrupt-driven and polling modes

- **Random Number Generation**:
  - Hardware RNG support via Zkr extension
  - Seed-based fallback implementation using counters etc

### Build System

For each target the build system (`make all`) will generate `build/libplatform_<target>.a` and `build/ldscripts/bmmap.<target>.ld`. Those can later be used for static linking
any C program, as long as it uses only the provided C functions from yalibc (should cover lots of use cases). The idea is to be able to run C code without any modifications, in
bare metal. The resulting binary can be striped and used directly, the linker script will take care of the memory layout and libplatform will take care of initializing and providing
the C environment.

Everything is compiled with LTO and a few other flags, you may also want to switch from -O2 to -Os but keep an eye for things that may break, since the compiler's optimization passes
may fight each other. For more infos check out `sdk/build.mk` and `sdk/sdk.mk`, applications may include `build.mk` directly in their Makefile to inherit flags/paths etc for simplicity.

## Limitations

BootROM code runs from a rom region that may be very far from the ram region. In such cases pc-relative addressing won't work (it's limited to +-2GB from pc), and we can only play with
gp-relative addressing (for more infos check out the [RISC-V ELF psABI spec](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc)), that's limited to +-4KB
from gp. That means we can move .data/.bss anywhere but it can't be larger than 4KB otherwise gp-relative addressing too won't work. The "we can move them anywhere" part is also a bit of
a challenge, check out `platform/include/templates/bmbase.ld.tmpl`, the template for the linker script, for more infos on how we deal with the whole mess.

Long story short: keep .data/.bss smaller than 4KB, you can still do whatever you want with your stack and heap, this is just an issue for global symbols that need to be resolved at
compile time. That's also the main reason why there is a malloc in yalibc.

## Getting Started

### Prerequisites

1. **RISC-V Toolchain**: You need a RISC-V GCC toolchain, since we use our own libc, it doesn't matter which one.

   We recommend using [YARVT](https://github.com/CARV-ICS-FORTH/yarvt) to build a proper toolchain:
   ```bash
   git clone https://github.com/CARV-ICS-FORTH/yarvt.git
   cd yarvt
   ./yarvt build_toolchain newlib
   source yarvt  # Sets up PATH
   ```

   Alternatively grab a pre-compiled toolchain from the [riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain/releases) repo.

2. **QEMU** (for testing): QEMU 9.0+ with RISC-V support
   ```bash
   sudo apt install qemu-system-misc
   ```

3. **Build Tools**:
   ```bash
   sudo apt install build-essential device-tree-compiler
   ```

### Building the SDK

```bash
cd sdk
make libs        # Build platform libraries for all targets
make ldscripts   # Generate linker scripts
make testsuite   # Build the test suite
```

Or build everything at once:
```bash
make all
```

### Running Tests

Each target includes a `run.sh` script for testing on QEMU:

```bash
make test TARGET=qemu           # Run testsuite on QEMU (default PLIC+CLINT)
make test TARGET=qemu-aplic     # Test with APLIC
make test TARGET=qemu-aia       # Test with full AIA (APLIC+IMSIC)
```

The test suite is interactive - you'll see a menu where you can select test categories (YaLibC or Platform) and individual tests to run.

### Available Targets

- **qemu**: Standard QEMU virt machine (PLIC + CLINT)
- **qemu-aplic**: QEMU with APLIC (Advanced PLIC)
- **qemu-aia**: QEMU with full AIA support (APLIC + IMSIC)

## Examples

**Tip**: Study the test suite in `sdk/testsuite/` for more complete examples showing how to use various SDK features.

## Target Configuration

Each target is defined by a `target_config.h` file that specifies:

- Memory layout (RAM/ROM addresses and sizes)
- UART configuration (base address, clock, baud rate)
- Interrupt controller type (PLIC/APLIC/IMSIC)
- Number of harts (cores)
- Available peripherals

Example from `targets/qemu/target_config.h`:
```c
#define PLAT_ROM_BASE       0x80000000
#define PLAT_RAM_BASE       0x80200000
#define PLAT_UART_BASE      0x10000000
#define PLAT_UART_CLOCK_HZ  3686400
#define PLAT_UART_BAUD_RATE 115200
#define PLAT_NUM_HARTS      4
```

To create a new target, copy the template from `platform/include/templates/target_template.h` and adjust values for your hardware.

## Advanced Features

### Multi-Hart Programming

Wake up additional harts and run code on them:

```c
#include <platform/riscv/hart.h>

void secondary_hart_main(void) {
    struct hart_state *hs = hart_get_hstate_self();
    printf("Hello from hart %d!\n", hs->hart_idx);
}

void main(void) {
    // Wake up hart 1 to run secondary_hart_main
    hart_wakeup_with_addr(1, (uintptr_t)secondary_hart_main, 0, 0, 0);

    // Or wake all harts
    hart_wakeup_all_with_addr((uintptr_t)secondary_hart_main, 0, 0, 0);
}
```

You may also chose to wake them up all together at a specific time (e.g. for stress-testing atomics etc), check out `hart.h` for more infos.

### Interrupt/trap Handling

Check out `uart.c` and `uart_test.c`, or `aplic_test.c` to get an idea on how to use external interrupts. It should be straight forward. Note
that most trap handlers are defined as weak symbols so applications can override them (see `hart.c` to get an idea).

### Hardware Capability Probing

Detect available hardware features at runtime:

```c
#include <platform/riscv/caps.h>
#include <platform/riscv/hart.h>

void main(void) {
    struct rvcaps caps;
    hart_probe_priv_caps(&caps);

    if (caps.has_fpu)
        printf("FPU available\n");
    if (caps.has_vector)
        printf("Vector extension available\n");
}
```

## License

```
SPDX-License-Identifier: Apache-2.0

Copyright 2019-2026 Nick Kossifidis <mick@ics.forth.gr>
Copyright 2019-2026 ICS/FORTH
```

Individual files may have different copyright years reflecting their actual development history.

Some files (`csr.h`, `mmio.h`) are in the public domain.

## Contributing

This SDK is developed as part of research at the Institute of Computer Science, Foundation for Research and Technology - Hellas (ICS-FORTH).
