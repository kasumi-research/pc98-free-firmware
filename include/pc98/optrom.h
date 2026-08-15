/*
 * PCI expansion ROMs: the C-bus window they run in, and the scan.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_OPTROM_H
#define PC98_OPTROM_H

#include <pc98/types.h>

/*
 * Open the model's option-ROM window and report its base; 0 means this
 * model does not run expansion ROMs.  The end address comes back from a
 * second call rather than through an out-parameter, deliberately: this
 * codebase has been bitten before by handing a pointer across a -m16
 * function boundary.  Two scalars cost nothing and cannot go wrong.
 */
u32 hal_optrom_stage(void);     /* where an image is initialised   */
u32 hal_optrom_base(void);      /* first resident address          */
u32 hal_optrom_limit(void);     /* end of the resident window      */

/*
 * The interrupt line the chipset routes this device's INTx to, or 0xFF
 * if the model's routing is not modelled.  0xFF is the PCI
 * specification's own "unknown", and config 0x3C is left alone for it.
 */
/*
 * Put the PCI interrupt router into a known state before any device is
 * routed.  Called once from pci_assign() ahead of the device walk.
 */
void hal_pci_irq_init(void);

u8 hal_pci_irq(u8 bdf, u8 pin);

/* Copy every PCI expansion ROM into the window and run its init. */
void optrom_scan(void);

/*
 * Far-call an option ROM entry with AX set to the value its contract
 * wants, on a stack of our own.  See core/boot/optcall.S.
 */
void optrom_call(u16 seg, u16 off, u16 ax);

#endif
