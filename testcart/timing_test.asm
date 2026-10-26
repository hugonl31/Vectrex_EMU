; timing_test.asm - Vectrex test cartridge for the emulator's analog timing (line ends, corners).
; Static pages to photograph on a real Vectrex and compare with Vectrex_EMU. Button 4: next page.
;   Page 1  SQUARES : 16 squares drawn with the BIOS line routine (Draw_VLp, as Mine Storm), at
;                     scales (timer 1 lengths) 8..23, each labelled with its scale (hex).
;   Page 2  LINES   : 16 horizontal lines at scales 8..23; after each, a dot is placed separately
;                     (a beam move of the same vector at the same scale) where the line should end:
;                     the gap between the lit end and the dot is the line's dark end.
;   Page 3  BOWTIES : a bow-tie outline at scales 8..23 (one closed path).
;   Page 4  SHIPS   : Mine Storm's ship (its vector list, as Mine Storm draws it) at scales $0A-$0D,
;                     top row at Mine Storm's intensity $5F, bottom row at $7F.
;   Page 5  BIG SHIPS: the same, every vector doubled (twice the speed: the same timing, gaps twice as
;                     wide - easier to photograph).
; Build: lwasm --format=raw -o timing_test.bin timing_test.asm  (then pad to 32 KB for the cart)

Wait_Recal      equ $F192
Read_Btns       equ $F1BA
Intensity_a     equ $F2AB
Dot_here        equ $F2C5
Moveto_d        equ $F312
Reset0Ref       equ $F354
Print_Str_d     equ $F37A
Draw_VLp        equ $F410
Vec_Buttons     equ $C811
Vec_Text_HW     equ $C82A
VIA_t1_cnt_lo   equ $D004

Page            equ $C880           ; 0..2
Idx             equ $C881           ; shape number 0..15
PosY            equ $C882
PosX            equ $C883

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D               ; (a BIOS tune)
        fcb     $F8,$50,$20,-$40
        fcc     "TIMING TEST"
        fcb     $80
        fcb     0

Start:
        clr     Page
Main:
        jsr     Wait_Recal
        jsr     Read_Btns
        lda     Vec_Buttons
        bita    #$08
        beq     @np
        lda     Page
        inca
        cmpa    #5
        blo     @sp
        clra
@sp     sta     Page
@np     ldd     #$F848              ; small text
        std     Vec_Text_HW
        lda     #$7F
        jsr     Intensity_a
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        ldu     #TitleTab           ; the page's name, top left
        lda     Page
        asla
        ldu     a,u
        ldd     #$7C90
        jsr     Print_Str_d
        clr     Idx
@lp     jsr     DrawOne
        inc     Idx
        lda     Idx
        cmpa    #16
        blo     @lp
        bra     Main

; DrawOne - shape Idx (scale Idx+8) on the current page, in a 4 x 4 grid (lines page: one column)
DrawOne:
        lda     Page
        cmpa    #1
        beq     DrawLine
        cmpa    #3
        lbhs    DrawShip
        lda     Idx                 ; grid cell: row = Idx / 4, column = Idx % 4
        lsra
        lsra
        ldb     #-52
        mul                         ; (B = -52 * row)
        addb    #68
        stb     PosY
        lda     Idx
        anda    #3
        ldb     #58
        mul
        subb    #100
        stb     PosX
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     PosY
        ldb     PosX
        jsr     Moveto_d
        lda     Idx
        adda    #8
        sta     VIA_t1_cnt_lo       ; the shape's scale
        ldx     #Square
        lda     Page
        beq     @d
        ldx     #Bowtie
@d      jsr     Draw_VLp
        lbra    Label

; DrawLine - line Idx: a horizontal line, then the dot where it should end
DrawLine:
        lda     Idx
        ldb     #-13
        mul
        addb    #95
        stb     PosY
        lda     #-60
        sta     PosX
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     PosY
        ldb     PosX
        jsr     Moveto_d
        lda     Idx
        adda    #8
        sta     VIA_t1_cnt_lo
        ldx     #HLine
        jsr     Draw_VLp
        jsr     Reset0Ref           ; the dot: the same start, a move of the same vector, then rest
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     PosY
        ldb     PosX
        jsr     Moveto_d
        lda     Idx
        adda    #8
        sta     VIA_t1_cnt_lo
        ldd     #$007F
        jsr     Moveto_d
        lda     #$7F
        jsr     Intensity_a
        jsr     Dot_here
        lda     PosX                ; the label left of the line
        suba    #38
        sta     PosX
        lda     PosY
        adda    #3
        sta     PosY
        lbra    Label

