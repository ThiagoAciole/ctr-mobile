package com.ctrnative;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Build;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.util.TypedValue;

import java.util.ArrayList;
import java.util.List;

/** Full-screen secondary screen for editing race touch controls. */
public final class CTRControlsActivity extends Activity {
    private static final String PREFS = "CTRNativePrefs";
    private static final String SCALE_KEY = "race_control_scale_percent";
    private static final String POSITION_PREFIX = "race_control_";
    private static final int COLOR_BLUE = Color.rgb(0, 65, 220);
    private static final int COLOR_ORANGE = Color.rgb(255, 166, 0);

    private final List<ControlItem> items = new ArrayList<>();
    private SharedPreferences preferences;
    private FrameLayout preview;
    private int scalePercent;
    private boolean resetToDefaults;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Menu/editor changes stay in place instead of sliding two screens together.
        if (Build.VERSION.SDK_INT >= 34) {
            overrideActivityTransition(OVERRIDE_TRANSITION_OPEN, 0, 0);
            overrideActivityTransition(OVERRIDE_TRANSITION_CLOSE, 0, 0);
        } else {
            overridePendingTransition(0, 0);
        }
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        preferences = getSharedPreferences(PREFS, MODE_PRIVATE);
        scalePercent = clamp(preferences.getInt(SCALE_KEY, 100), 70, 150);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        setContentView(root);
        setImmersiveMode();

