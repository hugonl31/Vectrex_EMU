; test_cart.asm - Test Cart: four pages to compare the picture between platforms (PC, iPhone / iPad, Android, RetroArch).
; Buttons 1-4 choose the page; a snapshot at any moment will do (every mover has a steady speed, its trail the same always).
;   1 TRAIL   the afterglow: bars moving right at 1, 2 and 4 units a frame (intensity $7F), one at 2 at $3F, a dot at 2 -
;             slow and in a small area (96 x 110 units in the middle): capture it close up
;   2 LEVELS  seven lines at intensity $10 .. $70 and one at $7F; the same line drawn fast and slow (scale $FF / $20)
;   3 DOTS    rests of 2 8 32 64 128 255 at intensity $7F, the same at $5F
;   4 CORNERS a square, a triangle, a cross and a zigzag in joined lines (corner dots, joins)
; Build: lwasm --format=raw -o test_cart_raw.bin test_cart.asm, then pad to 32768 bytes with $FF (64 KB: twice).

Wait_Recal      equ $F192
Read_Btns       equ $F1BA
Intensity_a     equ $F2AB
Dot_d           equ $F2C3
Moveto_d        equ $F312
Reset0Ref       equ $F354
Print_Str_d     equ $F37A
Draw_Line_d     equ $F3DF
Draw_VLp        equ $F410
Vec_Buttons     equ $C811
Vec_Dot_Dwell   equ $C828
Vec_Text_HW     equ $C82A
Page            equ $C880
P2              equ $C881
P4              equ $C882
P8              equ $C883
Cnt             equ $C884
Yrow            equ $C885

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$48
        fcc     "TEST CART"
        fcb     $80
        fcb     0

Start:
        clr     Page
        clr     P2
        clr     P4
        clr     P8
Main:
        jsr     Wait_Recal
        jsr     Read_Btns
        lda     Vec_Buttons
        bita    #1
        beq     B1
        clr     Page
B1      bita    #2
        beq     B2
        ldb     #1
        stb     Page
B2      bita    #4
        beq     B3
        ldb     #2
        stb     Page
B3      bita    #8
        beq     B4
        ldb     #3
        stb     Page
B4      lda     Page
        lbeq    Trail
        deca
        lbeq    Levels
        deca
        lbeq    Dots
        lbra    Corners

; ---------------------------------------------------------------- 1: the afterglow
; (slow and close together - the owner: a small area to capture, more pixels on it. The bars run 96 units, from x = -48,
;  and start again at the left)
Trail:
        ldx     #P2
        lda     #1
        bsr     Step
        lda     #2
        bsr     Step
        lda     #4
        bsr     Step
        lda     #$7F
        jsr     Intensity_a
        lda     #36                     ; a bar 20 units tall from y = 36, at x = P2 (1 unit a frame)
        ldb     P2
        bsr     Bar
        lda     #10                     ; 2 units a frame
        ldb     P4
        bsr     Bar
        lda     #-16                    ; 4 units a frame
        ldb     P8
        bsr     Bar
        lda     #$3F
        jsr     Intensity_a
        lda     #-42                    ; 2 units a frame, intensity $3F
        ldb     P4
        bsr     Bar
        lda     #$7F
        jsr     Intensity_a
        lda     #16
        sta     Vec_Dot_Dwell
        lda     #$7F
        sta     <$04
        lda     #-52                    ; a dot, 2 units a frame
        ldb     P4
        jsr     Dot_d
        jsr     Reset0Ref
        lbra    Main
Step:                                   ; ,x += a, back to -48 at 48; x on to the next
        adda    ,x
        cmpa    #48
        blt     St1
        suba    #96
St1     sta     ,x+
        rts
Bar:                                    ; a = y of its foot, b = x
        pshs    d
        lda     #$7F
        sta     <$04
        puls    d
        jsr     Moveto_d
        lda     #20
        clrb
        jsr     Draw_Line_d
        jmp     Reset0Ref

