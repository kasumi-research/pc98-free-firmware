# Free PC-9821 firmware

Out-of-tree replacement for the NEC PC-9821 firmware flash, built as a
real ROM image.

**Current goal is the emulator only**: pass the QEMU PC-98 machine's
self-tests and boot Windows 98 on `pc98-xa7` and Windows 2000 on
`pc98-rvii26`.
Running on real hardware is a long-term aspiration and an explicit
non-goal for now — but the firmware is written as if it will run there
eventually, so nothing may depend on QEMU-specific behaviour.

Targets: PC-9821Xa7, PC-9821RvII26, later PC-9801BA3.

This file is the map of the machine.  `notes.md` is the working record
that goes with it: what was measured, what was tried and rejected, and
which gaps are decisions rather than oversights.

## Building

```sh
make MODEL=xa7          # -> build/xa7/pc98-free-xa7.bin
make MODEL=rvii26       # -> build/rvii26/pc98-free-rvii26.bin
make every              # both
make font               # -> build/pc98-free-font.rom
make wadump             # -> build/wadump.bin, the work-area oracle
make clean
```

Needs stock host gcc, binutils, python3 and (for `wadump`) nasm.  `make
font` additionally fetches two freely licensed font sources over the
network; both are pinned and checked against a recorded SHA-256.

