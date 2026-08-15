/*
 * INT 18h — CRT and keyboard BIOS, plus the IRQ 1 keyboard handler.
 *
 * The keyboard half is a scancode translator feeding a 16-entry ring in
 * the work area at 0x0502.  Its tables and the ring's field layout are
 * published interface: DOS input methods read WA_KB_CODE_* to find the
 * live translate table, so the shape has to match even though the
 * addresses are ours.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/bregs.h>
#include <pc98/wa.h>
#include <pc98/gdc.h>
#include <pc98/layout.h>
#include <pc98/romdata.h>

#define KB_DATA         0x41
#define KB_STATUS       0x43
#define KB_CMD          0x43

/* The ring is a work-area field; wa.h owns its address and its size. */
#define KB_RING_BASE    WA_KB_BUF
#define KB_RING_END     (WA_KB_BUF + WA_KB_BUF_ENTRIES * 2)

extern const u8 keytable[8][0x60];

/* ---- key ring ---- */

static u16 key_get(void)
{
    u16 head, code;

    if (!wa_b(WA_KB_COUNT)) {
        return 0xffff;
    }
    head = wa_w(WA_KB_BUF_HEAD);
    code = wa_w(head);
    head += 2;
    if (head >= KB_RING_END) {
        head = KB_RING_BASE;
    }
    wa_setw(WA_KB_BUF_HEAD, head);
    wa_setb(WA_KB_COUNT, (u8)(wa_b(WA_KB_COUNT) - 1));
    return code;
}

static void key_put(u16 code)
{
    u16 tail;

    if (wa_b(WA_KB_COUNT) >= WA_KB_BUF_ENTRIES) {
        return;
    }
    tail = wa_w(WA_KB_BUF_TAIL);
    wa_setw(tail, code);
    tail += 2;
    if (tail >= KB_RING_END) {
        tail = KB_RING_BASE;
    }
    wa_setw(WA_KB_BUF_TAIL, tail);
    wa_setb(WA_KB_COUNT, (u8)(wa_b(WA_KB_COUNT) + 1));
}

/*
 * Pick the translate table the current shift state selects and publish
 * its offset.  The LED state rides along in MSW6, as on the NEC ROM.
 */
static void kb_update_shift(void)
{
    u8 sts = wa_b(WA_SHIFT_STS);
    unsigned base;

    msw_setb(5, (u8)((msw_b(5) & 0x3f) | (u8)(sts << 5)));
    if (sts & 0x10) {
        base = 7;                       /* graph */
    } else if (sts & 0x08) {
        base = 6;                       /* ctrl  */
    } else {
        base = sts & 7;
        if (base >= 6) {
            base -= 2;
        }
    }
    wa_setw(WA_KB_SHIFT_TBL, (u16)((u16)(unsigned)keytable + base * 0x60));
}

void kb_init(void)
{
    outb(KB_CMD, 0x3a);                 /* reset high */
    outb(KB_CMD, 0x32);                 /* reset low  */
    outb(KB_CMD, 0x16);                 /* clear the error latch */
    for (u16 o = KB_RING_BASE; o < KB_RING_END; o += 2) {
        wa_setw(o, 0);
    }
    /* the ring bookkeeping and the per-key state, up to and including
     * the shift state at WA_SHIFT_STS */
    for (u16 o = WA_KB_COUNT; o <= WA_SHIFT_STS; o++) {
        wa_setb(o, 0);
    }
    wa_setw(WA_KB_BUF_HEAD, KB_RING_BASE);
    wa_setw(WA_KB_BUF_TAIL, KB_RING_BASE);
    wa_setw(WA_KB_CODE_OFF, (u16)(unsigned)keytable);
    wa_setw(WA_KB_CODE_SEG, SEG_BIOS);
    kb_update_shift();
}

