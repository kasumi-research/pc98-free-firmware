/*
 * Interrupt entry points, and the table the IVT install copies from.
 *
 * Vectors that do real work land in a stub that names its service and
 * joins one common prologue: it pushes the register frame (bregs.h),
 * points DS at the caller's stack segment and calls C.  The epilogue
 * pops the frame back, so a handler returns values simply by assigning
 * to the frame -- including the flags word the final iret reloads.
 *
 * NO STACK SWITCH.  Measured, and explained in include/pc98/bregs.h: a
 * PC-98 BIOS service runs on the caller's stack, and there is nowhere
 * in low memory to put a private one.
 *
 * The trivial vectors stay in assembly: an EOI stub that costs a C call
 * frame would be a strange thing to put on an interrupt path, and the
 * timer's chain into INT 1Ch has to control the interrupt flag by hand.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/asm.h>
#include <pc98/layout.h>
#include <pc98/wa.h>
#include <pc98/bregs.h>

        .code16
        .section .text, "ax"

.macro  IENTRY name, svc
        .globl \name
\name:
        pushw   %es
        pushw   %ds
        pushal
        pushw   $\svc
        jmp     int_common
.endm

        IENTRY  ient_kbd,    SVC_KBD
        IENTRY  ient_fdc1,   SVC_FDC1
        IENTRY  ient_fdc2,   SVC_FDC2
        IENTRY  ient_18,     SVC_INT18
        IENTRY  ient_19,     SVC_INT19
        IENTRY  ient_1a,     SVC_INT1A
        IENTRY  ient_1b_own,  SVC_INT1B
        IENTRY  ient_1c,     SVC_INT1C
        IENTRY  ient_1e,     SVC_INT1E
        IENTRY  ient_1f,     SVC_INT1F

/*
 * The common prologue.
 *
 * THE HIGH HALF OF ESP IS NOT OURS AND IS NOT ZERO.  In real mode the
 * stack is addressed by SP alone -- push and pop touch only the low 16
 * bits -- so a caller that has been in protected mode arrives with
 * whatever its 32-bit ESP last held still sitting in the top half.
 * Windows 2000's NTLDR does exactly that: it thunks down to real mode
 * to reach the BIOS with SS:SP = 2342:7FA8 and ESP = 0x00067FA8.
 *
 * That matters because gcc -m16 addresses our own locals and arguments
 * with `addr32` displacements off %esp, which uses all 32 bits.  With
 * the stale half in place the very first instruction of bios_dispatch
 * reads 0x67FB8 in a segment whose limit is 0xFFFF -- a #GP, taken on
 * our own shared fault stub, which IRETs straight back onto the same
 * instruction.  The machine spins there forever: a fault storm that
 * looks exactly like an interrupt storm from the outside (the giveaway
 * is that the saved flags have IF clear, which no hardware IRQ could
 * produce).
 *
 * So clear it, and put the caller's value back on the way out -- it is
 * their register, not ours.
 */
int_common:
        popw    %ax                     /* the service id */
        cld
        movl    %esp, %ecx              /* the caller's ESP, dirty half */
        movzwl  %sp, %esp
        movw    %sp, %bp                /* &frame; BP itself is saved */
        pushl   %ecx
        pushw   %ss
        popw    %ds                     /* DS = SS, as gcc -m16 requires */
        movzwl  %ax, %eax
        pushl   %eax                    /* arg 2: svc                  */
        movzwl  %bp, %eax
        pushl   %eax                    /* arg 1: struct bregs *       */
        calll   bios_dispatch
        addw    $8, %sp
        popl    %ecx
        andl    $0xffff0000, %ecx
        orl     %ecx, %esp              /* the caller's high half back */
        popal
        popw    %ds
        popw    %es
        iret

