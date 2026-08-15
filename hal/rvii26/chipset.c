/*
 * PC-9821RvII26 chipset services: the C-bus option-ROM window and the
 * PCI interrupt routing.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/pci.h>
#include <pc98/optrom.h>
#include <pc98/sdip.h>
#include <pc98/pnp.h>

/*
 * Expansion ROMs are STAGED IN CONVENTIONAL MEMORY, at 0x80000, and
 * their residents are copied into the C-bus window afterwards.  That is
 * not a design choice, it is what the NEC ROM does, measured: under it
 * the AIC-7860's ROM runs its init with CS = 8000h, and only afterwards
 * does its 14 KiB resident appear at DC000 -- which is why a dump of
 * the running machine shows the resident there but no trace of the
 * 32 KiB image it was cut from.
 *
 * Initialising the image in the window instead does not work.  With it
 * at D8000 the whole init is register-for-register identical to the NEC
 * ROM for 36776 card accesses -- and then simply stops, at the point
 * where the NEC ROM relocates and carries on.
 *
 * 0x80000-0x8FFFF is free for the whole of the cold path: above
 * anything the boot sector or IO.SYS touches, below the 640 KiB
 * conventional limit, and reclaimed the moment an OS starts.
 */
u32 hal_optrom_stage(void)
{
    return 0x80000u;
}

/*
 * D8000-DFFFF for the residents: the 32 KiB the PC-98 option-ROM area
 * ends with.  Opening it means dropping the flash's own D8000 module
 * window so the 128 KiB of chipset RAM underneath decodes instead.
 *
 * The Champion CNB20-LE controls C0000-DFFFF through host-bridge config
 * dword 0x70; byte 0x72 bit 4 covers the D8000 page, CLEAR showing the
 * flash's BANK3 module.  We write 0x00F00020: the four D0000-DFFFF
 * pages RAM (0x72 = F0), C0000-CFFFF left showing nothing (0x71 = 00,
 * no C-bus ROM to expose), and the low byte as the machine's POST
 * leaves it.  Taking the RAM view permanently is deliberate -- our
 * BANK3 is empty, so leaving the flash window mapped would put 8 KiB of
 * erased 0xFF in the middle of the option-ROM area.
 *
 * The whole dword is written, not a byte, and that is deliberate: a
 * read-modify-write of one byte would carry the config-space reset
 * value of 0x73 (0xA8) into the chipset, and 0x73 is the E8000-FFFFF
 * shadow control.  0xA8 means "reads come from shadow RAM" -- which
 * nothing has populated yet, so the very next instruction fetch would
 * come out of empty RAM.  0x73 stays 0 until POST populates the shadow.
 */
u32 hal_optrom_base(void)
{
    pci_cfg_writel(PCI_BDF(0, 0), 0x70, 0x00f00020u);
    return 0xd8000u;
}

u32 hal_optrom_limit(void)
{
    return 0xe0000u;
}

/*
 * PCI interrupt routing.  The PC-98 PCI-to-C-bus bridge at device 6
 * carries the PIRQ router in config 0x60-0x63, one byte per PIRQ line
 * holding the IRQ it lands on (bit 7 set = the line is not routed).
 * The INTx-to-PIRQ rotation on this machine is (slot + intx) & 3.
 *
 * Both are measured: the router reads 80 80 03 06, and under the NEC
 * ROM the AIC-7860 at slot 10 INTA lands on IRQ 3 and the i82557 at
 * slot 11 INTA on IRQ 6 -- which is (10+0)&3 = 2 -> 0x03 and
 * (11+0)&3 = 3 -> 0x06.  Note the phase differs from the Xa7's,
 * (slot + intx + 3) & 3.
 *
 * The AIC-7860's expansion ROM needs this: its install entry reads the
 * card's interrupt number, hooks (irq+8)*4 in the IVT and unmasks the
 * line in the PIC.  With config 0x3C left at zero it would hook the
 * timer vector.
 */
/*
 * Nothing to do: this machine's ITF leaves the Champion's router
 * programmed and hal_pci_irq() only reads it back.
 */
void hal_pci_irq_init(void)
{
}

u8 hal_pci_irq(u8 bdf, u8 pin)
{
    unsigned slot = (unsigned)bdf >> 3;
    u8 route;

    if (pin < 1 || pin > 4) {
        return 0xff;
    }
    route = pci_cfg_readb(PCI_BDF(6, 0), (u8)(0x60 + ((slot + pin - 1) & 3)));
    return (route & 0x80) ? 0xff : (u8)(route & 0x0f);
}

/*
 * SDIP factory defaults: this machine's settled store, dumped from the
 * metal (rvii26-sdip-settled.bin) after the NEC ROM had rewritten and
 * used it across a boot.  Front bank then back.  Notable decodes
 * (io_sdip.txt): 851Eh = 73h is GDC 5 MHz / 25 rows / 80 columns, and
 * 861Eh bit 4 clear is the fast DMA clock -- the four settings a
 * working Windows install expects.
 */
const u8 *hal_sdip_defaults(void)
{
    static const u8 defaults[SDIP_NBYTES] = {
        0x7c, 0x73, 0xe6, 0x3e, 0xdc, 0x7f,
        0xff, 0xbf, 0x7f, 0x7f, 0x49, 0x98,
        0x8f, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    };

    return defaults;
}

/*
 * INT 1Fh AH=CEh has NOT been measured on this machine yet.  The RvII26
 * flash carries the same $PnP structure the Xa7's does (same offset,
 * same D800:003A entry, OEM identifier zero instead of "PC98"), so the
 * structure is published either way -- but the Xa7's CE answers are
 * constants read out of the Xa7's firmware, and there is no reason to
 * believe a different machine returns the same ones.
 *
 * Windows 2000 boots on this machine today.  Handing its PnP enumerator
 * numbers we guessed is a worse failure than telling it the service is
 * not there, because a wrong answer is one it will act on.  So: NULL
 * until test/pnpdump.asm has been run on the real RvII26 firmware.
 */
const struct pnp_ce *hal_pnp_ce(void)
{
    return 0;
}
