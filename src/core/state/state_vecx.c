/* state_vecx.c - vecx.c, unchanged, with all its data in the save-state sections (.vxd initialised, .vxb zeroed), so the
 * core can save and restore the running game whole: run-ahead (live.c). Built by build_core.bat (Visual C++, VX_STATES) and
 * the libretro core (clang);
 * the other systems' builds compile vecx.c itself and have no run-ahead. Copyright (c) 2026 Hugo Bokhorst. Part of
 * Vectrex_EMU; see LICENSE.md. */
#if defined(_MSC_VER)
#pragma data_seg(".vxd$m")
#pragma bss_seg(".vxb$m")
#elif defined(__clang__)                /* (clang: the sections vxd / vxb, bounded by the linker's __start_ / __stop_) */
#pragma clang section data="vxd" bss="vxb"
#else
#error "the save-state sections need Visual C++ or clang"
#endif
#include "../vecx.c"
#if defined(_MSC_VER)
#pragma data_seg()
#pragma bss_seg()
#else
#pragma clang section data="" bss=""
#endif

/* the big buffers that are not the running game's state, left out of a save: the display's own vector lists (vecx's
 * drawing, not read back), the cartridge image (saved apart, only for a flash cartridge that writes to it), the UVMC2 /
 * PiTrex images (those games have no run-ahead) and the speech ROM */
void vxs_excl_vecx (void (*add) (const void *, size_t))
{
	add (vectors_set, sizeof vectors_set); add (vector_hash, sizeof vector_hash); add (cart, sizeof cart);
	add (uvm2_image, sizeof uvm2_image); add (px_image, sizeof px_image); add (sp_rom, sizeof sp_rom);
}