        preview = new FrameLayout(this);
        preview.setClipChildren(false);
        preview.setClipToPadding(false);
        root.addView(preview, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        ImageView previewImage = new ImageView(this);
        previewImage.setImageResource(R.drawable.race_view);
        previewImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        preview.addView(previewImage, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        View previewTint = new View(this);
        previewTint.setBackgroundColor(0x40000000);
        preview.addView(previewTint, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        TextView gamepad = actionButton("GAMEPAD", COLOR_BLUE, Color.WHITE);
        gamepad.setFocusable(true);
        gamepad.setOnClickListener(view -> startActivity(new android.content.Intent(this, CTRGamepadActivity.class)));
        FrameLayout.LayoutParams gamepadParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dp(48), Gravity.TOP | Gravity.LEFT);
        gamepadParams.setMargins(dp(24), dp(12), 0, 0);
        root.addView(gamepad, gamepadParams);
        preview.addOnLayoutChangeListener((view, left, top, right, bottom,
                                           oldLeft, oldTop, oldRight, oldBottom) -> layoutControls());

        FrameLayout dialog = new FrameLayout(this);
        GradientDrawable dialogBackground = new GradientDrawable();
        dialogBackground.setColor(0xD9000000);
        dialogBackground.setStroke(dp(2), Color.WHITE);
        dialog.setBackground(dialogBackground);
        root.addView(dialog);

        TextView title = text("CONTROLES", Color.WHITE, 14, Gravity.CENTER);
        title.setShadowLayer(dp(2), 0, dp(2), Color.BLACK);
        title.setContentDescription("Arraste para mover o diálogo de controles");
        enableDialogDragging(title, dialog, root);
        dialog.addView(title);

        TextView sizeLabel = text("TAMANHO", Color.WHITE, 12, Gravity.CENTER_VERTICAL);
        sizeLabel.setShadowLayer(dp(1), 0, dp(1), Color.BLACK);
        dialog.addView(sizeLabel);
        TextView sizeValue = text(scalePercent + "%", Color.WHITE, 12,
                Gravity.RIGHT | Gravity.CENTER_VERTICAL);
        dialog.addView(sizeValue);

        SeekBar size = new SeekBar(this);
        size.setMax(80);
        size.setProgress(scalePercent - 70);
        CTRSeekBarStyle.apply(size, COLOR_ORANGE);
        dialog.addView(size);

        TextView back = actionButton("VOLTAR", COLOR_BLUE, Color.WHITE);
        back.setContentDescription("Voltar para configurações");
        back.setOnClickListener(view -> finish());
        dialog.addView(back);

        TextView save = actionButton("SALVAR", COLOR_ORANGE, Color.WHITE);
        save.setContentDescription("Salvar controles");
        save.setOnClickListener(view -> {
            saveLayout();
            finish();
        });
        dialog.addView(save);

        TextView restore = actionButton("RESETAR", Color.TRANSPARENT, Color.WHITE);
        restore.setContentDescription("Restaurar posição e tamanho padrão");
        restore.setOnClickListener(view -> {
            resetToDefaults = true;
            scalePercent = 100;
            size.setProgress(30);
            sizeValue.setText("100%");
            for (ControlItem item : items) {
                item.x = item.defaultX;
                item.y = item.defaultY;
                item.indicator.setVisibility(View.GONE);
            }
            layoutControls();
        });
        dialog.addView(restore);

        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                scalePercent = 70 + progress;
                sizeValue.setText(scalePercent + "%");
                layoutControls();
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) { }
        });

        root.post(() -> {
            int width = root.getWidth();
            int height = root.getHeight();
            if (width <= 0 || height <= 0) return;

            int safeLeft = 0;
            int safeTop = 0;
            int safeRight = 0;
            int safeBottom = 0;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowInsets insets = root.getRootWindowInsets();
                DisplayCutout cutout = insets == null ? null : insets.getDisplayCutout();
                if (cutout != null) {
                    safeLeft = cutout.getSafeInsetLeft();
                    safeTop = cutout.getSafeInsetTop();
                    safeRight = cutout.getSafeInsetRight();
                    safeBottom = cutout.getSafeInsetBottom();
                }
            }
            int contentWidth = Math.max(1, width - safeLeft - safeRight);
            int contentHeight = Math.max(1, height - safeTop - safeBottom);
            gamepadParams.leftMargin = safeLeft + dp(16);
            gamepadParams.topMargin = safeTop + dp(12);
            gamepad.setLayoutParams(gamepadParams);
            float dialogWidth = contentWidth * 0.48125f;
            float dialogHeight = contentHeight * 0.42625f;
            place(dialog, safeLeft + (contentWidth - dialogWidth) * 0.5f,
                    safeTop + contentHeight * 0.092f,
                    dialogWidth, dialogHeight, Gravity.TOP | Gravity.LEFT);

            float unit = Math.min(dialogWidth, dialogHeight);
            place(title, dialogWidth * 0.04f, dialogHeight * 0.045f,
                    dialogWidth * 0.92f, dialogHeight * 0.20f, Gravity.TOP | Gravity.LEFT);
            title.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.13f);
            place(sizeLabel, dialogWidth * 0.06f, dialogHeight * 0.31f,
                    dialogWidth * 0.56f, dialogHeight * 0.13f, Gravity.TOP | Gravity.LEFT);
            place(sizeValue, dialogWidth * 0.76f, dialogHeight * 0.31f,
                    dialogWidth * 0.18f, dialogHeight * 0.13f, Gravity.TOP | Gravity.LEFT);
            sizeLabel.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.09f);
            sizeValue.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.09f);
            place(size, dialogWidth * 0.055f, dialogHeight * 0.41f,
                    dialogWidth * 0.89f, dialogHeight * 0.22f, Gravity.TOP | Gravity.LEFT);

            float buttonTop = dialogHeight * 0.70f;
            float buttonWidth = dialogWidth * 0.29f;
            float buttonHeight = dialogHeight * 0.21f;
            place(back, dialogWidth * 0.035f, buttonTop,
                    buttonWidth, buttonHeight, Gravity.TOP | Gravity.LEFT);
            place(save, dialogWidth * 0.355f, buttonTop,
                    buttonWidth, buttonHeight, Gravity.TOP | Gravity.LEFT);
            place(restore, dialogWidth * 0.675f, buttonTop,
                    buttonWidth, buttonHeight, Gravity.TOP | Gravity.LEFT);
            float buttonTextSize = unit * 0.085f;
            back.setTextSize(TypedValue.COMPLEX_UNIT_PX, buttonTextSize);
            save.setTextSize(TypedValue.COMPLEX_UNIT_PX, buttonTextSize);
            restore.setTextSize(TypedValue.COMPLEX_UNIT_PX, buttonTextSize);
            createControls(width, height);
            preview.post(this::layoutControls);
        });
    }

    private void createControls(int screenWidth, int screenHeight) {
        float unit = Math.min(screenWidth, screenHeight);
        float widthRatio = unit / Math.max(1, screenWidth);
        float marginRatio = 0.045f * widthRatio;
        addControl("left", R.drawable.left, 0.115f, 0.80f, 0.235f, screenWidth, screenHeight);
        addControl("right", R.drawable.right, 0.285f, 0.80f, 0.235f, screenWidth, screenHeight);
        addControl("pause", R.drawable.pause, 1.0f - widthRatio * 0.0925f,
                0.0925f, 0.145f, screenWidth, screenHeight);
        addControl("jump", R.drawable.pular, 0.77f, 0.72f, 0.209f, screenWidth, screenHeight);
        addControl("brake", R.drawable.brake, 0.76f, 0.90f, 0.209f, screenWidth, screenHeight);
        addControl("weapon", R.drawable.weapon, 0.875f, 0.56f, 0.209f, screenWidth, screenHeight);
        addControl("accelerator", R.drawable.accelerator,
                1.0f - marginRatio - widthRatio * 0.306f * 0.5f,
                0.84f, 0.306f, screenWidth, screenHeight);
    }

    private void addControl(String key, int drawable, float defaultX, float defaultY,
                            float sizeFactor, int screenWidth, int screenHeight) {
        ControlItem item = new ControlItem(key, defaultX, defaultY, sizeFactor);
        item.x = preferences.getFloat(positionKey(key, "x"), defaultX);
        item.y = preferences.getFloat(positionKey(key, "y"), defaultY);
        ImageView icon = new ImageView(this);
        icon.setImageResource(drawable);
        icon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        icon.setContentDescription("Arraste para reposicionar " + key);
        icon.setClickable(true);
        icon.setOnTouchListener((view, event) -> drag(item, event));
        item.icon = icon;
        items.add(item);
        preview.addView(icon, new FrameLayout.LayoutParams(1, 1));
        GradientDrawable indicatorBackground = new GradientDrawable();
        indicatorBackground.setColor(0x55000000);
        indicatorBackground.setStroke(dp(2), COLOR_ORANGE);
        View indicator = new View(this);
        indicator.setBackground(indicatorBackground);
        indicator.setVisibility(View.GONE);
        indicator.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        item.indicator = indicator;
        preview.addView(indicator, new FrameLayout.LayoutParams(1, 1));
    }

    private boolean drag(ControlItem item, MotionEvent event) {
        View view = item.icon;
        ControlItem mate = directionMate(item);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                item.startLeft = view.getX();
                item.startTop = view.getY();
                item.startRawX = event.getRawX();
                item.startRawY = event.getRawY();
                if (mate != null) {
                    mate.startLeft = mate.icon.getX();
                    mate.startTop = mate.icon.getY();
                }
                for (ControlItem candidate : items) {
                    candidate.indicator.setVisibility(
                            candidate == item || candidate == mate ? View.VISIBLE : View.GONE);
                }
                view.bringToFront();
                if (mate != null) mate.icon.bringToFront();
                item.indicator.bringToFront();
                if (mate != null) mate.indicator.bringToFront();
                preview.requestDisallowInterceptTouchEvent(true);
                return true;
            case MotionEvent.ACTION_MOVE:
                float deltaX = event.getRawX() - item.startRawX;
                float deltaY = event.getRawY() - item.startRawY;
                if (mate != null) {
                    deltaX = clamp(deltaX,
                            Math.max(-item.startLeft, -mate.startLeft),
                            Math.min(preview.getWidth() - item.size - item.startLeft,
                                    preview.getWidth() - mate.size - mate.startLeft));
                    deltaY = clamp(deltaY,
                            Math.max(-item.startTop, -mate.startTop),
                            Math.min(preview.getHeight() - item.size - item.startTop,
                                    preview.getHeight() - mate.size - mate.startTop));
                    positionControl(item, item.startLeft + deltaX, item.startTop + deltaY);
                    positionControl(mate, mate.startLeft + deltaX, mate.startTop + deltaY);
                } else {
                    float left = clamp(item.startLeft + deltaX,
                            0, Math.max(0, preview.getWidth() - item.size));
                    float top = clamp(item.startTop + deltaY,
                            0, Math.max(0, preview.getHeight() - item.size));
                    positionControl(item, left, top);
                }
                resetToDefaults = false;
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                return true;
            default:
                return true;
        }
    }

    private void layoutControls() {
        if (preview == null || preview.getWidth() <= 0 || preview.getHeight() <= 0) return;
        float unit = Math.min(preview.getWidth(), preview.getHeight());
        int width = preview.getWidth();
        int height = preview.getHeight();
        for (ControlItem item : items) {
            item.size = Math.min(unit * item.sizeFactor * scalePercent / 100f, unit * 0.42f);
            float left = clamp(item.x * width - item.size * 0.5f, 0, width - item.size);
            float top = clamp(item.y * height - item.size * 0.5f, 0, height - item.size);
            FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                    Math.round(item.size), Math.round(item.size));
            item.icon.setLayoutParams(params);
            float indicatorSize = item.size * 0.74f;
            item.indicator.setLayoutParams(new FrameLayout.LayoutParams(
                    Math.round(indicatorSize), Math.round(indicatorSize)));
            positionControl(item, left, top);
        }
    }

    private void positionControl(ControlItem item, float left, float top) {
        item.x = (left + item.size * 0.5f) / Math.max(1, preview.getWidth());
        item.y = (top + item.size * 0.5f) / Math.max(1, preview.getHeight());
        item.icon.setX(left);
        item.icon.setY(top);
        float indicatorSize = item.size * 0.74f;
        item.indicator.setX(left + (item.size - indicatorSize) * 0.5f);
        item.indicator.setY(top + (item.size - indicatorSize) * 0.5f);
    }

    private ControlItem directionMate(ControlItem item) {
        if (!"left".equals(item.key) && !"right".equals(item.key)) return null;
        String mateKey = "left".equals(item.key) ? "right" : "left";
        for (ControlItem candidate : items) {
            if (mateKey.equals(candidate.key)) return candidate;
        }
        return null;
    }

    private void enableDialogDragging(View handle, View dialog, View root) {
        float[] start = new float[4];
        handle.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    start[0] = dialog.getX();
                    start[1] = dialog.getY();
                    start[2] = event.getRawX();
                    start[3] = event.getRawY();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    dialog.setX(clamp(start[0] + event.getRawX() - start[2],
                            0, Math.max(0, root.getWidth() - dialog.getWidth())));
                    dialog.setY(clamp(start[1] + event.getRawY() - start[3],
                            0, Math.max(0, root.getHeight() - dialog.getHeight())));
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    return true;
                default:
                    return true;
            }
        });
    }

    private void saveLayout() {
        SharedPreferences.Editor editor = preferences.edit().putInt(SCALE_KEY, scalePercent);
        for (ControlItem item : items) {
            if (resetToDefaults) {
                editor.remove(positionKey(item.key, "x"));
                editor.remove(positionKey(item.key, "y"));
            } else {
                editor.putFloat(positionKey(item.key, "x"), item.x);
                editor.putFloat(positionKey(item.key, "y"), item.y);
            }
        }
        editor.apply();
    }

    private TextView text(String value, int color, float sizeSp, int gravity) {
        TextView result = new TextView(this);
        result.setText(value);
        result.setTextColor(color);
        result.setTypeface(CTRFont.get(this));
        result.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp);
        result.setGravity(gravity);
        result.setSingleLine(true);
        result.setIncludeFontPadding(false);
        return result;
    }

    private TextView actionButton(String label, int backgroundColor, int textColor) {
        TextView button = text(label, textColor, 16, Gravity.CENTER);
        GradientDrawable background = new GradientDrawable();
        background.setColor(backgroundColor);
        background.setStroke(dp(1), Color.WHITE);
        button.setBackground(background);
        button.setElevation(dp(3));
        button.setClickable(true);
        button.setFocusable(true);
        return button;
    }

    private void place(View view, float x, float y, float width, float height, int gravity) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                Math.max(1, Math.round(width)), Math.max(1, Math.round(height)), gravity);
        if ((gravity & Gravity.RIGHT) == Gravity.RIGHT) params.rightMargin = Math.round(x);
        else params.leftMargin = Math.round(x);
        params.topMargin = Math.round(y);
        view.setLayoutParams(params);
    }

    private void setImmersiveMode() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode =
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                            ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                            : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = decor.getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }

    @Override
    public void finish() {
        super.finish();
        if (Build.VERSION.SDK_INT < 34) overridePendingTransition(0, 0);
    }

    @Override public boolean onKeyDown(int key, android.view.KeyEvent event) {
        if (key == android.view.KeyEvent.KEYCODE_BUTTON_B && CTRGamepadActivity.isGamepad(event.getDevice())) {
            finish();
            return true;
        }
        return super.onKeyDown(key, event);
    }

    @Override
    protected void onResume() {
        super.onResume();
        setImmersiveMode();
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) setImmersiveMode();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private static String positionKey(String control, String axis) {
        return POSITION_PREFIX + control + "_" + axis;
    }

    private static float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }

    private static int clamp(int value, int min, int max) {
        return Math.max(min, Math.min(max, value));
    }

    private static final class ControlItem {
        final String key;
        final float defaultX;
        final float defaultY;
        final float sizeFactor;
        ImageView icon;
        View indicator;
        float x;
        float y;
        float size;
        float startLeft;
        float startTop;
        float startRawX;
        float startRawY;

        ControlItem(String key, float defaultX, float defaultY, float sizeFactor) {
            this.key = key;
            this.defaultX = defaultX;
            this.defaultY = defaultY;
            this.sizeFactor = sizeFactor;
            x = defaultX;
            y = defaultY;
        }
    }
}
