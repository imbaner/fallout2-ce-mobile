package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.text.format.Formatter;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

// The game's files: the first launch's import of the player's own copy of
// Fallout 2 (a folder or an archive of it put on the phone) and "Replace
// game" (GameImport, GameFiles); the saves' import and export (GameSaves,
// MODE). One panel in the mobile UI's colors and font, its texts and buttons
// change by step: choose - look - what was found - copying - the game starts.
public class ImportActivity extends Activity {
    private static final String TAG = "ImportActivity";
    private static final int REQUEST_FOLDER = 1;
    private static final int REQUEST_ARCHIVE = 2;
    private static final int REQUEST_EXPORT = 3;

    // What it is opened for (the game's "Game files" settings): the game's
    // files (none), the saves' import or export.
    static final String MODE = "mode";
    static final String MODE_SAVES_IMPORT = "saves_import";
    static final String MODE_SAVES_EXPORT = "saves_export";

    private TextView title;
    private TextView message;
    private TextView details;
    private ImportProgressView progress;
    private TextView progressText;
    private Button primary;
    private Button secondary;
    private android.widget.ImageButton backToGame;

    private volatile boolean cancelled;
    private boolean busy;
    // A game is there: the chosen one replaces it ("Replace game": its saves
    // stay, the chosen one's don't come).
    private boolean replacing;
    // The chosen folder or archive while it is looked at or copied.
    private GameImport.Source openSource;
    private String mode;
    // Back: to the previous step, else the game (opened from it).
    private Runnable onBack;
    // After an export: what was being done (replacing all saves), else back
    // to the game.
    private Runnable afterExport;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_import);

        title = findViewById(R.id.import_title);
        message = findViewById(R.id.import_message);
        details = findViewById(R.id.import_details);
        progress = findViewById(R.id.import_progress);
        progressText = findViewById(R.id.import_progress_text);
        primary = findViewById(R.id.import_primary);
        secondary = findViewById(R.id.import_secondary);
        backToGame = findViewById(R.id.import_back);
        backToGame.setOnClickListener(v -> {
            closeQuietly(openSource);
            startGame();
        });

        applyFont(findViewById(R.id.import_panel));
        replacing = GameImport.isInstalled(getExternalFilesDir(null));
        mode = getIntent().getStringExtra(MODE);
        if (MODE_SAVES_EXPORT.equals(mode)) {
            startExport(null);
        } else {
            showWelcome();
        }
    }

    private boolean savesMode() {
        return MODE_SAVES_IMPORT.equals(mode) || MODE_SAVES_EXPORT.equals(mode);
    }

    // Match TouchSettings: Classic by default, PT Mono when the player chose it.
    private void applyFont(View view) {
        boolean mono = false;
        try {
            File config = GameFiles.configFile(getExternalFilesDir(null));
            if (config.isFile()) {
                mono = "1".equals(GameSaves.configValue(GameFiles.readText(config), "touch", "font"));
            }
        } catch (IOException ignored) {
            // An absent/unreadable first-run config uses the game's default.
        }
        Typeface font;
        try {
            font = Typeface.createFromAsset(getAssets(), "ce.dat/fonts/" + (mono ? "PTM55FT.ttf" : "FalloutClassicDialog.ttf"));
        } catch (RuntimeException e) {
            font = Typeface.MONOSPACE;
        }
        applyFont(view, font);
    }

    private void applyFont(View view, Typeface font) {
        if (view instanceof TextView) {
            ((TextView) view).setTypeface(font);
        } else if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = 0; index < group.getChildCount(); index++) {
                applyFont(group.getChildAt(index), font);
            }
        }
    }

    private void show(String messageText, String detailsText, String primaryText, View.OnClickListener onPrimary,
        String secondaryText, View.OnClickListener onSecondary) {
        title.setText(savesMode() ? R.string.saves_title : R.string.import_title);
        // Opened from the game (not the first install): back to it, but not
        // while copying (its own cancel).
        backToGame.setVisibility((replacing || savesMode()) && !busy ? View.VISIBLE : View.GONE);
        message.setText(messageText);
        details.setText(detailsText);
        details.setVisibility(detailsText != null ? View.VISIBLE : View.GONE);
        progress.setVisibility(View.GONE);
        progressText.setVisibility(View.GONE);

        primary.setText(primaryText);
        primary.setOnClickListener(onPrimary);
        primary.setVisibility(primaryText != null ? View.VISIBLE : View.GONE);
        secondary.setText(secondaryText);
        secondary.setOnClickListener(onSecondary);
        secondary.setVisibility(secondaryText != null ? View.VISIBLE : View.GONE);

        // The gap between the buttons only when both are there: one alone
        // spans the panel like the progress above it.
        LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) secondary.getLayoutParams();
        params.setMarginEnd(primaryText != null ? Math.round(10 * getResources().getDisplayMetrics().density) : 0);
        secondary.setLayoutParams(params);
    }

    private void showWelcome() {
        busy = false;
        onBack = null;
        int text = MODE_SAVES_IMPORT.equals(mode) ? R.string.saves_import_welcome
            : replacing ? R.string.import_replace_welcome : R.string.import_welcome;
        // The first import says whose app this is (not Fallout 2's own).
        String details = text == R.string.import_welcome ? getString(R.string.import_unofficial) : null;
        show(getString(text), details,
            getString(R.string.import_choose_folder), v -> chooseFolder(),
            getString(R.string.import_choose_archive), v -> chooseArchive());
    }

    private void showError(String text) {
        busy = false;
        show(text, null, getString(R.string.import_again), v -> showWelcome(), null, null);
    }

    private void chooseFolder() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_FOLDER);
    }

    // Any file: the types phones give archives differ (a .rar or .7z is often
    // just "octet-stream"); what it is is seen when it is read.
    private void chooseArchive() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, REQUEST_ARCHIVE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent resultData) {
        if (requestCode == REQUEST_EXPORT) {
            Uri uri = resultCode == Activity.RESULT_OK && resultData != null ? resultData.getData() : null;
            if (uri != null) {
                export(uri);
            } else {
                finishExport();
            }
            return;
        }
        if (requestCode != REQUEST_FOLDER && requestCode != REQUEST_ARCHIVE) {
            super.onActivityResult(requestCode, resultCode, resultData);
            return;
        }

        Uri uri = resultCode == Activity.RESULT_OK && resultData != null ? resultData.getData() : null;
        if (uri != null) {
            look(requestCode == REQUEST_FOLDER, uri);
        }
    }

    // Looks for the game in the chosen folder or archive.
    // Some archives are read through to list them (solid RAR, .tar.gz): the
    // progress shows how far, and it can be cancelled.
    private void look(boolean folder, Uri uri) {
        busy = true;
        cancelled = false;
        show(getString(R.string.import_checking), null, null, null, getString(R.string.import_cancel), v -> cancelled = true);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);

        new Thread(() -> {
            String name = folder ? null : displayName(uri);
            long[] lastUpdate = { 0 };
            GameImport.Source source = null;
            GameImport.Found found;
            GameFiles.Plan plan = null;
            try {
                source = folder
                    ? new GameImport.FolderSource(getContentResolver(), uri)
                    : new GameImport.ArchiveSource(getContentResolver(), uri, (done, total) -> {
                        if (cancelled) {
                            throw new GameImport.CancelledException();
                        }
                        long now = System.currentTimeMillis();
                        if (now - lastUpdate[0] > 100) {
                            lastUpdate[0] = now;
                            runOnUiThread(() -> {
                                progress.setIndeterminate(false);
                                progress.setFraction(total > 0 ? (float) done / total : 0.0f);
                            });
                        }
                    });
                openSource = source;
                if (MODE_SAVES_IMPORT.equals(mode)) {
                    GameSaves.Found saves = GameSaves.find(source, getExternalFilesDir(null), currentComposition(), currentCharset(), SaveComposition::compare);
                    GameImport.Source chosen = source;
                    if (saves == null) {
                        closeQuietly(source);
                        runOnUiThread(() -> showError(getString(R.string.saves_none_found)));
                    } else {
                        runOnUiThread(() -> showSaves(chosen, saves));
                    }
                    return;
                }
                found = GameImport.find(source, replacing);
                if (found != null) {
                    plan = GameImport.plan(found, getExternalFilesDir(null), !replacing);
                }
            } catch (GameImport.CancelledException e) {
                closeQuietly(source);
                runOnUiThread(this::showWelcome);
                return;
            } catch (GameImport.ChooseGameException e) {
                GameImport.Source chosen = source;
                runOnUiThread(() -> showChoice(chosen, e.roots));
                return;
            } catch (IOException | RuntimeException e) {
                closeQuietly(source);
                Log.w(TAG, "can't use " + uri, e);
                String text = problemText(e, name);
                runOnUiThread(() -> showError(text));
                return;
            }

            if (found == null) {
                closeQuietly(source);
                runOnUiThread(() -> showError(getString(R.string.import_not_found)));
                return;
            }

            GameImport.Source chosen = source;
            GameFiles.Plan chosenPlan = plan;
            runOnUiThread(() -> showFound(chosen, found, chosenPlan));
        }).start();
    }

    // Fallout 2 and a mod in its subfolder that uses its files ([roots]:
    // Fallout 2's first): which one to install. More mods than one: the
    // folder or archive with one of them.
    private void showChoice(GameImport.Source source, List<String> roots) {
        busy = false;
        progress.setIndeterminate(false);
        if (roots.size() != 2) {
            closeQuietly(source);
            showError(problemText(new GameImport.SeveralGamesException(roots), null));
            return;
        }

        String mod = baseName(folderName(roots.get(1)));
        onBack = () -> {
            closeQuietly(source);
            showWelcome();
        };
        show(getString(R.string.import_choose_game, mod), null,
            mod, v -> lookChosen(source, roots.get(1)),
            getString(R.string.import_fallout2), v -> lookChosen(source, roots.get(0)));
    }

    // The game the player chose of showChoice.
    private void lookChosen(GameImport.Source source, String root) {
        busy = true;
        onBack = null;
        show(getString(R.string.import_checking), null, null, null, null, null);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);

        new Thread(() -> {
            GameImport.Found found;
            GameFiles.Plan plan;
            try {
                found = GameImport.find(source, replacing, root);
                plan = found != null ? GameImport.plan(found, getExternalFilesDir(null), !replacing) : null;
            } catch (IOException | RuntimeException e) {
                closeQuietly(source);
                Log.w(TAG, "can't use " + root, e);
                String text = problemText(e, null);
                runOnUiThread(() -> showError(text));
                return;
            }
            if (found == null) {
                closeQuietly(source);
                runOnUiThread(() -> showError(getString(R.string.import_not_found)));
                return;
            }
            runOnUiThread(() -> showFound(source, found, plan));
        }).start();
    }

    private static String baseName(String path) {
        int slash = path.lastIndexOf('/');
        return slash == -1 ? path : path.substring(slash + 1);
    }

    private void showFound(GameImport.Source source, GameImport.Found found, GameFiles.Plan plan) {
        busy = false;
        progress.setIndeterminate(false);

        File target = getExternalFilesDir(null);
        long free = target != null ? target.getUsableSpace() : 0;

        StringBuilder text = new StringBuilder();
        if (!found.root.isEmpty()) {
            text.append(getString(R.string.import_root, folderName(found.root))).append('\n');
        }
        text.append(getString(R.string.import_language, languageName(found.language))).append('\n');
        if (replacing) {
            text.append(getString(R.string.import_changes, plan.added, plan.replaced, plan.removed.size())).append('\n');
            if (!plan.modsAdded.isEmpty()) {
                text.append(getString(R.string.import_mods_added, String.join(", ", plan.modsAdded))).append('\n');
            }
            if (!plan.modsRemoved.isEmpty()) {
                text.append(getString(R.string.import_mods_removed, String.join(", ", plan.modsRemoved))).append('\n');
            }
            String removed = removedFolders(plan.removed);
            if (!removed.isEmpty()) {
                text.append(getString(R.string.import_removed_from, removed)).append('\n');
            }
            if (found.modsOnly) {
                text.append(getString(R.string.import_originals_kept)).append('\n');
            }
            text.append(getString(R.string.import_saves_untouched));
            if (found.saves > 0) {
                text.append(' ').append(getString(R.string.import_saves_not_taken, found.saves));
            }
            text.append('\n');
        } else {
            text.append(getString(R.string.import_mods, found.mods.isEmpty() ? getString(R.string.import_no_mods) : String.join(", ", found.mods))).append('\n');
            text.append(getString(R.string.import_saves, found.saves)).append('\n');
        }
        text.append(getString(R.string.import_size, size(plan.bytesToWrite), size(free)));

        View.OnClickListener other = v -> {
            closeQuietly(source);
            showWelcome();
        };

        // A little more than the files: the game's own folders and saves.
        long needed = plan.bytesToWrite + 64L * 1024 * 1024;
        if (free < needed) {
            show(getString(R.string.import_found) + " " + getString(R.string.import_no_space, size(needed - free)), text.toString(),
                getString(R.string.import_choose_other), other, null, null);
            return;
        }

        show(getString(R.string.import_found), text.toString(),
            getString(replacing ? R.string.import_replace : R.string.import_copy), v -> copy(source, found, plan),
            getString(R.string.import_choose_other), other);
    }

    private void copy(GameImport.Source source, GameImport.Found found, GameFiles.Plan plan) {
        busy = true;
        cancelled = false;
        show(getString(R.string.import_copying), null, null, null, getString(R.string.import_cancel), v -> cancelled = true);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(false);
        progress.setFraction(0.0f);
        progressText.setVisibility(View.VISIBLE);
        progressText.setText(getString(R.string.import_progress, size(0), size(plan.bytesToRead)));

        new Thread(() -> {
            try {
                if (replacing && !waitForGameToClose()) {
                    String text = getString(R.string.import_game_running);
                    runOnUiThread(() -> showError(text));
                    return;
                }
                GameImport.install(source, found, plan, !replacing, getExternalFilesDir(null), new GameImport.Progress() {
                    @Override
                    public void update(long copied, long total, String path) {
                        runOnUiThread(() -> {
                            progress.setFraction(total > 0 ? (float) copied / total : 0.0f);
                            progressText.setText(getString(R.string.import_progress, size(copied), size(total)) + "  " + path);
                        });
                    }

                    @Override
                    public boolean isCancelled() {
                        return cancelled;
                    }
                });
            } catch (GameImport.CancelledException e) {
                runOnUiThread(() -> showError(getString(replacing ? R.string.import_replace_cancelled : R.string.import_cancelled)));
                return;
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "copy failed", e);
                String text = problemText(e, null);
                // Not the archive's nor the phone's (their texts say what to
                // do): the game, as it was, tells it at its start.
                if (replacing && !(e instanceof NativeArchive.Failure) && !isNoSpace(e)) {
                    writeProblem("replace-game copy " + e.getClass().getSimpleName());
                }
                runOnUiThread(() -> showError(text));
                return;
            } finally {
                closeQuietly(source);
            }

            writeProblem(null);
            runOnUiThread(this::startGame);
        }).start();
    }

    private void startGame() {
        startActivity(new Intent(this, MainActivity.class));
        finish();
    }

    // The game (the app's main process) closes itself when "Replace game" is
    // chosen in it; its files are replaced only once it has.
    private boolean waitForGameToClose() {
        ActivityManager manager = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
        for (int attempt = 0; attempt < 150; attempt++) {
            boolean running = false;
            List<ActivityManager.RunningAppProcessInfo> processes = manager.getRunningAppProcesses();
            if (processes != null) {
                for (ActivityManager.RunningAppProcessInfo process : processes) {
                    running |= process.processName.equals(getPackageName());
                }
            }
            if (!running) {
                return true;
            }
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return false;
    }

    // The phone's storage filled up while copying.
    private static boolean isNoSpace(Throwable e) {
        String text = String.valueOf(e.getMessage());
        return text.contains("ENOSPC") || text.contains("No space left");
    }

    // A problem the game tells at its next start (error_report.cc
    // "problem.txt": the reason's code and what failed, no paths), or none
    // ([line] null: the replacement went through after all).
    private void writeProblem(String line) {
        File file = new File(getExternalFilesDir(null), "problem.txt");
        if (line == null) {
            //noinspection ResultOfMethodCallIgnored
            file.delete();
            return;
        }
        try (java.io.OutputStream out = new java.io.FileOutputStream(file)) {
            out.write((line + "\n").getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (IOException e) {
            Log.w(TAG, "problem not kept", e);
        }
    }

    // What the player is told when the chosen folder or archive can't be used
    // ([name]: the archive's file name, for the parts of a split one).
    private String problemText(Throwable e, String name) {
        if (e instanceof GameImport.SeveralGamesException) {
            List<String> names = new ArrayList<>();
            for (String root : ((GameImport.SeveralGamesException) e).roots) {
                names.add(folderName(root));
            }
            return getString(R.string.import_several, String.join(", ", names));
        }
        if (isNoSpace(e)) {
            return getString(R.string.import_no_space_left);
        }
        if (!(e instanceof NativeArchive.Failure)) {
            return getString(R.string.import_failed, e.getMessage());
        }

        NativeArchive.Failure failure = (NativeArchive.Failure) e;
        switch (failure.reason) {
        case NativeArchive.ENCRYPTED:
            return getString(R.string.import_encrypted);
        case NativeArchive.MULTI_VOLUME:
            return getString(R.string.import_multivolume);
        case NativeArchive.UNSUPPORTED:
            return getString(R.string.import_unsupported, failure.getMessage());
        case NativeArchive.CORRUPT:
            return getString(R.string.import_corrupt, failure.getMessage());
        case NativeArchive.NOT_SEEKABLE:
            return getString(R.string.import_not_seekable);
        default:
            return isVolumeName(name) ? getString(R.string.import_multivolume) : getString(R.string.import_unreadable);
        }
    }

    // Names of a split archive's parts: game.z01, game.7z.002, game.part2.rar,
    // game.r00.
    private static boolean isVolumeName(String name) {
        if (name == null) {
            return false;
        }
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.matches(".*\\.(z\\d{2,}|\\d{3}|r\\d{2})") || lower.matches(".*\\.part\\d+\\.rar");
    }

    private String displayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return cursor.getString(0);
            }
        } catch (RuntimeException ignored) {
        }
        return null;
    }

    // Where the removed files are: "data/sound (2165), appearance (12)" -
    // folders (two levels in data), the most first.
    private static String removedFolders(List<String> removed) {
        java.util.Map<String, Integer> counts = new java.util.HashMap<>();
        for (String path : removed) {
            String[] parts = path.split("/");
            String folder = parts.length == 1 ? path
                : parts[0].equalsIgnoreCase("data") && parts.length > 2 ? parts[0] + "/" + parts[1] : parts[0];
            Integer count = counts.get(folder);
            counts.put(folder, count == null ? 1 : count + 1);
        }
        List<java.util.Map.Entry<String, Integer>> sorted = new ArrayList<>(counts.entrySet());
        java.util.Collections.sort(sorted, (a, b) -> b.getValue() - a.getValue());
        StringBuilder text = new StringBuilder();
        for (int index = 0; index < sorted.size() && index < 5; index++) {
            if (text.length() > 0) {
                text.append(", ");
            }
            text.append(sorted.get(index).getKey()).append(" (").append(sorted.get(index).getValue()).append(')');
        }
        if (sorted.size() > 5) {
            text.append(", …");
        }
        return text.toString();
    }

    // "Games/Fallout 2/" -> "Games/Fallout 2".
    private static String folderName(String root) {
        return root.endsWith("/") ? root.substring(0, root.length() - 1) : root;
    }

    @Override
    public void onBackPressed() {
        // Copying stops with its button (what was copied is removed). Not
        // replacing after all: back to the game.
        if (busy) {
            return;
        }
        if (onBack != null) {
            Runnable back = onBack;
            onBack = null;
            back.run();
        } else if (replacing) {
            startGame();
        } else {
            super.onBackPressed();
        }
    }

    // What the game is made of now (written by the game, src/loadsave.cc).
    private String currentComposition() {
        try {
            return new String(GameSaves.readAll(new File(getFilesDir(), GameSaves.COMPOSITION)), java.nio.charset.StandardCharsets.UTF_8).trim();
        } catch (IOException e) {
            return "";
        }
    }

    // Its texts' code page (written by the game, src/loadsave.cc).
    private String currentCharset() {
        try {
            return new String(GameSaves.readAll(new File(getFilesDir(), GameSaves.CHARSET)), java.nio.charset.StandardCharsets.UTF_8).trim();
        } catch (IOException e) {
            return "";
        }
    }

    private File recordsFile() {
        return new File(getFilesDir(), GameSaves.RECORDS);
    }

    // The saves found: a summary, the list (newest first, marks of
    // compatibility), add them or replace all.
    private void showSaves(GameImport.Source source, GameSaves.Found found) {
        busy = false;
        progress.setIndeterminate(false);
        onBack = () -> {
            closeQuietly(source);
            showWelcome();
        };

        int[] counts = new int[4];
        int duplicates = 0;
        StringBuilder list = new StringBuilder();
        for (GameSaves.FoundSave save : found.saves) {
            if (save.duplicate) {
                duplicates++;
            } else {
                counts[Math.max(0, Math.min(3, save.level))]++;
            }
            String mark = save.level == GameSaves.UNLIKELY ? "×" : save.level == GameSaves.LIKELY ? "?" : save.level == GameSaves.UNKNOWN ? "·" : " ";
            String title = save.header.description.isEmpty() ? save.header.name : save.header.description;
            list.append(mark).append(' ').append(title);
            if (save.header.valid) {
                list.append(String.format(Locale.ROOT, "  %02d.%02d.%04d", save.header.fileDay, save.header.fileMonth, save.header.fileYear));
            }
            if (save.duplicate) {
                list.append("  (").append(getString(R.string.saves_already_there)).append(')');
            }
            list.append('\n');
            for (String[] change : save.changes) {
                list.append("    ").append(changeText(change)).append('\n');
            }
        }

        StringBuilder summary = new StringBuilder(getString(R.string.saves_found, found.saves.size()));
        int[] labels = { R.string.saves_count_same, R.string.saves_count_likely, R.string.saves_count_unlikely, R.string.saves_count_unknown };
        for (int level = 0; level < 4; level++) {
            if (counts[level] > 0) {
                summary.append('\n').append(getString(labels[level], counts[level]));
            }
        }
        if (duplicates > 0) {
            summary.append('\n').append(getString(R.string.saves_count_duplicate, duplicates));
        }

        boolean anyNew = duplicates < found.saves.size();
        show(summary.toString(), list.toString(),
            anyNew ? getString(R.string.saves_add) : null, v -> importSaves(source, found, false),
            getString(R.string.saves_replace_all), v -> confirmReplaceSaves(source, found));
    }

    private String changeText(String[] change) {
        String archive = change[1].substring(change[1].lastIndexOf('/') + 1);
        switch (change[0]) {
        case "0":
            return getString(R.string.saves_change_game);
        case "1":
            return getString(R.string.saves_change_updated, archive);
        case "2":
            return getString(R.string.saves_change_added, archive);
        default:
            return getString(R.string.saves_change_removed, archive);
        }
    }

    // Replacing all: the saves there go - said first, exporting them offered.
    private void confirmReplaceSaves(GameImport.Source source, GameSaves.Found found) {
        int current = GameSaves.installedSaves(getExternalFilesDir(null)).size();
        if (current == 0) {
            importSaves(source, found, true);
            return;
        }
        onBack = () -> showSaves(source, found);
        show(getString(R.string.saves_replace_confirm, current), null,
            getString(R.string.saves_replace), v -> importSaves(source, found, true),
            getString(R.string.saves_export_first), v -> startExport(() -> confirmReplaceSaves(source, found)));
    }

    private void importSaves(GameImport.Source source, GameSaves.Found found, boolean replaceAll) {
        File game = getExternalFilesDir(null);
        GameSaves.Plan plan;
        try {
            plan = GameSaves.plan(found, game, replaceAll);
        } catch (GameSaves.NoRoomException e) {
            onBack = () -> showSaves(source, found);
            show(getString(R.string.saves_no_room, e.needed, e.free), null, null, null, getString(R.string.import_choose_other), v -> showSaves(source, found));
            return;
        }

        busy = true;
        onBack = null;
        show(getString(R.string.saves_copying), null, null, null, null, null);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);

        new Thread(() -> {
            try {
                // The game waits behind, paused (it reads the saves again
                // when back).
                GameSaves.apply(source, plan, game, recordsFile(), (done, total) -> runOnUiThread(() -> {
                    progress.setIndeterminate(false);
                    progress.setFraction(total > 0 ? (float) done / total : 0.0f);
                }));
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "saves import failed", e);
                String text = problemText(e, null);
                runOnUiThread(() -> showError(text));
                return;
            } finally {
                closeQuietly(source);
            }

            int count = plan.saves.size();
            runOnUiThread(() -> showDone(getString(R.string.saves_done, count), null));
        }).start();
    }

    // Done: said for a moment, then on by itself (back to the game, or to
    // what [next] goes on with).
    private void showDone(String text, Runnable next) {
        busy = true;
        onBack = null;
        show(text, null, null, null, null, null);
        message.postDelayed(next != null ? next : this::startGame, 1500);
    }

    // Export: where to (the system's picker), then the zip; [after] - what
    // goes on after it (or back to the game).
    private void startExport(Runnable after) {
        afterExport = after;
        if (GameSaves.installedSaves(getExternalFilesDir(null)).isEmpty()) {
            busy = true;
            onBack = null;
            show(getString(R.string.saves_export_none), null, null, null, null, null);
            message.postDelayed(this::finishExport, 1500);
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        String date = new java.text.SimpleDateFormat("yyyy-MM-dd", Locale.ROOT).format(new java.util.Date());
        intent.putExtra(Intent.EXTRA_TITLE, getString(R.string.saves_export_name, date));
        startActivityForResult(intent, REQUEST_EXPORT);
    }

    private void export(Uri uri) {
        busy = true;
        onBack = null;
        show(getString(R.string.saves_exporting), null, null, null, null, null);
        progress.setVisibility(View.VISIBLE);
        progress.setIndeterminate(true);

        new Thread(() -> {
            int count;
            try {
                try (java.io.OutputStream out = getContentResolver().openOutputStream(uri, "w")) {
                    if (out == null) {
                        throw new IOException("can't write " + uri);
                    }
                    count = GameSaves.export(getExternalFilesDir(null), recordsFile(), currentComposition(), out, (done, total) -> runOnUiThread(() -> {
                        progress.setIndeterminate(false);
                        progress.setFraction(total > 0 ? (float) done / total : 0.0f);
                    }));
                }
            } catch (IOException | RuntimeException e) {
                Log.w(TAG, "saves export failed", e);
                String text = getString(R.string.import_failed, e.getMessage());
                runOnUiThread(() -> showError(text));
                return;
            }
            runOnUiThread(() -> {
                Runnable after = afterExport;
                afterExport = null;
                showDone(getString(R.string.saves_exported, count), after != null ? () -> {
                    busy = false;
                    after.run();
                } : null);
            });
        }).start();
    }

    // The export not made (picker closed): back to what was being done.
    private void finishExport() {
        if (afterExport != null) {
            Runnable after = afterExport;
            afterExport = null;
            after.run();
        } else {
            startGame();
        }
    }

    // "russian" of the game's config in the phone's language ("русский").
    private static String languageName(String language) {
        String code;
        switch (language.toLowerCase(Locale.ROOT)) {
        case "english":
            code = "en";
            break;
        case "russian":
            code = "ru";
            break;
        case "german":
            code = "de";
            break;
        case "french":
            code = "fr";
            break;
        case "italian":
            code = "it";
            break;
        case "spanish":
            code = "es";
            break;
        case "polish":
            code = "pl";
            break;
        case "portuguese":
            code = "pt";
            break;
        default:
            return language;
        }
        return new Locale(code).getDisplayLanguage();
    }

    private String size(long bytes) {
        return Formatter.formatShortFileSize(this, bytes);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        cancelled = true;
        if (openSource != null && !busy) {
            closeQuietly(openSource);
        }
    }

    private static void closeQuietly(GameImport.Source source) {
        if (source == null) {
            return;
        }
        try {
            source.close();
        } catch (IOException ignored) {
        }
    }
}
