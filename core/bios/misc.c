/*
 * The dispatcher and the smaller services: INT 19h, 1Ah, 1Ch, 1Eh, 1Fh
 * and the FDC entry points.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/bregs.h>
#include <pc98/wa.h>
#include <pc98/serial.h>
#include <pc98/romdata.h>
#include <pc98/layout.h>
#include <pc98/pci.h>
#include <pc98/pnp.h>

/*
 * A one-line "the guest asked for something we do not implement" trace.
 * Quiet by default in the sense that it only fires on the paths that
 * return failure -- an unimplemented service that a guest never calls
 * costs nothing, and one it calls every frame would drown the log.
 */
void bios_trace(char tag, u8 ah)
{
    ser_putc('<');
    ser_putc(tag);
    ser_hexb(ah);
    ser_putc('>');
}

/* PIT channel 0 divisor for a 10 ms tick on the 2.4576 MHz family. */
#define PIT_10MS        (2457600 / 100)

void crt_puts_at(u8 row, u8 col, const char *s);

/* ---- the uPD4990 serial calendar clock ---- */

/*
 * The chip is bit-banged through one write-only port (0x20) with its
 * DATA OUT pin routed to bit 0 of the status port at 0x33:
 *   bit 3 STB  -- a RISING edge executes the command in bits 0-2
 *   bit 4 CLK  -- a RISING edge shifts the 52-bit register one place
 *   bit 5      -- serial data IN, sampled by any write with STB and CLK
 *                 both low
 * Command 3 latches the wall clock as BCD; command 1 starts shifting it
 * out, least significant bit of the seconds first.
 *
 * The awkward part is that a write with STB and CLK both low is a DATA
 * write: it stores bit 5 into the register at the current position.  So
 * the clock cannot simply be toggled low between shifts -- that would
 * clear each bit as it was read.  The bit just read is fed back on bit
 * 5 instead, which rewrites it unchanged.  That is how the hardware is
 * meant to be driven; it is a shift register, not a memory.
 *
 * A frozen clock here is not a cosmetic bug.  DOS reads the calendar,
 * checks it (leap year, weekday), writes a correction back with AH=01
 * and re-reads -- and if the value never changes it does that forever,
 * holding the DOS critical section, which on Win9x stops the System VM
 * and freezes the entire user interface while the ring-0 mouse cursor
 * still moves.  That is exactly how this was found.
 */
#define RTC_CMD         0x20
#define RTC_STB         0x08
#define RTC_CLK         0x10
#define RTC_DATA        0x20

static void rtc_out(u8 v)
{
    outb(RTC_CMD, v);
    iodelay();
}

static void rtc_read(u8 *reg)
{
    unsigned i;

    for (i = 0; i < 8; i++) {
        reg[i] = 0;
    }
    /* command 3: latch the wall clock */
    rtc_out(0x03);
    rtc_out(0x03 | RTC_STB);
    rtc_out(0x03);
    /* command 1: begin shifting it out */
    rtc_out(0x01);
    rtc_out(0x01 | RTC_STB);

    for (i = 0; i < 64; i++) {
        u8 bit = (u8)(inb(0x33) & 1);

        if (bit) {
            reg[7 - i / 8] |= (u8)(1 << (i & 7));
        }
        rtc_out((u8)(0x01 | (bit ? RTC_DATA : 0)));      /* feed it back */
        rtc_out(0x01 | RTC_CLK);                         /* shift        */
    }
}

/* ---- INT 1Ch: timer ---- */

static void timer_reload(void)
{
    outb(0x71, (u8)(PIT_10MS & 0xff));
    outb(0x71, (u8)(PIT_10MS >> 8));
    outb(0x02, (u8)(inb(0x02) & ~0x01));        /* unmask IRQ 0 */
}

void bios_int1c(struct bregs *r)
{
    switch (R_AH(r)) {
    case 0x00: {                                /* read the calendar */
        u8 reg[8];
        unsigned i;

        rtc_read(reg);
        /*
         * The register shifts out seconds first, so it lands reversed
         * against the six bytes the caller wants at ES:BX:
         * year, month<<4|weekday, day, hour, minute, second.
         */
        for (i = 0; i < 6; i++) {
            pokeb(r->es, (u16)(r->bx + i), reg[2 + i]);
        }
        break;
    }
    case 0x01:                                  /* set the calendar */
        /*
         * TODO: shift the six bytes back in and issue the time-set
         * command.  MSW8 carries the century/format byte the NEC ROM keeps
         * here, which is what callers read back.
         */
        msw_setb(7, peekb(r->es, r->bx));
        break;
    case 0x02:                                  /* start interval timer */
        pokew(0, 0x001c, r->bx);
        pokew(0, 0x001e, r->es);
        wa_setw(WA_TIMER_COUNT, r->cx);
        outb(0x77, 0x36);
        timer_reload();
        break;
    case 0x03:                                  /* continue */
        timer_reload();
        break;
    default:
        break;
    }
}

/* ---- INT 1Ah: printer / CMT / PCI ---- */

