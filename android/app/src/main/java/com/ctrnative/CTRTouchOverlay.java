package com.ctrnative;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.View;
import java.io.IOException;
import java.io.InputStream;

import java.util.ArrayList;
import java.util.List;

/** Touch controls drawn above the game at the Android display resolution. */
public final class CTRTouchOverlay extends View {

    public static final int SCREEN_MENU = 0;
    public static final int SCREEN_RACE = 1;
    public static final int SCREEN_PAUSE_RESULTS = 2;
    public static final int SCREEN_WAITING = 3;
    public static final int SCREEN_DEMO = 4;
    public static final int SCREEN_ROOT_MENU = 5;
    public static final int SCREEN_GAME_MENU = 6;
    public static final int SCREEN_CHARACTER_SELECT = 7;
    public static final int SCREEN_NAME_ENTRY = 8;
    public static final int SCREEN_TRACK_SELECT = 9;
    public static final int SCREEN_CUP_SELECT = 10;
    public static final int SCREEN_PAUSE_MENU = 11;
    public static final int SCREEN_CUTSCENE = 12;
    public static final int SCREEN_PAUSE_QUIT_CONFIRM = 13;
    public static final int SCREEN_PAUSE_OPTIONS = 14;
    public static final int SCREEN_ADVENTURE_GARAGE = 15;
    public static final int SCREEN_MASK_HINT = 17;
    public static final int SCREEN_ADVENTURE_ARENA = 18;
    public static final int SCREEN_PAUSE_SUBMENU = 19;
    public static final int SCREEN_RACE_RESULTS_MENU = 20;
    public static final int SCREEN_GHOST_SELECT = 21;
    public static final int SCREEN_LOADING = 22;

    private static final int BTN_START = 0x0008;
    private static final int BTN_UP = 0x0010;
    private static final int BTN_RIGHT = 0x0020;
    private static final int BTN_DOWN = 0x0040;
    private static final int BTN_LEFT = 0x0080;
    private static final int BTN_L1 = 0x0400;
    private static final int BTN_TRIANGLE = 0x1000;
    private static final int BTN_CIRCLE = 0x2000;
    private static final int BTN_CROSS = 0x4000;
    private static final int BTN_SQUARE = 0x8000;
    private static final int ALL_RELEASED = 0xffff;
    private static final class Control {
        final int button;
        final Bitmap icon;
        float cx;
        float cy;
        float size;
        int pointerId = -1;

        Control(int button, Bitmap icon) {
            this.button = button;
            this.icon = icon;
        }

        boolean contains(float x, float y) {
            float hitRadius = size * 0.43f;
            float dx = x - cx;
            float dy = y - cy;
            return dx * dx + dy * dy <= hitRadius * hitRadius;
        }
    }

    private static final class SelectionTextLabel {
        final boolean title;
        final float x;
        final float y;
        final String text;

        SelectionTextLabel(boolean title, float x, float y, String text) {
            this.title = title;
            this.x = x;
            this.y = y;
            this.text = text;
        }
    }

    private final Bitmap left;
    private final Bitmap right;
    private final Bitmap pause;
    private final Bitmap jump;
    private final Bitmap brake;
    private final Bitmap accelerator;
    private final Bitmap weapon;
    private final Bitmap rootLogo;
    private final Paint imagePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Paint pressedPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rootPanelPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rootBorderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rootSelectedPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint rootTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint rootTextStrokePaint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
    private final Paint garageCardPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint garageBarPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint garageBarTrackPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface rootMenuTypeface;
    private final List<Control> controls = new ArrayList<>();
    private final List<Integer> advancePointers = new ArrayList<>();
    private final List<SelectionTextLabel> selectionTextLabels = new ArrayList<>();
    private final float[] garageStatRatios = new float[3];
    private final Bitmap[] adventureCampaignIcons = new Bitmap[4];
    private final Bitmap[] raceResultIcons = new Bitmap[8];
    private final int[] raceResultIconIds = {-2, -2, -2, -2, -2, -2, -2, -2};

    private int screenState = -1;
    private int rootMenuSelection;
    private String characterSelectName = "";
    private String[] gameMenuRows = new String[0];
    private int[] gameMenuNativeChoices = new int[0];
    private int gameMenuSelection;
    private String gameMenuTitle = "";
    private String adventureHintTitle = "";
    private String adventureHintBody = "";
    private String[] adventureHintRows = new String[0];
    private int[] adventureHintRowIndexes = new int[0];
    private int adventureHintSelection;
    private boolean adventureHintDetails;
    private int adventureHintPointer = -1;
    private float adventureHintTouchStartY;
    private boolean adventureHintSwipe;
    private String adventureCampaignName = "";
    private int adventureCampaignPercent;
    private int[] adventureCampaignCounts = new int[4];
    private String adventurePauseTitle = "";
    private int adventurePausePage;
    private int adventurePauseType;
    private String[] adventurePauseRows = new String[0];
    private int raceResultRank = -1;
    private int raceResultLapCount;
    private final int[] raceResultLapTicks = new int[3];
    private int raceResultTotalTicks;
    private boolean adventurePauseProgressOpen;
    private Bitmap adventureCampaignPortrait;
    private int adventureCampaignPortraitId = -1;
    private int backPointer = -1;
    private int touchTapPulseButton;
    private long touchTapPulseUntil;
    private int lastMask = ALL_RELEASED;
    private boolean sending = false;
    private boolean controlsVisible = true;
    private long demoTapPulseUntil;
    private final Runnable finishDemoTapPulse = () -> {
        demoTapPulseUntil = 0;
        sendInput();
        invalidate();
    };
    private final Runnable finishTouchTapPulse = () -> {
        touchTapPulseButton = 0;
        touchTapPulseUntil = 0;
        sendInput();
        invalidate();
    };

    public CTRTouchOverlay(Context context) {
        super(context);
        setFocusable(false);
        setClickable(false);
        setLayerType(View.LAYER_TYPE_HARDWARE, null);

        left = decode(R.drawable.left);
        right = decode(R.drawable.right);
        pause = decode(R.drawable.pause);
        jump = decode(R.drawable.pular);
        brake = decode(R.drawable.brake);
        accelerator = decode(R.drawable.accelerator);
        weapon = decode(R.drawable.weapon);
        rootLogo = decode(R.drawable.ctr_logo);
        adventureCampaignIcons[0] = decodeCampaignIcon(R.drawable.campaign_trophy);
        adventureCampaignIcons[1] = decodeCampaignIcon(R.drawable.campaign_key);
        adventureCampaignIcons[2] = decodeCampaignIcon(R.drawable.campaign_relic);
        adventureCampaignIcons[3] = decodeCampaignIcon(R.drawable.campaign_ctr);

        pressedPaint.setColor(Color.argb(85, 255, 255, 255));

        rootPanelPaint.setColor(Color.argb(218, 10, 20, 27));
        rootBorderPaint.setColor(Color.rgb(130, 244, 234));
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootSelectedPaint.setColor(Color.argb(188, 204, 46, 30));
        rootTextPaint.setColor(Color.rgb(255, 166, 20));
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootMenuTypeface = CTRFont.get(getContext());
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextStrokePaint.setColor(Color.BLACK);
        rootTextStrokePaint.setTextAlign(Paint.Align.CENTER);
        rootTextStrokePaint.setTypeface(rootTextPaint.getTypeface());
        rootTextStrokePaint.setStyle(Paint.Style.STROKE);

    }

    private Bitmap decode(int resourceId) {
        return BitmapFactory.decodeResource(getResources(), resourceId);
    }

    private Bitmap decodeCampaignIcon(int resourceId) {
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inSampleSize = 4;
        return BitmapFactory.decodeResource(getResources(), resourceId, options);
    }

    private long snapshotGeneration;
    private boolean snapshotInputReady;
    private long snapshotFrame;
    private long drawnGeneration;

    void beginSnapshot(CTRTouchSnapshot snapshot) {
        if (snapshotGeneration != snapshot.generation) {
            releaseAllTouches();
            setScreenState(snapshot.screen);
            snapshotGeneration = snapshot.generation;
            if (CTRNativeActivity.isTouchTraceEnabled()) {
                android.util.Log.d("CTR-UI", "apply ms=" + SystemClock.uptimeMillis()
                        + " gen=" + snapshotGeneration + " screen=" + screenState);
            }
        }
        snapshotFrame = snapshot.frame;
        snapshotInputReady = snapshot.inputReady;
    }

    /** Change the preset and clear held inputs before the new layout appears. */
    public void setScreenState(int state) {
        state = (state == SCREEN_RACE || state == SCREEN_ADVENTURE_ARENA || state == SCREEN_PAUSE_RESULTS
                || state == SCREEN_WAITING || state == SCREEN_DEMO
                || state == SCREEN_ROOT_MENU || state == SCREEN_GAME_MENU
                || state == SCREEN_CHARACTER_SELECT || state == SCREEN_NAME_ENTRY
                || state == SCREEN_TRACK_SELECT || state == SCREEN_CUP_SELECT
                || state == SCREEN_PAUSE_MENU || state == SCREEN_CUTSCENE
                || state == SCREEN_PAUSE_QUIT_CONFIRM || state == SCREEN_PAUSE_OPTIONS
                || state == SCREEN_ADVENTURE_GARAGE || state == SCREEN_RACE_RESULTS_MENU
                || state == SCREEN_MASK_HINT || state == SCREEN_PAUSE_SUBMENU
                || state == SCREEN_GHOST_SELECT || state == SCREEN_LOADING)
                ? state : SCREEN_MENU;
        if (state != SCREEN_PAUSE_MENU) {
            adventurePauseProgressOpen = false;
        }
        if (screenState == state) {
            return;
        }

        removeCallbacks(finishDemoTapPulse);
        removeCallbacks(finishTouchTapPulse);
        demoTapPulseUntil = 0;
        touchTapPulseButton = 0;
        touchTapPulseUntil = 0;
        clearPointers();
        adventureHintPointer = -1;
        adventureHintSwipe = false;
        if (state == SCREEN_ROOT_MENU) {
            rootMenuSelection = 0;
        }
        screenState = state;
        ensureAdventurePauseProgressOption();
        rebuildControls();
        layoutControls(getWidth(), getHeight());
        sendInput();
        invalidate();
    }

