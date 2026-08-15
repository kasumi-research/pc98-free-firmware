/*
 * ITF / POST — the C half, entered from entry.S once there is a stack.
 *
 * The step order follows the NEC ROM's, which we know from the order it
 * reports and fails in (see README.md §3).  Each step is a stub for
 * now; they fill in as the work lands.
 *
 * SPDX-License-Identifier: MIT
 */
#include <pc98/types.h>
#include <pc98/io.h>
#include <pc98/serial.h>
#include <pc98/layout.h>
#include <pc98/model.h>
#include <pc98/hwinit.h>
#include <pc98/pci.h>
#include <pc98/sdip.h>

/*
 * POST progress is reported two ways, as the NEC ROM does: a code
 * latched where a bus analyser can see it, and a serial line for us.
 * Keep the codes stable — they are the first thing to look at when a
 * boot dies with no output.
 */
void post(u8 code, const char *what)
{
    outb(PORT_POST, code);
    ser_puts("POST ");
    ser_hexb(code);
    ser_putc(' ');
    ser_puts(what);
    ser_crlf();
}

void itf_handoff(void);

void __attribute__((noreturn)) itf_main(void)
{
    ser_init();
    ser_puts("\r\n=== " MODEL_NAME " ===\r\n");
    ser_puts("ITF in C, stack up, .bss clear\r\n");

    post(0x01, "serial console");
    ser_puts("  16550 at 0x238: ");
    ser_puts(ser_present() ? "present" : "ABSENT (output discarded)");
    ser_crlf();

    /*
     * Everything below is the work queue.  Order is the NEC ROM's.
     * Each becomes a call as it lands.
     */
    post(0x02, "mainboard peripherals");
    hw_init();
    post(0x03, "PCI resource assignment");
    pci_assign();
    post(0x04, "shadow RAM populate     [TODO]");
#ifdef CONFIG_SMP
    post(0x05, "MTRR + microcode        [TODO]");
    post(0x06, "AP start, IOAPIC, MPS   [TODO]");
#endif
#ifdef CONFIG_SMM
    post(0x07, "SMBASE relocate + SMI   [TODO]");
#endif
    post(0x08, "SDIP battery store");
    sdip_init();
    post(0x09, "hand off to BIOS");
    ser_puts("  mapping BANK7 and jumping to fd80:0000\r\n");
    ser_mark('>');
    itf_handoff();              /* does not return */
    __builtin_unreachable();
}
