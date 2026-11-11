package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.Dialog;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Outline;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.text.format.DateFormat;
import android.text.format.DateUtils;
import android.text.format.Formatter;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

// The app's list of games (Games.java): the games at the left, the one
// played last first, "Add game" under them; the chosen one's card at the
// right (its picture, its newest save, saves, size, language, mods) with
// Delete, Details and Play; Details on a page of their own (all of the game,
// Rename, Replace files). Opened from the game's main menu (over it, paused:
// back returns to it), or at the start instead of a game that failed at its
// last start. In its own process (":import", as the import screen): another
// game starts after the game played closes (GameProcess).
public class GamesActivity extends Activity {
    private static final String TAG = "GamesActivity";

    // The game the list was opened over (its id): back returns to it.
    static final String FROM_GAME = "from_game";
    // The game that failed at its last start (its id): the list says so.
    static final String FAILED = "failed";

    private static final int STYLE_NORMAL = 0;
    private static final int STYLE_PRIMARY = 1;
    private static final int STYLE_DANGER = 2;

    // What the list shows of a game beyond the registry, worked out in the
    // background.
    private static final class Stats {
        int saves;
        // Of all its files, -1: still counted (after every game's picture).
        long size = -1;
        String language;
        List<String> mods = new ArrayList<>();
        Map<String, String> card = new HashMap<>();
        Bitmap picture;
        Bitmap thumbnail;
    }

    private String fromGame;
    // The game that didn't reach its menu last time: said once the list is in.
    private String failedId;
    private String selectedId;
    private List<Games.Info> infos = new ArrayList<>();
    private final Map<String, Stats> stats = new HashMap<>();
    private boolean tablet;
    private boolean busy;
    private boolean details;
    private boolean cardLaidOut;
    private Typeface font;
    private Dialog dialog;
    // A tap past the window's panel closes it (not while a move is under way).
    private boolean dialogCancelable;

