/*
 * INT 1Bh — DISK BIOS, and the unit scan that feeds it.
 *
 * Call convention (np21w's SXSI disk BIOS, and the emulator's model of
 * it -- the interface, not its implementation; see NOTICE):
 *   AH  function, low nibble; AL  DA/UA -- high nibble = device class,
 *       bit7 additionally selects CHS addressing, low bits = unit
 *   BX  byte count (0 = 65536)
 *   ES:BP  buffer
 *   CHS: CX = cylinder, DH = head, DL = sector
 *   LBA: DL:CX = linear sector (DX:CX once the medium is > 24 bits)
 * Returns AH = status, CF = (status >= 0x20).
 *
 * Device classes: 0x00/0x80 SASI-and-IDE, 0x20/0xA0 SCSI, the odd
 * classes are floppy.  Only the IDE classes are handled here; anything
 * else reports "no device" rather than pretending.  On a model whose
 * disk hangs off an expansion ROM, that module claims its own classes
 * ahead of us and this handler never sees them (core/bios/vectors.S).
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/bregs.h>
#include <pc98/wa.h>
#include <pc98/ide.h>
#include <pc98/serial.h>

/* A scratch sector for IDENTIFY.  The boot sector's landing area at
 * 1FC0:0000 is free until the very last step of the boot, and it is
 * the only KiB of RAM the BIOS can borrow without owning it. */
#define SCRATCH_SEG     0x1fc0

static void geo_set(unsigned unit, u8 sectors, u8 heads, u16 cyls, u32 total)
{
    u16 base = (u16)IDEGEO(unit);

    pokeb(0, (u16)(base + IDEGEO_SECTORS), sectors);
    pokeb(0, (u16)(base + IDEGEO_HEADS), heads);
    pokew(0, (u16)(base + IDEGEO_CYLS), cyls);
    pokew(0, (u16)(base + IDEGEO_TOTAL), (u16)total);
    pokeb(0, (u16)(base + IDEGEO_TOTAL + 2), (u8)(total >> 16));
    pokeb(0, (u16)(base + IDEGEO_PRESENT), 1);
}

static u8 geo_sectors(unsigned unit)
{
    return peekb(0, (u16)(IDEGEO(unit) + IDEGEO_SECTORS));
}

static u8 geo_heads(unsigned unit)
{
    return peekb(0, (u16)(IDEGEO(unit) + IDEGEO_HEADS));
}

static u16 geo_cyls(unsigned unit)
{
    return peekw(0, (u16)(IDEGEO(unit) + IDEGEO_CYLS));
}

static u32 geo_total(unsigned unit)
{
    u16 base = (u16)IDEGEO(unit);

    return peekw(0, (u16)(base + IDEGEO_TOTAL)) |
           ((u32)peekb(0, (u16)(base + IDEGEO_TOTAL + 2)) << 16);
}

/*
 * Probe the four possible units (two channels x master/slave) and cache
 * what answers.  The PC-98 unit numbering the DA/UA byte uses is the
 * order of the scan, which is also the order the boot walk tries.
 */
