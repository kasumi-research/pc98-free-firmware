/*
 * Port I/O and far (segment:offset) memory access.
 *
 * The far helpers exist because the C environment runs with DS == SS in
 * ONE 64 KiB segment, and that segment is not the one CS points at (see
 * core/post/entry.S and romdata.h): a plain C pointer can only reach
 * DS, so anything outside it — the IVT, the BIOS work area at
 * 0000:0400, text VRAM at A000:0000, the memory switches at A3FE0 —
 * must go through these, and our own image through rom_*.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_IO_H
#define PC98_IO_H

#include <pc98/types.h>

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline void outw(u16 port, u16 v)
{
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline void outl(u16 port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 v;

    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline u16 inw(u16 port)
{
    u16 v;

    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline u32 inl(u16 port)
{
    u32 v;

    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/*
 * A short delay for slow peripherals.  The PC-98 idiom is a write to
 * port 0x5F, the machine's own "wait" port: the bus cycle is what
 * costs the time, and nothing decodes the value.
 */
static inline void iodelay(void)
{
    __asm__ volatile("outb %%al, $0x5f" : : : "memory");
}

/* ---- far memory access (segment:offset outside our own segment) ---- */

/*
 * ES is saved and restored inside each helper rather than declared as a
 * clobber: gcc does not accept segment registers in a clobber list.
 * Explicit pushw/popw keeps the operand size 16-bit and balanced under
 * -m16, which emits 32-bit pushes for a bare `push`.
 *
 * The offset is widened to 32 bits so it lands in a 32-bit register:
 * 16-bit addressing only accepts BX/BP/SI/DI as a base, whereas the
 * addr32 form gcc already emits everywhere accepts any register.
 *
 * EVERY operand is "r", never "rm".  A memory operand may be
 * stack-relative, and the pushw above moves the stack pointer out from
 * under its displacement -- so the asm would load ES from the wrong
 * two bytes.  It cost a debugging session: with `seg` a constant gcc
 * picked a register and text output worked, and the first caller that
 * passed a variable (the IDE PIO loop) silently wrote its sector data
 * into some other segment.
 */
static inline u8 peekb(u16 seg, u16 off)
{
    u8 v;

    __asm__ volatile("pushw %%es\n\t"
                     "movw %1, %%es\n\t"
                     "movb %%es:(%2), %0\n\t"
                     "popw %%es"
                     : "=q"(v) : "r"(seg), "r"((u32)off));
    return v;
}

static inline void pokeb(u16 seg, u16 off, u8 v)
{
    __asm__ volatile("pushw %%es\n\t"
                     "movw %0, %%es\n\t"
                     "movb %2, %%es:(%1)\n\t"
                     "popw %%es"
                     : : "r"(seg), "r"((u32)off), "q"(v) : "memory");
}

static inline u16 peekw(u16 seg, u16 off)
{
    u16 v;

    __asm__ volatile("pushw %%es\n\t"
                     "movw %1, %%es\n\t"
                     "movw %%es:(%2), %0\n\t"
                     "popw %%es"
                     : "=r"(v) : "r"(seg), "r"((u32)off));
    return v;
}

static inline void pokew(u16 seg, u16 off, u16 v)
{
    __asm__ volatile("pushw %%es\n\t"
                     "movw %0, %%es\n\t"
                     "movw %2, %%es:(%1)\n\t"
                     "popw %%es"
                     : : "r"(seg), "r"((u32)off), "r"(v) : "memory");
}

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }

/* Park forever.  Used by the not-yet-implemented tails of POST. */
static inline void __attribute__((noreturn)) halt_forever(void)
{
    for (;;) {
        cli();
        hlt();
    }
}

#endif
