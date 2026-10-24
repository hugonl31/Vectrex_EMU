from PIL import Image, ImageDraw, ImageFilter
import math
S=1024
def poly(d,p,fill,outline=None,w=0): d.polygon(p,fill=fill,outline=outline,width=w)
def glow(size, lines, width, col):
    lay=Image.new('RGBA',size,(0,0,0,0)); d=ImageDraw.Draw(lay)
    for l in lines: d.line(l,fill=col+(255,),width=width,joint='curve')
    g=lay.filter(ImageFilter.GaussianBlur(width*2.0))
    o=Image.alpha_composite(Image.new('RGBA',size,(0,0,0,0)),g); o=Image.alpha_composite(o,g); return Image.alpha_composite(o,lay)
def make(variant):
    im=Image.new('RGBA',(S,S),(0,0,0,0)); d=ImageDraw.Draw(im)
    # cabinet: front face x 150..690, y 150..730 ; depth offset (dx,dy)=(150,-90)
    fx0,fy0,fx1,fy1=200,130,660,770; dx,dy=150,-90
    fb=fy1+150                                   # (the box's bottom: one rectangle, the screen above, an opening below)
    # right side face
    poly(d,[(fx1,fy0),(fx1+dx,fy0+dy),(fx1+dx,fb+dy),(fx1,fb)],(38,38,41,255))
    # top
    poly(d,[(fx0,fy0),(fx0+dx,fy0+dy),(fx1+dx,fy0+dy),(fx1,fy0)],(66,66,70,255))
    # front face: the whole box
    d.rounded_rectangle((fx0,fy0,fx1,fb),18,fill=(50,50,54,255))
    # the opening below the screen (dark, recessed), its vents
    d.rectangle((fx0+30,fy1+10,fx1-30,fb),fill=(20,20,22,255))
    d.rectangle((fx0+30,fy1+10,fx1-30,fy1+26),fill=(12,12,13,255))
    for i in range(6):
        y=fy1+45+i*17; d.line([(fx0+150,y),(fx0+330,y)],fill=(42,42,45,255),width=7)
    # the top stripe
    d.line([(fx0+20,fy0+30),(fx1-20,fy0+30)],fill=(110,110,115,255),width=6)
    # screen recess: outer opening and inner glass (walls in between, lit differently)
    ox0,oy0,ox1,oy1=fx0+35,fy0+70,fx1-35,fy1-30
    gx0,gy0,gx1,gy1=ox0+70,oy0+55,ox1-40,oy1-70
    poly(d,[(ox0,oy0),(ox1,oy0),(gx1,gy0),(gx0,gy0)],(20,20,22,255))   # top wall (dark)
    poly(d,[(ox0,oy0),(gx0,gy0),(gx0,gy1),(ox0,oy1)],(34,34,37,255))   # left wall
    poly(d,[(ox1,oy0),(ox1,oy1),(gx1,gy1),(gx1,gy0)],(28,28,30,255))   # right wall
    poly(d,[(ox0,oy1),(gx0,gy1),(gx1,gy1),(ox1,oy1)],(58,58,62,255))   # bottom wall (lit)
    d.rounded_rectangle((gx0-6,gy0-6,gx1+6,gy1+6),50,fill=(12,12,13,255))
    scr=(150,152,148,255) if variant=='A' else (40,44,48,255)
    d.rounded_rectangle((gx0,gy0,gx1,gy1),46,fill=scr)
    if variant=='B':
        cx,cy=(gx0+gx1)/2,(gy0+gy1)/2; s=80
        ship=[(cx,cy-0.9*s),(cx+0.55*s,cy+0.7*s),(cx,cy+0.35*s),(cx-0.55*s,cy+0.7*s),(cx,cy-0.9*s)]
        im=Image.alpha_composite(im,glow((S,S),[ship],13,(235,245,255))); d=ImageDraw.Draw(im)
    # controller in front-left
    cx0,cy0,cx1,cy1=60,860,470,960; cdx,cdy=35,-35
    poly(d,[(cx0,cy0),(cx0+cdx,cy0+cdy),(cx1+cdx,cy0+cdy),(cx1,cy0)],(70,70,74,255))
    poly(d,[(cx1,cy0),(cx1+cdx,cy0+cdy),(cx1+cdx,cy1+cdy),(cx1,cy1)],(30,30,32,255))
    d.rectangle((cx0,cy0,cx1,cy1),fill=(44,44,48,255))
    d.ellipse((cx0+55,cy0-40,cx0+125,cy0+0),fill=(20,20,22,255))
    d.line([(cx0+90,cy0-20),(cx0+90,cy0-120)],fill=(25,25,27,255),width=22)
    d.ellipse((cx0+70,cy0-150,cx0+110,cy0-105),fill=(225,205,180,255))   # the stick's cream knob
    for i in range(4):
        bx=cx0+160+i*62; d.ellipse((bx,cy0-24,bx+46,cy0+0),fill=(18,18,20,255))
    d.line([(cx0+20,cy0+45),(cx1-20,cy0+45)],fill=(95,95,100,255),width=5)
    return im
for v in 'AB': make(v).save('icon4_%s_1024.png'%v)
sheet=Image.new('RGB',(2*420,2*330),(240,240,240)); d=ImageDraw.Draw(sheet); d.rectangle((0,330,840,660),fill=(32,32,32))
for i,v in enumerate('AB'):
    big=Image.open('icon4_%s_1024.png'%v)
    for row in (0,1):
        x=i*420+10; y=row*330+10
        im=big.resize((256,256),Image.LANCZOS); sheet.paste(im,(x,y),im)
        xx=x+270
        for sz in (48,32,16):
            sm=big.resize((sz,sz),Image.LANCZOS); sheet.paste(sm,(xx,y+10),sm)
            if sz<48: sm2=sm.resize((sz*2,sz*2),Image.NEAREST); sheet.paste(sm2,(xx,y+130),sm2)
            xx+=sz+12
        d.text((x,y+266),'option '+v,fill=(0,0,0) if row==0 else (220,220,220))
sheet.save('icon4_options.png')
