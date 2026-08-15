#!/usr/bin/env python3
"""
Assemble bank blobs into a 256 KiB PC-98 firmware flash image.

Eight 32 KiB banks, 0xFF-filled where nothing is placed.  A bank may
receive its blob at a nonzero offset, because a bank's link base is not
always its bank base: BANK7's BIOS is linked at 0 (CS = FD80) but lives
at bank offset 0x5800.

The reset vectors are checked, not assumed.  They are the two byte
sequences the machine cannot boot without, and both are placed by
assembler mnemonics whose encoding we would otherwise be trusting
blindly.

SPDX-License-Identifier: MIT
"""

import argparse
import sys

BANK_SIZE = 0x8000
NBANKS = 8
FLASH_SIZE = BANK_SIZE * NBANKS
RESETVEC_OFF = 0x7FF0

# (bank, offset, expected bytes, what it is)
REQUIRED_VECTORS = [
    (4, RESETVEC_OFF, bytes([0xEA, 0x00, 0x40, 0x00, 0xF8]),
     "BANK4 power-on reset vector: jmp f800:4000 (ITF entry)"),
    (7, RESETVEC_OFF, bytes([0xEA, 0x00, 0x00, 0x80, 0xFD]),
     "BANK7 warm-boot vector: jmp fd80:0000 (BIOS entry)"),
]


def mps_checksums(image):
    """Fill in the MPS 1.4 checksums.

    Both structures are checksummed by "every byte sums to zero mod
    256", which an assembler cannot express -- it would have to add up
    bytes it has not emitted yet.  They are static tables, so the sum
    belongs at build time rather than in POST, where it would need a
    writable copy of a ROM structure to put the answer in.

    The floating pointer is found by its signature and the
    configuration table through the pointer's own physical address,
    which is how the OS finds them too: if this pass cannot follow the
    trail, neither can Windows.  Returns a description for the build
    log, or None when the image carries no MPS tables (the Xa7).

    The search is confined to BANK7, the only bank mapped when an OS
    scans for the pointer.  A signature anywhere else would be a false
    positive, and patching a checksum over it would corrupt whatever it
    really was.
    """
    lo, hi = 7 * BANK_SIZE, 8 * BANK_SIZE
    at = image.find(b"_MP_", lo, hi)
    if at < 0:
        return None
    if image.find(b"_MP_", at + 4, hi) >= 0:
        sys.exit("mkflash: more than one _MP_ signature in BANK7")

    image[at + 10] = 0
    image[at + 10] = (-sum(image[at:at + 16])) & 0xFF

    # the pointer's target is physical; only F8000-FFFFF is in this image
    phys = int.from_bytes(image[at + 4:at + 8], "little")
    off = phys - 0xF8000
    cfg = lo + off
    if not 0 <= off < BANK_SIZE or bytes(image[cfg:cfg + 4]) != b"PCMP":
        sys.exit(f"mkflash: _MP_ at {at:#x} points at {phys:#x}, "
                 f"where there is no PCMP table")
    length = int.from_bytes(image[cfg + 4:cfg + 6], "little")
    if not 0 < length <= BANK_SIZE - off:
        sys.exit(f"mkflash: PCMP at {phys:#x} claims {length} bytes, "
                 f"which runs off the end of BANK7")
    image[cfg + 7] = 0
    image[cfg + 7] = (-sum(image[cfg:cfg + length])) & 0xFF
    return (f"  MPS: _MP_ at {0xF8000 + at - lo:#07X}, "
            f"PCMP at {phys:#07X}, {length} bytes")


def pnp_checksum(image):
    """Fill in the $PnP installation structure's checksum.

    Same reason as the MPS tables: the byte sum has to come out zero and
    an assembler cannot add up bytes it has not emitted yet.  Windows
    scans F0000-FFFFF on 16-byte boundaries and takes the first "$PnP"
    whose `length` bytes sum to zero, so a structure with the wrong sum
    is invisible -- exactly as invisible as no structure at all, and
    silently so.  Returns a description for the build log.

    Confined to BANK6, the bank mapped at F0000-F7FFF.  That is the only
    window the ring-0 scanner covers -- measured, it reads one dword per
    16-byte boundary across F0000-F7FFF and no further -- so a structure
    anywhere else is never found (banks/bank6.ld.in).
    """
    lo, hi = 6 * BANK_SIZE, 7 * BANK_SIZE
    at = image.find(b"$PnP", lo, hi)
    if at < 0:
        return None
    if image.find(b"$PnP", at + 4, hi) >= 0:
        sys.exit("mkflash: more than one $PnP signature in BANK6")
    if (at - lo) % 16:
        sys.exit(f"mkflash: $PnP at {0xF0000 + at - lo:#07X} is not "
                 f"16-byte aligned, so the OS scan will step over it")

    length = image[at + 5]
    if length < 0x21:
        sys.exit(f"mkflash: $PnP claims {length} bytes, too short for "
                 f"the 1.0 structure")
    image[at + 8] = 0
    image[at + 8] = (-sum(image[at:at + length])) & 0xFF
    return f"  PnP: $PnP at {0xF0000 + at - lo:#07X}, {length} bytes"