; ---------------------------------------------------------------- 2: the levels
Levels:
        lda     #$10
        sta     Cnt
        lda     #100
        sta     Yrow
Lv1     lda     Cnt
        jsr     Intensity_a
        lda     #$7F
        sta     <$04
        lda     Yrow
        ldb     #-90
        jsr     Moveto_d
        clra
        ldb     #100                    ; 100 units at scale $7F
        jsr     Draw_Line_d
        jsr     Reset0Ref
        lda     Yrow
        suba    #20
        sta     Yrow
        lda     Cnt
        adda    #$10
        sta     Cnt
        cmpa    #$80
        blo     Lv1
        lda     #$7F                    ; the eighth line: $7F
        jsr     Intensity_a
        lda     #$7F
        sta     <$04
        lda     #-40
        ldb     #-90
        jsr     Moveto_d
        clra
        ldb     #100
        jsr     Draw_Line_d
        jsr     Reset0Ref
        lda     #$7F                    ; fast: the same length in one stroke at scale $FF (50 units a cycle... the beam twice as fast)
        sta     <$04
        lda     #-70
        ldb     #-90
        jsr     Moveto_d
        lda     #$FF
        sta     <$04
        clra
        ldb     #50
        jsr     Draw_Line_d
        jsr     Reset0Ref
        lda     #$7F                    ; slow: four strokes of 100 at scale $20 - the beam four times slower than the fast one... joined
        sta     <$04
        lda     #-100
        ldb     #-90
        jsr     Moveto_d
        lda     #$20
        sta     <$04
        ldx     #Slow4
        jsr     Draw_VLp
        jsr     Reset0Ref
        lbra    Main
Slow4   fcb     $FF,0,100
        fcb     $FF,0,100
        fcb     $FF,0,100
        fcb     $FF,0,100
        fcb     $01

; ---------------------------------------------------------------- 3: the dots
Dots:
        lda     #$7F
        jsr     Intensity_a
        lda     #40
        sta     Yrow
        bsr     DotRow
        lda     #$5F
        jsr     Intensity_a
        lda     #-40
        sta     Yrow
        bsr     DotRow
        lbra    Main
DotRow:
        ldx     #Dwells
        ldb     #-100
DR1     lda     ,x+
        sta     Vec_Dot_Dwell
        pshs    b,x
        lda     #$7F
        sta     <$04
        lda     Yrow
        jsr     Dot_d
        jsr     Reset0Ref
        puls    b,x
        addb    #40
        cmpx    #DwEnd
        blo     DR1
        rts
Dwells  fcb     2,8,32,64,128,255
DwEnd

; ---------------------------------------------------------------- 4: the corners
Corners:
        lda     #$7F
        jsr     Intensity_a
        ldx     #Square
        lda     #20
        ldb     #-100
        bsr     Shape
        ldx     #Tri
        lda     #20
        ldb     #20
        bsr     Shape
        ldx     #Cross
        lda     #-90
        ldb     #-100
        bsr     Shape
        ldx     #Zig
        lda     #-60
        ldb     #10
        bsr     Shape
        lbra    Main
Shape:                                  ; x = list, a / b = its start
        pshs    x
        pshs    d
        lda     #$7F
        sta     <$04
        puls    d
        jsr     Moveto_d
        lda     #$40
        sta     <$04
        puls    x
        jsr     Draw_VLp
        jmp     Reset0Ref
Square  fcb     $FF,120,0
        fcb     $FF,0,120
        fcb     $FF,-120,0
        fcb     $FF,0,-120
        fcb     $01
Tri     fcb     $FF,120,60
        fcb     $FF,-120,60
        fcb     $FF,0,-120
        fcb     $01
Cross   fcb     $FF,120,120
        fcb     $00,0,-120
        fcb     $FF,-120,120
        fcb     $01
Zig     fcb     $FF,100,30
        fcb     $FF,-100,30
        fcb     $FF,100,30
        fcb     $FF,-100,30
        fcb     $FF,100,30
        fcb     $01
