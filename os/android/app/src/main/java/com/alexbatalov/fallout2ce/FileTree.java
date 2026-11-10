package com.alexbatalov.fallout2ce;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

// A game's folder copied, counted, deleted with its files' names as the bytes
// they are (src/file_tree.cc in the import library): Java reads names as
// UTF-8, and one in another encoding (cp1251 of a game copied from Windows)
// it can't open again - a move or a deletion of the game would stop on it.
// Without the library (the computer's unit tests) Java's own walk.
final class FileTree {
    private static final String TAG = "FileTree";
    private static final boolean NATIVE = load();

    private FileTree() {
    }

    interface Progress {
        // The bytes copied so far; an exception stops the copy.
        void update(long copied) throws IOException;
    }

    private static boolean load() {
        try {
            System.loadLibrary("fallout2-import");
            return true;
        } catch (UnsatisfiedLinkError | SecurityException e) {
            return false;
        }
    }

    // The bytes of the files under [file] (a file: its size); what can't be
    // read counts as nothing.
    static long size(File file) {
        if (NATIVE) {
            try {
                return nativeSize(file.getPath());
            } catch (IOException e) {
                Log.w(TAG, "size: " + e.getMessage());
            }
        }
        return javaSize(file);
    }

    // [from] (a folder or a file) copied to [to]: each file written to the
    // storage and checked for its size, times kept. What was copied before a
    // failure stays (the caller removes it).
    static void copy(File from, File to, Progress progress) throws IOException {
        if (NATIVE) {
            nativeCopy(from.getPath(), to.getPath(), progress);
            return;
        }
        javaCopy(from, to, new long[] { 0 }, progress);
    }

    // [file] and everything under it removed. False: something stayed.
    static boolean delete(File file) {
        if (NATIVE) {
            try {
                nativeDelete(file.getPath());
                return true;
            } catch (IOException e) {
                Log.w(TAG, "delete: " + e.getMessage());
                return false;
            }
        }
        return javaDelete(file);
    }

    private static long javaSize(File file) {
        if (file.isFile()) {
            return file.length();
        }
        long total = 0;
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                total += javaSize(child);
            }
        }
        return total;
    }

    private static void javaCopy(File source, File target, long[] copied, Progress progress) throws IOException {
        if (source.isDirectory()) {
            if (!target.isDirectory() && !target.mkdirs()) {
                throw new IOException("no folder: " + target.getName());
            }
            File[] children = source.listFiles();
            if (children == null) {
                throw new IOException("not listed: " + source.getName());
            }
            for (File child : children) {
                javaCopy(child, new File(target, child.getName()), copied, progress);
            }
            return;
        }
        byte[] buffer = new byte[1 << 20];
        try (InputStream in = new FileInputStream(source); FileOutputStream out = new FileOutputStream(target)) {
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                copied[0] += read;
                if (progress != null) {
                    progress.update(copied[0]);
                }
            }
            out.getFD().sync();
        }
        //noinspection ResultOfMethodCallIgnored
        target.setLastModified(source.lastModified());
        if (target.length() != source.length()) {
            throw new IOException("not copied whole: " + source.getName());
        }
    }

    private static boolean javaDelete(File file) {
        File[] children = file.isDirectory() ? file.listFiles() : null;
        boolean all = true;
        if (children != null) {
            for (File child : children) {
                all &= javaDelete(child);
            }
        }
        return (file.delete() || !file.exists()) && all;
    }

    private static native long nativeSize(String path) throws IOException;

    private static native void nativeCopy(String from, String to, Progress progress) throws IOException;

    private static native void nativeDelete(String path) throws IOException;
}
