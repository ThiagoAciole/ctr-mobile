package com.ctrnative;

import android.app.Activity;
import android.app.Dialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.system.Os;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.SeekBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.util.TypedValue;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.util.ArrayList;
import java.util.List;

/**
 * The screen the app opens on: pick the disc, change the settings, then play.
 *
 * It stays rather than getting out of the way once a disc exists. Settings used
 * to live only in ctrds.cfg or behind Select during a race, neither of which is
 * reachable when you just want to change the resolution before starting.
 */
public final class CTRNativeLauncherActivity extends Activity {
    private static final int REQUEST_DISC_IMAGE = 1001;
    private static final int COPY_BUFFER_SIZE = 1024 * 1024;
    private static final int RAW_SECTOR_SIZE = 2352;
    private static final int PVD_LBA = 16;
    private static final int FORM1_DATA_OFFSET = 24;
    private static final String CONTROL_PREFS = CTRAppPreferences.PREFS_NAME;
    private static final int COLOR_BACKGROUND = Color.rgb(18, 18, 18);
    private static final int COLOR_WARN = Color.rgb(255, 160, 122);

    private FrameLayout launcherRoot;
    private ImageView backgroundImage;
    private LauncherPanelView contentPanel;
    private LauncherActionButton importButton;
    private LauncherActionButton changeButton;
    private FrameLayout settingsButton;
    private OutlinedTextView titleText;
    private TextView discText;
    private ProgressBar importProgress;
    private TextView statusText;
    private LauncherActionButton playButton;
    private ImageView trophyImage;
    private boolean importInProgress;
    private String pendingDiscDisplayName;

    private static final String PREF_DISC_DISPLAY_NAME = "disc_display_name";

    private CTRDSSettings settings;
    private final List<TextView> optionButtons = new ArrayList<>();
    private TextView quickStartButton;

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
        setImmersiveMode();

        // Before anything else that could throw.
        CTRCrashLog.install(getApplicationContext());

