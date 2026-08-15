/*
 * Runtime BIOS — the cold path: work area, IVT, screen, disk, boot.
 *
 * Entered from entry.S once there is a stack.  Every value seeded here
 * was read out of a booted PC-9821Xa7 running the NEC ROM, at the
 * instant it handed control to the boot sector; test/wadump.asm is the
 * boot sector that prints them.  Where a field holds a pointer into the
 * firmware it is replaced by the equivalent address in ours -- the
 * pointer is interface, its value is not.
 *
 * Reminder (bregs.h): the BIOS has no writable segment.  DS here is 0,
 * so the work area is reachable, but .rodata is at CS and every read of
 * it goes through rom_*.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/serial.h>
#include <pc98/layout.h>
#include <pc98/model.h>
#include <pc98/romdata.h>
#include <pc98/wa.h>
#include <pc98/gdc.h>
#include <pc98/ide.h>
#include <pc98/hwinit.h>
#include <pc98/sdip.h>
#include <pc98/optrom.h>
#include <pc98/bregs.h>

extern const u16 ivt_template[32];
void bios_notfitted(void);

void kb_init(void);
void boot_enter(u16 daua);
u16 disk_boot_read(u16 daua, u16 seg, u16 bytes);

/* ---- text output for the BIOS's own messages ---- */

void crt_puts_at(u8 row, u8 col, const char *s)
{
    u16 off = (u16)((row * TEXT_COLS + col) * 2);
    char c;

    while ((c = (char)rom_b(s++)) != 0 && off < TEXT_COLS * 25 * 2) {
        pokew(SEG_TVRAM, off, (u8)c);
        pokeb(SEG_TVRAM, (u16)(TVRAM_ATTR + off), TXTATR_WHITE);
        off += 2;
    }
}

/* ---- IVT ---- */

static void ivt_install(void)
{
    unsigned i;

    for (i = 0; i < 32; i++) {
        pokew(0, (u16)(i * 4), rom_w(&ivt_template[i]));
        pokew(0, (u16)(i * 4 + 2), SEG_BIOS);
    }
}

/* ---- memory switches ---- */

/*
 * Defaults measured on a real Xa7.  MSW5's high nibble is the
 * boot-device selector: Ah = fixed disk.  A machine running the NEC ROM
 * needs Ah here to boot from its fixed disk -- the "normal" setting
 * never reaches one, because device class 06h answers AX=1004h forever
 * and the walk restarts rather than moving on.  Our own walk does not
 * behave that way, but the switches stay compatible.
 */
static const u8 msw_default[8] = {
    0x48, 0x05, 0x0c, 0x00, 0xa1, 0x00, 0x00, 0x41
};

static void msw_seed(void)
{
    unsigned i;
    bool valid = false;

    /*
     * Battery-backed, so normally there is nothing to do.  All eight
     * switches reading 0xFF is the signature of a machine that has
     * never been set up (or whose battery is flat); anything else is
     * the user's configuration and must be left alone.
     */
    for (i = 0; i < 8; i++) {
        if (msw_b(i) != 0xff) {
            valid = true;
        }
    }
    if (valid) {
        return;
    }
    msw_unlock();
    for (i = 0; i < 8; i++) {
        pokeb(MSW_SEG, (u16)MSW_OFF(i), rom_b(&msw_default[i]));
    }
    msw_lock();
}

/* ---- work area ---- */

