# Photo matching scripts (2026-11-21)

The working scripts of the session that fitted the dots and the beam-on-phosphor law to the owner's raw photos
(`G:\Google Drive\#Sharing\Vectrex\Testcarts`, the Dot Test photos in `...\Dot test`). Kept as they were used: they
expect to run from one folder that also holds their caches (`*_raw.npy` from the DNGs, `oct/*.npy`, the snapshots) and
the test core DLLs (`cb/`). To be made into one tool; until then, what each does:

| Script | What |
|---|---|
| `dng.py` | DNG (ProRAW, linear) -> `.npy` caches + previews |
| `pcpages.py`, `pcvb.py`, `pcgrab.py` | the PC app from source, minimised, focus kept, skip intro, no wobble / bending, G-Sync: Dot Test pages 1-4 / Vectorblade's logo (frame 520) / one page as F12 snapshots at the window's size. Env: `LOOK`, `BEAM=1` (beam trace), `VX_BT_EXPO`, `VX_BT_SPOT`, `VR` (another vecrender.py) |
| `renall.py`, `rend3.py` | the core (tools/lrhost.py) on every test cart and page |
| `compose.py`, `beamcmp.py` | raw photo next to render -> `testcart/compare/*.png` |
| `cmpd.py`, `pcsweep.py` | page 1: every dot's radius at 90 / 50 / 25 / 12 % of white, photo vs render |
| `erat.py` | page 1: every dot's light / the reference line's light a unit (linear; exposure- and blur-free) |
| `cut.py`, `cutemu.py` | page 4: how far each corner's tip stays inside the sharp corner - photo / the core's own beam path (`VT_RAMPRC`, `VT_VLAG`, ...) |
| `lagsim.py` | a 90 degree corner through 1-4 stage spot lags: none makes an 8-cycle rest as round as none |
| `ridge.py`, `vbrest.py` | Vectorblade's logo: joint dots / line level; the beam's time by speed |

Test rules (the owner): skip intro on, wobble and brightness bending off, G-Sync on, minimised, the owner's window
keeps the focus, pictures by F12 at the window's size.