Of the eight banks the image is built from, BANK4 holds the ITF, BANK6
the two structures an OS scans for (`_32_` at F4C40 and `$PnP` at
F51B0, at the reference's own addresses) and BANK7 the runtime BIOS.
Banks 0, 1, 2, 3 and 5 are still 0xFF.

## License

MIT; see `LICENSE`.  Every source file carries an SPDX tag.

Three files additionally carry BSD 3-Clause material derived from Neko
Project 21/W — `core/bios/crt.c`, `core/lib/hwinit.c` and
`include/pc98/gdc.h`, tagged `MIT AND BSD-3-Clause`.  `NOTICE` says
exactly what came from where and reproduces the licence.  **That
licence binds binary redistribution too**: ship `NOTICE` with a built
ROM image, not just with the source.

The character generator is a separate matter.  `tools/mkfont.py` is MIT
like the rest of the tree, but the ROM it *produces* embeds GNU Unifont
JP (OFL-1.1 / GPLv2+ with the font embedding exception) and IchigoJam
(MIT) glyph data, and those terms travel with the built font whatever
this repository's licence says.  Neither font is vendored here.

## 1. The flash: 256 KiB, eight 32 KiB banks

Sections 1 to 4 describe the **NEC** firmware, not this tree's output —
they are the map the replacement is written against.  Everything in
them was measured from Xa7 and RvII26 flash images and from booted
machines running the NEC ROM, not recalled.  Those images are not
redistributable and are not in this tree.

Banked at F8000-FFFFF via port 043Dh bit1 (gate) + 043Fh (code E0h..EEh
= BANK0..7).  Power-on state maps BANK4; its reset vector at FFFF0 is
`ea 00 40 00 f8` = `jmp f800:4000` (the ITF entry).  BANK7's FFFF0 is
`ea 00 00 80 fd` = `jmp fd80:0000` (the warm-boot entry).

| Bank | Xa7 | RvII26 | Notes |
|---|---|---|---|
| 0 | PCI/PnP BIOS | PCI/PnP BIOS | two `PC98` option-ROM headers; surfaced at D8000 when host-bridge 0x67 bits 5:4 = 00 |
| 1 | KBCRT module (standalone copy) | **CPU/MP init**: MTRR setup, microcode update, APIC/MPS bring-up; carries `_MP_` + `PCMP` tables | **role differs per model** |
| 2 | MMDUMP | MMDUMP | NEC crash memory-dump utility (`MMDUMP  DAT`, `DUMP0000DAT`) |
| 3 | IDE BIOS + IPL + setup menu | SCSI/IDE BIOS + IPL + setup menu | `IPL1` signature; head of this bank is normally what the D8000 C-bus window shows (TODO: confirm — may conflict with bank 0's condition above) |
| 4 | **ITF** | **ITF** | POST, error messages, setup menu, SMM install, password, SDIP |
| 5 | E8000 third | E8000 third | **byte-identical between the two machines** |
| 6 | F0000 third | F0000 third | ~19% differ; holds `$PnP` @F51B0 and `_32_` (BIOS32 service directory) @F4C40, and the KBCRT module @F3200 |
| 7 | F8000 third | F8000 third | ~30% differ; holds the F8E80 capability block, which POST patches in exactly one byte (F8E90, the IDE equipment bitmap) and which **must not read 0xFF** |

Banks 5/6/7 concatenated match a live dump of E8000-FFFFF off the
running Xa7 exactly, except for one byte that POST patches at runtime.

Bank 1 on the Xa7 is a standalone copy of the KBCRT module that is also
linked into bank 6 at F3200 (`KBCRT X47 891105`); on the RvII26 bank 1
is something else entirely.  **Bank roles are per-model, not fixed.**

## 2. The 96 KiB runtime BIOS (E8000-FFFFF = banks 5+6+7)

BIOS and N88-BASIC(86) are **interleaved**, not cleanly split — F8000
is DMA-controller BIOS code (ports 4A0h/4ACh) while BASIC's error
tables sit at F8F00 and FC300.  Do not assume the textbook map.

Contents identified so far:

* **N88-BASIC(86) v2.0** — interpreter, error tables, disk-BASIC error
  tables, keyword tables.  Copyright 1983 NEC/Microsoft.  About half of
  this 96 KiB region.  *Not needed to boot DOS/Windows.*
* **BIOS proper** — DMA, keyboard scancode tables (FE32C-FE8xx, four
  layouts), parity-error handlers (FE080), warm-boot entry at FD800.
* **`$PnP` installation structure** (F51B0) + **BIOS32 service
  directory `_32_`** (F4C40) — PnP BIOS and 32-bit PCI BIOS services.
* **KBCRT** module (F3200).

Model-difference map across E8000-FFFFF, 256-byte granularity:

```
E8000-F30FF same     F3100-F31FF DIFFER   F3200-F50FF same
F5100-F59FF DIFFER   F5A00-F5AFF same     F5B00-F5BFF DIFFER
F5C00-F65FF same     F6600-F6FFF DIFFER   F7000-F70FF same
F7100-F71FF DIFFER   F7200-F75FF same     F7600-F76FF DIFFER
F7700-F77FF same     F7800-F78FF DIFFER   F7900-F8DFF same
F8E00-F8EFF DIFFER   F8F00-F97FF same     F9800-F9AFF DIFFER
F9B00-FD7FF same     FD800-FDCFF DIFFER   FDD00-FDDFF same
FDE00-FDFFF DIFFER   FE000-FE1FF same     FE200-FE3FF DIFFER
FE400-FE5FF same     FE600-FE6FF DIFFER   FE700-FE7FF same
FE800-FFFFF DIFFER
```

~85% is model-independent.  The machine-specific code concentrates in
F5100-F79xx and FD800-FFFFF.  That is the natural shared-core / per-model
seam.

## 3. The ITF (bank 4) — what POST actually does

From what it reports as it runs, in rough order: MTRR init, processor
microcode update, CG/kanji ROM checksum, text+graphics VRAM test,
extended GVRAM, timer, DMA, timer interrupt, cache RAM (+ 2nd cache),
memory sizing and parity, SIMM configuration, protected-mode entry,
A20 line, MICON (the power/reset microcontroller), **SMRAM/SMM
install**, sound, I/O lock (1st CCU / 2nd CCU / printer / FD / PD),
password, software DIP switches, and the setup menu.  Plus a
"ROM DATA disk" flash-update path (`Insert the ROM DATA disk`).

It also puts up a banner naming the machine family and the ROM BIOS
revision, and reports the CPU identification and detected clock.

Not visible in strings but known from the emulator work: the ITF
relocates SMBASE to 0x98000 and installs the **INT 1Fh AH=9Ah power
management API served out of SMM**, and it populates + write-protects
the E8000-FFFFF shadow RAM via host-bridge attribute bytes
0x68/0x69/0x6C/0x71.

## 4. ROMs outside the flash

| Image | Size | What |
|---|---|---|
| character generator | 288 KiB (np2 format) | **Character generator**: ANK 8x16/8x8 + JIS level 1 & 2 kanji.  This is *font data*, not code — it cannot be "reimplemented", only substituted with a free JIS font. |
| SCSI card option ROM | 32 KiB | Adaptec AIC-7860 card option ROM (`55 AA 40`, `PC98` header at +0x1C).  Shrinks itself to a 14 KiB resident at DC000 (`55 AA 1C`) — the same bytes, which is why DC000 looked like a phantom module. |
| SCSI card SEEPROM | 128 B | AIC-7860 SEEPROM contents (host ID, sync rates, boot device). |
| Trident WAB ROM | 64 KiB | Trident TGUI9660 window accelerator board ROM (Xa7 option). |
| — | — | Matrox MGA-2064W has **no option ROM**; the NT miniport never reads one. |

Gaiji RAM and the SDIP (software DIP switches) are battery-backed
*state*, not ROM, but the firmware owns their format; the gaiji area
also holds hidden firmware config cells (`ACFG`).

