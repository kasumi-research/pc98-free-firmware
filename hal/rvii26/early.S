/*
 * PC-9821RvII26 — stackless early init.
 *
 * Runs from ROM, no stack, DS = CS.  See include/pc98/asm.h.
 *
 * The write sequence the NEC ROM performs on this machine before it
 * does anything conditional, observed on the port bus.  It is NOT the
 * same as the Xa7's — this machine writes two extra ports first and
 * uses a four-write 006Ah sequence where the Xa7 uses seven.  That
 * divergence is the reason early init is per-model HAL code.
 *
 *   00F6h  CPU control.  Value 03h = **A20 disable** (02h would
 *          enable it), confirmed against the emulator's model.  The
 *          firmware wants a clean 1 MiB-wrapping real mode at
 *          power-on.  A useful corroboration that keeping our early
 *          stack below 1 MiB is right.
 *   0FACh  display relay: bit0 RGB-IN pass-through, bit1 built-in
 *          accelerator.  Cleared — nothing is driving the CRT yet.
 *   0FAAh  word-wide, RvII26 only.  Display path; exact meaning not
 *          established.
 *   006Ah  display mode register.  Also owns the A0000-A3FFF aperture
 *          handover, so order is preserved as observed.
 *
 * The NEC ROM inserts a settle delay between the 0FAAh and 006Ah
 * writes; io_table_apply's IODELAY after every write covers that.
 *
 * Stops before the 18F0h/18F2h display-PLL programming the NEC ROM
 * does next.  Unlike the Xa7's, this machine's is self-contained and
 * ends in a bounded settle loop, so it can be taken as soon as we want
 * it — the loop just needs a table form.  TODO: take it, together
 * with the Xa7's, once the display path is worked out.
 *
 * TODO: Champion CNB20-LE DRAM init — sizing, mapping, REFRESH —
 * belongs here.  The emulator models the row sizing at Champion config
 * 0x90 with 0x70 as the row-boundary byte, which is where to start.
 * MTRR setup and the microcode update run before anything else on this
 * machine, and both are stackless work.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/asm.h>

        .code16
        .section .text, "ax"

/*
 *   link    %sp
 *   clobber %ax %bx %cx %dx %si %bp
 */
        .globl hal_early_init
hal_early_init:
        POSTCODE 0x02
        movw    $rvii26_early_tbl, %si
        CALLR   bp, io_table_apply
        POSTCODE 0x03
        RETR    sp

        .section .rodata
        .balign 2
rvii26_early_tbl:
        .word   7                       /* entry count */
        IOB     0x00f6, 0x03            /* A20 off */
        IOB     0x0fac, 0x00            /* display relay off */
        IOW     0x0faa, 0x0003          /* word-wide; RvII26 only */
        IOB     0x006a, 0x07            /* display mode sequence,  */
        IOB     0x006a, 0x44            /* in the order the NEC    */
        IOB     0x006a, 0x8e            /* ROM issues it           */
        IOB     0x006a, 0x06
