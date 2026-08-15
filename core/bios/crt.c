/*
 * CRT / text-screen BIOS: the hardware half of INT 18h.
 *
 * The mode tables are the ones the emulator models (np21w's CRT BIOS,
 * carried into qemu hw/i386/pc98), cross-checked against the work-area
 * bytes a booted Xa7 leaves behind: with the shipped DIP switches the
 * machine lands on the 400-line 25-row entry, and CRT_RASTER reads 0x0F
 * and CRT_STS_FLAG 0x84 -- which is exactly what this code computes.
 * That agreement is the check that the table is being indexed the way
 * the machine expects.
 *
 * The four parameter tables below and the index arithmetic of AH=30h
 * derive from Neko Project 21/W (bios/bios18.c), which is BSD 3-Clause:
 *
 *   Copyright (c) 1999-2025, NP2 developer team.  All rights reserved.
 *
 * The full licence text and its conditions -- which bind BINARY
 * redistribution of a built ROM as well as source -- are in NOTICE at
 * the top of this tree.
 *
 * SPDX-License-Identifier: MIT AND BSD-3-Clause
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/gdc.h>
#include <pc98/wa.h>
#include <pc98/layout.h>
#include <pc98/romdata.h>

/* raster, pl, bl, cl -- indexed 200-20, 200-25, 400-20, 400-25, 480-* */
static const u8 crtdata[7][4] = {
    { 0x09, 0x1f, 0x08, 0x08 },
    { 0x07, 0x00, 0x07, 0x08 },
    { 0x13, 0x1e, 0x11, 0x10 },
    { 0x0f, 0x00, 0x0f, 0x10 },
    { 0x17, 0x1c, 0x13, 0x10 },
    { 0x12, 0x1f, 0x11, 0x10 },
    { 0x0f, 0x00, 0x0f, 0x10 },
};

/* cursor form: LR, CFI -- indexed as crtdata is, by rows x lines */
static const u8 csrform[4][2] = {
    { 0x07, 0x3b }, { 0x09, 0x4b }, { 0x0f, 0x7b }, { 0x13, 0x9b },
};

/* master GDC SYNC parameter sets: 15 kHz, 24 kHz, 31 kHz, 480-line x3 */
static const u8 master_sync[6][8] = {
    { 0x10, 0x4e, 0x07, 0x25, 0x0d, 0x0f, 0xc8, 0x94 },
    { 0x10, 0x4e, 0x07, 0x25, 0x07, 0x07, 0x90, 0x65 },
    { 0x10, 0x4e, 0x47, 0x0c, 0x07, 0x0d, 0x90, 0x89 },
    { 0x10, 0x4e, 0x4b, 0x0c, 0x03, 0x06, 0xe0, 0x95 },
    { 0x10, 0x4e, 0x4b, 0x0c, 0x03, 0x0b, 0xdb, 0x95 },
    { 0x10, 0x4e, 0x4b, 0x0c, 0x03, 0x06, 0xe0, 0x95 },
};

static const u8 slave_sync[6][8] = {
    { 0x02, 0x26, 0x03, 0x11, 0x86, 0x0f, 0xc8, 0x94 },
    { 0x02, 0x4e, 0x4b, 0x0c, 0x83, 0x06, 0xe0, 0x95 },
    { 0x02, 0x26, 0x03, 0x11, 0x83, 0x07, 0x90, 0x65 },
    { 0x02, 0x4e, 0x07, 0x25, 0x87, 0x07, 0x90, 0x65 },
    { 0x02, 0x26, 0x41, 0x0c, 0x83, 0x0d, 0x90, 0x89 },
    { 0x02, 0x4e, 0x47, 0x0c, 0x87, 0x0d, 0x90, 0x89 },
};

/*
 * Extended analogue mode: the PEGC's 256-colour packed-pixel plane.
 * On this hardware that is mode flip-flop 2 value 21h / 20h.
 */
void crt_analog_ext(bool on)
{
    outb(GDC_MODEFF2, (u8)(on ? 0x21 : 0x20));
    iodelay();
}

/*
 * Bring up the analogue palette.
 *
 * A PC-9821 has the PEGC and its 256-entry analogue palette; POST has
 * to say so, because until it does the display hardware is still in the
 * PC-9801 digital 8-colour mode and every analogue palette write is
 * decoded as a digital colour pack instead.  The symptom is not subtle
 * and it is not a palette bug: the Win98 boot logo comes up as a yellow
 * and black checkerboard, because its 16-colour planar data is being
 * shown through three digital planes.
 *
 * The sequence is the PEGC presence probe: enable 256-colour packed
 * mode, drop back to 16-colour, then select the analogue palette.  A
 * machine without the PEGC ignores the first two writes.
 */
