/*
 * The F8E80 capability block.
 *
 * This is a small table of machine-capability bytes that DOS and the
 * Windows drivers read straight out of the ROM window -- it is
 * published interface at a FIXED ADDRESS, so unlike everything else in
 * this firmware it cannot be moved.  np21w's model has POST rewriting
 * three of these bytes: F8E90 (the IDE equipment bitmap), F8E91 and
 * F8E82.
 *
 * The values are measured, and the measurement corrected that model:
 * comparing E8000-FFFFF read out of a running Xa7 against the same
 * region before boot, exactly ONE byte differs -- F8E90, patched to the
 * IDE equipment bitmap.  F8E91 and F8E82 are NOT rewritten on real
 * hardware, whatever np21w does.
 *
 * Leaving this region at the flash's erased 0xFF is not neutral: every
 * capability bit reads as present, and Windows' PC-98 serial driver
 * then polls an 8251 receive path that no hardware answers, which is
 * where the Win98 boot hung before this block existed.
 *
 * F8E90 is a constant here because the ROM window is not writable: on a
 * machine running the NEC ROM the byte is patched in the RAM shadow
 * during POST, before the shadow is write-protected, and we have no
 * shadow yet.  TODO: once POST populates and write-protects the
 * E8000-FFFFF shadow, compute it from the disk scan.
 *
 * SPDX-License-Identifier: MIT
 */

        .code16
        .section .caps, "ax"

        .globl caps_block
caps_block:                             /* linker places this at F8E80 */
        .byte   0x98, 0x21, 0x1f, 0x20, 0x2c, 0xb0, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        /*
         * F8E90: IDE equipment bitmap, one bit per unit.  The machine
         * this is measured from had 0x05 (a disk and a CD-ROM); ours is
         * a single fixed disk on unit 0.
         */
        .byte   0x01, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        .byte   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

/*
 * F8EA0-F8ED1: more published bytes, and they are NOT optional.
 *
 * Comparing our image against pc98-flash.bin over F8E80-F8EFF, the only
 * differences used to be F8E90 (the IDE bitmap above, deliberate) and
 * this tail -- which we left as zero.  Both halves of it are read on a
 * live Win98 boot (scratch/logs/wa-free.log, paread on f8e80-f8f00):
 *
 *   pc=c0381ebf reads F8EBF          <- a lone capability byte, 0x21
 *   pc=c180699b sweeps F8E80-F8EF0   <- the BIOS32 signature scan
 *
 * so a VxD is reading F8EBF specifically and getting 0 from us where
 * the real machine answers 0x21.  Same lesson as the block above: this
 * region is published interface, and leaving it at a convenient value
 * is not neutral.  The semantics of 0x21 are not known -- it is
 * reproduced because it is measured.
 *
 * The routine at F8EC0 is a far-callable entry, self-contained and
 * ending in LRET, that sets or clears bit 3 of work-area byte 0x0480
 * from bit 3 of AL.  Transcribed instruction for instruction so it
 * assembles to the reference's bytes; tools/mkflash.py would not catch
 * a divergence here, so it is checked by eye against
 *   a8 08 74 08 80 0e 80 04 08 eb 06 90 80 26 80 04 f7 cb
 */
        .org    0x20                    /* = F8EA0; asserts the size    */
        .space  0x1f, 0x00              /* F8EA0-F8EBE, zero on metal  */
        .byte   0x21                    /* F8EBF, measured             */

        .globl  caps_set_0480_bit3
caps_set_0480_bit3:                     /* F8EC0, far-called           */
        testb   $0x08, %al
        jz      1f
        orb     $0x08, 0x0480
        jmp     2f
        nop
1:      andb    $0xf7, 0x0480
2:      lret