        settings = new CTRDSSettings(getConfigFile());
        buildLauncherView();
        if (savedInstanceState == null && CTRAppPreferences.isQuickStartEnabled(this)) {
            launcherRoot.post(() -> {
                File disc = getImportedDiscImage();
                if (disc.isFile() && isUsableDiscImage(disc)) startGame();
            });
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            setImmersiveMode();
        }
    }

    private void setImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override
    public void finish() {
        super.finish();
        if (Build.VERSION.SDK_INT < 34) overridePendingTransition(0, 0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        setImmersiveMode();
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        // The game rewrites this file whenever a setting is changed from the
        // in-game menu, so coming back from a session means re-reading it rather
        // than showing what the launcher last knew.
        if (settings != null) {
            settings = new CTRDSSettings(getConfigFile());
            refreshOptionButtons();
            refreshDiscStatus();
        }
    }

    private File getStorageRoot() {
        File root = getExternalFilesDir(null);
        return (root != null) ? root : getFilesDir();
    }

    private File getAssetDirectory() {
        return new File(getStorageRoot(), "assets");
    }

    private File getImportedBinImage() {
        return new File(getAssetDirectory(), "ctr-u.bin");
    }

    private File getImportedChdImage() {
        return new File(getAssetDirectory(), "ctr-u.chd");
    }

    /** Whichever of the two kinds of disc image is actually present. */
    private File getImportedDiscImage() {
        File chd = getImportedChdImage();
        return chd.isFile() ? chd : getImportedBinImage();
    }

    /**
     * A CHD is a compressed disc image and cannot be checked the way a raw BIN
     * is -- its sectors are inside compressed hunks. The header magic is enough
     * to tell what it is; whether it holds the right game is a question for the
     * native side, which reads its filesystem.
     */
    private boolean isChdImage(File image) {
        if (!image.isFile() || (image.length() < 16)) {
            return false;
        }

        byte[] magic = new byte[8];
        try (RandomAccessFile file = new RandomAccessFile(image, "r")) {
            file.readFully(magic);
        } catch (IOException exception) {
            return false;
        }

        return (magic[0] == 'M') && (magic[1] == 'C') && (magic[2] == 'o') && (magic[3] == 'm')
                && (magic[4] == 'p') && (magic[5] == 'r') && (magic[6] == 'H') && (magic[7] == 'D');
    }

    private boolean isUsableDiscImage(File image) {
        return isChdImage(image) || isRetailDiscImage(image);
    }

    private File getConfigFile() {
        return new File(getAssetDirectory(), "ctrds.cfg");
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private TextView makeText(String text, float size, int color) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTypeface(CTRFont.get(this));
        view.setTextColor(color);
        view.setTextSize(size);
        view.setGravity(Gravity.CENTER);
        view.setMaxWidth(dp(720));
        return view;
    }

    private void buildLauncherView() {
        launcherRoot = new FrameLayout(this);
        launcherRoot.setBackgroundColor(COLOR_BACKGROUND);

        backgroundImage = new ImageView(this);
        backgroundImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        backgroundImage.setImageResource(R.drawable.launcher_bg);
        launcherRoot.addView(backgroundImage, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        contentPanel = new LauncherPanelView();
        contentPanel.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        launcherRoot.addView(contentPanel);

        trophyImage = new ImageView(this);
        trophyImage.setImageResource(R.drawable.launcher_trophy);
        trophyImage.setScaleType(ImageView.ScaleType.FIT_CENTER);
        launcherRoot.addView(trophyImage);

        titleText = new OutlinedTextView();
        titleText.setTypeface(CTRFont.get(this));
        titleText.setGravity(Gravity.CENTER);
        titleText.setMaxLines(2);
        launcherRoot.addView(titleText);

        importButton = new LauncherActionButton(true);
        importButton.setContentDescription(getString(R.string.launcher_import_disc_description));
        importButton.setOnClickListener(view -> selectDiscImage());
        launcherRoot.addView(importButton);

        changeButton = new LauncherActionButton(false);
        changeButton.setText("ALTERAR ARQUIVO");
        changeButton.setContentDescription(getString(R.string.launcher_replace_disc_description));
        changeButton.setOnClickListener(view -> selectDiscImage());
        launcherRoot.addView(changeButton);

        playButton = new LauncherActionButton(true);
        playButton.setOnClickListener(view -> startGame());
        launcherRoot.addView(playButton);

        settingsButton = new FrameLayout(this);
        GradientDrawable settingsFrame = new GradientDrawable();
        settingsFrame.setColor(0x80000000);
        settingsFrame.setStroke(dp(2), 0xD9FFFFFF);
        settingsButton.setBackground(settingsFrame);
        settingsButton.setContentDescription(getString(R.string.launcher_settings_title));
        settingsButton.setClickable(true);
        settingsButton.setFocusable(true);
        settingsButton.setOnClickListener(view -> showSettings());
        ImageView settingsIcon = new ImageView(this);
        settingsIcon.setImageResource(R.drawable.launcher_settings_white);
        settingsIcon.setScaleType(ImageView.ScaleType.FIT_CENTER);
        int settingsIconPadding = dp(4);
        settingsIcon.setPadding(settingsIconPadding, settingsIconPadding,
                settingsIconPadding, settingsIconPadding);
        settingsButton.addView(settingsIcon, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        launcherRoot.addView(settingsButton);

        discText = new OutlinedTextView();
        discText.setTypeface(titleText.getTypeface());
        discText.setGravity(Gravity.CENTER);
        discText.setMaxLines(2);
        launcherRoot.addView(discText);

        importProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        importProgress.setMax(100);
        importProgress.setVisibility(View.GONE);
        FrameLayout.LayoutParams progressParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(8), Gravity.CENTER_HORIZONTAL | Gravity.TOP);
        launcherRoot.addView(importProgress, progressParams);

        statusText = makeText("", 13.0f, Color.WHITE);
        statusText.setBackgroundColor(0xAA124D78);
        statusText.setPadding(dp(10), dp(4), dp(10), dp(4));
        statusText.setVisibility(View.GONE);
        launcherRoot.addView(statusText);

        setContentView(launcherRoot);
        launcherRoot.addOnLayoutChangeListener((view, left, top, right, bottom,
                                                oldLeft, oldTop, oldRight, oldBottom) -> {
            if (right - left != oldRight - oldLeft || bottom - top != oldBottom - oldTop) {
                layoutLauncherControls();
            }
        });

        refreshDiscStatus();
    }

    private void layoutLauncherControls() {
        if ((launcherRoot == null) || (launcherRoot.getWidth() == 0) || (launcherRoot.getHeight() == 0)) {
            return;
        }

        int width = launcherRoot.getWidth();
        int height = launcherRoot.getHeight();
        titleText.setTextSize(TypedValue.COMPLEX_UNIT_PX, height * 0.095f);
        discText.setTextSize(TypedValue.COMPLEX_UNIT_PX, height * 0.048f);
        position(contentPanel, Math.round(width * 0.56f),
                Math.round(height * 0.35f),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL,
                Math.round(height * 0.27f));
        position(trophyImage, Math.round(width * 0.27f),
                Math.round(height * 0.26f),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.06f));
        position(titleText, Math.round(width * 0.72f),
                Math.round(height * 0.13f),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.36f));
        position(importButton, Math.round(width * 0.34f),
                Math.round(height * 0.15f), Gravity.TOP | Gravity.CENTER_HORIZONTAL,
                Math.round(height * 0.71f));
        position(playButton, Math.round(width * 0.34f),
                Math.round(height * 0.15f), Gravity.TOP | Gravity.CENTER_HORIZONTAL,
                Math.round(height * 0.71f));
        int changeWidth = Math.round(width * 0.23f);
        int changeHeight = Math.round(height * 0.105f);
        position(settingsButton, changeHeight, changeHeight,
                Gravity.TOP | Gravity.RIGHT, Math.round(height * 0.05f));
        position(changeButton, changeWidth, changeHeight,
                Gravity.TOP | Gravity.RIGHT, Math.round(height * 0.05f));
        position(discText, Math.round(width * 0.64f),
                Math.round(height * 0.07f),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.51f));
        position(importProgress, Math.round(width * 0.38f), dp(8),
                Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.855f));
        position(statusText, Math.round(width * 0.65f), ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.835f));
        float buttonTextSize = height * 0.050f;
        importButton.setTextSize(TypedValue.COMPLEX_UNIT_PX, buttonTextSize);
        playButton.setTextSize(TypedValue.COMPLEX_UNIT_PX, buttonTextSize);
        changeButton.setTextSize(TypedValue.COMPLEX_UNIT_PX, height * 0.04f);
    }

    private void position(View view, int width, int height, int gravity, int topMargin) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(width, height, gravity);
        params.topMargin = topMargin;
        if ((view == settingsButton) || (view == changeButton)) {
            params.rightMargin = Math.round(launcherRoot.getWidth() * 0.025f);
            if (view == settingsButton) {
                params.rightMargin += Math.round(launcherRoot.getWidth() * 0.015f);
            } else if (view == changeButton) {
                params.rightMargin += Math.round(launcherRoot.getHeight() * 0.105f)
                        + Math.round(launcherRoot.getWidth() * 0.045f);
            }
        }
        view.setLayoutParams(params);
    }

    private void showSettings() {
        Dialog dialog = new Dialog(this, android.R.style.Theme_Translucent_NoTitleBar_Fullscreen);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.TRANSPARENT);
        FrameLayout panelContainer = new FrameLayout(this);
        GradientDrawable panel = new GradientDrawable();
        panel.setColor(0xB8000000);
        panelContainer.setBackground(panel);
        root.addView(panelContainer);

        TextView heading = new OutlinedTextView();
        heading.setText("CONFIGURAÇÕES");
        heading.setTextColor(Color.WHITE);
        heading.setTypeface(CTRFont.get(this));
        heading.setGravity(Gravity.CENTER);
        panelContainer.addView(heading);

        ScrollView scrollView = new ScrollView(this);
        scrollView.setClipToPadding(false);
        scrollView.setFillViewport(false);
        scrollView.setVerticalScrollBarEnabled(false);
        scrollView.addView(buildSettingsPanel());
        panelContainer.addView(scrollView);

        TextView closeButton = new TextView(this);
        closeButton.setText("×");
        closeButton.setTextColor(Color.WHITE);
        closeButton.setTypeface(Typeface.DEFAULT_BOLD);
        closeButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30.0f);
        closeButton.setGravity(Gravity.CENTER);
        closeButton.setIncludeFontPadding(false);
        closeButton.setClickable(true);
        closeButton.setFocusable(true);
        closeButton.setContentDescription("Fechar configurações");
        panelContainer.addView(closeButton);
        closeButton.setOnClickListener(view -> dialog.dismiss());

        LauncherActionButton controlsButton = new LauncherActionButton(true);
        controlsButton.setText("CONTROLES");
        controlsButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.0f);
        controlsButton.setContentDescription("Abrir ajustes dos controles de corrida");
        panelContainer.addView(controlsButton);
        controlsButton.setOnClickListener(view ->
                startActivity(new Intent(this, CTRControlsActivity.class)));

        dialog.setContentView(root);
        dialog.setCancelable(true);
        dialog.setCanceledOnTouchOutside(true);
        dialog.show();

        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
            window.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_FULLSCREEN
                    | WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false);
            }
            window.getDecorView().setSystemUiVisibility(
                    getWindow().getDecorView().getSystemUiVisibility());
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams dialogAttributes = window.getAttributes();
                dialogAttributes.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                window.setAttributes(dialogAttributes);
            }
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.dimAmount = 0.68f;
            window.setAttributes(attributes);
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT);

            root.post(() -> {
                int width = root.getWidth();
                int height = root.getHeight();
                if (width <= 0 || height <= 0) return;
                panelContainer.setLayoutParams(new FrameLayout.LayoutParams(width, height));
                float unit = Math.min(width, height);
                heading.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.055f);
                position(heading, Math.round(width * 0.50f), Math.round(height * 0.12f),
                        Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.045f));
                int closeSize = Math.min(dp(48), Math.round(height * 0.12f));
                position(closeButton, closeSize, closeSize, Gravity.TOP | Gravity.LEFT,
                        Math.round(height * 0.045f));
                FrameLayout.LayoutParams closeParams = (FrameLayout.LayoutParams) closeButton.getLayoutParams();
                closeParams.leftMargin = Math.round(width * 0.025f);
                closeButton.setLayoutParams(closeParams);
                position(controlsButton, Math.round(width * 0.21f), Math.round(height * 0.12f),
                        Gravity.TOP | Gravity.RIGHT, Math.round(height * 0.045f));
                FrameLayout.LayoutParams controlsParams = (FrameLayout.LayoutParams) controlsButton.getLayoutParams();
                controlsParams.rightMargin = Math.round(width * 0.03f);
                controlsButton.setLayoutParams(controlsParams);
                position(scrollView, Math.round(width * 0.94f), Math.round(height * 0.79f),
                        Gravity.TOP | Gravity.CENTER_HORIZONTAL, Math.round(height * 0.20f));
            });
        }
    }

    private LinearLayout buildSettingsPanel() {
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(10), dp(6), dp(10), dp(6));
        optionButtons.clear();
        quickStartButton = null;

        for (int i = 0; i < CTRDSSettings.OPTIONS.length; i++) {
            final CTRDSSettings.Option option = CTRDSSettings.OPTIONS[i];
            final TextView value = new OutlinedTextView();
            value.setText(settings.nameOf(option));
            value.setTextColor(Color.rgb(255, 194, 32));
            value.setShadowLayer(dp(1), 0, dp(1), Color.rgb(74, 45, 0));
            value.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.0f);
            value.setGravity(Gravity.CENTER);
            value.setMinimumHeight(dp(39));
            value.setSingleLine(true);
            value.setEllipsize(android.text.TextUtils.TruncateAt.END);
            value.setContentDescription(option.label + ": " + settings.nameOf(option));
            CTRSettingsOptionRow row = new CTRSettingsOptionRow(this, option, value,
                    CTRSettingsOptionRow.Style.IN_GAME,
                    direction -> cycleSettingOption(option, value, direction));
            optionButtons.add(value);

            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    dp(44));
            rowParams.bottomMargin = dp(6);
            panel.addView(row, rowParams);
        }

        CTRDSSettings.Option option = CTRDSSettings.QUICK_START;
        quickStartButton = new OutlinedTextView();
        quickStartButton.setText(CTRAppPreferences.quickStartName(this));
        quickStartButton.setTextColor(Color.rgb(255, 194, 32));
        quickStartButton.setShadowLayer(dp(1), 0, dp(1), Color.rgb(74, 45, 0));
        quickStartButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14.0f);
        quickStartButton.setGravity(Gravity.CENTER);
        quickStartButton.setMinimumHeight(dp(39));
        quickStartButton.setSingleLine(true);
        quickStartButton.setContentDescription(option.label + ": "
                + CTRAppPreferences.quickStartName(this));
        CTRSettingsOptionRow quickStartRow = new CTRSettingsOptionRow(this, option,
                quickStartButton, CTRSettingsOptionRow.Style.IN_GAME,
                direction -> cycleQuickStartOption(quickStartButton, direction));
        panel.addView(quickStartRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(44)));

        return panel;
    }

    private void cycleSettingOption(CTRDSSettings.Option option, TextView value,
                                    int direction) {
        settings.cycle(option, direction);
        if (value != null) {
            value.setText(settings.nameOf(option));
            value.setContentDescription(option.label + ": " + settings.nameOf(option));
        }
        settings.save();
    }

    private void cycleQuickStartOption(TextView value, int direction) {
        String name = CTRAppPreferences.cycleQuickStart(this, direction);
        value.setText(name);
        value.setContentDescription(CTRDSSettings.QUICK_START.label + ": " + name);
    }

    private void refreshOptionButtons() {
        for (int i = 0; (i < optionButtons.size()) && (i < CTRDSSettings.OPTIONS.length); i++) {
            CTRDSSettings.Option option = CTRDSSettings.OPTIONS[i];
            optionButtons.get(i).setText(settings.nameOf(option));
            optionButtons.get(i).setContentDescription(option.label + ": " + settings.nameOf(option));
        }
        if (quickStartButton != null) {
            String name = CTRAppPreferences.quickStartName(this);
            quickStartButton.setText(name);
            quickStartButton.setContentDescription(CTRDSSettings.QUICK_START.label + ": " + name);
        }
    }

    private void refreshDiscStatus() {
        if ((backgroundImage == null) || (importButton == null) || (playButton == null)) {
            return;
        }

        File disc = getImportedDiscImage();
        boolean present = disc.isFile();
        boolean valid = present && isUsableDiscImage(disc);
        titleText.setTextColor(Color.rgb(255, 174, 15));
        discText.setTextColor(Color.WHITE);

        if (valid) {
            titleText.setText(R.string.launcher_loaded_title);
            importButton.setVisibility(View.GONE);
            changeButton.setVisibility(View.VISIBLE);
            String displayName = getPreferences(MODE_PRIVATE).getString(
                    PREF_DISC_DISPLAY_NAME, getString(isChdImage(disc)
                            ? R.string.launcher_default_chd_name
                            : R.string.launcher_default_bin_name));
            discText.setText(getString(R.string.launcher_disc_file_details,
                    displayName.toUpperCase(java.util.Locale.ROOT), disc.length() / (1024L * 1024L)));
            if (!importInProgress) {
                statusText.setVisibility(View.GONE);
            }
        } else {
            titleText.setText(R.string.launcher_empty_title);
            importButton.setText("CARREGAR ARQUIVO");
            importButton.setContentDescription(getString(R.string.launcher_import_disc_description));
            importButton.setVisibility(View.VISIBLE);
            changeButton.setVisibility(View.GONE);
            discText.setText(R.string.launcher_empty_hint);
            if (present && !importInProgress) {
                statusText.setText(R.string.setup_invalid_disc);
                statusText.setTextColor(COLOR_WARN);
                statusText.setVisibility(View.VISIBLE);
            } else if (!importInProgress) {
                statusText.setVisibility(View.GONE);
            }
        }

        playButton.setText("INICIAR O JOGO");
        playButton.setVisibility(valid ? View.VISIBLE : View.GONE);
        playButton.setEnabled(valid);
        layoutLauncherControls();
    }

    private void startGame() {
        if (importInProgress) {
            return;
        }

        settings.save();
        launchGame();
    }

    private void selectDiscImage() {
        if (importInProgress) {
            return;
        }

        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(intent, REQUEST_DISC_IMAGE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if ((requestCode != REQUEST_DISC_IMAGE) || (resultCode != RESULT_OK) || (data == null) || (data.getData() == null)) {
            return;
        }

        importDiscImage(data.getData());
    }

    private long querySourceSize(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.SIZE}, null, null, null)) {
            if ((cursor != null) && cursor.moveToFirst() && !cursor.isNull(0)) {
                return cursor.getLong(0);
            }
        } catch (Exception | Error exception) {
            return -1;
        }

        return -1;
    }

    private void importDiscImage(Uri uri) {
        importInProgress = true;
        pendingDiscDisplayName = querySourceDisplayName(uri);
        importButton.setEnabled(false);
        changeButton.setEnabled(false);
        playButton.setEnabled(false);

        long sourceSize = querySourceSize(uri);
        importProgress.setIndeterminate(sourceSize <= 0);
        importProgress.setProgress(0);
        importProgress.setVisibility(View.VISIBLE);
        statusText.setVisibility(View.GONE);

        Thread worker = new Thread(() -> copyDiscImage(uri, sourceSize), "CTR disc import");
        worker.start();
    }

    private String querySourceDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if ((cursor != null) && cursor.moveToFirst() && !cursor.isNull(0)) {
                String name = cursor.getString(0);
                if ((name != null) && !name.trim().isEmpty()) {
                    return name.trim();
                }
            }
        } catch (Exception | Error ignored) {
            // The provider may not expose a display name; the UI has a safe fallback.
        }
        return null;
    }

    private void copyDiscImage(Uri uri, long sourceSize) {
        File assetDirectory = getAssetDirectory();
        File temporary = new File(assetDirectory, "ctr-u.importing");

        try {
            if (!assetDirectory.isDirectory() && !assetDirectory.mkdirs()) {
                throw new IOException("Could not create the app asset directory");
            }

            if (temporary.exists() && !temporary.delete()) {
                throw new IOException("Could not replace an incomplete import");
            }

            ContentResolver resolver = getContentResolver();
            try (InputStream input = resolver.openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(temporary)) {
                if (input == null) {
                    throw new IOException("The selected file could not be opened");
                }

                byte[] buffer = new byte[COPY_BUFFER_SIZE];
                long copied = 0;
                int lastProgress = -1;
                int read;

                while ((read = input.read(buffer)) != -1) {
                    if (read == 0) {
                        continue;
                    }

                    output.write(buffer, 0, read);
                    copied += read;

                    if (sourceSize > 0) {
                        int progress = (int)Math.min(100, copied * 100 / sourceSize);
                        if (progress != lastProgress) {
                            lastProgress = progress;
                            int displayProgress = progress;
                            runOnUiThread(() -> updateImportProgress(displayProgress));
                        }
                    }
                }

                output.getFD().sync();
            }

            // What it is decides what it gets called, and the native side looks
            // for both names.
            boolean chd = isChdImage(temporary);

            if (!chd && !isRetailDiscImage(temporary)) {
                throw new IOException("The selected file is not a raw NTSC-U BIN or a CHD image");
            }

            File destination = chd ? getImportedChdImage() : getImportedBinImage();
            File other = chd ? getImportedBinImage() : getImportedChdImage();

            try {
                // rename replaces the destination atomically, preserving it if the import fails.
                Os.rename(temporary.getAbsolutePath(), destination.getAbsolutePath());
            } catch (android.system.ErrnoException exception) {
                throw new IOException("Could not finish the disc image import");
            }

            // Only one disc image may be left behind, or which one loads becomes
            // a matter of which name the native side happens to look for first.
            if (other.exists() && !other.delete()) {
                throw new IOException("Could not remove the previous disc image");
            }

            runOnUiThread(this::finishImport);
        } catch (Exception exception) {
            temporary.delete();
            String detail = exception.getMessage();
            if ((detail == null) || detail.isEmpty()) {
                detail = exception.getClass().getSimpleName();
            }

            String error = detail;
            runOnUiThread(() -> showImportError(error));
        }
    }

    /**
     * Imports land back on the launcher rather than starting the game. Picking a
     * disc is usually the first thing you do, and the settings underneath are
     * the second -- jumping straight into the game skips past them.
     */
    private void finishImport() {
        importInProgress = false;
        if (pendingDiscDisplayName != null) {
            getPreferences(MODE_PRIVATE).edit()
                    .putString(PREF_DISC_DISPLAY_NAME, pendingDiscDisplayName)
                    .apply();
        }
        pendingDiscDisplayName = null;
        importProgress.setVisibility(View.GONE);
        importButton.setEnabled(true);
        changeButton.setEnabled(true);
        refreshDiscStatus();
        statusText.setVisibility(View.GONE);
    }

    private final class OutlinedTextView extends TextView {
        OutlinedTextView() {
            super(CTRNativeLauncherActivity.this);
            setIncludeFontPadding(false);
            setShadowLayer(dp(1), 0, dp(2), Color.BLACK);
        }

        @Override
        protected void onDraw(android.graphics.Canvas canvas) {
            Paint paint = getPaint();
            Paint.Style originalStyle = paint.getStyle();
            int originalColor = getCurrentTextColor();
            float originalStrokeWidth = paint.getStrokeWidth();
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeJoin(Paint.Join.ROUND);
            paint.setStrokeWidth(dp(2));
            setTextColor(Color.BLACK);
            super.onDraw(canvas);
            setTextColor(originalColor);
            paint.setStyle(originalStyle);
            paint.setStrokeWidth(originalStrokeWidth);
            super.onDraw(canvas);
        }
    }

    /** Touch-friendly launcher button whose artwork is drawn by the app. */
    private final class LauncherActionButton extends TextView {
        private final boolean primary;
        private final Paint shapePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path shape = new Path();
        private final int[] gradientColors;

        LauncherActionButton(boolean primary) {
            super(CTRNativeLauncherActivity.this);
            this.primary = primary;
            gradientColors = primary
                    ? new int[]{Color.rgb(255, 179, 24), Color.rgb(255, 132, 0)}
                    : new int[]{Color.rgb(0, 74, 218), Color.rgb(0, 54, 188)};
            setGravity(Gravity.CENTER);
            setTextColor(Color.WHITE);
            setTypeface(CTRFont.get(getContext()));
            setIncludeFontPadding(false);
            setMaxLines(1);
            setPadding(dp(12), 0, dp(12), dp(3));
            setBackgroundColor(Color.TRANSPARENT);
            setClickable(true);
            setFocusable(true);
            setLayerType(View.LAYER_TYPE_SOFTWARE, null);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float width = getWidth();
            float height = getHeight();
            float inset = width * 0.035f;
            shape.reset();
            shape.moveTo(inset, 0);
            shape.lineTo(width - inset, 0);
            shape.lineTo(width - inset * 2.2f, height * 0.88f);
            shape.lineTo(inset * 2.2f, height * 0.88f);
            shape.close();

            shapePaint.setStyle(Paint.Style.FILL);
            shapePaint.setShader(new LinearGradient(0, 0, 0, height,
                    gradientColors[0], gradientColors[1], Shader.TileMode.CLAMP));
            shapePaint.setShadowLayer(dp(4), 0, dp(4), 0x99000000);
            canvas.drawPath(shape, shapePaint);
            shapePaint.clearShadowLayer();
            shapePaint.setShader(null);
            shapePaint.setStyle(Paint.Style.STROKE);
            shapePaint.setStrokeWidth(Math.max(dp(2), height * 0.035f));
            shapePaint.setColor(primary ? Color.rgb(255, 221, 140) : Color.WHITE);
            canvas.drawPath(shape, shapePaint);
            shapePaint.setStyle(Paint.Style.FILL);
            super.onDraw(canvas);
        }
    }

    /** Shared dark panel for the empty and loaded launcher states. */
    private final class LauncherPanelView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

        LauncherPanelView() {
            super(CTRNativeLauncherActivity.this);
            setClickable(false);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float width = getWidth();
            float height = getHeight();
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(0x66000000);
            canvas.drawRect(width * 0.018f, height * 0.06f,
                    width * 0.982f, height * 0.985f, paint);
            paint.setColor(0xCC000000);
            canvas.drawRect(0, 0, width * 0.965f, height * 0.93f, paint);
        }
    }

    private void updateImportProgress(int progress) {
        importProgress.setProgress(progress);
    }

    private void showImportError(String error) {
        importInProgress = false;
        importProgress.setVisibility(View.GONE);
        importButton.setEnabled(true);
        changeButton.setEnabled(true);
        refreshDiscStatus();

        // After refreshDiscStatus, so the reason the import failed is what stays
        // on screen rather than the generic instructions.
        statusText.setText(error);
        statusText.setTextColor(COLOR_WARN);
        statusText.setVisibility(View.VISIBLE);
    }

    private boolean isRetailDiscImage(File image) {
        if (!image.isFile() || (image.length() < (long)(PVD_LBA + 1) * RAW_SECTOR_SIZE) || ((image.length() % RAW_SECTOR_SIZE) != 0)) {
            return false;
        }

        byte[] sector = new byte[RAW_SECTOR_SIZE];
        try (RandomAccessFile file = new RandomAccessFile(image, "r")) {
            file.seek((long)PVD_LBA * RAW_SECTOR_SIZE);
            file.readFully(sector);
        } catch (IOException exception) {
            return false;
        }

        if ((sector[0] != 0) || (sector[11] != 0) || (sector[15] != 2)) {
            return false;
        }

        for (int i = 1; i < 11; i++) {
            if ((sector[i] & 0xff) != 0xff) {
                return false;
            }
        }

        return (sector[FORM1_DATA_OFFSET] == 1)
                && (sector[FORM1_DATA_OFFSET + 1] == 'C')
                && (sector[FORM1_DATA_OFFSET + 2] == 'D')
                && (sector[FORM1_DATA_OFFSET + 3] == '0')
                && (sector[FORM1_DATA_OFFSET + 4] == '0')
                && (sector[FORM1_DATA_OFFSET + 5] == '1')
                && (sector[FORM1_DATA_OFFSET + 6] == 1);
    }

    private void launchGame() {
        // Deliberately not finishing: coming back from the game lands on the
        // launcher again, which is where the settings are.
        startActivity(new Intent(this, CTRNativeActivity.class));
    }
}
