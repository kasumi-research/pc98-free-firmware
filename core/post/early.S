/*
 * Stackless helpers for the pre-DRAM phases of POST.
 *
 * Everything here runs from ROM with no stack.  See include/pc98/asm.h
 * for the link-register convention.
 *
 * Early chipset setup is driven from tables rather than straight-line
 * code: a table costs 3 bytes per write where inline code costs 5 to 7,
 * and — the reason that matters here — a table cannot accidentally use
 * a stack.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/asm.h>

        .code16
        .section .text, "ax"

/*
 * Apply a table of I/O port writes, byte- or word-wide.
 *
 *   in      %si   table, in CS
 *   link    %bp
 *   clobber %ax %bx %cx %dx %si
 *
 * Table layout — build it with the IOB / IOW macros:
 *      .word   count
 *      count * { .byte width ; .word port ; .word value }
 *
 * DS is not used — every fetch carries a CS override — so this is safe
 * whatever DS happens to hold, which matters because the early phases
 * hand DS around freely.
 */
        .globl io_table_apply
io_table_apply:
        movw    %cs:(%si), %cx
        addw    $2, %si
        jcxz    9f
1:      movb    %cs:(%si), %bl          /* width: 1 or 2 */
        movw    %cs:1(%si), %dx         /* port          */
        movw    %cs:3(%si), %ax         /* value         */
        addw    $5, %si
        cmpb    $2, %bl
        je      2f
        outb    %al, %dx
        jmp     3f
2:      outw    %ax, %dx
3:      IODELAY
        loop    1b
9:      RETR    bp
