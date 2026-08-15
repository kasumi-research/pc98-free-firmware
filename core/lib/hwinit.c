/*
 * Power-on programming of the mainboard peripherals: DMA, PIT, PICs.
 *
 * The sequence is the one the emulator models a PC-98 ITF performing
 * (np21w's ITF I/O table, carried into qemu hw/i386/pc98), including
 * the two interrupt-mask seeds that were measured on a real Xa7 rather
 * than taken from np21w's defaults.  It is idempotent, so both the ITF and
 * the BIOS's cold path run it -- a CPU-only reset re-enters the BIOS
 * with BANK7 still mapped and the ITF never runs again, and the machine
 * still has to come up.
 *
 * hw_table derives from Neko Project 21/W (bios/bios.c), which is
 * BSD 3-Clause:
 *
 *   Copyright (c) 1999-2025, NP2 developer team.  All rights reserved.
 *
 * The two interrupt-mask seeds at the end of the table are not from
 * np21w; they were measured on a real Xa7.  The full licence text and
 * its conditions -- which bind BINARY redistribution of a built ROM as
 * well as source -- are in NOTICE at the top of this tree.
 *
 * SPDX-License-Identifier: MIT AND BSD-3-Clause
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/romdata.h>
#include <pc98/hwinit.h>

struct io_write { u16 port; u8 val; };

static const struct io_write hw_table[] = {
    /* 8237 DMA: clear the mask/mode state of all four channels */
    { 0x29, 0x00 }, { 0x29, 0x01 }, { 0x29, 0x02 }, { 0x29, 0x03 },
    { 0x27, 0x00 }, { 0x21, 0x00 }, { 0x23, 0x00 }, { 0x25, 0x00 },
    { 0x1b, 0x00 }, { 0x11, 0x40 },
    /* 8253 PIT: ch0 rate generator, ch1 memory refresh, ch2 buzzer */
    { 0x77, 0x30 }, { 0x71, 0x00 }, { 0x71, 0x00 },
    { 0x77, 0x76 }, { 0x73, 0xcd }, { 0x73, 0x04 },
    { 0x77, 0xb6 },
    /*
     * 8259s.  ICW3 = 0x80 on the master: the slave cascades on IR7, not
     * IR2 as on a PC/AT.  Vector bases 08h and 10h.
     */
    { 0x00, 0x11 }, { 0x02, 0x08 }, { 0x02, 0x80 }, { 0x02, 0x1d },
    { 0x08, 0x11 }, { 0x0a, 0x10 }, { 0x0a, 0x07 }, { 0x0a, 0x09 },
    { 0x02, 0x7d }, { 0x0a, 0x75 },     /* IMR seeds, measured on the Xa7 */
};

void hw_init(void)
{
    unsigned i;

    for (i = 0; i < sizeof(hw_table) / sizeof(hw_table[0]); i++) {
        outb(rom_w(&hw_table[i].port), rom_b(&hw_table[i].val));
        iodelay();
    }
}
