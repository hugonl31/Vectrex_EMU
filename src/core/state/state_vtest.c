/* state_vtest.c - vtest.c, unchanged, with all its data in the save-state sections (.vxd initialised, .vxb zeroed), so the
 * core can save and restore the running game whole: run-ahead (live.c). Built by build_core.bat (Visual C++, VX_STATES);
 * the other systems' builds compile vtest.c itself and have no run-ahead. Copyright (c) 2026 Hugo Bokhorst. Part of
 * Vectrex_EMU; see LICENSE.md. */
#ifndef _MSC_VER
#error "the save-state sections need Visual C++ (#pragma data_seg / bss_seg)"
#endif
#pragma data_seg(".vxd$m")
#pragma bss_seg(".vxb$m")
#include "../vtest.c"
#pragma data_seg()
#pragma bss_seg()
