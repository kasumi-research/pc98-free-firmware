/*
 * PCI: configuration access, POST-time resource assignment, and the
 * INT 1Ah AH=B1h BIOS services Windows enumerates through.
 *
 * Mechanism 1 (CONFIG_ADDRESS at 0CF8h, CONFIG_DATA at 0CFCh), as the
 * real machine uses.  The measured trap (qemu hw/i386/pc98/pc98-lle.c)
 * is that bits 1:0 of CONFIG_ADDRESS must be masked off; a register
 * number written with its low bits intact selects the wrong dword.
 *
 * The memory window base is measured, not chosen: booting the NEC ROM
 * and reading the assignment back out of config space shows the Trident WAB's
 * 4 MiB aperture at 0x20000000 and its 64 KiB register window
 * immediately after it, so that is where our allocator starts and it
 * lands on the same addresses for the same card.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/pci.h>
#include <pc98/serial.h>
#include <pc98/optrom.h>

#define CFG_ADDR    0x0cf8
#define CFG_DATA    0x0cfc

#define PCI_VENDOR      0x00
#define PCI_COMMAND     0x04
#define PCI_CLASS_REV   0x08
#define PCI_HEADER      0x0e
#define PCI_BAR0        0x10
#define PCI_ROM_BAR     0x30
#define PCI_INT_LINE    0x3c
#define PCI_INT_PIN     0x3d

#define PCI_LATENCY     0x0d
#define PCI_MIN_GNT     0x3e
#define PCI_MAX_LAT     0x3f

#define CMD_IO          0x0001
#define CMD_MEM         0x0002
#define CMD_MASTER      0x0004

/*
 * Latency timer for a bus master, measured: the RvII26 firmware writes
 * 0x6000 to config dword 0x0C on the AIC-7860, i.e. cache line size 0
 * and latency timer 0x60.
 */
#define LATENCY_TIMER   0x60

/*
 * Where the assignment starts.  Both windows grow upward with natural
 * alignment, which is all the ordering a handful of devices needs.
 */
#define PCI_MEM_BASE    0x20000000u
#define PCI_IO_BASE     0x1000u
/*
 * Expansion ROMs get their own window, and the address is measured:
 * a real Xa7 reads config 0x30 back as 21000000 after POST, with the
 * enable bit CLEAR.  So the firmware places the BAR and leaves the
 * decoder off; a driver that reads the register sees the same value
 * it would on the metal.
 */
#define PCI_ROM_BASE    0x21000000u

static u32 cfg_addr(u8 bdf, u8 reg)
{
    return 0x80000000u | ((u32)bdf << 8) | (reg & 0xfcu);
}

u32 pci_cfg_readl(u8 bdf, u8 reg)
{
    outl(CFG_ADDR, cfg_addr(bdf, reg));
    return inl(CFG_DATA);
}

void pci_cfg_writel(u8 bdf, u8 reg, u32 v)
{
    outl(CFG_ADDR, cfg_addr(bdf, reg));
    outl(CFG_DATA, v);
}

u16 pci_cfg_readw(u8 bdf, u8 reg)
{
    return (u16)(pci_cfg_readl(bdf, reg) >> ((reg & 2) * 8));
}

u8 pci_cfg_readb(u8 bdf, u8 reg)
{
    return (u8)(pci_cfg_readl(bdf, reg) >> ((reg & 3) * 8));
}

void pci_cfg_writew(u8 bdf, u8 reg, u16 v)
{
    unsigned shift = (reg & 2) * 8;
    u32 old = pci_cfg_readl(bdf, reg);

    pci_cfg_writel(bdf, reg, (old & ~(0xffffu << shift)) | ((u32)v << shift));
}

void pci_cfg_writeb(u8 bdf, u8 reg, u8 v)
{
    unsigned shift = (reg & 3) * 8;
    u32 old = pci_cfg_readl(bdf, reg);

    pci_cfg_writel(bdf, reg, (old & ~(0xffu << shift)) | ((u32)v << shift));
}

/* ---- POST-time assignment ---- */

static u32 align_up(u32 v, u32 a)
{
    return (v + a - 1) & ~(a - 1);
}

/*
 * Size and place one BAR.  Writing all ones and reading back leaves the
 * decoder's don't-care bits clear, so the complement of the masked value
 * plus one is the region size.  A BAR that reads back zero is not
 * implemented and is skipped -- not assigned an address, which would
 * make an absent decoder look present.
 */
static void assign_bar(u8 bdf, u8 reg, u32 *memp, u32 *iop, bool *is64,
                       u16 *cmdp)
{
    u32 orig, mask, size, base;
    bool io;

    *is64 = false;
    orig = pci_cfg_readl(bdf, reg);
    pci_cfg_writel(bdf, reg, 0xffffffffu);
    mask = pci_cfg_readl(bdf, reg);
    if (mask == 0 || mask == 0xffffffffu) {
        pci_cfg_writel(bdf, reg, orig);
        return;
    }
    io = (orig & 1) != 0;
    if (io) {
        size = (~(mask & ~3u)) + 1;
        if (size < 4) {
            size = 4;
        }
        base = align_up(*iop, size);
        *iop = base + size;
    } else {
        *is64 = ((orig >> 1) & 3) == 2;
        size = (~(mask & ~0xfu)) + 1;
        if (size < 16) {
            size = 16;
        }
        base = align_up(*memp, size);
        *memp = base + size;
    }
    pci_cfg_writel(bdf, reg, base | (orig & (io ? 3u : 0xfu)));
    *cmdp |= io ? CMD_IO : CMD_MEM;
    if (*is64) {
        pci_cfg_writel(bdf, reg + 4, 0);
    }
}

