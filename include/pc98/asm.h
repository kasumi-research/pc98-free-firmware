/*
 * Assembler macros for the stackless phases of POST.
 *
 * Before DRAM is up there is no stack, so there is no `call` and no
 * `ret`.  The way out is LINK REGISTERS: a call loads a register with
 * the return address and jumps; the callee returns by jumping through
 * that register.  When there is no memory, SP is just a spare 16-bit
 * register like any other.
 *
 * We use the same convention.  One register per nesting level, so a
 * callee never clobbers its caller's return address:
 *
 *      level 0   entry.S  -> hal_early_init     %sp
 *      level 1   hal      -> io_table_apply     %bp
 *      level 2   (spare)                        %si, %cx
 *
 * Every stackless routine must document which link register it takes
 * and which registers it clobbers.  There is no compiler to check this.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_ASM_H
#define PC98_ASM_H
#ifdef __ASSEMBLER__

#include <pc98/model.h>

/* Stackless call: `reg` receives the return address. */
.macro  CALLR reg, target
        movw    $.Lcallr\@, %\reg
        jmp     \target
.Lcallr\@:
.endm

/* Stackless return through the link register. */
.macro  RETR reg
        jmp     *%\reg
.endm

/*
 * Stamp a POST progress code.  Swallowed with no POST card fitted, on
 * hardware and in the emulator alike, so it is free — always stamp.
 * Clobbers AL and DX.
 */
.macro  POSTCODE code
        movw    $PORT_POST, %dx
        movb    $\code, %al
        outb    %al, %dx
.endm

/* The PC-98 I/O settle idiom: a write to the machine's wait port. */
.macro  IODELAY
        outb    %al, $0x5f
.endm

/*
 * Entries for an io_table_apply table: one byte-wide or word-wide port
 * write.  5 bytes each; the table is preceded by a .word count.
 */
.macro  IOB port, val
        .byte   1
        .word   \port
        .word   \val
.endm

.macro  IOW port, val
        .byte   2
        .word   \port
        .word   \val
.endm

#endif  /* __ASSEMBLER__ */
#endif
