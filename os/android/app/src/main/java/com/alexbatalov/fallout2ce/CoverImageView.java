package com.alexbatalov.fallout2ce;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;
import android.util.AttributeSet;
import android.widget.ImageView;

// A game's picture in its frame, held at its right edge as the game's main
// menu holds it (mui_main_menu.cc). As wide as the frame or wider (4:3, 16:9):
// it fills the frame, a wider one gives up its left part. Much narrower (the
// game's own 640x480 menu picture right of its button plates): as tall as
// the frame, its left edge fading into the frame's black, as in the menu -
// filling the frame would blow its middle up.
public class CoverImageView extends ImageView {
    // Narrower than the frame by more than this: as in the menu.
    private static final float NARROW = 0.75f;
    // The fade over the picture's left part, of its width.
    private static final float FADE = 0.35f;

    private final Paint fade = new Paint();
    // Small (the list's thumbnails): always filled, a narrow picture too.
    private boolean alwaysFill;
    // The picture's left edge in the view, when it's drawn as in the menu.
    private float left = -1;
    private float fadeWidth;

    public CoverImageView(Context context, AttributeSet attributes) {
        super(context, attributes);
        setScaleType(ScaleType.MATRIX);
    }

    void setAlwaysFill(boolean alwaysFill) {
        this.alwaysFill = alwaysFill;
        fit();
    }

    @Override
    protected boolean setFrame(int left, int top, int right, int bottom) {
        boolean changed = super.setFrame(left, top, right, bottom);
        fit();
        return changed;
    }

    @Override
    public void setImageDrawable(Drawable drawable) {
        super.setImageDrawable(drawable);
        fit();
    }

    private void fit() {
        left = -1;
        Drawable drawable = getDrawable();
        int width = getWidth() - getPaddingLeft() - getPaddingRight();
        int height = getHeight() - getPaddingTop() - getPaddingBottom();
        if (drawable == null || width <= 0 || height <= 0) {
            return;
        }
        int pictureWidth = drawable.getIntrinsicWidth();
        int pictureHeight = drawable.getIntrinsicHeight();
        if (pictureWidth <= 0 || pictureHeight <= 0) {
            return;
        }
        float frameAspect = (float) width / height;
        float pictureAspect = (float) pictureWidth / pictureHeight;
        float scale;
        Matrix matrix = new Matrix();
        if (!alwaysFill && pictureAspect < frameAspect * NARROW) {
            scale = (float) height / pictureHeight;
            float drawnWidth = pictureWidth * scale;
            left = getPaddingLeft() + width - drawnWidth;
            fadeWidth = drawnWidth * FADE;
            fade.setShader(new LinearGradient(left, 0, left + fadeWidth, 0, Color.BLACK, Color.TRANSPARENT, Shader.TileMode.CLAMP));
            matrix.setScale(scale, scale);
            matrix.postTranslate(width - drawnWidth, 0);
        } else {
            scale = Math.max((float) width / pictureWidth, (float) height / pictureHeight);
            matrix.setScale(scale, scale);
            matrix.postTranslate(width - pictureWidth * scale, (height - pictureHeight * scale) / 2.0f);
        }
        setScaleType(ScaleType.MATRIX);
        setImageMatrix(matrix);
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (left >= 0 && getDrawable() != null) {
            canvas.drawRect(left, getPaddingTop(), left + fadeWidth, getHeight() - getPaddingBottom(), fade);
        }
    }
}
