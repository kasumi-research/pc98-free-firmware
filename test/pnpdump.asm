; PnP BIOS oracle dumper — a PC-98 fixed-disk boot sector.
;
; The firmware loads this at 1FC0:0000 and jumps to it.  It finds the
; $PnP installation structure the firmware publishes, calls the PnP BIOS
; through its real-mode entry, and dumps every system device node over
; the 16550 at 0x238 (115200 8N1 — the self-test suite's channel).
;
; Run it on the REAL firmware to capture the reference node list, then on
; ours and diff: Windows 98 (Xa7) and Windows 2000 (RvII26) both enumerate
; through this interface, so what we publish has to match field for field.
;
;   $PnP scan   F0000-FFFFF, 16-byte aligned, byte sum of `length` == 0
;   FUNC 00h    Get Number of System Device Nodes  -> count, max node size
;   FUNC 01h    Get System Device Node             -> the node itself
;   INT 1Fh AH=CEh AL=02/03  the PC-98 spelling of the same services
;
; SPDX-License-Identifier: MIT
        bits 16
        org 0

%define SER_THR 0x238
%define SER_LSR 0x23d

; Sector 0 is only the NEC IPL header and the boot signature; the dumper
; itself starts at 0x200.  The firmware loads 1 KiB, so sector 1 is
; already in memory when this jump is taken.
        jmp entry                       ; 3 bytes
        nop
        db "IPL1"

; ---- dump ES:BX for CX bytes, 16 per line ----
dump:
.line:  push cx
        mov ax, es
        call hexw
        mov al, ':'
        call putc
        mov ax, bx
        call hexw
        mov al, ' '
        call putc
        mov cx, 16
.byte:  mov al, [es:bx]
        call hexb
        mov al, ' '
        call putc
        inc bx
        loop .byte
        call crlf
        pop cx
        sub cx, 16
        jnz .line
        ret

; ---- serial ----
putc:
        push ax
        push dx
        mov ah, al
        mov dx, SER_LSR
.wait:  in al, dx
        test al, 0x20
        jz .wait
        mov dx, SER_THR
        mov al, ah
        out dx, al
        pop dx
        pop ax
        ret

puts:
        push ax
.lp:    lodsb
        test al, al
        jz .end
        call putc
        jmp .lp
.end:   pop ax
        ret

crlf:
        push ax
        mov al, 13
        call putc
        mov al, 10
        call putc
        pop ax
        ret

hexb:                                   ; AL
        push ax
        push cx
        mov cl, al
        shr al, 4
        call nib
        mov al, cl
        and al, 0x0f
        call nib
        pop cx
        pop ax
        ret

hexw:                                   ; AX
        push ax
        mov al, ah
        call hexb
        pop ax
        push ax
        call hexb
        pop ax
        ret

nib:
        and al, 0x0f
        add al, '0'
        cmp al, '9'
        jbe .ok
        add al, 7
.ok:    call putc
        ret

        times 0x1fa - ($ - $$) db 0
        db 0x80, 0x00, 0x0d, 0x00
        dw 0xaa55

entry:
        mov ax, cs
        mov ds, ax
        mov ss, ax
        mov sp, 0x0f00
        cld

        mov si, banner
        call puts

        ; ---- find $PnP in F0000-FFFFF ----
        mov ax, 0xf000
        mov es, ax
        xor bx, bx
.scan:
        cmp dword [es:bx], 0x506e5024   ; "$PnP"
        jne .next
        push bx
        movzx cx, byte [es:bx + 5]
        xor al, al
        mov si, bx
.sum:   add al, [es:si]
        inc si
        loop .sum
        pop bx
        test al, al
        jz .found
.next:
        add bx, 16
        jnz .scan
        mov si, m_nopnp
        call puts
        jmp ce_sweep

.found:
        mov si, m_found
        call puts
        mov ax, es
        call hexw
        mov al, ':'
        call putc
        mov ax, bx
        call hexw
        call crlf

        mov ax, [es:bx + 13]
        mov [pnp_off], ax
        mov ax, [es:bx + 15]
        mov [pnp_seg], ax
        mov ax, [es:bx + 27]
        mov [pnp_ds], ax

        ; ---- sweep the far-call function numbers ----
        mov word [fn], 0
.fnloop:
        push word [pnp_ds]
        push ds
        push word outw2
        push ds
        push word outw1
        push word [fn]
        call far [pnp_off]
        add sp, 12

        push ax
        mov si, m_far
        call puts
        mov ax, [fn]
        call hexb
        mov al, '/'
        call putc
        pop ax
        call hexw
        mov al, ' '
        call putc
        mov ax, [outw1]
        call hexw
        mov al, ' '
        call putc
        mov ax, [outw2]
        call hexw
        call crlf

        inc word [fn]
        cmp word [fn], 0x0a
        jb .fnloop

ce_sweep:
        ; ---- sweep INT 1Fh AH=CEh subfunctions ----
        mov word [fn], 0
.ce:
        mov si, m_ce
        call puts
        mov ax, [fn]
        call hexb
        mov al, ' '
        call putc

        push ds
        pop es
        mov bx, buf
        mov cx, 0
        mov dx, 0
        mov ax, 0xce00
        or ax, [fn]
        int 0x1f

        call hexw                       ; AX after
        mov al, ' '
        call putc
        mov ax, bx
        call hexw
        mov al, ' '
        call putc
        mov ax, cx
        call hexw
        mov al, ' '
        call putc
        mov ax, dx
        call hexw
        call crlf

        inc word [fn]
        cmp word [fn], 0x08
        jb .ce

done:
        mov si, m_done
        call puts
.halt:  hlt
        jmp .halt

banner:  db 13,10,"=== PNPDUMP ===",13,10,0
m_nopnp: db "NO $PnP",13,10,0
m_found: db "$PnP at ",0
m_far:   db "FAR fn/status ",0
m_ce:    db "CE ",0
m_done:  db 13,10,"=== END ===",13,10,0

pnp_off: dw 0
pnp_seg: dw 0
pnp_ds:  dw 0
outw1:   dw 0
outw2:   dw 0
fn:      dw 0
buf equ 0x800                   ; past the 1 KiB the IPL loads

        times 0x400 - ($ - $$) db 0
