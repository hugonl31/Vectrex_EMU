package io.github.hugonl31.vectrexemu;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.zip.CRC32;

/** The screen overlays: MiSTer .art files or PNGs named like the ROM (src/frontend/vectrex_emu.py
 *  find_overlay, vart.py). Looked for in the overlay folder granted in the settings, next to the ROM (and in
 *  an "overlays" folder there), and among the app's own (Mine Storm's). */
final class Overlays {
    /** an overlay: RGBA rows top first */
    static final class Art { int w, h; byte[] rgba; String name; }

    /** a file that may be an overlay: its name and how to open it */
    static final class Cand {
        final String name; final Uri uri; final File file; final String asset;
        Cand(String name, Uri uri, File file, String asset) { this.name = name; this.uri = uri; this.file = file; this.asset = asset; }
        InputStream open(Context c) throws IOException {
            if (uri != null) return c.getContentResolver().openInputStream(uri);
            if (file != null) return new FileInputStream(file);
            return c.getAssets().open(asset);
        }
    }

    static final Map<String, String> ALIAS = new HashMap<>();
    static { ALIAS.put("mr. boston clean sweep", "Clean Sweep"); }        // (the same game under its European name)

    private static final Pattern BASE = Pattern.compile(
            "\\s*[\\(\\[][^\\)\\]]*[\\)\\]]\\s*$|\\s+v\\d+(\\.\\d+)*[a-z]?$|\\s+proto$|(?<=[a-z])v\\d+(\\.\\d+)+$", Pattern.CASE_INSENSITIVE);
    private static final Pattern ROTATED = Pattern.compile("_90c?cw$", Pattern.CASE_INSENSITIVE);
    private static final Pattern HACK = Pattern.compile("hack|_\\(", Pattern.CASE_INSENSITIVE);

    /** 'Berzerk (Hack - Speed, 2016)' -> 'Berzerk', 'Thrust v1.2 (Hack, 2019)' -> 'Thrust', 'VectorbladeV1.13' -> 'Vectorblade' */
    static String base(String s) {
        while (true) {
            String t = BASE.matcher(s).replaceAll("");
            if (t.equals(s)) return s.trim();
            s = t;
        }
    }
    static String stem(String file) {
        String s = file; int k = s.lastIndexOf('.'); if (k > 0) s = s.substring(0, k);
        return s.replace('_', ' ').replaceAll("\\s+", " ").trim();
    }
    static String norm(String s) { return s.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]", ""); }

    static boolean isOverlay(String n) {
        String l = n.toLowerCase(Locale.ROOT);
        if (!(l.endsWith(".art") || l.endsWith(".png"))) return false;
        String s = n.substring(0, n.lastIndexOf('.'));
        return !ROTATED.matcher(s).find();
    }

    /** the overlay for a ROM name: the same name, else the same base name, else the same letters and digits */
    static Cand find(List<Cand> files, String name) {
        List<Cand> fs = new ArrayList<>(files);
        Collections.sort(fs, (a, b) -> {             // (the plain one first - no hack - then the shortest name)
            int ha = HACK.matcher(a.name).find() ? 1 : 0, hb = HACK.matcher(b.name).find() ? 1 : 0;
            if (ha != hb) return ha - hb;
            if (a.name.length() != b.name.length()) return a.name.length() - b.name.length();
            return a.name.toLowerCase(Locale.ROOT).compareTo(b.name.toLowerCase(Locale.ROOT));
        });
        String al = ALIAS.get(base(name).toLowerCase(Locale.ROOT));
        if (al != null) name = al;
        String nl = name.toLowerCase(Locale.ROOT), bl = base(name).toLowerCase(Locale.ROOT), bn = norm(base(name));
        for (int test = 0; test < 4; test++)
            for (Cand c : fs) {
                String st = stem(c.name);
                boolean hit = test == 0 ? st.toLowerCase(Locale.ROOT).equals(nl)
                        : test == 1 ? st.toLowerCase(Locale.ROOT).equals(bl)
                        : test == 2 ? base(st).toLowerCase(Locale.ROOT).equals(bl)
                        : norm(base(st)).equals(bn);
                if (hit) return c;
            }
        return null;
    }

