; full_grid_25.asm - Full Grid Test at exactly 25 frames a second: the Full Grid Test's picture (25 rows, 19 columns over the
; whole screen, scale $C8), with the refresh timer set to 60000 cycles. The original draws as fast as it can - a frame takes
; longer than the BIOS's 30000 cycles, so it runs at forty-something frames a second, out of step with the mains: on a real
; Vectrex the hum's wobble then crawls through the picture and no photo of it is sharp. At 25 a second (half of 50 Hz) every
; frame meets the same moment of the hum: the picture stands still.
; Build: lwasm --format=raw -o full_grid_25_raw.bin full_grid_25.asm, pad to 32768 with $FF (64 KB: twice).

Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Draw_Line_d     equ $F3DF
Vec_Rfrsh       equ $C83D
Count           equ $C880

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$48
        fcc     "FULL GRID 25"
        fcb     $80
        fcb     0

Start:
        ldx     #$60EA                  ; timer 2, low byte first: 60000 cycles = 25 frames a second
        stx     Vec_Rfrsh
Main:
        jsr     Wait_Recal
        lda     #$7F
        jsr     Intensity_a
        lda     #$C8
        sta     <$04
        jsr     Reset0Ref
        ldd     #$8800                  ; to the bottom (y -120), the middle
        jsr     Moveto_d
        lda     #25                     ; 25 rows, 10 apart: each from the middle to the right, back, to the left
        sta     Count
Row     ldd     #$007F
        jsr     Draw_Line_d
        ldd     #$0081
        jsr     Moveto_d
        ldd     #$0081
        jsr     Draw_Line_d
        dec     Count
        beq     Cols
        ldd     #$0A7F
        jsr     Moveto_d
        bra     Row
Cols    jsr     Reset0Ref
        ldd     #$00A6                  ; to the left (x -90), the middle
        jsr     Moveto_d
        lda     #19                     ; 19 columns, 10 apart: each from the middle up, back, down
        sta     Count
Col     ldd     #$7F00
        jsr     Draw_Line_d
        ldd     #$8100
        jsr     Moveto_d
        ldd     #$8100
        jsr     Draw_Line_d
        dec     Count
        beq     Main
        ldd     #$7F0A
        jsr     Moveto_d
        bra     Col
