/*
 * Serial debug console — 2nd CCU 16550 at 0x238, 115200 8N1, polled.
 *
 * Same port and framing as the QEMU PC-98 self-test suite's,
 * deliberately: anything built to read that suite's serial output
 * reads this firmware's the same way.
 *
 * Unlike the suite's version this driver is STATELESS — no "is the UART
 * present" flag.  A ROM has no writable globals until DRAM is up and
 * .data has somewhere to live, and we want serial working before that.
 * Statelessness is free here: an absent UART floats the bus high, so
 * LSR reads 0xFF, THRE (0x20) tests set, and the write goes nowhere
 * harmlessly and immediately.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_SERIAL_H
#define PC98_SERIAL_H

#define SER_BASE    0x238

#define SER_THR     (SER_BASE + 0)  /* tx holding / divisor low  */
#define SER_IER     (SER_BASE + 1)  /* int enable / divisor high */
#define SER_FCR     (SER_BASE + 2)  /* FIFO control              */
#define SER_LCR     (SER_BASE + 3)  /* line control              */
#define SER_MCR     (SER_BASE + 4)  /* modem control             */
#define SER_LSR     (SER_BASE + 5)  /* line status               */
#define SER_SCR     (SER_BASE + 7)  /* scratch                   */

#define LSR_THRE    0x20            /* tx holding register empty */
#define LSR_TEMT    0x40            /* tx shift register empty   */

/* The defines above are shared with entry.S; the rest is C only. */
#ifndef __ASSEMBLER__

#include <pc98/types.h>

void ser_init(void);
bool ser_present(void);
void ser_putc(char c);
void ser_puts(const char *s);
void ser_crlf(void);
void ser_hexb(u8 v);
void ser_hexw(u16 v);
void ser_hexl(u32 v);
void ser_dec(u32 v);
/* put a byte and drain to TEMT, so the byte provably reached the wire
 * before whatever comes next (a reset, a mode switch, a wedge) */
void ser_mark(char c);

#endif  /* __ASSEMBLER__ */
#endif
