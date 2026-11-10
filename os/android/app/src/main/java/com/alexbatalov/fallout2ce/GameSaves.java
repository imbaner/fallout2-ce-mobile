package com.alexbatalov.fallout2ce;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// The game's saves (data/SAVEGAME, laid out as on a computer) to and from
// other devices.
//
// Export: one zip - SAVEGAME as it is and, beside it, the port's records of
// its saves (INFO: when each was made, what the game was made of then, which
// were quick saves; src/save_records.h keeps them in the app).
// Import: from a folder or an archive of SAVEGAME, of SLOT folders, or of a
// whole game. Added to the saves there (the same save twice is skipped; any
// free slot, the list is by date) or replacing them all (quick saves go to
// the quick slots). Copied next to the saves first, then moved in by a
// journal finished on the next start if the app dies in between.
final class GameSaves {
    static final String INFO = "fallout2-saves.txt";
    // In the app's private folder (the engine's, src/loadsave.cc).
    static final String RECORDS = "saves.txt";
    static final String COMPOSITION = "game-composition.txt";
    static final String CHARSET = "game-charset.txt";

    static final int TOTAL_SLOTS = 1000;
    static final int SLOTS_PER_PAGE = 10;

    // Levels of SaveCompatibility (src/save_composition.h).
    static final int SAME = 0;
    static final int LIKELY = 1;
    static final int UNLIKELY = 2;
    static final int UNKNOWN = 3;

    private static final String STAGING = ".ce-import";
    private static final String OLD = ".ce-import-old";
    private static final String JOURNAL = ".ce-import-commit";
    private static final String JOURNAL_TEMP = ".ce-import-commit.tmp";
    // A composition meaning "the game's when it next starts".
    private static final String CURRENT = "*";

    private static final Pattern SLOT = Pattern.compile("(?i)slot(\\d+)");

    private GameSaves() {
    }

    // What the port knows of a save (src/save_records.h), plus whether it
    // was a quick save (export only).
    static final class Record {
        String id = "";
        long created;
        long order;
        String composition = "";
        boolean quick;
    }

    // The save's identity: FNV-1a 64 of SAVE.DAT's first 4 KB and its size
    // (as src/save_records.cc).
    static String saveId(byte[] data, int length, long size) {
        long hash = 0xcbf29ce484222325L;
        for (int index = 0; index < Math.min(length, 4096); index++) {
            hash ^= data[index] & 0xff;
            hash *= 0x100000001b3L;
        }
        return String.format(Locale.ROOT, "%016x-%d", hash, size);
    }

    static String saveId(File saveDat) {
        byte[] head = new byte[4096];
        try (InputStream in = new FileInputStream(saveDat)) {
            int length = 0;
            int read;
            while (length < head.length && (read = in.read(head, length, head.length - length)) > 0) {
                length += read;
            }
            return length > 0 ? saveId(head, length, saveDat.length()) : "";
        } catch (IOException e) {
            return "";
        }
    }

    // "SLOT07" -> 6; -1 - not a slot folder (as the game sees them).
    static int slotOf(String name) {
        Matcher matcher = SLOT.matcher(name);
        if (!matcher.matches()) {
            return -1;
        }
        int number;
        try {
            number = Integer.parseInt(matcher.group(1));
        } catch (NumberFormatException e) {
            return -1;
        }
        if (number < 1 || number > TOTAL_SLOTS || !name.equalsIgnoreCase(slotName(number - 1))) {
            return -1;
        }
        return number - 1;
    }

    static String slotName(int slot) {
        return String.format(Locale.ROOT, "SLOT%02d", slot + 1);
    }

    // The records file: slot -> record.
    static Map<Integer, Record> readRecords(File file) {
        try {
            return file.isFile() ? parseRecords(new String(readAll(file), StandardCharsets.UTF_8)) : new TreeMap<>();
        } catch (IOException e) {
            return new TreeMap<>();
        }
    }