void ide_scan(void)
{
    unsigned unit;
    u16 equip = 0;

    for (unit = 0; unit < IDE_UNITS; unit++) {
        u16 cyls, heads, sectors;
        u32 total;

        pokeb(0, (u16)(IDEGEO(unit) + IDEGEO_PRESENT), 0);
        if (!ide_identify(unit, SCRATCH_SEG, 0)) {
            continue;
        }
        cyls    = peekw(SCRATCH_SEG, 1 * 2);
        heads   = peekw(SCRATCH_SEG, 3 * 2);
        sectors = peekw(SCRATCH_SEG, 6 * 2);
        total   = (u32)peekw(SCRATCH_SEG, 60 * 2) |
                  ((u32)peekw(SCRATCH_SEG, 61 * 2) << 16);
        if (!sectors || !heads) {
            continue;
        }
        if (!total) {
            total = (u32)cyls * heads * sectors;
        }
        geo_set(unit, (u8)sectors, (u8)heads, cyls, total);
        equip |= (u16)(0x0100 << unit);
        /*
         * 0x0481 bits 1 and 0 are the drive MODE of SASI/IDE HD #1 and
         * #0 (Undocumented 9801): set means a thick drive with 512-byte
         * sectors, clear means 256-byte sectors or a thin drive.  Every
         * ATA device we can talk to is the former, so the bit follows
         * the unit being there.  Only units 0 and 1 have a bit.
         */
        if (unit < 2) {
            wa_orb(WA_BIOS_FLAG3, (u8)(1u << unit));
        }
        ser_puts("  IDE unit ");
        ser_hexb((u8)unit);
        ser_puts(": C/H/S ");
        ser_hexw(cyls);
        ser_putc('/');
        ser_hexb((u8)heads);
        ser_putc('/');
        ser_hexb((u8)sectors);
        ser_puts(" total ");
        ser_hexl(total);
        ser_crlf();
    }

    /* published equipment fields DOS reads directly */
    wa_setw(WA_DISK_EQUIP, (u16)((wa_w(WA_DISK_EQUIP) & 0xf0ff) | equip));
    /*
     * 0x5BA carries the equipment bitmap and 0x5BB stays zero -- that
     * is what a booted Xa7 shows, whatever np21w's model does with the
     * second byte.  The measurement wins.
     */
    wa_setb(WA_DISK_EQUIP_IDE, (u8)(equip >> 8));
    wa_setb(WA_DISK_HD_IDE, 0);
    if (equip) {
        wa_orb(WA_SYS_TYPE, 0x80);      /* "fixed disk fitted" */
    }
}

u8 ide_unit_rw(unsigned unit, u32 lba, unsigned nsec,
               u16 seg, u16 off, bool write)
{
    if (unit >= IDE_UNITS || !ide_present(unit)) {
        return IDE_ERR_NOTREADY;
    }
    if (lba + nsec > geo_total(unit)) {
        return IDE_ERR_PARAM;
    }
    return ide_rw(unit, lba, nsec, seg, off, write);
}

/* AH=84h reports the geometry DOS then uses for its own CHS maths. */
static u8 disk_sense(struct bregs *r, unsigned unit)
{
    if (R_AH(r) == 0x84) {
        r->bx = 512;
        r->cx = geo_cyls(unit);
        R_DH(r) = geo_heads(unit);
        R_DL(r) = geo_sectors(unit);
    }
    return 0x00;
}

static u8 disk_rw(struct bregs *r, unsigned unit, bool write)
{
    u32 lba;
    unsigned bytes = r->bx ? r->bx : 0x10000;
    unsigned nsec = (bytes + 511) / 512;

    if (R_AL(r) & 0x80) {               /* CHS */
        u8 sectors = geo_sectors(unit);
        u8 heads = geo_heads(unit);

        if (R_DL(r) >= sectors || R_DH(r) >= heads || r->cx >= geo_cyls(unit)) {
            return IDE_ERR_PARAM;
        }
        lba = (((u32)r->cx * heads) + R_DH(r)) * sectors + R_DL(r);
    } else {                            /* linear */
        if (geo_total(unit) > 0xffffff) {
            lba = ((u32)r->dx << 16) | r->cx;
        } else {
            lba = ((u32)R_DL(r) << 16) | r->cx;
        }
    }
    return ide_unit_rw(unit, lba, nsec, r->es, r->bp, write);
}

static u8 disk_sasi(struct bregs *r)
{
    unsigned func = R_AH(r) & 0x0f;
    unsigned unit = R_AL(r) & 0x03;

    if (func == 0x03) {                 /* initialize */
        ide_scan();
        return 0x00;
    }
    if (!ide_present(unit)) {
        return IDE_ERR_NOTREADY;
    }
    switch (func) {
    case 0x01:                          /* verify  */
    case 0x07:                          /* retract */
    case 0x0d:                          /* format: report success */
    case 0x0f:
        return 0x00;
    case 0x04:
        return disk_sense(r, unit);
    case 0x05:
        return disk_rw(r, unit, true);
    case 0x02:                          /* diagnostic read */
    case 0x06:
        return disk_rw(r, unit, false);
    default:
        return 0x40;
    }
}

void bios_int1b(struct bregs *r)
{
    u8 status;

    switch (R_AL(r) & 0xf0) {
    case 0x00:
    case 0x80:
        status = disk_sasi(r);
        break;
    default:                            /* SCSI, floppy, MO: not fitted */
        status = IDE_ERR_NOTREADY;
        break;
    }
    R_AH(r) = status;
    set_cf(r, status >= 0x20);
}