/*
 * Extended memory sizing.
 *
 * Two published fields, and they do not overlap: 0x0401 counts the
 * megabytes from 1 MiB in 128 KiB units and saturates at 15 (0x78),
 * while 0x0594 is a WORD counting megabytes above 16 MiB.  The
 * megabyte between them is the PC-98 system space and belongs to
 * neither.  Measured on a 512 MiB RvII26 running the NEC ROM:
 * 0x0401 = 0x78 and 0x0594 = 0x01F0 = 496, and 496 + 16 = 512.
 *
 * Reporting the 14 MiB placeholder these fields used to carry is not a
 * cosmetic error on a machine with half a gigabyte: an OS that sizes
 * memory through the firmware gets a machine it cannot run in.
 *
 * The two ranges are probed SEPARATELY, and that is not tidiness: on
 * the Xa7 the 15-16 MiB system space really is a hole, so a single scan
 * upward from 1 MiB stops at 15 and never sees the 32 MiB above it.
 * Reporting 15 MiB below 16 on that machine is not a rounding error
 * either -- HIMEM.SYS tests every megabyte the field claims, finds the
 * hole, prints "unreliable XMS memory at 00F60002h" and refuses to
 * load.  The RvII26 is contiguous and reports the full 15.
 *
 * The probe walks upward a megabyte at a time.  A binary search would
 * be quicker and would be wrong: it assumes DRAM is contiguous, and the
 * shape of the memory is the one thing a memory probe must not assume.
 */
#define MEM_PROBE_TOP   4096u           /* stop at 4 GiB regardless */

u32 ext_probe(u32 addr);

/*
 * Extended memory below the 15-16 MiB system space, in 128 KiB units --
 * which is the unit 0x0401 is expressed in, and the granularity this
 * has to be probed at.  A megabyte-granular scan rounds UP to whatever
 * megabyte the last live byte falls in, and the field then promises
 * memory that is not there; HIMEM.SYS tests every unit it is promised.
 *
 * The decoy write for the first probe lands at physical 0.  This runs
 * before the IVT is installed, so those four bytes are nobody's yet.
 */
#define MEM_UNIT        0x20000u        /* 128 KiB */
#define MEM_UNITS_MAX   120u            /* 1 MiB up to the 16 MiB space */

static u32 mem_low_units(void)
{
    u32 n = 0;

    while (n < MEM_UNITS_MAX && ext_probe(0x100000u + n * MEM_UNIT)) {
        n++;
    }
    return n;
}

/* Memory above 16 MiB, in MiB. */
static u32 mem_high_mb(void)
{
    u32 mb = 16;

    while (mb < MEM_PROBE_TOP && ext_probe(mb << 20)) {
        mb++;
    }
    return mb - 16;
}

