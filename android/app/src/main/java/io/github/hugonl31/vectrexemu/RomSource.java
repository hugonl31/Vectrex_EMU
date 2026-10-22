package io.github.hugonl31.vectrexemu;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Finds the game a front end asks for, in whatever form it comes:
 *  - a VIEW intent's data: content://... (read through the provider), or file:///path
 *  - a path or URI in an extra (ROM, rom, path, file, ... - see EXTRAS), or any extra ending in a ROM name
 *  A plain path is read directly when Android allows it; otherwise (Android 11 and later, without the
 *  All files access permission this app does not ask for) it is found in a folder the user granted once
 *  in the settings (Storage Access Framework): mapped onto it by its path, else looked up by name.
 *  Zips are opened: the first ROM inside. */
final class RomSource {
    static final String[] EXTRAS = { "ROM", "rom", "ROM_PATH", "rom_path", "romPath", "RomPath", "path", "PATH", "Path",
            "file", "FILE", "filePath", "file_path", "filename", "uri", "URI", "game", "GAME", "LaunchPath", "args" };
    static final String[] ROM_EXT = { ".bin", ".vec", ".gam", ".rom", ".zip" };

    /** a game found: its bytes, its name (for the overlay) and where it lies (for overlays next to it) */
    static final class Rom {
        byte[] data; String name; String source;
        Uri tree; String parentDoc;              // (found through a granted folder: that folder's document)
        File dir;                                // (read as a file: its folder)
    }

    static final class Failure extends Exception { Failure(String m) { super(m); } }

    /** the candidates from the intent, in order */
    static List<String> candidates(Intent in) {
        List<String> c = new ArrayList<>();
        if (in == null) return c;
        if (in.getData() != null) c.add(in.getData().toString());
        Bundle ex = in.getExtras();
        if (ex != null) {
            for (String k : EXTRAS) {
                Object v = ex.get(k);
                if (v instanceof String && !((String) v).isEmpty()) c.add((String) v);
                else if (v instanceof Uri) c.add(v.toString());
            }
            for (String k : ex.keySet()) {               // (any other extra that names a ROM file)
                Object v = ex.get(k);
                if (v instanceof String && hasRomExt((String) v) && !c.contains(v)) c.add((String) v);
            }
        }
        if (in.getClipData() != null && in.getClipData().getItemCount() > 0 && in.getClipData().getItemAt(0).getUri() != null)
            c.add(in.getClipData().getItemAt(0).getUri().toString());
        return c;
    }

    /** the game's own name, without a front end's mark on its copy: HyperSpin hands over a copy of a game it
     *  fetched from the network named "hsfetmp_<hex>_" + the name (the overlay and the save go by the name) */
    static String realName(String n) {
        return n == null ? null : n.replaceFirst("^hsfetmp_[0-9a-fA-F]+_", "");
    }

    static boolean hasRomExt(String s) {
        String l = s.toLowerCase(Locale.ROOT);
        for (String e : ROM_EXT) if (l.endsWith(e)) return true;
        return false;
    }

    static Rom resolve(Context ctx, Intent in) throws Failure {
        List<String> cs = candidates(in);
        if (cs.isEmpty()) return null;                   // no game: the Vectrex's own, Mine Storm
        StringBuilder why = new StringBuilder();
        for (String c : cs) {
            try {
                Rom r = open(ctx, c.trim());
                if (r != null) { r.name = realName(r.name); return r; }
            } catch (IOException | SecurityException e) {
                why.append(c).append(": ").append(e.getMessage()).append('\n');
            }
        }
        throw new Failure("Cannot read the game.\n\n" + why
                + "\nIf the front end passes a file path: open Vectrex_EMU's settings and choose your ROM folder"
                + " (Choose ROM folder), once.");
    }

    static Rom open(Context ctx, String s) throws IOException {
        ContentResolver cr = ctx.getContentResolver();
        if (s.startsWith("content://")) {
            Uri u = Uri.parse(s);
            Rom r = new Rom();
            r.name = displayName(cr, u);
            r.source = s;
            try (InputStream is = cr.openInputStream(u)) { r.data = read(is, r.name); }
            if (DocumentsContract.isDocumentUri(ctx, u) && u.getPathSegments().size() >= 4 && "tree".equals(u.getPathSegments().get(0))) {
                String doc = DocumentsContract.getDocumentId(u);          // (a document inside a granted tree:
                int k = doc.lastIndexOf('/');                             //  its folder, for the overlays)
                if (k > 0) { r.tree = DocumentsContract.buildTreeDocumentUri(u.getAuthority(), DocumentsContract.getTreeDocumentId(u)); r.parentDoc = doc.substring(0, k); }
            }
            return r;
        }
        String path = s.startsWith("file://") ? Uri.parse(s).getPath() : s;
        if (path == null || !path.startsWith("/")) throw new IOException("not a path or URI");
        path = normalize(path);
        File f = new File(path);
        IOException direct;
        try (InputStream is = new FileInputStream(f)) {                   // 1. the file itself, if readable
            Rom r = new Rom();
            r.name = f.getName(); r.source = path; r.dir = f.getParentFile();
            r.data = read(is, r.name);
            return r;
        } catch (IOException | SecurityException e) {
            direct = e instanceof IOException ? (IOException) e : new IOException(e.getMessage());
        }
        for (UriPermission p : cr.getPersistedUriPermissions()) {         // 2. through a granted folder
            if (!p.isReadPermission()) continue;
            Uri tree = p.getUri();
            String treeId;
            try { treeId = DocumentsContract.getTreeDocumentId(tree); } catch (IllegalArgumentException e) { continue; }
            String base = treePath(treeId);
            if (base != null && path.startsWith(base + "/")) {
                String rel = path.substring(base.length() + 1);
                String doc = treeId.endsWith(":") ? treeId + rel : treeId + "/" + rel;
                Rom r = fromDoc(ctx, tree, doc, f.getName(), path);
                if (r != null) return r;
            }
        }
        for (UriPermission p : cr.getPersistedUriPermissions()) {         // 3. by its name, in any granted folder
            if (!p.isReadPermission()) continue;
            Uri tree = p.getUri();
            String doc = findByName(cr, tree, f.getName());
            if (doc != null) { Rom r = fromDoc(ctx, tree, doc, f.getName(), path); if (r != null) return r; }
        }
        throw new IOException((direct.getMessage() != null ? direct.getMessage() : "not readable") + " (and not in a granted folder)");
    }

