package com.ctrnative;

import android.graphics.Color;
import android.graphics.drawable.ClipDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.view.Gravity;
import android.widget.SeekBar;

/** Shared orange-track and white-thumb style for in-game sliders. */
final class CTRSeekBarStyle {
    private CTRSeekBarStyle() {}

    static void apply(SeekBar seekBar, int progressColor) {
        float density = seekBar.getResources().getDisplayMetrics().density;
        int trackHeight = Math.round(8 * density);
        int cornerRadius = Math.round(4 * density);

        GradientDrawable track = new GradientDrawable();
        track.setColor(0xFF777777);
        track.setCornerRadius(cornerRadius);
        track.setSize(-1, trackHeight);
        GradientDrawable progress = new GradientDrawable();
        progress.setColor(progressColor);
        progress.setCornerRadius(cornerRadius);
        progress.setSize(-1, trackHeight);

        LayerDrawable progressTrack = new LayerDrawable(new android.graphics.drawable.Drawable[] {
                track, new ClipDrawable(progress, Gravity.LEFT, ClipDrawable.HORIZONTAL)
        }) {
            @Override
            protected void onBoundsChange(android.graphics.Rect bounds) {
                int inset = Math.max(0, (bounds.height() - trackHeight) / 2);
                setLayerInset(0, 0, inset, 0, inset);
                setLayerInset(1, 0, inset, 0, inset);
                super.onBoundsChange(bounds);
            }
        };
        progressTrack.setId(0, android.R.id.background);
        progressTrack.setId(1, android.R.id.progress);
        seekBar.setProgressDrawable(progressTrack);

        GradientDrawable thumb = new GradientDrawable();
        thumb.setShape(GradientDrawable.OVAL);
        thumb.setColor(Color.WHITE);
        int thumbSize = Math.round(16 * density);
        thumb.setSize(thumbSize, thumbSize);
        seekBar.setThumb(thumb);
        int horizontalPadding = Math.round(10 * density);
        seekBar.setPadding(horizontalPadding, 0, horizontalPadding, 0);
        seekBar.setThumbOffset(Math.round(8 * density));
    }
}