    private LinearLayout list;
    private ImageButton back;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_games);

        fromGame = getIntent().getStringExtra(FROM_GAME);
        selectedId = fromGame;
        tablet = getResources().getConfiguration().smallestScreenWidthDp >= 500;
        File gameFolder = null;
        try {
            gameFolder = Games.current(this).folder;
        } catch (IOException e) {
            Log.w(TAG, "no game played", e);
        }
        font = UiFont.typeface(this, gameFolder);

        list = findViewById(R.id.games_list);
        back = findViewById(R.id.games_back);
        back.setOnClickListener(v -> onBackPressed());
        findViewById(R.id.games_add).setOnClickListener(v -> showAdd());
        findViewById(R.id.games_location).setOnClickListener(v -> showLocation());
        findViewById(R.id.games_play).setOnClickListener(v -> play());
        findViewById(R.id.games_delete).setOnClickListener(v -> confirmDelete());
        findViewById(R.id.games_details).setOnClickListener(v -> showDetails(true));
        findViewById(R.id.games_rename).setOnClickListener(v -> showRename());
        findViewById(R.id.games_replace).setOnClickListener(v -> replaceFiles());

        layoutWindow();
        UiFont.apply(findViewById(R.id.games_root), font);
        centerAddButton();
        // Of one width while the labels fit, else by their lengths (as the
        // game's rows of tabs).
        ButtonRow.fit(findViewById(R.id.games_actions), dp(24));
        ButtonRow.fit(findViewById(R.id.games_details_actions), dp(24));
        // The pictures cut to their frames' rounded corners, the border over
        // them (games_frame_border).
        findViewById(R.id.games_cover).setClipToOutline(true);
        findViewById(R.id.games_details_picture).setClipToOutline(true);
        findViewById(R.id.games_details_left).addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) ->
            view.post(this::fitDetailsPicture));

        String failed = getIntent().getStringExtra(FAILED);
        if (failed != null && savedInstanceState == null) {
            selectedId = failed;
            failedId = failed;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        reload();
    }

    @Override
    protected void onDestroy() {
        if (dialog != null) {
            dialog.dismiss();
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (busy) {
            return;
        }
        if (details) {
            showDetails(false);
            return;
        }
        // To the game it was opened over (paused behind), else out.
        super.onBackPressed();
    }

    // Phones: the screen, the back button's column at its right edge (kept
    // when the button is hidden: nothing moves when it shows). Tablets: one
    // window in the middle (as the game's screens there), back beside it.
    // Both in the screen without the camera's cutout (SafeArea).
    private void layoutWindow() {
        SafeArea.apply(getWindow(), findViewById(R.id.games_root), false, this::placeWindow);
        View window = findViewById(R.id.games_window);
        if (!tablet) {
            window.setPaddingRelative(dp(10), dp(10), dp(72), dp(10));
            return;
        }
        placeWindow();
        findViewById(R.id.games_root).addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) {
                view.post(this::placeWindow);
            }
        });
        window.setBackgroundResource(R.drawable.import_panel);
        window.setPadding(dp(14), dp(14), dp(14), dp(14));

        // Roomier, as the save screen in a window.
        setWidth(R.id.games_left, 384);
        setWidth(R.id.games_details_left, 384);
        setHeight(R.id.games_bottom, 52);
        setHeight(R.id.games_actions, 52);
        ((TextView) findViewById(R.id.games_add)).setTextSize(18);
        ((TextView) findViewById(R.id.games_play)).setTextSize(18);
        ((TextView) findViewById(R.id.games_name)).setTextSize(24);
    }

    // Tablets: the window and its back button in the screen's safe area.
    private void placeWindow() {
        if (!tablet) {
            return;
        }
        float density = getResources().getDisplayMetrics().density;
        int[] area = safeArea();
        int screenWidth = Math.round(area[0] / density);
        int screenHeight = Math.round(area[1] / density);
        int margin = 24;
        int beside = 12 + 52;
        int width = Math.min(896, screenWidth - margin * 2 - beside);
        int height = Math.min(680, screenHeight - margin * 2);
        int left = (screenWidth - width) / 2;
        if (left + width + beside + margin > screenWidth) {
            left = screenWidth - margin - beside - width;
        }
        int top = (screenHeight - height) / 2;

        View window = findViewById(R.id.games_window);
        FrameLayout.LayoutParams windowParams = new FrameLayout.LayoutParams(dp(width), dp(height), Gravity.TOP | Gravity.START);
        windowParams.leftMargin = dp(left);
        windowParams.topMargin = dp(top);
        window.setLayoutParams(windowParams);

        FrameLayout.LayoutParams backParams = new FrameLayout.LayoutParams(dp(52), dp(52), Gravity.TOP | Gravity.START);
        backParams.leftMargin = dp(left + width + 12);
        backParams.topMargin = dp(top);
        back.setLayoutParams(backParams);
    }

    // The screen without its cutout (pixels): the list's root (the screen
    // before it is laid out) less its padding.
    private int[] safeArea() {
        View root = findViewById(R.id.games_root);
        boolean laidOut = root.getWidth() > 0 && root.getHeight() > 0;
        int width = laidOut ? root.getWidth() : getResources().getDisplayMetrics().widthPixels;
        int height = laidOut ? root.getHeight() : getResources().getDisplayMetrics().heightPixels;
        return new int[] { width - root.getPaddingLeft() - root.getPaddingRight(), height - root.getPaddingTop() - root.getPaddingBottom() };
    }

    // "Add game": its plus beside its text, the two in the middle, on one
    // line (the padding rounded down: the text keeps its whole width).
    private void centerAddButton() {
        Button add = findViewById(R.id.games_add);
        add.setCompoundDrawablePadding(dp(10));
        add.setSingleLine(true);
        add.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            android.graphics.drawable.Drawable plus = add.getCompoundDrawablesRelative()[0];
            float content = add.getPaint().measureText(add.getText().toString())
                + (plus != null ? plus.getIntrinsicWidth() + add.getCompoundDrawablePadding() : 0);
            int padding = Math.max(0, (int) Math.floor((right - left - content) / 2.0f) - 1);
            if (padding != add.getPaddingStart()) {
                add.setPaddingRelative(padding, 0, padding, 0);
            }
        });
    }

    // The card (import_panel): its padding, border and corners, dp.
    private static final int CARD_PADDING_DP = 8;
    private static final int CARD_BORDER_DP = 1;
    private static final int CARD_RADIUS_DP = 10;

    // The card's picture and lines: on phones the lines over the picture
    // (it fills the card, as large as can be); on tablets the picture 4:3
    // at the top, the lines under it (a tall card would cut the picture).
    private void layoutCard() {
        if (cardLaidOut) {
            return;
        }
        cardLaidOut = true;
        FrameLayout cover = findViewById(R.id.games_cover);
        TextView name = findViewById(R.id.games_name);
        LinearLayout info = findViewById(R.id.games_info);
        View top = findViewById(R.id.games_shade_top);
        View bottom = findViewById(R.id.games_shade_bottom);
        if (tablet) {
            top.setVisibility(View.GONE);
            bottom.setVisibility(View.GONE);
            View card = findViewById(R.id.games_card);
            card.post(() -> {
                int width = card.getWidth() - card.getPaddingLeft() - card.getPaddingRight();
                LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, width * 3 / 4);
                cover.setLayoutParams(params);
                View spare = findViewById(R.id.games_spare);
                spare.setVisibility(View.VISIBLE);
                spare.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
            });
            ((LinearLayout.LayoutParams) name.getLayoutParams()).topMargin = dp(10);
            name.setShadowLayer(0, 0, 0, 0);
            ((TextView) findViewById(R.id.games_last)).setTextColor(getColor(R.color.import_text));
            ((TextView) findViewById(R.id.games_last)).setTextSize(16);
            for (int id : new int[] { R.id.games_stats, R.id.games_mods }) {
                ((TextView) findViewById(id)).setTextColor(getColor(R.color.import_text_dim));
                ((TextView) findViewById(id)).setTextSize(14);
            }
            return;
        }
        if (name.getParent() == cover) {
            return;
        }
        // The picture to the card's border, cut to its rounded top corners:
        // one line around them, not the card's and the picture's frame side
        // by side. The buttons stay in the card under it, as the save
        // screen's.
        int inset = dp(CARD_PADDING_DP - CARD_BORDER_DP);
        LinearLayout.LayoutParams coverParams = (LinearLayout.LayoutParams) cover.getLayoutParams();
        coverParams.setMargins(-inset, -inset, -inset, 0);
        cover.setLayoutParams(coverParams);
        ((ViewGroup) cover.getParent()).setClipToPadding(false);
        cover.setBackgroundColor(Color.BLACK);
        cover.setForeground(null);
        cover.setPadding(0, 0, 0, 0);
        float radius = dp(CARD_RADIUS_DP - CARD_BORDER_DP);
        cover.setOutlineProvider(new ViewOutlineProvider() {
            @Override
            public void getOutline(View view, Outline outline) {
                // Square bottom corners: the rounded rectangle past them.
                outline.setRoundRect(0, 0, view.getWidth(), view.getHeight() + Math.round(radius), radius);
            }
        });
        ((LinearLayout.LayoutParams) findViewById(R.id.games_actions).getLayoutParams()).topMargin = dp(CARD_PADDING_DP);
        ((ViewGroup) name.getParent()).removeView(name);
        ((ViewGroup) info.getParent()).removeView(info);
        FrameLayout.LayoutParams nameParams = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP);
        nameParams.setMargins(dp(12), dp(8), dp(12), 0);
        cover.addView(name, nameParams);
        FrameLayout.LayoutParams infoParams = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM);
        infoParams.setMargins(dp(12), 0, dp(12), dp(9));
        cover.addView(info, infoParams);
    }

    // The games again (back from the import, from a deletion): the list,
    // then what each has, in the background.
    private void reload() {
        new Thread(() -> {
            List<Games.Info> loaded;
            try {
                Games.tidy(this);
                loaded = Games.list(this);
            } catch (IOException e) {
                Log.w(TAG, "no list of games", e);
                loaded = new ArrayList<>();
            }
            List<Games.Info> games = loaded;
            runOnUiThread(() -> {
                infos = games;
                if (infos.isEmpty()) {
                    // The last one deleted: the import, as at the first start.
                    startActivity(new Intent(this, ImportActivity.class));
                    finishAffinity();
                    return;
                }
                if (find(selectedId) == null) {
                    selectedId = infos.get(0).game.id;
                }
                refresh();
                // Named once the list is known.
                if (failedId != null) {
                    showFailed(failedId);
                    failedId = null;
                }
            });
            // The pictures first, the one chosen before the others: a
            // game's size is a walk over all its files (seconds in
            // Android/media), its picture shows without it.
            List<Games.Info> ordered = new ArrayList<>(games);
            String chosen = selectedId;
            for (int index = 0; index < ordered.size(); index++) {
                if (ordered.get(index).game.id.equals(chosen)) {
                    ordered.add(0, ordered.remove(index));
                    break;
                }
            }
            List<Stats> found = new ArrayList<>();
            for (Games.Info info : ordered) {
                Stats one = computeStats(info);
                found.add(one);
                runOnUiThread(() -> {
                    stats.put(info.game.id, one);
                    refresh();
                });
            }
            for (int index = 0; index < ordered.size(); index++) {
                Stats one = found.get(index);
                long size = size(ordered.get(index).game.folder);
                runOnUiThread(() -> {
                    one.size = size;
                    refresh();
                });
            }
        }, "games-load").start();
    }

    private Stats computeStats(Games.Info info) {
        Stats found = new Stats();
        File folder = info.game.folder;
        found.saves = GameSaves.installedSaves(folder).size();
        try {
            File config = GameFiles.configFile(folder);
            found.language = config.isFile() ? GameSaves.configValue(GameFiles.readText(config), "system", "language") : null;
        } catch (IOException e) {
            found.language = null;
        }
        if (found.language == null || found.language.isEmpty()) {
            found.language = "english";
        }
        File[] mods = new File(folder, "mods").listFiles();
        if (mods != null) {
            for (File mod : mods) {
                String name = mod.getName();
                if (mod.isDirectory() || name.toLowerCase(Locale.ROOT).endsWith(".dat")) {
                    found.mods.add(name);
                }
            }
            Collections.sort(found.mods, String.CASE_INSENSITIVE_ORDER);
        }
        found.card = Games.card(info.game);
        File picture = Games.cardPicture(info.game);
        if (picture.isFile()) {
            found.picture = decode(picture, 1280);
            found.thumbnail = thumbnail(found.picture, 320);
        }
        return found;
    }

    // [file] no wider than [width] (a power of two smaller).
    private static Bitmap decode(File file, int width) {
        BitmapFactory.Options bounds = new BitmapFactory.Options();
        bounds.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.getPath(), bounds);
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 1;
        while (bounds.outWidth / (options.inSampleSize * 2) >= width) {
            options.inSampleSize *= 2;
        }
        return BitmapFactory.decodeFile(file.getPath(), options);
    }

    // [picture] no wider than [width] (once decoded, not twice).
    private static Bitmap thumbnail(Bitmap picture, int width) {
        if (picture == null || picture.getWidth() <= width) {
            return picture;
        }
        int height = Math.max(1, Math.round((float) picture.getHeight() * width / picture.getWidth()));
        return Bitmap.createScaledBitmap(picture, width, height, true);
    }

    private static long size(File file) {
        return FileTree.size(file);
    }

    private Games.Info find(String id) {
        for (Games.Info info : infos) {
            if (info.game.id.equals(id)) {
                return info;
            }
        }
        return null;
    }

    private void refresh() {
        layoutCard();
        bindList();
        bindCard();
        if (details) {
            bindDetails();
        }
        back.setVisibility(details || fromGame != null ? View.VISIBLE : View.GONE);
    }

    private void bindList() {
        list.removeAllViews();
        LayoutInflater inflater = LayoutInflater.from(this);
        for (Games.Info info : infos) {
            View row = inflater.inflate(R.layout.row_game, list, false);
            boolean chosen = info.game.id.equals(selectedId);
            row.setSelected(chosen);
            TextView name = row.findViewById(R.id.game_name);
            name.setText(Games.displayName(this, info));
            name.setTextColor(getColor(chosen ? R.color.import_accent : R.color.import_text));
            ((TextView) row.findViewById(R.id.game_when)).setText(whenPlayed(info));
            row.findViewById(R.id.game_badge).setVisibility(info.game.id.equals(fromGame) ? View.VISIBLE : View.GONE);
            Stats found = stats.get(info.game.id);
            CoverImageView thumb = row.findViewById(R.id.game_thumb);
            thumb.setAlwaysFill(true);
            row.findViewById(R.id.game_thumb_frame).setClipToOutline(true);
            Bitmap thumbnail = found != null ? found.thumbnail : null;
            thumb.setImageBitmap(thumbnail);
            row.findViewById(R.id.game_no_thumb).setVisibility(found != null && thumbnail == null ? View.VISIBLE : View.GONE);
            if (tablet) {
                row.getLayoutParams().height = dp(80);
                View frame = row.findViewById(R.id.game_thumb_frame);
                frame.getLayoutParams().width = dp(104);
                frame.getLayoutParams().height = dp(65);
                name.setTextSize(16);
                ((TextView) row.findViewById(R.id.game_when)).setTextSize(14);
            }
            row.setOnClickListener(v -> {
                selectedId = info.game.id;
                refresh();
            });
            UiFont.apply(row, font);
            list.addView(row);
        }
    }

    private void bindCard() {
        Games.Info info = find(selectedId);
        if (info == null) {
            return;
        }
        Stats found = stats.get(info.game.id);
        ImageView picture = findViewById(R.id.games_picture);
        Bitmap bitmap = found != null ? found.picture : null;
        picture.setImageBitmap(bitmap);
        findViewById(R.id.games_no_picture).setVisibility(found != null && bitmap == null ? View.VISIBLE : View.GONE);

        ((TextView) findViewById(R.id.games_name)).setText(Games.displayName(this, info));
        TextView last = findViewById(R.id.games_last);
        TextView statsLine = findViewById(R.id.games_stats);
        TextView mods = findViewById(R.id.games_mods);
        if (found == null) {
            last.setText(null);
            statsLine.setText(null);
            mods.setText(null);
            return;
        }
        String place = found.card.get("place");
        last.setText(found.saves == 0 ? getString(R.string.games_no_saves)
            : place != null && !place.isEmpty() ? getString(R.string.games_last_save, place) : null);
        last.setVisibility(last.getText() == null || last.getText().length() == 0 ? View.GONE : View.VISIBLE);
        statsLine.setText(statsText(found));
        mods.setText(modsText(found.mods));
    }

    // "24 saves · 1.6 GB · Russian".
    private String statsText(Stats found) {
        List<String> parts = new ArrayList<>();
        if (found.saves > 0) {
            parts.add(getResources().getQuantityString(R.plurals.games_saves, found.saves, found.saves));
        }
        if (found.size >= 0) {
            parts.add(Formatter.formatShortFileSize(this, found.size));
        }
        parts.add(ImportActivity.languageName(found.language));
        return String.join(" · ", parts);
    }

    private String modsText(List<String> mods) {
        if (mods.isEmpty()) {
            return getString(R.string.games_mods_none);
        }
        if (mods.size() == 1) {
            return getString(R.string.games_mods_one, mods.get(0));
        }
        return getString(R.string.games_mods_more, mods.get(0), mods.size() - 1);
    }

    // "Today, 14:20", "Yesterday, 22:41", "3 November", "Not started yet".
    private String whenPlayed(Games.Info info) {
        if (info.played <= 0) {
            return getString(R.string.games_never_played);
        }
        String time = DateFormat.getTimeFormat(this).format(new Date(info.played));
        if (DateUtils.isToday(info.played)) {
            return getString(R.string.games_played_today, time);
        }
        if (DateUtils.isToday(info.played + DateUtils.DAY_IN_MILLIS)) {
            return getString(R.string.games_played_yesterday, time);
        }
        String pattern = DateFormat.getBestDateTimePattern(Locale.getDefault(), "dMMMM");
        return DateFormat.format(pattern, info.played).toString();
    }

    private void showDetails(boolean show) {
        details = show;
        findViewById(R.id.games_page).setVisibility(show ? View.GONE : View.VISIBLE);
        findViewById(R.id.games_details_page).setVisibility(show ? View.VISIBLE : View.GONE);
        refresh();
    }

    // All of the game: its picture whole, its version line, the numbers,
    // every mod.
    private void bindDetails() {
        Games.Info info = find(selectedId);
        if (info == null) {
            showDetails(false);
            return;
        }
        Stats found = stats.get(info.game.id);
        ImageView picture = findViewById(R.id.games_details_picture);
        picture.setImageBitmap(found != null ? found.picture : null);
        picture.setVisibility(found != null && found.picture != null ? View.VISIBLE : View.GONE);
        picture.post(this::fitDetailsPicture);
        ((TextView) findViewById(R.id.games_details_name)).setText(Games.displayName(this, info));
        String version = found != null ? found.card.get("version") : null;
        TextView versionView = findViewById(R.id.games_details_version);
        versionView.setText(version);
        versionView.setVisibility(version != null && !version.isEmpty() ? View.VISIBLE : View.GONE);

        LinearLayout rows = findViewById(R.id.games_details_info);
        rows.removeAllViews();
        if (found != null) {
            addRow(rows, R.string.games_label_saves, Integer.toString(found.saves));
            addRow(rows, R.string.games_label_size, found.size >= 0 ? Formatter.formatShortFileSize(this, found.size) : "…");
            addRow(rows, R.string.games_label_language, ImportActivity.languageName(found.language));
        }
        if (info.added > 0) {
            addRow(rows, R.string.games_label_added, java.text.DateFormat.getDateInstance(java.text.DateFormat.LONG).format(new Date(info.added)));
        }
        addRow(rows, R.string.games_label_played, whenPlayed(info));
        boolean canOpen = Games.canOpen(this);
        if (canOpen) {
            addRow(rows, R.string.games_label_folder, getString(info.open ? R.string.games_folder_open_short : R.string.games_folder_hidden_short));
        }
        Button location = findViewById(R.id.games_location);
        location.setVisibility(canOpen ? View.VISIBLE : View.GONE);
        location.setText(info.open ? R.string.games_folder_make_hidden : R.string.games_folder_make_open);
        if (found != null && !found.mods.isEmpty()) {
            TextView title = text(getString(R.string.games_mods_title, found.mods.size()), 13, R.color.import_accent);
            title.setPadding(0, dp(8), 0, dp(2));
            rows.addView(title);
            for (String mod : found.mods) {
                rows.addView(text(mod, 12, R.color.import_text));
            }
        }
        UiFont.apply(findViewById(R.id.games_details_page), font);
    }

    // The details' picture as large as the panel leaves it beside the name
    // and version line, its proportions kept.
    private void fitDetailsPicture() {
        View panel = findViewById(R.id.games_details_left);
        ImageView picture = findViewById(R.id.games_details_picture);
        TextView name = findViewById(R.id.games_details_name);
        TextView version = findViewById(R.id.games_details_version);
        int width = panel.getWidth() - panel.getPaddingLeft() - panel.getPaddingRight();
        int height = panel.getHeight() - panel.getPaddingTop() - panel.getPaddingBottom()
            - name.getHeight() - ((LinearLayout.LayoutParams) name.getLayoutParams()).topMargin
            - (version.getVisibility() == View.VISIBLE ? version.getHeight() : 0);
        android.graphics.drawable.Drawable drawable = picture.getDrawable();
        if (width <= 0 || height <= 0 || drawable == null || drawable.getIntrinsicWidth() <= 0 || drawable.getIntrinsicHeight() <= 0) {
            return;
        }
        // Up or down to the room (the frame's border inside it).
        int border = picture.getPaddingLeft() + picture.getPaddingRight();
        float scale = Math.min((float) (width - border) / drawable.getIntrinsicWidth(), (float) (height - border) / drawable.getIntrinsicHeight());
        int shownWidth = Math.round(drawable.getIntrinsicWidth() * scale) + border;
        int shownHeight = Math.round(drawable.getIntrinsicHeight() * scale) + border;
        ViewGroup.LayoutParams params = picture.getLayoutParams();
        if (params.width != shownWidth || params.height != shownHeight) {
            params.width = shownWidth;
            params.height = shownHeight;
            picture.setLayoutParams(params);
        }
    }

    private void addRow(LinearLayout rows, int label, String value) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        TextView key = text(getString(label), 13, R.color.import_text_dim);
        row.addView(key, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        TextView text = text(value, 13, R.color.import_text);
        text.setGravity(Gravity.END);
        row.addView(text, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        rows.addView(row);
    }

    private TextView text(String value, int size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, tablet ? size + 2 : size);
        view.setTextColor(getColor(color));
        view.setPadding(0, dp(2), 0, dp(2));
        view.setTypeface(font);
        return view;
    }

    // The chosen game starts: the one the list was opened over, back to it;
    // another, after the one running closes.
    private void play() {
        Games.Info info = find(selectedId);
        if (info == null || busy) {
            return;
        }
        if (info.game.id.equals(fromGame)) {
            finish();
            return;
        }
        busy = true;
        new Thread(() -> {
            boolean closed = GameProcess.close(this);
            if (closed) {
                try {
                    Games.setCurrent(this, info.game.id);
                } catch (IOException e) {
                    Log.w(TAG, "game not chosen", e);
                    closed = false;
                }
                // Tried again from here: a start of its own.
                //noinspection ResultOfMethodCallIgnored
                Games.startingMark(this).delete();
            }
            boolean started = closed;
            runOnUiThread(() -> {
                busy = false;
                if (!started) {
                    showNotice(getString(R.string.games_failed_action), null);
                    return;
                }
                startActivity(new Intent(this, MainActivity.class));
                finishAffinity();
            });
        }, "games-play").start();
    }

    // Folder or archive, then the import (into a new game's folder).
    private void showAdd() {
        showDialog(getString(R.string.games_add), getString(R.string.games_add_text), getString(R.string.games_add_note),
            true, null,
            new Action(getString(R.string.import_choose_folder), STYLE_PRIMARY, () -> add(ImportActivity.PICK_FOLDER)),
            new Action(getString(R.string.import_choose_archive), STYLE_PRIMARY, () -> add(ImportActivity.PICK_ARCHIVE)));
    }

    private void add(String pick) {
        try {
            Games.Game game = Games.create(this);
            startActivity(new Intent(this, ImportActivity.class)
                .putExtra(ImportActivity.GAME, game.id)
                .putExtra(ImportActivity.ADD, true)
                .putExtra(ImportActivity.PICK, pick));
        } catch (IOException e) {
            Log.w(TAG, "no new game", e);
            showNotice(getString(R.string.games_failed_action), null);
        }
    }

    // The game's files and saves deleted (export offered), after it closes
    // when it runs.
    private void confirmDelete() {
        Games.Info info = find(selectedId);
        if (info == null) {
            return;
        }
        Stats found = stats.get(info.game.id);
        int saves = found != null ? found.saves : 0;
        String size = found != null && found.size >= 0 ? Formatter.formatShortFileSize(this, found.size) : "?";
        String text = getString(R.string.games_delete_text, size, getResources().getQuantityString(R.plurals.games_saves, saves, saves));
        List<Action> actions = new ArrayList<>();
        actions.add(new Action(getString(R.string.games_cancel), STYLE_NORMAL, null));
        if (saves > 0) {
            actions.add(new Action(getString(R.string.games_export_saves), STYLE_NORMAL, () -> startActivity(new Intent(this, ImportActivity.class)
                .putExtra(ImportActivity.GAME, info.game.id)
                .putExtra(ImportActivity.MODE, ImportActivity.MODE_SAVES_EXPORT))));
        }
        actions.add(new Action(getString(R.string.games_delete), STYLE_DANGER, () -> delete(info)));
        showDialog(getString(R.string.games_delete_title, Games.displayName(this, info)), text,
            saves > 0 ? getString(R.string.games_delete_note) : null, false, null, actions.toArray(new Action[0]));
    }

    private void delete(Games.Info info) {
        busy = true;
        new Thread(() -> {
            boolean done = true;
            try {
                if (info.game.id.equals(Games.current(this).id) && !GameProcess.close(this)) {
                    done = false;
                } else {
                    Games.delete(this, info.game.id);
                }
            } catch (IOException e) {
                Log.w(TAG, "game not deleted", e);
                done = false;
            }
            boolean deleted = done;
            runOnUiThread(() -> {
                busy = false;
                if (!deleted) {
                    showNotice(getString(R.string.games_failed_action), null);
                    return;
                }
                if (info.game.id.equals(fromGame)) {
                    fromGame = null;
                }
                stats.remove(info.game.id);
                selectedId = null;
                showDetails(false);
                reload();
            });
        }, "games-delete").start();
    }

    private void showRename() {
        Games.Info info = find(selectedId);
        if (info == null) {
            return;
        }
        showDialog(getString(R.string.games_rename_title), null, null, false, Games.displayName(this, info),
            new Action(getString(R.string.games_cancel), STYLE_NORMAL, null),
            new Action(getString(R.string.games_save), STYLE_PRIMARY, null));
    }

    private void rename(String id, String name) {
        try {
            Games.rename(this, id, name);
        } catch (IOException e) {
            Log.w(TAG, "game not renamed", e);
        }
        reload();
    }

    // The game's files replaced (a mod's new version): its saves stay.
    private void replaceFiles() {
        Games.Info info = find(selectedId);
        if (info != null) {
            startActivity(new Intent(this, ImportActivity.class).putExtra(ImportActivity.GAME, info.game.id));
        }
    }

    // Where the chosen game's folder is: hidden in the app (the fastest
    // storage), or open to other apps (file managers, Syncthing, a computer
    // over USB; slower) - what each gives, and the move to the other.
    private void showLocation() {
        Games.Info info = find(selectedId);
        if (busy || info == null) {
            return;
        }
        String message = info.open ? getString(R.string.games_folder_open, shownPath(info.game.folder)) : getString(R.string.games_folder_hidden);
        showDialog(getString(R.string.games_folder_title), message, getString(R.string.games_folder_note), true, null,
            new Action(getString(info.open ? R.string.games_folder_make_hidden : R.string.games_folder_make_open), STYLE_PRIMARY,
                () -> moveGame(info.game.id, !info.open)));
    }

    // "Android/media/<app>/games/<id>": the path as file managers show it.
    private static String shownPath(File folder) {
        String path = folder.getPath();
        int android = path.indexOf("/Android/");
        return android != -1 ? path.substring(android + 1) : path;
    }

    // The game's folder moved (the game closes first when it's the one
    // running: its files move), with the progress.
    private void moveGame(String id, boolean open) {
        busy = true;
        View view = showDialog(getString(R.string.games_folder_moving), " ", null, false, null);
        lockDialog();
        TextView progress = view.findViewById(R.id.dialog_message);
        new Thread(() -> {
            String problem = null;
            boolean running = false;
            try {
                running = id.equals(Games.current(this).id);
            } catch (IOException e) {
                Log.w(TAG, "no game played", e);
            }
            if (running && !GameProcess.close(this)) {
                problem = getString(R.string.games_failed_action);
            } else {
                try {
                    long[] shown = { 0 };
                    Games.setOpen(this, id, open, (done, total) -> {
                        long now = System.currentTimeMillis();
                        if (now - shown[0] < 100 && done < total) {
                            return;
                        }
                        shown[0] = now;
                        runOnUiThread(() -> progress.setText(getString(R.string.games_folder_progress,
                            Formatter.formatShortFileSize(this, done), Formatter.formatShortFileSize(this, total))));
                    });
                } catch (Games.NoSpaceException e) {
                    problem = getString(R.string.games_folder_no_space, Formatter.formatShortFileSize(this, e.needed));
                } catch (IOException e) {
                    Log.w(TAG, "game not moved", e);
                    problem = getString(R.string.games_failed_action);
                }
            }
            String shownProblem = problem;
            boolean closed = running;
            runOnUiThread(() -> {
                busy = false;
                // The game the list was opened over has closed.
                if (closed) {
                    fromGame = null;
                }
                if (dialog != null) {
                    dialog.dismiss();
                }
                if (shownProblem != null) {
                    showNotice(shownProblem, null);
                }
                reload();
            });
        }, "games-move").start();
    }

    private void showFailed(String id) {
        Games.Info info = find(id);
        String name = info != null ? Games.displayName(this, info) : getString(R.string.games_unnamed, id);
        showNotice(getString(R.string.games_failed_title, name), getString(R.string.games_failed_text));
    }

    private void showNotice(String title, String message) {
        showDialog(title, message, null, false, null, new Action(getString(R.string.games_ok), STYLE_PRIMARY, null));
    }

    private static final class Action {
        final String label;
        final int style;
        final Runnable run;

        Action(String label, int style, Runnable run) {
            this.label = label;
            this.style = style;
            this.run = run;
        }
    }

    // A window over the list: [withBack] its back button (closing it, as the
    // game's windows, placeDialog), [input] a field with that text (renaming:
    // the last action saves it).
    private View showDialog(String title, String message, String note, boolean withBack, String input, Action... actions) {
        if (dialog != null) {
            dialog.dismiss();
        }
        View view = LayoutInflater.from(this).inflate(R.layout.dialog_games, null);
        ((TextView) view.findViewById(R.id.dialog_title)).setText(title);
        TextView messageView = view.findViewById(R.id.dialog_message);
        messageView.setText(message);
        messageView.setVisibility(message != null ? View.VISIBLE : View.GONE);
        TextView noteView = view.findViewById(R.id.dialog_note);
        noteView.setText(note);
        noteView.setVisibility(note != null ? View.VISIBLE : View.GONE);
        EditText field = view.findViewById(R.id.dialog_input);
        if (input != null) {
            field.setVisibility(View.VISIBLE);
            field.setText(input);
            field.setSelection(input.length());
        }
        Dialog shown = new Dialog(this);
        shown.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialogCancelable = true;
        view.setOnClickListener(v -> {
            if (dialogCancelable) {
                shown.cancel();
            }
        });
        ImageButton backButton = view.findViewById(R.id.dialog_back);
        backButton.setVisibility(withBack ? View.VISIBLE : View.GONE);
        backButton.setOnClickListener(v -> shown.dismiss());
        placeDialog(view, withBack);

        LinearLayout buttons = view.findViewById(R.id.dialog_buttons);
        String renamed = input != null ? selectedId : null;
        for (int index = 0; index < actions.length; index++) {
            Action action = actions[index];
            Button button = new Button(this);
            button.setText(action.label);
            button.setAllCaps(false);
            button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            button.setBackgroundResource(action.style == STYLE_PRIMARY ? R.drawable.import_button_primary
                : action.style == STYLE_DANGER ? R.drawable.import_button_danger : R.drawable.import_button);
            button.setTextColor(getColor(action.style == STYLE_PRIMARY ? R.color.import_button_primary_text
                : action.style == STYLE_DANGER ? R.color.import_danger_text : R.color.import_button_text));
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1);
            if (index > 0) {
                params.setMarginStart(dp(8));
            }
            boolean last = index == actions.length - 1;
            button.setOnClickListener(v -> {
                shown.dismiss();
                if (renamed != null && last) {
                    rename(renamed, field.getText().toString());
                } else if (action.run != null) {
                    action.run.run();
                }
            });
            buttons.addView(button, params);
        }
        ButtonRow.fit(buttons, dp(24));
        UiFont.apply(view, font);

        shown.setContentView(view);
        Window window = shown.getWindow();
        if (window != null) {
            // The dim drawn by the window itself (the system's isn't, behind
            // a window over the whole screen).
            window.setBackgroundDrawable(new ColorDrawable(Color.argb(150, 0, 0, 0)));
            window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            SafeArea.apply(window, view, true, null);
        }
        dialog = shown;
        shown.show();
        return view;
    }

    // The window's panel and back button (see dialog_games.xml), the panel as
    // wide as there is room for, at most 500dp.
    private void placeDialog(View view, boolean withBack) {
        FrameLayout root = view.findViewById(R.id.dialog_root);
        View area = view.findViewById(R.id.dialog_area);
        ImageButton backButton = view.findViewById(R.id.dialog_back);
        int room = safeArea()[0];
        if (tablet) {
            area.setPadding(dp(16), dp(16), dp(16), dp(16));
            view.findViewById(R.id.dialog_spacer).setVisibility(withBack ? View.VISIBLE : View.GONE);
            room -= dp(32) + (withBack ? dp(120) : 0);
        } else {
            area.setPaddingRelative(dp(10), dp(10), withBack ? dp(72) : dp(10), dp(10));
            room -= dp(20) + (withBack ? dp(62) : 0);
            if (withBack) {
                ((ViewGroup) backButton.getParent()).removeView(backButton);
                FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(dp(52), dp(52), Gravity.TOP | Gravity.END);
                params.setMargins(dp(10), dp(10), dp(10), dp(10));
                root.addView(backButton, params);
            }
        }
        View panel = view.findViewById(R.id.dialog_panel);
        panel.getLayoutParams().width = Math.min(dp(500), Math.max(room, dp(240)));
    }

    // The window up can't be closed (a move under way).
    private void lockDialog() {
        dialogCancelable = false;
        if (dialog != null) {
            dialog.setCancelable(false);
        }
    }

    private void setWidth(int id, int widthDp) {
        View view = findViewById(id);
        view.getLayoutParams().width = dp(widthDp);
        view.setLayoutParams(view.getLayoutParams());
    }

    private void setHeight(int id, int heightDp) {
        View view = findViewById(id);
        view.getLayoutParams().height = dp(heightDp);
        view.setLayoutParams(view.getLayoutParams());
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
