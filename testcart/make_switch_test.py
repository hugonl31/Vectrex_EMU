"""make_switch_test.py - writes switch_test.asm: three pages that separate the paths the Dot Test mixes (it changes RAMP
and the X speed in the same instant): how RAMP really starts and stops the integrators, how the spot follows a change
of speed with RAMP held on, and which of the two arrives first. Buttons 1-4 pick the page. Every write is scheduled to
the cycle (the times below are the cycles of the VIA writes themselves); README.md has the table.

Page 1  RAMP    a straight line in 15 pieces, RAMP off for R cycles between them (R = 5 ... 48): rows 1-2 at speed $40
                (24 cycles a piece), rows 3-4 at $7F (12 cycles a piece); rows 1 and 3 the beam on throughout (the joints'
                dots: where the beam dwells), rows 2 and 4 the beam off during the pauses of 20 cycles and more (the lit pieces' ends:
                where the beam was 5 cycles after RAMP off and 6 before RAMP on).
Page 2  X STEP  RAMP on throughout, the Y speed constant: the X speed steps 0 -> +V (top row) and -V -> +V (bottom row)
                for V = $20, $40, $7F - the spot's answer to a change of speed alone, no switch.
Page 3  X / RAMP a corner with a long rest (RAMP off 32 cycles) and the X flip written K cycles after RAMP off (top row:
                K = -12 ... +16) or K cycles after RAMP on again (bottom row: K = -12 ... +12): if the turn starts before
                the stop the corner is round and the dot sits down the second leg; if after, sharp with the dot on the tip.
Page 4  DECAY   dots travelling at known speeds: every frame one step to the right (then again from the left); the places a
                dot left glow on. Rows 1-3: a dot resting 64 cycles at intensity $7F, 8 / 16 / 24 units a frame (400 / 800 /
                1200 units a second); rows 4-6 at 16 units a frame: resting 16 cycles at $7F, 64 cycles at $5F and at $40;
                rows 7-8: a short line at $7F and $5F, 16 units a frame. Photograph at 1/25 s (two frames).
"""
import os
import make_dot_test as D
from make_dot_test import delay, b8, text, row_start, hand_on, hand_off, rel_move, set_y, w, A

class Sched:
    """instructions with their cycles; .t = the cycle the next instruction starts at"""
    def __init__(self): self.t = 0
    def op(self, s, cyc):
        w('        ' + s); self.t += cyc
    def until(self, t):
        n = t - self.t
        assert n == 0 or n >= 2, ('no room', self.t, t)
        for s in delay(n): w(s)
        self.t = t
    def write(self, s, cyc, off, at):
        """an instruction whose VIA write lands in its cycle `off`: placed so that this is cycle `at`"""
        self.until(at - off); self.op(s, cyc)

# (mnemonic, cycles, the cycle of the ORB write; a 16-bit store writes ORA one cycle later)
STX, STY, STD, STU = ('stx     <$00', 5, 4), ('sty     <$00', 6, 5), ('std     <$00', 5, 4), ('stu     <$00', 5, 4)
STA0, STA1, STB1, STAC, STBC = ('sta     <$00', 4, 4), ('sta     <$01', 4, 4), ('stb     <$01', 4, 4), ('sta     <$0C', 4, 4), ('stb     <$0C', 4, 4)

P1_R = [5, 7, 8, 9, 10, 11, 12, 13, 14, 16, 20, 24, 32, 48]      # (5 the shortest two writes allow; 6 cannot be)
P1_ROWS = [(0x40, 24, False, 0x50), (0x40, 24, True, 0x38), (0x7F, 12, False, 0x10), (0x7F, 12, True, -0x08)]   # speed, cycles a piece, blanked, y
P1_X0 = -0x70
P2_V = [0x20, 0x40, 0x7F]
P2_LEG = 32
P3_K1 = [-12, -8, -4, 0, 4, 8, 12, 16]
P3_K2 = [-12, -8, -5, 0, 5, 8, 12]                          # (5: the nearest the writes allow to RAMP on)
P3_R, P3_LEG, P3_V = 32, 24, 0x50

