/*
 * The $PnP installation structure, and the far-call entry it names.
 *
 * Windows 98 and Windows 2000 both look for this: they scan
 * F0000-FFFFF on 16-byte boundaries for "$PnP" and accept the first
 * structure whose bytes sum to zero.  Without one, Windows 98 falls
 * back to legacy detection, its display driver takes the probe-and-set
 * branch (INT 18h AH=31h/42h/30h/40h instead of AH=80h/81h), and the
 * AH=30h that lands there is recorded as a settings change -- the
 * pre-shell "新しいディスプレイの設定 ... restart" dialog.
 *
 * What the far entry DOES is measured: on the real Xa7 firmware every
 * standard PnP BIOS function from 0 through 9 comes back 0x82
 * (BAD_PARAMETER) -- test/pnpdump.asm calls all ten and printed 0082
 * ten times.  The one function the reference DOES implement is 40h,
 * which pnpdump never swept (it loops fn 0..9), and which is
 * transcribed below.  The services that carry real traffic are the
 * PC-98 spelling, INT 1Fh AH=CEh, in misc.c.
 *
 * PLACEMENT IS LOAD-BEARING.  This structure used to ride along at the
 * end of the image, around FD360, and the OS scan never got that far --
 * so it was never found and the entry was never called.  Measured with
 * paread probes on 16-byte boundaries across E0000-FFFFF: the ring-0
 * scanner reads F0000, F4C40, F8000, FC000 and stops before FD030
 * (scratch/logs/b32c.log).  The reference keeps its own at F51B0.
 * The linker now places .pnp low, next to .bios32, which has the same
 * constraint; after the move the structure is read and the entry is
 * called on every boot.
 *
 * Both NEC flashes carry the structure at flash offset 0x351b0 with
 * the entry at D800:003A -- inside the D8000 option-ROM window, which
 * only exists because their entry has to page that window in before it
 * can reach its code.  Ours is in the BIOS bank, which is always
 * mapped, so it needs no such thunk and can name itself directly.
 *
 * The checksum byte is left zero here and filled in by
 * tools/mkflash.py: an assembler cannot sum bytes it has not emitted
 * yet, and the structure is static, so the sum belongs at build time
 * rather than in POST.
 *
 * BANK6 is placed so that a symbol's value IS its offset from F0000
 * (layout.h), which is what lets the entry below be named as a plain
 * F000:symbol far pointer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/layout.h>

        .code16
        .section .pnp, "ax"

        .balign 16
        .globl pnp_struct
pnp_struct:
        .ascii  "$PnP"
        .byte   0x10                    /* structure revision 1.0     */
        .byte   pnp_struct_end - pnp_struct
        .word   0x0000                  /* control: no event mechanism */
        .byte   0                       /* checksum, filled by mkflash */
        .long   0                       /* event notification flag     */
        .word   PNP_ENTRY_OFF           /* real-mode entry offset      */
        .word   SEG_PNP_ENTRY           /*   ... segment (F560)        */
        .word   PNP_ENTRY_OFF           /* 16-bit protected-mode offset */
        .long   SEG_PNP_ENTRY << 4      /*   ... code segment base      */
#ifdef CONFIG_MODEL_XA7
        .ascii  "PC98"                  /* OEM device identifier       */
#else
        .long   0                       /* the RvII26 leaves it zero   */
#endif
        .word   0x0000                  /* real-mode data segment      */
        .long   0                       /* protected-mode data base    */
pnp_struct_end:

/*
 * The entry itself.  The PnP BIOS calling convention is 16-bit C: the
 * arguments are pushed on the stack, the function number nearest the
 * return address, the caller cleans up, and the status comes back in
 * AX.  0x82 is BAD_PARAMETER.
 *
 * The machine we are copying answers 0x82 to every function EXCEPT
 * 40h, so we do too.  Implemented for faithfulness, NOT because it
 * fixes anything: measured on a live boot, Windows 98 asks this entry
 * for exactly ONE function, 05h, exactly once, and both firmwares
 * answer 0x82 to it.  Function 40h is never called by the OS at all.
 * If you are here hunting the display-detection difference, this is not
 * it -- the PnP BIOS interface behaves identically on both firmwares.
 *
 * Function 40h is Get PnP ISA Configuration Structure, transcribed from
 * the reference's dispatcher at BANK0 lo half 0x726 (reached from the
 * published D800:003A entry, which pushes service index 0x24):
 *
 *   in    fn      = 40h            [bp+6]
 *         buffer  = FAR pointer    [bp+8]
 *         selector= BiosSelector   [bp+12], real mode 0000
 *   out   [buf+0] = 1              structure revision
 *         [buf+1] = high byte of the word at 05B7h  (number of CSNs)
 *         [buf+2] = (low byte of that word << 2) | 3   (read-data port)
 *         [buf+4] = 0
 *         AX      = 0
 *
 * 05B7h is zero on the reference, so the numbers it hands back are
 * trivial -- it is answering at all, not what it answers, that Windows
 * needs.  We read the same byte for the same reason: if this machine
 * ever has PnP ISA cards, POST fills that word in and both firmwares
 * then report the same thing.
 *
 * test/pnpdump.asm swept functions 0 through 9 only (`cmp word [fn],
 * 0x0a`), which is why this gap sat here looking measured.
 */
        .section .pnpentry, "ax"
        .globl pnp_entry
pnp_entry:
        push    %bp
        movw    %sp, %bp
        push    %bx
        push    %ds
        push    %es
        pushf
        cli

        movw    6(%bp), %ax             /* the function number         */
        cmpw    $0x40, %ax
        jne     1f

        movw    12(%bp), %ds            /* BiosSelector                */
        les     8(%bp), %bx             /* caller's buffer             */
        movw    0x5b7, %ax
        movb    $1, %es:(%bx)           /* +0 structure revision       */
        movb    %ah, %es:1(%bx)         /* +1 number of CSNs           */
        xorb    %ah, %ah
        shlw    $2, %ax
        orb     $3, %al
        movw    %ax, %es:2(%bx)         /* +2 ISA read-data port       */
        movw    $0, %es:4(%bx)          /* +4 reserved                 */
        xorw    %ax, %ax                /* success                     */
        jmp     2f

1:      movw    $0x0082, %ax            /* BAD_PARAMETER               */
2:      popf
        pop     %es
        pop     %ds
        pop     %bx
        pop     %bp
        lretw
