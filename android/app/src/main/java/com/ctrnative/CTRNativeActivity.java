package com.ctrnative;

import android.app.Activity;
import android.app.Dialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.media.MediaPlayer;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.hardware.display.DisplayManager;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.MotionEvent;
import android.view.KeyEvent;
import android.view.View;
import android.view.Surface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.ViewConfiguration;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.Window;
import android.widget.ImageView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public class CTRNativeActivity extends SDLActivity {
    private static final int PICK_DIRECTORY_REQUEST = 1001;
    private static final String PREFS_NAME = CTRAppPreferences.PREFS_NAME;
    private static final String KEY_ASSET_PATH = "assetPath";
    private static final String KEY_SETTINGS_BUTTON_X = "gameSettingsButtonX";
    private static final String KEY_SETTINGS_BUTTON_Y = "gameSettingsButtonY";
    private static final long STARTUP_PROGRESS_DURATION_MS = 25_000L;
    private static final long STARTUP_PROGRESS_OVERRUN_MS = 10_000L;
    private static final long STARTUP_PROGRESS_UPDATE_MS = 100L;
    private static volatile CTRNativeActivity activeActivity;
    private long appliedTouchGeneration;
    private CTRTouchSnapshot lastOverlaySnapshot;
    private final int[] gamepadBindings = CTRGamepadMapping.DEFAULTS.clone();
    private boolean gamepadBindingsApplied;
    private int rootAxisAction = -1;

    private boolean isRootGamepadScreen() {
        return startupOverlay == null && lastOverlaySnapshot != null
                && lastOverlaySnapshot.screen == CTRTouchOverlay.SCREEN_ROOT_MENU
                && touchOverlay != null && !rootMenuOptionsDialogOpen;
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (isRootGamepadScreen() && CTRGamepadActivity.isGamepad(event.getDevice())) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0)
                touchOverlay.handleRootGamepadAction(CTRGamepadMapping.actionForBinding(
                        gamepadBindings, CTRGamepadMapping.bindingForKey(event.getKeyCode())));
            return true;
        }
        if (mSurface != null && CTRGamepadActivity.isGamepad(event.getDevice()))
            return SDLActivity.handleKeyEvent(mSurface, event.getKeyCode(), event, null);
        return super.dispatchKeyEvent(event);
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (isRootGamepadScreen() && CTRGamepadActivity.isGamepad(event.getDevice())) {
            float y = event.getAxisValue(MotionEvent.AXIS_Y);
            float hat = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            int binding = hat < -.5f ? 11 : hat > .5f ? 12 : hatX < -.5f ? 13 : hatX > .5f ? 14 : -1;
            if (Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)) > .75f)
                binding = 0x4004;
            if (Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)) > .75f)
                binding = 0x4005;
            int action = binding >= 0 ? CTRGamepadMapping.actionForBinding(gamepadBindings, binding)
                    : y < -.5f ? 12 : y > .5f ? 13 : -1;
            if (action != rootAxisAction && action >= 0) touchOverlay.handleRootGamepadAction(action);
            rootAxisAction = action;
            return true;
        }
        rootAxisAction = -1;
        if (mSurface != null && CTRGamepadActivity.isGamepad(event.getDevice()))
            return org.libsdl.app.SDLControllerManager.handleJoystickMotionEvent(event);
        return super.dispatchGenericMotionEvent(event);
    }
    static boolean isTouchTraceEnabled() {
        CTRNativeActivity activity = activeActivity;
        return activity != null
                && (activity.getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
                && Log.isLoggable("CTR-UI", Log.DEBUG);
    }
    private final Handler startupProgressHandler = new Handler(Looper.getMainLooper());
    private FrameLayout startupOverlay;
    private ImageView startupImage;
    private ProgressBar startupProgress;
    private long startupProgressStartedAt;
    private Runnable startupProgressUpdater;
    private MediaPlayer startupMusic;

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "ctr_native"
        };
    }

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
        activeActivity = this;
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        // The game half needs this as much as the launcher does: a native crash
        // after PLAY looks identical to a launcher crash from the outside.
        CTRCrashLog.install(getApplicationContext());

        requestHighestRefreshRate(getWindow(), getWindowManager().getDefaultDisplay());
        enterImmersiveMode();

        startTouchControls();

        // Open the panel now rather than waiting for the first EndScene: the
        // boot splash presents VRAM directly and never reaches that path, so the
        // bottom screen would otherwise stay dark until the Naughty Dog logo.
        // The Presentation's own background is black, so the panel lights up
        // immediately and the idle art follows once assets are loaded.
        startCompanionDisplay();

        // Cover SDL's initial blank surface while the native game initializes.
        showStartupImage();

        checkStoragePermission();
    }

    private void showStartupImage() {
        if (mLayout == null) return;

        startupOverlay = new FrameLayout(this);
        startupOverlay.setClickable(false);
        startupOverlay.setBackgroundColor(Color.BLACK);
        startupImage = new ImageView(this);
        startupImage.setImageResource(R.drawable.loading);
        startupImage.setScaleType(ImageView.ScaleType.CENTER_CROP);
        startupImage.setClickable(false);
        startupImage.setFocusable(false);
        startupOverlay.addView(startupImage, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        startupProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        startupProgress.setIndeterminate(false);
        startupProgress.setMax(100);
        startupProgress.setProgress(0);
        startupProgress.setClickable(false);
        startupProgress.setFocusable(false);
        FrameLayout.LayoutParams progressParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(8), Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        startupOverlay.addView(startupProgress, progressParams);

        // Keep the loading art above SDL's touch overlay, which is added with
        // addContentView() and draws the native Android menu on top of mLayout.
        addContentView(startupOverlay, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        startupOverlay.bringToFront();
        startupOverlay.post(() -> {
            if (startupProgress == null || startupOverlay == null) return;
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) startupProgress.getLayoutParams();
            params.width = Math.round(startupOverlay.getWidth() * 0.32f);
            params.bottomMargin = Math.round(startupOverlay.getHeight() * 0.07f);
            startupProgress.setLayoutParams(params);
        });
        startStartupProgress();
        startStartupMusic();
    }

    private void startStartupMusic() {
        if (startupOverlay == null) return;
        try {
            if (startupMusic == null) {
                startupMusic = MediaPlayer.create(this, R.raw.loading);
                if (startupMusic == null) {
                    Log.e(CTRDS_TAG, "Could not open startup music");
                    return;
                }
                startupMusic.setLooping(true);
                startupMusic.setOnErrorListener((player, what, extra) -> {
                    Log.e(CTRDS_TAG, "Startup music error: " + what + "/" + extra);
                    stopStartupMusic();
                    return true;
                });
            }
            if (!startupMusic.isPlaying()) startupMusic.start();
        } catch (RuntimeException error) {
            Log.e(CTRDS_TAG, "Could not play startup music", error);
            stopStartupMusic();
        }
    }

    private void stopStartupMusic() {
        if (startupMusic != null) {
            startupMusic.release();
            startupMusic = null;
        }
    }

    private void startStartupProgress() {
        startupProgressStartedAt = SystemClock.uptimeMillis();
        startupProgressUpdater = new Runnable() {
            @Override
            public void run() {
                if (startupProgress == null) return;

                long elapsed = SystemClock.uptimeMillis() - startupProgressStartedAt;
                int progress;
                if (elapsed <= STARTUP_PROGRESS_DURATION_MS) {
                    progress = (int) (elapsed * 90L / STARTUP_PROGRESS_DURATION_MS);
                } else {
                    long overrun = Math.min(elapsed - STARTUP_PROGRESS_DURATION_MS, STARTUP_PROGRESS_OVERRUN_MS);
                    progress = 90 + (int) (overrun * 9L / STARTUP_PROGRESS_OVERRUN_MS);
                }
                startupProgress.setProgress(progress);
                if (progress < 99) {
                    startupProgressHandler.postDelayed(this, STARTUP_PROGRESS_UPDATE_MS);
                }
            }
        };
        startupProgressHandler.post(startupProgressUpdater);
    }

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    public static void onNativeMainMenuReady() {
        CTRNativeActivity activity = activeActivity;
        if (activity == null) return;

        activity.runOnUiThread(() -> {
            activity.finishStartupOverlay();
        });
    }

    public static void onNativeTouchScreenChanged() {
        CTRNativeActivity activity = activeActivity;
        if (activity != null && activity.touchHandler != null) {
            activity.touchHandler.removeCallbacks(activity.touchTransitionUpdate);
            activity.touchHandler.post(activity.touchTransitionUpdate);
        }
    }

    public static void showCampaignStorageError() {
        Activity activity = (Activity) SDLActivity.getContext();
        if (activity != null) {
            activity.runOnUiThread(() -> android.widget.Toast.makeText(activity,
                    "Não foi possível salvar a campanha. Tente novamente.",
                    android.widget.Toast.LENGTH_LONG).show());
        }
    }

    private void finishStartupOverlay() {
        if (startupOverlay == null) return;
        if (startupProgressUpdater != null) {
            startupProgressHandler.removeCallbacks(startupProgressUpdater);
        }
        // Native readiness follows the swap; apply its menu before uncovering SDL.
        updateTouchControls();
        stopStartupMusic();
        nativeFinishStartupAudio();
        removeStartupOverlay();
    }

    private void removeStartupOverlay() {
        if (startupOverlay == null) return;
        ViewGroup parent = (ViewGroup) startupOverlay.getParent();
        if (parent != null) parent.removeView(startupOverlay);
        startupOverlay = null;
        startupImage = null;
        startupProgress = null;
        startupProgressUpdater = null;
    }

    @Override
    protected void onDestroy() {
        stopStartupMusic();
        if (activeActivity == this) activeActivity = null;
        startupProgressHandler.removeCallbacksAndMessages(null);
        if (touchHandler != null) touchHandler.removeCallbacksAndMessages(null);
        if (touchOverlay != null) touchOverlay.releaseAllTouches();
        super.onDestroy();
    }

    @Override
    public void finish() {
        super.finish();
        if (Build.VERSION.SDK_INT < 34) overridePendingTransition(0, 0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        SharedPreferences gamepadPreferences = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        for (int i = 0; i < gamepadBindings.length; i++) {
            int binding = gamepadPreferences.getInt(CTRGamepadMapping.PREFIX + i, CTRGamepadMapping.DEFAULTS[i]);
            gamepadBindings[i] = CTRGamepadMapping.isValid(binding) ? binding : CTRGamepadMapping.DEFAULTS[i];
        }
        gamepadBindingsApplied = false;
        startStartupMusic();
        enterImmersiveMode();
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        if (touchOverlay != null) {
            touchOverlay.refreshCurrentLayout();
        }
    }

    @Override
    protected void onPause() {
        if (startupMusic != null && startupMusic.isPlaying()) startupMusic.pause();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // The bars come back on their own after a swipe or a focus change,
            // and each time they do the window shrinks and the game letterboxes.
            enterImmersiveMode();
        }
    }

    /**
     * Theme.NoTitleBar.Fullscreen no longer hides the status and gesture-nav
     * bars on modern Android: they stay, the window comes back short of the
     * panel height (1920x970 rather than 1920x1080), and the game is letterboxed
     * inside it. Hide them explicitly so the window is the whole display.
     */
    private void enterImmersiveMode() {
        applyImmersiveMode(getWindow());
    }

    private void applyImmersiveMode(Window window) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false);

                WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                    controller.setSystemBarsBehavior(
                            WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                window.getDecorView().setSystemUiVisibility(
                        android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                                | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
            }

            // Draw into the display cutout area too, rather than letterboxing
            // away from it.
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams lp = window.getAttributes();
                lp.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                window.setAttributes(lp);
            }
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "immersive mode failed: " + e.getMessage());
        }
    }

    private CTRTouchOverlay touchOverlay;
    private View gameSettingsButton;
    private Handler touchHandler;
    private final Runnable touchTransitionUpdate = this::updateTouchControls;
    private CTRSubmitNameDialog submitNameDialog;
    private boolean submitNameDialogRequested;
    private Dialog pauseOptionsDialog;
    private boolean pauseOptionsDialogRequested;
    private Dialog adventureCampaignDialog;
    private View adventureCampaignDialogContent;
    private boolean nativeTouchLayerActive;
    private boolean rootMenuOptionsDialogOpen;
    private boolean gameSettingsButtonCompact;
    private static final long TOUCH_SYNC_INTERVAL_MS = 50;

    /** Poll dynamic controller state and menu snapshots; screen transitions also notify us from native. */
    private void startTouchControls() {
        try {
            touchOverlay = new CTRTouchOverlay(this);
            touchOverlay.setVisibility(View.GONE);

            addContentView(touchOverlay, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    Gravity.FILL));

            touchHandler = new Handler(Looper.getMainLooper());
            touchHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    updateTouchControls();
                    applyPreferredFrameRate();
                    touchHandler.postDelayed(this, TOUCH_SYNC_INTERVAL_MS);
                }
            }, TOUCH_SYNC_INTERVAL_MS);
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "touch controls unavailable: " + e.getMessage());
        }
        ensureGameSettingsButton();
    }

    /** Keep settings reachable independently from the visibility of race/menu touch controls. */
    private void ensureGameSettingsButton() {
        if (gameSettingsButton == null) {
            FrameLayout gear = new FrameLayout(this);
            GradientDrawable gearBackground = new GradientDrawable();
            gearBackground.setShape(GradientDrawable.OVAL);
            gearBackground.setColor(0xBB000000);
            gearBackground.setStroke(dp(2), Color.rgb(220, 244, 255));
            gear.setBackground(gearBackground);
            gear.setAlpha(0.7f);
            gear.setClickable(true);
            gear.setFocusable(true);
            gear.setContentDescription("Abrir configurações");

            ImageView gearIcon = new ImageView(this);
            gearIcon.setImageResource(R.drawable.launcher_settings_white);
            gearIcon.setScaleType(ImageView.ScaleType.FIT_CENTER);
            gear.addView(gearIcon, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
            gear.setOnClickListener(view -> {
                if (touchOverlay != null) touchOverlay.pauseRaceForSettings();
                showInGameSettings();
            });

            gameSettingsButton = gear;
            addContentView(gear, new FrameLayout.LayoutParams(dp(36), dp(36),
                    Gravity.TOP | Gravity.LEFT));
            gear.post(() -> {
                ViewGroup parent = (ViewGroup) gear.getParent();
                int rootHeight = gear.getRootView().getHeight();
                int edge = Math.round(rootHeight * 0.105f);
                int iconPadding = dp(4);
                gearIcon.setPadding(iconPadding, iconPadding, iconPadding, iconPadding);
                FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) gear.getLayoutParams();
                params.width = edge;
                params.height = edge;
                int maxX = Math.max(0, parent.getWidth() - edge);
                int maxY = Math.max(0, parent.getHeight() - edge);
                int defaultPosition = Math.round(rootHeight * 0.025f);
                SharedPreferences prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
                float defaultX = maxX == 0 ? 0 : Math.min(1f, (float) defaultPosition / maxX);
                float defaultY = maxY == 0 ? 0 : Math.min(1f, (float) defaultPosition / maxY);
                params.leftMargin = Math.round(prefs.getFloat(KEY_SETTINGS_BUTTON_X, defaultX) * maxX);
                params.topMargin = Math.round(prefs.getFloat(KEY_SETTINGS_BUTTON_Y, defaultY) * maxY);
                gear.setLayoutParams(params);
                installSettingsButtonDrag(gear, parent);
            });
            return;
        }

        gameSettingsButton.setVisibility(View.VISIBLE);
        if (touchOverlay == null || gameSettingsButton.getParent() == null) return;
        ViewGroup parent = (ViewGroup) gameSettingsButton.getParent();
        if (parent.indexOfChild(gameSettingsButton) < parent.indexOfChild(touchOverlay)) {
            gameSettingsButton.bringToFront();
        }
    }

    private void installSettingsButtonDrag(View button, ViewGroup parent) {
        int touchSlop = ViewConfiguration.get(this).getScaledTouchSlop();
        float[] gesture = new float[4];
        boolean[] dragged = {false};
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    gesture[0] = event.getRawX();
                    gesture[1] = event.getRawY();
                    gesture[2] = view.getX();
                    gesture[3] = view.getY();
                    dragged[0] = false;
                    return true;
                case MotionEvent.ACTION_MOVE:
                    float dx = event.getRawX() - gesture[0];
                    float dy = event.getRawY() - gesture[1];
                    if (!dragged[0] && Math.hypot(dx, dy) > touchSlop) dragged[0] = true;
                    if (dragged[0]) {
                        view.setX(Math.max(0, Math.min(parent.getWidth() - view.getWidth(), gesture[2] + dx)));
                        view.setY(Math.max(0, Math.min(parent.getHeight() - view.getHeight(), gesture[3] + dy)));
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    if (dragged[0]) saveSettingsButtonPosition(view, parent);
                    else view.performClick();
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    if (dragged[0]) saveSettingsButtonPosition(view, parent);
                    return true;
                default:
                    return true;
            }
        });
    }

    private void updateGameSettingsButtonSize(int screenState) {
        if (gameSettingsButton == null || gameSettingsButton.getLayoutParams() == null) return;
        boolean compact = screenState == CTRTouchOverlay.SCREEN_ADVENTURE_ARENA;
        if (gameSettingsButtonCompact == compact) return;
        int rootHeight = gameSettingsButton.getRootView().getHeight();
        if (rootHeight <= 0) return;
        gameSettingsButtonCompact = compact;
        int edge = Math.round(rootHeight * (compact ? 0.084f : 0.105f));
        ViewGroup.LayoutParams params = gameSettingsButton.getLayoutParams();
        params.width = edge;
        params.height = edge;
        gameSettingsButton.setLayoutParams(params);
    }

    private void saveSettingsButtonPosition(View button, ViewGroup parent) {
        float maxX = Math.max(0, parent.getWidth() - button.getWidth());
        float maxY = Math.max(0, parent.getHeight() - button.getHeight());
        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                .putFloat(KEY_SETTINGS_BUTTON_X, maxX == 0 ? 0 : button.getX() / maxX)
                .putFloat(KEY_SETTINGS_BUTTON_Y, maxY == 0 ? 0 : button.getY() / maxY)
                .apply();
    }

    private void showInGameSettings() {
        File storage = getExternalFilesDir(null);
        if (storage == null) storage = getFilesDir();
        CTRDSSettings settings = new CTRDSSettings(new File(new File(storage, "assets"), "ctrds.cfg"));

        Dialog dialog = new Dialog(this, android.R.style.Theme_Translucent_NoTitleBar_Fullscreen);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.TRANSPARENT);
        FrameLayout panelContainer = new FrameLayout(this);
        GradientDrawable panel = new GradientDrawable();
        panel.setColor(0xB8000000);
        panelContainer.setBackground(panel);
        root.addView(panelContainer);

        TextView heading = makeSettingsText("CONFIGURAÇÕES", Color.WHITE, 20, Gravity.CENTER);
        heading.setShadowLayer(dp(2), 0, dp(2), Color.BLACK);
        panelContainer.addView(heading);

        TextView close = makeSettingsText("×", Color.WHITE, 30, Gravity.CENTER);
        close.setContentDescription("Fechar configurações");
        close.setOnClickListener(view -> dialog.dismiss());
        makeGamepadFocusable(close);
        panelContainer.addView(close);

        TextView controls = makeSettingsText("CONTROLES", Color.WHITE, 14, Gravity.CENTER);
        GradientDrawable controlsBg = new GradientDrawable();
        controlsBg.setColor(Color.rgb(255, 166, 0));
        controlsBg.setStroke(dp(2), Color.WHITE);
        controls.setBackground(controlsBg);
        controls.setOnClickListener(view -> startActivity(new Intent(this, CTRControlsActivity.class)));
        makeGamepadFocusable(controls);
        panelContainer.addView(controls);

        LinearLayout options = new LinearLayout(this);
        options.setOrientation(LinearLayout.VERTICAL);
        options.setPadding(dp(10), dp(6), dp(10), dp(6));
        for (int i = 0; i < CTRDSSettings.OPTIONS.length; i++) {
            CTRDSSettings.Option option = CTRDSSettings.OPTIONS[i];
            TextView value = makeSettingsText(settings.nameOf(option).toUpperCase(java.util.Locale.ROOT),
                    Color.rgb(255, 194, 32), 13, Gravity.CENTER);
            value.setShadowLayer(dp(1), 0, dp(1), Color.rgb(74, 45, 0));
            CTRSettingsOptionRow row = new CTRSettingsOptionRow(this, option, value,
                    CTRSettingsOptionRow.Style.IN_GAME,
                    direction -> cycleInGameSetting(settings, option, value, direction));
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(44));
            rowParams.bottomMargin = dp(6);
            options.addView(row, rowParams);
        }

        CTRDSSettings.Option quickStart = CTRDSSettings.QUICK_START;
        String quickStartName = CTRAppPreferences.quickStartName(this);
        TextView quickStartValue = makeSettingsText(quickStartName.toUpperCase(
                java.util.Locale.ROOT), Color.rgb(255, 194, 32), 13, Gravity.CENTER);
        quickStartValue.setShadowLayer(dp(1), 0, dp(1), Color.rgb(74, 45, 0));
        CTRSettingsOptionRow quickStartRow = new CTRSettingsOptionRow(this, quickStart,
                quickStartValue, CTRSettingsOptionRow.Style.IN_GAME,
                direction -> cycleQuickStartSetting(quickStartValue, direction));
        options.addView(quickStartRow, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(44)));

        ScrollView scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setClipToPadding(false);
        scroll.addView(options);
        panelContainer.addView(scroll);

        dialog.setContentView(root);
        dialog.setCancelable(true);
        dialog.setCanceledOnTouchOutside(true);
        dialog.setOnKeyListener((ignored, key, event) -> {
            if (!CTRGamepadActivity.isGamepad(event.getDevice()) || key != KeyEvent.KEYCODE_BUTTON_B)
                return false;
            if (event.getAction() == KeyEvent.ACTION_UP) dialog.dismiss();
            return true;
        });
        dialog.show();
        WindowManager.LayoutParams windowParams = dialog.getWindow().getAttributes();
        dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        windowParams.dimAmount = 0.68f;
        dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        dialog.getWindow().setAttributes(windowParams);
        dialog.getWindow().setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        applyImmersiveMode(dialog.getWindow());
        dialog.getWindow().setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);

        root.post(() -> {
            int width = root.getWidth();
            int height = root.getHeight();
            if (width <= 0 || height <= 0) return;
            float panelX = 0;
            float panelY = 0;
            float panelWidth = width;
            float panelHeight = height;
            placeSettingsView(panelContainer, panelX, panelY, panelWidth, panelHeight,
                    Gravity.TOP | Gravity.LEFT);
            float unit = Math.min(panelWidth, panelHeight);
            heading.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.055f);
            placeSettingsView(heading, panelWidth * 0.25f, panelHeight * 0.045f,
                    panelWidth * 0.50f, panelHeight * 0.12f, Gravity.TOP | Gravity.LEFT);
            placeSettingsView(close, panelWidth * 0.025f, panelHeight * 0.045f,
                    Math.min(dp(48), panelHeight * 0.12f), Math.min(dp(48), panelHeight * 0.12f),
                    Gravity.TOP | Gravity.LEFT);
            placeSettingsView(controls, panelWidth * 0.76f, panelHeight * 0.045f,
                    panelWidth * 0.21f, panelHeight * 0.12f, Gravity.TOP | Gravity.LEFT);
            placeSettingsView(scroll, panelWidth * 0.03f, panelHeight * 0.20f,
                    panelWidth * 0.94f, panelHeight * 0.79f, Gravity.TOP | Gravity.LEFT);
        });
    }

    void showRootMenuOptions() {
        if (pauseOptionsDialog != null && pauseOptionsDialog.isShowing()) return;
        rootMenuOptionsDialogOpen = true;
        showAndroidPauseOptions();
    }

    private void showAndroidPauseOptions() {
        Dialog dialog = new Dialog(this, android.R.style.Theme_Translucent_NoTitleBar_Fullscreen);
        pauseOptionsDialog = dialog;

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.TRANSPARENT);
        View chrome = new View(this) {
            private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

            @Override protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                float width = getWidth();
                float height = getHeight();
                paint.setStyle(Paint.Style.FILL);
                paint.setColor(rootMenuOptionsDialogOpen ? 0x99000000 : 0x66000000);
                canvas.drawRect(width * 0.08f, height * 0.22f,
                        width * 0.92f, height * 0.84f, paint);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(dp(2));
                paint.setColor(Color.WHITE);
                canvas.drawRect(width * 0.08f, height * 0.22f,
                        width * 0.92f, height * 0.84f, paint);

            }
        };
        root.addView(chrome, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        TextView heading = makeSettingsText("OPÇÕES", Color.WHITE, 20, Gravity.CENTER);
        root.addView(heading);

        View controls = new View(this) {
            @Override protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                if (touchOverlay != null) {
                    touchOverlay.drawPauseOptionsControlsButton(canvas, getWidth(), getHeight(),
                            Math.min(root.getWidth(), root.getHeight()));
                }
            }
        };
        controls.setContentDescription("Configurar controles de toque e gamepad");
        makeGamepadFocusable(controls);
        controls.setOnClickListener(view ->
                startActivity(new Intent(this, CTRControlsActivity.class)));
        root.addView(controls);

        View back = new View(this) {
            @Override protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                if (touchOverlay != null) {
                    touchOverlay.drawPauseOptionsBackButton(canvas, getWidth(), getHeight(),
                            Math.min(root.getWidth(), root.getHeight()));
                }
            }
        };
        back.setContentDescription("Voltar ao jogo");
        makeGamepadFocusable(back);
        back.setOnClickListener(view -> dialog.dismiss());
        root.addView(back);

        LinearLayout options = new LinearLayout(this);
        options.setOrientation(LinearLayout.VERTICAL);
        String[] labels = { "EFEITOS", "MÚSICA", "VOZ" };
        for (int i = 0; i < labels.length; i++) {
            int initialVolume;
            try {
                initialVolume = nativeGameAudioVolume(i);
            } catch (UnsatisfiedLinkError error) {
                initialVolume = 255;
            }
            if (initialVolume < 0) initialVolume = 255;

            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setGravity(Gravity.CENTER_VERTICAL);

            LinearLayout labelRow = new LinearLayout(this);
            labelRow.setGravity(Gravity.CENTER_VERTICAL);
            TextView label = makeSettingsText(labels[i], Color.WHITE, 16, Gravity.CENTER_VERTICAL);
            labelRow.addView(label, new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            TextView percent = makeSettingsText((initialVolume * 100 / 255) + "%",
                    Color.WHITE, 15, Gravity.RIGHT | Gravity.CENTER_VERTICAL);
            labelRow.addView(percent, new LinearLayout.LayoutParams(dp(54),
                    ViewGroup.LayoutParams.WRAP_CONTENT));
            row.addView(labelRow);

            SeekBar volume = new SeekBar(this);
            volume.setMax(255);
            volume.setProgress(initialVolume);
            volume.setContentDescription("Volume de " + labels[i].toLowerCase(java.util.Locale.ROOT));
            CTRSeekBarStyle.apply(volume, Color.rgb(255, 166, 0));
            final int volumeType = i;
            volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override
                public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    percent.setText((progress * 100 / 255) + "%");
                    if (fromUser) nativeSetGameAudioVolume(volumeType, progress);
                }

                @Override public void onStartTrackingTouch(SeekBar seekBar) {}
                @Override public void onStopTrackingTouch(SeekBar seekBar) {}
            });
            row.addView(volume, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(32)));
            LinearLayout.LayoutParams rowParams = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
            options.addView(row, rowParams);
        }

        root.addView(options);
        dialog.setContentView(root);
        dialog.setCancelable(true);
        dialog.setCanceledOnTouchOutside(true);
        dialog.setOnDismissListener(ignored -> {
            pauseOptionsDialog = null;
            rootMenuOptionsDialogOpen = false;
            try {
                if (nativeTouchScreenState() == CTRTouchOverlay.SCREEN_PAUSE_OPTIONS
                        && touchOverlay != null) {
                    touchOverlay.closePauseOptions();
                }
            } catch (UnsatisfiedLinkError error) {
                Log.e(CTRDS_TAG, "options close bridge missing: " + error.getMessage());
            }
        });
        dialog.setOnKeyListener((ignored, key, event) -> {
            if (!CTRGamepadActivity.isGamepad(event.getDevice()) || key != KeyEvent.KEYCODE_BUTTON_B)
                return false;
            if (event.getAction() == KeyEvent.ACTION_UP) dialog.dismiss();
            return true;
        });
        dialog.show();
        dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        dialog.getWindow().addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        WindowManager.LayoutParams window = dialog.getWindow().getAttributes();
        window.dimAmount = 0.68f;
        dialog.getWindow().setAttributes(window);
        dialog.getWindow().setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        applyImmersiveMode(dialog.getWindow());
        dialog.getWindow().setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        root.post(() -> {
            int width = root.getWidth();
            int height = root.getHeight();
            if (width <= 0 || height <= 0) return;
            float unit = Math.min(width, height);
            heading.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * 0.055f);
            placeSettingsView(heading, width * 0.32f, height * 0.245f,
                    width * 0.36f, height * 0.09f, Gravity.TOP | Gravity.LEFT);
            placeSettingsView(controls, width * 0.56f, height * 0.035f,
                    width * 0.20f, height * 0.12f, Gravity.TOP | Gravity.LEFT);
            placeSettingsView(back, width * 0.78f, height * 0.035f,
                    width * 0.20f, height * 0.12f, Gravity.TOP | Gravity.LEFT);
            options.setPadding(dp(8), dp(6), dp(8), dp(6));
            placeSettingsView(options, width * 0.10f, height * 0.34f,
                    width * 0.80f, height * 0.45f, Gravity.TOP | Gravity.LEFT);
            for (int i = 0; i < options.getChildCount(); i++) {
                LinearLayout row = (LinearLayout) options.getChildAt(i);
                LinearLayout labelRow = (LinearLayout) row.getChildAt(0);
                ((TextView) labelRow.getChildAt(0)).setTextSize(
                        TypedValue.COMPLEX_UNIT_PX, unit * 0.043f);
                ((TextView) labelRow.getChildAt(1)).setTextSize(
                        TypedValue.COMPLEX_UNIT_PX, unit * 0.040f);
                labelRow.setLayoutParams(new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, Math.round(unit * 0.050f)));
                SeekBar volume = (SeekBar) row.getChildAt(1);
                volume.setMinimumHeight(Math.round(unit * 0.050f));
                volume.setLayoutParams(new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, Math.round(unit * 0.060f)));
            }
        });
    }

    private void cycleInGameSetting(CTRDSSettings settings, CTRDSSettings.Option option,
                                    TextView value, int direction) {
        settings.cycle(option, direction);
        value.setText(settings.nameOf(option).toUpperCase(java.util.Locale.ROOT));
        settings.save();
    }

    private void cycleQuickStartSetting(TextView value, int direction) {
        value.setText(CTRAppPreferences.cycleQuickStart(this, direction)
                .toUpperCase(java.util.Locale.ROOT));
    }

    private TextView makeSettingsText(String text, int color, float size, int gravity) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextColor(color);
        view.setTypeface(CTRFont.get(this));
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, size);
        view.setGravity(gravity);
        view.setIncludeFontPadding(false);
        view.setSingleLine(true);
        view.setClickable(true);
        return view;
    }

    private void makeGamepadFocusable(View view) {
        view.setFocusable(true);
        android.graphics.drawable.StateListDrawable states = new android.graphics.drawable.StateListDrawable();
        GradientDrawable focused = new GradientDrawable();
        focused.setColor(Color.TRANSPARENT);
        focused.setStroke(dp(3), Color.rgb(255, 166, 0));
        states.addState(new int[]{android.R.attr.state_focused}, focused);
        states.addState(new int[]{}, view.getBackground() != null
                ? view.getBackground() : new ColorDrawable(Color.TRANSPARENT));
        view.setBackground(states);
    }

    private void placeSettingsView(View view, float x, float y, float width, float height, int gravity) {
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                Math.max(1, Math.round(width)), Math.max(1, Math.round(height)), gravity);
        params.leftMargin = Math.round(x);
        params.topMargin = Math.round(y);
        view.setLayoutParams(params);
    }

    private void updateTouchControls() {
        ensureGameSettingsButton();
        if (touchOverlay == null) {
            return;
        }

        int pads;
        try {
            pads = nativeGetGamepadCount();
        } catch (UnsatisfiedLinkError e) {
            Log.e(CTRDS_TAG, "nativeGetGamepadCount not bound: " + e.getMessage());
            return;
        }

        int mode = 0;
        try {
            mode = nativeTouchControlsMode();
        } catch (UnsatisfiedLinkError e) {
            // leave on automatic
        }

        CTRTouchSnapshot snapshot;
        try {
            nativeTouchTrace(isTouchTraceEnabled());
            snapshot = CTRTouchSnapshot.parse(nativeTouchSnapshot());
            if (!gamepadBindingsApplied) {
                nativeSetGamepadBindings(gamepadBindings);
                gamepadBindingsApplied = true;
            }
        } catch (UnsatisfiedLinkError e) {
            return;
        }
        if (snapshot == null || snapshot.generation < appliedTouchGeneration) return;
        boolean changed = snapshot.generation != appliedTouchGeneration;
        if (changed && isTouchTraceEnabled()) {
            Log.d("CTR-UI", "receive ms=" + SystemClock.uptimeMillis()
                    + " frame=" + snapshot.frame + " gen=" + snapshot.generation
                    + " screen=" + snapshot.screen + " producedNs=" + snapshot.producedNs);
        }
        appliedTouchGeneration = snapshot.generation;
        int screenState = snapshot.screen;
        String menuSnapshot = snapshot.menu;
        String adventureCampaignSnapshot = snapshot.campaign;
        String adventurePauseSnapshot = snapshot.pause;

        if (screenState == CTRTouchOverlay.SCREEN_PAUSE_OPTIONS) {
            if (!pauseOptionsDialogRequested) {
                pauseOptionsDialogRequested = true;
                showAndroidPauseOptions();
            }
        } else if (screenState == CTRTouchOverlay.SCREEN_ROOT_MENU
                && rootMenuOptionsDialogOpen) {
            pauseOptionsDialogRequested = false;
        } else {
            rootMenuOptionsDialogOpen = false;
            pauseOptionsDialogRequested = false;
            if (pauseOptionsDialog != null && pauseOptionsDialog.isShowing()) {
                pauseOptionsDialog.dismiss();
            }
            pauseOptionsDialog = null;
        }

        if (screenState == CTRTouchOverlay.SCREEN_NAME_ENTRY) {
            if (!submitNameDialogRequested) {
                submitNameDialogRequested = true;
                submitNameDialog = new CTRSubmitNameDialog(this, snapshot.name, (name, save) -> {
                    try {
                        nativeTouchSubmitName(name, save);
                    } catch (UnsatisfiedLinkError e) {
                        Log.e(CTRDS_TAG, "native name submit bridge missing: " + e.getMessage());
                    }
                });
                submitNameDialog.show();
            }
        } else {
            submitNameDialogRequested = false;
            if (submitNameDialog != null && submitNameDialog.isShowing()) {
                submitNameDialog.dismiss();
            }
            submitNameDialog = null;
        }

        final boolean touchOverlayRequired = screenState == CTRTouchOverlay.SCREEN_DEMO
                || screenState == CTRTouchOverlay.SCREEN_CUTSCENE
                || screenState == CTRTouchOverlay.SCREEN_MASK_HINT
                || screenState == CTRTouchOverlay.SCREEN_PAUSE_RESULTS
                || screenState == CTRTouchOverlay.SCREEN_RACE_RESULTS_MENU
                || screenState == CTRTouchOverlay.SCREEN_LOADING;
        final boolean gameplay = screenState == CTRTouchOverlay.SCREEN_RACE
                || screenState == CTRTouchOverlay.SCREEN_ADVENTURE_ARENA;
        final boolean wanted = (mode != 2) || touchOverlayRequired || !gameplay;
        final boolean showControls = screenState != CTRTouchOverlay.SCREEN_MASK_HINT
                && screenState != CTRTouchOverlay.SCREEN_LOADING
                && !(gameplay && pads > 0)
                && ((mode == 1) || ((mode != 2) && (pads == 0)));
        if (wanted) {
            touchOverlay.beginSnapshot(snapshot);
            if (!snapshot.hasSameContent(lastOverlaySnapshot)) {
                touchOverlay.setGameMenuSnapshot(menuSnapshot);
                touchOverlay.setAdventureCampaignSnapshot(adventureCampaignSnapshot);
                touchOverlay.setAdventurePauseSnapshot(screenState == CTRTouchOverlay.SCREEN_PAUSE_MENU
                        ? adventurePauseSnapshot : "");
                touchOverlay.setAdventureHintSnapshot(screenState == CTRTouchOverlay.SCREEN_PAUSE_SUBMENU
                        ? adventurePauseSnapshot : "");
                touchOverlay.setCharacterSelectName(snapshot.character);
                touchOverlay.setSelectionText(snapshot.selection);
                touchOverlay.setRaceResultsSnapshot(snapshot.results);
                lastOverlaySnapshot = snapshot;
            }
            updateGameSettingsButtonSize(screenState);
            if (gameSettingsButton != null) {
                gameSettingsButton.setVisibility(screenState == CTRTouchOverlay.SCREEN_MASK_HINT
                        || screenState == CTRTouchOverlay.SCREEN_LOADING
                        || (gameplay && pads > 0)
                        ? View.GONE : View.VISIBLE);
            }
            touchOverlay.setControlsVisible(showControls);
        }

        boolean showCampaignScreen = wanted
                && screenState == CTRTouchOverlay.SCREEN_GAME_MENU
                && (touchOverlay.isAdventureCampaignManagement()
                || touchOverlay.isAdventureCampaignDeleteConfirmation());
        if (showCampaignScreen) {
            showAdventureCampaignDialog();
            if (touchOverlay.getVisibility() != View.GONE) {
                touchOverlay.setVisibility(View.GONE);
            }
            if (adventureCampaignDialogContent != null) {
                adventureCampaignDialogContent.invalidate();
            }
        } else {
            dismissAdventureCampaignDialog();
            if (wanted && touchOverlay.getVisibility() != View.VISIBLE) {
                touchOverlay.setVisibility(View.VISIBLE);
            } else if (!wanted && touchOverlay.getVisibility() != View.GONE) {
                touchOverlay.releaseAllTouches();
                touchOverlay.setVisibility(View.GONE);
            }
        }

        if (wanted != nativeTouchLayerActive) {
            try {
                nativeTouchSetActive(wanted);
            } catch (UnsatisfiedLinkError e) {
                // native not up yet
            }
            nativeTouchLayerActive = wanted;

            Log.i(CTRDS_TAG, "touch layer " + (wanted ? "active" : "hidden")
                    + " (gamepads=" + pads + ", mode=" + mode + ")");
        }
    }

    private void showAdventureCampaignDialog() {
        if (adventureCampaignDialog != null && adventureCampaignDialog.isShowing()) return;

        Dialog dialog = new Dialog(this, android.R.style.Theme_Translucent_NoTitleBar_Fullscreen) {
            @Override public boolean dispatchKeyEvent(KeyEvent event) {
                if (CTRGamepadActivity.isGamepad(event.getDevice()) && mSurface != null)
                    return SDLActivity.handleKeyEvent(mSurface, event.getKeyCode(), event, null);
                return super.dispatchKeyEvent(event);
            }

            @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
                if (CTRGamepadActivity.isGamepad(event.getDevice()))
                    return org.libsdl.app.SDLControllerManager.handleJoystickMotionEvent(event);
                return super.dispatchGenericMotionEvent(event);
            }
        };
        dialog.setCancelable(false);
        dialog.setCanceledOnTouchOutside(false);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.argb(174, 0, 0, 0));

        adventureCampaignDialogContent = new View(this) {
            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                if (touchOverlay != null) {
                    if (touchOverlay.isAdventureCampaignDeleteConfirmation()) {
                        touchOverlay.drawAdventureCampaignDeleteConfirmation(canvas, getWidth(), getHeight());
                    } else {
                        touchOverlay.drawAdventureCampaignScreen(canvas, getWidth(), getHeight());
                    }
                }
            }

            @Override
            public boolean onTouchEvent(MotionEvent event) {
                return touchOverlay != null && touchOverlay.onTouchEvent(event);
            }
        };
        adventureCampaignDialogContent.setWillNotDraw(false);
        adventureCampaignDialogContent.setClickable(true);
        root.addView(adventureCampaignDialogContent, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        dialog.setContentView(root);

        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.setDimAmount(0f);
            window.addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                    | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
            window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT);
        }
        adventureCampaignDialog = dialog;
        dialog.show();
        if (dialog.getWindow() != null) {
            dialog.getWindow().setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT);
            applyImmersiveMode(dialog.getWindow());
        }
    }

    private void dismissAdventureCampaignDialog() {
        if (adventureCampaignDialog != null && adventureCampaignDialog.isShowing()) {
            adventureCampaignDialog.dismiss();
        }
        adventureCampaignDialog = null;
        adventureCampaignDialogContent = null;
    }

    private int lastRequestedFrameRate = 0;

    /**
     * Asks the display to run at the configured cap.
     *
     * preferredDisplayModeId, which requestHighestRefreshRate sets, is a hint
     * that modern Android and OEM refresh-rate managers routinely override --
     * this device asks for 165Hz at startup and gets 90. Surface.setFrameRate is
     * the API they actually honour for games, and it names a rate rather than a
     * mode, so the panel can pick whichever mode serves it.
     *
     * Repeated from the same poll as the touch overlay because the surface does
     * not exist yet in onCreate, and because the cap can change between runs.
     */
    private void applyPreferredFrameRate() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }

        int fps;
        try {
            fps = nativeTargetFps();
        } catch (UnsatisfiedLinkError e) {
            return;
        }

        if ((fps <= 0) || (fps == lastRequestedFrameRate)) {
            return;
        }

        try {
            if (mSurface == null) {
                return;
            }

            Surface surface = mSurface.getHolder().getSurface();
            if ((surface == null) || !surface.isValid()) {
                return;
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                surface.setFrameRate((float) fps,
                        Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                        Surface.CHANGE_FRAME_RATE_ALWAYS);
            } else {
                surface.setFrameRate((float) fps, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
            }

            lastRequestedFrameRate = fps;

            float actual = getWindowManager().getDefaultDisplay().getRefreshRate();
            Log.i(CTRDS_TAG, "asked the surface for " + fps + "Hz; display reports " + actual + "Hz");
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "setFrameRate failed: " + e.getMessage());
        }
    }

    private void checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.addCategory("android.intent.category.DEFAULT");
                    intent.setData(Uri.parse(String.format("package:%s", getApplicationContext().getPackageName())));
                    startActivity(intent);
                } catch (Exception e) {
                    Intent intent = new Intent();
                    intent.setAction(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivity(intent);
                }
            }
        }
    }

    private static boolean pickerActive = false;

    public static void pickFile() {
        Activity activity = (Activity) SDLActivity.getContext();
        pickerActive = true;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        // Filter for .bin and .BIG extensions is hard to do via intent, 
        // we'll check the result instead.
        activity.startActivityForResult(intent, PICK_DIRECTORY_REQUEST);
    }

    public static boolean isPickerActive() {
        return pickerActive;
    }

    public static String getStoredAssetPath() {
        Activity activity = (Activity) SDLActivity.getContext();
        SharedPreferences prefs = activity.getSharedPreferences(PREFS_NAME, MODE_PRIVATE);
        String path = prefs.getString(KEY_ASSET_PATH, null);
        Log.d("CTRNative", "getStoredAssetPath: " + path);
        return path;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_DIRECTORY_REQUEST) {
            pickerActive = false;
            if (resultCode == RESULT_OK && data != null) {
                Uri uri = data.getData();
                if (uri == null) return;
                try {
                    getContentResolver().takePersistableUriPermission(uri,
                            Intent.FLAG_GRANT_READ_URI_PERMISSION);

                    String displayName = getFileName(uri);
                    if (displayName != null && (displayName.toLowerCase().endsWith(".bin") || displayName.toLowerCase().endsWith(".big"))) {
                        copyFileToInternal(uri, displayName);

                        File assetsDir = new File(getFilesDir(), "assets");
                        SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
                        editor.putString(KEY_ASSET_PATH, assetsDir.getAbsolutePath());
                        editor.apply();
                        Log.d("CTRNative", "Stored internal assets dir: " + assetsDir.getAbsolutePath());
                        runOnUiThread(() -> android.widget.Toast.makeText(this, "Assets copied to internal storage. Please restart the app.", android.widget.Toast.LENGTH_LONG).show());
                    } else {
                        String path = getPathFromUri(uri);
                        if (path != null) {
                            File file = new File(path);
                            String parentDir = file.getParent();
                            if (parentDir != null) {
                                SharedPreferences.Editor editor = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit();
                                editor.putString(KEY_ASSET_PATH, parentDir);
                                editor.apply();
                                Log.d("CTRNative", "Stored parent dir: " + parentDir);
                                runOnUiThread(() -> android.widget.Toast.makeText(this, "Assets location saved. Please restart the app.", android.widget.Toast.LENGTH_LONG).show());
                            }
                        } else {
                            Log.e("CTRNative", "Failed to resolve path from URI: " + uri.toString());
                        }
                    }
                } catch (Exception e) {
                    Log.e("CTRNative", "Error processing selected file: " + e.getMessage());
                }
            }
        }
    }

    private String getFileName(Uri uri) {
        String result = null;
        if (uri.getScheme().equals("content")) {
            try (android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) {
                    int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (index != -1) {
                        result = cursor.getString(index);
                    }
                }
            }
        }
        if (result == null) {
            result = uri.getPath();
            int cut = result.lastIndexOf('/');
            if (cut != -1) {
                result = result.substring(cut + 1);
            }
        }
        return result;
    }

    private void copyFileToInternal(Uri uri, String fileName) {
        File assetsDir = new File(getFilesDir(), "assets");
        if (!assetsDir.exists()) {
            assetsDir.mkdirs();
        }
        File outFile = new File(assetsDir, fileName);
        try (InputStream in = getContentResolver().openInputStream(uri);
             FileOutputStream out = new FileOutputStream(outFile)) {
            byte[] buffer = new byte[16384];
            int read;
            while ((read = in.read(buffer)) != -1) {
                out.write(buffer, 0, read);
            }
            Log.d("CTRNative", "Copied file to: " + outFile.getAbsolutePath());
        } catch (Exception e) {
            Log.e("CTRNative", "Failed to copy file: " + e.getMessage());
        }
    }

    private String getPathFromUri(Uri uri) {
        if (uri == null) return null;
        
        String path = null;
        String host = uri.getHost();
        String docId = null;
        
        try {
            if ("com.android.externalstorage.documents".equals(host)) {
                docId = DocumentsContract.getDocumentId(uri);
                final String[] split = docId.split(":");
                final String type = split[0];
                if ("primary".equalsIgnoreCase(type)) {
                    path = Environment.getExternalStorageDirectory() + "/" + (split.length > 1 ? split[1] : "");
                } else {
                    // Secondary SD cards
                    path = "/storage/" + type + "/" + (split.length > 1 ? split[1] : "");
                }
            } else if ("com.android.providers.downloads.documents".equals(host)) {
                docId = DocumentsContract.getDocumentId(uri);
                if (docId.startsWith("raw:")) {
                    path = docId.substring(4);
                } else {
                    path = Environment.getExternalStorageDirectory() + "/Download/" + docId;
                }
            }
        } catch (Exception e) {
            Log.e("CTRNative", "Error in getPathFromUri: " + e.getMessage());
        }
        
        if (path != null) {
            path = path.replace("//", "/");
            if (path.endsWith("/")) {
                path = path.substring(0, path.length() - 1);
            }
        }

        return path;
    }

    // ---------------------------------------------------------------- CTR-DS
    //
    // Dual-screen companion panel. Native code calls startCompanionDisplay()
    // once the renderer is up; this opens a Presentation on the secondary
    // display and hands its Surface back down so native can make an EGLSurface
    // out of it. Everything here has to happen on the UI thread, while the
    // caller is on the game thread.

    private static final String CTRDS_TAG = "CTR-DS";

    /**
     * Android hands an app the display's default mode, which on this device is
     * 60Hz even though both panels also advertise 120Hz. Ask for the highest
     * refresh rate available at the mode's current resolution; without this the
     * frame loop can never exceed 60fps no matter how it is paced.
     */
    static void requestHighestRefreshRate(android.view.Window window, Display display) {
        if (window == null || display == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        try {
            Display.Mode current = display.getMode();
            Display.Mode best = current;

            for (Display.Mode mode : display.getSupportedModes()) {
                if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                        && mode.getPhysicalHeight() == current.getPhysicalHeight()
                        && mode.getRefreshRate() > best.getRefreshRate()) {
                    best = mode;
                }
            }

            WindowManager.LayoutParams lp = window.getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            window.setAttributes(lp);

            Log.i(CTRDS_TAG, "display " + display.getDisplayId() + " requested "
                    + best.getRefreshRate() + "Hz (mode " + best.getModeId() + ")");
        } catch (Exception e) {
            Log.e(CTRDS_TAG, "refresh rate request failed: " + e.getMessage());
        }
    }
    private static CTRDSPresentation ctrdsPresentation;

    public static native void nativeCompanionSurfaceChanged(Surface surface, int width, int height);

    public static native void nativeCompanionSurfaceDestroyed();

    /** Tells native this device has no second screen, so the HUD stays on the main one. */
    public static native void nativeCompanionUnavailable();

    /** On-screen controls -> pad. Mask is active-low in PSX bit order. */
    public static native void nativeTouchInput(int buttonMask, int stickX, int stickY, long generation);

    /** Touch coordinates for direct selection in game menus and character select. */
    public static native void nativeTouchMenuTap(float nx, float ny, long generation);

    /** Select a row in the native main-menu list. */
    public static native void nativeTouchRootMenuChoice(int choice, long generation);

    public static native void nativeTouchMenuChoice(int choice, long generation);

    public static native String[] nativeTouchSnapshot();
    public static native void nativeTouchTrace(boolean enabled);

    public static native String nativeTouchMenuSnapshot();

    public static native String nativeTouchRaceResultsSnapshot();

    public static native String nativeTouchAdventureCampaignSnapshot();

    public static native String nativeTouchAdventurePauseSnapshot();

    public static native int nativeGameAudioVolume(int type);

    private static native void nativeFinishStartupAudio();

    public static native void nativeSetGameAudioVolume(int type, int volume);

    public static native String nativeTouchCharacterSelectName();
    public static native String nativeTouchSelectionText();

    public static native void nativeTouchSetActive(boolean active);

    /** The configured frame cap, used to ask the display for a matching rate. */
    public static native int nativeTargetFps();

    public static native int nativeGetGamepadCount();
    public static native void nativeSetGamepadBindings(int[] bindings);

    /** 0 auto, 1 always, 2 never -- from touch_controls in ctrds.cfg. */
    public static native int nativeTouchControlsMode();

    /** Native touch screen state; 11 identifies the Android pause menu. */
    public static native int nativeTouchScreenState();

    public static native String nativeTouchSubmitNameSnapshot();

    public static native void nativeTouchSubmitName(String name, boolean save);

    /** A tap on the bottom screen, normalised 0..1. Drives the settings list. */
    public static native void nativePanelTap(float nx, float ny);

    /** Called from native. Safe to call more than once. */
    public static void startCompanionDisplay() {
        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity == null) {
            Log.e(CTRDS_TAG, "no activity; cannot open the companion display");
            return;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (ctrdsPresentation != null && ctrdsPresentation.isShowing()) {
                    return;
                }

                DisplayManager dm = (DisplayManager) activity.getSystemService(DISPLAY_SERVICE);
                if (dm == null) {
                    Log.e(CTRDS_TAG, "no DisplayManager");
                    return;
                }

                // DISPLAY_CATEGORY_PRESENTATION is the sanctioned way to find a
                // secondary panel; on the AYN Thor this is display 4, "Screen-2",
                // which reports FLAG_PRESENTATION.
                Display[] displays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
                if (displays == null || displays.length == 0) {
                    Log.w(CTRDS_TAG, "no presentation display; running single-screen");
                    try {
                        nativeCompanionUnavailable();
                    } catch (UnsatisfiedLinkError e) {
                        Log.e(CTRDS_TAG, "nativeCompanionUnavailable missing: " + e.getMessage());
                    }
                    return;
                }

                Display target = displays[0];
                Log.i(CTRDS_TAG, "companion display id=" + target.getDisplayId() + " name=" + target.getName());

                try {
                    ctrdsPresentation = new CTRDSPresentation(activity, target);
                    ctrdsPresentation.show();
                } catch (Exception e) {
                    Log.e(CTRDS_TAG, "failed to show companion presentation: " + e.getMessage());
                    ctrdsPresentation = null;
                }
            }
        });
    }

    /** Called from native on shutdown. */
    public static void stopCompanionDisplay() {
        final Activity activity = (Activity) SDLActivity.getContext();
        if (activity == null) {
            return;
        }

        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (ctrdsPresentation != null) {
                    try {
                        ctrdsPresentation.dismiss();
                    } catch (Exception e) {
                        Log.e(CTRDS_TAG, "dismiss failed: " + e.getMessage());
                    }
                    ctrdsPresentation = null;
                }
            }
        });
    }
}
