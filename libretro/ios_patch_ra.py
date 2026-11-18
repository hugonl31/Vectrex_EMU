"""ios_patch_ra.py - small changes to RetroArch's own source for the iOS app (run by the ios workflow on its clone):
  - its bundled assets unpacked again after every update: it read the app's build number as "n || 1", always 1, so only
    the first install unpacked them
  - the on-screen gamepad hidden while a keyboard is connected too (the iPad's Magic Keyboard), not only a controller:
    "Hide overlay when gamepad connected" asks GCKeyboard as well
  python ios_patch_ra.py <RetroArch's folder>
Copyright (c) 2026 Hugo Bokhorst. Part of Vectrex_EMU; see LICENSE.md."""
import os, sys

ra = sys.argv[1]


def patch(rel, old, new, count=1):
    p = os.path.join(ra, rel)
    s = open(p, encoding='utf-8').read()
    n = s.count(old)
    if n != count:
        sys.exit('%s: %d of "%s" (want %d)' % (rel, n, old[:60], count))
    open(p, 'w', encoding='utf-8').write(s.replace(old, new))
    print('patched', rel)


patch('frontend/drivers/platform_darwin.m', 'NSInteger bundleVersion = [bundleVersionString integerValue] || 1;',
      'NSInteger bundleVersion = MAX([bundleVersionString integerValue], 1);')

# a keyboard counts as a controller for the overlay (griffin: both files in one unit - the header's guard)
open(os.path.join(ra, 'vx_kb.h'), 'w').write('''#ifndef VX_KB_H
#define VX_KB_H
#include <stdbool.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
#include <objc/runtime.h>
#include <objc/message.h>
static bool vx_kb_connected(void)
{
   Class c = objc_getClass("GCKeyboard");
   if (!c)
      return false;
   return ((id (*)(id, SEL))objc_msgSend)((id)c, sel_registerName("coalescedKeyboard")) != nil;
}
#else
static bool vx_kb_connected(void) { return false; }
#endif
#endif
''')
patch('input/input_driver.c', '#include "input_driver.h"\n', '#include "input_driver.h"\n#include "../vx_kb.h"\n')
patch('input/input_driver.c', 'hide = hide || (input_config_get_device_name(0) != NULL);',
      'hide = hide || (input_config_get_device_name(0) != NULL) || vx_kb_connected();')
patch('runloop.c', '#include "runloop.h"\n', '#include "runloop.h"\n#include "vx_kb.h"\n')
patch('runloop.c', 'bool controller_connected             = (input_config_get_device_name(0) != NULL);',
      'bool controller_connected             = (input_config_get_device_name(0) != NULL) || vx_kb_connected();')
