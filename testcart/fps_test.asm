; fps_test.asm - a still picture whose frame rate changes every 128 frames, to compare the emulator's
; brightness with a real Vectrex as a game slows down (on the tube the picture keeps its brightness; only
; the mains wobble grows). The BIOS starts a frame every Vec_Rfrsh cycles (30000 = 50 fps at 1.5 MHz).
; Phases (the name on screen): 50 FPS, 40 FPS, 30 FPS, 25 FPS, 50/25 ALTERNATE, IRREGULAR - then again.
; Build: lwasm --format=raw -o fps_test_raw.bin fps_test.asm, then pad to 32768 bytes with $FF.
; -DLINE: one long horizontal line instead of the boxes (fps_line.bin, roms\FPS Line Test.bin).

Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Print_Str_d     equ $F37A
Draw_VLc        equ $F3CE
Draw_Line_d     equ $F3DF
Vec_Rfrsh       equ $C83D
VIA_t1_cnt_lo   equ $D004

Count           equ $C880               ; frames in this phase
Phase           equ $C881               ; 0..5
Step            equ $C882               ; the frame within the alternate / irregular pattern

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$40
        fcc     "FPS TEST"
        fcb     $80
        fcb     0

        clr     Count
        clr     Phase
        clr     Step
Main:   ; this frame's length (Vec_Rfrsh, cycles), from the phase
        ldb     Phase
        cmpb    #4
        bhs     Pattern
        lslb
        ldx     #Fixed
        ldd     b,x
        bra     SetLen
Pattern:
        ldb     Step
        incb
        andb    #7
        stb     Step
        lda     Phase
        cmpa    #4
        bne     Irreg
        ldx     #Alt
        bra     PatLen
Irreg:  ldx     #Irr
PatLen: lslb
        ldd     b,x
SetLen: exg     a,b                     ; (Wait_Recal reads Vec_Rfrsh low byte first)
        std     Vec_Rfrsh
        jsr     Wait_Recal
        ; the next phase after 128 frames
        inc     Count
        lda     Count
        cmpa    #128
        blo     Draw
        clr     Count
        lda     Phase
        inca
        cmpa    #6
        blo     PhOk
        clra
PhOk:   sta     Phase
Draw:   lda     #$7F
        jsr     Intensity_a
 ifdef LINE
        ; (the LINE build: one long horizontal line across the middle, two strokes - for a line-by-line check)
        jsr     Reset0Ref
        lda     #$FF
        sta     VIA_t1_cnt_lo
        ldd     #$00A0                  ; (y 0, x -96)
        jsr     Moveto_d
        ldd     #$0060                  ; (dy 0, dx 96)
        jsr     Draw_Line_d
        ldd     #$0060
        jsr     Draw_Line_d
        bra     Text
 endc
        ; a ring of 8 boxes and one in the middle - lines all over the screen
        ldu     #Places
Boxes:  ldd     ,u++
        cmpd    #$8080
        beq     Text
        pshs    u
        pshs    d
        jsr     Reset0Ref
        lda     #$60
        sta     VIA_t1_cnt_lo
        puls    d
        jsr     Moveto_d
        ldx     #Box
        jsr     Draw_VLc
        puls    u
        bra     Boxes
Text:   jsr     Reset0Ref
        ldu     #Title
        ldd     #$58C0
        jsr     Print_Str_d
        jsr     Reset0Ref
        ldb     Phase
        lslb
        ldx     #Names
        ldu     b,x
        ldd     #$98C0
        jsr     Print_Str_d
        lbra    Main

Fixed:  fdb     30000,37500,50000,60000            ; 50, 40, 30, 25 fps
Alt:    fdb     30000,60000,30000,60000,30000,60000,30000,60000
Irr:    fdb     30000,30000,30000,52000,30000,64000,30000,40000
Places: fdb     $3080,$3000,$3060,$00A0,$0060,$C0A0,$C000,$C060,$0000
        fdb     $8080
Box:    fcb     3                       ; a 32 x 32 box (dy, dx)
        fcb     0,32,-32,0,0,-32,32,0
Title:  fcc     "FPS TEST"
        fcb     $80
N50:    fcc     "50 FPS"
        fcb     $80
N40:    fcc     "40 FPS"
        fcb     $80
N30:    fcc     "30 FPS"
        fcb     $80
N25:    fcc     "25 FPS"
        fcb     $80
NAlt:   fcc     "50/25 ALTERNATE"
        fcb     $80
NIrr:   fcc     "IRREGULAR"
        fcb     $80
Names:  fdb     N50,N40,N30,N25,NAlt,NIrr