/*
 * The printer half, dispatched on the LOW NIBBLE of AH -- so AH=10h,
 * 20h and 30h all reach function 0.
 *
 * The status byte matters more than it looks: it is port 42h bit 2, and
 * on this machine that bit reads SET, meaning no printer answers.  An
 * earlier version of this file returned 0 ("ready") to every printer
 * call on the theory that a caller's poll should terminate.  It does
 * terminate -- into Windows believing a printer is attached.  The rule
 * this bought, and it applies well beyond the printer: a service we
 * have not implemented must report what the hardware says, not what
 * makes the caller go away.
 */
static void bios_int1a_printer(struct bregs *r)
{
    u8 status = (u8)((inb(0x42) >> 2) & 1);

    switch (R_AH(r) & 0x0f) {
    case 0x00:
        if (R_AH(r) == 0x30) {          /* print a string from ES:BX */
            R_AH(r) = 0x02;             /* nothing to print it on */
            break;
        }
        outb(0x37, 0x0d);               /* printer flip-flop */
        outb(0x46, 0x82);               /* reset            */
        outb(0x46, 0x0f);               /* PSTB inactive    */
        outb(0x37, 0x0c);
        R_AH(r) = status;
        break;
    case 0x01:                          /* print one character */
        R_AH(r) = 0x02;
        break;
    case 0x02:                          /* sense */
        R_AH(r) = status;
        break;
    default:
        R_AH(r) = 0x00;
        break;
    }
}

void bios_int1a(struct bregs *r)
{
    sti();
    if (R_AH(r) == 0xb1 && pci_bios(r)) {
        return;
    }
    if (!(R_AH(r) & 0x10)) {
        /* cassette BIOS: not fitted.  AH=04 (read) errors, rest succeed */
        R_AH(r) = (R_AH(r) == 0x04) ? 0x02 : 0x00;
        return;
    }
    bios_int1a_printer(r);
}

/* ---- INT 1Fh: extended BIOS ---- */

void ext_copy(u32 dst, u32 src, u32 len);

/*
 * AH=90h, the extended-memory block move -- PC-98's answer to the PC/AT
 * INT 15h AH=87h, and the reason HIMEM and EMM386 can stage their page
 * tables above 1 MB at all.  ES:BX points at a descriptor table whose
 * entries at +0x10 (source) and +0x18 (destination) each carry a 16-bit
 * limit and a 24-bit base; SI and DI are offsets into those segments and
 * CX is the byte count, with 0 meaning 65536.
 */
static void int1f_blockmove(struct bregs *r)
{
    u32 src, dst;
    u32 srclimit, dstlimit;
    u32 srcoff = r->si, dstoff = r->di;
    u32 len = ((u32)((r->cx - 1) & 0xffff)) + 1;
    u16 base = (u16)(r->bx + 0x10);

    srclimit = (u32)peekw(r->es, base) + 1;
    src = (u32)peekw(r->es, (u16)(base + 2)) |
          ((u32)peekb(r->es, (u16)(base + 4)) << 16);
    dstlimit = (u32)peekw(r->es, (u16)(base + 8)) + 1;
    dst = (u32)peekw(r->es, (u16)(base + 10)) |
          ((u32)peekb(r->es, (u16)(base + 12)) << 16);

    if (srcoff >= srclimit || dstoff >= dstlimit) {
        set_cf(r, true);
        return;
    }
    if (len > srclimit - srcoff) {
        len = srclimit - srcoff;
    }
    if (len > dstlimit - dstoff) {
        len = dstlimit - dstoff;
    }
    ext_copy(dst + dstoff, src + srcoff, len);
    set_cf(r, false);
}

/* ---- INT 1Fh AH=CEh: the PnP BIOS services ---- */

/*
 * Measured against the NEC firmware with test/pnpdump.asm, which calls
 * each subfunction with its own values in BX/CX/DX and prints what came
 * back:
 *
 *   AL=00  ->  AX=0000 BX=0208 CX=0002 DX=4341   (Xa7)
 *   AL=01  ->  AX=0000 BX=1000 CX=1000 DX=0000
 *
 * and from the boot trace of a real Windows 98 startup, which calls
 *
 *   AL=02  with BX=DF18 DX=063C  ->  AX=0000, caller's buffer untouched
 *   AL=03  with BX=0018 DX=0040  ->  AX=0000
 *
 * AL=02/03 are called once each, very early, by the real-mode PC-98
 * support with a PCI device descriptor in the buffer they name; the
 * firmware answers success and writes nothing back that the caller
 * reads.  So they succeed here and touch nothing -- doing more would be
 * inventing a contract rather than reproducing one.
 *
 * AX=0 is success throughout; the error convention for a subfunction
 * this firmware does not serve has not been measured, so anything else
 * takes the trace-and-carry-set path with the rest of INT 1Fh.
 */
