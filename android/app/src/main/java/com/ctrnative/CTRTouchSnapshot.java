package com.ctrnative;

/** One immutable presentation from the engine, read with a single JNI call. */
final class CTRTouchSnapshot {
    final int screen;
    final boolean inputReady;
    final long generation;
    final long frame;
    final long producedNs;
    final String menu, campaign, pause, character, selection, results, name;

    private CTRTouchSnapshot(String[] fields, String[] header) {
        screen = Integer.parseInt(header[0]);
        generation = Long.parseLong(header[1]);
        frame = Long.parseLong(header[2]);
        producedNs = Long.parseLong(header[3]);
        inputReady = "1".equals(header[4]);
        menu = fields[1];
        campaign = fields[2];
        pause = fields[3];
        character = fields[4];
        selection = fields[5];
        results = fields[6];
        name = fields[7];
    }

    static CTRTouchSnapshot parse(String[] fields) {
        if (fields == null || fields.length != 8) return null;
        for (String field : fields) if (field == null) return null;
        String[] header = fields[0].split("\\|", -1);
        if (header.length != 5 || !("0".equals(header[4]) || "1".equals(header[4]))) return null;
        try {
            CTRTouchSnapshot snapshot = new CTRTouchSnapshot(fields, header);
            return snapshot.generation > 0 && snapshot.screen >= 0 && snapshot.screen <= 22
                    ? snapshot : null;
        } catch (NumberFormatException ignored) {
            return null;
        }
    }

    boolean hasSameContent(CTRTouchSnapshot other) {
        return other != null && generation == other.generation && screen == other.screen && inputReady == other.inputReady
                && menu.equals(other.menu) && campaign.equals(other.campaign) && pause.equals(other.pause)
                && character.equals(other.character) && selection.equals(other.selection)
                && results.equals(other.results) && name.equals(other.name);
    }
}
