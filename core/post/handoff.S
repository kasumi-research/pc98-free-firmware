/*
 * ITF -> BIOS hand-off.
 *
 * Selecting BANK7 replaces the F8000-FFFFF window, which is where this
 * code is executing from: the instant the bank flips, the ITF is gone.
 * So the port write and the far jump have to run from somewhere that
 * does not move.  This copies a short stub into the data segment (RAM)
 * and transfers to it; the stub flips the bank and jumps to the BIOS.
 *
 * Bank control, measured (qemu hw/i386/pc98/pc98-lle.c): port 043Dh
 * bit1 clear selects the bank chosen by 043Fh, bit1 SET selects the
 * normal BANK7.  Reset state is BANK4, so one write of 02h is the whole
 * switch.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/asm.h>
#include <pc98/layout.h>

        .code16
        .section .text, "ax"

/* void itf_handoff(void) — does not return. */
        .globl itf_handoff
itf_handoff:
        cli
        cld
        /* DS is already SEG_ITF_DATA; copy the stub there from ROM */
        movw    $tramp, %si
        movw    $ITF_TRAMP_OFF, %di
        movw    $tramp_end - tramp, %cx
1:      movb    %cs:(%si), %al
        movb    %al, (%di)
        incw    %si
        incw    %di
        loop    1b
        ljmp    $SEG_ITF_DATA, $ITF_TRAMP_OFF

/*
 * Runs from RAM.  Position-independent by construction: immediates and
 * one absolute far jump, no PC-relative references.
 */
tramp:
        movw    $PORT_BANK_GATE, %dx
        movb    $0x02, %al              /* bit1 set: map the normal BANK7 */
        outb    %al, %dx
        ljmp    $SEG_BIOS_WARM, $0
tramp_end:
