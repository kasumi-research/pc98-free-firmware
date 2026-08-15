/*
 * Extended-memory block copy for INT 1Fh AH=90h.
 *
 * The service moves bytes between 24-bit physical addresses, which real
 * mode cannot reach.  The way out is unreal mode: enter protected mode
 * just long enough to load FS and GS with a flat 4 GiB data descriptor,
 * leave again without ever reloading CS, and then use 32-bit addressing
 * through those two segments while the CPU is back in real mode.  Their
 * descriptor caches keep the big limit until something reloads them.
 *
 * Two details are load-bearing:
 *   - interrupts stay off for the whole excursion.  An interrupt taken
 *     with PE set and a real-mode CS cached would not come back.
 *   - A20 is enabled for the copy and masked again afterwards, which is
 *     what the NEC ROM does; resident drivers (HIMEM, EMM386) know it
 *     and re-enable the gate themselves.
 *
 * The GDT lives in ROM.  lgdt only reads it, and putting it in RAM would
 * mean owning RAM the BIOS does not have.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/layout.h>

/* selector = byte offset of the descriptor within the GDT */
#define SEL_FLAT    (gdt_flat - gdt)

        .code16
        .section .text, "ax"

/* void ext_copy(u32 dst, u32 src, u32 len) */
        .globl ext_copy
ext_copy:
        pushw   %bp
        movw    %sp, %bp
        pushfl
        cli
        pushal
        pushw   %fs
        pushw   %gs

        xorb    %al, %al
        outb    %al, $0xf2              /* A20 on for the transfer */

        lgdt    %cs:gdtr
        movl    %cr0, %eax
        orl     $1, %eax
        movl    %eax, %cr0
        jmp     1f                      /* flush the prefetch queue */
1:      movw    $SEL_FLAT, %ax
        movw    %ax, %fs
        movw    %ax, %gs
        movl    %cr0, %eax
        andl    $0xfffffffe, %eax
        movl    %eax, %cr0
        jmp     2f
2:
        movl    6(%bp), %edi            /* dst */
        movl    10(%bp), %esi           /* src */
        movl    14(%bp), %ecx           /* len */
        testl   %ecx, %ecx
        jz      4f
3:      movb    %gs:(%esi), %al
        movb    %al, %fs:(%edi)
        incl    %esi
        incl    %edi
        decl    %ecx
        jnz     3b
4:
        movb    $0x03, %al
        outb    %al, $0xf6              /* leave the gate masked again */

        popw    %gs
        popw    %fs
        popal
        popfl
        popw    %bp
        retl

/*
 * u32 ext_probe(u32 addr) -- non-zero if writable DRAM answers there.
 *
 * The same unreal-mode excursion as ext_copy, used by POST to size
 * memory: a 16-bit segment cannot reach the second megabyte at all, so
 * there is no other way to ask.
 *
 * Two details:
 *
 *  - nothing is saved and restored.  POST owns everything above 1 MiB
 *    -- no OS, no loader, nothing of ours lives there -- so the probe
 *    simply writes.  Saving would cost a read and buy nothing.
 *  - the megabyte BELOW the probe address gets the complement of the
 *    pattern first.  That is the alias check: an address past the top
 *    of DRAM often wraps back into it rather than floating, and a
 *    single pattern written and read at one address cannot tell a wrap
 *    from real memory.  The caller probes upward from a known-good
 *    megabyte, so the decoy always lands in real DRAM.
 */
        .globl ext_probe
ext_probe:
        pushw   %bp
        movw    %sp, %bp
        pushfl
        cli
        pushl   %ebx
        pushl   %esi
        pushl   %edi
        pushw   %gs

        xorb    %al, %al
        outb    %al, $0xf2              /* A20 on for the probe */

        lgdt    %cs:gdtr
        movl    %cr0, %eax
        orl     $1, %eax
        movl    %eax, %cr0
        jmp     1f
1:      movw    $SEL_FLAT, %ax
        movw    %ax, %gs
        movl    %cr0, %eax
        andl    $0xfffffffe, %eax
        movl    %eax, %cr0
        jmp     2f
2:
        movl    6(%bp), %esi
        movl    %esi, %edi
        subl    $0x100000, %edi
        movl    $0xa5c31f5a, %ecx
        movl    %ecx, %gs:(%esi)
        notl    %ecx
        movl    %ecx, %gs:(%edi)        /* decoy, one megabyte below */
        movl    %gs:(%esi), %edx
        notl    %ecx
        xorl    %ecx, %edx              /* zero only if the pattern held */

        movb    $0x03, %al
        outb    %al, $0xf6              /* leave the gate masked again */

        xorl    %eax, %eax
        testl   %edx, %edx
        jnz     3f
        incl    %eax
3:
        popw    %gs
        popl    %edi
        popl    %esi
        popl    %ebx
        popfl
        popw    %bp
        retl

/*
 * Two unused slots, then one flat 4 GiB data descriptor at selector 16.
 * The second null is padding, not a descriptor: it keeps gdt_flat at a
 * selector whose low bits are clear and leaves room for a second
 * descriptor without renumbering the first.  Nothing here is ever used
 * as a code segment: CS is never reloaded, so it keeps its real-mode
 * cached descriptor throughout.
 */
        .balign 8
gdt:
        .quad   0
        .quad   0
gdt_flat:
        .word   0xffff                  /* limit 15:0                 */
        .word   0x0000                  /* base 15:0                  */
        .byte   0x00                    /* base 23:16                 */
        .byte   0x92                    /* present, data, read/write   */
        .byte   0xcf                    /* 4 KiB granularity, 32-bit   */
        .byte   0x00                    /* base 31:24                 */
gdt_end:

gdtr:
        .word   gdt_end - gdt - 1
        .long   gdt + (SEG_BIOS << 4)   /* linear: this bank runs at F8000 */