void pnp_ce(struct bregs *r)
{
    const struct pnp_ce *ce = hal_pnp_ce();
    u8 al = R_AL(r);

    if (!ce) {
        bios_trace('P', al);
        set_cf(r, true);
        return;
    }
    /*
     * The table is in the ROM image, so it is at CS and unreachable
     * through DS -- rom_w(), not a plain dereference (romdata.h).  Read
     * plainly it hands back zeros, which the probe duly printed.
     */
    switch (al) {
    case 0x00:
        r->bx = rom_w(&ce->f0_bx);
        r->cx = rom_w(&ce->f0_cx);
        r->dx = rom_w(&ce->f0_dx);
        break;
    case 0x01:
        r->bx = rom_w(&ce->f1_bx);
        r->cx = rom_w(&ce->f1_cx);
        r->dx = rom_w(&ce->f1_dx);
        break;
    case 0x02:
    case 0x03:
        break;                          /* success, nothing written */
    default:
        bios_trace('P', al);
        set_cf(r, true);
        return;
    }
    r->ax = 0x0000;
    set_cf(r, false);
}

void bios_int1f(struct bregs *r)
{
    u8 ah = R_AH(r);

    if (!(ah & 0x80)) {
        return;                         /* not an extended function */
    }
    if (ah == 0x90) {
        int1f_blockmove(r);
        return;
    }
    /*
     * AH=CCh is the PC-98 PCI BIOS, and it is the SAME service as
     * INT 1Ah AH=B1h -- same AL subfunction numbers, same registers,
     * same AH status codes.  Measured from the AIC-7860 expansion ROM,
     * which reaches config space with AX=CC0A (read dword), CC0D
     * (write dword), CC08/CC0B (byte) and CC02 (find by ID), and only
     * falls back to bit-banging 0CF8h when the call comes back with
     * ECX = FFFFFFFF.  Windows uses the INT 1Ah spelling; option ROMs
     * use this one.
     *
     * This must be tested BEFORE the 80h-8Fh block below: 0xCC has bit
     * 4 clear, so the "succeed and do nothing" arm would swallow it and
     * hand the caller whatever was left in ECX.
     */
    if (ah == 0xcc) {
        pci_bios(r);
        return;
    }
    /*
     * AH=CEh is the PnP BIOS, in the spelling the machine actually
     * serves: the $PnP structure's own far-call entry answers 0x82 to
     * every standard function (measured, see core/bios/pnp.S), and this
     * is what Windows and the real-mode PC-98 support call instead.
     * Like AH=CCh it has to be tested before the 80h-8Fh arm below.
     */
    if (ah == 0xce) {
        pnp_ce(r);
        return;
    }
    /*
     * AH=CDh is the chipset window-decode service, and it has to be
     * tested here for the same reason AH=CCh and AH=CEh do: 0xCD has
     * bit 4 clear, so the "succeed and do nothing" arm below would
     * swallow it and hand the caller success plus whatever was left in
     * the registers.  For a query whose whole answer is a register that
     * is the worst of the three possible behaviours -- the caller acts
     * on it and cannot tell.
     *
     * We report "not supported" in the service's own idiom.  Measured
     * from scratch/re/bank0_lo.bin: the AH=CCh/CDh/CEh dispatcher at
     * 0x7a5 routes everything that is not CCh or CEh to 0xa7d, which
     * answers AX=0000h on success and AX=8000h on a request it cannot
     * serve, and the dispatcher copies the handler's carry into the
     * IRET frame's flags.
     *
     * The contract is fully decoded and written up in notes.md
     * ("Deliberate gaps", item 6); it is not implemented here because
     * it has no measured consumer and because the register it reads is
     * per-machine (host-bridge cfg 0x66/0x67 on the Xa7, 0x70-0x73 on
     * the RvII26's Champion), which needs a HAL hook and a measurement
     * on a machine we cannot boot the probe on yet.
     */
    if (ah == 0xcd) {
        r->ax = 0x8000;
        set_cf(r, true);
        return;
    }
    if (!(ah & 0x10)) {
        set_cf(r, false);               /* 80h-8Fh: succeed */
        return;
    }
    bios_trace('F', ah);
    set_cf(r, true);
}

/* ---- INT 19h: RS-232C ---- */

static void bios_int19(struct bregs *r)
{
    /* No 8251 channel is wired on this machine; report an idle line. */
    R_AH(r) = 0x00;
}

/* ---- INT 1Eh: no system ---- */

static const char nosys_msg[] =
    "No system disk.  Insert a system disk and reset.";

static void bios_nosys(void)
{
    crt_puts_at(10, 10, nosys_msg);
    ser_puts("\r\nBOOT: no system\r\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* ---- dispatch ---- */

void bios_dispatch(struct bregs *r, u16 svc)
{
    switch (svc) {
    case SVC_KBD:
        bios_irq_kbd(r);
        break;
    case SVC_INT18:
        bios_int18(r);
        break;
    case SVC_INT19:
        bios_int19(r);
        break;
    case SVC_INT1A:
        bios_int1a(r);
        break;
    case SVC_INT1B:
        bios_int1b(r);
        break;
    case SVC_INT1C:
        bios_int1c(r);
        break;
    case SVC_INT1F:
        bios_int1f(r);
        break;
    case SVC_INT1E:
        bios_nosys();
        break;
    case SVC_FDC1:
    case SVC_FDC2:
        R_AH(r) = 0x60;                 /* no drive fitted */
        set_cf(r, true);
        break;
    default:
        break;
    }
}