void crt_analog_init(void)
{
    crt_analog_ext(true);               /* PEGC 256-colour on  */
    crt_analog_ext(false);              /* ... and back to 16  */
    modeff2(0, true);                   /* analogue 16-colour  */
}

void crt_display(bool on)
{
    gdc_m_cmd(on ? GDCCMD_BCTRL_ON : GDCCMD_BCTRL_OFF);
}

void crt_cursor(bool on)
{
    gdc_m_cmd(GDCCMD_CSRFORM);
    gdc_m_par((u8)(wa_b(WA_CRT_RASTER) | (on ? 0x80 : 0x00)));
}

void crt_cursor_pos(u16 addr)
{
    gdc_m_cmd(GDCCMD_CSRW);
    gdc_m_par((u8)(addr & 0xff));
    gdc_m_par((u8)((addr >> 8) & 0x1f));
    gdc_m_par(0);
}

/*
 * Display area 1: start address (in words) and length (in SCANLINES,
 * bits 13:4 of the second word).  25 rows x 16 = 400 = 0x190 -> 0x1900.
 * LEN = 1 makes the GDC restart at address 0 after a single row and
 * draw row 0 over and over; that bug cost a bring-up session.
 */
void crt_area1(u16 addr, u16 raster)
{
    gdc_m_cmd(GDCCMD_PRAM + 0);
    gdc_m_par((u8)(addr & 0xff));
    gdc_m_par((u8)((addr >> 8) & 0x1f));
    gdc_m_par((u8)((raster << 4) & 0xf0));
    gdc_m_par((u8)(raster >> 4));
}

/*
 * INT 18h AH=0Ah: set the text screen mode.
 *   bit0 20/25 rows, bit1 40/80 columns, bit2 attributes, bit3 code access
 * The 200/400-line half comes from DIP switch 1-1, not from AL.
 */
void crt_mode_init(u8 mode)
{
    unsigned idx = 0;
    u8 sts = mode;
    u8 raster, pl, bl, cl;
    unsigned i;

    /*
     * DIP switch 1-1 selects the 400-line CRT.  Port 33h bit3 carries
     * it INVERTED (qemu hw/i386/pc98/pc98-sysport.c: ((~dipsw1) & 1)
     * << 3), so bit3 set means "400 line" -- which is how a shipped Xa7
     * is configured.
     */
    if (inb(0x33) & 0x08) {
        sts |= 0x80;
        idx += 2;
    }
    if (!(mode & 0x01)) {
        idx += 1;                       /* 25 rows */
    }

    raster = rom_b(&crtdata[idx][0]);
    pl     = rom_b(&crtdata[idx][1]);
    bl     = rom_b(&crtdata[idx][2]);
    cl     = rom_b(&crtdata[idx][3]);

    wa_setb(WA_CRT_STS_FLAG, sts);
    wa_setb(WA_CRT_RASTER, raster);

    /* mode flip-flop 1: attributes, column count, line count, KAC */
    modeff1(MODE1_ATTR, (mode & 0x04) != 0);
    modeff1(MODE1_40COL, (mode & 0x02) != 0);
    modeff1(MODE1_400LINE, (sts & 0x80) != 0);
    modeff1(MODE1_KAC, (mode & 0x08) != 0);

    /* master GDC: sync set for the 24 kHz CRT this family drives */
    gdc_m_cmd(GDCCMD_SYNC);
    for (i = 0; i < 8; i++) {
        gdc_m_par(rom_b(&master_sync[1][i]));
    }
    gdc_m_cmd(GDCCMD_PITCH);
    gdc_m_par(TEXT_COLS);

    /* slave GDC: keep the graphics plane quiet but synced */
    gdc_s_cmd(GDCCMD_SYNC);
    for (i = 0; i < 8; i++) {
        gdc_s_par(rom_b(&slave_sync[3][i]));
    }
    gdc_s_cmd(GDCCMD_PITCH);
    gdc_s_par(TEXT_COLS);
    gdc_s_cmd(GDCCMD_BCTRL_OFF);

    /* CRTC text layout */
    outb(CRTC_PL + 0, pl);
    outb(CRTC_PL + 2, bl);
    outb(CRTC_PL + 4, cl);
    outb(CRTC_PL + 6, 0);               /* ssl */
    outb(CRTC_PL + 8, 1);               /* sur */
    outb(CRTC_PL + 10, 0);              /* sdr */

    /* cursor form for this geometry, cursor off */
    {
        unsigned pos = (sts & 0x01) + ((sts & 0x80) ? 2 : 0);

        gdc_m_cmd(GDCCMD_CSRFORM);
        gdc_m_par(rom_b(&csrform[pos][0]));
        gdc_m_par(0);
        gdc_m_par(rom_b(&csrform[pos][1]));
        wa_setb(WA_CRT_CNT, 0);
    }

    crt_area1(0, (sts & 0x80) ? 400 : 200);
    wa_setw(WA_CRT_W_VRAMADR, 0);
    /*
     * CRT_W_RASTER is deliberately NOT written here: a booted Xa7 has
     * it zero at this point, and only INT 18h AH=0Eh (set a single
     * display area) fills it in.  Seeding it looked harmless and put a
     * value in the work-area diff that the NEC ROM does not have.
     */
    crt_cursor_pos(0);
}

