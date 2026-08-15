/*
 * Runtime BIOS entry — BANK7, physical FD800-FFFFF, CS = FD80.
 *
 * Three ways in, all through fd80:0000:
 *   - the ITF's hand-off at the end of POST;
 *   - the bank's own reset vector at FFFF0 (`jmp fd80:0000`), which a
 *     CPU-only reset (OUT F0h) re-enters -- BANK7 stays mapped, so the
 *     ITF does not run again;
 *   - a warm return from protected mode, which is the same entry with
 *     the 8255 SHUT0 flag cleared.
 *
 * The SHUT0 test therefore has to come first and has to be cheap: it is
 * the return path of every HIMEM/EMM386 protected-mode excursion.
 * Measured: port 35h bit7 clear = warm return (restore SS:SP from the
 * work area and retf), bit7 set = cold (POST already ran; init and
 * boot).  Power-on reads 0xB8 -- both flags set IS the cold signature.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/serial.h>
#include <pc98/layout.h>
#include <pc98/wa.h>
#include <pc98/model.h>

/*
 * fd80:0000 -- the published entry.  Three bytes of trampoline into the
 * BIOS proper, which is linked at CS = F800 so it can use the whole
 * bank (layout.h).
 */
        .code16
        .section .warment, "ax"
        .globl _bios_warm
_bios_warm:
        ljmp    $SEG_BIOS, $_bios_start

        .code16
        .section .entry, "ax"
        .globl _bios_start
_bios_start:
        cli
        cld

#ifdef CONFIG_SMP
        /*
         * Park an application processor, exactly as the ITF entry does
         * and for the same reason -- this is the OTHER reset target.
         * Once POST has mapped BANK7 the top of the ROM window reads
         * `jmp fd80:0000`, so a CPU that escapes wait-for-SIPI after the
         * hand-off arrives HERE rather than at the ITF, finds the
         * power-on SHUT0 flag set, and runs the whole cold path a second
         * time underneath the first.  Seen: two interleaved copies of
         * every BIOS message, and an option-ROM scan running twice.
         *
         * IA32_APIC_BASE bit 8 is the BSP flag; the check costs three
         * instructions and needs nothing initialised.
         */
        movl    $0x1b, %ecx             /* IA32_APIC_BASE */
        rdmsr
        testb   $0x01, %ah              /* bit 8: this CPU is the BSP */
        jnz     0f
9:      cli
        hlt
        jmp     9b
0:
#endif
        /*
         * A CPU reset masks A20 on this hardware (Undocumented 9801).
         * Do it here, at the reset target, so it is atomic with the
         * reset and cannot race a RETF back into a protected-mode
         * dance.
         */
        movb    $0x03, %al
        outb    %al, $0xf6

        xorw    %ax, %ax
        movw    %ax, %ds
        inb     $PORT_SYS_PORTC, %al
        testb   $0x80, %al              /* SHUT0 clear -> warm return */
        jz      warm_return

        /* ---- cold: take the boot stack and enter C ---- */
        /*
         * SS:SP = 0000:0400, i.e. the stack grows down through the tail
         * of the IVT.  That is where the NEC ROM puts its boot stack
         * too -- it hands a boot sector control with SS:SP = 0020:01D6,
         * measured.  Vectors 80h-FFh are not live until the OS installs
         * them, and by then we are running on the caller's stack.
         *
         * AX must be re-zeroed: the SHUT0 inb above left the 8255 port
         * C byte in AL, and loading THAT into SS put the C stack at
         * SS = 00B8 while DS stayed 0000 -- every %esp-relative access
         * and every pointer to a local silently disagreed about where
         * the stack was.  Found by sdip_init reading back its own
         * buffer differently through the two paths.
         */
        xorw    %ax, %ax
        movw    %ax, %ss                /* mov-to-SS masks interrupts   */
        movl    $BIOS_BOOT_SP, %esp     /* for one instruction: keep    */
        movw    %ax, %es                /* the SP load adjacent         */
        calll   bios_cold
1:      cli
        hlt
        jmp     1b

/*
 * The other side of the CPU-reset dance.  Restore SHUT0 before
 * returning: the dance cleared it to request this entry, and leaving it
 * clear would make the NEXT entry warm-return too -- including a
 * genuine reboot, which would RETF through whatever stale SS:SP the
 * last excursion left at 0404.  Registers are undefined after a reset
 * anyway, so AL is free.
 */
warm_return:
        movb    $0x0f, %al              /* 8255 bit set/reset: SHUT0 = 1 */
        outb    %al, $PORT_SYS_CTRL
        movw    WA_WARM_SS, %ss
        movw    WA_WARM_SP, %sp
        lret

/*
 * Warm-boot vector: EA 00 00 80 FD = `jmp fd80:0000`, byte-exact -- the
 * entry every protected-mode return and CPU-only reset lands on.
 * Verified by tools/mkflash.py.
 */
        .section .resetvec, "a"
        .code16
        ljmp    $SEG_BIOS_WARM, $0
