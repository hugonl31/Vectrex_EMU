; input_delay_test.asm - Input Delay Test: the screen dark, a bright square (six lines) while any of player 1's
; buttons is held. The phone apps time a press from the button's own event to the first picture with the square
; and to the glass (Android: the app's log, "input delay"). The owner, 2026-11-25.
; Build: lwasm --format=raw -o input_delay_test_raw.bin input_delay_test.asm, then padded (make_input_delay_test.py).

Wait_Recal      equ $F192
Read_Btns       equ $F1BA
Intensity_a     equ $F2AB
Moveto_d        equ $F312
Reset0Ref       equ $F354
Draw_Line_d     equ $F3DF
Vec_Btn_State   equ $C80F

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D
        fcb     $F8,$50,$20,-$56
        fcc     "INPUT DELAY"
        fcb     $80
        fcb     0

Main:
        jsr     Wait_Recal          ; (the frame: the BIOS waits for timer 2, DP = $D0)
        jsr     Read_Btns           ; (the buttons now: Vec_Btn_State, held ones set)
        lda     Vec_Btn_State
        anda    #$0F                ; (player 1's four)
        beq     Main                ; (none held: nothing drawn - the screen dark)
        jsr     Reset0Ref
        lda     #$7F
        jsr     Intensity_a
        lda     #$7F
        sta     <$04                ; (scale $7F)
        ldd     #$C0C0              ; (to the square's lower left: y -64, x -64)
        jsr     Moveto_d
        ldd     #$7F00              ; (up)
        jsr     Draw_Line_d
        ldd     #$007F              ; (right)
        jsr     Draw_Line_d
        ldd     #$8100              ; (down)
        jsr     Draw_Line_d
        ldd     #$0081              ; (left)
        jsr     Draw_Line_d
        ldd     #$7F7F              ; (a diagonal across)
        jsr     Draw_Line_d
        ldd     #$8100              ; (down the right side again)
        jsr     Draw_Line_d
        bra     Main
