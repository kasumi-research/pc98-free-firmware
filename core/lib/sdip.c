/*
 * SDIP battery store validation.
 *
 * The software DIP switches live in twelve battery-backed registers at
 * 841Eh..8F1Eh, two banks deep, bank-selected through 8F1Fh (80h front,
 * C0h back).  On a machine that has never been powered, or whose
 * battery has failed, they read back as garbage -- typically all zero
 * -- and every setting derived from them (GDC clock, text rows, DMA
 * clock, graphics mode) comes out wrong.  The NEC ROM detects that and
 * rewrites factory defaults; a free firmware that does not leaves a
 * brand-new emulator install booting with nonsense switches.
 *
 * Detection is the store's own parity discipline (Undocumented 9801,
 * io_sdip.txt): every byte carries odd parity over itself, except that
 * 891Eh has no parity bit of its own and 8A1Eh bit 7 covers the PAIR --
 * the two bytes together must sum odd.  Both metal captures (Xa7 and
 * RvII26 settled stores) satisfy exactly this rule, and an all-zero
 * store fails it in every byte.
 *
 * A valid store is never touched: it is the user's configuration, not
 * ours.  An invalid one is rewritten whole, both banks, from the
 * model's measured defaults, then read back -- a mismatch means the
 * store is not accepting writes, which is a machine (or emulator) bug
 * worth a serial line.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/serial.h>
#include <pc98/romdata.h>
#include <pc98/sdip.h>

#define SDIP_PORT(i)    (u16)(0x841e + (unsigned)(i) * 0x100)
#define PORT_SDIP_BANK  0x8f1f
#define SDIP_BANK_FRONT 0x80
#define SDIP_BANK_BACK  0xc0

/* front-bank index of the 891Eh/8A1Eh pair */
#define SDIP_IDX_891E   5
#define SDIP_IDX_8A1E   6

static void sdip_select(u8 bank)
{
    outb(PORT_SDIP_BANK, bank);
    iodelay();
}

static void sdip_read_bank(u8 bank, u8 *buf)
{
    unsigned i;

    sdip_select(bank);
    for (i = 0; i < SDIP_NREGS; i++) {
        buf[i] = inb(SDIP_PORT(i));
    }
}

static bool parity_odd(u8 b)
{
    b ^= b >> 4;
    b ^= b >> 2;
    b ^= b >> 1;
    return b & 1;
}

static void sdip_whine(const char *bank, unsigned i, u8 val)
{
    ser_puts("SDIP: parity fail ");
    ser_puts(bank);
    ser_putc('[');
    ser_hexb((u8)i);
    ser_puts("]=");
    ser_hexb(val);
    ser_crlf();
}

/*
 * Both banks under one rule: odd parity per byte, except the front
 * bank's 891Eh/8A1Eh pair, which is odd over the two together.
 */
static bool sdip_valid(const u8 *front, const u8 *back)
{
    unsigned i;

    for (i = 0; i < SDIP_NREGS; i++) {
        if (i == SDIP_IDX_891E || i == SDIP_IDX_8A1E) {
            continue;
        }
        if (!parity_odd(front[i])) {
            sdip_whine("F", i, front[i]);
            return false;
        }
    }
    if (parity_odd(front[SDIP_IDX_891E]) == parity_odd(front[SDIP_IDX_8A1E])) {
        sdip_whine("P", SDIP_IDX_8A1E, front[SDIP_IDX_8A1E]);
        return false;
    }
    for (i = 0; i < SDIP_NREGS; i++) {
        if (!parity_odd(back[i])) {
            sdip_whine("B", i, back[i]);
            return false;
        }
    }
    return true;
}

static void sdip_write_bank(u8 bank, const u8 *def)
{
    unsigned i;

    sdip_select(bank);
    for (i = 0; i < SDIP_NREGS; i++) {
        outb(SDIP_PORT(i), rom_b(def + i));
        iodelay();
    }
}

static void sdip_dump(const char *tag, const u8 *front, const u8 *back)
{
    unsigned i;

    ser_puts("SDIP: ");
    ser_puts(tag);
    ser_puts(" F=");
    for (i = 0; i < SDIP_NREGS; i++) {
        ser_hexb(front[i]);
    }
    ser_puts(" B=");
    for (i = 0; i < SDIP_NREGS; i++) {
        ser_hexb(back[i]);
    }
    ser_crlf();
}

void sdip_init(void)
{
    u8 front[SDIP_NREGS], back[SDIP_NREGS];
    const u8 *def = hal_sdip_defaults();
    unsigned i;
    bool ok;

    if (!def) {
        return;                         /* model has no SDIP store */
    }

    sdip_read_bank(SDIP_BANK_FRONT, front);
    sdip_read_bank(SDIP_BANK_BACK, back);
    sdip_select(SDIP_BANK_FRONT);

    if (sdip_valid(front, back)) {
        ser_puts("SDIP: store valid, keeping it\r\n");
        return;
    }

    ser_puts("SDIP: store fails parity, writing defaults\r\n");
    sdip_dump("read", front, back);
    sdip_write_bank(SDIP_BANK_FRONT, def);
    sdip_write_bank(SDIP_BANK_BACK, def + SDIP_NREGS);

    /* read back: a store that will not hold writes deserves a line */
    sdip_read_bank(SDIP_BANK_FRONT, front);
    sdip_read_bank(SDIP_BANK_BACK, back);
    sdip_select(SDIP_BANK_FRONT);
    ok = true;
    for (i = 0; i < SDIP_NREGS; i++) {
        if (front[i] != rom_b(def + i) ||
            back[i] != rom_b(def + SDIP_NREGS + i)) {
            ok = false;
        }
    }
    if (!ok) {
        ser_puts("SDIP: readback MISMATCH, store not accepting writes\r\n");
        sdip_dump("readback", front, back);
    }
}
