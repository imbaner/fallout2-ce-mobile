package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

// Games: the games' folders, an earlier build's game moved into the first
// one, on files of a temporary folder.
public class GamesTest {
    @Rule
    public TemporaryFolder temporary = new TemporaryFolder();

    private static void write(File file, String text) throws IOException {
        File folder = file.getParentFile();
        if (folder != null && !folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("no folder");
        }
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }

    private static String read(File file) throws IOException {
        return GameFiles.readText(file);
    }

    private static String[] names(File folder) {
        String[] names = folder.list();
        if (names == null) {
            return new String[0];
        }
        Arrays.sort(names);
        return names;
    }

    // An earlier build's app folder: the game, its saves, the app's own.
    private File[] oldLayout() throws IOException {
        File app = temporary.newFolder("app");
        File data = temporary.newFolder("data");
        write(new File(app, "master.dat"), "master");
        write(new File(app, "critter.dat"), "critter");
        write(new File(app, "fallout2.cfg"), "[touch]\nfont=1\n");
        write(new File(app, "data/SAVEGAME/SLOT01/SAVE.DAT"), "save");
        write(new File(app, "mods/rpu.dat"), "rpu");
        write(new File(app, "actions.log"), "log");
        write(new File(app, "ce.dat/fonts/a.ttf"), "font");
        write(new File(app, "running.txt"), "run");
        write(new File(app, "problem.txt"), "problem");
        write(new File(data, "saves.txt"), "records");
        write(new File(data, "game-composition.txt"), "composition");
        write(new File(data, "game-charset.txt"), "cp1251");
        return new File[] { app, data };
    }

    @Test
    public void movesAnEarlierBuildsGameIntoTheFirstFolder() throws IOException {
        File[] layout = oldLayout();
        File app = layout[0];
        File data = layout[1];

        Games.Game game = Games.current(app, data);

        assertEquals("1", game.id);
        assertEquals(new File(app, "games/1"), game.folder);
        assertEquals(new File(data, "games/1"), game.data);
        // The app's own stay, everything else is the game's.
        assertArrayEquals(new String[] { "ce.dat", "games", "problem.txt", "running.txt" }, names(app));
        assertArrayEquals(new String[] { "actions.log", "critter.dat", "data", "fallout2.cfg", "master.dat", "mods" }, names(game.folder));
        assertEquals("save", read(new File(game.folder, "data/SAVEGAME/SLOT01/SAVE.DAT")));
        assertEquals("rpu", read(new File(game.folder, "mods/rpu.dat")));
        assertEquals("records", read(new File(game.data, "saves.txt")));
        assertEquals("composition", read(new File(game.data, "game-composition.txt")));
        assertEquals("cp1251", read(new File(game.data, "game-charset.txt")));
        assertFalse(new File(data, "saves.txt").exists());
        assertFalse(new File(data, "games-move.txt").exists());
        assertTrue(GameImport.isInstalled(game.folder));
    }

    @Test
    public void staysPutOnceMoved() throws IOException {
        File[] layout = oldLayout();
        Games.Game first = Games.current(layout[0], layout[1]);
        // A file the game made later in its folder stays there.
        write(new File(first.folder, "data/SAVEGAME/SLOT02/SAVE.DAT"), "second");

        Games.Game again = Games.current(layout[0], layout[1]);

        assertEquals(first.folder, again.folder);
        assertEquals("second", read(new File(again.folder, "data/SAVEGAME/SLOT02/SAVE.DAT")));
        assertArrayEquals(new String[] { "ce.dat", "games", "problem.txt", "running.txt" }, names(layout[0]));
    }

    @Test
    public void aNewInstallGetsAnEmptyFirstGame() throws IOException {
        File app = temporary.newFolder("app");
        File data = temporary.newFolder("data");
        write(new File(app, "ce.dat/fonts/a.ttf"), "font");

        Games.Game game = Games.current(app, data);

        assertEquals("1", game.id);
        assertTrue(game.folder.isDirectory());
        assertEquals(0, names(game.folder).length);
        assertFalse(GameImport.isInstalled(game.folder));
    }

    @Test
    public void finishesAMoveCutShort() throws IOException {
        File[] layout = oldLayout();
        File app = layout[0];
        File data = layout[1];
        // The app died after moving a part: the journal says where to.
        write(new File(data, "games-move.txt"), "1\n");
        File folder = new File(app, "games/1");
        assertTrue(folder.mkdirs());
        assertTrue(new File(app, "master.dat").renameTo(new File(folder, "master.dat")));
        assertTrue(new File(app, "data").renameTo(new File(folder, "data")));

        Games.Game game = Games.current(app, data);

        assertEquals(folder, game.folder);
        assertArrayEquals(new String[] { "actions.log", "critter.dat", "data", "fallout2.cfg", "master.dat", "mods" }, names(folder));
        assertEquals("records", read(new File(game.data, "saves.txt")));
        assertFalse(new File(data, "games-move.txt").exists());
        assertTrue(new File(data, "games.txt").isFile());
    }
}
