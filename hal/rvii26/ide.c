/*
 * PC-9821RvII26 storage transport — not yet implemented.
 *
 * This machine boots from an Adaptec AIC-7860 SCSI adapter, not from a
 * built-in IDE channel: its disk path is the card's option ROM plus the
 * sequencer-driven host adapter, which is not implemented yet.  Until
 * then the disk layer answers honestly rather than pretending: no unit
 * responds,
 * so the boot walk falls through to the "no system" handler instead of
 * reading garbage off a controller that is not there.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/ide.h>

bool ide_identify(unsigned unit, u16 seg, u16 off)
{
    return false;
}

u8 ide_rw(unsigned unit, u32 lba, unsigned nsec,
          u16 seg, u16 off, bool write)
{
    return IDE_ERR_NOTREADY;
}