    public void setGameMenuSnapshot(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) {
            gameMenuRows = new String[0];
            gameMenuNativeChoices = new int[0];
            gameMenuSelection = 0;
            gameMenuTitle = "";
        } else {
            String[] values = snapshot.split("\\n");
            if (values.length > 1 && "@EMPTY".equals(values[1])) {
                gameMenuRows = new String[0];
                gameMenuNativeChoices = new int[0];
                gameMenuSelection = 0;
                gameMenuTitle = "";
                invalidate();
                return;
            }
            try {
                gameMenuSelection = values.length > 1
                        ? Math.max(0, Math.min(Integer.parseInt(values[0]), values.length - 2)) : 0;
            } catch (NumberFormatException error) {
                gameMenuSelection = 0;
            }
            int firstRow = values.length > 1 && values[1].startsWith("@TITLE:") ? 2 : 1;
            gameMenuTitle = firstRow == 2 ? values[1].substring(7) : "";
            gameMenuRows = values.length > firstRow
                    ? java.util.Arrays.copyOfRange(values, firstRow, Math.min(values.length, firstRow + 8))
                    : new String[0];
            gameMenuNativeChoices = new int[gameMenuRows.length];
            for (int i = 0; i < gameMenuNativeChoices.length; i++) {
                gameMenuNativeChoices[i] = i;
            }
        }
        invalidate();
    }

    public void setRaceResultsSnapshot(String snapshot) {
        raceResultRank = -1;
        raceResultLapCount = 0;
        raceResultTotalTicks = 0;
        java.util.Arrays.fill(raceResultLapTicks, 0);
        if (snapshot != null && !snapshot.isEmpty()) {
            String[] values = snapshot.split("\\|");
            if (values.length >= 14) {
                try {
                    raceResultRank = Integer.parseInt(values[0]);
                    raceResultLapCount = Math.max(0, Math.min(3, Integer.parseInt(values[1])));
                    for (int i = 0; i < 3; i++) raceResultLapTicks[i] = Math.max(0, Integer.parseInt(values[i + 2]));
                    raceResultTotalTicks = Math.max(0, Integer.parseInt(values[5]));
                    for (int i = 0; i < raceResultIcons.length; i++) {
                        int iconId = Integer.parseInt(values[i + 6]);
                        if (iconId != raceResultIconIds[i]) {
                            raceResultIconIds[i] = iconId;
                            raceResultIcons[i] = decodeRaceResultIcon(iconId);
                        }
                    }
                } catch (NumberFormatException ignored) {
                    raceResultRank = -1;
                    raceResultLapCount = 0;
                }
            }
        }
        invalidate();
    }

    private Bitmap decodeRaceResultIcon(int iconId) {
        if (iconId < 0) return null;
        try (InputStream input = getContext().getAssets().open("hd/icon_" + iconId + ".png")) {
            BitmapFactory.Options options = new BitmapFactory.Options();
            options.inSampleSize = 2;
            return BitmapFactory.decodeStream(input, null, options);
        } catch (IOException ignored) {
            return null;
        }
    }

    public void setAdventureHintSnapshot(String snapshot) {
        adventureHintTitle = "";
        adventureHintBody = "";
        adventureHintRows = new String[0];
        adventureHintRowIndexes = new int[0];
        adventureHintSelection = 0;
        adventureHintDetails = false;
        if (snapshot != null && !snapshot.isEmpty()) {
            String[] lines = snapshot.split("\\n", -1);
            String[] header = lines[0].split("\\t", -1);
            if (header.length >= 3 && "L".equals(header[0])) {
                adventureHintTitle = header[1];
                try { adventureHintSelection = Integer.parseInt(header[2]); }
                catch (NumberFormatException ignored) { adventureHintSelection = 0; }
                java.util.ArrayList<String> labels = new java.util.ArrayList<>();
                java.util.ArrayList<Integer> indexes = new java.util.ArrayList<>();
                for (int i = 1; i < lines.length; i++) {
                    String[] row = lines[i].split("\\t", 3);
                    if (row.length != 3 || !"R".equals(row[0])) continue;
                    try {
                        indexes.add(Integer.parseInt(row[1]));
                        labels.add(row[2]);
                    } catch (NumberFormatException ignored) { }
                }
                adventureHintRows = labels.toArray(new String[0]);
                adventureHintRowIndexes = new int[indexes.size()];
                for (int i = 0; i < indexes.size(); i++) adventureHintRowIndexes[i] = indexes.get(i);
            } else if (header.length >= 3 && "D".equals(header[0])) {
                adventureHintDetails = true;
                adventureHintTitle = header[1];
                try { adventureHintSelection = Integer.parseInt(header[2]); }
                catch (NumberFormatException ignored) { adventureHintSelection = 0; }
                if (lines.length > 1) {
                    StringBuilder body = new StringBuilder();
                    for (int i = 1; i < lines.length; i++) {
                        if (i > 1) body.append('\n');
                        body.append(lines[i]);
                    }
                    adventureHintBody = body.toString();
                }
            }
        }
        invalidate();
    }

    public void setAdventureCampaignSnapshot(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) {
            adventureCampaignName = "";
            adventureCampaignPortrait = null;
            adventureCampaignPortraitId = -1;
            invalidate();
            return;
        }
        String[] fields = snapshot.split("\\t", -1);
        if (fields.length < 7) return;
        adventureCampaignName = fields[0];
        try {
            adventureCampaignPercent = Math.max(0, Math.min(100, Integer.parseInt(fields[1])));
            for (int i = 0; i < adventureCampaignCounts.length; i++) {
                adventureCampaignCounts[i] = Math.max(0, Integer.parseInt(fields[i + 2]));
            }
            int iconId = Integer.parseInt(fields[6]);
            if (iconId != adventureCampaignPortraitId) {
                adventureCampaignPortraitId = iconId;
                adventureCampaignPortrait = decodeCampaignPortrait(iconId);
            }
        } catch (NumberFormatException error) {
            adventureCampaignName = "";
        }
        invalidate();
    }

    public void setAdventurePauseSnapshot(String snapshot) {
        if (snapshot == null || snapshot.isEmpty()) {
            adventurePauseTitle = "";
            adventurePauseRows = new String[0];
            invalidate();
            return;
        }
        String[] lines = snapshot.split("\\n");
        String[] header = lines[0].split("\\t", -1);
        if (header.length < 3) return;
        adventurePauseTitle = header[0];
        try {
            adventurePausePage = Integer.parseInt(header[1]);
            adventurePauseType = Integer.parseInt(header[2]);
        } catch (NumberFormatException error) {
            adventurePauseTitle = "";
            adventurePauseRows = new String[0];
            invalidate();
            return;
        }
        adventurePauseRows = java.util.Arrays.copyOfRange(lines, 1, lines.length);
        ensureAdventurePauseProgressOption();
        reorderAdventurePauseMenu();
        invalidate();
    }

    private void ensureAdventurePauseProgressOption() {
        if (adventurePauseTitle.isEmpty()) return;
        for (int i = 0; i < gameMenuRows.length; i++) {
            if ("PROGRESSO".equals(gameMenuRows[i])) {
                gameMenuNativeChoices[i] = -1;
                return;
            }
        }
        gameMenuRows = java.util.Arrays.copyOf(gameMenuRows, gameMenuRows.length + 1);
        gameMenuRows[gameMenuRows.length - 1] = "PROGRESSO";
        gameMenuNativeChoices = java.util.Arrays.copyOf(gameMenuNativeChoices, gameMenuRows.length);
        gameMenuNativeChoices[gameMenuNativeChoices.length - 1] = -1;
    }

    private void reorderAdventurePauseMenu() {
        Integer[] order = new Integer[gameMenuRows.length];
        for (int i = 0; i < order.length; i++) order[i] = i;
        java.util.Arrays.sort(order, (a, b) -> Integer.compare(
                adventurePauseMenuOrder(gameMenuRows[a]), adventurePauseMenuOrder(gameMenuRows[b])));

        String[] rows = new String[order.length];
        int[] nativeChoices = new int[order.length];
        int selectedChoice = gameMenuSelection < gameMenuNativeChoices.length
                ? gameMenuNativeChoices[gameMenuSelection] : -1;
        int selection = gameMenuSelection;
        for (int i = 0; i < order.length; i++) {
            rows[i] = gameMenuRows[order[i]];
            nativeChoices[i] = gameMenuNativeChoices[order[i]];
            if (selectedChoice >= 0 && nativeChoices[i] == selectedChoice) selection = i;
        }
        gameMenuRows = rows;
        gameMenuNativeChoices = nativeChoices;
        gameMenuSelection = selection;
    }

    private int adventurePauseMenuOrder(String label) {
        String row = label.toUpperCase(java.util.Locale.ROOT);
        if (row.contains("RETOMAR") || row.contains("RESUME")) return 0;
        if (row.contains("PROGRESSO") || row.contains("PROGRESS")) return 1;
        if (row.contains("CONSELH") || row.contains("AKU") || row.contains("HINT")) return 2;
        if (row.contains("OPÇ") || row.contains("OPC") || row.contains("OPTION")) return 3;
        if (row.contains("SAIR") || row.contains("QUIT")) return 4;
        return 5;
    }

    private Bitmap decodeCampaignPortrait(int iconId) {
        if (iconId < 0) return null;
        try (InputStream stream = getContext().getAssets().open("hd/icon_" + iconId + ".png")) {
            return BitmapFactory.decodeStream(stream);
        } catch (IOException error) {
            return null;
        }
    }

    public boolean isAdventureCampaignManagement() {
        return adventureCampaignName.length() > 0 && gameMenuRows.length == 2
                && "EXCLUIR CAMPANHA".equals(gameMenuRows[0]);
    }

    public boolean isAdventureCampaignDeleteConfirmation() {
        return "EXCLUIR CAMPANHA?".equals(gameMenuTitle) && gameMenuRows.length == 2;
    }

    public void setCharacterSelectName(String name) {
        String value = (name != null) ? name : "";
        if (!characterSelectName.equals(value)) {
            characterSelectName = value;
            invalidate();
        }
    }

    public void setSelectionText(String snapshot) {
        selectionTextLabels.clear();
        java.util.Arrays.fill(garageStatRatios, 0f);
        if (snapshot != null && !snapshot.isEmpty()) {
            for (String line : snapshot.split("\\n")) {
                String[] fields = line.split("\\t", 4);
                if (fields.length != 4) continue;
                try {
                    if ("B".equals(fields[0])) {
                        int index = Integer.parseInt(fields[1]);
                        if (index >= 0 && index < garageStatRatios.length) {
                            garageStatRatios[index] = clamp(Float.parseFloat(fields[2]) / 80f, 0f, 1f);
                        }
                        continue;
                    }
                    selectionTextLabels.add(new SelectionTextLabel(
                            "T".equals(fields[0]),
                            Float.parseFloat(fields[1]) / 512f,
                            Float.parseFloat(fields[2]) / 216f,
                            fields[3]));
                } catch (NumberFormatException ignored) {
                    // Ignore malformed native rows and keep rendering valid labels.
                }
            }
        }
        invalidate();
    }

    /** Keep the transparent hit layer while suppressing its artwork for a hardware pad. */
    public void setControlsVisible(boolean visible) {
        if (controlsVisible != visible) {
            if (!visible) releaseAllTouches();
            controlsVisible = visible;
            invalidate();
        }
    }

    /** Re-reads saved control positions after returning from the layout editor. */
    public void refreshCurrentLayout() {
        layoutControls(getWidth(), getHeight());
        invalidate();
    }

    /** Pause the race before an Android dialog covers the game view. */
    public void pauseRaceForSettings() {
        if (screenState == SCREEN_RACE || screenState == SCREEN_ADVENTURE_ARENA) {
            queueTouchTapPulse(BTN_START);
            sendInput();
        }
    }

    public void closePauseOptions() {
        queueTouchTapPulse(BTN_CIRCLE);
        sendInput();
    }

    /** Clear all virtual buttons before the view is hidden or detached. */
    public void releaseAllTouches() {
        removeCallbacks(finishDemoTapPulse);
        removeCallbacks(finishTouchTapPulse);
        demoTapPulseUntil = 0;
        touchTapPulseButton = 0;
        touchTapPulseUntil = 0;
        clearPointers();
        adventureHintPointer = -1;
        adventureHintSwipe = false;
        sendInput();
        invalidate();
    }

    private void clearPointers() {
        for (Control control : controls) {
            control.pointerId = -1;
        }
        advancePointers.clear();
        backPointer = -1;
    }

    private void add(int button, Bitmap icon) {
        controls.add(new Control(button, icon));
    }

    private void rebuildControls() {
        controls.clear();

        if (screenState == SCREEN_RACE || screenState == SCREEN_ADVENTURE_ARENA) {
            add(BTN_LEFT, left);
            add(BTN_RIGHT, right);
            add(BTN_START, pause);
            add(BTN_L1, jump);
            add(BTN_SQUARE, brake);
            add(BTN_CROSS, accelerator);
            add(BTN_CIRCLE, weapon);
            return;
        }

    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    private void layoutControls(int w, int h) {
        if (w <= 0 || h <= 0) {
            return;
        }

        if (screenState == SCREEN_RACE || screenState == SCREEN_ADVENTURE_ARENA) {
            layoutRace(w, h, Math.min(w, h));
        }
    }

    private void layoutRace(int w, int h, float unit) {
        SharedPreferences preferences = getContext().getSharedPreferences("CTRNativePrefs", Context.MODE_PRIVATE);
        float normal = unit * 0.235f;
        float small = unit * 0.145f;
        float action = unit * 0.19f;
        float large = unit * 0.255f;
        float margin = unit * 0.045f;
        float scale = Math.max(70, Math.min(150,
                preferences.getInt("race_control_scale_percent", 100))) / 100.0f;

        place(BTN_LEFT, w * 0.115f, h * 0.80f, normal, "left", preferences, scale, w, h);
        place(BTN_RIGHT, w * 0.285f, h * 0.80f, normal, "right", preferences, scale, w, h);
        place(BTN_START, w - unit * 0.02f - small * 0.5f, unit * 0.02f + small * 0.5f, small,
                "pause", preferences, scale, w, h);
        place(BTN_L1, w * 0.77f, h * 0.72f, action * 1.10f,
                "jump", preferences, scale, w, h);
        place(BTN_SQUARE, w * 0.76f, h * 0.90f, action * 1.10f,
                "brake", preferences, scale, w, h);
        place(BTN_CROSS, w - margin - large * 0.5f, h * 0.84f, large * 1.20f,
                "accelerator", preferences, scale, w, h);
        place(BTN_CIRCLE, w * 0.875f, h * 0.56f, action * 1.10f,
                "weapon", preferences, scale, w, h);
    }

    private void place(int button, float cx, float cy, float size, String key,
                       SharedPreferences preferences, float scale, int width, int height) {
        String prefix = "race_control_" + key + "_";
        float normalizedX = clamp(preferences.getFloat(prefix + "x", cx / width), 0.0f, 1.0f);
        float normalizedY = clamp(preferences.getFloat(prefix + "y", cy / height), 0.0f, 1.0f);
        float finalSize = Math.min(size * scale, Math.min(width, height) * 0.42f);
        for (Control control : controls) {
            if (control.button == button) {
                control.cx = clamp(normalizedX * width, finalSize * 0.5f,
                        width - finalSize * 0.5f);
                control.cy = clamp(normalizedY * height, finalSize * 0.5f,
                        height - finalSize * 0.5f);
                control.size = finalSize;
                return;
            }
        }
    }

    private float clamp(float value, float min, float max) {
        return Math.max(min, Math.min(max, value));
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (drawnGeneration != snapshotGeneration) {
            drawnGeneration = snapshotGeneration;
            if (CTRNativeActivity.isTouchTraceEnabled()) {
                android.util.Log.d("CTR-UI", "draw ms=" + SystemClock.uptimeMillis()
                        + " gen=" + snapshotGeneration + " frame=" + snapshotFrame
                        + " screen=" + screenState);
            }
        }
        if (screenState == SCREEN_ROOT_MENU) {
            drawRootMenu(canvas);
            drawRootLogo(canvas);
            return;
        }
        if (screenState == SCREEN_LOADING) {
            float w = getWidth(), h = getHeight(), unit = Math.min(w, h);
            drawOutlinedText(canvas, getContext().getString(com.ctrnative.R.string.loading_label),
                    w * 0.5f, h * 0.52f, unit * 0.075f, w * 0.8f, unit, Paint.Align.CENTER, Color.rgb(255, 174, 15));
            return;
        }
        if (screenState == SCREEN_GAME_MENU) {
            if (isAdventureCampaignDeleteConfirmation()) {
                drawAdventureCampaignDeleteConfirmation(canvas, getWidth(), getHeight());
            } else if (isAdventureCampaignManagement()) {
                drawAdventureCampaignScreen(canvas, getWidth(), getHeight());
            } else {
                drawGameMenu(canvas);
                drawRootLogo(canvas);
            }
            return;
        }
        if (screenState == SCREEN_GHOST_SELECT) {
            if (gameMenuRows.length == 0) {
                float w = getWidth(), h = getHeight(), unit = Math.min(getWidth(), getHeight());
                drawMenuPanel(canvas, "CONTRA O TEMPO", w, h, unit);
                drawCampaignText(canvas, "NENHUM FANTASMA SALVO", w * 0.5f, h * 0.56f,
                        unit * 0.065f, Color.WHITE, Paint.Align.CENTER);
                drawMenuBackButton(canvas);
            } else {
                drawResponsiveMenu(canvas, gameMenuTitle.isEmpty() ? "SELECIONE UM FANTASMA" : gameMenuTitle);
            }
            return;
        }
        if (screenState == SCREEN_PAUSE_MENU) {
            if (adventurePauseProgressOpen) {
                drawAdventurePauseProgress(canvas);
            } else {
                drawPauseMenu(canvas);
            }
            return;
        }
        if (screenState == SCREEN_PAUSE_SUBMENU) {
            if (!adventureHintTitle.isEmpty()) {
                drawAdventureHintMenu(canvas);
            } else {
                drawResponsiveMenu(canvas, gameMenuTitle.isEmpty() ? "JOGO PAUSADO" : gameMenuTitle);
            }
            return;
        }
        if (screenState == SCREEN_PAUSE_RESULTS) {
            drawRaceResults(canvas);
            return;
        }
        if (screenState == SCREEN_RACE_RESULTS_MENU) {
            drawRaceResultsMenu(canvas);
            return;
        }
        if (screenState == SCREEN_PAUSE_QUIT_CONFIRM) {
            drawQuitConfirmation(canvas);
            return;
        }
        if (screenState == SCREEN_CHARACTER_SELECT || screenState == SCREEN_TRACK_SELECT) {
            if (screenState == SCREEN_CHARACTER_SELECT) {
                drawCharacterSelectText(canvas);
                if (snapshotInputReady) drawCharacterSelectActions(canvas);
            } else {
                drawTrackSelectText(canvas);
                if (snapshotInputReady) drawTrackSelectActions(canvas);
            }
            return;
        }
        if (screenState == SCREEN_CUP_SELECT) {
            drawCupSelectText(canvas);
            if (snapshotInputReady) drawMenuBackButton(canvas);
            return;
        }
        if (screenState == SCREEN_ADVENTURE_GARAGE) {
            float unit = Math.min(getWidth(), getHeight());
            if (!selectionTextLabels.isEmpty()) {
                drawGarageCard(canvas);
                drawGarageNameAndArrows(canvas);
            }
            drawGarageBackButton(canvas, unit);
            drawGarageContinueButton(canvas, unit);
            return;
        }
        if (screenState == SCREEN_NAME_ENTRY) {
            return;
        }
        if (screenState == SCREEN_CUTSCENE || screenState == SCREEN_DEMO) {
            drawSkipButton(canvas, "TOQUE PARA PULAR");
            return;
        }
        if (screenState == SCREEN_MASK_HINT) {
            drawMaskHintSkipButton(canvas);
            return;
        }
        if (!controlsVisible) {
            return;
        }
        for (Control control : controls) {
            float half = control.size * 0.5f;
            canvas.drawBitmap(control.icon, null,
                    new android.graphics.RectF(control.cx - half, control.cy - half,
                            control.cx + half, control.cy + half), imagePaint);
            if (control.pointerId >= 0) {
                canvas.drawCircle(control.cx, control.cy, control.size * 0.34f, pressedPaint);
            }
        }
    }

    private void drawMaskHintSkipButton(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float unit = Math.min(width, height);
        if (unit <= 0) return;

        float buttonWidth = unit * 0.48f;
        float buttonHeight = unit * 0.095f;
        float left = (width - buttonWidth) * 0.5f;
        float top = height * 0.86f;
        float bottom = Math.min(height * 0.96f, top + buttonHeight);
        drawSelectionActionButton(canvas, left, top, left + buttonWidth, bottom,
                "PULAR", Color.rgb(10, 22, 255), unit);
    }

    private void drawSkipButton(Canvas canvas, String label) {
        float width = getWidth();
        float height = getHeight();
        float unit = Math.min(width, height);
        if (unit <= 0) return;

        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootTextPaint.setTextSize(unit * 0.038f);
        float centerX = width * 0.5f;
        float baseline = height * 0.93f;
        float paddingX = unit * 0.045f;
        float paddingY = unit * 0.018f;
        float halfText = rootTextPaint.measureText(label) * 0.5f;
        float left = centerX - halfText - paddingX;
        float top = baseline + rootTextPaint.ascent() - paddingY;
        float right = centerX + halfText + paddingX;
        float bottom = baseline + rootTextPaint.descent() + paddingY;

        rootPanelPaint.setColor(Color.argb(208, 0, 0, 0));
        canvas.drawRoundRect(left, top, right, bottom, unit * 0.025f, unit * 0.025f, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.004f));
        rootBorderPaint.setColor(Color.WHITE);
        canvas.drawRoundRect(left, top, right, bottom, unit * 0.025f, unit * 0.025f, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);

        rootTextStrokePaint.setTypeface(rootMenuTypeface);
        rootTextStrokePaint.setTextAlign(Paint.Align.CENTER);
        rootTextStrokePaint.setTextSize(rootTextPaint.getTextSize());
        rootTextStrokePaint.setStrokeWidth(unit * 0.004f);
        rootTextStrokePaint.setColor(Color.BLACK);
        canvas.drawText(label, centerX, baseline, rootTextStrokePaint);
        rootTextPaint.setColor(Color.WHITE);
        canvas.drawText(label, centerX, baseline, rootTextPaint);
    }

    private void drawRaceResults(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float unit = Math.min(width, height);
        if (unit <= 0) return;

        canvas.drawColor(Color.argb(58, 0, 0, 0));
        if (raceResultRank >= 0) {
            drawCampaignText(canvas, Integer.toString(raceResultRank + 1), width * 0.16f,
                    height * 0.22f, unit * 0.34f, Color.rgb(255, 177, 0), Paint.Align.CENTER);
        }
        float iconSize = Math.min(unit * 0.105f, width * 0.083f);
        float iconTop = height * 0.43f;
        for (int i = 0; i < raceResultIcons.length; i++) {
            float centerX = width * (i + 0.5f) / raceResultIcons.length;
            float left = centerX - iconSize * 0.5f;
            rootPanelPaint.setColor(Color.argb(195, 0, 0, 0));
            canvas.drawRect(left, iconTop, left + iconSize, iconTop + iconSize, rootPanelPaint);
            if (raceResultIcons[i] != null) {
                float iconW = iconSize * 0.86f;
                float iconH = iconSize * 0.86f;
                canvas.drawBitmap(raceResultIcons[i], null,
                        new android.graphics.RectF(centerX - iconW * 0.5f, iconTop + iconSize * 0.07f,
                                centerX + iconW * 0.5f, iconTop + iconSize * 0.07f + iconH), imagePaint);
            }
            drawCampaignText(canvas, Integer.toString(i + 1), left + iconSize * 0.92f,
                    iconTop + unit * 0.018f, unit * 0.025f, Color.WHITE, Paint.Align.RIGHT);
        }

        if (raceResultLapCount > 0) {
            float panelLeft = width * 0.61f, panelRight = width * 0.94f;
            float panelTop = height * 0.045f, panelBottom = height * 0.405f;
            rootPanelPaint.setColor(Color.argb(222, 4, 4, 4));
            canvas.drawRect(panelLeft, panelTop, panelRight, panelBottom, rootPanelPaint);
            rootBorderPaint.setStyle(Paint.Style.STROKE);
            rootBorderPaint.setColor(Color.WHITE);
            rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.004f));
            canvas.drawRect(panelLeft, panelTop, panelRight, panelBottom, rootBorderPaint);
            rootBorderPaint.setStyle(Paint.Style.FILL);
            float labelX = panelLeft + unit * 0.025f;
            float valueX = panelRight - unit * 0.025f;
            float rowStep = height * 0.073f;
            for (int i = 0; i < raceResultLapCount; i++) {
                float y = panelTop + height * 0.062f + i * rowStep;
                drawCampaignText(canvas, "VOLTA " + (i + 1), labelX, y, unit * 0.039f,
                        Color.rgb(255, 190, 0), Paint.Align.LEFT);
                drawCampaignText(canvas, formatRaceTime(raceResultLapTicks[i]), valueX, y,
                        unit * 0.039f, Color.WHITE, Paint.Align.RIGHT);
            }
            float dividerY = panelTop + height * 0.286f;
            canvas.drawLine(labelX, dividerY, valueX, dividerY, rootBorderPaint);
            float totalY = panelTop + height * 0.347f;
            drawCampaignText(canvas, "TOTAL", labelX, totalY, unit * 0.039f,
                    Color.rgb(255, 79, 0), Paint.Align.LEFT);
            drawCampaignText(canvas, formatRaceTime(raceResultTotalTicks), valueX, totalY,
                    unit * 0.039f, Color.rgb(255, 79, 0), Paint.Align.RIGHT);
        }

        drawSelectionActionButton(canvas, width * 0.335f, height * 0.855f,
                width * 0.665f, height * 0.965f, "CONTINUAR", Color.rgb(0, 35, 235), unit);
    }

    private String formatRaceTime(int ticks) {
        int centiseconds = (int) (Math.max(0L, ticks) * 100L / 960L);
        return String.format(java.util.Locale.ROOT, "%02d:%02d:%02d",
                centiseconds / 6000, (centiseconds / 100) % 60, centiseconds % 100);
    }

    private void drawRaceResultsMenu(Canvas canvas) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w, h);
        if (w <= 0 || h <= 0 || gameMenuRows.length == 0) return;
        canvas.drawColor(Color.argb(150, 0, 0, 0));
        float left = w * 0.24f, right = w * 0.76f, gap = h * 0.018f;
        float rowHeight = Math.min(h * 0.095f, (h * 0.43f - gap * (gameMenuRows.length - 1)) / gameMenuRows.length);
        float blockHeight = gameMenuRows.length * rowHeight + (gameMenuRows.length - 1) * gap;
        float top = h * 0.28f + (h * 0.43f - blockHeight) * 0.5f;
        float textSize = Math.min(unit * 0.061f, (right - left) * 0.095f);
        int previousSelection = rootMenuSelection;
        rootMenuSelection = gameMenuSelection;
        for (int i = 0; i < gameMenuRows.length; i++) {
            drawRootMenuRow(canvas, left, right, top + i * (rowHeight + gap), rowHeight,
                    gameMenuRows[i], i, textSize, unit);
        }
        rootMenuSelection = previousSelection;
    }

    private boolean isRaceResultsContinueAt(float x, float y) {
        return x >= getWidth() * 0.335f && x <= getWidth() * 0.665f
                && y >= getHeight() * 0.855f && y <= getHeight() * 0.965f;
    }

    private void drawGameMenu(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || gameMenuRows.length == 0) return;
        float unit = Math.min(w, h);
        float rowLeft = w * 0.59f;
        float rowRight = w * 0.97f;
        float gap = h * 0.018f;
        float rowHeight = Math.min(h * 0.13f,
                (h * 0.60f - gap * (gameMenuRows.length - 1)) / gameMenuRows.length);
        float blockHeight = gameMenuRows.length * rowHeight + (gameMenuRows.length - 1) * gap;
        float top = (h - blockHeight) * 0.5f;
        float textSize = Math.min(unit * 0.074f, (rowRight - rowLeft) * 0.105f);
        int previousSelection = rootMenuSelection;
        rootMenuSelection = gameMenuSelection;
        for (int i = 0; i < gameMenuRows.length; i++) {
            drawRootMenuRow(canvas, rowLeft, rowRight, top + i * (rowHeight + gap),
                    rowHeight, gameMenuRows[i], i, textSize, unit);
        }
        rootMenuSelection = previousSelection;
        drawMenuBackButton(canvas);
    }

    private void drawResponsiveMenu(Canvas canvas, String title) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w, h);
        if (w <= 0 || h <= 0 || gameMenuRows.length == 0) return;
        drawMenuPanel(canvas, title, w, h, unit);
        float left = w * 0.12f, right = w * 0.88f;
        float gap = h * 0.018f;
        float rowHeight = responsiveMenuRowHeight(h, gameMenuRows.length);
        float top = responsiveMenuRowsTop(h, gameMenuRows.length);
        float textSize = Math.min(unit * 0.064f, (right - left) * 0.085f);
        int previousSelection = rootMenuSelection;
        rootMenuSelection = gameMenuSelection;
        for (int i = 0; i < gameMenuRows.length; i++) {
            drawRootMenuRow(canvas, left, right, top + i * (rowHeight + gap),
                    rowHeight, gameMenuRows[i], i, textSize, unit);
        }
        rootMenuSelection = previousSelection;
        drawMenuBackButton(canvas);
    }

    private void drawAdventureHintMenu(Canvas canvas) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w, h);
        if (w <= 0 || h <= 0) return;
        drawMenuPanel(canvas, adventureHintTitle, w, h, unit);
        if (adventureHintDetails) {
            TextPaint bodyPaint = new TextPaint(rootTextPaint);
            bodyPaint.setTypeface(rootMenuTypeface);
            bodyPaint.setTextSize(unit * 0.047f);
            bodyPaint.setTextAlign(Paint.Align.LEFT);
            bodyPaint.setColor(Color.WHITE);
            int bodyWidth = (int)(w * 0.80f);
            String bodyText = adventureHintBody.replace("(", "").replace(")", "");
            StaticLayout body = new StaticLayout(bodyText, bodyPaint, bodyWidth,
                    Layout.Alignment.ALIGN_CENTER, 1.12f, 0f, true);
            canvas.save();
            canvas.translate((w - bodyWidth) * 0.5f, h * 0.31f);
            body.draw(canvas);
            canvas.restore();
        } else if (adventureHintRows.length > 0) {
            float left = w * 0.12f, right = w * 0.88f;
            float gap = h * 0.018f;
            float rowHeight = responsiveMenuRowHeight(h, adventureHintRows.length);
            float top = responsiveMenuRowsTop(h, adventureHintRows.length);
            float textSize = Math.min(unit * 0.064f, (right - left) * 0.085f);
            int previousSelection = rootMenuSelection;
            for (int i = 0; i < adventureHintRows.length; i++) {
                rootMenuSelection = adventureHintRowIndexes[i] == adventureHintSelection ? i : -1;
                drawRootMenuRow(canvas, left, right, top + i * (rowHeight + gap),
                        rowHeight, adventureHintRows[i], i, textSize, unit);
            }
            rootMenuSelection = previousSelection;
        }
        drawMenuBackButton(canvas);
    }

    private void drawMenuPanel(Canvas canvas, String title, float w, float h, float unit) {
        canvas.drawColor(Color.argb(142, 0, 0, 0));
        rootPanelPaint.setColor(Color.argb(212, 3, 3, 3));
        canvas.drawRect(w * 0.055f, h * 0.17f, w * 0.945f, h * 0.91f, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.005f));
        canvas.drawRect(w * 0.055f, h * 0.17f, w * 0.945f, h * 0.91f, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        String heading = title.isEmpty() ? "MENU" : title.toUpperCase(java.util.Locale.ROOT);
        drawCampaignText(canvas, heading, w * 0.5f, h * 0.245f,
                unit * 0.065f, Color.WHITE, Paint.Align.CENTER);
    }

    private float responsiveMenuRowsTop(float h, int count) {
        float gap = h * 0.018f;
        float rowHeight = Math.min(h * 0.13f, (h * 0.52f - gap * (count - 1)) / count);
        float blockHeight = count * rowHeight + (count - 1) * gap;
        return h * 0.31f + (h * 0.52f - blockHeight) * 0.5f;
    }

    private float responsiveMenuRowHeight(float h, int count) {
        float gap = h * 0.018f;
        return Math.min(h * 0.13f, (h * 0.52f - gap * (count - 1)) / count);
    }

    public void drawAdventureCampaignDeleteConfirmation(Canvas canvas, float w, float h) {
        float unit = Math.min(w, h);
        rootPanelPaint.setColor(Color.argb(238, 0, 0, 0));
        canvas.drawRect(w * 0.20f, h * 0.31f, w * 0.80f, h * 0.68f, rootPanelPaint);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.005f));
        canvas.drawRect(w * 0.20f, h * 0.31f, w * 0.80f, h * 0.68f, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        drawCampaignText(canvas, "DESEJA EXCLUIR A CAMPANHA?", w * 0.5f, h * 0.42f,
                unit * 0.046f, Color.WHITE, Paint.Align.CENTER);
        drawCampaignButton(canvas, w * 0.33f, h * 0.52f, w * 0.49f, h * 0.62f,
                "SIM", Color.RED, gameMenuSelection == 0, unit);
        drawCampaignButton(canvas, w * 0.51f, h * 0.52f, w * 0.67f, h * 0.62f,
                "NÃO", Color.rgb(0, 65, 225), gameMenuSelection == 1, unit);
    }

    public void drawAdventureCampaignScreen(Canvas canvas, float w, float h) {
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);

        float backLeft = w * 0.73f;
        float backTop = h * 0.065f;
        float backRight = w * 0.89f;
        float backBottom = h * 0.16f;
        rootPanelPaint.setColor(Color.rgb(6, 48, 176));
        canvas.drawRect(backLeft, backTop, backRight, backBottom, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.006f));
        canvas.drawRect(backLeft, backTop, backRight, backBottom, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        drawCampaignText(canvas, "VOLTAR", (backLeft + backRight) * 0.5f,
                (backTop + backBottom) * 0.5f, unit * 0.035f, Color.WHITE, Paint.Align.CENTER);

        float left = w * 0.09f;
        float top = h * 0.205f;
        float right = w * 0.90f;
        float bottom = h * 0.82f;

        rootPanelPaint.setColor(Color.argb(238, 0, 0, 0));
        canvas.drawRect(left, top, right, bottom, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.009f));
        canvas.drawRect(left, top, right, bottom, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);

        float contentLeft = left + (right - left) * 0.035f;
        float portraitSize = Math.min((bottom - top) * 0.53f, (right - left) * 0.19f);
        float portraitTop = top + (bottom - top) * 0.19f;
        if (adventureCampaignPortrait != null) {
            float ratio = Math.min(portraitSize / adventureCampaignPortrait.getWidth(),
                    portraitSize / adventureCampaignPortrait.getHeight());
            float pw = adventureCampaignPortrait.getWidth() * ratio;
            float ph = adventureCampaignPortrait.getHeight() * ratio;
            float px = contentLeft + (portraitSize - pw) * 0.5f;
            float py = portraitTop + (portraitSize - ph) * 0.5f;
            canvas.drawBitmap(adventureCampaignPortrait, null,
                    new android.graphics.RectF(px, py, px + pw, py + ph), imagePaint);
        } else {
            rootPanelPaint.setColor(Color.rgb(35, 43, 53));
            canvas.drawCircle(contentLeft + portraitSize * 0.5f, portraitTop + portraitSize * 0.5f,
                    portraitSize * 0.46f, rootPanelPaint);
        }

        float infoLeft = left + (right - left) * 0.26f;
        float nameY = top + (bottom - top) * 0.19f;
        drawCampaignText(canvas, adventureCampaignName.toUpperCase(java.util.Locale.ROOT),
                infoLeft, nameY, unit * 0.08f, Color.WHITE, Paint.Align.LEFT);

        float barLeft = left + (right - left) * 0.46f;
        float barRight = left + (right - left) * 0.86f;
        float barY = nameY - unit * 0.012f;
        rootPanelPaint.setColor(Color.rgb(80, 80, 80));
        canvas.drawRoundRect(barLeft, barY, barRight, barY + unit * 0.024f,
                unit * 0.012f, unit * 0.012f, rootPanelPaint);
        rootPanelPaint.setColor(Color.rgb(250, 158, 13));
        canvas.drawRoundRect(barLeft, barY, barLeft + (barRight - barLeft) * adventureCampaignPercent / 100f,
                barY + unit * 0.024f, unit * 0.012f, unit * 0.012f, rootPanelPaint);
        drawCampaignText(canvas, adventureCampaignPercent + "%", right - (right - left) * 0.035f,
                nameY, unit * 0.06f, Color.WHITE, Paint.Align.RIGHT);

        String[] labels = {"TROFEUS", "CHAVES", "RELIQUIA", "CTR"};
        float statsLeft = infoLeft;
        float statsRight = right - (right - left) * 0.035f;
        float colWidth = (statsRight - statsLeft) / 4f;
        float iconY = top + (bottom - top) * 0.40f;
        float iconSize = unit * 0.13f;
        for (int i = 0; i < labels.length; i++) {
            float centerX = statsLeft + colWidth * (i + 0.5f);
            Bitmap icon = adventureCampaignIcons[i];
            if (icon != null) {
                float iconScale = Math.min(iconSize / icon.getWidth(), iconSize / icon.getHeight());
                float iconWidth = icon.getWidth() * iconScale;
                float iconHeight = icon.getHeight() * iconScale;
                canvas.drawBitmap(icon, null, new android.graphics.RectF(
                        centerX - iconWidth * 0.5f, iconY - iconHeight * 0.5f,
                        centerX + iconWidth * 0.5f, iconY + iconHeight * 0.5f), imagePaint);
            }
            drawCampaignText(canvas, labels[i], centerX, iconY + unit * 0.11f,
                    unit * 0.04f, Color.WHITE, Paint.Align.CENTER);
            drawCampaignText(canvas, adventureCampaignCounts[i] + "/16", centerX,
                    iconY + unit * 0.155f, unit * 0.038f, Color.WHITE, Paint.Align.CENTER);
        }

        float buttonTop = bottom - (bottom - top) * 0.22f;
        float buttonBottom = bottom - (bottom - top) * 0.075f;
        float buttonWidth = (right - left) * 0.255f;
        float buttonLeft = (left + right - buttonWidth) * 0.5f;
        drawCampaignButton(canvas, buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonBottom,
                "DELETAR", Color.rgb(255, 0, 0), gameMenuSelection == 0, unit);
    }

    private void drawCampaignButton(Canvas canvas, float left, float top, float right, float bottom,
                                    String label, int color, boolean selected, float unit) {
        float skew = unit * 0.025f;
        Path button = new Path();
        button.moveTo(left + skew, top);
        button.lineTo(right, top);
        button.lineTo(right - skew, bottom);
        button.lineTo(left, bottom);
        button.close();
        rootPanelPaint.setColor(color);
        canvas.drawPath(button, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * (selected ? 0.008f : 0.005f)));
        canvas.drawPath(button, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        drawCampaignText(canvas, label, (left + right) * 0.5f, (top + bottom) * 0.5f,
                unit * 0.043f, Color.WHITE, Paint.Align.CENTER);
    }

    private void drawCampaignText(Canvas canvas, String text, float x, float centerY,
                                  float size, int color, Paint.Align align) {
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(align);
        rootTextPaint.setTextSize(size);
        rootTextPaint.setColor(color);
        rootTextStrokePaint.setTypeface(rootMenuTypeface);
        rootTextStrokePaint.setTextAlign(align);
        rootTextStrokePaint.setTextSize(size);
        rootTextStrokePaint.setStrokeWidth(Math.max(1f, size * 0.09f));
        rootTextStrokePaint.setColor(Color.argb(220, 0, 0, 0));
        float baseline = centerY - (rootTextPaint.ascent() + rootTextPaint.descent()) * 0.5f;
        canvas.drawText(text, x, baseline, rootTextStrokePaint);
        canvas.drawText(text, x, baseline, rootTextPaint);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
    }

    private void drawPauseMenu(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || gameMenuRows.length == 0) {
            return;
        }

        float unit = Math.min(w, h);
        float panelLeft = w * 0.18f;
        float panelRight = w * 0.82f;
        float panelTop = h * 0.075f;

        canvas.drawColor(Color.argb(142, 2, 13, 25));

        String title = "JOGO PAUSADO";
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootTextPaint.setColor(Color.rgb(255, 177, 33));
        float titleSize = unit * 0.09f;
        rootTextPaint.setTextSize(titleSize);
        float titleWidth = panelRight - panelLeft - unit * 0.14f;
        if (rootTextPaint.measureText(title) > titleWidth) {
            titleSize *= titleWidth / rootTextPaint.measureText(title);
            rootTextPaint.setTextSize(titleSize);
        }
        float titleY = panelTop + h * 0.125f;
        rootTextStrokePaint.setTypeface(rootMenuTypeface);
        rootTextStrokePaint.setTextAlign(Paint.Align.CENTER);
        rootTextStrokePaint.setTextSize(titleSize);
        rootTextStrokePaint.setStrokeWidth(unit * 0.004f);
        rootTextStrokePaint.setColor(Color.rgb(12, 34, 54));
        canvas.drawText(title, w * 0.5f, titleY, rootTextStrokePaint);
        canvas.drawText(title, w * 0.5f, titleY, rootTextPaint);

        float rowLeft = w * 0.235f;
        float rowRight = w * 0.765f;
        float gap = pauseMenuRowGap(h);
        float rowHeight = pauseMenuRowHeight(h, gameMenuRows.length);
        float top = pauseMenuRowsTop(h, gameMenuRows.length);
        float textSize = Math.min(unit * 0.062f, (rowRight - rowLeft) * 0.105f);
        int previousSelection = rootMenuSelection;
        rootMenuSelection = gameMenuSelection;
        for (int i = 0; i < gameMenuRows.length; i++) {
            drawRootMenuRow(canvas, rowLeft, rowRight, top + i * (rowHeight + gap),
                    rowHeight, gameMenuRows[i], i, textSize, unit);
        }
        drawMenuBackButton(canvas);
        rootMenuSelection = previousSelection;
    }

    private void drawAdventurePauseProgress(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || adventurePauseTitle.isEmpty()) return;
        float unit = Math.min(w, h);
        canvas.drawColor(Color.argb(112, 0, 0, 0));

        float left = w * 0.18f;
        float top = h * 0.17f;
        float right = w * 0.82f;
        float bottom = h * 0.94f;
        rootPanelPaint.setColor(Color.argb(220, 3, 3, 3));
        canvas.drawRect(left, top, right, bottom, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.005f));
        canvas.drawRect(left, top, right, bottom, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);

        float panelWidth = right - left;
        float panelHeight = bottom - top;
        float titleY = top + panelHeight * 0.10f;
        float leftArrowX = left + panelWidth * 0.20f;
        float rightArrowX = right - panelWidth * 0.20f;
        String title = adventurePauseTitle.toUpperCase(java.util.Locale.ROOT);
        float titleSize = unit * 0.064f;
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextSize(titleSize);
        float titleMaxWidth = rightArrowX - leftArrowX - unit * 0.20f;
        float titleWidth = rootTextPaint.measureText(title);
        if (titleWidth > titleMaxWidth) titleSize *= titleMaxWidth / titleWidth;
        drawCampaignText(canvas, title, w * 0.5f, titleY, titleSize, Color.WHITE, Paint.Align.CENTER);
        drawPausePageArrow(canvas, leftArrowX, titleY, false, unit);
        drawPausePageArrow(canvas, rightArrowX, titleY, true, unit);

        float listTop = top + panelHeight * 0.22f;
        float listBottom = bottom - panelHeight * 0.035f;
        int rowCount = adventurePauseRows.length;
        if (rowCount > 0) {
            float rowGap = panelHeight * 0.006f;
            float rowHeight = Math.min(panelHeight * 0.11f,
                    (listBottom - listTop - rowGap * (rowCount - 1)) / rowCount);
            float textSize = Math.min(unit * 0.052f, rowHeight * 0.62f);
            for (int i = 0; i < rowCount; i++) {
                String[] fields = adventurePauseRows[i].split("\\t", -1);
                if (fields.length < 2 || fields[0].isEmpty()) continue;
                float rowTop = listTop + i * (rowHeight + rowGap);
                float rowBottom = rowTop + rowHeight;

                String kind = fields[0];
                String label;
                try {
                    if ("R".equals(kind) && fields.length >= 5) {
                        label = fields[1];
                        drawPauseRewardIcon(canvas, 0, w * 0.59f, (rowTop + rowBottom) * 0.5f,
                                rowHeight * 0.82f, Integer.parseInt(fields[2]) != 0);
                        drawPauseRewardIcon(canvas, 2, w * 0.65f, (rowTop + rowBottom) * 0.5f,
                                rowHeight * 0.82f, Integer.parseInt(fields[3]) != 0);
                        drawPauseRewardIcon(canvas, 3, w * 0.71f, (rowTop + rowBottom) * 0.5f,
                                rowHeight * 0.82f, Integer.parseInt(fields[4]) != 0);
                    } else if ("B".equals(kind) && fields.length >= 3) {
                        label = fields[1];
                        if (adventurePausePage == 0) {
                            drawPauseStatusMark(canvas, w * 0.70f, (rowTop + rowBottom) * 0.5f,
                                    rowHeight * 0.56f, "★", Integer.parseInt(fields[2]) != 0);
                        } else {
                            drawPauseRewardIcon(canvas, 1, w * 0.70f, (rowTop + rowBottom) * 0.5f,
                                    rowHeight * 0.62f, Integer.parseInt(fields[2]) != 0);
                        }
                    } else if ("C".equals(kind) && fields.length >= 3) {
                        label = fields[1];
                        drawPauseRewardIcon(canvas, 3, w * 0.70f, (rowTop + rowBottom) * 0.5f,
                                rowHeight * 0.62f, Integer.parseInt(fields[2]) != 0);
                    } else if ("T".equals(kind) && fields.length >= 3) {
                        label = "CTR " + (Integer.parseInt(fields[1]) + 1);
                        drawCampaignText(canvas, fields[2], w * 0.70f, (rowTop + rowBottom) * 0.5f,
                                textSize, Color.WHITE, Paint.Align.CENTER);
                    } else if ("L".equals(kind) && fields.length >= 3) {
                        String[] relicNames = {"SAFIRA", "OURO", "PLATINA"};
                        int relic = Integer.parseInt(fields[1]);
                        label = relicNames[Math.max(0, Math.min(relic, relicNames.length - 1))];
                        drawPauseRewardIcon(canvas, 2, w * 0.65f, (rowTop + rowBottom) * 0.5f,
                                rowHeight * 0.62f, true);
                        drawCampaignText(canvas, fields[2], w * 0.71f, (rowTop + rowBottom) * 0.5f,
                                textSize, Color.WHITE, Paint.Align.CENTER);
                    } else {
                        continue;
                    }
                } catch (NumberFormatException error) {
                    continue;
                }
                drawCampaignText(canvas, label.toUpperCase(java.util.Locale.ROOT),
                        left + panelWidth * 0.045f, (rowTop + rowBottom) * 0.5f,
                        textSize, Color.WHITE, Paint.Align.LEFT);
            }
        }
        drawMenuBackButton(canvas);
    }

    private void drawPausePageArrow(Canvas canvas, float x, float centerY, boolean pointsRight, float unit) {
        float halfWidth = unit * 0.046f;
        float halfHeight = unit * 0.037f;
        rootPanelPaint.setColor(Color.rgb(255, 190, 0));
        canvas.drawRect(x - halfWidth, centerY - halfHeight,
                x + halfWidth, centerY + halfHeight, rootPanelPaint);
        Path arrow = new Path();
        if (pointsRight) {
            arrow.moveTo(x - halfWidth * 0.26f, centerY - halfHeight * 0.56f);
            arrow.lineTo(x + halfWidth * 0.42f, centerY);
            arrow.lineTo(x - halfWidth * 0.26f, centerY + halfHeight * 0.56f);
        } else {
            arrow.moveTo(x + halfWidth * 0.26f, centerY - halfHeight * 0.56f);
            arrow.lineTo(x - halfWidth * 0.42f, centerY);
            arrow.lineTo(x + halfWidth * 0.26f, centerY + halfHeight * 0.56f);
        }
        arrow.close();
        rootPanelPaint.setColor(Color.BLACK);
        canvas.drawPath(arrow, rootPanelPaint);
    }

    private void drawPauseRewardIcon(Canvas canvas, int iconIndex, float x, float y,
                                    float size, boolean collected) {
        Bitmap icon = adventureCampaignIcons[iconIndex];
        if (icon == null) return;
        float scale = Math.min(size / icon.getWidth(), size / icon.getHeight());
        float width = icon.getWidth() * scale;
        float height = icon.getHeight() * scale;
        imagePaint.setAlpha(collected ? 255 : 82);
        canvas.drawBitmap(icon, null, new android.graphics.RectF(x - width * 0.5f,
                y - height * 0.5f, x + width * 0.5f, y + height * 0.5f), imagePaint);
        imagePaint.setAlpha(255);
    }

    private void drawPauseStatusMark(Canvas canvas, float x, float y, float size,
                                     String mark, boolean active) {
        rootPanelPaint.setColor(active ? Color.rgb(255, 166, 0) : Color.rgb(73, 73, 73));
        canvas.drawCircle(x, y, size * 0.5f, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(1f, size * 0.055f));
        canvas.drawCircle(x, y, size * 0.5f, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        drawCampaignText(canvas, mark, x, y, size * 0.58f, Color.WHITE, Paint.Align.CENTER);
    }

    private void drawQuitConfirmation(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || gameMenuRows.length < 2) return;

        float unit = Math.min(w, h);
        float buttonWidth = Math.min(w * 0.15f, unit * 0.34f);
        float gap = unit * 0.025f;
        float buttonHeight = unit * 0.095f;
        float left = (w - buttonWidth * 2f - gap) * 0.5f;
        float top = h * 0.445f;
        int previousSelection = rootMenuSelection;
        rootMenuSelection = gameMenuSelection;
        drawRootMenuRow(canvas, left, left + buttonWidth, top, buttonHeight,
                gameMenuRows[1], 1, unit * 0.052f, unit);
        drawRootMenuRow(canvas, left + buttonWidth + gap, left + buttonWidth * 2f + gap,
                top, buttonHeight, gameMenuRows[0], 0, unit * 0.052f, unit);
        rootMenuSelection = previousSelection;
        drawMenuBackButton(canvas);
    }

    private void drawCharacterSelectActions(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        float buttonTop = h * 0.80f;
        float buttonBottom = h * 0.92f;
        drawSelectionActionButton(canvas, w * 0.79f, buttonTop, w * 0.97f, buttonBottom,
                "CONTINUAR", Color.rgb(250, 158, 13), unit);
        drawSelectionActionButton(canvas, w * 0.03f, buttonTop, w * 0.21f, buttonBottom,
                "VOLTAR", Color.rgb(0, 63, 213), unit);
    }

    private void drawCharacterSelectText(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        drawOutlinedText(canvas, "SELECIONAR PERSONAGEM", w * 0.5f, h * 0.13f,
                unit * 0.092f, w * 0.90f, unit, Paint.Align.CENTER, Color.WHITE);
        if (!characterSelectName.isEmpty()) {
            float right = w * 0.81f;
            float previewRight = w * 0.83f;
            float baseline = w * 0.25f;
            float size = unit * 0.052f;
            rootTextPaint.setTypeface(rootMenuTypeface);
            rootTextPaint.setTextSize(size);
            rootTextPaint.setTextAlign(Paint.Align.RIGHT);
            float textWidth = Math.min(rootTextPaint.measureText(characterSelectName), w * 0.20f);
            float left = right - textWidth - unit * 0.045f;
            float top = baseline + rootTextPaint.ascent() - unit * 0.025f;
            float bottom = baseline + rootTextPaint.descent() + unit * 0.025f;
            Path label = new Path();
            label.moveTo(left + unit * 0.04f, top);
            label.lineTo(previewRight, top);
            label.lineTo(previewRight, bottom);
            label.lineTo(left, bottom);
            label.close();
            rootPanelPaint.setColor(Color.argb(175, 5, 47, 113));
            canvas.drawPath(label, rootPanelPaint);
            drawOutlinedText(canvas, characterSelectName, right, baseline,
                    size, w * 0.20f, unit, Paint.Align.RIGHT, Color.WHITE);
        }
    }

    private void drawTrackSelectText(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        float unit = Math.min(w, h);
        for (SelectionTextLabel label : selectionTextLabels) {
            float size = unit * (label.title ? 0.07f : 0.045f);
            float baseline = (label.y + (label.title ? 12f : 9f) / 216f) * h;
            drawOutlinedText(canvas, label.text, label.x * w, baseline, size,
                    w * (label.title ? 0.46f : 0.44f), unit,
                    label.title ? Paint.Align.CENTER : Paint.Align.LEFT, Color.WHITE);
        }
    }

    private void drawCupSelectText(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        float unit = Math.min(w, h);
        for (SelectionTextLabel label : selectionTextLabels) {
            float baseline = label.title ? Math.max(label.y * h, h * 0.15f) : label.y * h;
            float size = unit * (label.title ? 0.092f : 0.07f);
            float maxWidth = w * (label.title ? 0.90f : 0.34f);
            if (!label.title) {
                String text = label.text.replace("(", "").replace(")", "");
                rootTextPaint.setTypeface(rootMenuTypeface);
                rootTextPaint.setTextSize(size);
                float textWidth = rootTextPaint.measureText(text);
                if (textWidth > maxWidth) size *= maxWidth / textWidth;
                rootTextPaint.setTextSize(size);
                float halfText = rootTextPaint.measureText(text) * 0.5f;
                float paddingX = unit * 0.025f;
                float paddingY = unit * 0.014f;
                float top = baseline + rootTextPaint.ascent() - paddingY;
                float bottom = baseline + rootTextPaint.descent() + paddingY;
                rootPanelPaint.setColor(Color.argb(190, 0, 63, 213));
                canvas.drawRect(label.x * w - halfText - paddingX, top,
                        label.x * w + halfText + paddingX, bottom, rootPanelPaint);
            }
            drawOutlinedText(canvas, label.text, label.x * w, baseline, size,
                    maxWidth, unit,
                    Paint.Align.CENTER, Color.WHITE);
        }
    }

    private void drawGarageCard(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        SelectionTextLabel classLabel = garageClassLabel();
        float left = w * (classLabel != null && classLabel.x < 0.5f ? 0.02f : 0.52f);
        float right = left + w * 0.46f;
        float top = h * 0.04f;
        float bottom = h * 0.42f;
        float width = right - left;
        float height = bottom - top;

        garageCardPaint.setColor(Color.argb(224, 0, 0, 0));
        canvas.drawRect(left, top, right, bottom, garageCardPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.005f));
        rootBorderPaint.setColor(Color.WHITE);
        canvas.drawRect(left, top, right, bottom, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);

        drawOutlinedText(canvas, garageClassName(), left + width * 0.5f,
                top + height * 0.17f, unit * 0.052f, width * 0.90f,
                unit, Paint.Align.CENTER, Color.rgb(255, 166, 20));

        float barLeft = left + width * 0.045f;
        float barRight = right - width * 0.045f;
        float barHeight = Math.max(4f, h * 0.019f);
        float radius = barHeight * 0.5f;
        garageBarPaint.setShader(new LinearGradient(barLeft, top, barRight, top,
                new int[]{Color.rgb(0, 70, 255), Color.rgb(0, 200, 35),
                        Color.rgb(255, 210, 0), Color.rgb(255, 122, 0)},
                new float[]{0f, 0.28f, 0.72f, 1f}, Shader.TileMode.CLAMP));
        for (int i = 0; i < garageStatRatios.length; i++) {
            SelectionTextLabel label = i < selectionTextLabels.size()
                    ? selectionTextLabels.get(i) : null;
            if (label != null) {
                drawOutlinedText(canvas, label.text, barLeft,
                        top + height * (0.35f + i * 0.20f), unit * 0.031f,
                        width * 0.90f, unit, Paint.Align.LEFT, Color.WHITE);
            }
            float barTop = top + height * (0.41f + i * 0.20f);
            float fillRight = barLeft + (barRight - barLeft) * garageStatRatios[i];
            garageBarTrackPaint.setColor(Color.rgb(75, 75, 75));
            canvas.drawRoundRect(barLeft, barTop, barRight, barTop + barHeight,
                    radius, radius, garageBarTrackPaint);
            if (fillRight > barLeft) {
                canvas.drawRoundRect(barLeft, barTop, fillRight, barTop + barHeight,
                        radius, radius, garageBarPaint);
            }
        }
        garageBarPaint.setShader(null);
    }

    private String garageClassName() {
        SelectionTextLabel label = garageClassLabel();
        return label == null ? "" : label.text;
    }

    private SelectionTextLabel garageClassLabel() {
        for (SelectionTextLabel label : selectionTextLabels) {
            if (label.title && label.y < 0.5f) return label;
        }
        return null;
    }

    private void drawGarageNameAndArrows(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        float top = h * 0.82f;
        float bottom = h;
        garageCardPaint.setShader(new LinearGradient(0, top, 0, bottom,
                new int[]{Color.TRANSPARENT, Color.argb(140, 0, 0, 0),
                        Color.argb(238, 0, 0, 0)},
                new float[]{0f, 0.55f, 1f}, Shader.TileMode.CLAMP));
        canvas.drawRect(0, top, w, bottom, garageCardPaint);
        garageCardPaint.setShader(null);
        for (SelectionTextLabel label : selectionTextLabels) {
            if (label.title && label.y > 0.5f) {
                drawOutlinedText(canvas, label.text, w * 0.5f,
                        top + (bottom - top) * 0.69f, unit * 0.061f,
                        w * 0.38f, unit, Paint.Align.CENTER, Color.WHITE);
                break;
            }
        }
        drawGarageArrowButton(canvas, w * 0.29f, (top + bottom) * 0.5f,
                unit * 0.095f, true);
        drawGarageArrowButton(canvas, w * 0.71f, (top + bottom) * 0.5f,
                unit * 0.095f, false);
    }

    private void drawGarageArrowButton(Canvas canvas, float centerX, float centerY,
            float size, boolean isLeft) {
        float width = size;
        float height = size * 0.82f;
        float left = centerX - width * 0.5f;
        float top = centerY - height * 0.5f;
        garageCardPaint.setColor(Color.rgb(250, 158, 13));
        canvas.drawRect(left, top, left + width, top + height, garageCardPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(1.5f, size * 0.045f));
        rootBorderPaint.setColor(Color.WHITE);
        canvas.drawRect(left, top, left + width, top + height, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        Path arrow = new Path();
        float direction = isLeft ? -1f : 1f;
        float midX = centerX + direction * width * 0.07f;
        arrow.moveTo(midX - direction * width * 0.20f, centerY - height * 0.29f);
        arrow.lineTo(midX + direction * width * 0.20f, centerY);
        arrow.lineTo(midX - direction * width * 0.20f, centerY + height * 0.29f);
        arrow.close();
        garageCardPaint.setColor(Color.WHITE);
        canvas.drawPath(arrow, garageCardPaint);
    }

    private void drawGarageBackButton(Canvas canvas, float unit) {
        float w = getWidth();
        float h = getHeight();
        drawSelectionActionButton(canvas, w * 0.035f, h * 0.90f,
                w * 0.18f, h * 0.99f, "VOLTAR", Color.rgb(0, 63, 213), unit);
    }

    private void drawGarageContinueButton(Canvas canvas, float unit) {
        float w = getWidth();
        float h = getHeight();
        drawSelectionActionButton(canvas, w * 0.82f, h * 0.90f,
                w * 0.965f, h * 0.99f, "CONTINUAR", Color.rgb(250, 158, 13), unit);
    }

    private void drawOutlinedText(Canvas canvas, String text, float x, float baseline,
            float preferredSize, float maxWidth, float unit, Paint.Align align, int color) {
        String displayText = text.replace("(", "").replace(")", "");
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(align);
        rootTextPaint.setTextSize(preferredSize);
        float measuredWidth = rootTextPaint.measureText(displayText);
        if (measuredWidth > maxWidth) {
            rootTextPaint.setTextSize(preferredSize * maxWidth / measuredWidth);
        }
        rootTextStrokePaint.setTypeface(rootMenuTypeface);
        rootTextStrokePaint.setTextAlign(align);
        rootTextStrokePaint.setTextSize(rootTextPaint.getTextSize());
        rootTextStrokePaint.setStrokeWidth(unit * 0.006f);
        rootTextStrokePaint.setColor(Color.BLACK);
        rootTextPaint.setColor(color);
        canvas.drawText(displayText, x, baseline, rootTextStrokePaint);
        canvas.drawText(displayText, x, baseline, rootTextPaint);
    }

    private void drawTrackSelectActions(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        float buttonTop = h * 0.82f;
        float buttonBottom = h * 0.95f;
        drawSelectionActionButton(canvas, w * 0.79f, buttonTop, w * 0.97f, buttonBottom,
                "INICIAR CORRIDA", Color.rgb(250, 158, 13), unit);
        drawSelectionActionButton(canvas, w * 0.595f, buttonTop, w * 0.775f, buttonBottom,
                "VOLTAR", Color.rgb(0, 63, 213), unit);
    }

    private void drawSelectionActionButton(Canvas canvas, float buttonLeft, float buttonTop,
            float buttonRight, float buttonBottom, String label, int fillColor, float unit) {
        Path button = new Path();
        float skew = unit * 0.025f;
        button.moveTo(buttonLeft + skew, buttonTop);
        button.lineTo(buttonRight, buttonTop);
        button.lineTo(buttonRight - skew, buttonBottom);
        button.lineTo(buttonLeft, buttonBottom);
        button.close();
        rootSelectedPaint.setColor(fillColor);
        canvas.drawPath(button, rootSelectedPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.006f));
        rootBorderPaint.setColor(Color.rgb(194, 235, 250));
        canvas.drawPath(button, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        rootTextPaint.setColor(Color.WHITE);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootTextPaint.setTextSize(unit * 0.043f);
        canvas.drawText(label, (buttonLeft + buttonRight) * 0.5f,
                buttonTop + (buttonBottom - buttonTop) * 0.62f, rootTextPaint);
    }

    private void drawMenuBackButton(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        float unit = Math.min(w, h);
        drawSelectionActionButton(canvas, w * 0.78f, h * 0.035f, w * 0.97f, h * 0.15f,
                "VOLTAR", Color.rgb(0, 63, 213), unit);
    }

    private boolean isTopMenuBackButtonAt(float x, float y) {
        float w = getWidth(), h = getHeight();
        return isActionButtonAt(x, y, w * 0.78f, h * 0.035f,
                w * 0.97f, h * 0.15f, w * 0.01f, h * 0.015f);
    }

    private boolean isAdventurePausePageArrowAt(float x, float y, boolean pointsRight) {
        float w = getWidth(), h = getHeight(), unit = Math.min(w, h);
        float centerX = pointsRight ? w * 0.692f : w * 0.308f;
        float centerY = h * 0.247f;
        return Math.abs(x - centerX) <= unit * 0.07f
                && Math.abs(y - centerY) <= unit * 0.06f;
    }

    private static boolean isActionButtonAt(float x, float y, float left, float top,
            float right, float bottom, float paddingX, float paddingY) {
        return x >= left - paddingX && x <= right + paddingX
                && y >= top - paddingY && y <= bottom + paddingY;
    }

    void drawPauseOptionsBackButton(Canvas canvas, float width, float height, float unit) {
        drawSelectionActionButton(canvas, 0, 0, width, height,
                "VOLTAR", Color.rgb(0, 63, 213), unit);
    }

    void drawPauseOptionsControlsButton(Canvas canvas, float width, float height, float unit) {
        drawSelectionActionButton(canvas, 0, 0, width, height,
                "CONTROLES", Color.rgb(250, 159, 13), unit);
    }

    private boolean isBackButton(float x, float y) {
        if (screenState == SCREEN_GAME_MENU && isAdventureCampaignDeleteConfirmation()) return false;
        if (getWidth() <= 0 || getHeight() <= 0) {
            return false;
        }
        if (screenState == SCREEN_CHARACTER_SELECT) {
            return isActionButtonAt(x, y, getWidth() * 0.03f, getHeight() * 0.80f,
                    getWidth() * 0.21f, getHeight() * 0.92f,
                    getWidth() * 0.01f, getHeight() * 0.02f);
        }
        if (screenState == SCREEN_TRACK_SELECT) {
            return isActionButtonAt(x, y, getWidth() * 0.595f, getHeight() * 0.82f,
                    getWidth() * 0.775f, getHeight() * 0.95f,
                    getWidth() * 0.01f, getHeight() * 0.02f);
        }
        if (screenState == SCREEN_CUP_SELECT) {
            return isTopMenuBackButtonAt(x, y);
        }
        if (screenState == SCREEN_ADVENTURE_GARAGE) {
            return isActionButtonAt(x, y, getWidth() * 0.035f, getHeight() * 0.90f,
                    getWidth() * 0.18f, getHeight() * 0.99f,
                    getWidth() * 0.01f, getHeight() * 0.01f);
        }
        if (screenState == SCREEN_PAUSE_MENU && adventurePauseProgressOpen) {
            return isTopMenuBackButtonAt(x, y);
        }
        if (screenState == SCREEN_GAME_MENU || screenState == SCREEN_PAUSE_MENU
                || screenState == SCREEN_PAUSE_SUBMENU || screenState == SCREEN_PAUSE_QUIT_CONFIRM
                || screenState == SCREEN_GHOST_SELECT) {
            return isTopMenuBackButtonAt(x, y);
        }
        float unit = Math.min(getWidth(), getHeight());
        float dx = x - getWidth() * 0.84f;
        float dy = y - getHeight() * 0.09f;
        float radius = unit * 0.065f;
        return dx * dx + dy * dy <= radius * radius;
    }

    private void drawRootMenu(Canvas canvas) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) {
            return;
        }

        float unit = Math.min(w, h);
        float rowLeft = w * 0.625f;
        float rowRight = w * 0.965f;
        float rowHeight = h * 0.105f;
        float rowGap = h * 0.015f;
        float firstTop = h * 0.205f;
        float rowTextSize = Math.min(unit * 0.064f, (rowRight - rowLeft) * 0.11f);
        drawRootMenuRow(canvas, rowLeft, rowRight, firstTop, rowHeight,
                "MODO AVENTURA", 0, rowTextSize, unit);
        drawRootMenuRow(canvas, rowLeft, rowRight, firstTop + rowHeight + rowGap,
                rowHeight, "ARCADE LOCAL", 1, rowTextSize, unit);
        drawRootMenuRow(canvas, rowLeft, rowRight, firstTop + 2f * (rowHeight + rowGap),
                rowHeight, "OPÇÕES", 2, rowTextSize, unit);
        drawRootMenuRow(canvas, rowLeft, rowRight, firstTop + 3f * (rowHeight + rowGap),
                rowHeight, "SAIR", 3, rowTextSize, unit);

        String prompt = "TOQUE PARA SELECIONAR";
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootTextPaint.setColor(Color.WHITE);
        rootTextPaint.setTextSize(unit * 0.034f);
        float promptX = w * 0.84f;
        float baseline = h * 0.92f;
        float paddingX = unit * 0.025f;
        float paddingY = unit * 0.012f;
        float halfText = rootTextPaint.measureText(prompt) * 0.5f;
        float left = promptX - halfText - paddingX;
        float right = promptX + halfText + paddingX;
        float top = baseline + rootTextPaint.ascent() - paddingY;
        float bottom = baseline + rootTextPaint.descent() + paddingY;
        float shadow = unit * 0.006f;
        rootPanelPaint.setColor(Color.rgb(160, 160, 160));
        canvas.drawRect(left - shadow, top + shadow, right - shadow, bottom + shadow, rootPanelPaint);
        rootPanelPaint.setColor(Color.rgb(7, 26, 137));
        canvas.drawRect(left, top, right, bottom, rootPanelPaint);
        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.004f));
        rootBorderPaint.setColor(Color.WHITE);
        canvas.drawRect(left, top, right, bottom, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);
        canvas.drawText(prompt, promptX, baseline, rootTextPaint);
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
    }

    private void drawRootLogo(Canvas canvas) {
        if (rootLogo == null || getWidth() <= 0 || getHeight() <= 0) {
            return;
        }

        float maxWidth = getWidth() * 0.50f;
        float maxHeight = getHeight() * 0.66f;
        float scale = Math.min(maxWidth / rootLogo.getWidth(), maxHeight / rootLogo.getHeight());
        float width = rootLogo.getWidth() * scale;
        float height = rootLogo.getHeight() * scale;
        float left = getWidth() * 0.28f - width * 0.5f;
        float top = getHeight() * 0.52f - height * 0.5f;
        canvas.drawBitmap(rootLogo, null,
                new android.graphics.RectF(left, top, left + width, top + height), imagePaint);
    }

    private void drawRootMenuRow(Canvas canvas, float left, float right, float top,
                                 float height, String label, int choice,
                                 float textSize, float unit) {
        String displayLabel = label.replace("(", "").replace(")", "");
        boolean selected = rootMenuSelection == choice;
        float skew = unit * 0.025f;
        Path shape = new Path();
        shape.moveTo(left + skew, top);
        shape.lineTo(right, top);
        shape.lineTo(right - skew * 0.55f, top + height);
        shape.lineTo(left, top + height);
        shape.close();

        if (selected) {
            canvas.save();
            canvas.translate(unit * 0.012f, unit * 0.014f);
            rootSelectedPaint.setShader(null);
            rootSelectedPaint.setColor(Color.argb(145, 0, 0, 0));
            canvas.drawPath(shape, rootSelectedPaint);
            canvas.restore();
            rootSelectedPaint.setShader(null);
            rootSelectedPaint.setColor(Color.rgb(250, 158, 13));
            canvas.drawPath(shape, rootSelectedPaint);
        } else {
            rootSelectedPaint.setShader(null);
            rootSelectedPaint.setColor(Color.argb(178, 18, 20, 24));
            canvas.drawPath(shape, rootSelectedPaint);
        }

        rootBorderPaint.setStyle(Paint.Style.STROKE);
        rootBorderPaint.setColor(Color.WHITE);
        rootBorderPaint.setStrokeWidth(Math.max(2f, unit * 0.0045f));
        canvas.drawPath(shape, rootBorderPaint);
        rootBorderPaint.setStyle(Paint.Style.FILL);

        rootTextPaint.setColor(Color.WHITE);
        rootTextPaint.setTextAlign(Paint.Align.CENTER);
        rootTextPaint.setTypeface(rootMenuTypeface);
        rootTextPaint.setTextSize(textSize);
        float fittedTextSize = Math.min(textSize,
                textSize * ((right - left) * 0.88f) / Math.max(1f, rootTextPaint.measureText(displayLabel)));
        rootTextPaint.setTextSize(fittedTextSize);
        rootTextStrokePaint.setTextSize(fittedTextSize);
        rootTextStrokePaint.setStrokeWidth(unit * (selected ? 0.005f : 0.004f));
        rootTextStrokePaint.setColor(Color.argb(210, 0, 0, 0));
        rootTextStrokePaint.setTextAlign(Paint.Align.CENTER);
        rootTextStrokePaint.setTypeface(rootMenuTypeface);
        float baseline = top + (height - rootTextPaint.ascent() - rootTextPaint.descent()) * 0.5f;
        canvas.drawText(displayLabel, (left + right) * 0.5f, baseline, rootTextStrokePaint);
        canvas.drawText(displayLabel, (left + right) * 0.5f, baseline, rootTextPaint);
    }
    private int rootMenuChoiceAt(float x, float y) {
        float leftEdge = getWidth() * 0.60f;
        float rightEdge = getWidth() * 0.99f;
        float firstTop = getHeight() * 0.205f;
        float rowHeight = getHeight() * 0.105f;
        float rowGap = getHeight() * 0.015f;
        if (x < leftEdge || x > rightEdge) {
            return -1;
        }
        for (int choice = 0; choice <= 3; choice++) {
            float top = firstTop + choice * (rowHeight + rowGap);
            if (y >= top && y <= top + rowHeight) return choice;
        }
        return -1;
    }

    /** The Android root has extra options absent from the native menu. */
    public void handleRootGamepadAction(int action) {
        if (screenState != SCREEN_ROOT_MENU || !snapshotInputReady) return;
        if (action == 12) rootMenuSelection = (rootMenuSelection + 3) % 4;
        if (action == 13) rootMenuSelection = (rootMenuSelection + 1) % 4;
        if (action == 0) {
            float y = getHeight() * (0.205f + rootMenuSelection * 0.12f + 0.0525f);
            claimRootMenuChoice(-1, getWidth() * .8f, y);
            sendInput();
        }
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!snapshotInputReady) return true;
        if (!controlsVisible && (screenState == SCREEN_RACE || screenState == SCREEN_ADVENTURE_ARENA))
            return false;
        int action = event.getActionMasked();
        if (screenState == SCREEN_PAUSE_RESULTS) {
            if (action == MotionEvent.ACTION_DOWN) {
                if (isRaceResultsContinueAt(event.getX(), event.getY())) {
                    queueTouchTapPulse(BTN_CROSS);
                } else {
                    queueMenuTap(event.getX(), event.getY());
                }
            } else if (action == MotionEvent.ACTION_CANCEL) {
                releaseAllTouches();
                return true;
            }
            sendInput();
            invalidate();
            return true;
        }
        if (screenState == SCREEN_MASK_HINT) {
            if (action == MotionEvent.ACTION_DOWN) {
                queueTouchTapPulse(BTN_TRIANGLE);
            }
            sendInput();
            invalidate();
            return true;
        }
        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            int index = event.getActionIndex();
            float x = event.getX(index);
            float y = event.getY(index);
            if (screenState == SCREEN_PAUSE_MENU && adventurePauseProgressOpen) {
                if (isBackButton(x, y)) {
                    adventurePauseProgressOpen = false;
                    invalidate();
                } else if (isAdventurePausePageArrowAt(x, y, false)) {
                    queueTouchTapPulse(BTN_LEFT);
                } else if (isAdventurePausePageArrowAt(x, y, true)) {
                    queueTouchTapPulse(BTN_RIGHT);
                }
                break;
            } else if (screenState == SCREEN_DEMO) {
                queueDemoSkip();
            } else if (screenState == SCREEN_CUTSCENE) {
                queueTouchTapPulse(BTN_START);
            }
            if (screenState == SCREEN_DEMO || screenState == SCREEN_CUTSCENE) {
                // Any touch skips the attract sequence or cinematic.
            } else if (screenState == SCREEN_RACE && isRaceItemTarget(x, y)) {
                queueTouchTapPulse(BTN_CIRCLE);
            } else if (screenState == SCREEN_ROOT_MENU) {
                claimRootMenuChoice(event.getPointerId(index), x, y);
            } else if (screenState == SCREEN_PAUSE_SUBMENU && !adventureHintTitle.isEmpty()) {
                if (isBackButton(x, y)) {
                    queueTouchTapPulse(BTN_TRIANGLE);
                } else if (!adventureHintDetails) {
                    adventureHintPointer = event.getPointerId(index);
                    adventureHintTouchStartY = y;
                    adventureHintSwipe = false;
                    if (adventureHintChoiceAt(x, y) < 0) {
                        adventureHintPointer = -1;
                    }
                }
            } else if (screenState == SCREEN_GAME_MENU || screenState == SCREEN_RACE_RESULTS_MENU
                    || screenState == SCREEN_PAUSE_SUBMENU || screenState == SCREEN_GHOST_SELECT
                    || screenState == SCREEN_PAUSE_MENU
                    || screenState == SCREEN_PAUSE_QUIT_CONFIRM) {
                int pointerId = event.getPointerId(index);
                if (screenState == SCREEN_GAME_MENU && isAdventureCampaignManagement()) {
                    claimGameMenuChoice(pointerId, adventureCampaignChoiceAt(x, y));
                } else if (isBackButton(x, y)) {
                    if (screenState == SCREEN_PAUSE_MENU) {
                        claimGameMenuChoice(pointerId, 0);
                    } else {
                        queueTouchTapPulse(BTN_TRIANGLE);
                        backPointer = pointerId;
                    }
                } else {
                    claimGameMenuChoice(pointerId, gameMenuChoiceAt(x, y));
                }
            } else if (screenState == SCREEN_CHARACTER_SELECT) {
                queueCharacterSelectTap(event.getPointerId(index), x, y);
            } else if (screenState == SCREEN_TRACK_SELECT) {
                queueTrackSelectTap(event.getPointerId(index), x, y);
            } else if (screenState == SCREEN_CUP_SELECT) {
                queueCupSelectTap(event.getPointerId(index), x, y);
            } else if (screenState == SCREEN_ADVENTURE_GARAGE) {
                queueAdventureGarageTap(x, y);
            } else {
                boolean claimed = claim(event.getPointerId(index), x, y);
                if (!claimed && screenState != SCREEN_RACE
                        && screenState != SCREEN_ADVENTURE_ARENA
                        && screenState != SCREEN_WAITING && screenState != SCREEN_DEMO) {
                    queueMenuTap(x, y);
                }
            }
            break;
        }
        case MotionEvent.ACTION_MOVE:
            if (adventureHintPointer >= 0) {
                int index = event.findPointerIndex(adventureHintPointer);
                if (index >= 0 && Math.abs(event.getY(index) - adventureHintTouchStartY)
                        >= Math.max(getHeight() * 0.04f, 18f)) {
                    adventureHintSwipe = true;
                }
            }
            for (int i = 0; i < event.getPointerCount(); i++) {
                int id = event.getPointerId(i);
                if (!isClaimed(id)) {
                    claim(id, event.getX(i), event.getY(i));
                }
            }
            break;
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
            int pointerId = event.getPointerId(event.getActionIndex());
            if (pointerId == adventureHintPointer) {
                finishAdventureHintTouch(event.getX(event.getActionIndex()),
                        event.getY(event.getActionIndex()));
            }
            release(pointerId);
            break;
        case MotionEvent.ACTION_CANCEL:
            releaseAllTouches();
            adventureHintPointer = -1;
            adventureHintSwipe = false;
            return true;
        default:
            break;
        }

        sendInput();
        invalidate();
        return true;
    }

    private boolean claim(int pointerId, float x, float y) {
        if (controlsVisible && (screenState == SCREEN_RACE || screenState == SCREEN_ADVENTURE_ARENA)) {
            for (Control control : controls) {
                if (control.pointerId < 0 && control.contains(x, y)) {
                    advancePointers.remove(Integer.valueOf(pointerId));
                    control.pointerId = pointerId;
                    if (control.button == BTN_START || control.button == BTN_CIRCLE) {
                        queueTouchTapPulse(control.button);
                    }
                    return true;
                }
            }
        }

        return false;
    }

    private void claimRootMenuChoice(int pointerId, float x, float y) {
        int choice = rootMenuChoiceAt(x, y);
        if (choice < 0) {
            return;
        }
        rootMenuSelection = choice;
        if (choice == 3 && getContext() instanceof CTRNativeActivity) {
            releaseAllTouches();
            ((CTRNativeActivity) getContext()).finishAndRemoveTask();
            return;
        }
        if (choice == 2 && getContext() instanceof CTRNativeActivity) {
            ((CTRNativeActivity) getContext()).showRootMenuOptions();
            invalidate();
            return;
        }
        try {
            CTRNativeActivity.nativeTouchRootMenuChoice(choice, snapshotGeneration);
            queueTouchTapPulse(BTN_CROSS);
            if (!advancePointers.contains(pointerId)) {
                advancePointers.add(pointerId);
            }
            invalidate();
        } catch (UnsatisfiedLinkError error) {
            // Native may still be starting while the overlay is already visible.
        }
    }

    private void claimGameMenuChoice(int pointerId, int choice) {
        if (choice < 0) {
            return;
        }
        if (screenState == SCREEN_GHOST_SELECT) {
            try {
                CTRNativeActivity.nativeTouchMenuChoice(choice, snapshotGeneration);
            } catch (UnsatisfiedLinkError error) {
                // Native may still be starting while the overlay is already visible.
            }
            return;
        }
        if (screenState == SCREEN_PAUSE_MENU && "PROGRESSO".equals(gameMenuRows[choice])) {
            gameMenuSelection = choice;
            adventurePauseProgressOpen = true;
            invalidate();
            return;
        }
        try {
            gameMenuSelection = choice;
            int nativeChoice = gameMenuNativeChoices[choice];
            if (nativeChoice < 0) return;
            CTRNativeActivity.nativeTouchMenuChoice(nativeChoice, snapshotGeneration);
            queueTouchTapPulse(BTN_CROSS);
            if (!advancePointers.contains(pointerId)) {
                advancePointers.add(pointerId);
            }
            invalidate();
        } catch (UnsatisfiedLinkError error) {
            // Native may still be starting while the overlay is already visible.
        }
    }

    private void queueCharacterSelectTap(int pointerId, float x, float y) {
        float width = getWidth();
        float height = getHeight();
        if (width <= 0 || height <= 0) {
            return;
        }
        if (isBackButton(x, y)) {
            queueTouchTapPulse(BTN_TRIANGLE);
            backPointer = pointerId;
            return;
        }
        if (isConfirmButton(x, y)) {
            queueTouchTapPulse(BTN_CROSS);
            return;
        }
        // Native portrait hitboxes only change the selected character/preview.
        // Advancement is exclusively the explicit CONTINUAR button above.
        queueMenuTap(x, y);
    }

    private void queueTrackSelectTap(int pointerId, float x, float y) {
        if (isBackButton(x, y)) {
            queueTouchTapPulse(BTN_TRIANGLE);
            backPointer = pointerId;
            return;
        }
        if (isConfirmButton(x, y)) {
            queueTouchTapPulse(BTN_CROSS);
            return;
        }
        // Preserve the native track list and use its original hit regions.
        queueMenuTap(x, y);
    }

    private void queueCupSelectTap(int pointerId, float x, float y) {
        if (isBackButton(x, y)) {
            queueTouchTapPulse(BTN_TRIANGLE);
            backPointer = pointerId;
            return;
        }
        int choice = cupCardChoiceAt(x, y);
        if (choice < 0) return;
        try {
            CTRNativeActivity.nativeTouchMenuChoice(choice, snapshotGeneration);
            // A cup card is a complete touch target: select it and confirm.
            queueTouchTapPulse(BTN_CROSS);
        } catch (UnsatisfiedLinkError error) {
            // Native may still be starting while the overlay is already visible.
        }
    }

    private void queueAdventureGarageTap(float x, float y) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0) return;
        if (isBackButton(x, y)) {
            queueTouchTapPulse(BTN_TRIANGLE);
        } else if (x >= w * 0.82f && x <= w * 0.965f
                && y >= h * 0.89f && y <= h * 0.99f) {
            queueTouchTapPulse(BTN_CROSS);
        } else if (y >= h * 0.86f && x <= w * 0.34f) {
            queueTouchTapPulse(BTN_LEFT);
        } else if (y >= h * 0.86f && x >= w * 0.66f) {
            queueTouchTapPulse(BTN_RIGHT);
        }
    }

    private boolean isConfirmButton(float x, float y) {
        if (screenState == SCREEN_CHARACTER_SELECT) {
            return isActionButtonAt(x, y, getWidth() * 0.79f, getHeight() * 0.80f,
                    getWidth() * 0.97f, getHeight() * 0.92f,
                    getWidth() * 0.01f, getHeight() * 0.02f);
        }
        return isActionButtonAt(x, y, getWidth() * 0.79f, getHeight() * 0.82f,
                getWidth() * 0.97f, getHeight() * 0.95f,
                getWidth() * 0.01f, getHeight() * 0.02f);
    }

    private boolean isRaceItemTarget(float x, float y) {
        return x >= getWidth() * 0.37f && x <= getWidth() * 0.49f
                && y >= getHeight() * 0.015f && y <= getHeight() * 0.20f;
    }

    private void queueTouchTapPulse(int button) {
        touchTapPulseButton = button;
        touchTapPulseUntil = SystemClock.uptimeMillis() + 140;
        removeCallbacks(finishTouchTapPulse);
        postDelayed(finishTouchTapPulse, 140);
    }

    private int gameMenuChoiceAt(float x, float y) {
        if (screenState == SCREEN_GAME_MENU && isAdventureCampaignDeleteConfirmation()) {
            float w = getWidth(), h = getHeight();
            if (y < h * 0.52f || y > h * 0.62f) return -1;
            if (x >= w * 0.33f && x <= w * 0.49f) return 0;
            if (x >= w * 0.51f && x <= w * 0.67f) return 1;
            return -1;
        }
        if (screenState == SCREEN_PAUSE_QUIT_CONFIRM) {
            return quitConfirmationChoiceAt(x, y);
        }
        if (screenState == SCREEN_PAUSE_MENU) {
            return pauseMenuChoiceAt(x, y);
        }
        if (screenState == SCREEN_RACE_RESULTS_MENU) {
            if (gameMenuRows.length == 0 || x < getWidth() * 0.20f || x > getWidth() * 0.80f) return -1;
            float h = getHeight(), gap = h * 0.018f;
            float rowHeight = Math.min(h * 0.095f, (h * 0.43f - gap * (gameMenuRows.length - 1)) / gameMenuRows.length);
            float blockHeight = gameMenuRows.length * rowHeight + (gameMenuRows.length - 1) * gap;
            float top = h * 0.28f + (h * 0.43f - blockHeight) * 0.5f;
            return menuRowChoiceAt(y, h, gameMenuRows.length, top, rowHeight, gap);
        }
        if (screenState == SCREEN_GAME_MENU) {
            float w = getWidth(), h = getHeight();
            if (gameMenuRows.length == 0 || x < w * 0.59f || x > w * 0.97f) return -1;
            float gap = h * 0.018f;
            float rowHeight = Math.min(h * 0.13f,
                    (h * 0.60f - gap * (gameMenuRows.length - 1)) / gameMenuRows.length);
            float blockHeight = gameMenuRows.length * rowHeight + (gameMenuRows.length - 1) * gap;
            float top = (h - blockHeight) * 0.5f;
            return menuRowChoiceAt(y, h, gameMenuRows.length, top, rowHeight, gap);
        }
        if (gameMenuRows.length == 0 || x < getWidth() * 0.12f || x > getWidth() * 0.88f) {
            return -1;
        }
        float h = getHeight();
        return menuRowChoiceAt(y, h, gameMenuRows.length,
                responsiveMenuRowsTop(h, gameMenuRows.length),
                responsiveMenuRowHeight(h, gameMenuRows.length), h * 0.018f);
    }

    private int adventureHintChoiceAt(float x, float y) {
        float w = getWidth(), h = getHeight();
        int count = adventureHintRows.length;
        if (count == 0 || x < w * 0.10f || x > w * 0.90f) return -1;
        return menuRowChoiceAt(y, h, count,
                responsiveMenuRowsTop(h, count), responsiveMenuRowHeight(h, count), h * 0.018f);
    }

    private int cupCardChoiceAt(float x, float y) {
        float w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return -1;
        int choice = 0;
        for (SelectionTextLabel label : selectionTextLabels) {
            if (label.title) continue;
            // These offsets map the animated native label back to its 180x78 card in 512x216 space.
            float left = label.x * w - (90f / 512f) * w;
            float top = label.y * h - (31f / 216f) * h;
            if (x >= left && x <= left + (180f / 512f) * w
                    && y >= top && y <= top + (78f / 216f) * h) {
                return choice;
            }
            choice++;
        }
        return -1;
    }

    private int menuRowChoiceAt(float y, float h, int count, float top,
            float rowHeight, float gap) {
        if (h <= 0 || count <= 0) return -1;
        float stride = rowHeight + gap;
        int row = (int)Math.floor((y - top + gap * 0.5f) / stride);
        float hitTop = top + row * stride - gap * 0.5f;
        return row >= 0 && row < count && y >= hitTop && y <= hitTop + stride ? row : -1;
    }

    private void finishAdventureHintTouch(float x, float y) {
        float deltaY = y - adventureHintTouchStartY;
        if (adventureHintSwipe) {
            queueTouchTapPulse(deltaY < 0 ? BTN_DOWN : BTN_UP);
        } else {
            int row = adventureHintChoiceAt(x, y);
            if (row >= 0 && row < adventureHintRowIndexes.length) {
                try {
                    CTRNativeActivity.nativeTouchMenuChoice(adventureHintRowIndexes[row], snapshotGeneration);
                } catch (UnsatisfiedLinkError error) {
                    // Native may still be starting while the overlay is already visible.
                }
            }
        }
        adventureHintPointer = -1;
        adventureHintSwipe = false;
    }

    private int adventureCampaignChoiceAt(float x, float y) {
        float w = getWidth();
        float h = getHeight();
        if (x >= w * 0.73f && x <= w * 0.89f && y >= h * 0.065f && y <= h * 0.16f) {
            return 1;
        }
        float left = w * 0.09f;
        float right = w * 0.90f;
        float top = h * 0.205f;
        float bottom = h * 0.82f;
        float buttonTop = bottom - (bottom - top) * 0.22f;
        float buttonBottom = bottom - (bottom - top) * 0.075f;
        float buttonWidth = (right - left) * 0.255f;
        float buttonLeft = (left + right - buttonWidth) * 0.5f;
        if (y < buttonTop || y > buttonBottom) return -1;
        if (x >= buttonLeft && x <= buttonLeft + buttonWidth) return 0;
        return -1;
    }

    private int quitConfirmationChoiceAt(float x, float y) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || gameMenuRows.length < 2) return -1;
        float unit = Math.min(w, h);
        float buttonWidth = Math.min(w * 0.15f, unit * 0.34f);
        float gap = unit * 0.025f;
        float left = (w - buttonWidth * 2f - gap) * 0.5f;
        float top = h * 0.445f;
        float buttonHeight = unit * 0.095f;
        if (y < top || y > top + buttonHeight) return -1;
        if (x >= left && x <= left + buttonWidth) return 1;
        if (x >= left + buttonWidth + gap && x <= left + buttonWidth * 2f + gap) return 0;
        return -1;
    }

    private int pauseMenuChoiceAt(float x, float y) {
        float w = getWidth();
        float h = getHeight();
        if (w <= 0 || h <= 0 || gameMenuRows.length == 0
                || x < w * 0.22f || x > w * 0.78f) {
            return -1;
        }
        return menuRowChoiceAt(y, h, gameMenuRows.length,
                pauseMenuRowsTop(h, gameMenuRows.length),
                pauseMenuRowHeight(h, gameMenuRows.length), pauseMenuRowGap(h));
    }

    private float pauseMenuRowsTop(float h, int count) {
        float gap = pauseMenuRowGap(h);
        float available = h * 0.52f;
        float rowHeight = pauseMenuRowHeight(h, count);
        float blockHeight = count * rowHeight + (count - 1) * gap;
        return h * (0.075f + 0.17f) + (available - blockHeight) * 0.5f;
    }

    private float pauseMenuRowHeight(float h, int count) {
        float gap = pauseMenuRowGap(h);
        float available = h * 0.52f;
        return count > 0 ? Math.min(h * 0.13f,
                (available - gap * (count - 1)) / count) : 0f;
    }

    private float pauseMenuRowGap(float h) {
        return h * 0.014f;
    }

    private void queueDemoSkip() {
        demoTapPulseUntil = SystemClock.uptimeMillis() + 120;
        removeCallbacks(finishDemoTapPulse);
        postDelayed(finishDemoTapPulse, 120);
    }

    private void queueMenuTap(float x, float y) {
        if (getWidth() <= 0 || getHeight() <= 0) {
            return;
        }
        try {
            CTRNativeActivity.nativeTouchMenuTap(x / getWidth(), y / getHeight(), snapshotGeneration);
        } catch (UnsatisfiedLinkError error) {
            // Native may still be starting while the overlay is already visible.
        }
    }

    private boolean isClaimed(int pointerId) {
        if (backPointer == pointerId || adventureHintPointer == pointerId
                || advancePointers.contains(pointerId)) {
            return true;
        }
        for (Control control : controls) {
            if (control.pointerId == pointerId) {
                return true;
            }
        }
        return false;
    }

    private void release(int pointerId) {
        for (Control control : controls) {
            if (control.pointerId == pointerId) {
                control.pointerId = -1;
            }
        }
        advancePointers.remove(Integer.valueOf(pointerId));
        if (backPointer == pointerId) {
            backPointer = -1;
        }
    }

    private void sendInput() {
        int mask = ALL_RELEASED;
        int stickY = 0;
        for (Control control : controls) {
            if (control.pointerId >= 0) {
                mask &= ~control.button;
                if (control.button == BTN_SQUARE) {
                    stickY = 32767;
                }
            }
        }
        if (!advancePointers.isEmpty() || SystemClock.uptimeMillis() < demoTapPulseUntil) {
            mask &= ~BTN_CROSS;
        }
        if (touchTapPulseButton != 0 && SystemClock.uptimeMillis() < touchTapPulseUntil) {
            mask &= ~touchTapPulseButton;
        }
        if (backPointer >= 0) {
            mask &= ~BTN_TRIANGLE;
        }

        if (sending && mask == lastMask) {
            return;
        }
        lastMask = mask;
        try {
            CTRNativeActivity.nativeTouchInput(mask, 0, stickY, snapshotGeneration);
            sending = true;
        } catch (UnsatisfiedLinkError error) {
            // Native may not be loaded until the first visible touch.
            sending = false;
        }
    }
}
