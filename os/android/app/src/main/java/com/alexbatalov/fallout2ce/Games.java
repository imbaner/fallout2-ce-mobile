package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Properties;
import java.util.Set;

// The app's games: each one a folder of its own, as on a computer
// (files/games/<id>, the engine works in it), with the port's records of its
// saves in a private folder of its own (games/<id> in the app's private
// files). Which games there are and which one is played: the private
// "games.txt".
//
// Builds before this kept one game right in the app's folder: moved into the
// first game's folder once (renamed, nothing copied), by a journal finished at
// the next start if the app dies meanwhile.
final class Games {
    private static final String TAG = "Games";

    static final String GAMES = "games";
    private static final String REGISTRY = "games.txt";
    private static final String JOURNAL = "games-move.txt";
    private static final String LOCK = "games.lock";
    private static final String KEY_CURRENT = "current";
    private static final String KEY_GAMES = "games";
    private static final String FIRST = "1";

    // The app's own in its folder, shared by its games (not moved into one):
    // the engine's data and the marks of a run (error_report.cc).
    private static final Set<String> APP_FILES = new HashSet<>(Arrays.asList(
        "ce.dat", GAMES, "running.txt", "problem.txt"));
    // The port's records of the saves of the game kept in the app's folder.
    private static final String[] RECORDS = {
        GameSaves.RECORDS, GameSaves.COMPOSITION, GameSaves.CHARSET };

    // The port's own settings, the same for all games (game_config.cc).
    static final String PORT_SETTINGS = "f2touch.cfg";

    static final class Game {
        final String id;
        // As on a computer: the game's files, its fallout2.cfg, its saves.
        final File folder;
        // The port's records of its saves.
        final File data;

        Game(String id, File folder, File data) {
            this.id = id;
            this.folder = folder;
            this.data = data;
        }
    }

    private Games() {
    }

    // The game played now (the first one, empty until it's imported), the
    // earlier builds' game moved into its folder first.
    static Game current(Context context) throws IOException {
        File app = context.getExternalFilesDir(null);
        if (app == null) {
            throw new IOException("no app folder");
        }
        Game game = current(app, context.getFilesDir());
        Log.i(TAG, "game " + game.id);
        return game;
    }

    // [app]: the app's folder (Android/data/<app>/files), [data]: its
    // private one.
    static synchronized Game current(File app, File data) throws IOException {
        // The game runs in the app's process and the import screen in its own.
        try (RandomAccessFile lockFile = new RandomAccessFile(new File(data, LOCK), "rw");
             FileLock ignored = lockFile.getChannel().lock()) {
            Properties registry = read(new File(data, REGISTRY));
            File journal = new File(data, JOURNAL);
            if (registry == null || journal.isFile()) {
                registry = moveOldGame(app, data, journal, registry);
            }
            String id = registry.getProperty(KEY_CURRENT, FIRST);
            Game game = new Game(id, new File(new File(app, GAMES), id), new File(new File(data, GAMES), id));
            makeFolder(game.folder);
            makeFolder(game.data);
            return game;
        }
    }

    // The port's settings shared by all games.
    static File portSettings(Context context) {
        return new File(context.getFilesDir(), PORT_SETTINGS);
    }

    // The app's folder of an earlier build as the first game: everything but
    // the app's own into its folder, the records of its saves into its
    // private one. A new install gets the first game's folders empty.
    private static Properties moveOldGame(File app, File data, File journal, Properties registry) throws IOException {
        String id = journal.isFile() ? GameFiles.readText(journal).trim() : FIRST;
        if (id.isEmpty()) {
            id = FIRST;
        }
        if (!journal.isFile()) {
            writeAtomically(journal, id + "\n");
        }

        File folder = new File(new File(app, GAMES), id);
        makeFolder(folder);
        File[] entries = app.listFiles();
        if (entries != null) {
            for (File entry : entries) {
                if (APP_FILES.contains(entry.getName())) {
                    continue;
                }
                move(entry, new File(folder, entry.getName()));
            }
        }
        File gameData = new File(new File(data, GAMES), id);
        makeFolder(gameData);
        for (String name : RECORDS) {
            File record = new File(data, name);
            if (record.exists()) {
                move(record, new File(gameData, name));
            }
        }

        if (registry == null) {
            registry = new Properties();
            registry.setProperty(KEY_CURRENT, id);
            registry.setProperty(KEY_GAMES, id);
            registry.setProperty("game." + id + ".added", Long.toString(System.currentTimeMillis()));
            write(registry, new File(data, REGISTRY));
        }
        if (!journal.delete() && journal.exists()) {
            throw new IOException("journal not removed");
        }
        return registry;
    }

    // A rename in one storage. Done before (the journal finished again): the
    // source is gone.
    private static void move(File from, File to) throws IOException {
        if (to.exists()) {
            throw new IOException("already there: " + to.getName());
        }
        if (!from.renameTo(to)) {
            throw new IOException("not moved: " + from.getName());
        }
    }

    private static void makeFolder(File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("no folder " + folder.getName());
        }
    }

    private static Properties read(File file) throws IOException {
        if (!file.isFile()) {
            return null;
        }
        Properties properties = new Properties();
        try (InputStream in = new FileInputStream(file)) {
            properties.load(new InputStreamReader(in, StandardCharsets.UTF_8));
        }
        return properties;
    }

    private static void write(Properties properties, File file) throws IOException {
        File temporary = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            OutputStreamWriter writer = new OutputStreamWriter(out, StandardCharsets.UTF_8);
            properties.store(writer, "F2 Touch games (Games.java)");
            writer.flush();
            out.getFD().sync();
        }
        if (!temporary.renameTo(file)) {
            throw new IOException("not written: " + file.getName());
        }
    }

    private static void writeAtomically(File file, String text) throws IOException {
        File temporary = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!temporary.renameTo(file)) {
            throw new IOException("not written: " + file.getName());
        }
    }
}
