/*
 * Issue an INT 1Bh read from the BIOS's own cold path.
 *
 * The boot walk cannot call the disk layer directly once a module owns
 * the class: on the RvII26 the AIC-7860's expansion ROM is what knows
 * how to reach the disk, and the only way in is the vector.  Going
 * through INT 1Bh also means the walk is identical for a device we
 * drive ourselves and one a module drives -- which is what the NEC ROM
 * does, and what makes a boot from either look the same to the sector
 * we hand control to.
 *
 * SPDX-License-Identifier: MIT
 */

        .code16
        .section .text, "ax"

/*
 * u16 disk_boot_read(u16 daua, u16 seg, u16 bytes)
 *
 * Reads from sector 0 (CX = cylinder 0, DX = head 0 / sector 0, which
 * is also linear sector 0) into seg:0000.  Returns the AH status the
 * module left, 0 on success.
 */
        .globl disk_boot_read
disk_boot_read:
        pushw   %bp
        movw    %sp, %bp
        pushw   %bx
        pushw   %cx
        pushw   %dx
        pushw   %si
        pushw   %di
        pushw   %ds
        pushw   %es

        movw    6(%bp), %ax
        movb    $0x06, %ah              /* read */
        movw    10(%bp), %cx
        movw    %cx, %es
        movw    14(%bp), %bx            /* byte count */
        xorw    %cx, %cx
        xorw    %dx, %dx
        pushw   %bp
        xorw    %bp, %bp                /* ES:BP is the buffer */
        int     $0x1b
        popw    %bp

        movb    %ah, %al
        movzbl  %al, %eax

        popw    %es
        popw    %ds
        popw    %di
        popw    %si
        popw    %dx
        popw    %cx
        popw    %bx
        popw    %bp
        retl
