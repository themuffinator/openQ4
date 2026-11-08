// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import java.util.UUID;

/**
 * One atomic interface preference. Profile 0 is Automatic (Phone or Tablet from
 * the smallest screen width); a 0 percentage follows the profile default.
 * An applied transaction has a new identity; returning to the game is not a new Apply.
 * Orientation stays with the Activity: the engine record carries only the sizes.
 */
final class InterfaceSettings {
    static final String SAVED = "interface_settings", DRAFT = "interface_settings_draft";
    /** Landscape either way up, as the manifest selects; or follow the device into portrait. */
    static final int ORIENTATION_LANDSCAPE = 0, ORIENTATION_AUTOMATIC = 1;
    static final InterfaceSettings DEFAULT = new InterfaceSettings("00000000000000000000000000000000", 0, 0, 0);
    final String revision;
    final int profile, uiPercent, textPercent, orientation;

    InterfaceSettings(String revision, int profile, int uiPercent, int textPercent) {
        this(revision, profile, uiPercent, textPercent, ORIENTATION_LANDSCAPE);
    }

    InterfaceSettings(String revision, int profile, int uiPercent, int textPercent, int orientation) {
        if (revision == null || !revision.matches("[0-9a-f]{32}") || profile < 0 || profile > 5 ||
                !scaleValid(uiPercent, 75) || !scaleValid(textPercent, 100) ||
                (orientation != ORIENTATION_LANDSCAPE && orientation != ORIENTATION_AUTOMATIC))
            throw new IllegalArgumentException("Invalid interface settings");
        this.revision = revision; this.profile = profile; this.uiPercent = uiPercent; this.textPercent = textPercent;
        this.orientation = orientation;
    }

    private static boolean scaleValid(int value, int min) {
        return value == 0 || (value >= min && value <= 200 && value % 5 == 0);
    }
    InterfaceSettings select(int profile, int ui, int text) {
        return new InterfaceSettings(revision, profile, ui, text, orientation);
    }
    InterfaceSettings withOrientation(int value) {
        return new InterfaceSettings(revision, profile, uiPercent, textPercent, value);
    }
    InterfaceSettings applied() {
        return new InterfaceSettings(UUID.randomUUID().toString().replace("-", ""), profile, uiPercent, textPercent, orientation);
    }
    String encode() { return "2:" + revision + ":" + profile + ":" + uiPercent + ":" + textPercent + ":" + orientation; }
    /** Version 1 records predate the orientation choice and stay landscape. */
    static InterfaceSettings decodeOrNull(String encoded) {
        if (encoded == null || !encoded.matches("1:[0-9a-f]{32}:[0-5]:[0-9]{1,3}:[0-9]{1,3}|2:[0-9a-f]{32}:[0-5]:[0-9]{1,3}:[0-9]{1,3}:[01]"))
            return null;
        try {
            String[] fields = encoded.split(":");
            return new InterfaceSettings(fields[1], Integer.parseInt(fields[2]), Integer.parseInt(fields[3]),
                    Integer.parseInt(fields[4]), fields.length > 5 ? Integer.parseInt(fields[5]) : ORIENTATION_LANDSCAPE);
        } catch (IllegalArgumentException invalid) { return null; }
    }
    static InterfaceSettings decode(String encoded) {
        InterfaceSettings value = decodeOrNull(encoded);
        return value == null ? DEFAULT : value;
    }
    int resolvedProfile(boolean tablet) { return profile == 0 ? (tablet ? 5 : 4) : profile; }
    int resolvedUi(boolean tablet) {
        if (uiPercent != 0) return uiPercent;
        int selected = resolvedProfile(tablet);
        return selected == 2 ? 150 : selected == 3 ? 125 : 100;
    }
    int resolvedText(boolean tablet, float systemFontScale) {
        if (textPercent != 0) return textPercent;
        int selected = resolvedProfile(tablet);
        if (selected == 2) return 125;
        if (selected != 4 && selected != 5) return 100;
        float font = Float.isFinite(systemFontScale) ? systemFontScale : 1;
        return Math.max(100, Math.min(200, Math.round(font * 20) * 5));
    }
    String engineValues(boolean tablet, float systemFontScale) {
        return "1:" + revision + ":" + resolvedProfile(tablet) + ":" + resolvedUi(tablet) + ":" + resolvedText(tablet, systemFontScale);
    }
}
