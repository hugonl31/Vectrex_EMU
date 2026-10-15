; wobble_test.asm - a still picture at a chosen frame rate: shows the mains hum wobble against the game's
; frame rate (README). The BIOS starts a frame every Vec_Rfrsh cycles (30000 = 50 fps at 1.5 MHz); a game
; whose frame takes longer than that runs slower - 37500 cycles = 40 fps. Built three times:
;   lwasm --format=raw -DREFRESH=30000 -o wobble_50.bin wobble_test.asm   (50 fps: in step with 50 Hz mains)
;   lwasm --format=raw -DREFRESH=37500 -o wobble_40.bin wobble_test.asm   (40 fps: out of step, it sways)
;   lwasm --format=raw -DREFRESH=60000 -o wobble_25.bin wobble_test.asm   (25 fps: in step again)
; then padded to 32768 bytes with $FF.

Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Print_Str_d     equ $F37A
Draw_VLc        equ $F3CE
Vec_Rfrsh       equ $C83D
VIA_t1_cnt_lo   equ $D004

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$40
        fcc     "WOBBLE TEST"
        fcb     $80
        fcb     0

Main:   ldd     #(REFRESH%256)*256+(REFRESH/256)   ; (Wait_Recal writes it low byte first)
        std     Vec_Rfrsh
        jsr     Wait_Recal
        lda     #$7F
        jsr     Intensity_a
        jsr     Reset0Ref               ; the frame: two boxes, one inside the other
        lda     #$FF
        sta     VIA_t1_cnt_lo
        ldd     #$4CC6                  ; (y 76, x -58)
        jsr     Moveto_d
        ldx     #Box
        jsr     Draw_VLc
        jsr     Reset0Ref
        lda     #$E0
        sta     VIA_t1_cnt_lo
        ldd     #$4CC6
        jsr     Moveto_d
        ldx     #Box
        jsr     Draw_VLc
        jsr     Reset0Ref               ; the text
        ldu     #Txt1
        ldd     #$20C8
        jsr     Print_Str_d
        jsr     Reset0Ref
        ldu     #Txt2
        ldd     #$E0D2
        jsr     Print_Str_d
        lbra    Main

Box:    fcb     7                       ; 8 lines (dy, dx)
        fcb     0,58,0,58,-76,0,-76,0,0,-58,0,-58,76,0,76,0
Txt1:   fcc     "VECTREX"
        fcb     $80
Txt2:   fcc     "WOBBLE TEST"
        fcb     $80
