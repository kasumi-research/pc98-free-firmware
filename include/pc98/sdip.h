/*
 * Software DIP switch (SDIP) battery store.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_SDIP_H
#define PC98_SDIP_H

#include <pc98/types.h>

/* 12 registers per bank, front bank then back bank. */
#define SDIP_NREGS      12
#define SDIP_NBYTES     (2 * SDIP_NREGS)

/*
 * The model's power-on defaults, front bank then back, as the NEC ROM
 * leaves them on a healthy battery.  Measured, not composed: each HAL
 * returns its machine's settled store, dumped from the metal.
 */
const u8 *hal_sdip_defaults(void);

void sdip_init(void);

#endif
