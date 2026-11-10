package com.alexbatalov.fallout2ce;

import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;

// A row of buttons laid out as the game's rows of tabs (mui.cc
// `muiLayoutTabRow`, character creation): all of one width while each label
// fits its share of the row; otherwise each as wide as its label and an equal
// share of what is left, the labels smaller first when even their widths
// don't fit (to 10sp); still too long, in proportion to their labels.
final class ButtonRow {
    private ButtonRow() {
    }

    // [row]'s buttons laid out so from now on (its width or labels change).
    // [padding]: the room around a label in its button, pixels.
    static void fit(LinearLayout row, int padding) {
        row.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) ->
            view.post(() -> apply(row, padding)));
    }

    private static void apply(LinearLayout row, int padding) {
        int room = row.getWidth() - row.getPaddingLeft() - row.getPaddingRight();
        int count = 0;
        for (int index = 0; index < row.getChildCount(); index++) {
            View child = row.getChildAt(index);
            if (child.getVisibility() == View.GONE) {
                continue;
            }
            ViewGroup.MarginLayoutParams margins = (ViewGroup.MarginLayoutParams) child.getLayoutParams();
            room -= margins.getMarginStart() + margins.getMarginEnd();
            count++;
        }
        if (room <= 0 || count == 0) {
            return;
        }

        int[] natural = new int[row.getChildCount()];
        int total = measure(row, padding, natural, 1.0f);
        boolean fit = true;
        for (int index = 0; index < row.getChildCount(); index++) {
            if (natural[index] > room / count) {
                fit = false;
            }
        }
        // Smaller labels when even their widths are too long.
        float scale = 1.0f;
        if (total > room) {
            int labels = total - padding * count;
            // A little under: letters don't shrink exactly with the size.
            scale = labels > 0 ? Math.max((float) (room - padding * count) / labels * 0.95f, 0.0f) : 1.0f;
            total = measure(row, padding, natural, scale);
        }

        boolean changed = false;
        for (int index = 0; index < row.getChildCount(); index++) {
            View child = row.getChildAt(index);
            if (child.getVisibility() == View.GONE) {
                continue;
            }
            int width;
            float weight;
            if (fit) {
                width = 0;
                weight = 1;
            } else if (total <= room) {
                width = natural[index];
                weight = 1;
            } else {
                width = (int) ((long) natural[index] * room / total);
                weight = 0;
            }
            LinearLayout.LayoutParams params = (LinearLayout.LayoutParams) child.getLayoutParams();
            if (params.width != width || params.weight != weight) {
                params.width = width;
                params.weight = weight;
                child.setLayoutParams(params);
                changed = true;
            }
        }
        if (changed) {
            row.requestLayout();
        }
    }

    // The labels' widths with [padding] at [scale] of their own text sizes
    // (to 10sp; the size set), their sum.
    private static int measure(LinearLayout row, int padding, int[] natural, float scale) {
        int total = 0;
        for (int index = 0; index < row.getChildCount(); index++) {
            View child = row.getChildAt(index);
            if (child.getVisibility() == View.GONE || !(child instanceof TextView)) {
                continue;
            }
            TextView label = (TextView) child;
            label.setSingleLine(true);
            Object base = label.getTag(R.id.button_row_text_size);
            if (!(base instanceof Float)) {
                base = label.getTextSize();
                label.setTag(R.id.button_row_text_size, base);
            }
            float smallest = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 10, label.getResources().getDisplayMetrics());
            float size = Math.max((Float) base * scale, Math.min(smallest, (Float) base));
            if (label.getTextSize() != size) {
                label.setTextSize(TypedValue.COMPLEX_UNIT_PX, size);
            }
            natural[index] = (int) Math.ceil(label.getPaint().measureText(label.getText().toString())) + padding;
            total += natural[index];
        }
        return total;
    }
}
