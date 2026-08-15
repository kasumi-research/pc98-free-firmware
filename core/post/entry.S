/*
 * ITF entry — the first instruction the machine executes.
 *
 * Reset fetches FFFFFFF0, which the machine aliases onto the
 * E8000-FFFFF composite, so the CPU lands on BANK4's reset vector at
 * bank offset 0x7FF0.  That is `jmp f800:4000` and brings us here with
 * CS = F800, IP = 0x4000.
 *
 * Three phases, in this order for a reason:
 *
 *   0. ROM-resident and STACKLESS.  No call, no push: there is no
 *      stack, and on real hardware there is not yet any DRAM either.
 *      All this does is open the serial line and say hello, so that a
 *      machine which dies in phase 1 still tells us it got this far.
 *   1. Chipset and DRAM bring-up — still stackless (see below).
 *   2. Point DS/SS at the data segment, build a stack, zero .bss and
 *      enter C.  CS never changes: we keep running from ROM.
 *
 * Phases 0 and 1 run with DS = CS, and may not use a stack at all.
 * That is not a self-imposed hardship, it is the situation: at power-on
 * there is no DRAM to put a stack in, so a firmware that wants to run
 * before it has configured memory cannot use one.  The NEC ROM is in
 * the same position and takes a stack only once memory is up, in the
 * tail of the IVT.  Anything phase 1 needs uses the register-link
 * convention in include/pc98/asm.h instead.
 *
 * Phase 2 does NOT copy the image to RAM.  gcc -m16 requires DS == SS
 * (it will pass a pointer to a stack local to code that dereferences
 * through DS), so DS must be the RAM stack segment — which leaves
 * .rodata unreachable through DS.  Every read of our own image goes
 * through the rom_* accessors instead.  The payoff is that POST can
 * memory-test all of DRAM, because no firmware code lives in it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/serial.h>
#include <pc98/layout.h>
#include <pc98/asm.h>

        .code16
        .section .entry, "ax"
        .globl _start
_start:
        cli
        cld

#ifdef CONFIG_SMP
        /*
         * ---------------- the application processor ----------------
         *
         * Only the bootstrap processor runs POST.  An AP that reaches
         * the reset vector at all should not be here -- it is supposed
         * to be parked in wait-for-SIPI -- but "should not" is not a
         * guarantee, and the consequence of getting it wrong is not
         * subtle: two CPUs run POST at once, their serial output
         * interleaves mid-word, and they program the chipset from under
         * each other.  An AP escaping wait-for-SIPI is an emulator/
         * hardware bug, not something this firmware can fix outright --
         * the guard below is the belt to whatever reset logic is the
         * braces.
         *
         * IA32_APIC_BASE bit 8 is the BSP flag, set by the hardware at
         * power-on and readable before anything else is initialised --
         * no stack, no DRAM, no chipset.  An AP parks here.  Parking
         * costs it nothing later: INIT is not a maskable interrupt, so
         * the SIPI sequence the OS (or a future POST) sends still
         * resets it out of this loop and starts it at the SIPI vector.
         *
         * This is only compiled for models that have a second CPU; the
         * MSR is a Pentium-and-later register and the Xa7 path must not
         * grow a dependency on it.
         */
        movl    $0x1b, %ecx             /* IA32_APIC_BASE */
        rdmsr
        testb   $0x01, %ah              /* bit 8: this CPU is the BSP */
        jnz     1f
0:      cli
        hlt
        jmp     0b
1:
#endif

        /* CS is F800; make DS match so ROM strings need no override */
        movw    %cs, %ax
        movw    %ax, %ds

        /* ---------------- phase 0: serial, stackless ---------------- */

        movw    $SER_LCR, %dx
        movb    $0x80, %al              /* DLAB */
        outb    %al, %dx
        movw    $SER_THR, %dx
        movb    $1, %al                 /* divisor 1 = 115200 */
        outb    %al, %dx
        movw    $SER_IER, %dx
        xorb    %al, %al
        outb    %al, %dx
        movw    $SER_LCR, %dx
        movb    $0x03, %al              /* 8N1 */
        outb    %al, %dx
        movw    $SER_FCR, %dx
        movb    $0x07, %al              /* FIFOs on + reset both */
        outb    %al, %dx
        movw    $SER_MCR, %dx
        movb    $0x03, %al              /* DTR | RTS */
        outb    %al, %dx

        /*
         * puts, open-coded because a call needs a stack.  The THRE poll
         * is unbounded, which is safe for the same reason the C driver
         * is stateless: an absent UART floats the bus high, LSR reads
         * 0xFF, THRE tests set, and the loop falls straight through.
         */
        movw    $banner, %si
1:      lodsb
        testb   %al, %al
        jz      3f
        movb    %al, %bl
        movw    $SER_LSR, %dx
2:      inb     %dx, %al
        testb   $LSR_THRE, %al
        jz      2b
        movw    $SER_THR, %dx
        movb    %bl, %al
        outb    %al, %dx
        jmp     1b
3:

        /* ------------- phase 1: chipset / DRAM bring-up ------------- */
        /*
         * Still stackless.  hal_early_init takes its return address in
         * %sp (link level 0) and may clobber everything else; the
         * remaining DRAM work is listed in its TODOs.
         */
        POSTCODE 0x01
        CALLR   sp, hal_early_init

        /* ---------- phase 2: stack in RAM, enter C from ROM --------- */
        /*
         * CS stays F800.  DS = SS = the data segment, so .bss and
         * locals work normally and .rodata does NOT — it is at CS and
         * must be read with rom_* (romdata.h).
         */
        movw    $SEG_ITF_DATA, %ax
        movw    %ax, %ds
        movw    %ax, %ss                /* mov-to-SS masks interrupts   */
        movl    $ITF_STACK_TOP, %esp    /* for one instruction: keep    */
        movw    %ax, %es                /* the SP load adjacent         */

        /* zero .bss (NOLOAD, so it was never in the ROM image) */
        movw    $_bss_start, %di
        movw    $_bss_end, %cx
        subw    %di, %cx
        xorb    %al, %al
        rep     stosb

        calll   itf_main                /* i386 ABI, as gcc -m16 emits */
5:      cli
        hlt
        jmp     5b

banner:
        .asciz  "\r\n\r\nPC-98 free firmware: ITF entry\r\n"

/*
 * The power-on reset vector.  Byte-exact and non-negotiable:
 * EA 00 40 00 F8 = `jmp f800:4000`, the entry the machine's power-on
 * bank mapping requires.  tools/mkflash.py re-checks these bytes in the
 * assembled image, so a change in how gas encodes this cannot slip
 * through.
 */
        .section .resetvec, "a"
        .code16
        ljmp    $SEG_ROM_WINDOW, $ITF_ENTRY_OFF
