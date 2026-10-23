package com.ctrnative;

import android.content.Context;
import android.content.SharedPreferences;

/** Preferences for Android app behavior, separate from game-owned ctrds.cfg. */
final class CTRAppPreferences {
    static final String PREFS_NAME = "CTRNativePrefs";
    private static final String KEY_QUICK_START = CTRDSSettings.QUICK_START.key;

    private CTRAppPreferences() {}

    static boolean isQuickStartEnabled(Context context) {
        return preferences(context).getBoolean(KEY_QUICK_START, false);
    }

    static String cycleQuickStart(Context context, int direction) {
        boolean enabled = isQuickStartEnabled(context);
        int current = enabled ? 1 : 0;
        int nextIndex = (current + (direction > 0 ? 1 : -1) + 2) % 2;
        boolean next = nextIndex == 1;
        preferences(context).edit().putBoolean(KEY_QUICK_START, next).apply();
        return CTRDSSettings.QUICK_START.names[nextIndex];
    }

    static String quickStartName(Context context) {
        return CTRDSSettings.QUICK_START.names[isQuickStartEnabled(context) ? 1 : 0];
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }
}
