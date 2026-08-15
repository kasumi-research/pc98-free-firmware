/*
 * IDE transport interface, and the per-unit geometry cache.
 *
 * The geometry cache has to live in the work area: the BIOS has no
 * writable segment of its own, and re-issuing IDENTIFY on every disk
 * call is not an option.  0x0410-0x042F is used -- 32 bytes the NEC ROM
 * leaves zero both after POST and on a booted Win98 desktop, measured
 * from low memory in both states.  This is ours, not published
 * interface; anything DOS reads goes in the documented fields.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_IDE_H
#define PC98_IDE_H

#include <pc98/types.h>
#include <pc98/io.h>

#define IDE_ERR_NOTREADY    0x60    /* no such device / not ready      */
#define IDE_ERR_IO          0x40    /* transfer failed                 */
#define IDE_ERR_PARAM       0xd0    /* address outside the medium      */

/*
 * Units are numbered the way the DA/UA byte numbers them, which is also
 * the order the boot walk tries: unit >> 1 selects the channel through
 * the 0x430 bank register, unit & 1 the master/slave bit in the ATA
 * device register.
 */
#define IDE_UNITS           4

/* geometry cache, 8 bytes per unit at 0x0410 */
#define IDEGEO_BASE         0x0410
#define IDEGEO(u)           (IDEGEO_BASE + (u) * 8)
#define IDEGEO_PRESENT      0       /* byte: 0 = absent                */
#define IDEGEO_SECTORS      1       /* byte: sectors per track         */
#define IDEGEO_HEADS        2       /* byte                            */
#define IDEGEO_CYLS         3       /* word                            */
#define IDEGEO_TOTAL        5       /* 3 bytes: total sectors          */

static inline bool ide_present(unsigned unit)
{
    return peekb(0, (u16)(IDEGEO(unit) + IDEGEO_PRESENT)) != 0;
}

bool ide_identify(unsigned unit, u16 seg, u16 off);
u8 ide_rw(unsigned unit, u32 lba, unsigned nsec,
          u16 seg, u16 off, bool write);

void ide_scan(void);
u8 ide_unit_rw(unsigned unit, u32 lba, unsigned nsec,
               u16 seg, u16 off, bool write);

#endif
