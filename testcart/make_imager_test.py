"""make_imager_test.py - the 3D Imager Test cartridge: objects at known depths through the 3D Imager's goggles, for the
emulator's Centre eye view (vtest.c imager_centre - Tony, 2026-11-26: aiming in 3D Narrow Escape). Every object is drawn
for each eye moved sideways by its own depth - the right eye's picture right of its middle by half its offset, the left
eye's left of it - so the Centre eye view has to put each one back on its middle. The flat build (--flat) draws every
offset 0: its left eye's picture is what the Centre eye view of the real build should give, line for line.

The goggles as 3D Narrow Escape drives them (its code read 2026-11-27): the disc's motor on the AY's port A bit 6 (the port
set to output with register 7 bit 6; $80 on, $FF off), its index hole on CA1 - an interrupt each turn that restarts the
frame; the motor runs for a turn when the last turn took longer than the frame (VIA timer 2 expired), else it coasts - one
turn a frame. Then each half turn shows one eye (the first the right eye's, the second the left eye's), each half blue,
green, red (the disc's thirds, imager.c: 0.1631 / 0.3305 of a turn): the picture waits on timer 2 for each colour's part
and draws that eye's lines of that colour there.

Pages (button 4 / 1: the next / the one before):
  1 DEPTHS    six boxes at six depths (two in each colour), a tick at depth 0 under each
  2 REVERSED  the same, the left eye drawing its boxes in reverse order and each line the other way (3D Crazy Coaster
              draws a pass the other way round)
  3 RAILS     two rails into the distance (the near end moved more than the far one) and crossbars at four depths
  4 MOVING    a box moving on between the two half turns too (a picture drawn later is a little further on)
  5 TEXT      text at three depths (the BIOS's text: many short pieces)

    python make_imager_test.py [--flat]          -> imager_test.asm (imager_flat.asm)
    sh build_cart.sh imager_test; python make_imager_test.py --pack   -> imager_test.bin, imager_test_64k.bin"""
import os, sys

FLAT = '--flat' in sys.argv
STEM = 'imager_flat' if FLAT else 'imager_test'
TURN = 60000                       # (one turn of the disc, cycles: 25 turns a second - the motor's top is 28.6)
A1, A2 = 0.1631, 0.3305            # (the colour sectors' starts in each half turn, imager.c - Narrow Escape's disc)
MARGIN = 0.03                      # (each colour's lines start this far into its sector: the speed is never exact)
SCALE = 0x7F

L = []
def w(s=''): L.append(s)

def off(o): return 0 if FLAT else o

# ---------------------------------------------------------------- the pages' objects
# a box: (colour 0 blue / 1 green / 2 red, x, y, offset): the right eye draws it at x + offset/2, the left at x - offset/2
BOXES = [(0, -80, 40, 0), (0, 60, 40, 24), (1, -40, -10, 8), (1, 80, -10, 32), (2, 0, 70, 16), (2, -90, -60, 40)]
BOX = [(0, 16), (16, 0), (0, -16), (-16, 0)]                    # (dy, dx), drawn from its lower left corner
BOX_REV = [(16, 0), (0, 16), (-16, 0), (0, -16)]                # (the same box the other way round)
# rails: (colour, x near, y near, x far, y far, offset near, offset far)
RAILS = [(2, -70, -100, -12, 90, 48, 4), (2, 70, -100, 12, 90, 48, 4)]
BARS = [(1, -60, -70, 60, 40), (1, -46, -20, 46, 28), (1, -32, 30, 32, 16), (1, -20, 70, 20, 8)]   # (colour, x0, y, x1, offset)
TEXTS = [(0, -100, 80, 'NEAR', 36), (1, -40, 0, 'MIDDLE', 16), (2, -10, -80, 'FAR', 4)]

