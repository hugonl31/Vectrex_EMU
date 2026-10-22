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
        ITEMS.add(new Item("speech", "Controller port 2", "off", new String[] { "off", "vecvoice", "vecvox" },
                new String[] { "Joystick", "VecVoice speech (SP0256)", "VecVox speech (SpeakJet)" }, "Needs the speech ROM (al2.bin): Choose speech ROM, above"));
        ITEMS.add("Picture");
        ITEMS.add(new Item("renderer", "Renderer", "auto", new String[] { "auto", "vulkan", "gles" },
                new String[] { "Auto (Vulkan, else OpenGL ES)", "Vulkan", "OpenGL ES 3.1" }, null));
        ITEMS.add(new Item("render_scale", "Phosphor resolution", 1.0f, 0.5f, 1.0f, "Below 1 for a slow graphics chip"));
        ITEMS.add(new Item("phosphor", "Phosphor and bloom", "1", "Off: crisp lines, no afterglow"));
        ITEMS.add(new Item("trail_on", "Afterglow", "1", null));
        ITEMS.add(new Item("trail", "Afterglow length", 0.6f, 0.05f, 1.2f, "1 = as measured on a real tube"));
        ITEMS.add(new Item("bloom", "Bloom strength", 0.55f, 0f, 1.1f, null));
        ITEMS.add(new Item("phos_grow", "Afterglow joining", 0.5f, 0f, 1f, null));
        ITEMS.add(new Item("phos_smooth", "Afterglow overlap", 0.8f, 0f, 1.6f, null));
        ITEMS.add(new Item("halo_on", "Corner glow", "1", null));
        ITEMS.add(new Item("halo_k", "Corner glow strength", 0.014f, 0f, 0.028f, null));
        ITEMS.add(new Item("curve_on", "Curved tube", "1", null));
        ITEMS.add(new Item("wob_on", "Mains hum wobble", "1", null));
        ITEMS.add(new Item("wob_strength", "Wobble strength", 0.1074f, 0f, 0.2148f, null));
        ITEMS.add(new Item("mains_hz", "Mains frequency", "50", new String[] { "50", "60" }, new String[] { "50 Hz (Europe)", "60 Hz (USA)" }, null));
        ITEMS.add(new Item("bend_on", "Brightness bending", "1", "The picture grows a little when much is lit"));
        ITEMS.add(new Item("bend_strength", "Bending strength", 0.6f, 0f, 1.2f, null));
        ITEMS.add(new Item("line_gap", "Line gaps", 12.3f, 0f, 24.6f, "The integrators' stop delay (cycles): the dark line ends"));
        ITEMS.add("Overlay");
        ITEMS.add(new Item("art_on", "Overlay", "1", "MiSTer .art or PNG, named like the ROM: in the overlay folder or next to the ROM"));
        ITEMS.add(new Item("art_tint", "Tint strength", 1.0f, 0f, 2f, null));
        ITEMS.add(new Item("art_room", "Room light", 0.0648f, 0f, 0.1296f, null));
        ITEMS.add(new Item("art_print", "Printed art brightness", 0.36f, 0f, 0.72f, null));
        ITEMS.add(new Item("art_parallax", "Parallax", "1", "The sheet a little in front of the tube"));
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

    /** everything for the native side, as two arrays */
    static String[][] forNative(Context c, String speechRom) {
        SharedPreferences p = get(c);
        List<String> k = new ArrayList<>(), v = new ArrayList<>();
        for (Object o : ITEMS) if (o instanceof Item) { Item it = (Item) o; k.add(it.key); v.add(value(p, it)); }
        if (speechRom != null) { k.add("speech_rom"); v.add(speechRom); }
        return new String[][] { k.toArray(new String[0]), v.toArray(new String[0]) };
    }
}