    // ---------------------------------------------------------------- where to look
    static void fromTree(ContentResolver cr, Uri tree, String dirDoc, List<Cand> out, boolean intoOverlays) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dirDoc);
        try (Cursor c = cr.query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null)) {
            if (c == null) return;
            String sub = null;
            while (c.moveToNext()) {
                String id = c.getString(0), dn = c.getString(1), mt = c.getString(2);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mt)) { if (intoOverlays && "overlays".equalsIgnoreCase(dn)) sub = id; }
                else if (dn != null && isOverlay(dn)) out.add(new Cand(dn, DocumentsContract.buildDocumentUriUsingTree(tree, id), null, null));
            }
            if (sub != null) fromTree(cr, tree, sub, out, false);
        } catch (RuntimeException e) { /* (not listable) */ }
    }
    static void fromDir(File d, List<Cand> out, boolean intoOverlays) {
        File[] fs = d == null ? null : d.listFiles();
        if (fs == null) return;
        for (File f : fs) {
            if (f.isDirectory()) { if (intoOverlays && f.getName().equalsIgnoreCase("overlays")) fromDir(f, out, false); }
            else if (isOverlay(f.getName())) out.add(new Cand(f.getName(), null, f, null));
        }
    }

    /** every overlay candidate for this ROM (null: Mine Storm, no cartridge) */
    static List<Cand> candidates(Context ctx, RomSource.Rom rom) {
        List<Cand> out = new ArrayList<>();
        String t = Prefs.get(ctx).getString(Prefs.OVERLAY_TREE, null);
        if (t != null) {
            Uri tree = Uri.parse(t);
            try { fromTree(ctx.getContentResolver(), tree, DocumentsContract.getTreeDocumentId(tree), out, true); } catch (RuntimeException e) { /* gone */ }
        }
        if (rom != null && rom.tree != null && rom.parentDoc != null) fromTree(ctx.getContentResolver(), rom.tree, rom.parentDoc, out, true);
        if (rom != null && rom.dir != null) fromDir(rom.dir, out, true);
        try {
            String[] as = ctx.getAssets().list("overlays");
            if (as != null) for (String a : as) if (isOverlay(a)) out.add(new Cand(a, null, null, "overlays/" + a));
        } catch (IOException e) { /* none */ }
        return out;
    }

    static Art load(Context ctx, Cand c) {
        try (InputStream is = c.open(ctx)) {
            byte[] b = RomSource.all(is);
            Art a = c.name.toLowerCase(Locale.ROOT).endsWith(".png") ? png(b) : vart(b);
            if (a != null) a.name = c.name;
            return a;
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    // ---------------------------------------------------------------- PNG (alpha: 255 printed, 1-254 filter, 0 clear;
    // without alpha: black clear, the rest a colour filter, 51)
    static Art png(byte[] b) {
        BitmapFactory.Options o = new BitmapFactory.Options();
        o.inPremultiplied = false; o.inPreferredConfig = Bitmap.Config.ARGB_8888;
        Bitmap bm = BitmapFactory.decodeByteArray(b, 0, b.length, o);
        if (bm == null) return null;
        Art a = new Art(); a.w = bm.getWidth(); a.h = bm.getHeight();
        int[] px = new int[a.w * a.h]; bm.getPixels(px, 0, a.w, 0, 0, a.w, a.h);
        boolean alpha = bm.hasAlpha();
        a.rgba = new byte[a.w * a.h * 4];
        for (int i = 0; i < px.length; i++) {
            int p = px[i], r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255, al = (p >>> 24);
            if (!alpha) al = (r | g | bl) == 0 ? 0 : 51;
            a.rgba[i * 4] = (byte) r; a.rgba[i * 4 + 1] = (byte) g; a.rgba[i * 4 + 2] = (byte) bl; a.rgba[i * 4 + 3] = (byte) al;
        }
        bm.recycle();
        return a;
    }

    // ---------------------------------------------------------------- MiSTer VART v1 (vart.py): the largest plane
    static Art vart(byte[] c) {
        if (c.length < 64 || c[0] != 'V' || c[1] != 'A' || c[2] != 'R' || c[3] != 'T' || c[4] != 1) return null;
        ByteBuffer bb = ByteBuffer.wrap(c).order(ByteOrder.LITTLE_ENDIAN);
        int planes = c[7] & 255;
        long total = bb.getInt(8) & 0xffffffffL, crc = bb.getInt(12) & 0xffffffffL;
        int doff = bb.getInt(16), dsize = bb.getShort(20) & 0xffff;
        if (total != c.length || doff != 64 || dsize != 48 || planes == 0) return null;
        CRC32 cr = new CRC32(); cr.update(c, doff, c.length - doff);
        if (cr.getValue() != crc) return null;
        int best = -1, bw = 0, bh = 0;
        for (int i = 0; i < planes; i++) {
            int o = doff + i * 48, w = bb.getShort(o) & 0xffff, h = bb.getShort(o + 2) & 0xffff;
            if (best < 0 || w * h > bw * bh) { best = o; bw = w; bh = h; }
        }
        int bits = c[best + 6] & 255;
        int palOff = bb.getInt(best + 12), palSize = bb.getInt(best + 16), payOff = bb.getInt(best + 20);
        if ((bits != 4 && bits != 6 && bits != 8) || palSize != (1 << bits) * 4) return null;
        Art a = new Art(); a.w = bw; a.h = bh; a.rgba = new byte[bw * bh * 4];
        int[] prev = null, row = new int[bw];
        int off = payOff, mask = (1 << bits) - 1;
        for (int y = 0; y < bh; y++) {
            int nbits = bb.getShort(off) & 0xffff;
            int size = (2 + (nbits + 7) / 8 + 7) / 8 * 8;
            // packets: literals, a run, or a copy of the row above
            long pos = 0; int x = 0, base = off + 2;
            while (x < bw) {
                if (pos + 8 > nbits) return null;
                int head = bitsAt(c, base, pos, 8); pos += 8;
                if ((head & 0x80) == 0) {
                    for (int k = 0, n = (head & 0x7f) + 1; k < n && x < bw; k++) { row[x++] = bitsAt(c, base, pos, bits) & mask; pos += bits; }
                } else if ((head & 0x40) == 0) {
                    int v = bitsAt(c, base, pos, bits) & mask; pos += bits;
                    for (int k = 0, n = (head & 0x3f) + 1; k < n && x < bw; k++) row[x++] = v;
                } else {
                    if (prev == null) return null;
                    for (int k = 0, n = (head & 0x3f) + 1; k < n && x < bw; k++, x++) row[x] = prev[x];
                }
            }
            if (pos != nbits) return null;
            for (int i = 0; i < bw; i++) System.arraycopy(c, palOff + row[i] * 4, a.rgba, (y * bw + i) * 4, 4);
            int[] t = prev == null ? new int[bw] : prev; prev = row; row = t;
            off += size;
        }
        return a;
    }

    /** n (<= 8) bits starting at bit pos of the little-endian bit stream at base */
    private static int bitsAt(byte[] c, int base, long pos, int n) {
        int i = base + (int) (pos >> 3), sh = (int) (pos & 7);
        int v = (c[i] & 255) | (i + 1 < c.length ? (c[i + 1] & 255) << 8 : 0);
        return (v >> sh) & ((1 << n) - 1);
    }
}
