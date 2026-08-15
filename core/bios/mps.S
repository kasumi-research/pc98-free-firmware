/*
 * MPS 1.4 tables — the RvII26's two processors, as Windows 2000's MPS
 * HAL wants to be told about them.
 *
 * Every number here was decoded from the table the NEC ROM publishes,
 * read out of the memory of a booted RvII26.  The interrupt map in
 * particular is measured and not negotiable: on this machine
 *
 *   pin = irq        for IRQ0-6
 *   pin = irq - 1    for IRQ8-15
 *   IRQ7 has NO pin  -- it is the PC-98 slave-PIC cascade
 *   pin 15           carries the master PIC's INTR as ExtINT
 *
 * and IRQ3 and IRQ6 are the only level-triggered lines, which is
 * exactly the two PCI interrupts (AIC-7860 on 3, the NIC on 6).
 *
 * Both structures live in ROM rather than being built in RAM.  The
 * floating pointer has to be somewhere the OS scans, and F0000-FFFFF is
 * one of the three places the specification names.  Ours is in the BIOS
 * bank, which is in that range and needs no shadow to be readable.  The
 * configuration table may sit anywhere the pointer names, so it follows
 * immediately.
 *
 * BANK7 is linked so that a symbol's value IS its offset from F8000
 * (layout.h), which is what makes the physical address below a constant
 * the assembler can fold.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/layout.h>

#ifdef CONFIG_MPS

#define PHYS(sym)   ((SEG_BIOS << 4) + (sym))

/* PCMP entry types */
#define MP_CPU      0
#define MP_BUS      1
#define MP_IOAPIC   2
#define MP_IOINT    3
#define MP_LINT     4

/* interrupt types */
#define INT_INT     0
#define INT_NMI     1
#define INT_EXTINT  3

/* polarity/trigger flags: 01 = active high, 01/11 = edge/level */
#define EDGE        0x0005
#define LEVEL       0x000d

        .section .mps, "a"
        .balign 16

/* ---- the floating pointer ---- */
        .globl mp_float
mp_float:
        .ascii  "_MP_"
        .long   PHYS(mp_config)
        .byte   1                       /* length, in 16-byte paragraphs */
        .byte   4                       /* MPS 1.4                        */
        .byte   0                       /* checksum, patched below        */
        .byte   0                       /* feature 1: config table present */
        /*
         * Feature 2 bit 7 is IMCRP, "an IMCR is present and the OS must
         * write port 22h/23h to leave PIC mode".  A PC-98 has no IMCR:
         * the 8259s reach the local APICs through virtual wire and there
         * is nothing to switch.  Claiming one would send the HAL poking
         * at ports this machine does not decode.
         */
        .byte   0, 0, 0, 0
mp_float_end:

/* ---- the configuration table ---- */
        .balign 16
mp_config:
        .ascii  "PCMP"
        .word   mp_config_end - mp_config
        .byte   4                       /* MPS 1.4 */
        .byte   0                       /* checksum, patched below */
        .ascii  "NEC     "              /* OEM id, 8 bytes  */
        .ascii  "T63M0.06    "          /* product id, 12   */
        .long   0                       /* OEM table pointer */
        .word   0                       /* OEM table size    */
        .word   23                      /* 2 CPU, 2 bus, 1 IOAPIC,
                                           16 I/O int, 2 local int */
        .long   0xfee00000              /* local APIC address */
        .word   0                       /* extended table length   */
        .byte   0                       /* extended table checksum */
        .byte   0                       /* reserved */

mp_entries:
/*
 * Processors.  Signature 0x633 is a Klamath C0 Pentium II and the
 * feature word 0x381 is what this machine's CPUs report; the HAL reads
 * both back out of this table rather than running CPUID on the AP.
 */
        .macro  MPCPU id, flags
        .byte   MP_CPU, \id, 0x11, \flags
        .long   0x00000633              /* CPUID signature */
        .long   0x00000381              /* feature flags   */
        .long   0, 0
        .endm
        MPCPU   0, 0x03                 /* enabled, bootstrap */
        MPCPU   1, 0x01                 /* enabled            */

/* Buses.  The name field is 6 bytes, space padded. */
        .byte   MP_BUS, 0
        .ascii  "NEC98 "
        .byte   MP_BUS, 1
        .ascii  "PCI   "

/* The I/O APIC. */
        .byte   MP_IOAPIC, 2, 0x11, 0x01
        .long   0xfec00000

/*
 * I/O interrupt assignments.  irq -> pin is the measured map above; the
 * two level-triggered lines are the two PCI devices.
 */
        .macro  MPIOINT type, flags, irq, pin
        .byte   MP_IOINT, \type
        .word   \flags
        .byte   0, \irq, 2, \pin
        .endm
        MPIOINT INT_INT,    EDGE,  0,  0
        MPIOINT INT_INT,    EDGE,  1,  1
        MPIOINT INT_INT,    EDGE,  2,  2
        MPIOINT INT_INT,    LEVEL, 3,  3         /* AIC-7860 */
        MPIOINT INT_INT,    EDGE,  4,  4
        MPIOINT INT_INT,    EDGE,  5,  5
        MPIOINT INT_INT,    LEVEL, 6,  6         /* i82557 NIC */
        MPIOINT INT_INT,    EDGE,  8,  7
        MPIOINT INT_INT,    EDGE,  9,  8
        MPIOINT INT_INT,    EDGE, 10,  9
        MPIOINT INT_INT,    EDGE, 11, 10
        MPIOINT INT_INT,    EDGE, 12, 11
        MPIOINT INT_INT,    EDGE, 13, 12
        MPIOINT INT_INT,    EDGE, 14, 13
        MPIOINT INT_INT,    EDGE, 15, 14
        MPIOINT INT_EXTINT, EDGE,  0, 15         /* the master PIC's INTR */

/* Local interrupts: the standard virtual-wire pair on the BSP. */
        .byte   MP_LINT, INT_EXTINT
        .word   EDGE
        .byte   0, 0, 0, 0
        .byte   MP_LINT, INT_NMI
        .word   EDGE
        .byte   0, 1, 0, 1
mp_config_end:

#endif  /* CONFIG_MPS */
