/*
 * PC-9821Xa7 chipset services: the C-bus option-ROM window and PCI
 * interrupt routing.
 *
 * The option-ROM window is not implemented and says so rather than
 * guessing; the interrupt routing is.
 *
 * The option-ROM window: the mechanism exists (the Wildcat's control is
 * host-bridge config byte 0x67, 0xFF hiding the D8000 window so the
 * chipset RAM underneath decodes), but the only PCI expansion ROM on
 * this machine is the Trident WAB's, and Windows 98 already reaches a
 * working desktop without it -- the driver programs the card itself.
 * Running it would be a change to a path that works, for no measured
 * gain.  A staging address of 0 means "this model runs no expansion
 * ROMs" and the scan does nothing.
 *
 * The interrupt routing: the router IS the C-bus bridge's, at 00:06.0
 * config 0x60-0x63, exactly where the RvII26's Champion keeps its own --
 * an earlier version of this comment said it "has not been located on
 * this chipset", which was simply wrong.  Four bytes, one per PIRQ leg:
 * bit 7 set disables the leg, the low nibble is the IRQ.  The Xa7's
 * slot rotation is (slot + intx + 3) & 3, a different phase from the
 * RvII26's.
 *
 * Leaving it alone cost us the display.  The machine powers up with legs
 * 0-2 carrying IRQ 3/6/10 and leg 3 disabled, and the Trident WAB at
 * 00:08.0 INTA lands on leg 3 -- so its config 0x3C stayed 0xFF.
 * Windows saw a display adapter with no interrupt, concluded the boot
 * display was the mainboard's own adapter, and loaded EGCN4.DRV (the
 * built-in PEGC driver) where the reference loads TR968X.DLL (the
 * Trident driver).  That is what put up the pre-shell "display settings
 * updated ... restart" dialog on every virgin image.
 *
 * The reference firmware ends POST with 0A 80 80 80 in those four
 * bytes: exactly one leg enabled, carrying IRQ 10, and the card's 0x3C
 * reading 0x0A.  So it clears the power-up defaults and routes only the
 * legs it uses.  We do the same -- hal_pci_irq_init() disables all four
 * and each device with a pin claims its leg on first ask.  The pool is
 * the free-IRQ bitmap the NEC arbitrator computes for this machine,
 * 0x0448 = IRQ 3, 6 and 10, taken highest first so the one device on
 * our bus gets the 10 the reference gives it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/optrom.h>
#include <pc98/pnp.h>
#include <pc98/sdip.h>
#include <pc98/pci.h>
#include <pc98/romdata.h>

u32 hal_optrom_stage(void)
{
    return 0;
}

u32 hal_optrom_base(void)
{
    return 0;
}

u32 hal_optrom_limit(void)
{
    return 0;
}

/* the C-bus bridge, and its four PIRQ bytes */
#define CBUS_BRIDGE     PCI_BDF(6, 0)
#define PIRQ_BASE       0x60
#define PIRQ_OFF        0x80            /* bit 7: leg disabled */

/* IRQ 3, 6, 10 -- the arbitrator's free-IRQ bitmap 0x0448, highest first */
static const u8 irq_pool[] = { 10, 6, 3 };

void hal_pci_irq_init(void)
{
    unsigned leg;

    for (leg = 0; leg < 4; leg++) {
        pci_cfg_writeb(CBUS_BRIDGE, (u8)(PIRQ_BASE + leg), PIRQ_OFF);
    }
}

u8 hal_pci_irq(u8 bdf, u8 pin)
{
    unsigned slot = (unsigned)bdf >> 3;
    unsigned leg, i, j;
    u8 route;

    if (pin < 1 || pin > 4) {
        return 0xff;
    }
    leg = (slot + pin + 2) & 3;         /* = (slot + intx + 3) & 3 */
    route = pci_cfg_readb(CBUS_BRIDGE, (u8)(PIRQ_BASE + leg));
    if (!(route & PIRQ_OFF)) {
        return (u8)(route & 0x0f);      /* another device shares this leg */
    }
    /*
     * The pool table lives in ROM, so it is at CS and unreachable
     * through DS -- rom_b(), not a plain index (romdata.h).
     */
    for (i = 0; i < sizeof(irq_pool); i++) {
        u8 want = rom_b(&irq_pool[i]);
        bool taken = false;

        for (j = 0; j < 4; j++) {
            u8 r = pci_cfg_readb(CBUS_BRIDGE, (u8)(PIRQ_BASE + j));

            if (!(r & PIRQ_OFF) && (r & 0x0f) == want) {
                taken = true;
            }
        }
        if (!taken) {
            pci_cfg_writeb(CBUS_BRIDGE, (u8)(PIRQ_BASE + leg), want);
            return want;
        }
    }
    return 0xff;                        /* pool exhausted; say so */
}

/*
 * SDIP factory defaults: this machine's settled store, dumped from the
 * metal.  Identical to the RvII26's except 861Eh (F7h here): the Xa7
 * runs the compatible DMA clock (bit 4 set) and the auto-switching FDD
 * interface mode (bit 0 set).  Front bank then back.
 */
const u8 *hal_sdip_defaults(void)
{
    static const u8 defaults[SDIP_NBYTES] = {
        0x7c, 0x73, 0xf7, 0x3e, 0xdc, 0x7f,
        0xff, 0xbf, 0x7f, 0x7f, 0x49, 0x98,
        0x8f, 0x80, 0x80, 0x80, 0x80, 0x80,
        0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    };

    return defaults;
}

/*
 * INT 1Fh AH=CEh, as the NEC Xa7 firmware answers it.  Measured with
 * test/pnpdump.asm, which passed its own values in BX/CX/DX and got all
 * three back changed:
 *
 *   AL=00  ->  BX=0208 CX=0002 DX=4341
 *   AL=01  ->  BX=1000 CX=1000 DX=0000
 */
const struct pnp_ce *hal_pnp_ce(void)
{
    static const struct pnp_ce ce = {
        .f0_bx = 0x0208, .f0_cx = 0x0002, .f0_dx = 0x4341,
        .f1_bx = 0x1000, .f1_cx = 0x1000, .f1_dx = 0x0000,
    };

    return &ce;
}
