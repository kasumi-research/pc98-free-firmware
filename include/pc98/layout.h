/*
 * Firmware address layout — shared by the C code, entry.S and the
 * linker scripts.  Only bare #defines here — the bank linker scripts
 * include it too, and they run through cpp, not a C compiler.
 *
 * Flash: 256 KiB, eight 32 KiB banks, the selected one visible at
 * F8000-FFFFF (port 043Dh bit1 gates, 043Fh selects, codes E0h..EEh =
 * BANK0..7).  Power-on maps BANK4, whose reset vector is the ITF entry.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_LAYOUT_H
#define PC98_LAYOUT_H

#define BANK_SIZE           0x8000
#define FLASH_SIZE          0x40000

/* Where the F8000 window lives, as a real-mode segment. */
#define SEG_ROM_WINDOW      0xf800

/*
 * BANK4 (the ITF).  The reset vector at bank offset 0x7FF0 is
 * `jmp f800:4000`, so the ITF is linked at 0x4000 and bank offset ==
 * segment offset (CS = F800).  Measured from the real firmware;
 * tools/mkflash.py verifies the bytes.
 */
#define ITF_ENTRY_OFF       0x4000
#define RESETVEC_OFF        0x7ff0

/*
 * BANK6 — the F0000-F7FFF window, mapped unconditionally.  Holds the
 * structures an OS scans for, at the addresses the reference uses,
 * because the ring-0 scanner only covers this window (bank6.ld.in).
 */
#define SEG_BANK6_WINDOW    0xf000
#define BANK6_BIOS32_OFF    0x4c40
#define BANK6_PNP_OFF       0x51b0

/*
 * The $PnP entry point is not ours to choose.  Windows 98's BIOS.VXD
 * recognises PC-98 PnP BIOS entries from a hardcoded pair and ignores
 * every other address:
 *
 *   c1806a6d  cmpl $0xd8000,-0xc(%ebp)   PM code base 000D8000
 *   c1806a76  cmpw $0x3a,-0x8(%ebp)        with entry offset 003A
 *   c1806a7d  cmpl $0xf5600,-0xc(%ebp)   PM code base 000F5600
 *   c1806a86  cmp  %si,-0x8(%ebp)          with entry offset 0024
 *
 * Only those two reach the path that enumerates the bus.  The first is
 * the NEC firmware's own D800:003A, inside the paged D8000 window; the
 * second lands at F5624, in this bank, which we can serve directly.
 */
#define SEG_PNP_ENTRY       0xf560
#define PNP_ENTRY_OFF       0x0024
#define BANK6_PNPENTRY_OFF  ((SEG_PNP_ENTRY << 4) - 0xf0000 + PNP_ENTRY_OFF)

/*
 * BANK7 (the runtime BIOS).
 *
 * fd80:0000 is a PUBLISHED entry -- the reset vector at FFFF0 jumps
 * there and so does every protected-mode return -- but it is only an
 * entry, not a base.  Linking the BIOS at FD80 caps it at the 10 KiB
 * between FD800 and FFFF0, and it outgrew that.  So the BIOS is linked
 * at CS = F800 and spans the whole bank, and fd80:0000 holds a
 * three-byte trampoline into it.  The original firmware makes the same
 * move in the other direction, far-calling a module layer down in
 * F5000-F7FFF.
 *
 * Bank offsets and segment offsets are the same number here, because
 * CS = F800 is where the bank window starts.
 */
#define SEG_BIOS            0xf800      /* the BIOS's own CS          */
#define SEG_BIOS_WARM       0xfd80      /* the published entry point  */
#define BIOS_CAPS_OFF       0x0e80      /* F8E80 capability block     */
#define BIOS_TEXT_OFF       0x1200
#define BIOS_WARM_OFF       0x5800      /* = fd80:0000                */
#define BIOS_RESETVEC_OFF   RESETVEC_OFF

/*
 * The ITF runs FROM ROM, like the real firmware does.  Only its stack
 * and .bss live in RAM, in this segment (DS = SS), while CS stays
 * F800.
 *
 * Consequence, and the single most important rule in the codebase:
 * .rodata is at CS and is therefore NOT reachable through DS.  Every
 * read of our own image — string literals, tables, constants — goes
 * through the rom_* accessors in romdata.h.  A plain C dereference of
 * a pointer into our image reads the data segment instead and returns
 * garbage.
 *
 * 0x1000 = physical 0x10000-0x1FFFF: below 1 MiB, so nothing in early
 * POST depends on A20; clear of the IVT, the work area and the boot
 * sector.  Because only the stack and .bss are here, POST's memory
 * test can relocate a few KiB rather than the whole firmware — which
 * is exactly what running from ROM buys us.
 */
#define SEG_ITF_DATA        0x1000
#define ITF_TRAMP_OFF       0x0080      /* scratch: the hand-off stub  */
#define ITF_BSS_BASE        0x0100      /* offset in SEG_ITF_DATA; not 0 */
#define ITF_STACK_TOP       0xfffc      /* top of SEG_ITF_DATA, 4-aligned */

/*
 * The BIOS has NO data segment of its own and NO private stack.
 *
 * That is measured, not a compromise: a PC-98 INT 18h handler runs on
 * the caller's stack with DS = 0 (see include/pc98/bregs.h), and there
 * is nowhere to put a private stack anyway --
 * the work area below 0x0600 is fully allocated and IO.SYS starts AT
 * 0x0600 on a booted Win98.  BIOS state lives in the work area (wa.h),
 * BIOS constants in ROM (romdata.h).
 *
 * The one exception is the cold path, which runs before any OS exists
 * and needs a stack to reach C.  It uses the tail of the IVT, growing
 * down from 0x0400 -- exactly where the NEC ROM puts its own (it hands
 * a boot sector control with SS:SP = 0020:01D6, measured).  Vectors
 * 80h-FFh are not live until the OS installs them.
 */
#define BIOS_BOOT_SP        0x0400

/*
 * The stack an expansion ROM runs on (core/boot/optcall.S).  Segment
 * 0x2000 = physical 0x20000-0x2FFFF: above the boot sector's landing
 * area at 0x1FC00, below anything DOS or NTLDR loads, and unused for
 * the whole of the cold path.  The BIOS's own stack -- the tail of the
 * IVT -- is far too small to hand to third-party code.
 */
#define OPTROM_STACK_SEG    0x2000
#define OPTROM_STACK_SP     0xfff0

/* Text VRAM: codes at A000:0000, attributes at A000:2000. */
#define SEG_TVRAM           0xa000
#define TVRAM_ATTR          0x2000
#define TXTATR_ST           0x01        /* bit0: cell is displayed */
#define TXTATR_WHITE        0xe1        /* G|R|B|ST -- the normal attribute */
#define TEXT_COLS           80

#endif
