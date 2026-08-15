/*
 * PCI configuration access and the POST-time resource assignment.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_PCI_H
#define PC98_PCI_H

#include <pc98/types.h>

/* bdf packs device and function the way CONFIG_ADDRESS wants them */
#define PCI_BDF(dev, fn)    (u8)(((dev) << 3) | ((fn) & 7))

u32 pci_cfg_readl(u8 bdf, u8 reg);
u16 pci_cfg_readw(u8 bdf, u8 reg);
u8 pci_cfg_readb(u8 bdf, u8 reg);
void pci_cfg_writel(u8 bdf, u8 reg, u32 v);
void pci_cfg_writew(u8 bdf, u8 reg, u16 v);
void pci_cfg_writeb(u8 bdf, u8 reg, u8 v);

void pci_assign(void);

struct bregs;
bool pci_bios(struct bregs *r);

#endif