def bios32_checksum(image):
    """Fill in the BIOS32 service directory's checksum.

    Third structure with the same problem as the MPS tables and $PnP,
    and the same fix.  An OS looking for the firmware's PCI BIOS scans
    F0000-FFFFF on 16-byte boundaries for "_32_" and takes the first one
    whose bytes sum to zero, so a wrong sum makes the directory as
    invisible as no directory at all -- and Windows' display driver
    responds to "no PCI BIOS" by probing and setting the display mode
    itself, which is the pre-shell settings dialog.

    Confined to BANK6 for the same reason the $PnP pass is: that is the
    window the scan actually covers.
    """
    lo, hi = 6 * BANK_SIZE, 7 * BANK_SIZE
    at = image.find(b"_32_", lo, hi)
    if at < 0:
        return None
    if image.find(b"_32_", at + 4, hi) >= 0:
        sys.exit("mkflash: more than one _32_ signature in BANK6")
    if (at - lo) % 16:
        sys.exit(f"mkflash: _32_ at {0xF0000 + at - lo:#07X} is not "
                 f"16-byte aligned, so the OS scan will step over it")

    paragraphs = image[at + 9]
    if paragraphs != 1:
        sys.exit(f"mkflash: _32_ claims {paragraphs} paragraphs; the "
                 f"structure is defined as exactly one")

    length = paragraphs * 16
    image[at + 10] = 0
    image[at + 10] = (-sum(image[at:at + length])) & 0xFF

    entry = int.from_bytes(image[at + 4:at + 8], "little")
    if not 0xF0000 <= entry < 0x100000:
        sys.exit(f"mkflash: _32_ entry {entry:#07X} is outside "
                 f"F0000-FFFFF, so it is not mapped when an OS calls it")
    return (f"  BIOS32: _32_ at {0xF0000 + at - lo:#07X}, "
            f"entry {entry:#07X}")


def parse_placement(spec):
    """N=FILE[@OFFSET] -> (bank, offset, path)"""
    if "=" not in spec:
        raise argparse.ArgumentTypeError(
            f"{spec!r}: expected N=FILE[@OFFSET]")
    bank_s, rest = spec.split("=", 1)
    off = 0
    if "@" in rest:
        rest, off_s = rest.rsplit("@", 1)
        off = int(off_s, 0)
    bank = int(bank_s, 0)
    if not 0 <= bank < NBANKS:
        raise argparse.ArgumentTypeError(f"bank {bank} out of range 0..7")
    return bank, off, rest


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--out", required=True, help="output image")
    ap.add_argument("--bank", action="append", default=[], type=parse_placement,
                    metavar="N=FILE[@OFFSET]",
                    help="place FILE in bank N, optionally at OFFSET")
    ap.add_argument("--no-verify", action="store_true",
                    help="skip the reset-vector check (do not use)")
    args = ap.parse_args()

    image = bytearray(b"\xff" * FLASH_SIZE)
    used = [0] * NBANKS
    placed = []

    for bank, off, path in args.bank:
        try:
            with open(path, "rb") as f:
                blob = f.read()
        except OSError as e:
            sys.exit(f"mkflash: {e}")

        if off + len(blob) > BANK_SIZE:
            sys.exit(f"mkflash: {path} ({len(blob)} bytes) at offset "
                     f"{off:#x} overruns bank {bank} "
                     f"by {off + len(blob) - BANK_SIZE} bytes")

        base = bank * BANK_SIZE + off
        # Refuse to silently overlay an earlier placement.
        region = image[base:base + len(blob)]
        if region != b"\xff" * len(blob):
            sys.exit(f"mkflash: {path} would overwrite already-placed "
                     f"bytes in bank {bank} at offset {off:#x}")

        image[base:base + len(blob)] = blob
        used[bank] += len(blob)
        placed.append((bank, off, len(blob), path))

    mps = mps_checksums(image)
    pnp = pnp_checksum(image)
    b32 = bios32_checksum(image)

    if not args.no_verify:
        for bank, off, want, what in REQUIRED_VECTORS:
            base = bank * BANK_SIZE + off
            got = bytes(image[base:base + len(want)])
            if got != want:
                sys.exit(
                    f"mkflash: {what}\n"
                    f"  at bank {bank} offset {off:#06x} "
                    f"(physical {0xF8000 + off:#07X} when banked in)\n"
                    f"  expected {want.hex(' ')}\n"
                    f"  got      {got.hex(' ')}\n"
                    f"  The machine cannot boot without this. If the "
                    f"bank is intentionally empty, place its entry stub.")

    with open(args.out, "wb") as f:
        f.write(image)

    print(f"{args.out}: {FLASH_SIZE} bytes")
    for bank, off, size, path in sorted(placed):
        pct = 100.0 * size / BANK_SIZE
        print(f"  BANK{bank} +{off:#06x}  {size:6d} bytes "
              f"({pct:4.1f}% of bank)  {path}")
    if mps:
        print(mps)
    if pnp:
        print(pnp)
    if b32:
        print(b32)
    empty = [b for b in range(NBANKS) if not used[b]]
    if empty:
        print(f"  empty (0xFF): {', '.join('BANK%d' % b for b in empty)}")


if __name__ == "__main__":
    main()