void pci_assign(void)
{
    u32 mem = PCI_MEM_BASE;
    u32 io = PCI_IO_BASE;
    u32 rom = PCI_ROM_BASE;
    unsigned dev;

    hal_pci_irq_init();

    for (dev = 0; dev < 32; dev++) {
        u8 bdf = (u8)(dev << 3);
        u32 id = pci_cfg_readl(bdf, PCI_VENDOR);
        u8 hdr;
        unsigned bar;
        u16 cmd = 0;

        if (id == 0xffffffffu || id == 0) {
            continue;
        }
        hdr = (u8)(pci_cfg_readb(bdf, PCI_HEADER) & 0x7f);
        if (hdr != 0) {
            continue;                   /* bridges: nothing behind them */
        }
        for (bar = 0; bar < 6; bar++) {
            bool is64;

            assign_bar(bdf, (u8)(PCI_BAR0 + bar * 4), &mem, &io, &is64, &cmd);
            if (is64) {
                bar++;
            }
        }
        /* place the expansion-ROM BAR, decoder left disabled */
        {
            u32 orig = pci_cfg_readl(bdf, PCI_ROM_BAR);
            u32 mask;

            pci_cfg_writel(bdf, PCI_ROM_BAR, 0xfffff800u);
            mask = pci_cfg_readl(bdf, PCI_ROM_BAR) & 0xfffff800u;
            if (mask) {
                u32 size = (~mask) + 1;

                rom = align_up(rom, size);
                pci_cfg_writel(bdf, PCI_ROM_BAR, rom);
                rom += size;
            } else {
                pci_cfg_writel(bdf, PCI_ROM_BAR, orig & ~1u);
            }
        }

        /*
         * Bus mastering, for the devices that are bus masters.
         *
         * A device that drives the bus itself declares how much of it
         * it needs in MIN_GNT/MAX_LAT, and a device that never does
         * leaves both zero -- that is what those registers are for, and
         * it is the one signal in config space that answers the
         * question without a device list.  Both sides of it are
         * measured here: the AIC-7860 reads 04/04 and under the NEC ROM
         * ends up with command 0x015F (I/O + memory + master + special
         * + MWI + PERR + SERR) and latency 0x60, while the Trident
         * reads 00/00 and a real Xa7 shows command 0x0002 after POST --
         * memory decode only, no master bit.
         *
         * Without this the AIC-7860's expansion ROM gets through its
         * whole init, issues its first command, and the transfer goes
         * nowhere: QEMU gates DMA on the master bit exactly as the real
         * bridge does, so the sequencer raised SEQINT where under the
         * NEC ROM it reports CMDCMPLT.  That was the entire difference
         * between the two register traces.
         */
        if (pci_cfg_readb(bdf, PCI_MIN_GNT) || pci_cfg_readb(bdf, PCI_MAX_LAT)) {
            cmd |= CMD_MASTER;
            pci_cfg_writeb(bdf, PCI_LATENCY, LATENCY_TIMER);
        }
        /*
         * The interrupt line, for every device that has an interrupt
         * pin at all.  An expansion ROM reads this to find the IRQ it
         * must hook -- the AIC-7860's would otherwise hook vector 8,
         * the timer.
         *
         * A device the machine has no routing for still gets a write,
         * of 0xff: "unrouted", which is what the metal leaves and the
         * value PCI reserves for it.  The Xa7 capture (probe.log, the
         * Trident WAB at B00 D08 F0) reads 3c=ff 3d=01 after POST,
         * while the three pin-less devices read 3c=00 -- so the byte
         * tracks the pin, not the routing.  Leaving it at the model's
         * 0x00 instead told Windows 98 the display adapter was wired
         * to IRQ 0, the system timer: the PCI enumerator reconfigured
         * the adapter's device node on every virgin boot and put up
         * "新しいディスプレイの設定 ... restart", before the shell.
         */
        {
            u8 pin = pci_cfg_readb(bdf, PCI_INT_PIN);

            if (pin) {
                pci_cfg_writeb(bdf, PCI_INT_LINE, hal_pci_irq(bdf, pin));
            }
        }
        /*
         * Finally the command register: the decodes this device
         * actually has BARs for (accumulated in `cmd` by assign_bar),
         * plus whatever it already had set.  Nothing else.  The Trident
         * hardwires every command bit but I/O and memory to zero, and
         * the metal reads 0x0002 back after POST -- I/O decode OFF,
         * because the card has no I/O BAR.  Setting bits a device does
         * not implement makes a config dump diverge from the machine
         * for no gain.
         */
        cmd |= pci_cfg_readw(bdf, PCI_COMMAND);
        pci_cfg_writew(bdf, PCI_COMMAND, cmd);

        ser_puts("  PCI ");
        ser_hexb(bdf);
        ser_putc(' ');
        ser_hexl(id);
        ser_crlf();
    }
}
