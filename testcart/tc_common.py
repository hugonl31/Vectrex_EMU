"""tc_common.py - what the later test carts share: the header, the page switch by buttons 1-4, the build / pack step.
(make_dot_test's helpers: w, A, text, row_start, hand_on, hand_off, set_y, delay, b8.)"""
import os, sys, subprocess
import make_dot_test as D
from make_dot_test import delay, b8, text, row_start, hand_on, hand_off, set_y, w, A

EQU = (('Wait_Recal', 0xF192), ('Read_Btns', 0xF1BA), ('Intensity_a', 0xF2AB), ('Moveto_d', 0xF312), ('Reset0Ref', 0xF354),
       ('Print_Str_d', 0xF37A), ('Draw_Line_d', 0xF3DF), ('Draw_VLc', 0xF3CE), ('Draw_VLp', 0xF410), ('Vec_Btn_State', 0xC80F),
       ('Vec_Text_HW', 0xC82A), ('Vec_Rfrsh', 0xC83D), ('Page', 0xC880))

def begin(title, note, frame_hook=None):
    del A[:]
    w('; %s' % note); w('')
    for n, a in EQU: w('%-15s equ $%04X' % (n, a))
    w('')
    w('        org     $0000'); w('        fcc     "g GCE 2026"'); w('        fcb     $80'); w('        fdb     $FD0D')
    w('        fcb     $F8,$50,$20,-$40'); w('        fcc     "%s"' % title); w('        fcb     $80'); w('        fcb     0')
    w('')
    w('Start:  clr     Page')
    w('Main:')
    if frame_hook: frame_hook()
    w('        jsr     Wait_Recal'); w('        lda     #$D0'); w('        tfr     a,dp'); w('        setdp   $D0')
    w('        jsr     Read_Btns'); w('        lda     Vec_Btn_State')
    for i in range(4):
        w('        bita    #$%02X' % (1 << i)); w('        beq     NoB%d' % i); w('        ldb     #%d' % i); w('        stb     Page'); w('NoB%d:' % i)
    w('        ldb     Page'); w('        lbeq    Page1'); w('        decb'); w('        lbeq    Page2'); w('        decb'); w('        lbeq    Page3'); w('        lbra    Page4')

def page(n): w(''); w('Page%d:' % n)
def page_end(): w('        lbra    Main')
def string(label, t):
    w('%-7s fcc     "%s"' % (label, t)); w('        fcb     $80')

def finish(stem, name):
    """write <stem>.asm; with --pack: <stem>_raw.bin -> <stem>.bin (32 KB) and <stem>_64k.bin"""
    here = os.path.dirname(os.path.abspath(__file__))
    if '--pack' in sys.argv:
        raw = open(os.path.join(here, stem + '_raw.bin'), 'rb').read(); assert len(raw) <= 32768, len(raw)
        img = raw + b'\xFF' * (32768 - len(raw))
        open(os.path.join(here, stem + '.bin'), 'wb').write(img); open(os.path.join(here, stem + '_64k.bin'), 'wb').write(img * 2)
        os.remove(os.path.join(here, stem + '_raw.bin')); print('packed %s.bin (32 KB, %d used), %s_64k.bin' % (stem, len(raw), stem))
    else:
        open(os.path.join(here, stem + '.asm'), 'w', newline='\n').write('\n'.join(A) + '\n'); print('wrote %s.asm' % stem)
