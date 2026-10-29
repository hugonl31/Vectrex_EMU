# The phosphor model: fast-moving vectors

Snapshots from reworking the phosphor afterglow (October 2026). The test object was a helicopter-like shape: a still body plus four pizza-slice blades spinning fast and flying sideways. It's modelled on how Armor Attack draws its helicopter.

On the real tube, every blade position flashes once, razor sharp. The blades light up one after another around the circle, and only the moving body leaves a fading trail. Getting there took the steps below.

Unless noted, the images are crops of the renderer's own phosphor picture, read straight from the GPU (screen captures can catch a picture half-drawn). They are brightened with a 0.4 gamma so the faint trails show.

| File | What it shows |
|---|---|
| `00_rotor_test_cart.png` | The rotor test cart (`testcart/make_rotor4_test.py`): three helicopters at 2, 5 and 9 / 64 turns per frame. |
| `01_old_model_dotted_trails_and_gaps.png` | The model before the rework, in three display modes. Every blade copy left dotted trails (the beam's bright corner dots outlived the lines), and a short-lived "release" rule cut gaps into the body. |
| `02_release_rule_cuts_blades.png` | Single blades: the release rule on its own cuts the blades at the hub; averaging several game frames on its own is clean. |
| `03_afterglow_cap_removes_corner_dots.png` | Afterglow capped at a line's brightness: the corner dots fade with their line, and the dotted trail is gone. |
| `04_gsync_held_copies_before.png` | G-Sync, before: each flash was held at full brightness for a whole game frame, so 2 or 3 blade sets showed at once. |
| `05_gsync_one_flash_per_picture.png` | G-Sync, after: whatever the new picture doesn't draw again starts fading at once. One sharp blade set per picture. |
| `06_gsync_one_flash_screen.png` | The same on screen (with bloom and tone). |
| `07_glow_cut_by_trail.png` | The remaining flaw of the one-layer model: one brightness and age per pixel, so a new line's soft glow was cut wherever the old trail was brighter. |
| `08_one_layer_trail_steps.png` | One-layer model, trail detail: as now, joining off, joining and smoothing off. With smoothing off the trail is all hard steps: overlapping copies replaced each other. |
| `09_two_layer_trail_looks.png` | The new two-layer model: joining + smoothing (left), smoothing only (middle), crisp copies (right). Crisp copies became the default. |
| `10_two_layer_holes_fixed.png` | Two-layer model: before (left), afterglow started only where nothing was lit, which left trail holes shaped like the new lines (middle); fixed (right). |
| `11_two_layer_final.png` | The final model: whole glow on every new line, crisp fading copies behind, no cut-outs. |

`reports/` holds screenshots of what was seen in the emulator at each step: helicopter artefacts, sharp edges in Smooth motion and G-Sync, trail steps, glow cut by the trail, and joining growing blocks while paused.

## The model in short

- **Lit layer.** A spot at full brightness while the beam's flash is held: until the game draws its next picture. A whole new picture (G-Sync, Smooth motion) replaces this layer completely, so a moved line flashes once.
- **Afterglow layer.** The two parts of the afterglow measured on a real Vectrex (about 14 % with a 45 ms time constant, and 6 % with 350 ms), each kept as an energy per pixel. A flash starts its afterglow when it leaves the lit layer, or for the part of its light a new flash doesn't replace. Overlapping afterglows add up.
- **The screen.** Lit layer plus afterglow, added, so a new line's glow over a trail adds to it.
- **Options.** Afterglow joining (a fading flash spreads a few pixels once) and afterglow smoothing, both off by default, in Config > Tube.