def line_row(v, L, y):
    """page 1, the beam on throughout: 15 pieces; the times are the ORB writes - on at 10, then off / on alternately"""
    row_start(0x7F, y, P1_X0); hand_on(); set_y(0)
    w('        ldx     #$01%02X' % v); w('        ldy     #$81%02X' % v); w('        lda     #$EE'); w('        ldb     #$CE')
    s = Sched(); s.write(*STX, 10)                         # RAMP on (the X speed with it)
    s.write(*STAC, 15)                                     # the beam on, 6 cycles later (the same in every row)
    t = 10
    for R in P1_R:
        t += L; s.write(*STY, t)                           # RAMP off
        t += R; s.write(*STX, t)                           # RAMP on again
    t += L; s.write(*STY, t)
    s.op('stb     <$0C', 4)
    hand_off()

def emit_page1():
    for v, L, blank, y in P1_ROWS:
        w('; a line in 15 pieces: speed $%02X, %d cycles a piece, %s' % (v, L, 'the beam off during the pauses of 20 cycles and more (from 5 after RAMP off to 6 before RAMP on)' if blank else 'the beam on throughout'))
        if not blank:
            line_row(v, L, y)
        else:
            row_start(0x7F, y, P1_X0); hand_on(); set_y(0)
            w('        ldx     #$01%02X' % v); w('        ldy     #$81%02X' % v); w('        lda     #$EE'); w('        ldb     #$CE')
            s = Sched(); s.write(*STX, 10); s.write(*STAC, 15)
            t = 10
            for R in P1_R:
                t += L
                if R >= 20:                                # (room for the two beam writes: off 5 cycles after RAMP off, on 6 before RAMP on)
                    s.write(*STY, t); s.write(*STBC, t + 5); s.write(*STAC, t + R - 6); s.write(*STX, t + R)
                else:
                    s.write(*STY, t); s.write(*STX, t + R)
                t += R
            t += L; s.write(*STY, t); s.op('stb     <$0C', 4)
            hand_off()

def emit_page2():
    for row, flip in ((0, False), (1, True)):
        for i, v in enumerate(P2_V):
            w('; X step: %s $%02X, the Y speed -$40, RAMP on throughout' % ('-V -> +V,' if flip else '0 -> +V,', v))
            row_start(0x7F, 0x58 - row * 0x50, -0x58 + i * 0x48 + (0x18 if flip else 0)); hand_on(); set_y(-0x40)
            w('        ldx     #$01%02X' % (b8(-v) if flip else 0)); w('        ldy     #$81%02X' % v); w('        lda     #$EE'); w('        ldb     #$%02X' % v)
            s = Sched(); s.write(*STX, 10); s.write(*STAC, 15)
            s.write(*STB1, 10 + P2_LEG)                    # the X speed steps (ORA alone)
            s.write(*STY, 10 + 2 * P2_LEG)                 # RAMP off
            w('        lda     #$CE'); s.op('sta     <$0C', 4)
            hand_off()

