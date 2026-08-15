/*
 * Serial debug console.  See include/pc98/serial.h for why this is
 * stateless.
 *
 * NOTE the rom_b() reads.  Firmware C runs with DS pointing at RAM
 * while .rodata stays in ROM at CS, so string literals and const
 * tables are NOT reachable by a plain dereference.  ser_puts therefore
 * takes a pointer into our own image and reads it with a CS override,
 * which is what makes ser_puts("...") work at the call site.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/serial.h>
#include <pc98/romdata.h>

/* Bounded so a dead or half-programmed line can never hang POST. */
#define TX_SPIN     0x10000

void ser_init(void)
{
    outb(SER_LCR, 0x80);            /* DLAB */
    outb(SER_THR, 1);               /* divisor 1 = 115200 */
    outb(SER_IER, 0);
    outb(SER_LCR, 0x03);            /* 8N1 */
    outb(SER_FCR, 0x07);            /* FIFOs on + reset both */
    outb(SER_MCR, 0x03);            /* DTR | RTS */
    outb(SER_IER, 0);               /* polled: no interrupts */
}

/*
 * Scratch-register wiggle plus a floating-bus guard, as the self-test
 * suite does it.  Only for diagnostics — ser_putc does not consult it.
 */
bool ser_present(void)
{
    outb(SER_SCR, 0xaa);
    if (inb(SER_SCR) != 0xaa) {
        return false;
    }
    outb(SER_SCR, 0x55);
    if (inb(SER_SCR) != 0x55) {
        return false;
    }
    return inb(SER_LSR) != 0xff;
}

void ser_putc(char c)
{
    uint n;

    for (n = TX_SPIN; n; n--) {
        if (inb(SER_LSR) & LSR_THRE) {
            outb(SER_THR, (u8)c);
            return;
        }
    }
}

void ser_mark(char c)
{
    uint n;

    ser_putc(c);
    for (n = TX_SPIN; n; n--) {
        if (inb(SER_LSR) & LSR_TEMT) {
            return;
        }
    }
}

/* `s` points into our own ROM image (a literal or a const table). */
void ser_puts(const char *s)
{
    char c;

    while ((c = (char)rom_b(s++)) != 0) {
        ser_putc(c);
    }
}

void ser_crlf(void)
{
    ser_putc('\r');
    ser_putc('\n');
}

static const char hexdig[] = "0123456789ABCDEF";

void ser_hexb(u8 v)
{
    ser_putc((char)rom_b(&hexdig[v >> 4]));
    ser_putc((char)rom_b(&hexdig[v & 0x0f]));
}

void ser_hexw(u16 v)
{
    ser_hexb((u8)(v >> 8));
    ser_hexb((u8)v);
}

void ser_hexl(u32 v)
{
    ser_hexw((u16)(v >> 16));
    ser_hexw((u16)v);
}

void ser_dec(u32 v)
{
    char buf[10];
    int n = 0;

    if (!v) {
        ser_putc('0');
        return;
    }
    while (v) {
        buf[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n) {
        ser_putc(buf[--n]);
    }
}