/* ---- display-mode services (INT 18h AH=30h and AH=42h) ---- */

static void gdc_sync(bool slave, const u8 *set)
{
    unsigned i;

    if (slave) {
        gdc_s_cmd(GDCCMD_SYNC);
        for (i = 0; i < 8; i++) {
            gdc_s_par(rom_b(&set[i]));
        }
    } else {
        gdc_m_cmd(GDCCMD_SYNC);
        for (i = 0; i < 8; i++) {
            gdc_m_par(rom_b(&set[i]));
        }
    }
}

static void gdc_s_scroll(u16 addr, u8 mode)
{
    gdc_s_cmd(GDCCMD_PRAM + 0);
    gdc_s_par((u8)(addr & 0xff));
    gdc_s_par((u8)((addr >> 8) & 0x1f));
    gdc_s_par(0);
    gdc_s_par(mode);
}

/*
 * AH=30h: pick the CRT timing.  AL carries the scan rate (bit2 = 31 kHz)
 * and BH the screen selection (bits 5:4 = line count, bits 1:0 = rows).
 * The index arithmetic is np21w's CRT BIOS's, which the emulator
 * already models; what changes here is that the parameters go out
 * through the GDC and CRTC ports instead of into emulator state.
 *
 * Returns the count of GDC parameter bytes the service reports in AH.
 */
u8 crt_mode30(u8 rate, u8 scrn)
{
    unsigned crt, master, slave;
    const u8 *p;
    u8 f;

    if ((rate & 0xf8) != 0x08 || (scrn & (u8)~0x33) || (scrn & 3) == 3) {
        return 0;
    }
    if ((scrn & 0x30) == 0x30) {                /* 640x480 */
        if (!(rate & 0x0c)) {
            return 0;
        }
        /*
         * The 480-line mode IS the extended-analogue (256-colour packed)
         * mode: this service has to switch the PEGC, not merely record
         * that it did.  PRXDUPD bit7 is what the display driver reads
         * back, and it draws PACKED pixels once it is set -- so setting
         * the bit without switching the hardware leaves Windows writing
         * one byte per pixel into planar VRAM.  The symptom is a legible
         * logo laid over a fine dashed grid, because the background
         * colour byte 0x07 becomes three lit pixels in every eight.
         */
        crt_analog_ext(true);
        wa_orb(WA_PRXDUPD, 0x80);
        crt = 4;
        master = 3 + (scrn & 3);
        slave = 1;
    } else {
        if ((scrn & 3) >= 2) {
            return 0;
        }
        if (rate & 4) {                         /* 31 kHz */
            crt = 2; master = 2; slave = 4;
        } else if (wa_b(WA_PRXCRT) & 0x40) {    /* 24 kHz */
            crt = 2; master = 1; slave = 2;
        } else {
            crt = 0; master = 0; slave = 0;
        }
        if ((scrn & 0x20) && (wa_b(WA_PRXDUPD) & 0x04)) {
            slave += 1;
        } else {
            crt_analog_ext(false);
            wa_andb(WA_PRXDUPD, (u8)~0x80);
        }
    }
    crt += scrn & 3;

    gdc_sync(false, master_sync[master]);
    crt_area1(0, 0);
    gdc_m_cmd(GDCCMD_PITCH);
    gdc_m_par(TEXT_COLS);

    p = crtdata[crt];
    gdc_m_cmd(GDCCMD_CSRFORM);
    gdc_m_par(rom_b(&p[0]));
    gdc_m_par(0);
    gdc_m_par((u8)((rom_b(&p[0]) << 3) + 3));
    outb(CRTC_PL + 0, rom_b(&p[1]));
    outb(CRTC_PL + 2, rom_b(&p[2]));
    outb(CRTC_PL + 4, rom_b(&p[3]));
    outb(CRTC_PL + 6, 0);
    outb(CRTC_PL + 8, 1);
    outb(CRTC_PL + 10, 0);
    wa_setb(WA_CRT_RASTER, rom_b(&p[0]));

    gdc_sync(true, slave_sync[slave]);
    gdc_s_cmd(GDCCMD_PITCH);
    if (slave & 1) {
        gdc_s_par(TEXT_COLS);
        wa_orb(WA_PRXDUPD, 0x04);
        gdc_s_scroll(0, 0x40);
    } else {
        gdc_s_par(TEXT_COLS / 2);
        wa_andb(WA_PRXDUPD, (u8)~0x04);
        gdc_s_scroll(0, 0);
    }
    if ((scrn & 0x30) == 0x10) {
        gdc_s_scroll(200 * 40, (u8)((slave & 1) ? 0x40 : 0));
    }
    modeff1(4, !((scrn & 0x20) || !(wa_b(WA_PRXCRT) & 0x40)));

    /*
     * The text screen goes OFF across a mode change, as it does on the
     * NEC ROM.  Leaving it on paints the old 400-line text layout over the
     * new raster -- which looks exactly like the framebuffer is being
     * decoded wrongly, and sent one debugging session chasing the
     * packing rules instead of the display enable.
     */
    crt_display(false);

    wa_setb(WA_CRT_BIOS, (u8)((wa_b(WA_CRT_BIOS) & ~3) | ((scrn >> 4) & 3)));
    f = (u8)(wa_b(WA_CRT_STS_FLAG) & ~0x11);
    if (!(scrn & 1)) {
        f |= 0x01;
    }
    if (scrn & 2) {
        f |= 0x10;
    }
    wa_setb(WA_CRT_STS_FLAG, f);
    return 5;
}

