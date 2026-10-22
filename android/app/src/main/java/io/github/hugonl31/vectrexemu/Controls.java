package io.github.hugonl31.vectrexemu;

import android.content.Context;
import android.view.InputDevice;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/** Each controller's mapping, kept by its name as the desktop does (vectrex_emu.py default_map): its keys or
 *  buttons for the Vectrex buttons 1-4 and for half the stick, which stick moves the Vectrex's stick, its dead
 *  zone, and which player it is. Keyboards and TV remotes share one mapping, "Keyboard". */
final class Controls {
    static final String KEYBOARD = "Keyboard";
    static final String[] BUTTONS = { "b1", "b2", "b3", "b4", "half" };
    static final String[] BUTTON_NAMES = { "Button 1", "Button 2", "Button 3", "Button 4", "Half stick (held: half the deflection)" };

    static final class Map {
        int[] keys = new int[5];                // (b1-b4, half; 0 = none)
        String stick = "left";                  // left, right, dpad
        float dz = 0.2f;
        String player = "auto";                 // auto, 1, 2
    }

    /** a game controller (each source tested whole: SOURCE_GAMEPAD and SOURCE_JOYSTICK share the button class
     *  bit with every keyboard, power key and touch screen) */
    static boolean isPad(InputDevice d) {
        if (d == null) return false;
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }
    static String nameOf(InputDevice d) { return isPad(d) ? d.getName() : KEYBOARD; }

    /** the mapping as set, or the defaults: a controller as the desktop's (its buttons 1-4 left to right: X Y A
     *  B; the earlier Buttons 1-4 setting if it was changed), a keyboard A W S D and Shift for half the stick */
    static Map get(Context c, String device) {
        Prefs.Store p = Prefs.get(c);
        Map m = new Map();
        boolean kb = KEYBOARD.equals(device);
        int[] def;
        if (kb) def = new int[] { KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_D, KeyEvent.KEYCODE_SHIFT_LEFT };
        else {
            String layout = p.getString(Prefs.BUTTONS, "xyab");
            def = new int[5];
            for (int i = 0; i < 4; i++) {
                char ch = i < layout.length() ? layout.charAt(i) : 'x';
                def[i] = ch == 'a' ? KeyEvent.KEYCODE_BUTTON_A : ch == 'b' ? KeyEvent.KEYCODE_BUTTON_B
                       : ch == 'y' ? KeyEvent.KEYCODE_BUTTON_Y : KeyEvent.KEYCODE_BUTTON_X;
            }
        }
        for (int i = 0; i < 5; i++) {
            try { m.keys[i] = Integer.parseInt(p.getString(Prefs.mapOf(device, BUTTONS[i]), "" + def[i])); } catch (NumberFormatException e) { m.keys[i] = def[i]; }
        }
        m.stick = p.getString(Prefs.mapOf(device, "stick"), kb ? "dpad" : "left");
        try { m.dz = Float.parseFloat(p.getString(Prefs.mapOf(device, "dz"), p.getString(Prefs.DEADZONE, "0.2"))); } catch (NumberFormatException e) { /* default */ }
        m.player = p.getString(Prefs.mapOf(device, "player"), "auto");
        return m;
    }

    static void reset(Context c, String device) {
        Prefs.Store.Editor e = Prefs.get(c).edit();
        for (String b : BUTTONS) e.remove(Prefs.mapOf(device, b));
        for (String k : new String[] { "stick", "dz", "player" }) e.remove(Prefs.mapOf(device, k));
        e.apply();
    }

    /** the controllers to set up: the keyboard, the connected ones, and the ones set up before */
    static List<String> devices(Context c) {
        Set<String> s = new LinkedHashSet<>();
        s.add(KEYBOARD);
        for (int id : InputDevice.getDeviceIds()) { InputDevice d = InputDevice.getDevice(id); if (isPad(d) && !d.isVirtual()) s.add(d.getName()); }
        Prefs.Store p = Prefs.get(c);
        String known = p.getString("controllers", "");
        for (String n : known.split("\n")) if (!n.isEmpty()) s.add(n);
        return new ArrayList<>(s);
    }
    static void remember(Context c, String device) {
        Prefs.Store p = Prefs.get(c);
        String known = p.getString("controllers", "");
        for (String n : known.split("\n")) if (n.equals(device)) return;
        p.edit().putString("controllers", known.isEmpty() ? device : known + "\n" + device).apply();
    }

    static String keyName(int code) {
        if (code == 0) return "none";
        String s = KeyEvent.keyCodeToString(code);
        return s.startsWith("KEYCODE_") ? s.substring(8).replace("BUTTON_", "").replace('_', ' ') : s;
    }
}
