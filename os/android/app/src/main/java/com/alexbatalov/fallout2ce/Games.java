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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
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
    private static final String STARTING = "starting.txt";
    // Folders of a deleted game while they're deleted.
    private static final String TRASH = ".trash-";
    // A game's card for the list (game_card.cc), in its private folder.
    private static final String CARD_TEXT = "card.txt";
    private static final String CARD_PICTURE = "card.png";

    // The app's own in its folder, shared by its games (not moved into one):
    // the engine's data and the marks of a run (error_report.cc).
    private static final Set<String> APP_FILES = new HashSet<>(Arrays.asList(
        "ce.dat", GAMES, "running.txt", "problem.txt", STARTING));
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
            Properties registry = registry(app, data);
            String id = registry.getProperty(KEY_CURRENT, FIRST);
            Game game = game(app, data, id);
            makeFolder(game.folder);
            makeFolder(game.data);
            return game;
        }
    }

    // The port's settings shared by all games.
    static File portSettings(Context context) {
        return new File(context.getFilesDir(), PORT_SETTINGS);
    }

    // The mark of a start under way, in the app's folder (game_card.cc):
    // the engine removes it at its main menu or in play. Still there at the
    // next start: that game failed at its start, the list of games opens.
    static File startingMark(Context context) {
        return new File(context.getExternalFilesDir(null), STARTING);
    }

    // A game of the list: its folders and what the list shows of it.
    static final class Info {
        final Game game;
        // As the player named it, empty: not named (`displayName`).
        final String name;
        final long added;
        // Last started, 0: never.
        final long played;

        Info(Game game, String name, long added, long played) {
            this.game = game;
            this.name = name;
            this.added = added;
            this.played = played;
        }
    }

    // The games there are (installed: an import cut short or under way
    // isn't one), the one played last first.
    static List<Info> list(Context context) throws IOException {
        File app = appFolder(context);
        File data = context.getFilesDir();
        List<Info> infos = new ArrayList<>();
        synchronized (Games.class) {
            try (RandomAccessFile lockFile = new RandomAccessFile(new File(data, LOCK), "rw");
                 FileLock ignored = lockFile.getChannel().lock()) {
                Properties registry = registry(app, data);
                for (String id : ids(registry)) {
                    Game game = game(app, data, id);
                    if (!GameImport.isInstalled(game.folder)) {
                        continue;
                    }
                    infos.add(new Info(game, registry.getProperty(key(id, "name"), ""),
                        number(registry, key(id, "added")), number(registry, key(id, "played"))));
                }
            }
        }
        Collections.sort(infos, (a, b) -> a.played != b.played ? Long.compare(b.played, a.played) : Long.compare(b.added, a.added));
        return infos;
    }

    // The game of [id] (its folders, made when missing).
    static Game get(Context context, String id) throws IOException {
        Game game = game(appFolder(context), context.getFilesDir(), id);
        makeFolder(game.folder);
        makeFolder(game.data);
        return game;
    }

    // A new game's folders, empty: the import fills them, `add` lists it.
    static Game create(Context context) throws IOException {
        return change(context, (app, data, registry) -> {
            int next = 1;
            for (String id : ids(registry)) {
                try {
                    next = Math.max(next, Integer.parseInt(id) + 1);
                } catch (NumberFormatException ignored) {
                    // Not a number: not one of ours to count.
                }
            }
            // Not over a folder an earlier attempt left.
            while (new File(new File(app, GAMES), Integer.toString(next)).exists()
                || new File(new File(data, GAMES), Integer.toString(next)).exists()) {
                next++;
            }
            String id = Integer.toString(next);
            List<String> ids = ids(registry);
            ids.add(id);
            registry.setProperty(KEY_GAMES, String.join(",", ids));
            registry.setProperty(key(id, "added"), Long.toString(System.currentTimeMillis()));
            Game game = game(app, data, id);
            makeFolder(game.folder);
            makeFolder(game.data);
            return game;
        });
    }

    // The game of [id] imported: named [name] and the one played.
    static void add(Context context, String id, String name) throws IOException {
        change(context, (app, data, registry) -> {
            if (name != null && !name.trim().isEmpty() && registry.getProperty(key(id, "name"), "").isEmpty()) {
                registry.setProperty(key(id, "name"), uniqueName(registry, id, name.trim()));
            }
            registry.setProperty(KEY_CURRENT, id);
            return null;
        });
    }

    // The game played from now on (its start counted as played).
    static void setCurrent(Context context, String id) throws IOException {
        change(context, (app, data, registry) -> {
            registry.setProperty(KEY_CURRENT, id);
            registry.setProperty(key(id, "played"), Long.toString(System.currentTimeMillis()));
            return null;
        });
    }

    static void rename(Context context, String id, String name) throws IOException {
        change(context, (app, data, registry) -> {
            String trimmed = name.trim();
            if (trimmed.isEmpty()) {
                registry.remove(key(id, "name"));
            } else {
                registry.setProperty(key(id, "name"), uniqueName(registry, id, trimmed));
            }
            return null;
        });
    }

    // The game of [id] gone from the list at once, its folders deleted in the
    // background (renamed away first: nothing half deleted is ever seen as a
    // game). The one played: the one played last of the others, else a new
    // empty one (the import's).
    static void delete(Context context, String id) throws IOException {
        File trash = change(context, (app, data, registry) -> {
            List<String> ids = ids(registry);
            ids.remove(id);
            registry.setProperty(KEY_GAMES, String.join(",", ids));
            for (String field : new String[] { "name", "added", "played" }) {
                registry.remove(key(id, field));
            }
            Game game = game(app, data, id);
            File folder = new File(new File(app, GAMES), TRASH + id + "-" + System.currentTimeMillis());
            if (game.folder.exists() && !game.folder.renameTo(folder)) {
                throw new IOException("not moved away: " + id);
            }
            File dataFolder = new File(new File(data, GAMES), TRASH + id + "-" + System.currentTimeMillis());
            if (game.data.exists() && !game.data.renameTo(dataFolder)) {
                throw new IOException("records not moved away: " + id);
            }
            if (id.equals(registry.getProperty(KEY_CURRENT))) {
                String next = null;
                long latest = -1;
                for (String other : ids) {
                    long played = Math.max(number(registry, key(other, "played")), number(registry, key(other, "added")));
                    if (GameImport.isInstalled(game(app, data, other).folder) && played > latest) {
                        latest = played;
                        next = other;
                    }
                }
                if (next == null) {
                    next = ids.isEmpty() ? FIRST : ids.get(0);
                    if (next.equals(id)) {
                        next = FIRST;
                    }
                }
                registry.setProperty(KEY_CURRENT, next);
            }
            return new File(app, GAMES);
        });
        emptyTrash(trash, new File(context.getFilesDir(), GAMES));
    }

    // Imports given up (folders no game was made of), and the trash.
    static void tidy(Context context) throws IOException {
        File games = change(context, (app, data, registry) -> {
            List<String> ids = ids(registry);
            String current = registry.getProperty(KEY_CURRENT, FIRST);
            List<String> kept = new ArrayList<>();
            for (String id : ids) {
                Game game = game(app, data, id);
                if (id.equals(current) || GameImport.isInstalled(game.folder)) {
                    kept.add(id);
                    continue;
                }
                registry.remove(key(id, "added"));
                registry.remove(key(id, "name"));
                registry.remove(key(id, "played"));
                File away = new File(new File(app, GAMES), TRASH + id + "-" + System.currentTimeMillis());
                if (game.folder.exists() && !game.folder.renameTo(away)) {
                    throw new IOException("not moved away: " + id);
                }
                File dataAway = new File(new File(data, GAMES), TRASH + id + "-" + System.currentTimeMillis());
                if (game.data.exists() && !game.data.renameTo(dataAway)) {
                    throw new IOException("records not moved away: " + id);
                }
            }
            registry.setProperty(KEY_GAMES, String.join(",", kept));
            return new File(app, GAMES);
        });
        emptyTrash(games, new File(context.getFilesDir(), GAMES));
    }

    // The name the list shows: the player's, else the game's version line
    // (its card, once it ran), else "Game <id>".
    static String displayName(Context context, Info info) {
        if (!info.name.isEmpty()) {
            return info.name;
        }
        String version = card(info.game).get("version");
        if (version != null && !version.isEmpty()) {
            return version;
        }
        return context.getString(R.string.games_unnamed, info.game.id);
    }

    // What the game wrote of itself for the list (game_card.cc): "version",
    // "place" of its newest save. Empty before it ran.
    static Map<String, String> card(Game game) {
        Map<String, String> values = new HashMap<>();
        File file = new File(game.data, CARD_TEXT);
        if (!file.isFile()) {
            return values;
        }
        try {
            String text = new String(GameSaves.readAll(file), StandardCharsets.UTF_8);
            for (String line : text.split("\n")) {
                int equals = line.indexOf('=');
                if (equals > 0) {
                    values.put(line.substring(0, equals), line.substring(equals + 1).trim());
                }
            }
        } catch (IOException e) {
            Log.w(TAG, "card not read", e);
        }
        return values;
    }

    // The game's picture (its main menu's), written by it at its menu.
    static File cardPicture(Game game) {
        return new File(game.data, CARD_PICTURE);
    }

    // Its files replaced: the picture made again at its next menu.
    static void forgetPicture(Game game) {
        //noinspection ResultOfMethodCallIgnored
        cardPicture(game).delete();
    }

    private interface Change<T> {
        T apply(File app, File data, Properties registry) throws IOException;
    }

    // [change] of the registry under the lock, written after.
    private static synchronized <T> T change(Context context, Change<T> change) throws IOException {
        File app = appFolder(context);
        File data = context.getFilesDir();
        try (RandomAccessFile lockFile = new RandomAccessFile(new File(data, LOCK), "rw");
             FileLock ignored = lockFile.getChannel().lock()) {
            Properties registry = registry(app, data);
            T result = change.apply(app, data, registry);
            write(registry, new File(data, REGISTRY));
            return result;
        }
    }

    private static Properties registry(File app, File data) throws IOException {
        Properties registry = read(new File(data, REGISTRY));
        File journal = new File(data, JOURNAL);
        if (registry == null || journal.isFile()) {
            registry = moveOldGame(app, data, journal, registry);
        }
        return registry;
    }

    private static File appFolder(Context context) throws IOException {
        File app = context.getExternalFilesDir(null);
        if (app == null) {
            throw new IOException("no app folder");
        }
        return app;
    }

    private static Game game(File app, File data, String id) {
        return new Game(id, new File(new File(app, GAMES), id), new File(new File(data, GAMES), id));
    }

    private static List<String> ids(Properties registry) {
        List<String> ids = new ArrayList<>();
        for (String id : registry.getProperty(KEY_GAMES, "").split(",")) {
            if (!id.trim().isEmpty()) {
                ids.add(id.trim());
            }
        }
        return ids;
    }

    private static String key(String id, String field) {
        return "game." + id + "." + field;
    }

    private static long number(Properties registry, String key) {
        try {
            return Long.parseLong(registry.getProperty(key, "0"));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    // [name], "[name] (2)" when another game has it.
    static String uniqueName(Properties registry, String id, String name) {
        Set<String> taken = new HashSet<>();
        for (String other : ids(registry)) {
            if (!other.equals(id)) {
                taken.add(registry.getProperty(key(other, "name"), "").toLowerCase(Locale.ROOT));
            }
        }
        String candidate = name;
        for (int number = 2; taken.contains(candidate.toLowerCase(Locale.ROOT)); number++) {
            candidate = name + " (" + number + ")";
        }
        return candidate;
    }

    // The folders moved away (`TRASH`) deleted, in the background: a game is
    // a few thousand files.
    private static void emptyTrash(File... folders) {
        new Thread(() -> {
            for (File folder : folders) {
                File[] entries = folder.listFiles();
                if (entries == null) {
                    continue;
                }
                for (File entry : entries) {
                    if (entry.getName().startsWith(TRASH)) {
                        deleteTree(entry);
                    }
                }
            }
        }, "games-trash").start();
    }

    private static void deleteTree(File file) {
        File[] children = file.isDirectory() ? file.listFiles() : null;
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
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
