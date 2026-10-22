package io.github.hugonl31.vectrexemu;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** The flash cartridges' saved state (Vectorblade's high scores and settings): as on the desktop a file next
 *  to the ROM, its name the ROM's with .sav added ("Vectorblade.bin.sav"), so both programs share it. The core
 *  writes a copy in the app's own storage (a second after the game wrote its flash, and at the end); it is
 *  copied next to the ROM when the game ends - or, when a front end started the next game without the last
 *  one ending (the emulator stopped from outside), at that next start. A ROM folder granted read-only (or a
 *  ROM from a front end's content:// URI): the save stays in the app's storage. */
final class Saves {
    static final String TAG = "VectrexEMU";

    /** where a game's save lives next to its ROM: a granted folder's document, or a directory */
    static final class Home {
        Uri tree; String parentDoc; File dir; String name;
        String encode() { return tree != null ? "t|" + tree + "|" + parentDoc + "|" + name : "d|" + dir.getPath() + "|" + name; }
        static Home decode(String s) {
            String[] f = s.split("\\|", -1);
            Home h = new Home();
            if (f[0].equals("t") && f.length == 4) { h.tree = Uri.parse(f[1]); h.parentDoc = f[2]; h.name = f[3]; }
            else if (f[0].equals("d") && f.length == 3) { h.dir = new File(f[1]); h.name = f[2]; }
            else return null;
            return h;
        }
    }

    static File app(Context c, String romName) {
        File d = new File(c.getFilesDir(), "saves"); d.mkdirs();
        return new File(d, romName.replaceAll("[/\\\\:*?\"<>|]", "_") + ".sav");
    }

    /** before the game: the save next to the ROM, if there is one, into the app's storage (it wins: the
     *  desktop may have played since); an older app-only save under the game's plain name is taken over */
    static void before(Context c, Home h, File app, String game) {
        File old = new File(app.getParentFile(), game.replaceAll("[/\\\\:*?\"<>|]", "_") + ".sav");
        if (!app.exists() && old.exists() && !old.equals(app)) old.renameTo(app);
        if (h == null) return;
        try (InputStream in = open(c, h)) {
            if (in != null) {
                try (OutputStream out = new FileOutputStream(app)) { copy(in, out); }
                Log.i(TAG, "save: " + h.name + " from the ROM's folder");
            }
        } catch (IOException | RuntimeException e) { /* none there */ }
        // (where it goes, kept either way: a first save goes there too, also when the game is ended from outside)
        Prefs.get(c).edit().putString("sav_home/" + app.getName(), h.encode()).apply();
    }

    /** after the game (or at the next start): the app's copy next to the ROM */
    static void after(Context c, Home h, File app) {
        if (h == null || !app.exists()) return;
        try (InputStream in = new FileInputStream(app); OutputStream out = create(c, h)) {
            if (out == null) return;
            copy(in, out);
            Log.i(TAG, "save: " + h.name + " written next to the ROM");
        } catch (IOException | RuntimeException e) {
            Log.i(TAG, "save: " + h.name + " stays in the app (" + e.getMessage() + ")");
        }
    }

    /** every game's save to its ROM's folder (at each start: games the front end ended from outside) */
    static void syncAll(Context c) {
        File d = new File(c.getFilesDir(), "saves");
        File[] fs = d.listFiles();
        if (fs == null) return;
        Prefs.Store p = Prefs.get(c);
        for (File f : fs) {
            String s = p.getString("sav_home/" + f.getName(), null);
            Home h = s != null ? Home.decode(s) : null;
            if (h != null && f.lastModified() > p.getInt("sav_synced/" + f.getName(), 0) * 1000L) {
                after(c, h, f);
                p.edit().putInt("sav_synced/" + f.getName(), (int) (System.currentTimeMillis() / 1000)).apply();
            }
        }
    }

    private static InputStream open(Context c, Home h) throws IOException {
        if (h.dir != null) { File f = new File(h.dir, h.name); return f.exists() ? new FileInputStream(f) : null; }
        String doc = child(c.getContentResolver(), h);
        return doc == null ? null : c.getContentResolver().openInputStream(DocumentsContract.buildDocumentUriUsingTree(h.tree, doc));
    }
    private static OutputStream create(Context c, Home h) throws IOException {
        if (h.dir != null) return new FileOutputStream(new File(h.dir, h.name));
        ContentResolver cr = c.getContentResolver();
        String doc = child(cr, h);
        Uri u = doc != null ? DocumentsContract.buildDocumentUriUsingTree(h.tree, doc)
              : DocumentsContract.createDocument(cr, DocumentsContract.buildDocumentUriUsingTree(h.tree, h.parentDoc), "application/octet-stream", h.name);
        return u == null ? null : cr.openOutputStream(u, "wt");
    }
    private static String child(ContentResolver cr, Home h) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(h.tree, h.parentDoc);
        try (Cursor cu = cr.query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            if (cu != null) while (cu.moveToNext()) if (h.name.equals(cu.getString(1))) return cu.getString(0);
        } catch (RuntimeException e) { /* not listable */ }
        return null;
    }
    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] b = new byte[65536]; int n;
        while ((n = in.read(b)) > 0) out.write(b, 0, n);
    }
}
