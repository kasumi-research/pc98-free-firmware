/*
 * The BIOS32 service directory and the 32-bit $PCI service.
 *
 * Windows finds the firmware's PCI BIOS through this, and without it
 * it does not find one at all: on a reference boot the guest makes 18
 * INT 1Fh AH=CCh calls (a driver at cs=033f doing the installation
 * check, finding 1023:9660, then sizing the Trident's BAR0 by writing
 * FFFF:FFFF and restoring it), and on ours it made zero.  A ring-0 VxD
 * is on record reading the NEC directory at 0xF4C40 and the entry
 * pointer at +4 through Windows' high-linear alias during boot
 * (scratch/logs/b32read.log).  With no PCI BIOS in evidence its
 * display driver takes the probe-and-set branch instead of the PnP
 * one, sets the mode itself, and that is recorded as a settings
 * change -- the pre-shell "新しいディスプレイの設定 ... restart"
 * dialog.
 *
 * The structure is what an OS scans for: it has to sit on a 16-byte
 * boundary with its bytes summing to zero, and it has to be inside the
 * window the scan actually covers -- F0000-F7FFF, which is why this
 * lives in BANK6 and not with the rest of the BIOS (banks/bank6.ld.in
 * has the measurement).  tools/mkflash.py fills the checksum in for
 * the same reason it does the MPS tables and $PnP: an assembler cannot
 * add up bytes it has not emitted yet.
 *
 * BANK6 is placed so a symbol's value IS its offset from F0000
 * (layout.h), which is what makes the absolute addresses below simple:
 * physical = F0000 + symbol, and the service offset the directory
 * hands back is the symbol itself.
 *
 * WHAT THE REFERENCE DOES, measured from scratch/re/bank0_lo.bin
 * (BANK0 lo half, the D8000 module; jump table +0x30 and +0x35):
 *
 *   directory @0x10f0  EBX must be 0 or AL=0x81; EAX '$PCI' or '$ACF'
 *                      or AL=0x80; answers EBX=base ECX=len EDX=offset
 *   service   @0x1155  requires AL bit 7 SET, then dispatches on
 *                      AH = CCh/B1h -> PCI, AH = CEh/B4h -> ACFG
 *
 * That AL bit-7 gate is not a quirk of the shim -- the handler at
 * 0x14d0 dispatches on AL = 0x81..0x8F, i.e. the PCI BIOS function
 * numbers with bit 7 set.  So the 32-bit spelling of a function is
 * 0x80|fn while the real-mode INT 1Fh AH=CCh spelling of the same
 * function is plain fn.  Getting that backwards makes every call fail
 * closed, silently, which is why it is written down here.
 *
 * core/pci/pcibios.c is the normative implementation of these
 * functions; this file is the 32-bit protected-mode spelling of the
 * same service and the two have to agree.  They cannot share code: the
 * C is 16-bit real mode and there is no 16-bit code descriptor to call
 * it through from here.
 *
 * SPDX-License-Identifier: MIT
 */

#include <pc98/layout.h>

/* Physical base of the F0000 window, and so of every symbol here. */
#define BIOS32_BASE     (SEG_BANK6_WINDOW << 4)

/* "$PCI", little-endian. */
#define SERVICE_PCI     0x49435024

/* Status codes, as in core/pci/pcibios.c. */
#define PCIBIOS_OK              0x00
#define PCIBIOS_BAD_FUNC        0x81
#define PCIBIOS_NOT_FOUND       0x86
#define PCIBIOS_BAD_REGISTER    0x87

        .section .bios32, "ax"

/* ---- the directory an OS scans for ---- */

        .balign 16
        .globl bios32_struct
bios32_struct:
        .ascii  "_32_"
        .long   BIOS32_BASE + bios32_entry
        .byte   0                       /* revision 0                  */
        .byte   1                       /* length, 16-byte paragraphs  */
        .byte   0                       /* checksum, filled by mkflash */
        .byte   0, 0, 0, 0, 0           /* reserved, must be zero      */