static void wa_init(void)
{
    unsigned i;
    u32 low = mem_low_units();
    u32 high = mem_high_mb();

    for (i = 0x0400; i < 0x0600; i++) {
        wa_setb((u16)i, 0);
    }

    wa_setb(0x0400, 0x02);
    wa_setb(WA_EXPMMSZ, (u8)low);
    wa_setw(WA_EXTMMSZ, (u16)high);
    ser_puts("BIOS: memory ");
    ser_hexl(low);
    ser_puts(" x 128K below 16, ");
    ser_hexl(high);
    ser_puts(" MiB above\r\n");

    /* the warm-return SS:SP the CPU-reset dance restores from */
    wa_setw(WA_WARM_SP, 0x01f8);
    wa_setw(WA_WARM_SS, 0x0020);

    /* ROM/BIOS capability block, as measured.  +2 stays zero. */
    wa_setb(WA_ROMCAP + 0, 0x97);
    wa_setb(WA_ROMCAP + 1, 0x02);
    wa_setb(WA_ROMCAP + 3, 0xa4);
    wa_setb(WA_ROMCAP + 4, 0x84);
    wa_setb(WA_ROMCAP + 5, 0x40);
    wa_setb(WA_ROMCAP + 6, 0x48);

    /*
     * 0x480: CPU/equipment flags.  0x53 here; the disk scan ORs in
     * bit7 ("fixed disk fitted") once a unit answers, which is what
     * makes a booted Xa7 read 0xD3.  bit1 = 286-or-better, which the
     * reset dance keys on.
     */
    wa_setb(WA_SYS_TYPE, 0x53);
    /*
     * 0x0481 is KEYB_TYPE (Undocumented 9801), and bits 6 and 3
     * together say which keyboard is attached:
     *
     *      bit6 bit3
     *        1    1   new keyboard, NUM key, DIP2-7 off
     *        0    1   new keyboard, NUM key, DIP2-7 on
     *        1    0   new keyboard, no NUM key   <- a desktop 9821
     *        0    0   OLD keyboard, or none attached at reset
     *
     * "New" means the CAPS and kana locks can be driven from software,
     * and the documentation is explicit that the BIOS of a
     * new-keyboard-capable machine -- every i80286-or-later model bar a
     * handful of pre-9821 ones -- probes the keyboard at reset and sets
     * this.  Both our machines are 286+ desktops, so bit 6 set and bit
     * 3 clear, which is what a real RvII26 reads back.
     *
     * This byte is why Windows 2000 had no keyboard.  We used to write
     * 0x02 here, copied from an Xa7 capture without decoding it: bits 6
     * and 3 clear is the "old keyboard, or none" row, so NTDETECT
     * reported a keyboard Windows has no driver for, the devnode never
     * came present, and IOAPIC pin 1 was left masked at vector 255 for
     * the life of the boot.  The mouse bound normally, which is what
     * made it look like a keyboard-specific emulator bug.
     *
     * Bits 1 and 0 are not keyboard at all -- they are the SASI/IDE
     * HD #1/#0 drive mode, and the IDE scan fills them in.
     */
    wa_setb(WA_BIOS_FLAG3, 0x40);
    /*
     * 0x0484 and 0x0543 are measured residue whose fields we have not
     * identified.  They are reproduced rather than left zero because
     * the work area is published: a caller that reads one of them gets
     * what a PC-98 puts there, and a byte we cannot name is not a byte
     * we can safely invent.
     */
    wa_setb(0x0484, 0x0e);
    wa_setw(WA_SAVED_DX, 0x0543);

    /* C-bus option-ROM scan residue; the boot load segment is the last */
    wa_setw(WA_SCAN_PHASE, 0x0000);
    wa_setw(WA_SCAN_SEG, 0x1fc0);

    wa_setb(WA_BIOS_FLAG0, 0x03);
    /*
     * 0x501: bits 0-2 are the conventional memory size in 128 KiB units
     * minus one -- 4 = 640 KiB.  bit5 is set on every 286+ machine.
     */
    wa_setb(WA_BIOS_FLAG1, 0x24);

    wa_setb(WA_CRT_RASTER, 0x0f);
    wa_setb(WA_CRT_STS_FLAG, 0x84);
    /*
     * PRXCRT/PRXDUPD: display capability bits DOS and the Windows
     * display driver both read.  0x4F/0x70 measured; bit6 of PRXCRT is
     * "24 kHz high-resolution CRT", bit2 of PRXDUPD "both graphics
     * planes at 640x400".
     */
    wa_setb(WA_PRXCRT, 0x4f);
    wa_setb(WA_PRXDUPD, 0x70);
    wa_setb(WA_CRT_BIOS, 0x84);
    /*
     * RS-232C parameter residue, measured.  Windows' serial driver
     * reads these; zeros there and it decides a port exists that does
     * not answer.
     */
    wa_setb(WA_RS_PARAM + 0, 0xf3);
    wa_setb(WA_RS_PARAM + 1, 0x6d);
    wa_setb(WA_RS_PARAM + 2, 0xcb);
    wa_setb(WA_F144_SUP, 0x77);
    /* more measured residue we have not named; see the note above */
    wa_setb(0x05a4, 0xff);
    wa_setb(0x05b0, 0xff);
    wa_setb(0x05b3, 0x80);
    /* per-unit disk result blocks: unit tag in the first byte */
    for (i = 0; i < 4; i++) {
        wa_setb((u16)(WA_DISK_RESULT + i * 8), (u8)(0x78 + i));
    }
    wa_setb(WA_F2HD_MODE, 0xff);
    wa_setb(WA_F2DD_MODE, 0xff);
    wa_setb(0x05cb, 0x96);              /* unnamed residue, as above */

    /*
     * Far pointers the OS may call.  On a machine running the NEC ROM
     * they are filled in even with no drive attached; ours point at a
     * stub that reports "not fitted" rather than at 0000:0000.
     */
    wa_setd(WA_F2DD_POINTER, SEG_BIOS, (u16)(unsigned)bios_notfitted);
    wa_setd(WA_F2HD_POINTER, SEG_BIOS, (u16)(unsigned)bios_notfitted);
    wa_setd(WA_DISK_POINTER0, SEG_BIOS, (u16)(unsigned)bios_notfitted);
    wa_setd(WA_DISK_POINTER1, SEG_BIOS, (u16)(unsigned)bios_notfitted);
}

