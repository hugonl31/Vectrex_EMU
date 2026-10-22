package io.github.hugonl31.vectrexemu;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.ArrayList;
import java.util.List;

/** The settings: every one with its default (the desktop program's, the owner's choices) and how the
 *  settings screen shows it. The native side gets them as key / value strings (engine.cpp configure). */
final class Prefs {
    static final String FILE = "vectrex";
    // keys of the app itself (not passed to the emulator's look)
    static final String ROM_TREE = "rom_tree", OVERLAY_TREE = "overlay_tree", BUTTONS = "buttons", DEADZONE = "deadzone";
    static final String ORIENT = "orientation", OFF_EFFECT = "off_effect", SEED = "scr_seed", RECENT = "recent_games";
    // per game: its overlay ("" automatic, "none", or a file name); per overlay file: its position and size
    static String overlayFor(String game) { return "overlay_for/" + game; }
    static String adjustOf(String overlay) { return "overlay_adjust/" + overlay; }

    enum Kind { BOOL, CHOICE, SLIDER }

    static final class Item {
        final String key, label, help, def; final Kind kind;
        final String[] values, names;          // CHOICE
        final float min, max;                  // SLIDER
        Item(String key, String label, String def, String help) {
            this.key = key; this.label = label; this.def = def; this.help = help; kind = Kind.BOOL;
            values = names = null; min = max = 0;
        }
        Item(String key, String label, String def, String[] values, String[] names, String help) {
            this.key = key; this.label = label; this.def = def; this.help = help; kind = Kind.CHOICE;
            this.values = values; this.names = names; min = max = 0;
        }
        Item(String key, String label, float def, float min, float max, String help) {
            this.key = key; this.label = label; this.def = Float.toString(def); this.help = help; kind = Kind.SLIDER;
            values = names = null; this.min = min; this.max = max;
        }
    }