def eye_x(x, o, eye):              # eye 0 the right (the first half turn), 1 the left
    return x + (off(o) // 2 if eye == 0 else -(off(o) // 2))

def b8(v): return v & 0xFF

def draw_box(c, x, y, o, eye, rev):
    xx = eye_x(x, o, eye)
    w('        jsr     Reset0Ref')
    w('        lda     #$%02X' % SCALE); w('        sta     <$04')
    w('        lda     #$7F'); w('        jsr     Intensity_a')
    if rev:                        # (the same box the other way round: up, right, down, left - each side drawn backwards)
        w('        ldd     #$%02X%02X' % (b8(y), b8(xx)))
        w('        jsr     Moveto_d'); w('        ldx     #BoxRev')
    else:
        w('        ldd     #$%02X%02X' % (b8(y), b8(xx)))
        w('        jsr     Moveto_d'); w('        ldx     #Box')
    w('        jsr     Draw_VLc')
    w('        jsr     Reset0Ref')                                # (a tick at depth 0 under it: no offset)
    w('        ldd     #$%02X%02X' % (b8(y - 14), b8(x + 8)))
    w('        jsr     Moveto_d'); w('        ldx     #Tick'); w('        jsr     Draw_VLc')

def draw_line(x0, y0, x1, y1):     # (a line from (x0, y0) to (x1, y1): split where it is longer than a byte's move)
    w('        jsr     Reset0Ref')
    w('        lda     #$%02X' % SCALE); w('        sta     <$04')
    w('        lda     #$7F'); w('        jsr     Intensity_a')
    w('        ldd     #$%02X%02X' % (b8(y0), b8(x0))); w('        jsr     Moveto_d')
    dx, dy = x1 - x0, y1 - y0
    n = max(1, (max(abs(dx), abs(dy)) + 126) // 127)
    px = py = 0
    for k in range(1, n + 1):
        nx, ny = round(dx * k / n), round(dy * k / n)
        w('        ldd     #$%02X%02X' % (b8(ny - py), b8(nx - px))); w('        jsr     Draw_Line_d'); px, py = nx, ny

def page_objects(p, eye, col, frame_var):
    """the lines of page p for one eye and one colour"""
    if p in (1, 2):
        items = [b for b in BOXES if b[0] == col]
        rev = p == 2 and eye == 1
        for c, x, y, o in (reversed(items) if rev else items):
            draw_box(c, x, y, o, eye, rev)
    elif p == 3:
        for c, xn, yn, xf, yf, on, of_ in RAILS:
            if c == col: draw_line(eye_x(xn, on, eye), yn, eye_x(xf, of_, eye), yf)
        for c, x0, y, x1, o in BARS:
            if c == col: draw_line(eye_x(x0, o, eye), y, eye_x(x1, o, eye), y)
    elif p == 4:
        if col == 1:               # (a green box moving right, wrapping; the left eye's half turn later: 2 units on)
            o = off(20)
            w('        jsr     Reset0Ref'); w('        lda     #$%02X' % SCALE); w('        sta     <$04')
            w('        lda     #$7F'); w('        jsr     Intensity_a')
            w('        ldb     MoveX')
            if eye == 1: w('        addb    #2')
            w('        addb    #%d' % (o // 2 if eye == 0 else -(o // 2)))
            w('        lda     #$00'); w('        jsr     Moveto_d'); w('        ldx     #Box'); w('        jsr     Draw_VLc')
            w('        jsr     Reset0Ref'); w('        ldb     MoveX')
            if eye == 1: w('        addb    #2')
            w('        addb    #8'); w('        lda     #$%02X' % b8(-14)); w('        jsr     Moveto_d'); w('        ldx     #Tick'); w('        jsr     Draw_VLc')
        if col == 0:               # (and a still box beside it)
            draw_box(0, -60, 60, 12, eye, False)
    elif p == 5:
        for k, (c, x, y, t, o) in enumerate(TEXTS):
            if c == col:
                w('        jsr     Reset0Ref'); w('        lda     #$7F'); w('        jsr     Intensity_a')
                w('        ldd     #$%02X%02X' % (b8(y), b8(eye_x(x, o, eye)))); w('        ldu     #Txt%d' % k); w('        jsr     Print_Str_d')

# ---------------------------------------------------------------- the program
w('; %s - generated by make_imager_test.py%s' % (STEM, ' (flat: every offset 0)' if FLAT else ''))
for n, a in (('Reset0Ref', 0xF354), ('Moveto_d', 0xF312), ('Draw_VLc', 0xF3CE), ('Draw_Line_d', 0xF3DF), ('Intensity_a', 0xF2AB),
             ('Print_Str_d', 0xF37A), ('Read_Btns', 0xF1BA), ('Sound_Byte', 0xF256), ('Vec_Btn_State', 0xC80F),
             ('Vec_Buttons', 0xC811), ('Vec_Text_HW', 0xC82A), ('Vec_Snd7', 0xC807), ('Vec_IRQ', 0xCBF8), ('Page', 0xC880), ('MoveX', 0xC881),
             ('Motor', 0xC882)):
    w('%-15s equ $%04X' % (n, a))
w()
w('        org     $0000')
w('        fcc     "g GCE 2026"'); w('        fcb     $80'); w('        fdb     $FD0D')
w('        fcb     $F8,$50,$20,-$56'); w('        fcc     "3D IMAGER TEST"'); w('        fcb     $80'); w('        fcb     0')
w()
w('Start:  orcc    #$10')                                  # (no interrupts while it is set up)
w('        lds     #$CBEA')
w('        lda     #$D0'); w('        tfr     a,dp'); w('        setdp   $D0')
w('        clr     Page'); w('        clr     MoveX')
w('        lda     #$7E'); w('        sta     Vec_IRQ'); w('        ldx     #Irq'); w('        stx     Vec_IRQ+1')   # (the index's interrupt: Irq)
w('        lda     #$80'); w('        sta     Motor')                     # (the motor on: the disc spins up)
w('        lda     #$CD'); w('        sta     <$0C')                      # (CA1: its edge - as Narrow Escape)
w('        lda     #$82'); w('        sta     <$0E')                      # (CA1's interrupt on)
w('        lda     #$7F'); w('        sta     <$0D')                      # (all flags clear)
w('        ldb     Vec_Snd7'); w('        orb     #$40'); w('        lda     #7'); w('        jsr     Sound_Byte')   # (port A an output)
w('        lda     #14'); w('        ldb     Motor'); w('        jsr     Sound_Byte')
w('        lda     #$%02X' % ((TURN & 0xFF))); w('        sta     <$08'); w('        lda     #$%02X' % (TURN >> 8)); w('        sta     <$09')
w('        andcc   #$EF')
w('Spin:   cwai    #$EF')                                   # (until the disc's index comes round: Irq restarts the frame)
w('        bra     Spin')
w()
w('Irq:    lds     #$CBEA')                                 # (a turn has gone round: the frame starts again)
w('        lda     #$D0'); w('        tfr     a,dp')
w('        ldb     #$FF')                                   # (the motor: on for this turn when the last took longer than a frame)
w('        lda     <$0D'); w('        bita    #$20'); w('        beq     MotOff'); w('        ldb     #$80')
w('MotOff: stb     Motor')
w('        lda     #$%02X' % ((TURN & 0xFF))); w('        sta     <$08'); w('        lda     #$%02X' % (TURN >> 8)); w('        sta     <$09')   # (timer 2: this turn)
w('        lda     #$02'); w('        sta     <$0D')                      # (CA1's flag cleared)
w('        lda     #14'); w('        ldb     Motor'); w('        jsr     Sound_Byte')
w('        andcc   #$EF')
w('        jmp     Frame')
w()
w('Frame:  ldb     Vec_Snd7'); w('        andb    #$BF'); w('        lda     #7'); w('        jsr     Sound_Byte')   # (the buttons: port A an input a moment)
w('        jsr     Read_Btns')
w('        ldb     Vec_Snd7'); w('        orb     #$40'); w('        lda     #7'); w('        jsr     Sound_Byte')
w('        lda     #14'); w('        ldb     Motor'); w('        jsr     Sound_Byte')
w('        lda     Vec_Buttons')                          # (the presses since the last read: a page a press)
w('        bita    #$08'); w('        beq     NoNext'); w('        lda     Page'); w('        inca'); w('        cmpa    #5'); w('        blo     SetPg'); w('        clra')
w('SetPg:  sta     Page'); w('        bra     Moved')
w('NoNext: bita    #$01'); w('        beq     Moved'); w('        lda     Page'); w('        deca'); w('        bpl     SetPg'); w('        lda     #4'); w('        bra     SetPg')
w('Moved:  inc     MoveX')
w('        lda     MoveX'); w('        cmpa    #100'); w('        blt     MxOk'); w('        lda     #$%02X' % b8(-100)); w('        sta     MoveX')
w('MxOk:   ldd     #$F848'); w('        std     Vec_Text_HW')
w('        ldb     Page')
for p in range(1, 6):
    w('        cmpb    #%d' % (p - 1)); w('        lbeq    Pg%d' % p)
w('        lbra    Pg1')
for p in range(1, 6):
    w(); w('Pg%d:' % p)
    for s in range(6):
        eye, col = s // 3, s % 3
        start = (0.5 * eye + (0.0, A1, A2)[col] + MARGIN) * TURN
        th = (TURN - int(start)) >> 8                           # (timer 2 counts down from TURN: wait until its high byte is down to here)
        w('P%dS%d:  lda     <$09' % (p, s)); w('        cmpa    #$%02X' % th); w('        bhi     P%dS%d' % (p, s))
        if s == 0:                                              # (the page's name, at depth 0, in the right eye's blue)
            pass
        page_objects(p, eye, col, 'MoveX')
        if s == 0 or s == 3:
            w('        jsr     Reset0Ref'); w('        lda     #$5F'); w('        jsr     Intensity_a')
            w('        ldd     #$%02X%02X' % (b8(110), b8(-40))); w('        ldu     #Name%d' % p); w('        jsr     Print_Str_d')
    w('        jsr     Reset0Ref')
    w('        cwai    #$EF')                                   # (the turn's rest: the next index restarts the frame)
    w('        bra     *-2')
w()
w('Box:    fcb     %d' % (len(BOX) - 1))
for dy, dx in BOX: w('        fcb     %d,%d' % (dy, dx))
w('BoxRev: fcb     %d' % (len(BOX_REV) - 1))
for dy, dx in BOX_REV: w('        fcb     %d,%d' % (dy, dx))
w('Tick:   fcb     0'); w('        fcb     -6,0')
for p, n in enumerate(('1 DEPTHS', '2 REVERSED', '3 RAILS', '4 MOVING', '5 TEXT'), 1):
    w('Name%d:  fcc     "%s"' % (p, n)); w('        fcb     $80')
for k, (c, x, y, t, o) in enumerate(TEXTS):
    w('Txt%d:   fcc     "%s"' % (k, t)); w('        fcb     $80')

here = os.path.dirname(os.path.abspath(__file__))
if '--pack' in sys.argv:
    raw = open(os.path.join(here, STEM + '_raw.bin'), 'rb').read(); assert len(raw) <= 32768, len(raw)
    img = raw + b'\xFF' * (32768 - len(raw))
    open(os.path.join(here, STEM + '.bin'), 'wb').write(img); open(os.path.join(here, STEM + '_64k.bin'), 'wb').write(img * 2)
    os.remove(os.path.join(here, STEM + '_raw.bin')); print('packed %s.bin (%d bytes used), %s_64k.bin' % (STEM, len(raw), STEM))
else:
    open(os.path.join(here, STEM + '.asm'), 'w', newline='\n').write('\n'.join(L) + '\n'); print('wrote %s.asm' % STEM)
