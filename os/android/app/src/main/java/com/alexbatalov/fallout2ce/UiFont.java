package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.graphics.Typeface;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;

// The app's own screens in the game's font (TouchSettings: Classic by default,
// PT Mono when the player chose it).
final class UiFont {
    private UiFont() {
    }

    // [gameFolder]: an earlier build's choice in that game's config, where
    // the port's settings (Games.PORT_SETTINGS) don't have it yet.
    static Typeface typeface(Context context, File gameFolder) {
        boolean mono = false;
        try {
            File config = Games.portSettings(context);
            String value = config.isFile() ? GameSaves.configValue(GameFiles.readText(config), "touch", "font") : null;
            if (value == null && gameFolder != null) {
                config = GameFiles.configFile(gameFolder);
                value = config.isFile() ? GameSaves.configValue(GameFiles.readText(config), "touch", "font") : null;
            }
            mono = "1".equals(value);
        } catch (IOException ignored) {
            // An absent/unreadable first-run config uses the game's default.
        }
        try {
            return Typeface.createFromAsset(context.getAssets(), "ce.dat/fonts/" + (mono ? "PTM55FT.ttf" : "FalloutClassicDialog.ttf"));
        } catch (RuntimeException e) {
            return Typeface.MONOSPACE;
        }
    }

    static void apply(View view, Typeface font) {
        if (view instanceof TextView) {
            ((TextView) view).setTypeface(font);
        } else if (view instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) view;
            for (int index = 0; index < group.getChildCount(); index++) {
                apply(group.getChildAt(index), font);
            }
        }
    }
}
