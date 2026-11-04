#!/usr/bin/env python3
# SPDX-FileType: SOURCE
#
# SPDX-FileCopyrightText: 2025-2026 Nick Kossifidis <mick@ics.forth.gr>
# SPDX-FileCopyrightText: 2025-2026 ICS/FORTH
#
# SPDX-License-Identifier: Apache-2.0
"""
Replace the MONITOR (OpenSBI) payload inside an existing CVITek/Sophgo FIP
image with our own bare-metal binary, without needing the vendor SDK.

Background
----------
On the SG2000 / CV181x the mask ROM boot chain is driven by a "FIP" image
(fip.bin, written to the start of the boot medium). Its layout, produced by
the vendor's fsbl/plat/cv181x/fiptool.py, is:

    param1 (0x1000)        header: magic, checksums, PARAM2_LOADADDR, ...
    BLCP                   (body1)
    BL2  (FSBL)            (body1)
    param2 (0x1000)        header: per-component load/run addrs, sizes, crc
    [DDR_PARAM]            (body2, often absent)
    BLCP_2ND  (RTOS)       (body2)
    MONITOR   (OpenSBI)    (body2)   <-- this is what we replace
    LOADER_2ND (u-boot)    (body2, usually lzma-compressed)

The FSBL (BL2) copies each body2 component to its RUNADDR and, for us,
jumps into MONITOR at MONITOR_RUNADDR (0x80000000). By dropping our
bare-metal image in place of OpenSBI we take over the application core
right there.

This tool parses an existing fip.bin, swaps out just the MONITOR region
(recomputing sizes/checksums and re-laying-out everything after param2),
and writes a new fip.bin. Everything else - the signed param1 header,
BLCP, BL2, BLCP_2ND and the compressed LOADER_2ND - is preserved byte for
byte, so param1 (which is covered by the SoC's secure-boot signatures) is
never touched.

It only depends on the Python standard library.

Example
-------
    ./fip_replace_monitor.py fip.bin bm_testsuite.milkv-duos.bin fip_bm.bin

By default MONITOR_RUNADDR is set to DEFAULT_MONITOR_RUNADDR below (where our
image is linked, i.e. PLAT_ROM_BASE). Override with --runaddr if you relink
the image elsewhere.
"""

import argparse
import binascii
import struct
import sys

# Where the FSBL loads the MONITOR (our bare-metal image) and jumps to it.
# MUST match PLAT_ROM_BASE in target_config.h: we place our image at the top
# of DRAM, so this differs from the stock OpenSBI MONITOR_RUNADDR (0x80000000).
DEFAULT_MONITOR_RUNADDR = 0x9fb80000

IMAGE_ALIGN = 512
PARAM1_SIZE = 0x1000
PARAM2_SIZE = 0x1000

PARAM1_MAGIC = b"CVBL01\n\0"
PARAM2_MAGIC = b"CVLD02\n\0"

# Offsets inside param1 we care about (see vendor fiptool.py param1 table).
P1_MAGIC1 = 0x00
P1_PARAM2_LOADADDR = 0xE0

# Offsets inside param2 (see vendor fiptool.py param2 table).
P2_MAGIC1 = 0x00
P2_CKSUM = 0x08  # crc covers everything *after* this field
P2_DDR_PARAM_CKSUM = 0x10
P2_DDR_PARAM_LOADADDR = 0x14
P2_DDR_PARAM_SIZE = 0x18
P2_BLCP_2ND_CKSUM = 0x20
P2_BLCP_2ND_LOADADDR = 0x24
P2_BLCP_2ND_SIZE = 0x28
P2_BLCP_2ND_RUNADDR = 0x2C
P2_MONITOR_CKSUM = 0x30
P2_MONITOR_LOADADDR = 0x34
P2_MONITOR_SIZE = 0x38
P2_MONITOR_RUNADDR = 0x3C
P2_LOADER_2ND_LOADADDR = 0x44

# LOADER_2ND header (prepended to the u-boot payload).
LDR_HDR_MAGIC = 0x04
LDR_HDR_SIZE = 0x0C
LOADER_2ND_MAGICS = (b"BL33", b"B3MA", b"B3Z4")  # raw / lzma / lz4


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def set_u32(buf, off, val):
    struct.pack_into("<I", buf, off, val & 0xFFFFFFFF)


def image_crc(image):
    """CVITek image checksum: crc16-ccitt (crc_hqx) + literal 0xCAFE tag."""
    crc = binascii.crc_hqx(bytes(image), 0)
    return struct.pack("<H", crc) + b"\xFE\xCA"


def pad(data, block_size=IMAGE_ALIGN):
    rem = len(data) % block_size
    if rem:
        data = data + b"\0" * (block_size - rem)
    return data


def extract(fip, loadaddr, size, what):
    if loadaddr + size > len(fip):
        sys.exit("error: %s region [%#x,+%#x) runs past end of fip (%#x bytes)"
                 % (what, loadaddr, size, len(fip)))
    return fip[loadaddr:loadaddr + size]


