package com.ctrnative;

import android.content.Context;
import android.graphics.Typeface;

/** One shared font for the Android views and Canvas overlays. */
final class CTRFont {
    private static Typeface font;

    private CTRFont() { }

    static synchronized Typeface get(Context context) {
        if (font == null) {
            font = Typeface.createFromAsset(context.getAssets(), "fonts/LuckiestGuy-Regular.ttf");
        }
        return font;
    }
}