/* ---- boot ---- */

#define BOOT_SEG    0x1fc0
#define BOOT_BYTES  0x400

/*
 * One candidate device.  The read goes through INT 1Bh rather than
 * straight to the transport, because on a machine whose disk hangs off
 * an expansion ROM the vector is the only way in -- and doing it the
 * same way for a device we drive ourselves keeps one boot path instead
 * of two.
 *
 * A sector that comes back is accepted whatever it contains.  A PC-98
 * IPL ends 55 AA and tags "IPL1" at offset 4, but a boot sector we do
 * not recognise is still the user's boot sector: refusing it would be
 * our bug, not theirs.
 */
static bool boot_try(u16 daua)
{
    if (disk_boot_read(daua, BOOT_SEG, BOOT_BYTES) != 0) {
        return false;
    }
    wa_setb(WA_DISK_BOOT, (u8)daua);
    ser_puts("BOOT: DA/UA ");
    ser_hexb((u8)daua);
    ser_puts(" -> 1fc0:0000\r\n");
    ser_mark('!');
    boot_enter(daua);
    return true;                        /* boot_enter does not return */
}

/*
 * The boot walk.  This deliberately does NOT reproduce the
 * behaviour described at msw_default above, where the "normal" MSW5
 * order never reaches the fixed disk.  Ours simply tries every unit
 * that answered a scan, in scan order.
 *
 * SCSI first, then IDE: a machine with both is a machine whose system
 * disk is the SCSI one (the RvII26 has no IDE channel at all), and the
 * SCSI equipment bitmap at 0x0482 is written by whichever module
 * claimed the bus -- for us, the AIC-7860's expansion ROM during
 * optrom_scan().
 */
static void boot(void)
{
    unsigned unit;
    u8 scsi = wa_b(WA_DISK_EQUIPS);

    for (unit = 0; unit < 8; unit++) {
        if (scsi & (1u << unit)) {
            boot_try((u16)(0xa0 + unit));
        }
    }
    for (unit = 0; unit < IDE_UNITS; unit++) {
        if (ide_present(unit)) {
            boot_try((u16)(0x80 + unit));
        }
    }
    __asm__ volatile("int $0x1e");
}

/* ---- entry ---- */

void __attribute__((noreturn)) bios_cold(void)
{
    ser_init();
    ser_puts("\r\nBIOS: cold entry, " MODEL_NAME "\r\n");

    hw_init();
    sdip_init();                /* CPU-only reset: the ITF never reran */
    msw_seed();
    wa_init();
    ivt_install();
    kb_init();

    crt_analog_init();
    crt_mode_init(0x04);
    crt_fill(' ', TXTATR_WHITE);
    crt_display(true);
    crt_puts_at(0, 0, "PC-9821 free firmware (" MODEL_NAME ")");
    crt_puts_at(1, 0, "(c) 2026 Kasumigaseki Pte. Ltd.");

    ser_puts("BIOS: work area, IVT, screen up\r\n");

    /*
     * Interrupts come on before the expansion ROMs run: they are third
     * party code that may wait on a timer tick, and the IVT and work
     * area they need are both up by now.
     */
    sti();
    optrom_scan();
    ide_scan();
    boot();

    halt_forever();
}
