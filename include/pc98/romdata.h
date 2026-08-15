/*
 * ROM-resident data access for the runtime BIOS.
 *
 * Firmware C never runs with DS pointing at its own image.  In the ITF,
 * DS = SS is the RAM data segment while CS stays in the ROM window; in
 * the BIOS, an INT handler is entered from arbitrary guest code — DOS,
 * NTLDR, a boot sector — and runs on the CALLER's stack with DS = SS
 * set to the caller's stack segment (no stack switch; see bregs.h).
 * Either way `.rodata` sits at CS, out of reach of a plain C pointer
 * dereference.
 *
 * Everything read out of our own image therefore goes through these
 * CS-override accessors.  This is SeaBIOS's GET_GLOBAL pattern and it
 * is deliberate, not a workaround: the alternative designs were weighed
 * and rejected.
 *
 * Rule of thumb:
 *   - constants and tables in the ROM image  -> rom_b/rom_w/rom_l
 *   - the work area at 0000:0400-05FF        -> peekb/pokeb (io.h)
 *   - locals and scratch                     -> ordinary C, on our stack
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_ROMDATA_H
#define PC98_ROMDATA_H

#include <pc98/types.h>

static inline u8 rom_b(const void *p)
{
    u8 v;

    __asm__("movb %%cs:(%1), %0" : "=q"(v) : "r"(p));
    return v;
}

static inline u16 rom_w(const void *p)
{
    u16 v;

    __asm__("movw %%cs:(%1), %0" : "=r"(v) : "r"(p));
    return v;
}

static inline u32 rom_l(const void *p)
{
    u32 v;

    __asm__("movl %%cs:(%1), %0" : "=r"(v) : "r"(p));
    return v;
}

/* Pull a ROM table into a caller-provided buffer (locals, so DS-based). */
static inline void rom_copy(void *dst, const void *src, u16 n)
{
    u8 *d = dst;
    const u8 *s = src;

    while (n--) {
        *d++ = rom_b(s++);
    }
}

/* strlen for a ROM-resident string. */
static inline u16 rom_strlen(const char *s)
{
    u16 n = 0;

    while (rom_b(s + n)) {
        n++;
    }
    return n;
}

#endif
