; fullgrid.asm - a test cartridge: a grid over the whole tube and past its edges (for the case's reflections).
; 25 rows x 19 columns, 10 apart at scale 200, every line from the middle out past the tube's edges. Build: lwasm --6809 --format=raw -o fullgrid.bin fullgrid.asm
Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Draw_Line_d     equ $F3DF
Count           equ $C880

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D                   ; (the BIOS's start-up music)
        fcb     $F8,$50,$20,$C8         ; title: height, width, y, x
        fcc     "FULL GRID TEST"
        fcb     $80
        fcb     0

Start:
Frame   jsr     Wait_Recal              ; (DP = $D0)
        lda     #$7F
        jsr     Intensity_a
        lda     #$C8
        sta     <$04                    ; scale 200 (VIA_t1_cnt_lo): one cell = 10 = 2000 of the emulator's units
; the rows (25, y -120 .. +120): each from the middle out, right then left - the emulator only starts a line
; on the screen, so every line starts there and runs past the edge
        jsr     Reset0Ref
        ldd     #$8800                  ; y -120, x 0
        jsr     Moveto_d
        lda     #25
        sta     Count
Row     ldd     #$007F
        jsr     Draw_Line_d             ; to the right, past the edge
        ldd     #$0081
        jsr     Moveto_d                ; back to the middle
        ldd     #$0081
        jsr     Draw_Line_d             ; to the left
        dec     Count
        beq     Cols
        ldd     #$0A7F                  ; up 10, back to the middle
        jsr     Moveto_d
        bra     Row
; the columns (19, x -90 .. +90): each from the middle out, up then down
Cols    jsr     Reset0Ref
        ldd     #$00A6                  ; y 0, x -90
        jsr     Moveto_d
        lda     #19
        sta     Count
Col     ldd     #$7F00
        jsr     Draw_Line_d             ; up, past the edge
        ldd     #$8100
        jsr     Moveto_d
        ldd     #$8100
        jsr     Draw_Line_d             ; down
        dec     Count
        beq     Frame
        ldd     #$7F0A                  ; back to the middle, right 10
        jsr     Moveto_d
        bra     Col