/*
 * The service directory entry point.  Called with a far call in flat
 * 32-bit protected mode: EAX = service identifier, EBX = function
 * (only 0, "return the service entry", is defined).  Answers AL = 0
 * with EBX = physical base, ECX = length, EDX = entry offset from that
 * base; AL = 0x80 for a service we do not have, 0x81 for a function
 * that does not exist.
 *
 * We publish only $PCI.  The reference also publishes $ACF, the ACFG
 * configuration-store service -- deliberately not offered here: its
 * write side is not implemented and would hand an OS a store we cannot
 * keep (notes.md, "Deliberate gaps", item 5).
 */
        .code32
        .globl bios32_entry
bios32_entry:
        pushfl
        testl   %ebx, %ebx
        jnz     .Ldir_badfunc
        cmpl    $SERVICE_PCI, %eax
        jne     .Ldir_nosvc

        movl    $BIOS32_BASE, %ebx      /* the bank window            */
        movl    $BANK_SIZE, %ecx        /*   ... all of it            */
        movl    $bios32_pci, %edx       /* symbol == offset from base */
        movb    $0x00, %al
        popfl
        lret

.Ldir_nosvc:
        movb    $0x80, %al
        popfl
        lret

.Ldir_badfunc:
        movb    $0x81, %al
        popfl
        lret

/* ---- the $PCI service ---- */

/*
 * Entered by far call in flat 32-bit protected mode with AH = CCh (the
 * PC-98 spelling) or B1h (the PC/AT one) and AL = 0x80|function.
 * Returns status in AH and carry, everything else per the PCI BIOS
 * specification.
 *
 * Carry is returned the way the reference does it, and it is the only
 * way that works here: the entry saves the caller's flags and the exit
 * restores them, so setting CF in the flags register would be undone
 * by the popfl.  Both paths edit the SAVED copy on the stack instead.
 * After `pushfl; pushl %ebp; movl %esp,%ebp` that copy is at 4(%ebp).
 */
        .globl bios32_pci
bios32_pci:
        pushfl
        pushl   %ebp
        movl    %esp, %ebp
        cli

        testb   $0x80, %al              /* the 32-bit spelling has it set */
        jz      .Lpci_badfunc
        cmpb    $0xcc, %ah
        je      1f
        cmpb    $0xb1, %ah
        jne     .Lpci_badfunc
1:
        andb    $0x7f, %al              /* now the plain function number */

        cmpb    $0x01, %al
        je      .Lpci_present
        cmpb    $0x02, %al
        je      .Lpci_find_device
        cmpb    $0x03, %al
        je      .Lpci_find_class
        cmpb    $0x08, %al
        je      .Lpci_read_byte
        cmpb    $0x09, %al
        je      .Lpci_read_word
        cmpb    $0x0a, %al
        je      .Lpci_read_dword
        cmpb    $0x0b, %al
        je      .Lpci_write_byte
        cmpb    $0x0c, %al
        je      .Lpci_write_word
        cmpb    $0x0d, %al
        je      .Lpci_write_dword

/*
 * Everything else -- 06h special cycle, 0Eh get IRQ routing, 0Fh set
 * IRQ -- is genuinely absent rather than stubbed, and says so.  The
 * 16-bit service does not implement them either; when it grows them
 * they belong here too.
 */
.Lpci_badfunc:
        movb    $PCIBIOS_BAD_FUNC, %ah
        jmp     .Lpci_fail

/*
 * Installation check.  The version is the reference's: its AL=01
 * answers BX=0200h, PCI BIOS 2.0.  Only bus 0 exists on either
 * machine, so the last bus number is zero.
 */
.Lpci_present:
        movl    $0x20494350, %edx       /* "PCI ", bytes 50 43 49 20 */
        movb    $0x01, %al              /* configuration mechanism 1 */
        movw    $0x0200, %bx
        movb    $0x00, %cl
        jmp     .Lpci_ok

/*
 * Find a device by ID (CX = device, DX = vendor) or by class (ECX =
 * class code, 24 bits), skipping SI matches.  Returns BH = bus,
 * BL = device/function.
 *
 * Only bus 0 is scanned, matching the installation check's "last bus
 * = 0" and the 16-bit implementation's bdf_ok().  An absent function
 * reads back FFFFFFFF, which no valid target matches, so nothing has
 * to special-case it.
 */