/* IRQ 1: one scancode, translated into the ring. */
void bios_irq_kbd(struct bregs *r)
{
    u8 key, sts;
    unsigned pos;
    u8 bit;
    u16 code = 0xffff;

    sts = inb(KB_STATUS);
    if (sts & 0x38) {                   /* receive error: ask again */
        if (wa_b(WA_KB_RETRY) < 3) {
            wa_setb(WA_KB_RETRY, (u8)(wa_b(WA_KB_RETRY) + 1));
            outb(KB_CMD, 0x14);
        }
        (void)inb(KB_DATA);
        outb(0x00, 0x20);               /* EOI */
        return;
    }
    outb(KB_CMD, 0x16);
    wa_setb(WA_KB_RETRY, 0);
    key = inb(KB_DATA);
    outb(0x00, 0x20);                   /* EOI master */

    pos = (unsigned)((key & 0x7f) >> 3);
    bit = (u8)(1 << (key & 7));

    if (key & 0x80) {                   /* break */
        wa_setb((u16)(WA_KB_KY_STS + pos),
                (u8)(wa_b((u16)(WA_KB_KY_STS + pos)) & ~bit));
        if ((key >= 0xf0 && key < 0xf5) || key == 0xfd) {
            if (key == 0xf0 || key == 0xfd) {
                wa_andb(WA_SHIFT_STS, (u8)~0x01);
            } else {
                wa_andb(WA_SHIFT_STS, (u8)~bit);
            }
            kb_update_shift();
        }
        return;
    }

    wa_setb((u16)(WA_KB_KY_STS + pos),
            (u8)(wa_b((u16)(WA_KB_KY_STS + pos)) | bit));

    if (key <= 0x51) {
        const u8 *tbl = (const u8 *)(unsigned)wa_w(WA_KB_SHIFT_TBL);
        u8 v = rom_b(tbl + key);

        if (key == 0x51 || key == 0x35 || key == 0x3e) {
            code = (v == 0xff) ? 0xffff : (u16)(v << 8);
        } else if (v != 0xff) {
            code = (u16)((key << 8) | v);
        }
    } else if (key == 0x5e) {
        code = 0xae00;                  /* HOME */
    } else if (key >= 0x62 && key < 0x70) {
        const u8 *tbl = (const u8 *)(unsigned)wa_w(WA_KB_SHIFT_TBL);
        u8 v = rom_b(tbl + key - 0x0c);

        code = (v == 0xff) ? 0xffff : (u16)(v << 8);
    } else if (key == 0x70 || key == 0x7d) {
        wa_orb(WA_SHIFT_STS, 0x01);
        kb_update_shift();
    } else if (key < 0x75) {
        wa_orb(WA_SHIFT_STS, bit);
        kb_update_shift();
    }

    if (code != 0xffff) {
        key_put(code);
    }
}

/* ---- INT 18h ---- */

