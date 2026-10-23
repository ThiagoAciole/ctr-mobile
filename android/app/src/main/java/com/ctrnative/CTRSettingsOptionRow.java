package com.ctrnative;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Shared row for cycling one CTRDS setting in the launcher and in-game dialogs. */
final class CTRSettingsOptionRow extends LinearLayout {
    enum Style {
        LAUNCHER,
        IN_GAME
    }

    interface CycleListener {
        void onCycle(int direction);
    }

    CTRSettingsOptionRow(Context context, CTRDSSettings.Option option, TextView value,
                         Style style, CycleListener cycleListener) {
        super(context);
        boolean launcher = style == Style.LAUNCHER;

        setOrientation(HORIZONTAL);
        setGravity(Gravity.CENTER_VERTICAL);
        setPadding(dp(context, 12), dp(context, 2), dp(context, 10), dp(context, 2));
        if (launcher) {
            setBackgroundColor(Color.rgb(5, 58, 94));
            setElevation(dp(context, 3));
        } else {
            GradientDrawable background = new GradientDrawable();
            background.setColor(0xCC050505);
            background.setStroke(dp(context, 1), 0x66505050);
            setBackground(background);
            setElevation(dp(context, 2));
        }

        TextView label = new TextView(context);
        label.setText(launcher ? option.label : option.label.toUpperCase(java.util.Locale.ROOT));
        label.setTextColor(Color.WHITE);
        Typeface optionTypeface = CTRFont.get(context);
        label.setTypeface(optionTypeface);
        value.setTypeface(optionTypeface);
        label.setTextSize(launcher ? 14.0f : 13.0f);
        label.setSingleLine(true);
        label.setEllipsize(TextUtils.TruncateAt.END);
        if (launcher) {
            label.setShadowLayer(dp(context, 1), 0, dp(context, 1), Color.BLACK);
        } else {
            label.setGravity(Gravity.CENTER_VERTICAL);
            label.setIncludeFontPadding(false);
            label.setClickable(true);
        }
        addView(label, new LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT,
                launcher ? 0.44f : 0.38f));

        int arrowSize = dp(context, launcher ? 40 : 34);
        ImageView previous = arrow(context, R.drawable.left,
                (launcher ? "Anterior: " : "Valor anterior de ") + option.label);
        if (launcher) previous.setFocusable(true);
        LayoutParams previousParams = new LayoutParams(arrowSize, arrowSize);
        if (launcher) previousParams.leftMargin = dp(context, 3);
        previous.setOnClickListener(view -> cycleListener.onCycle(-1));
        addView(previous, previousParams);

        LayoutParams valueParams = new LayoutParams(0, dp(context, launcher ? 39 : 38),
                launcher ? 0.56f : 0.42f);
        valueParams.leftMargin = dp(context, launcher ? 5 : 4);
        if (!launcher) valueParams.rightMargin = dp(context, 4);
        addView(value, valueParams);

        ImageView next = arrow(context, R.drawable.right,
                (launcher ? "Próximo: " : "Próximo valor de ") + option.label);
        if (launcher) {
            next.setFocusable(true);
            LayoutParams nextParams = new LayoutParams(arrowSize, arrowSize);
            nextParams.leftMargin = dp(context, 3);
            next.setOnClickListener(view -> cycleListener.onCycle(1));
            addView(next, nextParams);

            value.setOnClickListener(view -> cycleListener.onCycle(1));
            value.setOnLongClickListener(view -> {
                cycleListener.onCycle(-1);
                return true;
            });
        } else {
            next.setOnClickListener(view -> cycleListener.onCycle(1));
            addView(next, new LayoutParams(arrowSize, arrowSize));
        }
    }

    private static ImageView arrow(Context context, int drawable, String description) {
        ImageView arrow = new ImageView(context);
        arrow.setImageResource(drawable);
        arrow.setScaleType(ImageView.ScaleType.FIT_CENTER);
        arrow.setContentDescription(description);
        return arrow;
    }

    private static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }
}
