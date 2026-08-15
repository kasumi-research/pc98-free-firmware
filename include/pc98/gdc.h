/*
 * uPD7220 GDC, CRTC and the display mode flip-flops.
 *
 * Two GDCs: the master drives the text plane (ports 60h-6Eh even), the
 * slave the graphics planes (A0h-AEh even).  The CRTC's text-layout
 * registers share the 70h-7Eh block with the GRCG.
 *
 * The uPD7220 command opcodes and the mode flip-flop bit numbers follow
 * Neko Project 21/W (io/gdc_cmd.h, io/gdc.c), which is BSD 3-Clause:
 *
 *   Copyright (c) 1999-2025, NP2 developer team.  All rights reserved.
 *
 * These are hardware constants and the attribution is precautionary,
 * but it costs nothing to be exact.  The full licence text and its
 * conditions -- which bind BINARY redistribution of a built ROM as well
 * as source -- are in NOTICE at the top of this tree.
 *
 * SPDX-License-Identifier: MIT AND BSD-3-Clause
 */
#ifndef PC98_GDC_H
#define PC98_GDC_H

#include <pc98/types.h>
#include <pc98/io.h>

#define GDC_M_PARAM     0x60
#define GDC_M_CMD       0x62
#define GDC_MODEFF1     0x68        /* bit set/reset: (bit << 1) | on   */
#define GDC_MODEFF2     0x6a
#define GDC_15KHZ       0x6e
#define GDC_S_PARAM     0xa0
#define GDC_S_CMD       0xa2

#define CRTC_PL         0x70        /* pl bl cl ssl sur sdr, 2 apart    */
#define CRTC_GRCG_MODE  0x7c
#define CRTC_GRCG_TILE  0x7e

/* GDC commands */
#define GDCCMD_RESET    0x00
#define GDCCMD_SYNC     0x0e        /* +1 = display on                  */
#define GDCCMD_BCTRL_ON 0x0d
#define GDCCMD_BCTRL_OFF 0x0c
#define GDCCMD_ZOOM     0x46
#define GDCCMD_PITCH    0x47
#define GDCCMD_CSRW     0x49
#define GDCCMD_CSRFORM  0x4b
#define GDCCMD_START    0x6b
#define GDCCMD_PRAM     0x70        /* +n: load parameter RAM from n    */

/* mode flip-flop 1 bits (write (bit << 1) | on to GDC_MODEFF1) */
#define MODE1_ATTR      0           /* colour attributes                */
#define MODE1_40COL     2
#define MODE1_400LINE   3
#define MODE1_KAC       5           /* kanji code access                */
#define MODE1_MSW       6           /* memory switches writable         */

static inline void modeff1(unsigned bit, bool on)
{
    outb(GDC_MODEFF1, (u8)((bit << 1) | (on ? 1 : 0)));
}

static inline void modeff2(unsigned bit, bool on)
{
    outb(GDC_MODEFF2, (u8)((bit << 1) | (on ? 1 : 0)));
}

static inline void gdc_m_cmd(u8 c)   { outb(GDC_M_CMD, c); }
static inline void gdc_m_par(u8 p)   { outb(GDC_M_PARAM, p); }
static inline void gdc_s_cmd(u8 c)   { outb(GDC_S_CMD, c); }
static inline void gdc_s_par(u8 p)   { outb(GDC_S_PARAM, p); }

void crt_analog_ext(bool on);
void crt_analog_init(void);
void crt_mode_init(u8 mode);
void crt_fill(u8 chr, u8 attr);
void crt_display(bool on);
void crt_cursor(bool on);
void crt_cursor_pos(u16 addr);
void crt_area1(u16 addr, u16 raster);
u8 crt_mode30(u8 rate, u8 scrn);
void crt_mode42(u8 mode);

#endif