    static Rom fromDoc(Context ctx, Uri tree, String doc, String name, String source) {
        Uri u = DocumentsContract.buildDocumentUriUsingTree(tree, doc);
        try (InputStream is = ctx.getContentResolver().openInputStream(u)) {
            if (is == null) return null;
            Rom r = new Rom();
            r.name = name; r.source = source; r.data = read(is, name); r.tree = tree;
            int k = doc.lastIndexOf('/');
            r.parentDoc = k > 0 ? doc.substring(0, k) : DocumentsContract.getTreeDocumentId(tree);
            return r;
        } catch (IOException | SecurityException | IllegalArgumentException e) {
            return null;
        }
    }

    /** /sdcard, /mnt/sdcard, /storage/self/primary -> /storage/emulated/0 */
    static String normalize(String p) {
        for (String a : new String[] { "/sdcard", "/mnt/sdcard", "/storage/self/primary", "/mnt/user/0/primary" })
            if (p.equals(a) || p.startsWith(a + "/")) return "/storage/emulated/0" + p.substring(a.length());
        return p;
    }

    /** a tree's folder as a path, for the external storage provider's ids ("primary:Games/Vectrex",
     *  "1234-ABCD:ROMs"); null for other providers */
    static String treePath(String treeId) {
        int k = treeId.indexOf(':');
        if (k < 0) return null;
        String vol = treeId.substring(0, k), rel = treeId.substring(k + 1);
        String root = vol.equals("primary") ? "/storage/emulated/0" : vol.equals("home") ? "/storage/emulated/0/Documents" : "/storage/" + vol;
        return rel.isEmpty() ? root : root + "/" + rel;
    }

    /** a file by name anywhere in a granted folder (breadth first, at most a few thousand entries) */
    static String findByName(ContentResolver cr, Uri tree, String name) {
        ArrayDeque<String> todo = new ArrayDeque<>();
        try { todo.add(DocumentsContract.getTreeDocumentId(tree)); } catch (IllegalArgumentException e) { return null; }
        int seen = 0;
        while (!todo.isEmpty() && seen < 5000) {
            String dir = todo.poll();
            Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dir);
            try (Cursor c = cr.query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null)) {
                if (c == null) continue;
                while (c.moveToNext()) {
                    seen++;
                    String id = c.getString(0), dn = c.getString(1), mt = c.getString(2);
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mt)) todo.add(id);
                    else if (name.equalsIgnoreCase(dn)) return id;
                }
            } catch (RuntimeException e) { /* (a folder that cannot be listed) */ }
        }
        return null;
    }

    static String displayName(ContentResolver cr, Uri u) {
        try (Cursor c = cr.query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) return c.getString(0);
        } catch (RuntimeException e) { /* (some providers have no name) */ }
        String last = u.getLastPathSegment();
        if (last == null) return "game.bin";
        int k = last.lastIndexOf('/');
        return k >= 0 ? last.substring(k + 1) : last;
    }

    /** the ROM's bytes (at most 1 MB, the largest cartridge); a zip: its first ROM */
    static byte[] read(InputStream is, String name) throws IOException {
        if (is == null) throw new IOException("no data");
        if (name != null && name.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            try (ZipInputStream z = new ZipInputStream(is)) {
                ZipEntry e;
                while ((e = z.getNextEntry()) != null)
                    if (!e.isDirectory() && hasRomExt(e.getName()) && !e.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) return all(z);
            }
            throw new IOException("no ROM in the zip");
        }
        return all(is);
    }

    static byte[] all(InputStream is) throws IOException { return all(is, 1 << 20, "larger than 1 MB: not a Vectrex ROM"); }
    static byte[] all(InputStream is, int max, String tooLarge) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[65536]; int n;
        while ((n = is.read(b)) > 0) { o.write(b, 0, n); if (o.size() > max) throw new IOException(tooLarge); }
        return o.toByteArray();
    }

    /** the game's name without its extension (for the overlay) */
    static String stem(String name) {
        if (name == null) return "";
        int k = name.lastIndexOf('.');
        return k > 0 ? name.substring(0, k) : name;
    }
}
