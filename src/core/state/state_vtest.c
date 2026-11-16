/* state_vtest.c - vtest.c, unchanged, with all its data in the save-state sections (.vxd initialised, .vxb zeroed), so the
 * core can save and restore the running game whole: run-ahead (live.c). Built by build_core.bat (Visual C++, VX_STATES) and
 * the libretro core (clang);
 * the other systems' builds compile vtest.c itself and have no run-ahead. Copyright (c) 2026 Hugo Bokhorst. Part of
 * Vectrex_EMU; see LICENSE.md. */
#if defined(_MSC_VER)
#pragma data_seg(".vxd$m")
#pragma bss_seg(".vxb$m")
#elif defined(__clang__) && defined(__APPLE__)   /* (Mach-O: a segment and a section - state_core.c bounds them) */
#pragma clang section data="__DATA,vxd" bss="__DATA,vxb"
#elif defined(__clang__)                /* (clang: the sections vxd / vxb, bounded by the linker's __start_ / __stop_) */
#pragma clang section data="vxd" bss="vxb"
#else
#error "the save-state sections need Visual C++ or clang"
#endif
#include "../vtest.c"
#if defined(_MSC_VER)
#pragma data_seg()
#pragma bss_seg()
#else
#pragma clang section data="" bss=""
#endif
