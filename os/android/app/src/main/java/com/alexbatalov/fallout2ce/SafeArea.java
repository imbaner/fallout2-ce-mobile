package com.alexbatalov.fallout2ce;

import android.graphics.Insets;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

// The screen without the camera's cutout, as the game lays its screens out
// (hud_layout.cc: the cutout's safe insets, the game drawn under the cutout,
// SDLActivity's LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES): the background
// fills the screen, what is on it stays off the cutout. Off the system's
// bars too where they are over the app (Android 15 and later lay apps out
// under them: the navigation buttons, the gesture bar).
final class SafeArea {
    private SafeArea() {
    }

    // [root] padded by the insets of [window], [changed] run when they change
    // (the window laid out again in what is left). [whole]: the window over
    // the whole screen, its bars included (a window over the list: its dim
    // covers all).
    static void apply(Window window, View root, boolean whole, Runnable changed) {
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            if (whole && Build.VERSION.SDK_INT >= 30) {
                attributes.setFitInsetsTypes(0);
            }
            window.setAttributes(attributes);
        }
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            int[] safe = insets(insets);
            if (safe[0] != view.getPaddingLeft() || safe[1] != view.getPaddingTop()
                || safe[2] != view.getPaddingRight() || safe[3] != view.getPaddingBottom()) {
                view.setPadding(safe[0], safe[1], safe[2], safe[3]);
                if (changed != null) {
                    view.post(changed);
                }
            }
            return insets;
        });
        root.requestApplyInsets();
    }

    // Left, top, right, bottom in pixels.
    private static int[] insets(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) {
            Insets safe = insets.getInsets(WindowInsets.Type.displayCutout() | WindowInsets.Type.systemBars());
            return new int[] { safe.left, safe.top, safe.right, safe.bottom };
        }
        if (Build.VERSION.SDK_INT >= 28) {
            DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null) {
                return new int[] { cutout.getSafeInsetLeft(), cutout.getSafeInsetTop(), cutout.getSafeInsetRight(), cutout.getSafeInsetBottom() };
            }
        }
        return new int[4];
    }
}