def main():
    ap = argparse.ArgumentParser(
        description="Replace the MONITOR (OpenSBI) payload in a CVITek/Sophgo "
                    "fip.bin with a bare-metal binary.")
    ap.add_argument("input_fip", help="existing fip.bin to start from")
    ap.add_argument("monitor", help="bare-metal binary to install as MONITOR")
    ap.add_argument("output_fip", help="fip.bin to write")
    ap.add_argument("--runaddr", type=lambda x: int(x, 0), default=None,
                    help="MONITOR_RUNADDR (default %#x, = PLAT_ROM_BASE)"
                         % DEFAULT_MONITOR_RUNADDR)
    args = ap.parse_args()

    with open(args.input_fip, "rb") as f:
        fip = f.read()
    with open(args.monitor, "rb") as f:
        new_monitor = f.read()

    if len(fip) < PARAM1_SIZE or fip[P1_MAGIC1:P1_MAGIC1 + 8] != PARAM1_MAGIC:
        sys.exit("error: %s is not a CVITek FIP (bad param1 magic)" % args.input_fip)

    param2_loadaddr = u32(fip, P1_PARAM2_LOADADDR)
    if param2_loadaddr == 0 or param2_loadaddr + PARAM2_SIZE > len(fip):
        sys.exit("error: bad PARAM2_LOADADDR (%#x)" % param2_loadaddr)

    param2 = bytearray(fip[param2_loadaddr:param2_loadaddr + PARAM2_SIZE])
    if param2[P2_MAGIC1:P2_MAGIC1 + 8] != PARAM2_MAGIC:
        sys.exit("error: bad param2 magic")

    # Everything up to and including body1 is kept verbatim; param1 is signed
    # and PARAM2_LOADADDR does not move, so param1 needs no changes.
    fip1 = fip[:param2_loadaddr]

    # --- Pull the existing body2 components out of the input image. ---
    ddr_size = u32(param2, P2_DDR_PARAM_SIZE)
    ddr = extract(fip, u32(param2, P2_DDR_PARAM_LOADADDR), ddr_size, "DDR_PARAM") if ddr_size else b""

    blcp2_size = u32(param2, P2_BLCP_2ND_SIZE)
    blcp2 = extract(fip, u32(param2, P2_BLCP_2ND_LOADADDR), blcp2_size, "BLCP_2ND") if blcp2_size else b""

    old_mon_size = u32(param2, P2_MONITOR_SIZE)
    old_mon_runaddr = u32(param2, P2_MONITOR_RUNADDR)

    loader_loadaddr = u32(param2, P2_LOADER_2ND_LOADADDR)
    loader = b""
    if loader_loadaddr:
        magic = fip[loader_loadaddr + LDR_HDR_MAGIC:loader_loadaddr + LDR_HDR_MAGIC + 4]
        if magic not in LOADER_2ND_MAGICS:
            sys.exit("error: LOADER_2ND magic %r unrecognised" % magic)
        loader_size = u32(fip, loader_loadaddr + LDR_HDR_SIZE)
        loader = extract(fip, loader_loadaddr, loader_size, "LOADER_2ND")

    runaddr = args.runaddr if args.runaddr is not None else DEFAULT_MONITOR_RUNADDR

    # --- Rebuild from param2 onwards, mirroring fiptool's body2 layout:
    #     param2, [DDR_PARAM], [BLCP_2ND], MONITOR, [LOADER_2ND], each
    #     512-aligned, with LOADADDR = offset within the image. ---
    out = bytearray(fip1)
    assert len(out) == param2_loadaddr
    out += b"\0" * PARAM2_SIZE  # param2 placeholder, filled in below

    def place(payload, off_loadaddr, off_size=None, off_cksum=None):
        nonlocal out
        out = bytearray(pad(out))
        body = pad(payload)
        set_u32(param2, off_loadaddr, len(out))
        if off_size is not None:
            set_u32(param2, off_size, len(body))
        if off_cksum is not None:
            param2[off_cksum:off_cksum + 4] = image_crc(body)
        out += body

    if ddr:
        place(ddr, P2_DDR_PARAM_LOADADDR, P2_DDR_PARAM_SIZE, P2_DDR_PARAM_CKSUM)
    if blcp2:
        place(blcp2, P2_BLCP_2ND_LOADADDR, P2_BLCP_2ND_SIZE, P2_BLCP_2ND_CKSUM)

    # MONITOR: our new payload.
    place(new_monitor, P2_MONITOR_LOADADDR, P2_MONITOR_SIZE, P2_MONITOR_CKSUM)
    set_u32(param2, P2_MONITOR_RUNADDR, runaddr)

    if loader:
        # Keep the (already compressed) loader bytes untouched, just relocate.
        place(loader, P2_LOADER_2ND_LOADADDR)

    # Refresh the param2 checksum (covers everything after the cksum field)
    # and splice param2 back into the image.
    param2[P2_CKSUM:P2_CKSUM + 4] = b"\0\0\0\0"
    param2[P2_CKSUM:P2_CKSUM + 4] = image_crc(param2[P2_CKSUM + 4:])
    out[param2_loadaddr:param2_loadaddr + PARAM2_SIZE] = param2

    with open(args.output_fip, "wb") as f:
        f.write(out)

    print("MONITOR: %#x -> %#x bytes, runaddr %#x -> %#x%s"
          % (old_mon_size, len(pad(new_monitor)), old_mon_runaddr, runaddr,
             " (default)" if args.runaddr is None else " (--runaddr)"))
    print("wrote %s (%d bytes)" % (args.output_fip, len(out)))


if __name__ == "__main__":
    main()