/*
 * INT 1Bh -- the DISK BIOS, and the one vector that is a DISPATCHER
 * rather than a handler.
 *
 * A disk module (an expansion ROM, or the NEC ROM's own D8000 module)
 * claims device classes by writing its segment's high byte into the
 * work-area table at 0x04B0 + class, where class is the high nibble of
 * the DA/UA in AL.  A class with no module registered falls through to
 * the firmware's own handler.
 *
 * The dispatch protocol is fixed by the modules, not by us:
 *
 *   - the module is entered by a far JMP, not a call, and ends with its
 *     own `iret` -- it owns the interrupt from that point;
 *   - an expansion ROM (55 AA header) is entered at +0x30, a flash
 *     module at +0x18;
 *   - the far pointer it is reached through is the work-area pair at
 *     0x04AC/0x04AE, which the dispatcher fills in on every call;
 *   - it finds a NINE-WORD register frame on the stack, pushed in the
 *     order below, with BP pointing at the saved AX.  The AIC-7860's
 *     entry reads its function from [bp+0x51] and returns status and
 *     carry by writing [bp+0x51] and [bp+0x66], i.e. it reaches past
 *     its own frame into the interrupt frame underneath.
 *
 * That last point is why this stub builds the frame by hand instead of
 * using the bregs.h one: the offsets are the module's ABI, so the
 * layout and the push order have to be exactly what a PC-98 disk module
 * expects.
 *
 * With no module registered for the class we unwind back to a bare
 * interrupt frame and fall into our own handler, which is an ordinary
 * IENTRY.
 */
        .globl ient_1b
ient_1b:
        sti
        cld
        pushw   %ds
        pushw   %si
        pushw   %di
        pushw   %es
        pushw   %bp
        pushw   %dx
        pushw   %cx
        pushw   %bx
        pushw   %ax
        movw    %sp, %bp
        xorw    %bx, %bx
        movw    %bx, %ds
        shrb    $4, %al
        movw    $WA_DISK_XROM, %bx
        xlatb
        testb   %al, %al
        jz      1f

        movb    %al, %ah
        xorb    %al, %al                /* AX = the module's segment */
        movw    %ax, WA_SCAN_SEG
        movw    %ax, %es
        movw    $0x0018, %ax            /* flash module entry */
        cmpw    $0xaa55, %es:0x0        /* an expansion ROM header */
        jne     2f
        movw    $0x0030, %ax            /* expansion ROM entry */
2:      movw    %ax, WA_SCAN_PHASE
        movw    (%bp), %ax              /* the caller's AX back */
        movw    $WA_SCAN_PHASE, %bx
        ljmp    *(%bx)

1:      popw    %ax
        popw    %bx
        popw    %cx
        popw    %dx
        popw    %bp
        popw    %es
        popw    %di
        popw    %si
        popw    %ds
        jmp     ient_1b_own

/*
 * A far-callable "this service is not fitted" stub.
 *
 * Several work-area cells hold FAR POINTERS the OS may call: the two
 * floppy entry points at 0x05CC and 0x05F8, and the disk entries at
 * 0x05E8/0x05EC.  On a machine running the NEC ROM they are filled in
 * even with no drive attached.  Leaving them zero would send a caller
 * to 0000:0000; pointing them here makes the call return an error,
 * which is what the machine actually is.
 */
        .globl bios_notfitted
bios_notfitted:
        movb    $0x60, %ah              /* device not ready */
        stc
        lret

/* ---- the vectors that stay in assembly ---- */

        .globl ient_dummy
ient_dummy:
        iret

/*
 * NMI is a memory parity error on this machine.  Disable further NMIs
 * (port 50h) and stop: continuing after a parity error would be worse
 * than halting, and there is no way to report it yet.
 */
        .globl ient_nmi
ient_nmi:
        pushw   %ax
        movb    $0x08, %al
        outb    %al, $0x50
        popw    %ax
1:      hlt
        jmp     1b

/* master-PIC EOI only, for the IRQs nothing claims */
        .globl ient_master
ient_master:
        pushw   %ax
        movb    $0x20, %al
        outb    %al, $0x00
        IODELAY
        popw    %ax
        iret