/*
 * AH=42h: graphics display mode.  CH bits 7:6 pick the plane layout,
 * bit5 the GRCG/EGC enable and bit4 the displayed page.
 */
void crt_mode42(u8 mode)
{
    static const u8 modenum[4] = { 3, 1, 0, 2 };
    unsigned crtmode = rom_b(&modenum[mode >> 6]);
    u8 prxdupd = wa_b(WA_PRXDUPD);
    u16 scroll = 0;

    if (crtmode == 2) {                         /* 640x400, both planes */
        if ((prxdupd & 0x24) == 0x20) {
            prxdupd ^= 0x04;
            gdc_sync(true, slave_sync[3]);
            gdc_s_cmd(GDCCMD_PITCH);
            gdc_s_par(TEXT_COLS);
            prxdupd |= 0x08;
        }
    } else {
        if ((prxdupd & 0x24) == 0x24) {
            prxdupd ^= 0x04;
            gdc_sync(true, slave_sync[(wa_b(WA_PRXCRT) & 0x40) ? 2 : 0]);
            gdc_s_cmd(GDCCMD_PITCH);
            gdc_s_par(TEXT_COLS / 2);
            prxdupd |= 0x08;
        }
        if (crtmode & 1) {                      /* upper half */
            scroll = 200 * 40;
        }
    }
    gdc_s_scroll(scroll, (u8)((prxdupd & 0x04) ? 0x40 : 0x00));
    wa_setb(WA_PRXDUPD, prxdupd);

    modeff1(4, !(crtmode == 2 || !(wa_b(WA_PRXCRT) & 0x40)));
    if (crtmode != 3) {
        outb(0xa4, (u8)((mode >> 4) & 1));      /* displayed page */
    }
    modeff1(1, (mode & 0x20) != 0);
    modeff2(2, (mode & 0x20) != 0);
}

/*
 * INT 18h AH=16h: fill the text plane.  The attribute half runs to
 * A3FE0 and stops: beyond that are the memory switches, and the
 * measured hazard is that a fill which does not stop sprays the
 * attribute byte over MSW5 and changes the boot device.
 */
void crt_fill(u8 chr, u8 attr)
{
    u16 off;

    for (off = 0; off < 0x2000; off += 2) {
        pokew(SEG_TVRAM, off, chr);
    }
    for (off = 0x2000; off < 0x3fe0; off += 2) {
        pokeb(SEG_TVRAM, off, attr);
    }
}