void bios_int18(struct bregs *r)
{
    u8 ah = R_AH(r);

    if (ah < 0x40) {
        sti();
    }
    switch (ah) {
    case 0x00:                          /* blocking key read */
        while (!wa_b(WA_KB_COUNT)) {
            __asm__ volatile("sti; hlt");
        }
        r->ax = key_get();
        break;
    case 0x01:                          /* key buffer sense */
        if (wa_b(WA_KB_COUNT)) {
            r->ax = wa_w(wa_w(WA_KB_BUF_HEAD));
            R_BH(r) = 1;
        } else {
            R_BH(r) = 0;
        }
        break;
    case 0x02:
        R_AL(r) = wa_b(WA_SHIFT_STS);
        break;
    case 0x03:
        kb_init();
        break;
    case 0x04:
        R_AH(r) = wa_b((u16)(WA_KB_KY_STS + (R_AL(r) & 0x0f)));
        break;
    case 0x05:                          /* non-blocking key read */
        if (wa_b(WA_KB_COUNT)) {
            r->ax = key_get();
            R_BH(r) = 1;
        } else {
            R_BH(r) = 0;
        }
        break;
    case 0x0a:
        crt_mode_init(R_AL(r));
        break;
    case 0x0b:
        R_AL(r) = wa_b(WA_CRT_STS_FLAG);
        break;
    case 0x0c:
        crt_display(true);
        break;
    case 0x0d:
        crt_display(false);
        break;
    case 0x0e:                          /* single display area */
        crt_area1((u16)(r->dx >> 1),
                  (u16)((wa_b(WA_CRT_STS_FLAG) & 0x80) ? 400 : 200));
        wa_setw(WA_CRT_W_VRAMADR, (u16)(r->dx >> 1));
        break;
    case 0x10:                          /* cursor form */
        wa_setb(WA_CRT_CNT, (u8)((R_AL(r) & 1) << 5));
        crt_cursor(false);
        break;
    case 0x11:
        crt_cursor(true);
        break;
    case 0x12:
        crt_cursor(false);
        break;
    case 0x13:
        crt_cursor_pos((u16)(r->dx >> 1));
        break;
    case 0x16:
        crt_fill(R_DL(r), R_DH(r));
        break;
    case 0x17:                          /* buzzer on  */
        outb(0x37, 0x06);
        break;
    case 0x18:                          /* buzzer off */
        outb(0x37, 0x07);
        break;
    case 0x1b:                          /* kanji code access mode */
        if (R_AL(r) == 0) {
            wa_andb(WA_CRT_STS_FLAG, (u8)~0x08);
            modeff1(MODE1_KAC, false);
        } else if (R_AL(r) == 1) {
            wa_orb(WA_CRT_STS_FLAG, 0x08);
            modeff1(MODE1_KAC, true);
        }
        break;
    case 0x30:                          /* set display mode */
        if (wa_b(WA_CRT_BIOS) & 0x80) {
            u8 got = crt_mode30(R_AL(r), R_BH(r));

            R_AH(r) = got;
            R_AL(r) = (got == 0x05) ? 0 : 1;
            R_BH(r) = (got == 0x05) ? 0 : 1;
        }
        break;
    case 0x42:
        crt_mode42(R_CH(r));
        break;
    case 0x31:                          /* get display mode */
        /*
         * The AH=30h pair, read back: AL is the "rate" byte and BH the
         * "scrn" byte, in the encoding crt_mode30() accepts.  Both have
         * to describe the mode the machine is ACTUALLY in, because
         * Windows 98 reads this pair at startup and, when it differs
         * from the mode it means to run, sets it with AH=30h and
         * records the change -- which is the pre-shell
         * "新しいディスプレイの設定 ... restart" dialog, and (via that
         * restart) the reboot hang.  Returning a hardcoded AL=08 with
         * BH from CRT_BIOS bits 0-1 (zero until something calls AH=30h)
         * reported "no 24 kHz CRT, 200-line" on a machine sitting in
         * 400-line text on a 24 kHz CRT.  The NEC ROM answers 09/21
         * here; so does this, from the same state it already keeps.
         *
         * Bit0 of both bytes tracks the 24 kHz CRT, BH bit5 the 400-line
         * text mode and BH bit4 the extended-analogue (480-line) mode --
         * bits 5:4 both set is what crt_mode30() reads as 640x480, so
         * the two directions agree.  Measured against the NEC ROM in
         * one state only (the boot state); the other combinations want
         * a self-test cell before they are trusted.
         */
        if (wa_b(WA_CRT_BIOS) & 0x80) {
            u8 rate = 0x08;
            u8 scrn = 0;

            if (wa_b(WA_PRXCRT) & 0x40) {       /* 24 kHz CRT       */
                rate |= 0x01;
                scrn |= 0x01;
            }
            if (wa_b(WA_CRT_STS_FLAG) & 0x80) { /* 400-line text    */
                scrn |= 0x20;
            }
            if (wa_b(WA_PRXDUPD) & 0x80) {      /* extended analogue */
                scrn |= 0x10;
            }
            R_AL(r) = rate;
            R_BH(r) = scrn;
        }
        break;
    case 0x40:                          /* graphics display on  */
        gdc_s_cmd(GDCCMD_BCTRL_ON);
        wa_orb(WA_PRXCRT, 0x80);
        break;
    case 0x41:                          /* graphics display off */
        gdc_s_cmd(GDCCMD_BCTRL_OFF);
        wa_andb(WA_PRXCRT, (u8)~0x80);
        break;
    default:
        bios_trace('8', ah);
        break;
    }
}
