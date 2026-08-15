/*
 * Far-call an expansion ROM entry point.
 *
 * Two things this does that a plain lcall would not:
 *
 *   - it gives the ROM a stack of its own.  The BIOS cold path runs on
 *     the tail of the IVT (0000:0400 growing down), which leaves a few
 *     hundred bytes -- fine for our own handlers, not something to hand
 *     to third-party code.  The AIC-7860 card BIOS alone opens a 0x50
 *     byte frame in its INT 1Bh entry before it calls anything.
 *   - it sets AX for the ROM's contract and DS/ES to 0 (the work area),
 *     which is the state the NEC ROM calls option ROMs in.
 *
 * The old SS:SP is parked on the NEW stack rather than in registers: an
 * option ROM may return with any register content, but a `retf` puts SP
 * back where the call found it, and SS is restored from a literal.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/layout.h>

        .code16
        .section .text, "ax"

/* void optrom_call(u16 seg, u16 off, u16 ax) */
        .globl optrom_call
optrom_call:
        pushw   %bp
        movw    %sp, %bp
        pushal
        pushw   %ds
        pushw   %es
        pushw   %fs
        pushw   %gs

        /* the arguments must be read before SS moves out from under BP */
        movw    6(%bp), %si             /* seg */
        movw    10(%bp), %di            /* off */
        movw    14(%bp), %ax            /* the ROM's AX */
        movw    %ss, %cx
        movw    %sp, %dx

        cli
        movw    $OPTROM_STACK_SEG, %bx
        movw    %bx, %ss                /* mov-to-SS masks one instruction */
        movw    $OPTROM_STACK_SP, %sp
        pushw   %cx                     /* old SS */
        pushw   %dx                     /* old SP */
        pushw   %si                     /* far pointer: segment */
        pushw   %di                     /*              offset  */
        movw    %sp, %bx
        xorw    %cx, %cx
        movw    %cx, %ds
        movw    %cx, %es
        sti
        lcall   *%ss:(%bx)

        cli
        movw    $OPTROM_STACK_SEG, %bx
        movw    %bx, %ss
        movw    $OPTROM_STACK_SP - 4, %sp
        popw    %dx                     /* old SP */
        popw    %cx                     /* old SS */
        movw    %cx, %ss
        movw    %dx, %sp
        sti

        popw    %gs
        popw    %fs
        popw    %es
        popw    %ds
        popal
        popw    %bp
        retl
