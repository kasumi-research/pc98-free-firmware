/*
 * The BIOS work area at 0000:0400-05FF, and the memory switches.
 *
 * This is published interface, not private state: DOS, Windows and
 * option ROMs read these bytes directly, so the layout must match the
 * NEC ROM's field for field.  Every address here was taken from a dump
 * of a booted PC-9821Xa7 running the NEC ROM — test/wadump.asm is the
 * boot sector that prints it — or, for the field NAMES only, from the
 * ones np21w uses and the emulator already models (see NOTICE).
 *
 * The work area ENDS at 0x05FF.  Measured: on a booted Win98 the bytes
 * from 0x0600 up are IO.SYS, so nothing of ours may live there -- which
 * is also why the BIOS has no private stack (see bregs.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef PC98_WA_H
#define PC98_WA_H

#define WA_SEG              0x0000

/* --- 0x0400 block --- */
#define WA_EXPMMSZ          0x0401  /* extended memory, 128 KiB units << 3 */
#define WA_WARM_SP          0x0404  /* SS:SP saved across the CPU-reset  */
#define WA_WARM_SS          0x0406  /* dance (protected-mode return)     */
#define WA_ROMCAP           0x0457  /* 7-byte ROM/BIOS capability block  */
#define WA_SYS_TYPE         0x0480  /* CPU/equipment flags               */
#define WA_BIOS_FLAG3       0x0481  /* bit6 = "new keyboard"             */
#define WA_DISK_EQUIPS      0x0482  /* SCSI equipment bitmap             */
#define WA_SAVED_DX         0x0486
#define WA_F2HD_MODE        0x0493
#define WA_SCAN_PHASE       0x04ac  /* C-bus option-ROM scan cursor      */
#define WA_SCAN_SEG         0x04ae
#define WA_DISK_XROM        0x04b0  /* option-ROM scan results, 8 bytes  */
#define WA_DISK_XROM2       0x04b8
#define WA_TRAMP            0x04f8  /* firmware far-jump trampoline      */

/* --- 0x0500 block --- */
#define WA_BIOS_FLAG0       0x0500
#define WA_BIOS_FLAG1       0x0501  /* bits 0-2: conventional RAM size   */
#define WA_KB_BUF           0x0502  /* key ring, WA_KB_BUF_ENTRIES words  */
#define WA_KB_BUF_ENTRIES   16
#define WA_KB_SHIFT_TBL     0x0522  /* offset of the live translate table */
#define WA_KB_BUF_HEAD      0x0524
#define WA_KB_BUF_TAIL      0x0526
#define WA_KB_COUNT         0x0528
#define WA_KB_RETRY         0x0529
#define WA_KB_KY_STS        0x052a  /* 16-byte per-key down bitmap       */
#define WA_SHIFT_STS        0x053a
#define WA_CRT_RASTER       0x053b
#define WA_CRT_STS_FLAG     0x053c
#define WA_CRT_CNT          0x053d
#define WA_CRT_AREA_OFF     0x053e
#define WA_CRT_AREA_SEG     0x0540
#define WA_CRT_AREA_NUM     0x0547
#define WA_CRT_W_VRAMADR    0x0548
#define WA_CRT_W_RASTER     0x054a
#define WA_PRXCRT           0x054c
#define WA_PRXDUPD          0x054d
#define WA_RS_S_FLAG        0x055b
#define WA_DISK_EQUIP       0x055c  /* word: high byte = IDE unit bitmap */
#define WA_DISK_INTL        0x055e
#define WA_DISK_RESULT      0x0564  /* 4 x 8-byte per-unit result block  */
#define WA_DISK_BOOT        0x0584  /* the DA/UA we booted from          */
#define WA_TIMER_COUNT      0x058a  /* INT 1Ch interval countdown        */
#define WA_EXTMMSZ          0x0594  /* word: MiB above 16 MiB            */
#define WA_CRT_BIOS         0x0597
#define WA_RS_PARAM         0x05a9  /* 3-byte RS-232C parameter residue  */
#define WA_F144_SUP         0x05ae
#define WA_DISK_EQUIP_IDE   0x05ba
#define WA_DISK_HD_IDE      0x05bb
#define WA_RS_D_FLAG        0x05c1
#define WA_KB_CODE_OFF      0x05c6  /* far pointer to the translate tables */
#define WA_KB_CODE_SEG      0x05c8
#define WA_F2DD_MODE        0x05ca
#define WA_F2DD_POINTER     0x05cc
#define WA_DISK_POINTER0    0x05e8  /* far entries a disk module publishes */
#define WA_DISK_POINTER1    0x05ec
#define WA_F2HD_POINTER     0x05f8

/* --- memory switches: text VRAM A3FE0.., byte per 4 --- */
#define MSW_SEG             0xa3fe
#define MSW_OFF(n)          ((n) * 4 + 2)    /* MSW1..MSW8 -> n = 0..7 */
#define MSW5_BOOT_MASK      0xf0

/* The defines above are shared with the assembly; the rest is C only. */
#ifndef __ASSEMBLER__

#include <pc98/types.h>
#include <pc98/io.h>

/* Accessors: the work area is outside our data segment, always. */
static inline u8 wa_b(u16 off)              { return peekb(WA_SEG, off); }
static inline void wa_setb(u16 off, u8 v)   { pokeb(WA_SEG, off, v); }
static inline u16 wa_w(u16 off)             { return peekw(WA_SEG, off); }
static inline void wa_setw(u16 off, u16 v)  { pokew(WA_SEG, off, v); }

static inline void wa_setd(u16 off, u16 seg, u16 o)
{
    pokew(WA_SEG, off, o);
    pokew(WA_SEG, off + 2, seg);
}

static inline void wa_orb(u16 off, u8 v)    { wa_setb(off, wa_b(off) | v); }
static inline void wa_andb(u16 off, u8 v)   { wa_setb(off, wa_b(off) & v); }

/*
 * The memory switches live in text VRAM and are write-protected by mode
 * flip-flop 1 bit 6: `out 68h,0Dh` unlocks, `out 68h,0Ch` locks again.
 * Measured hazard (qemu hw/i386/pc98/pc98.c): with the lock off, INT
 * 18h AH=16h's screen fill sprays its attribute byte straight over
 * MSW5.
 */
static inline void msw_unlock(void) { outb(0x68, 0x0d); }
static inline void msw_lock(void)   { outb(0x68, 0x0c); }

static inline u8 msw_b(unsigned n)  { return peekb(MSW_SEG, MSW_OFF(n)); }

static inline void msw_setb(unsigned n, u8 v)
{
    msw_unlock();
    pokeb(MSW_SEG, MSW_OFF(n), v);
    msw_lock();
}

#endif  /* __ASSEMBLER__ */
#endif