def corner(k, rel_on, y, x):
    """page 3: leg 1 (-V), RAMP off for P3_R, leg 2 (+V); the X flip k cycles after RAMP off (rel_on: after RAMP on again)"""
    v = P3_V
    row_start(0x7F, y, x); hand_on(); set_y(-v)
    t_off = 11 + P3_LEG; t_on = t_off + P3_R; t_flip = (t_on if rel_on else t_off) + k   # (leg 1: 25 cycles - room for the registers)
    # the registers: X = RAMP on + leg 1's speed; A = RAMP off (ORB alone); B = the new X speed (ORA alone); Y = RAMP on
    # again (+ the X speed of that moment); U = RAMP off + the new X speed (the flip with RAMP off: one store)
    cur = v if t_flip <= t_on else -v                      # (the X speed when RAMP comes on again)
    w('        ldx     #$01%02X' % b8(-v)); w('        ldy     #$01%02X' % b8(cur)); w('        ldu     #$81%02X' % v)
    w('        ldb     #$%02X' % v); w('        lda     #$EE')
    s = Sched(); s.write(*STX, 10); s.write(*STAC, 15)
    w('        lda     #$81'); s.t += 2
    done_flip = False
    for t, what in sorted([(t_off, 'off'), (t_on, 'on'), (t_flip, 'flip')], key=lambda e: (e[0], e[1] != 'flip')):
        if what == 'flip':
            if t == t_off: s.write(*STU, t); done_flip = 'off'    # (RAMP off and the flip: one store)
            elif t == t_on: done_flip = 'on'                        # (Y carries it)
            else: s.write(*STB1, t)
        elif what == 'off':
            if done_flip != 'off': s.write(*STA0, t)
        else:
            s.write(*STY, t)
    s.write(*STA0, t_on + P3_LEG)                           # RAMP off: the end of leg 2
    w('        lda     #$CE'); s.op('sta     <$0C', 4)
    hand_off()

def emit_page3():
    for i, k in enumerate(P3_K1):
        w('; a corner, RAMP off %d cycles: the X flip %+d cycles after RAMP off' % (P3_R, k))
        corner(k, False, 0x58, -0x60 + i * 0x1B)
    for i, k in enumerate(P3_K2):
        w('; a corner, RAMP off %d cycles: the X flip %+d cycles after RAMP on again' % (P3_R, k))
        corner(k, True, 0x00, -0x60 + i * 0x1F)

# page 4: Frame = 0 .. 29; a row's dot is at (Frame mod P) x S units from the left - S units a frame (50 frames a second)
P4_ROWS = [(0x7F, 64, 8, 30, 0x58), (0x7F, 64, 16, 15, 0x44), (0x7F, 64, 24, 10, 0x30),      # intensity, rest (cycles), S (units a frame), P (positions), y
           (0x7F, 16, 16, 15, 0x1C), (0x5F, 64, 16, 15, 0x08), (0x40, 64, 16, 15, -0x0C)]
