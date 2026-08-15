; Work-area / IVT oracle dumper — a PC-98 fixed-disk boot sector.
;
; The firmware loads this at 1FC0:0000 and jumps to it.  It hex-dumps
; the regions the firmware is responsible for leaving behind, over the
; 16550 at 0x238 (115200 8N1 — the self-test suite's channel):
;
;   0000:0000-07FF   IVT + BIOS work area
;   A3FE0-A3FFF      memory switches
;
; and a handful of live register/port values.  Run it on the REAL
; firmware to capture the reference, then on ours and diff: everything
; DOS or an option ROM reads has to match field for field.
;
; It also reports the register state it was ENTERED with, which is part
; of the IPL contract (the firmware passes the boot device in AL etc).
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

        times 0x1fa - ($ - $$) db 0
        db 0x80, 0x00, 0x0d, 0x00
        dw 0xaa55

entry:
        ; capture the entry register state before touching anything
        mov [cs:r_ax], ax
        mov [cs:r_bx], bx
        mov [cs:r_cx], cx
        mov [cs:r_dx], dx
        mov [cs:r_si], si
        mov [cs:r_di], di
        mov [cs:r_bp], bp
        mov [cs:r_ds], ds
        mov [cs:r_es], es
        mov [cs:r_ss], ss
        mov [cs:r_sp], sp
        pushf
        pop ax
        mov [cs:r_fl], ax

        mov ax, cs
        mov ds, ax
        mov ss, ax
        mov sp, 0x0f00
        cld

        mov si, banner
        call puts

        ; --- entry register state ---
        mov si, m_regs
        call puts
        mov si, r_ax
        mov cx, 12
.rl:    push cx
        mov ax, [si]
        call hexw
        mov al, ' '
        call putc
        pop cx
        inc si
        inc si
        loop .rl
        call crlf

        ; --- live port state the firmware leaves behind ---
        mov si, m_ports
        call puts
        mov dx, 0x0002          ; master 8259 IMR
        in al, dx
        call hexb
        mov al, ' '
        call putc
        mov dx, 0x000a          ; slave 8259 IMR
        in al, dx
        call hexb
        mov al, ' '
        call putc
        in al, 0x35             ; 8255 port C
        call hexb
        mov al, ' '
        call putc
        in al, 0x33
        call hexb
        mov al, ' '
        call putc
        in al, 0x31
        call hexb
        call crlf

        ; --- 0000:0000-07FF ---
        mov si, m_low
        call puts
        xor ax, ax
        mov es, ax
        xor bx, bx
        mov cx, 0x800
        call dump

        ; --- A3FE0-A3FFF (memory switches) ---
        mov si, m_msw
        call puts
        mov ax, 0xa3fe
        mov es, ax
        xor bx, bx
        mov cx, 0x20
        call dump

        mov si, m_done
        call puts
.halt:  hlt
        jmp .halt

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

banner: db 13,10,"=== WADUMP ===",13,10,0
m_regs: db "REGS ax bx cx dx si di bp ds es ss sp fl",13,10,0
m_ports: db "PORTS imr0 imr1 35 33 31",13,10,0
m_low:  db "--- LOW ---",13,10,0
m_msw:  db "--- MSW ---",13,10,0
m_done: db "=== END ===",13,10,0

r_ax:   dw 0
r_bx:   dw 0
r_cx:   dw 0
r_dx:   dw 0
r_si:   dw 0
r_di:   dw 0
r_bp:   dw 0
r_ds:   dw 0
r_es:   dw 0
r_ss:   dw 0
r_sp:   dw 0
r_fl:   dw 0

        times 0x400 - ($ - $$) db 0
