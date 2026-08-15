/*
 * PC-9821 built-in IDE (ATA) transport.
 *
 * Port map, cross-checked against qemu hw/i386/pc98/pc98-ide.c and the
 * Undocumented 9801 IDE notes: the ATA task file is at
 * 0x640 + 2*N
 * with data at 0x640, device control / alt status at 0x74C, and a
 * PC-98-specific channel selector at 0x430/0x432 (00 = channel 1,
 * 01 = channel 2; a write with bit7 set is a dummy that only latches
 * the value for read-back).
 *
 * Only what INT 1Bh and the boot path need: identify, LBA/CHS read and
 * write, PIO only, polled.  No DMA, no interrupts -- IRQ 9 is left
 * masked and the status register is polled, which is what a PC-98 disk
 * BIOS does for its own transfers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/ide.h>
#include <pc98/serial.h>

#define IDE_DATA        0x0640
#define IDE_REG(n)      (0x0640 + (n) * 2)
#define IDE_ERROR       IDE_REG(1)
#define IDE_FEATURE     IDE_REG(1)
#define IDE_NSECT       IDE_REG(2)
#define IDE_LBA0        IDE_REG(3)
#define IDE_LBA1        IDE_REG(4)
#define IDE_LBA2        IDE_REG(5)
#define IDE_DEVICE      IDE_REG(6)
#define IDE_STATUS      IDE_REG(7)
#define IDE_COMMAND     IDE_REG(7)
#define IDE_DEVCTL      0x074c
#define IDE_BANK        0x0430

#define ST_BSY          0x80
#define ST_DRDY         0x40
#define ST_DRQ          0x08
#define ST_ERR          0x01

#define CMD_READ        0x20
#define CMD_WRITE       0x30
#define CMD_IDENTIFY    0xec

/*
 * Timeouts are counted in port reads rather than in time: there is no
 * calibrated delay this early, and a bounded spin is the one thing that
 * turns a dead drive into an error return instead of a hang.  600000
 * reads of IDE_STATUS is a generous margin on any host.
 */
#define IDE_SPIN        600000

static void ide_select_chan(unsigned unit)
{
    outb(IDE_BANK, (u8)((unit >> 1) & 1));
}

static bool ide_wait(u8 mask, u8 want)
{
    u32 n = IDE_SPIN;
    u8 st;

    while (n--) {
        st = inb(IDE_STATUS);
        if (st == 0xff) {
            return false;               /* nothing driving the bus */
        }
        if (!(st & ST_BSY) && (st & mask) == want) {
            return true;
        }
        if (!(st & ST_BSY) && (st & ST_ERR)) {
            return false;
        }
    }
    return false;
}

static bool ide_select_dev(unsigned unit)
{
    u32 n = 100000;

    ide_select_chan(unit);
    outb(IDE_DEVICE, (u8)(0xa0 | ((unit & 1) << 4)));
    /* the device takes a moment to put its own status on the bus */
    while (n--) {
        if (!(inb(IDE_STATUS) & ST_BSY)) {
            return true;
        }
    }
    return false;
}

/*
 * Read one 512-byte sector's worth of PIO words straight into the
 * caller's buffer.  The buffer is a far pointer because every INT 1Bh
 * caller supplies ES:BP, and a bounce buffer would need RAM the BIOS
 * does not have.
 */
static void ide_pio_in(u16 seg, u16 off, unsigned words)
{
    unsigned i;

    for (i = 0; i < words; i++) {
        pokew(seg, (u16)(off + i * 2), inw(IDE_DATA));
    }
}

static void ide_pio_out(u16 seg, u16 off, unsigned words)
{
    unsigned i;

    for (i = 0; i < words; i++) {
        outw(IDE_DATA, peekw(seg, (u16)(off + i * 2)));
    }
}

bool ide_identify(unsigned unit, u16 seg, u16 off)
{
    if (!ide_select_dev(unit)) {
        return false;
    }
    outb(IDE_NSECT, 0);
    outb(IDE_LBA0, 0);
    outb(IDE_LBA1, 0);
    outb(IDE_LBA2, 0);
    outb(IDE_COMMAND, CMD_IDENTIFY);
    if (!ide_wait(ST_DRQ, ST_DRQ)) {
        return false;
    }
    ide_pio_in(seg, off, 256);
    return true;
}

/*
 * One transfer.  `lba` is a linear 512-byte sector number; the caller
 * has already turned any CHS request into one (core/bios/disk.c).
 * Split into 256-sector bursts because the ATA sector count is a byte.
 */
u8 ide_rw(unsigned unit, u32 lba, unsigned nsec,
          u16 seg, u16 off, bool write)
{
    while (nsec) {
        unsigned burst = nsec > 256 ? 256 : nsec;
        unsigned i;

        if (!ide_select_dev(unit)) {
            return IDE_ERR_NOTREADY;
        }
        if (!ide_wait(ST_DRDY, ST_DRDY)) {
            return IDE_ERR_NOTREADY;
        }
        outb(IDE_NSECT, (u8)(burst & 0xff));
        outb(IDE_LBA0, (u8)(lba & 0xff));
        outb(IDE_LBA1, (u8)((lba >> 8) & 0xff));
        outb(IDE_LBA2, (u8)((lba >> 16) & 0xff));
        outb(IDE_DEVICE, (u8)(0xe0 | ((unit & 1) << 4) |
                              ((lba >> 24) & 0x0f)));
        outb(IDE_COMMAND, write ? CMD_WRITE : CMD_READ);

        for (i = 0; i < burst; i++) {
            if (!ide_wait(ST_DRQ, ST_DRQ)) {
                return IDE_ERR_IO;
            }
            if (write) {
                ide_pio_out(seg, off, 256);
            } else {
                ide_pio_in(seg, off, 256);
            }
            /*
             * Advance the SEGMENT, not the offset: a 40 KiB IPL read
             * from ES:BP would otherwise wrap the offset back through
             * the start of the buffer.  512 bytes = 32 paragraphs.
             */
            seg = (u16)(seg + 32);
        }
        if (write) {
            /* flush the last block out of the drive's buffer */
            ide_wait(ST_DRQ, 0);
        }
        if (inb(IDE_STATUS) & ST_ERR) {
            return IDE_ERR_IO;
        }
        lba += burst;
        nsec -= burst;
    }
    return 0;
}
