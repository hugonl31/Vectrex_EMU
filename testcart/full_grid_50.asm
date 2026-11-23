; full_grid_50.asm - Full Grid Test at the BIOS's own 50 frames a second: the Full Grid Test's lines, drawn the same way
; (each from the middle outwards, scale $C8), every third line a frame, in turn. The whole grid takes 36 ms of beam time, more than a 20 ms frame; half of
; it (23 lines, 21.6 ms) still does not fit, a third does. Every frame is 30000
; cycles: in step with a 50 Hz mains, the picture stands still. A photo of 1/50 s holds one third (a grid 30 apart; the first
; third has the outer lines), 3 frames (60 ms) or a multiple the whole grid.
; Build: lwasm --format=raw -o full_grid_50_raw.bin full_grid_50.asm, pad to 32768 with $FF (64 KB: twice).

Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Draw_Line_d     equ $F3DF
Count           equ $C880
Phase           equ $C881

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$48
        fcc     "FULL GRID 50"
        fcb     $80
        fcb     0

Start:
        clr     Phase
Main:
        jsr     Wait_Recal
        lda     #$7F
        jsr     Intensity_a
        lda     #$C8
        sta     <$04
        jsr     Reset0Ref
        lda     Phase                   ; the next third: 0, 1, 2
        inca
        cmpa    #3
        blo     P1
        clra
P1      sta     Phase
        ldx     #Tab
        asla
        asla
        leax    a,x                     ; x: this third's row start, row count, column start, column count
        lda     ,x                      ; to the first row (y), the middle
        clrb
        pshs    x
        jsr     Moveto_d
        puls    x
        lda     1,x
        sta     Count
        pshs    x
Row     ldd     #$007F                  ; a row: from the middle to the right, back, to the left
        jsr     Draw_Line_d
        ldd     #$0081
        jsr     Moveto_d
        ldd     #$0081
        jsr     Draw_Line_d
        dec     Count
        beq     Cols
        ldd     #$1E7F                  ; 30 up, back to the middle
        jsr     Moveto_d
        bra     Row
Cols    jsr     Reset0Ref
        puls    x
        ldb     2,x                     ; to the first column (x), the middle
        clra
        pshs    x
        jsr     Moveto_d
        puls    x
        lda     3,x
        sta     Count
Col     ldd     #$7F00                  ; a column: from the middle up, back, down
        jsr     Draw_Line_d
        ldd     #$8100
        jsr     Moveto_d
        ldd     #$8100
        jsr     Draw_Line_d
        dec     Count
        lbeq    Main
        ldd     #$7F1E                  ; back to the middle, 30 to the right
        jsr     Moveto_d
        bra     Col

Tab     fcb     -120,9,-90,7            ; the first third: rows -120 .. 120 (9), columns -90 .. 90 (7) - the outer lines
        fcb     -110,8,-80,6
        fcb     -100,8,-70,6
