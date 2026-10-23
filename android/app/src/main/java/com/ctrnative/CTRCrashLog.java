package com.ctrnative;

import android.content.Context;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Writes an uncaught exception somewhere readable before the process dies.
 *
 * A force close on a device that will not authorise adb leaves nothing to go on
 * but guesswork, and guessing costs a build and a round trip each time. The
 * stack trace goes next to the disc image, where a file manager can reach it.
 */
final class CTRCrashLog {

    private CTRCrashLog() {
    }

    static void install(final Context context) {
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();

        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            write(context, thread, error);

            // Still let the platform do what it would have done, so the crash is
            // reported normally rather than swallowed into a hang.
            if (previous != null) {
                previous.uncaughtException(thread, error);
            } else {
                System.exit(2);
            }
        });
    }

    private static void write(Context context, Thread thread, Throwable error) {
        try {
            File root = context.getExternalFilesDir(null);
            if (root == null) {
                return;
            }

            StringWriter trace = new StringWriter();
            try (PrintWriter printer = new PrintWriter(trace)) {
                error.printStackTrace(printer);
            }

            String when = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date());

            // Appended rather than replaced: the interesting crash is sometimes
            // the one before the one you are looking at.
            try (FileWriter writer = new FileWriter(new File(root, "crash.txt"), true)) {
                writer.write("---- " + when + " on thread " + thread.getName() + " ----\n");
                writer.write("android " + android.os.Build.VERSION.SDK_INT + ", " + android.os.Build.MODEL
                        + ", " + android.os.Build.SUPPORTED_ABIS[0] + "\n");
                writer.write(trace.toString());
                writer.write("\n");
            }
        } catch (IOException | RuntimeException ignored) {
            // Whatever happens here, the original crash matters more.
        }
    }
}
