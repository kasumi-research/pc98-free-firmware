/*
 * Per-model configuration.
 *
 * The CONFIG_* symbols are set on the command line from config/<model>.mk
 * (the roms/config.vga-* pattern QEMU uses).  Core code must branch on
 * capability flags, never on the model — a `#ifdef CONFIG_MODEL_RVII26`
 * inside core/ means the HAL seam is in the wrong place.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_MODEL_H
#define PC98_MODEL_H

#if defined(CONFIG_MODEL_XA7)
#  define MODEL_NAME    "PC-9821Xa7"
#elif defined(CONFIG_MODEL_RVII26)
#  define MODEL_NAME    "PC-9821RvII26"
#elif defined(CONFIG_MODEL_BA3)
#  define MODEL_NAME    "PC-9801BA3"
#else
#  error "no CONFIG_MODEL_* selected; build via the Makefile with MODEL="
#endif

/*
 * POST progress port.  The ITF stamps one byte per stage here; with no
 * POST card fitted the write is swallowed, on hardware and in the
 * emulator alike (qemu hw/i386/pc98/pc98-lle.c).  Free diagnostics, so
 * always stamp.
 */
#define PORT_POST       0x610

/* Flash bank window control. */
#define PORT_BANK_GATE  0x043d      /* bit1: 0 = selected bank, 2 = BANK7 */
#define PORT_BANK_SEL   0x043f      /* even codes E0h..EEh = BANK0..7     */
#define BANK_CODE(n)    (0xe0 + ((n) << 1))

/* 8255 system port: bit7 SHUT0 (cold/warm), bit5 ITF request. */
#define PORT_SYS_PORTC  0x35
#define PORT_SYS_CTRL   0x37

#endif
