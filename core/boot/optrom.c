/*
 * The PCI expansion-ROM scan.
 *
 * This is how the RvII26 gets a disk.  Its boot device hangs off an
 * Adaptec AIC-7860, and the code that drives it is the card's own 32 KiB
 * expansion ROM -- a PC-98 module, `55 AA <blocks> CB` with a `PC98`
 * signature at +0x1C and an entry table of 4-byte slots from +0x20.
 *
 * Everything below was measured: from the card's own ROM image, and
 * from register-level traces of the NEC ROM and of this firmware
 * driving the same card, compared against each other.
 *
 * The protocol has TWO stages, and missing the second one is a quiet
 * failure rather than a loud one -- the disk is found, its geometry is
 * written into the work area, and then nothing can reach it:
 *
 *   1. STAGE AND INITIALISE.  The image is copied into conventional
 *      memory and its init entry at +0x20 is far-called with AH = bus,
 *      AL = devfn (the PC/AT expansion-ROM contract, at the PC-98
 *      offset; the AIC ROM's first act is to stash AH and AL>>3 for its
 *      config cycles).  Init scans the SCSI bus, fills in 0x0482 and
 *      the per-target blocks at 0x0460, and shrinks its own size byte
 *      -- the AIC-7860's goes 0x40 (32 KiB) to 0x1C (14 KiB).  What is
 *      left is the resident.
 *
 *   2. RELOCATE AND INSTALL.  The resident is copied into the C-bus
 *      option-ROM window and its +0x24 entry is far-called THERE.  That
 *      entry hooks the card's IRQ into the IVT, unmasks it in the PIC,
 *      and registers the module for the device classes it serves by
 *      writing its own segment's high byte into the work-area table at
 *      0x04B0 + class (the AIC-7860 takes 2, A and C).  Our INT 1Bh
 *      dispatches through that table the same way (core/bios/vectors.S).
 *
 * Both the staging address and the two-stage shape are forced by
 * measurement, not chosen.  Running the image in the C-bus window
 * instead of staging it is register-for-register identical to the NEC
 * ROM for 36776 card accesses -- and then simply stops, right where the
 * NEC ROM relocates and carries on.  The staging segment is not a guess
 * either: under the NEC ROM the card's init runs with CS = 8000h, while
 * the resident ends up at DC000.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/pci.h>
#include <pc98/wa.h>
#include <pc98/optrom.h>
#include <pc98/serial.h>

void ext_copy(u32 dst, u32 src, u32 len);

#define PCI_COMMAND     0x04
#define PCI_HEADER      0x0e
#define PCI_ROM_BAR     0x30
#define CMD_MEM         0x0002

#define ROM_SIG         0xaa55
#define ROM_INIT_OFF    0x0020          /* entry table slot 0: initialise */
#define ROM_INSTALL_OFF 0x0024          /* slot 1: hook IRQ, claim classes */

/* ROMs are placed on 2 KiB boundaries, which is also the BAR's grain. */
static u32 round_up_2k(u32 v)
{
    return (v + 0x7ff) & ~0x7ffu;
}

static bool is_pc98_module(u16 seg)
{
    /* "PC98" at +0x1C, as two words so no 32-bit far read is needed */
    return peekw(seg, 0) == ROM_SIG &&
           peekw(seg, 0x1c) == 0x4350 && peekw(seg, 0x1e) == 0x3839;
}

/*
 * Stage one device's expansion ROM at `stage` and initialise it.
 * Returns the size of the resident it wants kept, or 0 for a device
 * with no ROM, no PC-98 module in it, or nothing left after init.
 */
static u32 optrom_init(u8 bdf, u32 stage)
{
    u32 rombar = pci_cfg_readl(bdf, PCI_ROM_BAR);
    u32 base = rombar & 0xfffff800u;
    u16 cmd = pci_cfg_readw(bdf, PCI_COMMAND);
    u16 seg = (u16)(stage >> 4);
    u32 bytes;

    if (!base) {
        return 0;
    }
    /* the ROM decoder only answers while the device's memory decode is on */
    pci_cfg_writew(bdf, PCI_COMMAND, (u16)(cmd | CMD_MEM));
    pci_cfg_writel(bdf, PCI_ROM_BAR, base | 1u);

    ext_copy(stage, base, 0x200);
    bytes = (peekw(seg, 0) == ROM_SIG) ? (u32)peekb(seg, 2) * 512 : 0;
    if (bytes) {
        ext_copy(stage, base, bytes);
    }

    /*
     * Decoder off again, and for good.  A real Xa7 reads config 0x30
     * back after POST with the enable bit CLEAR, so leaving it on would
     * diverge from the machine.
     */
    pci_cfg_writel(bdf, PCI_ROM_BAR, base);
    pci_cfg_writew(bdf, PCI_COMMAND, cmd);

    if (!bytes) {
        return 0;
    }
    /*
     * Only PC-98 modules have an entry table at +0x20; a PC/AT-only ROM
     * has a `retf` at +3 and nothing we can call.  Say so and move on
     * rather than jumping into it.
     */
    if (!is_pc98_module(seg)) {
        ser_puts("  optrom: ");
        ser_hexb(bdf);
        ser_puts(" has no PC98 module\r\n");
        return 0;
    }

    ser_puts("  optrom: ");
    ser_hexb(bdf);
    ser_puts(" init at ");
    ser_hexw(seg);
    ser_puts(":0020\r\n");
    optrom_call(seg, ROM_INIT_OFF, (u16)bdf);   /* AH = bus 0, AL = devfn */

    return (u32)peekb(seg, 2) * 512;
}

void optrom_scan(void)
{
    u32 stage = hal_optrom_stage();
    u32 place = hal_optrom_base();
    u32 limit = hal_optrom_limit();
    unsigned dev, fn;

    if (!stage) {
        return;
    }
    for (dev = 0; dev < 32; dev++) {
        for (fn = 0; fn < 8; fn++) {
            u8 bdf = PCI_BDF(dev, fn);
            u32 id = pci_cfg_readl(bdf, 0x00);
            u32 kept;

            if (id == 0xffffffffu || id == 0) {
                if (fn == 0) {
                    break;              /* no function 0: no device */
                }
                continue;
            }
            kept = optrom_init(bdf, stage);
            if (kept && place + kept <= limit) {
                u16 seg = (u16)(place >> 4);

                ext_copy(place, stage, kept);
                ser_puts("  optrom: resident ");
                ser_hexl(kept);
                ser_puts(" bytes at ");
                ser_hexw(seg);
                ser_puts(":0000, install\r\n");
                optrom_call(seg, ROM_INSTALL_OFF, (u16)bdf);
                place += round_up_2k(kept);
            } else if (kept) {
                ser_puts("  optrom: no room for ");
                ser_hexb(bdf);
                ser_crlf();
            }
            if (fn == 0 && !(pci_cfg_readb(bdf, PCI_HEADER) & 0x80)) {
                break;                  /* single-function device */
            }
        }
    }
    /*
     * The scan cursor a PC-98 firmware leaves behind.  0x04AC/0x04AE is
     * also the far pointer INT 1Bh jumps through, so it is rewritten on
     * every disk call; seeding it here just keeps a dump of the work
     * area recognisable before the first one.
     */
    wa_setw(WA_SCAN_SEG, (u16)(place >> 4));
}