; DrawShip - ship Idx (0-7): scale $0A + Idx % 4, intensity $5F (first row) or $7F (second row)
DrawShip:
        lda     Idx
        cmpa    #8
        bhs     @x
        anda    #3
        ldb     #58
        mul
        subb    #95
        stb     PosX
        lda     #50
        ldb     Idx
        cmpb    #4
        blo     @r
        lda     #-45
@r      sta     PosY
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     PosY
        ldb     PosX
        jsr     Moveto_d
        lda     #$5F
        ldb     Idx
        cmpb    #4
        blo     @i
        lda     #$7F
@i      jsr     Intensity_a
        lda     Idx
        anda    #3
        adda    #$0A
        sta     VIA_t1_cnt_lo
        ldx     #Ship
        lda     Page
        cmpa    #4
        bne     @s
        ldx     #Ship2
@s      jsr     Draw_VLp
        lda     #$7F
        jsr     Intensity_a
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     Idx
        anda    #3
        adda    #2
        asla
        asla
        ldu     #Hex
        leau    a,u
        lda     PosY
        suba    #14
        ldb     PosX
        subb    #4
        jmp     Print_Str_d
@x      rts

; Label - the scale in hex next to the shape
Label:
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     Idx
        asla
        asla
        ldu     #Hex
        leau    a,u
        lda     Page
        cmpa    #1
        beq     @l
        lda     Page
        bne     @b
        lda     PosY                ; squares: the label above (they hang down from PosY)
        adda    #14
        ldb     PosX
        jmp     Print_Str_d
@b      lda     PosY                ; bowties: below (they reach up and down from PosY)
        suba    #18
        ldb     PosX
        subb    #12
        jmp     Print_Str_d
@l      lda     PosY
        ldb     PosX
        jmp     Print_Str_d

; vector lists for Draw_VLp: pattern ($FF draw,$00 move), y, x; $01 ends
Square  fcb     $FF,$00,$60,$FF,-$60,$00,$FF,$00,-$60,$FF,$60,$00,$01
HLine   fcb     $FF,$00,$7F,$01
; the bow-tie: (-w,4.5) (-w,-4.5) (w,4.5) (w,-4.5) (-w,4.5), w = 6 (x8 art units / 2)
Bowtie  fcb     $00,$30,-$40,$FF,-$60,$00,$FF,$60,$7F,$FF,-$60,$00,$FF,$60,-$7F,$01

; Mine Storm's ship (pointing up) as Mine Storm builds it in RAM ($CB89)
Ship    fcb     $00,$3F,$00,$FF,$C4,$08,$FF,$D8,$D8,$FF,$20,$00,$00,$00,$40,$FF,$E0,$00,$FF,$28,$D8,$FF,$3C,$08,$01
Ship2   fcb     $00,$7E,$00,$FF,$88,$10,$FF,$B0,$B0,$FF,$40,$00,$00,$00,$7F,$FF,$C0,$00,$FF,$50,$B0,$FF,$78,$10,$01
TitleTab fdb    TxSq,TxLn,TxBt,TxSh,TxSh2
TxSq    fcc     "SQUARES 08-17"
        fcb     $80
TxLn    fcc     "LINES + END DOTS 08-17"
        fcb     $80
TxBt    fcc     "BOWTIES 08-17"
        fcb     $80
TxSh    fcc     "SHIPS 0A-0D  5F / 7F"
        fcb     $80
TxSh2   fcc     "BIG SHIPS 0A-0D  5F / 7F"
        fcb     $80
Hex     fcc     "08"
        fcb     $80,0
        fcc     "09"
        fcb     $80,0
        fcc     "0A"
        fcb     $80,0
        fcc     "0B"
        fcb     $80,0
        fcc     "0C"
        fcb     $80,0
        fcc     "0D"
        fcb     $80,0
        fcc     "0E"
        fcb     $80,0
        fcc     "0F"
        fcb     $80,0
        fcc     "10"
        fcb     $80,0
        fcc     "11"
        fcb     $80,0
        fcc     "12"
        fcb     $80,0
        fcc     "13"
        fcb     $80,0
        fcc     "14"
        fcb     $80,0
        fcc     "15"
        fcb     $80,0
        fcc     "16"
        fcb     $80,0
        fcc     "17"
        fcb     $80,0
