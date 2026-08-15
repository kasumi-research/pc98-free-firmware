/*
 * Hand control to the boot sector.
 *
 * The register state is the IPL contract, and it is measured, not
 * invented: a boot sector loaded by the NEC ROM on an Xa7 is entered
 * with AX=0080 BX=0400 CX=0008 DX=0000 SI=09B1 DI=0008 BP=0000 DS=0000
 * ES=1FC0 SS=0020 SP=01D6 and IF set -- test/wadump.asm is a boot
 * sector that reports exactly this, and printed those values.  AL is
 * the DA/UA of the device we booted from, which is the one field a boot
 * sector reliably uses; the rest are reproduced because a sector that
 * reads any of them would otherwise see garbage.
 *
 * SS:SP lands in the tail of the IVT, the same scratch the cold path
 * was already using.
 *
 * SPDX-License-Identifier: MIT
 */

        .code16
        .section .text, "ax"

/* void boot_enter(u16 daua) -- does not return */
        .globl boot_enter
boot_enter:
        movw    %sp, %bp
        movw    4(%bp), %ax             /* AL = DA/UA */
        cli
        cld
        movw    $0x1fc0, %dx
        movw    %dx, %es
        xorw    %dx, %dx
        movw    %dx, %ds
        movw    $0x0400, %bx
        movw    $0x0008, %cx
        movw    $0x09b1, %si
        movw    $0x0008, %di
        xorw    %bp, %bp
        movw    $0x0020, %dx
        movw    %dx, %ss                /* mov-to-SS masks interrupts   */
        movw    $0x01d6, %sp            /* for one instruction          */
        xorw    %dx, %dx
        sti
        ljmp    $0x1fc0, $0x0000
