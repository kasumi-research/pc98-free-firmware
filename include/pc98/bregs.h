/*
 * The interrupt-entry register frame, and the BIOS's calling contract.
 *
 * MEASURED, and it overrides an earlier design that gave the BIOS a
 * private stack.  A PC-98 BIOS service never switches stacks:
 * it runs on the CALLER's stack, and its only writable state is the
 * work area at 0000:0400 (reached with DS = 0) plus the caller's
 * registers.  Watching the stack pointer across an INT 18h call into
 * the NEC ROM shows it unchanged; a caller with only a few hundred
 * bytes of stack still gets served.
 *
 * A private stack has nowhere to live anyway -- DOS's IO.SYS starts at
 * 0000:0600 (measured on a booted Win98) and the work area below that
 * is fully allocated.
 *
 * So: the entry stub pushes the frame below, sets DS = SS (which gcc
 * requires -- it will hand a pointer to a stack local to code that
 * dereferences it through DS), and calls C.  Consequences:
 *
 *   - the BIOS has NO globals and NO .bss.  Persistent state goes in
 *     the work area through wa_* (wa.h), constants in ROM through rom_*.
 *   - handlers must keep their stack frames small; the caller's stack
 *     is all we have.
 *   - registers are returned to the caller by writing them into the
 *     frame, including the flags word the final iret reloads.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_BREGS_H
#define PC98_BREGS_H

/*
 * Service ids, pushed by the entry stubs that call into C (see
 * core/bios/vectors.S).  The vectors handled entirely in assembly --
 * the EOI stubs, the timer, NMI and the do-nothing entries -- never
 * reach bios_dispatch and so have no id here.  The numbers are the
 * vector numbers, which makes a trace readable.
 */
#define SVC_KBD     0x09        /* IRQ1  */
#define SVC_FDC1    0x12
#define SVC_FDC2    0x13
#define SVC_INT18   0x18
#define SVC_INT19   0x19
#define SVC_INT1A   0x1a
#define SVC_INT1B   0x1b
#define SVC_INT1C   0x1c
#define SVC_INT1E   0x1e
#define SVC_INT1F   0x1f

/* The defines above are shared with the assembly; the rest is C only. */
#ifndef __ASSEMBLER__

#include <pc98/types.h>

/*
 * Pushed by the entry stub as `pushw %es; pushw %ds; pushal`, on top of
 * the ip/cs/flags the interrupt itself pushed.  Offsets rise from the
 * stack pointer, so the struct reads in push-reverse order: the pushal
 * block first (EDI lowest), then DS, ES, and the interrupt frame.
 *
 * The registers are 32 bits wide because some services need them to be
 * -- the PCI BIOS passes and returns dwords in ECX and EAX.  Each has a
 * 16-bit alias so the ordinary 16-bit services read naturally; this is
 * little-endian, so the alias is the low half.
 */
struct bregs {
    union { u32 edi; u16 di; };
    union { u32 esi; u16 si; };
    union { u32 ebp; u16 bp; };
    u32 esp_at_entry;                   /* pushal stores it, popal drops it */
    union { u32 ebx; u16 bx; };
    union { u32 edx; u16 dx; };
    union { u32 ecx; u16 cx; };
    union { u32 eax; u16 ax; };
    u16 ds, es;
    u16 ip, cs, flags;
};

#define F_CF        0x0001
#define F_ZF        0x0040
#define F_IF        0x0200

static inline void set_cf(struct bregs *r, bool on)
{
    r->flags = on ? (r->flags | F_CF) : (r->flags & (u16)~F_CF);
}

static inline void set_zf(struct bregs *r, bool on)
{
    r->flags = on ? (r->flags | F_ZF) : (r->flags & (u16)~F_ZF);
}

/* byte halves of the frame's general registers */
#define R_AL(r)     (*(u8 *)&(r)->ax)
#define R_AH(r)     (*((u8 *)&(r)->ax + 1))
#define R_BL(r)     (*(u8 *)&(r)->bx)
#define R_BH(r)     (*((u8 *)&(r)->bx + 1))
#define R_CL(r)     (*(u8 *)&(r)->cx)
#define R_CH(r)     (*((u8 *)&(r)->cx + 1))
#define R_DL(r)     (*(u8 *)&(r)->dx)
#define R_DH(r)     (*((u8 *)&(r)->dx + 1))

void bios_dispatch(struct bregs *r, u16 svc);
void bios_int18(struct bregs *r);
void bios_int1a(struct bregs *r);
void bios_int1b(struct bregs *r);
void bios_int1c(struct bregs *r);
void bios_int1f(struct bregs *r);
void bios_irq_kbd(struct bregs *r);
void bios_irq_timer(struct bregs *r);
void bios_trace(char tag, u8 ah);

#endif  /* __ASSEMBLER__ */
#endif