.Lpci_find_device:
        movzwl  %cx, %edi               /* EDI = device<<16 | vendor */
        shll    $16, %edi
        movzwl  %dx, %eax
        orl     %eax, %edi
        xorl    %edx, %edx              /* EDX = config register 0   */
        jmp     .Lfind_scan

.Lpci_find_class:
        movl    %ecx, %edi
        andl    $0x00ffffff, %edi       /* EDI = class code          */
        movl    $0x08, %edx             /* from config register 8    */

.Lfind_scan:
        xorl    %ebx, %ebx              /* EBX = devfn, bus 0        */
1:      movl    %ebx, %eax
        shll    $8, %eax
        orl     %edx, %eax
        orl     $0x80000000, %eax
        pushl   %edx
        movw    $0x0cf8, %dx
        outl    %eax, %dx
        movw    $0x0cfc, %dx
        inl     (%dx), %eax
        popl    %edx
        testl   %edx, %edx              /* class search? shift off the */
        jz      2f                      /*   revision byte             */
        shrl    $8, %eax
2:      cmpl    %edi, %eax
        jne     3f
        testw   %si, %si
        jz      .Lpci_ok                /* EBX is already BH=0 BL=devfn */
        decw    %si
3:      incl    %ebx
        cmpl    $0x100, %ebx
        jb      1b
        movb    $PCIBIOS_NOT_FOUND, %ah
        jmp     .Lpci_fail

/* ---- configuration space ---- */

/*
 * Program CONFIG_ADDRESS from BX (bus:devfn) and DI (register number),
 * and leave DX pointing at the CONFIG_DATA lane for that register.
 * Clobbers EAX and EDX, which no caller below needs preserved.
 *
 * CONFIG_ADDRESS has bits 1:0 hardwired to zero -- on the real chipset
 * and in the model -- so the byte and word lanes come from the data
 * port address, not from the address register.
 */
.Lcfg_addr:
        movzwl  %bx, %eax
        shll    $8, %eax
        movl    %edi, %edx
        andl    $0xfc, %edx
        orl     %edx, %eax
        orl     $0x80000000, %eax
        movw    $0x0cf8, %dx
        outl    %eax, %dx
        movl    %edi, %edx
        andl    $0x03, %edx
        addl    $0x0cfc, %edx
        ret

/*
 * Alignment is the caller's job and the specification says so, but a
 * misaligned request has to be refused rather than quietly folded down
 * to the aligned one -- the caller would read a neighbouring register
 * and never know.
 */
.Lpci_read_byte:
        call    .Lcfg_addr
        inb     (%dx), %al
        movb    %al, %cl
        jmp     .Lpci_ok

.Lpci_read_word:
        testl   $1, %edi
        jnz     .Lpci_badreg
        call    .Lcfg_addr
        inw     (%dx), %ax
        movw    %ax, %cx
        jmp     .Lpci_ok

.Lpci_read_dword:
        testl   $3, %edi
        jnz     .Lpci_badreg
        call    .Lcfg_addr
        inl     (%dx), %eax
        movl    %eax, %ecx
        jmp     .Lpci_ok

.Lpci_write_byte:
        call    .Lcfg_addr
        movb    %cl, %al
        outb    %al, (%dx)
        jmp     .Lpci_ok

.Lpci_write_word:
        testl   $1, %edi
        jnz     .Lpci_badreg
        call    .Lcfg_addr
        movw    %cx, %ax
        outw    %ax, (%dx)
        jmp     .Lpci_ok

.Lpci_write_dword:
        testl   $3, %edi
        jnz     .Lpci_badreg
        call    .Lcfg_addr
        movl    %ecx, %eax
        outl    %eax, (%dx)
        jmp     .Lpci_ok

.Lpci_badreg:
        movb    $PCIBIOS_BAD_REGISTER, %ah
        jmp     .Lpci_fail

/* ---- exits: carry goes back through the saved flags ---- */

.Lpci_ok:
        movb    $PCIBIOS_OK, %ah
        andl    $~1, 4(%ebp)
        popl    %ebp
        popfl
        lret

.Lpci_fail:
        orl     $1, 4(%ebp)
        popl    %ebp
        popfl
        lret