    /** sections of the settings screen: a title, then its items */
    static final List<Object> ITEMS = new ArrayList<>();
    static {
        ITEMS.add("Vectrex");
        ITEMS.add(new Item("skip_boot", "Skip the BIOS intro", "0", "Power on straight into the cartridge (the VECTREX logo and its tune run unseen)"));
        ITEMS.add(new Item("cart_hw", "Cartridge hardware", "auto",
                new String[] { "auto", "off", "pb6", "vb", "hb512", "multi" },
                new String[] { "Auto (by the ROM's size)", "Off - bone stock, 32 KB", "64 KB, PB6 bank switching",
                               "Vectorblade / MiSTer flash, 4 x 48 KB", "512 KB flash with bank latch", "1 MB multi-cart" }, null));
        ITEMS.add(new Item("ideal", "Perfect Vectrex", "0", "No analog drift: an ideal machine instead of a real one"));
        ITEMS.add(new Item(OFF_EFFECT, "Switch off when quitting", "0",
                "The picture's last zap and the switch's click as you quit (half a second); off: back at once"));
        ITEMS.add(new Item("speech", "Controller port 2", "off", new String[] { "off", "vecvoice", "vecvox" },
                new String[] { "Joystick", "VecVoice speech (SP0256)", "VecVox speech (SpeakJet)" }, "Needs the speech ROM (al2.bin): Choose speech ROM, above"));
        ITEMS.add("Picture");
        ITEMS.add(new Item("renderer", "Renderer", "auto", new String[] { "auto", "vulkan", "gles" },
                new String[] { "Auto (Vulkan, else OpenGL ES)", "Vulkan", "OpenGL ES 3.1" }, null));
        ITEMS.add(new Item("render_scale", "Phosphor resolution", 1.0f, 0.5f, 1.0f, "Below 1 for a slow graphics chip"));
        ITEMS.add(new Item(ORIENT, "Screen", "landscape", new String[] { "landscape", "portrait", "reverse_portrait" },
                new String[] { "Landscape", "Portrait (the device on its side: the picture fills it)", "Portrait, the other way" }, null));
        ITEMS.add(new Item("phosphor", "Phosphor and bloom", "1", "Off: crisp lines, no afterglow"));
        ITEMS.add(new Item("trail_on", "Afterglow", "1", null));
        ITEMS.add(new Item("trail", "Afterglow length", 0.6f, 0.05f, 1.2f, "1 = as measured on a real tube"));
        ITEMS.add(new Item("bloom", "Bloom strength", 0.55f, 0f, 1.1f, null));
        ITEMS.add(new Item("phos_grow", "Afterglow joining", 0.5f, 0f, 1f, null));
        ITEMS.add(new Item("phos_smooth", "Afterglow overlap", 0.8f, 0f, 1.6f, null));
        ITEMS.add(new Item("halo_on", "Corner glow", "1", null));
        ITEMS.add(new Item("halo_k", "Corner glow strength", 0.014f, 0f, 0.028f, null));
        ITEMS.add(new Item("even_beam", "Even beam", "0", "An ideal vector display: every point as bright and wide as a line, no corner glow"));
        ITEMS.add(new Item("curve_on", "Curved tube", "1", null));
        ITEMS.add(new Item("wob_on", "Mains hum wobble", "1", null));
        ITEMS.add(new Item("wob_strength", "Wobble strength", 0.1074f, 0f, 0.2148f, null));
        ITEMS.add(new Item("mains_hz", "Mains frequency", "50", new String[] { "50", "60" }, new String[] { "50 Hz (Europe)", "60 Hz (USA)" }, null));
        ITEMS.add(new Item("bend_on", "Brightness bending", "1", "The picture grows a little when much is lit"));
        ITEMS.add(new Item("bend_strength", "Bending strength", 0.6f, 0f, 1.2f, null));
        ITEMS.add(new Item("bend_type", "Bending type", "0", new String[] { "0", "1", "2" }, new String[] { "Grow", "Shift", "Grow and shift" }, null));
        ITEMS.add(new Item("line_gap", "Line gaps", 12.3f, 9.3f, 15.3f, "The integrators' stop delay (cycles): 12.3 a well-tuned Vectrex"));
        ITEMS.add("Overlay");
        ITEMS.add(new Item("art_on", "Overlay", "1", "MiSTer .art or PNG, named like the ROM: in the overlay folder or next to the ROM"));
        ITEMS.add(new Item("art_tint", "Tint strength", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("art_room", "Room light", 0.0648f, 0f, 0.1296f, null));
        ITEMS.add(new Item("art_print", "Printed art brightness", 0.36f, 0f, 0.72f, null));
        ITEMS.add(new Item("art_parallax", "Parallax", "1", "The sheet a little in front of the tube"));
        ITEMS.add(new Item("art_glow", "Plastic glow", "1", "The plastic spreads the light softly around bright lines."
                + " (A slow graphics chip: with Scratches and this both off, the overlay costs next to nothing)"));
        ITEMS.add(new Item("art_glow_k", "Plastic glow strength", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_on", "Scratches", "1", "Forty years of handling, made on the graphics card: they catch the tube's light"));
        ITEMS.add(new Item("scr_wear", "Wear (how much handled)", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_fine", "Fine scratches (the matte buff)", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_medium", "Medium scratches", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_large", "Damage (deep, through the colour)", 0.1f, 0f, 2f, "Little by default: most people want none"));
        ITEMS.add(new Item("scr_scrib", "Scribbles (curly ones)", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_glint", "Glint strength", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("scr_reach", "Glint reach", 1.0f, 0f, 2f, "Left: close to the lines, right: farther out"));
        ITEMS.add("Sound");
        ITEMS.add(new Item("volume", "Volume", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("buzz_on", "Buzz", "1", "The beam's currents in the speaker"));
        ITEMS.add(new Item("buzz_vol", "Buzz volume", 0.0912f, 0f, 0.1824f, null));
        ITEMS.add(new Item("speaker_on", "Built-in speaker", "1", null));
        ITEMS.add(new Item("voice_vol", "Speech volume", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("voice_full", "Speech: full-range speakers", "1", "Off: a small speaker"));
        ITEMS.add("Controls");
        ITEMS.add(new Item(BUTTONS, "Buttons 1-4", "xyab", new String[] { "xyab", "abxy", "yxba" },
                new String[] { "X Y A B (as the desktop)", "A B X Y", "Y X B A" }, null));
        ITEMS.add(new Item(DEADZONE, "Stick dead zone", 0.2f, 0f, 0.5f, null));
    }

    static SharedPreferences get(Context c) { return c.getSharedPreferences(FILE, Context.MODE_PRIVATE); }

    static String value(SharedPreferences p, Item it) { return p.getString(it.key, it.def); }

    static Item item(String key) {
        for (Object o : ITEMS) if (o instanceof Item && ((Item) o).key.equals(key)) return (Item) o;
        return null;
    }

    static String value(Context c, String key) {
        Item it = item(key);
        return get(c).getString(key, it != null ? it.def : "");
    }

    /** everything for the native side, as two arrays (more: extra key / value pairs) */
    static String[][] forNative(Context c, String... more) {
        SharedPreferences p = get(c);
        List<String> k = new ArrayList<>(), v = new ArrayList<>();
        for (Object o : ITEMS) if (o instanceof Item) { Item it = (Item) o; k.add(it.key); v.add(value(p, it)); }
        k.add(SEED); v.add(Integer.toString(p.getInt(SEED, 1)));
        for (int i = 0; i + 1 < more.length; i += 2) if (more[i + 1] != null) { k.add(more[i]); v.add(more[i + 1]); }
        return new String[][] { k.toArray(new String[0]), v.toArray(new String[0]) };
    }

    /** an overlay's position and size (picture heights, x): dx, dy, scale */
    static float[] adjust(Context c, String overlay) {
        float[] a = { 0f, 0f, 1f };
        String s = overlay == null ? null : get(c).getString(adjustOf(overlay), null);
        if (s != null) {
            String[] f = s.split(",");
            try { for (int i = 0; i < 3 && i < f.length; i++) a[i] = Float.parseFloat(f[i]); } catch (NumberFormatException e) { /* default */ }
        }
        return a;
    }

    /** the games played lately (newest first, at most 40): for the per-game overlay settings */
    static List<String> recent(Context c) {
        List<String> r = new ArrayList<>();
        for (String g : get(c).getString(RECENT, "").split("\n")) if (!g.isEmpty()) r.add(g);
        return r;
    }
    static void played(Context c, String game) {
        List<String> r = recent(c);
        r.remove(game); r.add(0, game);
        while (r.size() > 40) r.remove(r.size() - 1);
        get(c).edit().putString(RECENT, String.join("\n", r)).apply();
    }
}
