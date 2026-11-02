package com.alexbatalov.fallout2ce;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.UnsupportedEncodingException;
import java.net.URLEncoder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

// The problem window the game can't draw itself (error_report.h): it
// stopped showing frames. Its texts are the game's own (in the game's
// language), prepared by it before (MainActivity.nativeReportTexts): what
// happened, the report as it goes, the channels - the Google Form (no
// account), GitHub and Telegram (an account), the text's copy.
final class ProblemDialog {
    private static final String TAG = "Fallout2";

    private ProblemDialog() {
    }

    // [loading]: the game stopped while loading. [onWait] - the player waits
    // for it, [onClose] - closes it.
    static AlertDialog hang(Activity activity, String textsJson, boolean loading, Runnable onWait, Runnable onClose) {
        JSONObject texts;
        try {
            texts = new JSONObject(textsJson);
        } catch (JSONException e) {
            Log.w(TAG, "No problem texts", e);
            return null;
        }
        String title = texts.optString("hangTitle");
        String report = texts.optString("hangReport").replace("{state}", loading ? "loading" : "playing");

        float density = activity.getResources().getDisplayMetrics().density;
        int padding = Math.round(20 * density);
        LinearLayout layout = new LinearLayout(activity);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(padding, Math.round(8 * density), padding, 0);

        TextView message = new TextView(activity);
        message.setText(texts.optString("hang"));
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        layout.addView(message);

        // The report as it goes, scrolling.
        TextView body = new TextView(activity);
        body.setText(report);
        body.setTypeface(Typeface.MONOSPACE);
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        body.setTextIsSelectable(true);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(body);
        int maxHeight = activity.getResources().getDisplayMetrics().heightPixels / 4;
        LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, maxHeight);
        scrollParams.topMargin = Math.round(8 * density);
        layout.addView(scroll, scrollParams);

        LinearLayout channels = new LinearLayout(activity);
        channels.setOrientation(LinearLayout.HORIZONTAL);
        String form = texts.optString("form");
        if (!form.isEmpty()) {
            addChannel(channels, texts.optString("formLabel"), texts.optString("noAccount"), () -> open(activity, form + encode(fittedText(report, URL_LIMIT - form.length()))));
        }
        String fence = encode("```\n");
        String githubPrefix = texts.optString("github") + "?title=" + encode(title) + "&body=" + fence;
        String github = githubPrefix + encode(fittedText(report, URL_LIMIT - githubPrefix.length() - fence.length())) + fence;
        addChannel(channels, "GitHub", texts.optString("account"), () -> open(activity, github));
        Button copy = new Button(activity);
        Runnable copyReport = () -> {
            ClipboardManager clipboard = (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
            clipboard.setPrimaryClip(ClipData.newPlainText(title, report));
            copy.setText(texts.optString("copied"));
        };
        // The channel's direct messages; Telegram may leave the draft out,
        // so the report is copied first (as the game's window does).
        String telegram = texts.optString("telegram");
        addChannel(channels, "Telegram", texts.optString("account"), () -> {
            copyReport.run();
            open(activity, telegram + encode(fittedText(report, URL_LIMIT - telegram.length())));
        });
        layout.addView(channels);

        copy.setText(texts.optString("copy"));
        copy.setOnClickListener(v -> copyReport.run());
        layout.addView(copy);

        return new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(title)
            .setView(layout)
            .setNegativeButton(texts.optString("wait"), (dialog, which) -> onWait.run())
            .setPositiveButton(texts.optString("close"), (dialog, which) -> onClose.run())
            .setOnCancelListener(dialog -> onWait.run())
            .create();
    }

    // A channel's button, what it needs under it.
    private static void addChannel(LinearLayout row, String label, String caption, Runnable action) {
        Context context = row.getContext();
        LinearLayout cell = new LinearLayout(context);
        cell.setOrientation(LinearLayout.VERTICAL);
        Button button = new Button(context);
        button.setText(label);
        button.setAllCaps(false);
        button.setOnClickListener(v -> action.run());
        cell.addView(button);
        TextView note = new TextView(context);
        note.setText(caption);
        note.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        note.setGravity(Gravity.CENTER_HORIZONTAL);
        cell.addView(note);
        row.addView(cell, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f));
    }

    private static void open(Activity activity, String url) {
        try {
            activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "No browser", e);
        }
    }

    // Google Forms and GitHub refuse links longer than about 8 KB (a letter
    // of another alphabet takes 6 characters in a link): what doesn't fit
    // goes from the oldest events, then from the end (as the game's links do).
    static final int URL_LIMIT = 8000;

    static String fittedText(String text, int room) {
        List<String> lines = new ArrayList<>(Arrays.asList(text.split("\n", -1)));
        while (!lines.isEmpty() && encode(String.join("\n", lines)).length() > room) {
            int event = -1;
            for (int index = 0; index < lines.size() && event < 0; index++) {
                event = lines.get(index).startsWith("- ") ? index : -1;
            }
            lines.remove(event >= 0 ? event : Math.max(lines.size() - 2, 0));
        }
        return String.join("\n", lines);
    }

    static String encode(String value) {
        try {
            return URLEncoder.encode(value, "UTF-8").replace("+", "%20");
        } catch (UnsupportedEncodingException e) {
            throw new AssertionError(e);
        }
    }
}