P4_LINES = [(0x7F, 16, 15, -0x30), (0x5F, 16, 15, -0x4C)]   # a short vertical line stepping likewise: intensity, S, P, y
P4_X0 = -0x78
def p4_pos(S, P):
    """A = 0, B = (Frame mod P) x S / 2: the move at scale $FF (units x 2)"""
    w('        lda     Frame')
    for k in range(30 // P - 1):
        w('        cmpa    #%d' % P); w('        blo     *+4'); w('        suba    #%d' % P)
    w('        ldb     #%d' % (S // 2)); w('        mul'); w('        clra')
def emit_page4():
    """dots travelling at known speeds: every frame one step to the right (and again from the left): the places a dot left
    glow on - the trail's length = the speed x the time the phosphor stays visible; its light by place = the decay at
    20 ms steps. Frame = $C881 (0 .. 29)"""
    w('        lda     Frame'); w('        inca'); w('        cmpa    #30'); w('        blo     P4f'); w('        clra')
    w('P4f:    sta     Frame')
    for z, n, S, P, y in P4_ROWS:
        w('; a dot resting %d cycles at intensity $%02X, %d units a frame (%d positions)' % (n, z, S, P))
        row_start(z, y, P4_X0)
        w('        lda     #$FF'); w('        sta     <$04                ; scale $FF: units x 2')
        p4_pos(S, P); w('        jsr     Moveto_d')
        hand_on(); D.dot(n); hand_off()
    for z, S, P, y in P4_LINES:
        w('; a short line (Draw_Line_d, 8 units up at scale $7F), intensity $%02X, %d units a frame' % (z, S))
        row_start(z, y, P4_X0)
        w('        lda     #$FF'); w('        sta     <$04')
        p4_pos(S, P); w('        jsr     Moveto_d')
        w('        lda     #$7F'); w('        sta     <$04'); w('        ldd     #$0800'); w('        jsr     Draw_Line_d')

if __name__ == '__main__':
    del A[:]
    w('; switch_test.asm - generated by make_switch_test.py: RAMP pulses, X steps, the X flip against the RAMP edges; buttons 1-3.')
    w('')
    for n, a in (('Wait_Recal', 0xF192), ('Read_Btns', 0xF1BA), ('Intensity_a', 0xF2AB), ('Moveto_d', 0xF312), ('Reset0Ref', 0xF354),
                 ('Print_Str_d', 0xF37A), ('Draw_Line_d', 0xF3DF), ('Vec_Btn_State', 0xC80F), ('Vec_Text_HW', 0xC82A), ('Page', 0xC880), ('Frame', 0xC881)):
        w('%-15s equ $%04X' % (n, a))
    w('')
    w('        org     $0000'); w('        fcc     "g GCE 2026"'); w('        fcb     $80'); w('        fdb     $FD0D')
    w('        fcb     $F8,$50,$20,-$40'); w('        fcc     "SWITCH TEST"'); w('        fcb     $80'); w('        fcb     0')
    w('')
    w('Start:  clr     Page')
    w('Main:   jsr     Wait_Recal'); w('        lda     #$D0'); w('        tfr     a,dp'); w('        setdp   $D0')
    w('        jsr     Read_Btns'); w('        lda     Vec_Btn_State')
    for i in range(4):
        w('        bita    #$%02X' % (1 << i)); w('        beq     NoB%d' % i); w('        ldb     #%d' % i); w('        stb     Page'); w('NoB%d:' % i)
    w('        ldb     Page'); w('        lbeq    Page1'); w('        decb'); w('        lbeq    Page2'); w('        decb'); w('        lbeq    Page3'); w('        lbra    Page4')
    w(''); w('Page1:'); emit_page1()
    text('L1A', 0x68, P1_X0); text('L1B', -0x20, P1_X0); text('T1', -0x68, -0x50); w('        lbra    Main')
    w(''); w('Page2:'); emit_page2()
    text('L2', 0x70, -0x60); text('T2', -0x68, -0x50); w('        lbra    Main')
    w(''); w('Page3:'); emit_page3()
    text('L3A', 0x6C, -0x68); text('L3B', 0x14, -0x68); text('T3', -0x68, -0x50); w('        lbra    Main')
    w(''); w('Page4:'); emit_page4()
    text('L4', 0x6C, -0x78); text('T4', -0x70, -0x50); w('        lbra    Main')
    w('')
    def s(label, t):
        w('%-7s fcc     "%s"' % (label, t)); w('        fcb     $80')
    s('L1A', 'RAMP OFF %d TO %d' % (P1_R[0], P1_R[-1]))
    s('L1B', '40 ON  40 BLANK  7F ON  7F BLANK')
    s('T1', '1 RAMP')
    s('L2', 'X STEP 20 40 7F  0-V / -V-V')
    s('T2', '2 X STEP')
    s('L3A', 'FLIP AT OFF %d TO %d' % (P3_K1[0], P3_K1[-1]))
    s('L3B', 'FLIP AT ON %d TO %d' % (P3_K2[0], P3_K2[-1]))
    s('T3', '3 X VS RAMP')
    s('L4', 'SPEED 8 16 24 A FRAME')
    s('T4', '4 DECAY')
    here = os.path.dirname(os.path.abspath(__file__))
    if '--pack' in os.sys.argv:
        raw = open(os.path.join(here, 'switch_test_raw.bin'), 'rb').read(); assert len(raw) <= 32768, len(raw)
        img = raw + b'\xFF' * (32768 - len(raw))
        open(os.path.join(here, 'switch_test.bin'), 'wb').write(img); open(os.path.join(here, 'switch_test_64k.bin'), 'wb').write(img * 2)
        os.remove(os.path.join(here, 'switch_test_raw.bin')); print('packed switch_test.bin (32 KB), switch_test_64k.bin')
    else:
        open(os.path.join(here, 'switch_test.asm'), 'w', newline='\n').write('\n'.join(A) + '\n'); print('wrote switch_test.asm')
