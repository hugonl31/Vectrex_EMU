"""palette.py - the interface's colour scheme: blue (the default) or green.

The owner, 2026-11-22: next to a real Vectrex the green deck and menus made the vectors look greenish - the tube's white is a
blue one. So everything green may be a deeper blue: a green (hue 100 - 190 degrees, with some colour in it) goes to hue 216,
1.25 x as strong; white, orange, red and the vectors' own cool white stay. One rule in every interface (libretro/browser.c
vx_pal, the iOS app's vx_pal, the Android app's Main.pal).

NAME is set before the first window is made (vectrex_emu.py, from the settings). Tk: every colour a widget is given passes
through Misc._options - hooked here, so no call site names the palette. OpenGL (the deck, the OSD): rgb()."""
import colorsys, re
import tkinter as tk

NAME = 'blue'
HUE, SAT_K = 216.0 / 360.0, 1.25
_HEX = re.compile(r'^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6})$')
_cache = {}

def rgb(c):
    """(r, g, b) 0..1 in the scheme"""
    if NAME != 'blue':
        return c
    r, g, b = c[0], c[1], c[2]
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if 100.0 / 360.0 < h < 190.0 / 360.0 and s > 0.12:
        return colorsys.hsv_to_rgb(HUE, min(1.0, s * SAT_K), v) + tuple(c[3:])
    return c

def hexs(c):
    """'#rgb' / '#rrggbb' in the scheme (anything else: as it is)"""
    if NAME != 'blue' or not isinstance(c, str) or not _HEX.match(c):
        return c
    o = _cache.get(c)
    if o is None:
        t = c[1:]
        if len(t) == 3:
            t = ''.join(ch * 2 for ch in t)
        r, g, b = rgb(tuple(int(t[i:i + 2], 16) / 255.0 for i in (0, 2, 4)))
        o = _cache[c] = '#%02x%02x%02x' % (int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5))
    return o

def bgr(v):
    """a Windows COLORREF (0x00bbggrr) in the scheme"""
    r, g, b = rgb(((v & 255) / 255.0, (v >> 8 & 255) / 255.0, (v >> 16 & 255) / 255.0))
    return int(b * 255 + 0.5) << 16 | int(g * 255 + 0.5) << 8 | int(r * 255 + 0.5)

_opts = tk.Misc._options
def _options(self, cnf, kw=None):
    res = _opts(self, cnf, kw)
    if NAME != 'blue':
        return res
    return tuple(hexs(v) if isinstance(v, str) and v[:1] == '#' else v for v in res)
tk.Misc._options = _options
