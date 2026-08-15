/*
 * PC-9821Xa7 — stackless early init.
 *
 * Runs from ROM, no stack, DS = CS.  See include/pc98/asm.h.
 *
 * The write sequence below is the one the NEC ROM performs on this
 * machine before it does anything conditional, observed on the port
 * bus.  The ports are identified; the values are the machine's own and
 * are not independently derived:
 *
 *   0FACh  display relay.  bit0 = RGB-IN pass-through (a PCI card's
 *          output looped back by cable), bit1 = built-in accelerator.
 *          Cleared here: no card driving the CRT yet.
 *   006Ah  display mode register.  Seven writes; this is the port that
 *          also owns the A0000-A3FFF aperture handover, so the order
 *          matters and the sequence is kept as observed.
 *
 * Deliberately STOPS before the display-PLL programming at 18F0h/18F2h
 * that the NEC ROM does next, because on this model that programming is
 * not self-contained and what completes it is not yet understood --
 * programming half of a PLL sequence is worse than programming none of
 * it.  TODO: pick it up once the display path is worked out.
 *
 * TODO: Wildcat DRAM controller init — sizing, mapping, and above
 * all REFRESH — belongs here, before phase 2 uses the stack.  Under
 * QEMU memory is unconditionally present, so its absence is invisible.
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
        movw    $xa7_early_tbl, %si
        CALLR   bp, io_table_apply
        POSTCODE 0x03
        RETR    sp

        .section .rodata
        .balign 2
xa7_early_tbl:
        .word   8                       /* entry count */
        IOB     0x0fac, 0x00            /* display relay off        */
        IOB     0x006a, 0x07            /* display mode sequence,   */
        IOB     0x006a, 0x44            /* in the order the NEC ROM */
        IOB     0x006a, 0x86            /* issues it.  The RvII26's */
        IOB     0x006a, 0x8e            /* equivalent is only four  */
        IOB     0x006a, 0x8a            /* writes, so this really   */
        IOB     0x006a, 0x88            /* is per-model.            */
        IOB     0x006a, 0x06
