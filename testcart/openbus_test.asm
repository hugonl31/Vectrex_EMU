; openbus_test.asm - Vectrex test cartridge: what the CPU reads where nothing answers (open bus).
; One static page: each line is a read of an address no chip drives ($8000-$BFFF, $C000-$C7FF), done
; with a different instruction, and the byte read in hex. On a real Vectrex the floating data bus
; decides it (the last byte on the bus? the BIOS $FFFF byte of an idle cycle? all ones?). Mine Storm's
; split-mine bug draws such bytes as a vector list (2026-10-14): the emulator needs the real rule.
; The last line reads $3408 inside this cartridge (the image is padded with $FF): it shows how the
; cartridge maps its image there.
; Build: lwasm --format=raw -o openbus_test_raw.bin openbus_test.asm, then pad to 32768 bytes with $FF.

Wait_Recal      equ $F192
Intensity_a     equ $F2AB
Reset0Ref       equ $F354
Print_Str_d     equ $F37A
Vec_Text_HW     equ $C82A
VIA_t1_cnt_lo   equ $D004

Vals            equ $C880           ; the bytes read (NVAL)
HexBuf          equ $C8A0           ; their hex strings, 3 bytes each (2 digits, $80)
Idx             equ $C8E0
PosY            equ $C8E1
NVAL            equ 11

        org     $0000
        fcc     "g GCE 2026"
        fcb     $80
        fdb     $FD0D               ; (a BIOS tune)
        fcb     $F8,$50,$20,-$40
        fcc     "OPEN BUS"
        fcb     $80
        fcb     0

Start:
Main:
        jsr     Wait_Recal
        jsr     DoReads
        jsr     MakeHex
        ldd     #$F848              ; small text
        std     Vec_Text_HW
        lda     #$7F
        jsr     Intensity_a
        jsr     Reset0Ref
        lda     #$7F
        sta     VIA_t1_cnt_lo
        ldu     #Title
        ldd     #$7090
        jsr     Print_Str_d
        clr     Idx
        lda     #$58
        sta     PosY
@lp     jsr     Reset0Ref           ; the label
        lda     #$7F
        sta     VIA_t1_cnt_lo
        ldx     #LabTab
        ldb     Idx
        aslb
        ldu     b,x
        lda     PosY
        ldb     #-$70
        jsr     Print_Str_d
        jsr     Reset0Ref           ; the byte read, in hex
        lda     #$7F
        sta     VIA_t1_cnt_lo
        lda     Idx
        ldb     #3
        mul
        ldu     #HexBuf
        leau    d,u
        lda     PosY
        ldb     #$38
        jsr     Print_Str_d
        lda     PosY
        suba    #$10
        sta     PosY
        inc     Idx
        lda     Idx
        cmpa    #NVAL
        blo     @lp
        bra     Main

; DoReads - the reads (the instructions as written: their bytes and cycles decide what a floating
; bus holds; Draw_VLp, the BIOS vector list routine, uses LDD 1,X / LDA ,X / LDB ,X+)
DoReads:
        lda     $9000               ; extended: the last byte fetched before the read is $00
        sta     Vals+0
        lda     $9055               ; ... $55
        sta     Vals+1
        lda     $90AA               ; ... $AA
        sta     Vals+2
        lda     $C123               ; the $C000-$C7FF hole
        sta     Vals+3
        ldx     #$9000
        lda     ,x                  ; indexed (an idle cycle at $FFFF before the read)
        sta     Vals+4
        ldd     1,x                 ; Draw_VLp's vector read
        std     Vals+5
        ldb     ,x+                 ; Draw_VLp_scale's scale read
        stb     Vals+7
        ldx     #$C2E8
        lda     ,x                  ; where Mine Storm's stray list walked
        sta     Vals+8
        lda     #$90
        tfr     a,dp
        lda     <$00                ; direct page: $9000
        sta     Vals+9
        lda     #$D0
        tfr     a,dp
        lda     $3408               ; inside this cartridge (padding)
        sta     Vals+10
        rts

; MakeHex - Vals to HexBuf strings
MakeHex:
        ldx     #Vals
        ldu     #HexBuf
        ldb     #NVAL
@m      lda     ,x
        lsra
        lsra
        lsra
        lsra
        bsr     Nib
        sta     ,u+
        lda     ,x+
        anda    #$0F
        bsr     Nib
        sta     ,u+
        lda     #$80
        sta     ,u+
        decb
        bne     @m
        rts

Nib:
        adda    #$30
        cmpa    #$39
        bls     @n
        adda    #7
@n      rts

Title   fcc     "OPEN BUS READS"
        fcb     $80
LabTab  fdb     L0,L1,L2,L3,L4,L5,L6,L7,L8,L9,L10
L0      fcc     "LDA 9000"
        fcb     $80
L1      fcc     "LDA 9055"
        fcb     $80
L2      fcc     "LDA 90AA"
        fcb     $80
L3      fcc     "LDA C123"
        fcb     $80
L4      fcc     "LDA ,X 9000"
        fcb     $80
L5      fcc     "LDD 1,X HI"
        fcb     $80
L6      fcc     "LDD 1,X LO"
        fcb     $80
L7      fcc     "LDB ,X+"
        fcb     $80
L8      fcc     "LDA ,X C2E8"
        fcb     $80
L9      fcc     "LDA <00 DP90"
        fcb     $80
L10     fcc     "LDA 3408 CART"
        fcb     $80