    // Records' lines (the file's, INFO's).
    static Map<Integer, Record> parseRecords(String text) {
        Map<Integer, Record> records = new TreeMap<>();
        for (String line : text.split("\r?\n")) {
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }
            String[] fields = line.split("\t", -1);
            int slot = fields.length >= 4 ? slotOf(fields[0]) : -1;
            if (slot == -1 || fields[1].isEmpty()) {
                continue;
            }
            Record record = new Record();
            record.id = fields[1];
            record.created = parseLong(fields[2]);
            record.order = parseLong(fields[3]);
            record.composition = fields.length >= 5 ? fields[4] : "";
            record.quick = fields.length >= 6 && fields[5].equals("quick");
            records.put(slot, record);
        }
        return records;
    }

    static void writeRecords(File file, Map<Integer, Record> records) throws IOException {
        StringBuilder text = new StringBuilder("# Fallout 2 CE: when each save was made and what the game was made of then (src/save_records.h).\n");
        for (Map.Entry<Integer, Record> it : new TreeMap<>(records).entrySet()) {
            Record record = it.getValue();
            text.append(slotName(it.getKey())).append('\t').append(record.id).append('\t')
                .append(record.created).append('\t').append(record.order).append('\t')
                .append(record.composition).append('\n');
        }
        File temporary = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(text.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!temporary.renameTo(file)) {
            throw new IOException("can't write " + file);
        }
    }

    private static long parseLong(String value) {
        try {
            return Long.parseLong(value.trim());
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    // SAVE.DAT's header (big-endian, src/loadsave.cc lsgLoadHeaderInSlot).
    static final class Header {
        String name = "";
        String description = "";
        int fileDay;
        int fileMonth;
        int fileYear;
        int gameDay;
        int gameMonth;
        int gameYear;
        long gameTime;
        boolean valid;
    }

    static Header parseHeader(byte[] data, int length, Charset charset) {
        Header header = new Header();
        if (length < 131 || !new String(data, 0, 17, StandardCharsets.ISO_8859_1).equals("FALLOUT SAVE FILE")) {
            return header;
        }
        header.name = text(data, 29, 32, charset);
        header.description = text(data, 61, 30, charset);
        header.fileDay = short16(data, 91);
        header.fileMonth = short16(data, 93);
        header.fileYear = short16(data, 95);
        header.gameMonth = short16(data, 101);
        header.gameDay = short16(data, 103);
        header.gameYear = short16(data, 105);
        header.gameTime = ((data[107] & 0xffL) << 24) | ((data[108] & 0xffL) << 16) | ((data[109] & 0xffL) << 8) | (data[110] & 0xffL);
        header.valid = true;
        return header;
    }

    private static int short16(byte[] data, int offset) {
        return (short) (((data[offset] & 0xff) << 8) | (data[offset + 1] & 0xff));
    }

    private static String text(byte[] data, int offset, int length, Charset charset) {
        int end = offset;
        while (end < offset + length && data[end] != 0) {
            end++;
        }
        return new String(data, offset, end - offset, charset).trim();
    }

    // The code page of the game's texts: the one the game found (CHARSET,
    // src/game_charset.h); before it ever ran, its language's.
    static Charset gameCharset(String found, String language) {
        String name = found;
        if (name == null || name.isEmpty()) {
            switch (language.toLowerCase(Locale.ROOT)) {
            case "polish":
            case "czech":
            case "slovak":
            case "hungarian":
                name = "windows-1250";
                break;
            case "russian":
            case "ukrainian":
            case "belarusian":
            case "bulgarian":
                name = "windows-1251";
                break;
            case "greek":
                name = "windows-1253";
                break;
            case "turkish":
                name = "windows-1254";
                break;
            case "lithuanian":
            case "latvian":
            case "estonian":
                name = "windows-1257";
                break;
            default:
                name = "windows-1252";
                break;
            }
        }
        try {
            return Charset.forName(name);
        } catch (RuntimeException e) {
            return StandardCharsets.ISO_8859_1;
        }
    }

    // A config's value ([section] key=value), null without it.
    static String configValue(String text, String section, String key) {
        String current = "";
        String found = null;
        for (String line : text.split("\r?\n")) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                current = trimmed.substring(1, trimmed.length() - 1).trim();
                continue;
            }
            int equals = trimmed.indexOf('=');
            if (equals == -1 || trimmed.startsWith(";")) {
                continue;
            }
            if ((section == null || current.equalsIgnoreCase(section)) && trimmed.substring(0, equals).trim().equalsIgnoreCase(key)) {
                String value = trimmed.substring(equals + 1);
                int comment = value.indexOf(';');
                found = (comment == -1 ? value : value.substring(0, comment)).trim();
            }
        }
        return found;
    }

    // Quick save slots {first, count}: CE's fallout2.cfg ([ui]
    // auto_quick_save pages from auto_quick_save_page), else sfall's
    // ddraw.ini (AutoQuickSave, AutoQuickSavePage). Configs may be null.
    static int[] quickRange(String fallout2Cfg, String ddrawIni) {
        String pages = fallout2Cfg != null ? configValue(fallout2Cfg, "ui", "auto_quick_save") : null;
        String page = fallout2Cfg != null ? configValue(fallout2Cfg, "ui", "auto_quick_save_page") : null;
        if (pages == null && ddrawIni != null) {
            pages = configValue(ddrawIni, null, "AutoQuickSave");
            page = configValue(ddrawIni, null, "AutoQuickSavePage");
        }
        int count = (int) Math.max(0, Math.min(10, parseLong(pages != null ? pages : "0"))) * SLOTS_PER_PAGE;
        int first = (int) Math.max(0, parseLong(page != null ? page : "1")) * SLOTS_PER_PAGE;
        if (first >= TOTAL_SLOTS) {
            return new int[] { 0, 0 };
        }
        return new int[] { first, Math.min(count, TOTAL_SLOTS - first) };
    }

    // The game's saves folder ("data/SAVEGAME" in any case).
    static File savesFolder(File game) {
        File data = childIgnoringCase(game, "data");
        File saves = data != null ? childIgnoringCase(data, "SAVEGAME") : null;
        return saves != null ? saves : new File(new File(game, "data"), "SAVEGAME");
    }

    private static File childIgnoringCase(File folder, String name) {
        File[] children = folder.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.getName().equalsIgnoreCase(name)) {
                    return child;
                }
            }
        }
        return null;
    }

    // The game's saves: slot -> its folder (with a SAVE.DAT).
    static Map<Integer, File> installedSaves(File game) {
        Map<Integer, File> saves = new TreeMap<>();
        File[] children = savesFolder(game).listFiles();
        if (children == null) {
            return saves;
        }
        for (File child : children) {
            int slot = slotOf(child.getName());
            if (slot != -1 && child.isDirectory() && childIgnoringCase(child, "SAVE.DAT") != null) {
                saves.put(slot, child);
            }
        }
        return saves;
    }

    // Slots with anything in them (a save, a broken folder): never reused.
    private static Set<Integer> takenSlots(File game) {
        Set<Integer> taken = new HashSet<>();
        String[] names = savesFolder(game).list();
        if (names != null) {
            for (String name : names) {
                int slot = slotOf(name);
                if (slot != -1) {
                    taken.add(slot);
                }
            }
        }
        return taken;
    }

    // A save found in the chosen folder or archive.
    static final class FoundSave {
        // "…/SLOT03/".
        String folder;
        int sourceSlot;
        final List<GameImport.Entry> files = new ArrayList<>();
        GameImport.Entry saveDat;
        Header header = new Header();
        String id = "";
        Record record = new Record();
        boolean quick;
        // The game has it already.
        boolean duplicate;
        int level = UNKNOWN;
        // {kind, archive} as SaveComposition gives them.
        List<String[]> changes = new ArrayList<>();
    }

    static final class Found {
        // SAVEGAME's place in the source ("" or ending with "/").
        String root;
        final List<FoundSave> saves = new ArrayList<>();
        // The port's records came along (an export).
        boolean withInfo;
        // A whole game came along: its files are this game's (saves without
        // records were made with it).
        boolean sameGame;
    }

    // Compares compositions (SaveComposition.compare; tests give their own).
    interface Comparer {
        String compare(String made, String now);
    }

    // The saves of [source], newest first; null - none there. [game] - the
    // installed game (its saves for duplicates, its files for "the same
    // game"), [composition] - what it is made of now (COMPOSITION),
    // [charset] - its texts' code page (CHARSET), "" - not known.
    static Found find(GameImport.Source source, File game, String composition, String charset, Comparer comparer) throws IOException {
        List<GameImport.Entry> entries = source.list();

        // SLOT folders with a SAVE.DAT, grouped by the folder they're in.
        Map<String, List<GameImport.Entry>> roots = new HashMap<>();
        for (GameImport.Entry entry : entries) {
            String path = entry.path;
            String name = baseName(path);
            if (!name.equalsIgnoreCase("SAVE.DAT")) {
                continue;
            }
            String slotFolder = path.substring(0, path.length() - name.length());
            String folderName = baseName(slotFolder.substring(0, slotFolder.length() - 1));
            if (slotOf(folderName) == -1) {
                continue;
            }
            String root = slotFolder.substring(0, slotFolder.length() - folderName.length() - 1);
            List<GameImport.Entry> list = roots.get(root);
            if (list == null) {
                list = new ArrayList<>();
                roots.put(root, list);
            }
            list.add(entry);
        }
        if (roots.isEmpty()) {
            return null;
        }

        // The shallowest SAVEGAME (a game's backups deeper in are left).
        List<String> shallowest = new ArrayList<>();
        int least = Integer.MAX_VALUE;
        for (String root : roots.keySet()) {
            int depth = root.split("/", -1).length;
            if (depth < least) {
                least = depth;
                shallowest.clear();
            }
            if (depth == least) {
                shallowest.add(root);
            }
        }
        if (shallowest.size() > 1) {
            Collections.sort(shallowest);
            throw new GameImport.SeveralGamesException(shallowest);
        }

        Found found = new Found();
        found.root = shallowest.get(0);
        Map<String, FoundSave> byFolder = new HashMap<>();
        for (GameImport.Entry saveDat : roots.get(found.root)) {
            FoundSave save = new FoundSave();
            save.saveDat = saveDat;
            save.folder = saveDat.path.substring(0, saveDat.path.length() - baseName(saveDat.path).length());
            save.sourceSlot = slotOf(baseName(save.folder.substring(0, save.folder.length() - 1)));
            byFolder.put(save.folder.toLowerCase(Locale.ROOT), save);
            found.saves.add(save);
        }
        for (GameImport.Entry entry : entries) {
            String lower = entry.path.toLowerCase(Locale.ROOT);
            int slash = lower.indexOf('/', found.root.length());
            if (!lower.startsWith(found.root.toLowerCase(Locale.ROOT)) || slash == -1) {
                continue;
            }
            FoundSave save = byFolder.get(lower.substring(0, slash + 1));
            String name = baseName(lower);
            // Earlier builds' CE-META.TXT: the records are what counts now.
            if (save == null || name.startsWith("._") || name.equals(".ds_store") || name.equals("ce-meta.txt")) {
                continue;
            }
            if (entry.encrypted) {
                throw new NativeArchive.Failure(NativeArchive.ENCRYPTED, entry.path);
            }
            if (entry.unsupported) {
                throw new NativeArchive.Failure(NativeArchive.UNSUPPORTED, entry.path);
            }
            save.files.add(entry);
        }

        // Next to SAVEGAME: the export's records; a whole game's configs.
        String gameRoot = gameRootOf(found.root);
        GameImport.Entry info = null;
        GameImport.Entry config = null;
        GameImport.Entry ddraw = null;
        // The game's config is `GameFiles.configName` of its ddraw.ini: the
        // top folder's .cfg files are kept until ddraw.ini tells which.
        Map<String, GameImport.Entry> configs = new HashMap<>();
        Map<String, Long> sourceArchives = new HashMap<>();
        for (GameImport.Entry entry : entries) {
            String lower = entry.path.toLowerCase(Locale.ROOT);
            String parent = found.root.isEmpty() ? "" : parentOf(found.root);
            if (lower.equals((parent + INFO).toLowerCase(Locale.ROOT)) || lower.equals((found.root + INFO).toLowerCase(Locale.ROOT))) {
                info = entry;
            }
            if (gameRoot != null && lower.startsWith(gameRoot.toLowerCase(Locale.ROOT))) {
                String path = lower.substring(gameRoot.length());
                if (path.endsWith(".cfg") && path.indexOf('/') == -1) {
                    configs.put(path, entry);
                } else if (path.equals("ddraw.ini")) {
                    ddraw = entry;
                } else if (isArchive(path)) {
                    sourceArchives.put(path, entry.size);
                }
            }
        }

        if (ddraw != null) {
            String name = GameFiles.configName(new String(readAll(source, Collections.singletonList(ddraw)).get(ddraw), StandardCharsets.ISO_8859_1));
            config = configs.get(name.toLowerCase(Locale.ROOT));
        } else {
            config = configs.get(GameFiles.CONFIG);
        }

        // One pass: the saves' SAVE.DAT, the records, the configs.
        List<GameImport.Entry> wanted = new ArrayList<>();
        for (FoundSave save : found.saves) {
            wanted.add(save.saveDat);
        }
        for (GameImport.Entry entry : new GameImport.Entry[] { info, config, ddraw }) {
            if (entry != null) {
                wanted.add(entry);
            }
        }
        Map<GameImport.Entry, byte[]> contents = readAll(source, wanted);

        String language = "english";
        File installedConfig = GameFiles.configFile(game);
        if (installedConfig.isFile()) {
            String value = configValue(new String(readAll(installedConfig), StandardCharsets.ISO_8859_1), "system", "language");
            language = value != null ? value : language;
        }
        Charset textCharset = gameCharset(charset, language);

        Map<Integer, Record> infoRecords = new HashMap<>();
        if (info != null) {
            infoRecords = parseRecords(new String(contents.get(info), StandardCharsets.UTF_8));
            found.withInfo = true;
        }

        int[] sourceQuick = quickRange(
            config != null ? new String(contents.get(config), StandardCharsets.ISO_8859_1) : null,
            ddraw != null ? new String(contents.get(ddraw), StandardCharsets.ISO_8859_1) : null);
        found.sameGame = !sourceArchives.isEmpty() && sourceArchives.equals(installedArchives(game));

        Set<String> installedIds = new HashSet<>();
        for (File folder : installedSaves(game).values()) {
            File saveDat = childIgnoringCase(folder, "SAVE.DAT");
            if (saveDat != null) {
                installedIds.add(saveId(saveDat));
            }
        }

        for (FoundSave save : found.saves) {
            byte[] data = contents.get(save.saveDat);
            save.header = parseHeader(data, data.length, textCharset);
            save.id = saveId(data, data.length, data.length);
            save.duplicate = installedIds.contains(save.id);

            Record record = infoRecords.get(save.sourceSlot);
            if (record != null && record.id.equals(save.id)) {
                save.record = record;
                save.quick = record.quick;
            } else {
                // A computer's save: its place in the list by the day in its
                // header and the game time within it; when it was made isn't
                // known (no time shown).
                save.record.id = save.id;
                save.record.created = 0;
                save.record.composition = found.sameGame ? CURRENT : "";
                save.quick = save.sourceSlot >= sourceQuick[0] && save.sourceSlot < sourceQuick[0] + sourceQuick[1];
            }
            save.record.id = save.id;

            String made = save.record.composition;
            if (made.equals(CURRENT)) {
                save.level = SAME;
            } else if (made.isEmpty()) {
                save.level = UNKNOWN;
            } else {
                parseComparison(comparer.compare(made, composition), save);
            }
        }

        // Records without an order (a computer's saves): by the day, then
        // the game's time.
        List<FoundSave> unordered = new ArrayList<>();
        for (FoundSave save : found.saves) {
            if (save.record.order <= 0) {
                unordered.add(save);
            }
        }
        Collections.sort(unordered, (a, b) -> {
            int byDay = Long.compare(dayOf(a.header), dayOf(b.header));
            return byDay != 0 ? byDay : Long.compare(a.header.gameTime, b.header.gameTime);
        });
        for (int index = 0; index < unordered.size(); index++) {
            FoundSave save = unordered.get(index);
            save.record.order = Math.max(dayOf(save.header), 1) * 1000000 + index;
        }

        Collections.sort(found.saves, (a, b) -> Long.compare(b.record.order, a.record.order));
        return found;
    }

    // "Games/Fallout 2/data/SAVEGAME/" -> "Games/Fallout 2/"; null when
    // SAVEGAME isn't in a game's data folder.
    private static String gameRootOf(String root) {
        String lower = root.toLowerCase(Locale.ROOT);
        if (lower.equals("data/savegame/")) {
            return "";
        }
        if (lower.endsWith("/data/savegame/")) {
            return root.substring(0, root.length() - "data/savegame/".length());
        }
        return null;
    }

    private static String parentOf(String folder) {
        String trimmed = folder.substring(0, folder.length() - 1);
        int slash = trimmed.lastIndexOf('/');
        return slash == -1 ? "" : trimmed.substring(0, slash + 1);
    }

    // The game's archives (Fallout 2's, sfall's, the mods'): what tells one
    // game from another.
    private static boolean isArchive(String lower) {
        return lower.equals("master.dat") || lower.equals("critter.dat") || lower.equals("patch000.dat")
            || lower.equals("sfall.dat") || (lower.startsWith("mods/") && lower.endsWith(".dat") && lower.indexOf('/', 5) == -1);
    }

    private static Map<String, Long> installedArchives(File game) {
        Map<String, Long> archives = new HashMap<>();
        for (String path : GameFiles.installedGameFiles(game).values()) {
            String lower = path.toLowerCase(Locale.ROOT);
            if (isArchive(lower)) {
                archives.put(lower, new File(game, path).length());
            }
        }
        return archives;
    }

    // Unix seconds of the header's day (noon), 0 without one.
    private static long dayOf(Header header) {
        if (!header.valid || header.fileYear < 1990 || header.fileMonth < 1 || header.fileMonth > 12 || header.fileDay < 1 || header.fileDay > 31) {
            return 0;
        }
        Calendar calendar = Calendar.getInstance();
        calendar.clear();
        calendar.set(header.fileYear, header.fileMonth - 1, header.fileDay, 12, 0, 0);
        return calendar.getTimeInMillis() / 1000;
    }

    private static void parseComparison(String result, FoundSave save) {
        String[] lines = result.split("\n");
        save.level = (int) parseLong(lines[0]);
        for (int index = 1; index < lines.length; index++) {
            String[] parts = lines[index].split("\t", 2);
            if (parts.length == 2) {
                save.changes.add(parts);
            }
        }
    }

    private static Map<GameImport.Entry, byte[]> readAll(GameImport.Source source, List<GameImport.Entry> wanted) throws IOException {
        Map<GameImport.Entry, byte[]> contents = new HashMap<>();
        if (wanted.isEmpty()) {
            return contents;
        }
        source.read(wanted, new GameImport.EntrySink() {
            private ByteArrayOutputStream current;

            @Override
            public void begin(GameImport.Entry entry) {
                current = new ByteArrayOutputStream();
            }

            @Override
            public void data(byte[] buffer, int length) {
                current.write(buffer, 0, length);
            }

            @Override
            public void end(GameImport.Entry entry) {
                contents.put(entry, current.toByteArray());
            }
        });
        return contents;
    }

    // Where the saves go.
    static final class Plan {
        final List<FoundSave> saves = new ArrayList<>();
        final List<Integer> slots = new ArrayList<>();
        // The game's save folders removed (replacing all).
        final List<String> removed = new ArrayList<>();
        // Found saves the game has already (adding), left out.
        int skipped;
    }

    static final class NoRoomException extends IOException {
        final int needed;
        final int free;

        NoRoomException(int needed, int free) {
            super("no room: " + needed + " saves, " + free + " free slots");
            this.needed = needed;
            this.free = free;
        }
    }

    // [replaceAll]: the game's saves go, the found ones' quick saves take
    // the game's quick slots (newest first; the rest become saves of their
    // own). Else they are added to free slots outside the quick ones, the
    // game's saves skipped.
    static Plan plan(Found found, File game, boolean replaceAll) throws NoRoomException {
        Plan plan = new Plan();
        int[] quick = quickRange(readConfig(game), null);
        Set<Integer> taken = replaceAll ? new HashSet<>() : takenSlots(game);
        if (replaceAll) {
            String[] names = savesFolder(game).list();
            if (names != null) {
                for (String name : names) {
                    if (slotOf(name) != -1) {
                        plan.removed.add(name);
                    }
                }
            }
        }

        List<Integer> freeQuick = new ArrayList<>();
        List<Integer> freeManual = new ArrayList<>();
        for (int slot = 0; slot < TOTAL_SLOTS; slot++) {
            if (taken.contains(slot)) {
                continue;
            }
            if (slot >= quick[0] && slot < quick[0] + quick[1]) {
                freeQuick.add(slot);
            } else {
                freeManual.add(slot);
            }
        }

        // Newest first: the newest quick saves get the quick slots.
        int needed = 0;
        for (FoundSave save : found.saves) {
            if (!replaceAll && save.duplicate) {
                plan.skipped++;
                continue;
            }
            needed++;
        }
        int manualNeeded = 0;
        for (FoundSave save : found.saves) {
            if (!replaceAll && save.duplicate) {
                continue;
            }
            Integer slot;
            if (replaceAll && save.quick && !freeQuick.isEmpty()) {
                slot = freeQuick.remove(0);
            } else if (!freeManual.isEmpty()) {
                slot = freeManual.remove(0);
            } else {
                manualNeeded++;
                continue;
            }
            plan.saves.add(save);
            plan.slots.add(slot);
        }
        if (manualNeeded > 0) {
            throw new NoRoomException(needed, needed - manualNeeded);
        }
        return plan;
    }

    private static String readConfig(File game) {
        File config = GameFiles.configFile(game);
        try {
            return config.isFile() ? new String(readAll(config), StandardCharsets.ISO_8859_1) : null;
        } catch (IOException e) {
            return null;
        }
    }

    interface Progress {
        void update(long done, long total) throws IOException;
    }

    // Copies [plan]'s saves into the game's saves folder, their records into
    // [records] (the app's private RECORDS file).
    static void apply(GameImport.Source source, Plan plan, File game, File records, Progress progress) throws IOException {
        File saves = savesFolder(game);
        if (!saves.isDirectory() && !saves.mkdirs()) {
            throw new IOException("can't make " + saves);
        }
        finish(game);

        File staging = new File(saves, STAGING);
        Map<GameImport.Entry, File> targets = new HashMap<>();
        List<GameImport.Entry> wanted = new ArrayList<>();
        long total = 0;
        for (int index = 0; index < plan.saves.size(); index++) {
            FoundSave save = plan.saves.get(index);
            File folder = new File(staging, slotName(plan.slots.get(index)));
            for (GameImport.Entry entry : save.files) {
                targets.put(entry, GameImport.targetFile(folder, entry.path.substring(save.folder.length())));
                wanted.add(entry);
                total += entry.size;
            }
        }

        long[] done = { 0 };
        long totalBytes = total;
        try {
            source.read(wanted, new GameImport.EntrySink() {
                private FileOutputStream out;

                @Override
                public void begin(GameImport.Entry entry) throws IOException {
                    File file = targets.get(entry);
                    File folder = file.getParentFile();
                    if (folder != null && !folder.isDirectory() && !folder.mkdirs()) {
                        throw new IOException("can't make " + folder);
                    }
                    out = new FileOutputStream(file);
                }

                @Override
                public void data(byte[] buffer, int length) throws IOException {
                    out.write(buffer, 0, length);
                    done[0] += length;
                    progress.update(done[0], totalBytes);
                }

                @Override
                public void end(GameImport.Entry entry) throws IOException {
                    out.getFD().sync();
                    out.close();
                    out = null;
                }
            });
        } catch (IOException | RuntimeException e) {
            GameFiles.deleteRecursively(staging);
            throw e;
        }

        // The records first: until the saves are moved in they don't match
        // (another save or none in the slot) and are ignored.
        Map<Integer, Record> kept = readRecords(records);
        if (!plan.removed.isEmpty()) {
            kept.clear();
        }
        for (int index = 0; index < plan.saves.size(); index++) {
            Record record = plan.saves.get(index).record;
            record.quick = false;
            kept.put(plan.slots.get(index), record);
        }
        writeRecords(records, kept);

        StringBuilder journal = new StringBuilder();
        for (String name : plan.removed) {
            journal.append("O\t").append(name).append('\n');
        }
        for (int slot : plan.slots) {
            journal.append("M\t").append(slotName(slot)).append('\n');
        }
        File temporary = new File(saves, JOURNAL_TEMP);
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write(journal.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!temporary.renameTo(new File(saves, JOURNAL))) {
            throw new IOException("can't write the journal");
        }
        finish(game);
    }

    // Carries out a journal of `apply` (again after a crash: each step can be
    // repeated), then removes what is left aside.
    static void finish(File game) throws IOException {
        File saves = savesFolder(game);
        File journalFile = new File(saves, JOURNAL);
        File staging = new File(saves, STAGING);
        File old = new File(saves, OLD);
        if (journalFile.isFile()) {
            String journal = new String(readAll(journalFile), StandardCharsets.UTF_8);
            for (String line : journal.split("\n")) {
                String[] parts = line.split("\t");
                if (parts.length != 2) {
                    continue;
                }
                if (parts[0].equals("O")) {
                    File folder = new File(saves, parts[1]);
                    if (folder.exists()) {
                        if (!old.isDirectory() && !old.mkdirs()) {
                            throw new IOException("can't make " + old);
                        }
                        if (!folder.renameTo(new File(old, parts[1]))) {
                            throw new IOException("can't move " + parts[1] + " aside");
                        }
                    }
                } else if (parts[0].equals("M")) {
                    File from = new File(staging, parts[1]);
                    if (from.isDirectory() && !from.renameTo(new File(saves, parts[1]))) {
                        throw new IOException("can't move " + parts[1] + " in");
                    }
                }
            }
            if (!journalFile.delete()) {
                throw new IOException("can't remove the journal");
            }
        }
        //noinspection ResultOfMethodCallIgnored
        new File(saves, JOURNAL_TEMP).delete();
        GameFiles.deleteRecursively(staging);
        GameFiles.deleteRecursively(old);
    }

    static boolean isCommitPending(File game) {
        return new File(savesFolder(game), JOURNAL).isFile();
    }

    // Writes all the game's saves into [out] as a zip (SAVEGAME/SLOTnn/...
    // and INFO); the number of saves.
    static int export(File game, File records, String composition, OutputStream out, Progress progress) throws IOException {
        Map<Integer, File> saves = installedSaves(game);
        Map<Integer, Record> known = readRecords(records);
        int[] quick = quickRange(readConfig(game), null);

        long total = 0;
        for (File folder : saves.values()) {
            total += sizeOf(folder);
        }

        StringBuilder info = new StringBuilder("# Fallout 2 CE: the saves' records (src/save_records.h), \"quick\" - a quick save.\n");
        long[] done = { 0 };
        byte[] buffer = new byte[256 * 1024];
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            for (Map.Entry<Integer, File> it : saves.entrySet()) {
                int slot = it.getKey();
                File folder = it.getValue();
                String name = slotName(slot);
                addFolder(zip, folder, "SAVEGAME/" + name + "/", buffer, done, total, progress);

                File saveDat = childIgnoringCase(folder, "SAVE.DAT");
                String id = saveId(saveDat);
                Record record = known.get(slot);
                long created = saveDat.lastModified() / 1000;
                long order = created * 1000000;
                String made = "";
                if (record != null && record.id.equals(id)) {
                    created = record.created > 0 ? record.created : created;
                    order = record.order > 0 ? record.order : order;
                    made = record.composition.equals(CURRENT) ? composition : record.composition;
                }
                info.append(name).append('\t').append(id).append('\t').append(created).append('\t').append(order).append('\t')
                    .append(made).append('\t').append(slot >= quick[0] && slot < quick[0] + quick[1] ? "quick" : "").append('\n');
            }
            zip.putNextEntry(new ZipEntry(INFO));
            zip.write(info.toString().getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
        }
        return saves.size();
    }

    private static void addFolder(ZipOutputStream zip, File folder, String prefix, byte[] buffer, long[] done, long total, Progress progress) throws IOException {
        File[] children = folder.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            if (child.isDirectory()) {
                addFolder(zip, child, prefix + child.getName() + "/", buffer, done, total, progress);
                continue;
            }
            if (child.getName().equalsIgnoreCase("CE-META.TXT")) {
                continue;
            }
            ZipEntry entry = new ZipEntry(prefix + child.getName());
            entry.setTime(child.lastModified());
            zip.putNextEntry(entry);
            try (InputStream in = new FileInputStream(child)) {
                int read;
                while ((read = in.read(buffer)) > 0) {
                    zip.write(buffer, 0, read);
                    done[0] += read;
                    progress.update(done[0], total);
                }
            }
            zip.closeEntry();
        }
    }

    private static long sizeOf(File file) {
        return FileTree.size(file);
    }

    private static String baseName(String path) {
        int slash = path.lastIndexOf('/');
        return slash == -1 ? path : path.substring(slash + 1);
    }

    static byte[] readAll(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }
}
