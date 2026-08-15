/*
 * INT 1Ah AH=B1h — the PCI BIOS.
 *
 * This is how Windows enumerates the bus: it asks for the installation
 * check, then walks devices by ID or class and reads their config space
 * through these calls rather than touching 0CF8h itself.  The function
 * numbering and the AH return codes are the PCI BIOS specification's,
 * which is why the register use looks like a PC/AT service on a machine
 * that has nothing else in common with one.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/bregs.h>
#include <pc98/pci.h>

#define PCIBIOS_PRESENT         0x01
#define PCIBIOS_FIND_DEVICE     0x02
#define PCIBIOS_FIND_CLASS      0x03
#define PCIBIOS_READ_BYTE       0x08
#define PCIBIOS_READ_WORD       0x09
#define PCIBIOS_READ_DWORD      0x0a
#define PCIBIOS_WRITE_BYTE      0x0b
#define PCIBIOS_WRITE_WORD      0x0c
#define PCIBIOS_WRITE_DWORD     0x0d

#define PCIBIOS_OK              0x00
#define PCIBIOS_BAD_FUNC        0x81
#define PCIBIOS_BAD_REGISTER    0x87
#define PCIBIOS_NOT_FOUND       0x86

/*
 * BX is the caller's bus/device/function.  Only bus 0 exists on this
 * machine; a request for any other bus is "device not found" rather
 * than a wild config cycle.
 */
static bool bdf_ok(u16 bx)
{
    return (bx >> 8) == 0;
}

static u8 find_device(struct bregs *r, bool by_class)
{
    unsigned index = r->si;
    unsigned dev, fn;
    unsigned seen = 0;

    for (dev = 0; dev < 32; dev++) {
        for (fn = 0; fn < 8; fn++) {
            u8 bdf = PCI_BDF(dev, fn);
            u32 id = pci_cfg_readl(bdf, 0x00);
            bool match;

            if (id == 0xffffffffu || id == 0) {
                if (fn == 0) {
                    break;              /* no function 0: no device */
                }
                continue;
            }
            if (by_class) {
                match = (pci_cfg_readl(bdf, 0x08) >> 8) == (r->ecx & 0xffffffu);
            } else {
                match = id == (((u32)r->cx << 16) | r->dx);
            }
            if (match) {
                if (seen == index) {
                    r->bx = bdf;
                    return PCIBIOS_OK;
                }
                seen++;
            }
            if (fn == 0 && !(pci_cfg_readb(bdf, 0x0e) & 0x80)) {
                break;                  /* single-function device */
            }
        }
    }
    return PCIBIOS_NOT_FOUND;
}

/* The caller has already checked AH/AL and knows this is a PCI BIOS
 * call before dispatching here; every AL subfunction is handled,
 * including unrecognised ones (PCIBIOS_BAD_FUNC), so this always
 * returns true with the frame holding the answer. */
bool pci_bios(struct bregs *r)
{
    u8 fn = R_AL(r);
    u8 reg = (u8)r->di;
    u8 bdf = (u8)r->bx;
    u8 status = PCIBIOS_OK;

    switch (fn) {
    case PCIBIOS_PRESENT:
        r->edx = 0x20494350;            /* bytes 50 43 49 20 = "PCI "    */
        R_AL(r) = 0x01;                 /* configuration mechanism 1     */
        /*
         * PCI BIOS 2.0, which is what the machine answers: the NEC
         * module's own AL=01 loads BX with 0200h (scratch/re/bank0_lo.bin
         * at 0xb5e, the PCI BIOS behind INT 1Fh AH=CCh).  We claimed 2.10
         * for no reason but the specification's version number -- and a
         * caller that believes it may go looking for 2.1-only functions
         * we do not have.  core/pci/bios32.S answers the same value.
         */
        r->bx = 0x0200;
        R_CL(r) = 0;                    /* last bus number               */
        set_cf(r, false);
        R_AH(r) = PCIBIOS_OK;
        return true;
    case PCIBIOS_FIND_DEVICE:
        status = find_device(r, false);
        break;
    case PCIBIOS_FIND_CLASS:
        status = find_device(r, true);
        break;
    case PCIBIOS_READ_BYTE:
    case PCIBIOS_READ_WORD:
    case PCIBIOS_READ_DWORD:
    case PCIBIOS_WRITE_BYTE:
    case PCIBIOS_WRITE_WORD:
    case PCIBIOS_WRITE_DWORD:
        if (!bdf_ok(r->bx)) {
            status = PCIBIOS_NOT_FOUND;
            break;
        }
        /* alignment is the caller's job, and the spec says so */
        if ((fn == PCIBIOS_READ_WORD || fn == PCIBIOS_WRITE_WORD) &&
            (reg & 1)) {
            status = PCIBIOS_BAD_REGISTER;
            break;
        }
        if ((fn == PCIBIOS_READ_DWORD || fn == PCIBIOS_WRITE_DWORD) &&
            (reg & 3)) {
            status = PCIBIOS_BAD_REGISTER;
            break;
        }
        switch (fn) {
        case PCIBIOS_READ_BYTE:
            R_CL(r) = pci_cfg_readb(bdf, reg);
            break;
        case PCIBIOS_READ_WORD:
            r->cx = pci_cfg_readw(bdf, reg);
            break;
        case PCIBIOS_READ_DWORD:
            r->ecx = pci_cfg_readl(bdf, reg);
            break;
        case PCIBIOS_WRITE_BYTE:
            pci_cfg_writeb(bdf, reg, R_CL(r));
            break;
        case PCIBIOS_WRITE_WORD:
            pci_cfg_writew(bdf, reg, r->cx);
            break;
        default:
            pci_cfg_writel(bdf, reg, r->ecx);
            break;
        }
        break;
    default:
        status = PCIBIOS_BAD_FUNC;
        break;
    }
    R_AH(r) = status;
    set_cf(r, status != PCIBIOS_OK);
    return true;
}
