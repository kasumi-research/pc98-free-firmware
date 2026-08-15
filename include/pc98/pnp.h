/*
 * The PC-98 PnP BIOS, as far as the machines actually implement one.
 *
 * The $PnP installation structure itself is in core/bios/pnp.S; this is
 * the part with model-specific answers in it.  Both are needed together
 * -- Windows finds the structure by scanning, then talks through
 * INT 1Fh AH=CEh, which is the PC-98 spelling of the services (the
 * far-call entry the structure names answers 0x82 to everything, on the
 * real firmware as well as ours).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_PNP_H
#define PC98_PNP_H

#include <pc98/types.h>
#include <pc98/bregs.h>

/*
 * What INT 1Fh AH=CEh AL=00h and AL=01h answer on this model, measured
 * from the NEC firmware with test/pnpdump.asm.  They are pure constants
 * there: the probe passed its own values in BX/CX/DX and got these
 * back, so the firmware writes all three.
 *
 * hal_pnp_ce() returning NULL means this model's answers have not been
 * measured yet.  The service then reports itself unsupported rather
 * than handing an OS numbers we invented -- on a machine that boots
 * Windows 2000 today, a plausible-looking wrong answer is worse than a
 * clear "no".
 */
struct pnp_ce {
    u16 f0_bx, f0_cx, f0_dx;
    u16 f1_bx, f1_cx, f1_dx;
};

const struct pnp_ce *hal_pnp_ce(void);

/* INT 1Fh AH=CEh. */
void pnp_ce(struct bregs *r);

#endif /* PC98_PNP_H */
