"""make_pitrex_module.py - src/frontend/pitrex_art.py from icons/pitrex_cart.png and icons/pitrex_box.png: the PiTrex's tiles
held in the program (no data files to ship). Run after the pictures change."""
import base64, io, os
here = os.path.dirname(os.path.abspath(__file__)); root = os.path.dirname(here)
L = ['"""pitrex_art.py - the PiTrex\'s tiles in the game library, held here (made by tools/make_pitrex_module.py from',
     'icons/pitrex_cart.png and icons/pitrex_box.png - the owner\'s photo of the cartridge and its logo, 2026-11-22).',
     'path(root, look): the look\'s picture as a file in the program\'s cache folder, written once."""',
     'import base64, os', '', 'PNG = {']
for look, f in (('carts', 'pitrex_cart.png'), ('boxes', 'pitrex_box.png')):
    b = base64.b64encode(open(os.path.join(root, 'icons', f), 'rb').read()).decode()
    L.append("    '%s': (" % look)
    for i in range(0, len(b), 120):
        L.append("        '%s'" % b[i:i + 120])
    L.append('    ),')
L += ['}', '', 'def path(root, look):',
      '    """the picture for the look (the overlays: the cartridge\'s) as a file - None if it cannot be written"""',
      "    k = look if look in PNG else 'carts'",
      "    d = os.path.join(root, 'cache'); p = os.path.join(d, 'pitrex_%s.png' % k); data = base64.b64decode(PNG[k])",
      '    try:', '        if not os.path.isfile(p) or os.path.getsize(p) != len(data):', '            os.makedirs(d, exist_ok=True)',
      "            with open(p, 'wb') as f:", '                f.write(data)', '        return p', '    except OSError:', '        return None', '']
io.open(os.path.join(root, 'src', 'frontend', 'pitrex_art.py'), 'w', encoding='utf-8', newline='\n').write('\n'.join(L))
print('wrote src/frontend/pitrex_art.py')
