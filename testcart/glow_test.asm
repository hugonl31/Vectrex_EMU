; glow_test.asm - Glow Test: one vertical and one horizontal line, full length, moving across the screen and back -
; still, slow, medium, fast (256 frames each, then again). Buttons 1-4 hold one speed (still / slow / medium / fast).
; For the overlay's plastic glow: does it stay on the line as it moves, does it flicker.
; Build: lwasm --format=raw -o glow_test_raw.bin glow_test.asm, then pad to 32768 bytes with $FF (64 KB: twice).

Wait_Recal      equ $F192
Read_Btns       equ $F1BA
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Draw_Line_d     equ $F3DF
Vec_Buttons     equ $C811
Vec_Btn_State   equ $C80F
Pos             equ $C880
Dir             equ $C881
Frame           equ $C882
Phase           equ $C883
Hold            equ $C884
Rate            equ $C885
Vec_Rfrsh       equ $C83D
LIM             equ 96

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$48
        fcc     "GLOW TEST"
        fcb     $80
        fcb     0

Start:
        clr     Pos
        clr     Dir
        clr     Frame
        clr     Phase
        clr     Rate
        lda     #$FF
        sta     Hold
Main:
        jsr     Wait_Recal
        jsr     Read_Btns
        lda     Vec_Buttons             ; a button just pressed: that speed held
        bita    #1
        beq     B1
        tst     Hold
        bne     Still
        ldb     Rate                    ; still already: the next refresh rate (50 45 40 35 30 25 a second)
        incb
        cmpb    #6
        blo     R1
        clrb
R1      stb     Rate
        aslb
        ldx     #Rates
        ldx     b,x
        stx     Vec_Rfrsh
        bra     B1
Still   clr     Hold
B1      bita    #2
        beq     B2
        ldb     #1
        stb     Hold
B2      bita    #4
        beq     B3
        ldb     #2
        stb     Hold
B3      bita    #8
        beq     B4
        ldb     #3
        stb     Hold
B4      inc     Frame                   ; every 256 frames the next speed
        bne     NoPh
        inc     Phase
NoPh    lda     Hold
        bpl     Held
        lda     Phase
        anda    #3
Held    ldx     #Speeds
        lda     a,x
        tst     Dir
        bpl     Fwd
        nega
Fwd     adda    Pos
        sta     Pos
        cmpa    #LIM
        blt     C1
        ldb     #$FF
        stb     Dir
C1      cmpa    #-LIM
        bgt     C2
        clr     Dir
C2      lda     #$7F
        jsr     Intensity_a
        lda     #$7F                    ; the vertical line at x = Pos
        sta     <$04
        lda     #-100
        ldb     Pos
        jsr     Moveto_d
        lda     #$FF
        sta     <$04
        lda     #100
        clrb
        jsr     Draw_Line_d
        jsr     Reset0Ref
        lda     #$7F                    ; the horizontal line at y = Pos
        sta     <$04
        lda     Pos
        ldb     #-100
        jsr     Moveto_d
        lda     #$FF
        sta     <$04
        clra
        ldb     #100
        jsr     Draw_Line_d
        jsr     Reset0Ref
        lbra    Main

Speeds  fcb     0,1,3,8
Rates   fdb     $3075,$3582,$7C92,$69A7,$50C3,$60EA     ; timer 2, low byte first: 30000 33333 37500 42857 50000 60000 cycles