/* slave first, then master: the slave cascades on master IR7 */
        .globl ient_slave
ient_slave:
        pushw   %ax
        movb    $0x20, %al
        outb    %al, $0x08
        IODELAY
        movb    $0x20, %al
        outb    %al, $0x00
        IODELAY
        popw    %ax
        iret

/*
 * IRQ 0, the interval timer.  Count the INT 1Ch interval down in the
 * work area; while it has not expired, chain INT 1Ch AH=03 on every
 * tick -- the OS hooks vector 1Ch and counts time through exactly that,
 * and the default handler reloads the PIT and unmasks IRQ 0.  On expiry
 * mask IRQ 0 and deliver the user callback through vector 7, where
 * INT 1Ch AH=02 stored it.
 *
 * Interrupt-flag discipline is load-bearing: interrupts stay off across
 * the EOI and come back on for the chained software interrupt.
 */
        .globl ient_timer
ient_timer:
        sti
        pushw   %ax
        pushw   %ds
        xorw    %ax, %ax
        movw    %ax, %ds
        decw    WA_TIMER_COUNT
        popw    %ds
        cli
        jz      2f
        movb    $0x20, %al
        outb    %al, $0x00
        IODELAY
        sti
        movb    $0x03, %ah
        int     $0x1c
        popw    %ax
        iret
2:      inb     $0x02, %al
        orb     $0x01, %al              /* mask IRQ0 again */
        IODELAY
        outb    %al, $0x02
        movb    $0x20, %al
        outb    %al, $0x00
        IODELAY
        sti
        popw    %ax
        int     $0x07                   /* user interval callback */
        iret

/*
 * The vector table the cold path copies into 0000:0000-007F: 32 words,
 * one entry offset per vector, all with segment CS (= FD80).  Order and
 * assignment follow the IVT read out of a booted Xa7 --
 * every firmware vector there lives in one segment.
 */
        .section .rodata
        .balign 2
        .globl ivt_template
ivt_template:
        .word   ient_dummy              /* 00 divide error            */
        .word   ient_dummy              /* 01 single step             */
        .word   ient_nmi                /* 02 NMI / parity            */
        .word   ient_dummy              /* 03 breakpoint              */
        .word   ient_dummy              /* 04 overflow                */
        .word   ient_dummy              /* 05 COPY key user vector    */
        .word   ient_dummy              /* 06 STOP key user vector    */
        .word   ient_dummy              /* 07 interval timer callback */
        .word   ient_timer              /* 08 IRQ0 timer              */
        .word   ient_kbd                /* 09 IRQ1 keyboard           */
        .word   ient_master             /* 0a IRQ2 CRT vsync          */
        .word   ient_master             /* 0b IRQ3                    */
        .word   ient_master             /* 0c IRQ4 RS-232C            */
        .word   ient_master             /* 0d IRQ5                    */
        .word   ient_master             /* 0e IRQ6                    */
        .word   ient_master             /* 0f IRQ7 cascade            */
        .word   ient_slave              /* 10 IRQ8                    */
        .word   ient_slave              /* 11 IRQ9 disk               */
        .word   ient_fdc1               /* 12 FDC BIOS group 1        */
        .word   ient_fdc2               /* 13 FDC BIOS group 2        */
        .word   ient_slave              /* 14 IRQ12                   */
        .word   ient_slave              /* 15 IRQ13 mouse             */
        .word   ient_slave              /* 16 IRQ14                   */
        .word   ient_slave              /* 17 IRQ15                   */
        .word   ient_18                 /* 18 CRT / keyboard BIOS     */
        .word   ient_19                 /* 19 RS-232C BIOS            */
        .word   ient_1a                 /* 1a printer / CMT / PCI     */
        .word   ient_1b                 /* 1b DISK BIOS               */
        .word   ient_1c                 /* 1c TIMER BIOS              */
        .word   ient_dummy              /* 1d                         */
        .word   ient_1e                 /* 1e "no system"             */
        .word   ient_1f                 /* 1f extended BIOS           */
