// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import java.util.Arrays;

/**
 * The built-in touch controls' placement and the layout editor's record.
 *
 * A port of src/sys/android/android_touch_controls.h (ComputePresetLayout,
 * PlaceButton, PlacePortraitDeck, ComputeLayout and ParseLayoutEdits) in the
 * same float arithmetic, so the editor previews exactly what the engine
 * draws. tools/tests/android_touch_layout.py runs both over the same matrix
 * and compares them. No Android types: the JVM tests compile it on its own.
 */
final class TouchLayout {
    /** The setup preference keys: the applied record and the editor's draft. */
    static final String SAVED = "touch_layout", DRAFT = "touch_layout_draft";
    static final int FIRE = 0, JUMP = 1, CROUCH = 2, WEAPON = 3, RELOAD = 4, ZOOM = 5, PAUSE = 6, SCORES = 7, CHAT = 8,
            COUNT = 9;
    static final int PRESET_DEFAULT = 0, PRESET_COMPACT = 1, PRESET_LEFT_HANDED = 2;
    static final int SIZE_DEFAULT = 100, SIZE_MIN = 75, SIZE_MAX = 150;
    static final int OPACITY_DEFAULT = 35, OPACITY_MIN = 10, OPACITY_MAX = 80, STEP = 5;
    static final int GRID_DP = 8, POSITION_LIMIT = 8192, RECORD_LIMIT = 512;
    static final float REST_OPACITY = .35f, PRESSED_OPACITY = .80f;
    static final float STICK_BASE_RADIUS_DP = 64, STICK_KNOB_RADIUS_DP = 28;

    private static final int TRAILING_BOTTOM = 0, LEADING_TOP = 1, TRAILING_TOP = 2;
    private static final int[] ANCHOR = {TRAILING_BOTTOM, TRAILING_BOTTOM, TRAILING_BOTTOM, TRAILING_BOTTOM,
        TRAILING_BOTTOM, TRAILING_BOTTOM, LEADING_TOP, TRAILING_TOP, TRAILING_TOP};
    private static final float[] FROM_SIDE = {128, 52, 52, 148, 224, 232, 40, 40, 120};
    private static final float[] FROM_END = {120, 196, 60, 236, 88, 160, 40, 40, 40};
    private static final float[] DIAMETER = {88, 64, 64, 64, 56, 56, 48, 48, 48};

    private TouchLayout() {}

    static boolean multiplayerOnly(int button) { return button == SCORES || button == CHAT; }

    /** One control's edits; positions are whole dp from the nearest edges (negative: right or bottom). */
    static final class Edit {
        static final Edit DEFAULT = new Edit(false, 0, 0, SIZE_DEFAULT, OPACITY_DEFAULT, false);
        final boolean moved, hidden;
        final int x, y, size, opacity;

        Edit(boolean moved, int x, int y, int size, int opacity, boolean hidden) {
            if (moved && (x == 0 || y == 0 || Math.abs(x) > POSITION_LIMIT || Math.abs(y) > POSITION_LIMIT))
                throw new IllegalArgumentException("Invalid control position");
            if (size < SIZE_MIN || size > SIZE_MAX || size % STEP != 0 || opacity < OPACITY_MIN ||
                    opacity > OPACITY_MAX || opacity % STEP != 0)
                throw new IllegalArgumentException("Invalid control size or opacity");
            this.moved = moved; this.x = moved ? x : 0; this.y = moved ? y : 0;
            this.size = size; this.opacity = opacity; this.hidden = hidden;
        }

        boolean isDefault() { return !moved && size == SIZE_DEFAULT && opacity == OPACITY_DEFAULT && !hidden; }
        Edit at(int x, int y) { return new Edit(true, x, y, size, opacity, hidden); }
        Edit unmoved() { return new Edit(false, 0, 0, size, opacity, hidden); }
        Edit sized(int value) { return new Edit(moved, x, y, value, opacity, hidden); }
        Edit faded(int value) { return new Edit(moved, x, y, size, value, hidden); }
        Edit hiding(boolean value) { return new Edit(moved, x, y, size, opacity, value); }

        @Override public boolean equals(Object other) {
            if (!(other instanceof Edit)) return false;
            Edit that = (Edit) other;
            return moved == that.moved && x == that.x && y == that.y && size == that.size &&
                   opacity == that.opacity && hidden == that.hidden;
        }
        @Override public int hashCode() { return Arrays.hashCode(new int[] {moved ? 1 : 0, x, y, size, opacity, hidden ? 1 : 0}); }
    }

    /** One orientation's edits, in control order. */
    static final class Edits {
        static final Edits DEFAULT = new Edits(defaults());
        private final Edit[] controls;

        Edits(Edit[] controls) {
            if (controls.length != COUNT) throw new IllegalArgumentException("Wrong control count");
            if (controls[PAUSE].hidden) throw new IllegalArgumentException("Pause cannot be hidden");
            this.controls = controls.clone();
        }

        private static Edit[] defaults() { Edit[] all = new Edit[COUNT]; Arrays.fill(all, Edit.DEFAULT); return all; }
        Edit get(int button) { return controls[button]; }
        Edits with(int button, Edit edit) { Edit[] next = controls.clone(); next[button] = edit; return new Edits(next); }
        boolean isDefault() { for (Edit edit : controls) if (!edit.isDefault()) return false; return true; }
        @Override public boolean equals(Object other) {
            return other instanceof Edits && Arrays.equals(controls, ((Edits) other).controls);
        }
        @Override public int hashCode() { return Arrays.hashCode(controls); }
    }

    /** Both orientations' edits, the engine's in_touchLayout record. */
    static final class Record {
        static final Record DEFAULT = new Record(Edits.DEFAULT, Edits.DEFAULT);
        final Edits landscape, portrait;

        Record(Edits landscape, Edits portrait) { this.landscape = landscape; this.portrait = portrait; }
        Edits get(boolean tall) { return tall ? portrait : landscape; }
        Record with(boolean tall, Edits edits) { return tall ? new Record(landscape, edits) : new Record(edits, portrait); }

        String encode() {
            StringBuilder out = new StringBuilder("1");
            for (Edits edits : new Edits[] {landscape, portrait}) {
                out.append('|');
                for (int i = 0; i < COUNT; i++) {
                    Edit edit = edits.get(i);
                    if (i > 0) out.append(';');
                    if (edit.moved) out.append(edit.x).append(',').append(edit.y);
                    else out.append("*,*");
                    out.append(',').append(edit.size).append(',').append(edit.opacity).append(',').append(edit.hidden ? 1 : 0);
                }
            }
            return out.toString();
        }

        /** Null for a missing, malformed or oversized record, as ParseLayoutEdits refuses it. */
        static Record decode(String record) {
            if (record == null || record.length() >= RECORD_LIMIT ||
                    !record.matches("1(\\|(\\*|-?[0-9]{1,4}),(\\*|-?[0-9]{1,4}),[0-9]{1,4},[0-9]{1,4},[0-9]{1,4}" +
                                    "(;(\\*|-?[0-9]{1,4}),(\\*|-?[0-9]{1,4}),[0-9]{1,4},[0-9]{1,4},[0-9]{1,4}){8}){2}"))
                return null;
            String[] orientations = record.split("\\|");
            try {
                Edits[] parsed = new Edits[2];
                for (int o = 0; o < 2; o++) {
                    String[] entries = orientations[o + 1].split(";");
                    Edit[] controls = new Edit[COUNT];
                    for (int i = 0; i < COUNT; i++) {
                        String[] f = entries[i].split(",");
                        boolean movedX = !f[0].equals("*"), movedY = !f[1].equals("*");
                        int hidden = Integer.parseInt(f[4]);
                        if (movedX != movedY || hidden > 1) return null;
                        controls[i] = new Edit(movedX, movedX ? Integer.parseInt(f[0]) : 0, movedY ? Integer.parseInt(f[1]) : 0,
                                               Integer.parseInt(f[2]), Integer.parseInt(f[3]), hidden == 1);
                    }
                    parsed[o] = new Edits(controls);
                }
                return new Record(parsed[0], parsed[1]);
            } catch (IllegalArgumentException invalid) { return null; }
        }
    }

    /** A computed layout in the units it was given (window pixels, or dp with a dp scale of 1). */
    static final class Result {
        float width, height, nativeDp = 1, dp = 1;
        boolean available, leftHanded, edited, editsRejected;
        int hidden, moved;
        final float[] x = new float[COUNT], y = new float[COUNT], radius = new float[COUNT], restOpacity = new float[COUNT];
        float stickHomeX, stickHomeY, stickBaseRadius, stickKnobRadius, stickTop, leadingEdge;

        Result copy() {
            Result r = new Result();
            r.width = width; r.height = height; r.nativeDp = nativeDp; r.dp = dp; r.available = available;
            r.leftHanded = leftHanded; r.edited = edited; r.editsRejected = editsRejected; r.hidden = hidden; r.moved = moved;
            System.arraycopy(x, 0, r.x, 0, COUNT); System.arraycopy(y, 0, r.y, 0, COUNT);
            System.arraycopy(radius, 0, r.radius, 0, COUNT); System.arraycopy(restOpacity, 0, r.restOpacity, 0, COUNT);
            r.stickHomeX = stickHomeX; r.stickHomeY = stickHomeY; r.stickBaseRadius = stickBaseRadius;
            r.stickKnobRadius = stickKnobRadius; r.stickTop = stickTop; r.leadingEdge = leadingEdge;
            return r;
        }

        float opacity(int button) { return restOpacity[button] > 0 ? restOpacity[button] : REST_OPACITY; }
    }

    private static float distanceSquared(float ax, float ay, float bx, float by) {
        final float dx = ax - bx, dy = ay - by;
        return dx * dx + dy * dy;
    }

    static float hitRadius(Result layout, int button) {
        return Math.max(layout.radius[button], 24.0f * layout.nativeDp);
    }

    private static boolean placeButton(Result layout, int button, int placed, boolean free) {
        final float radius = hitRadius(layout, button);
        final float gap = 8.0f * layout.nativeDp;
        final boolean top = !free && ANCHOR[button] != TRAILING_BOTTOM;
        final float edge = (top ? 8.0f : 24.0f) * layout.nativeDp;
        final float topEdge = (top || free ? 8.0f : 24.0f) * layout.nativeDp;
        final float left = edge + radius;
        final float right = layout.width - left;
        final float bottom = layout.height - edge - radius;
        final float yMin = topEdge + radius;
        final float yMax = top ? yMin : bottom;
        if (right < left || yMax < yMin) return false;
        final float desiredXRaw = layout.x[button], desiredYRaw = layout.y[button];
        final float desiredX = Math.max(left, Math.min(right, desiredXRaw));
        final float desiredY = Math.max(yMin, Math.min(yMax, desiredYRaw));
        if (fits(layout, button, placed, radius, gap, desiredX, desiredY)) {
            layout.x[button] = desiredX; layout.y[button] = desiredY;
            return true;
        }
        final float step = Math.max(4.0f * layout.nativeDp, Math.max(layout.width, layout.height) / 256.0f);
        float best = -1.0f;
        for (float y = yMin; y <= yMax; y += step) {
            for (float x = left; x <= right; x += step) {
                final float cost = distanceSquared(x, y, desiredXRaw, desiredYRaw);
                if ((best >= 0.0f && cost >= best) || !fits(layout, button, placed, radius, gap, x, y)) continue;
                best = cost;
                layout.x[button] = x; layout.y[button] = y;
            }
        }
        return best >= 0.0f;
    }

    private static boolean fits(Result layout, int button, int placed, float radius, float gap, float x, float y) {
        final float centerClearance = radius + 24.0f * layout.nativeDp;
        if (distanceSquared(x, y, layout.width * 0.5f, layout.height * 0.5f) < centerClearance * centerClearance) return false;
        for (int other = 0; other < COUNT; ++other) {
            if ((placed & (1 << other)) == 0) continue;
            final float separation = radius + hitRadius(layout, other) + gap;
            if (distanceSquared(x, y, layout.x[other], layout.y[other]) < separation * separation) return false;
        }
        return true;
    }

    static float deckTop(float width, float height) {
        return height > width ? height * 0.5f + width * 0.375f : 0.0f;
    }

    private static float deckRise(float separation, float across) {
        return across >= separation ? 0.0f : (float) Math.ceil((float) Math.sqrt(separation * separation - across * across));
    }

    private static float max(float a, float b, float c) { return Math.max(Math.max(a, b), c); }

    private static void placePortraitDeck(Result layout, float dp) {
        final float gap = 8.0f * layout.nativeDp + 1.0f, edge = 24.0f * layout.nativeDp;
        final float fireR = hitRadius(layout, FIRE), weaponR = hitRadius(layout, WEAPON);
        final float sideR = Math.max(hitRadius(layout, JUMP), hitRadius(layout, CROUCH));
        final float innerR = Math.max(hitRadius(layout, ZOOM), hitRadius(layout, RELOAD));
        final float center = layout.width * 0.25f;
        final float outside = Math.max(0.0f, Math.min(76.0f * dp, center - edge - sideR));
        final float inside = Math.max(0.0f, Math.min(96.0f * dp, center - innerR));
        final float low = Math.max(60.0f * dp, edge + Math.max(sideR, innerR));
        final float fireToSide = fireR + sideR + gap, fireToInner = fireR + innerR + gap;
        final float fire = max(128.0f * dp, low + deckRise(fireToSide, outside), low + deckRise(fireToInner, inside));
        final float jump = Math.max(fire + deckRise(fireToSide, outside), low + 2.0f * sideR + gap);
        final float zoom = Math.max(fire + deckRise(fireToInner, inside), low + 2.0f * innerR + gap);
        final float weapon = max(fire + fireR + weaponR + gap, jump + deckRise(sideR + weaponR + gap, outside),
                                 zoom + deckRise(innerR + weaponR + gap, inside));
        final int[] buttons = {FIRE, JUMP, CROUCH, WEAPON, RELOAD, ZOOM};
        final float[] fromSide = {center, center - outside, center - outside, center, center + inside, center + inside};
        final float[] fromBottom = {fire, jump, low, weapon, low, zoom};
        for (int i = 0; i < buttons.length; i++) {
            layout.x[buttons[i]] = layout.width - fromSide[i];
            layout.y[buttons[i]] = layout.height - fromBottom[i];
        }
    }

    static Result computePreset(float width, float height, float dpScale, float playerScale, int preset) {
        Result layout = new Result();
        if (!Float.isFinite(width) || !Float.isFinite(height) || width <= 1.0f || height <= 1.0f ||
                width > 65536.0f || height > 65536.0f) return layout;
        layout.width = width; layout.height = height;
        final float scale = Float.isFinite(playerScale) ? Math.max(0.75f, Math.min(1.5f, playerScale)) : 1.0f;
        layout.nativeDp = Float.isFinite(dpScale) && dpScale >= 0.25f && dpScale <= 16.0f ? dpScale : 1.0f;
        final float dp = layout.nativeDp * scale * (preset == PRESET_COMPACT ? 0.8f : 1.0f);
        layout.dp = dp;
        for (int i = 0; i < COUNT; i++) {
            layout.radius[i] = DIAMETER[i] * 0.5f * dp;
            layout.x[i] = ANCHOR[i] == LEADING_TOP ? FROM_SIDE[i] * dp : layout.width - FROM_SIDE[i] * dp;
            layout.y[i] = ANCHOR[i] == TRAILING_BOTTOM ? layout.height - FROM_END[i] * dp : FROM_END[i] * dp;
        }
        final boolean portrait = height > width;
        if (portrait) placePortraitDeck(layout, dp);
        int placed = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (!placeButton(layout, i, placed, false)) return layout;
            placed |= 1 << i;
        }
        layout.available = true;
        layout.stickBaseRadius = STICK_BASE_RADIUS_DP * dp;
        layout.stickKnobRadius = STICK_KNOB_RADIUS_DP * dp;
        layout.stickHomeX = portrait ? layout.width * 0.25f : Math.min((STICK_BASE_RADIUS_DP + 56.0f) * dp, layout.width * 0.25f);
        layout.stickHomeY = layout.height - (STICK_BASE_RADIUS_DP + 48.0f) * dp;
        layout.leadingEdge = layout.width * 0.5f;
        layout.stickTop = deckTop(width, height);
        layout.stickHomeY = Math.max(layout.stickHomeY, (layout.stickTop + height) * 0.5f);
        if (preset == PRESET_LEFT_HANDED) {
            layout.leftHanded = true;
            for (int i = 0; i < COUNT; i++) layout.x[i] = width - layout.x[i];
            layout.stickHomeX = width - layout.stickHomeX;
        }
        return layout;
    }

    /** ComputeLayout: placed controls first, where the player put them, then the rest. */
    static Result compute(float width, float height, float dpScale, float playerScale, int preset, Edits edits) {
        Result layout = computePreset(width, height, dpScale, playerScale, preset);
        if (edits == null || edits.isDefault() || !layout.available) return layout;
        Result edited = layout.copy();
        edited.edited = true;
        for (int i = 0; i < COUNT; ++i) {
            final Edit edit = edits.get(i);
            edited.restOpacity[i] = (float) edit.opacity / 100.0f;
            if (edit.hidden && i != PAUSE) edited.hidden |= 1 << i;
            edited.radius[i] = layout.radius[i] * (float) edit.size / 100.0f;
            if (edit.moved) {
                edited.moved |= 1 << i;
                edited.x[i] = edit.x > 0 ? edit.x * layout.nativeDp : width + edit.x * layout.nativeDp;
                edited.y[i] = edit.y > 0 ? edit.y * layout.nativeDp : height + edit.y * layout.nativeDp;
            }
        }
        int placed = 0;
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < COUNT; ++i) {
                final boolean moved = (edited.moved & (1 << i)) != 0;
                if ((edited.hidden & (1 << i)) != 0 || moved != (pass == 0)) continue;
                if (!placeButton(edited, i, placed, moved)) {
                    layout.editsRejected = true;
                    return layout;
                }
                placed |= 1 << i;
            }
        }
        return edited;
    }

    /** The control under (x, y), nearest first, among the visible mask; -1 for none. */
    static int hit(Result layout, float x, float y, int visible) {
        if (!layout.available) return -1;
        int best = -1;
        float bestDistance = 0;
        for (int i = 0; i < COUNT; i++) {
            if ((visible & (1 << i)) == 0) continue;
            final float reach = hitRadius(layout, i), distance = distanceSquared(x, y, layout.x[i], layout.y[i]);
            if (distance <= reach * reach && (best < 0 || distance < bestDistance)) { best = i; bestDistance = distance; }
        }
        return best;
    }

    /** Whole dp from the nearest edges for a center in pixels, snapped to the editor grid when asked. */
    static int[] edgeOffsets(Result layout, float x, float y, boolean snap) {
        final float widthDp = layout.width / layout.nativeDp, heightDp = layout.height / layout.nativeDp;
        final float xDp = x / layout.nativeDp, yDp = y / layout.nativeDp;
        int fromX = xDp <= widthDp * 0.5f ? Math.round(xDp) : -Math.round(widthDp - xDp);
        int fromY = yDp <= heightDp * 0.5f ? Math.round(yDp) : -Math.round(heightDp - yDp);
        if (snap) { fromX = snapToGrid(fromX); fromY = snapToGrid(fromY); }
        return new int[] {nonZero(fromX), nonZero(fromY)};
    }

    private static int snapToGrid(int value) {
        int sign = value < 0 ? -1 : 1, magnitude = Math.abs(value);
        return sign * Math.max(GRID_DP, Math.round(magnitude / (float) GRID_DP) * GRID_DP);
    }

    private static int nonZero(int value) {
        return Math.max(-POSITION_LIMIT, Math.min(POSITION_LIMIT, value == 0 ? 1 : value));
    }

    /**
     * The edit for a control dropped at (x, y) pixels: the snapped grid point
     * when it fits there, otherwise the free spot placement chooses for it,
     * so the record always says what the player sees. Null when nothing fits.
     */
    static Edits drop(float width, float height, float dpScale, float playerScale, int preset, Edits edits,
                      int button, float x, float y) {
        Result base = computePreset(width, height, dpScale, playerScale, preset);
        if (!base.available) return null;
        int[] snapped = edgeOffsets(base, x, y, true);
        Edits candidate = edits.with(button, edits.get(button).at(snapped[0], snapped[1]));
        Result placed = compute(width, height, dpScale, playerScale, preset, candidate);
        if (!placed.edited) return null;
        int[] resolved = edgeOffsets(placed, placed.x[button], placed.y[button], false);
        int[] wanted = edgeOffsets(placed, toPixels(base, snapped[0], true), toPixels(base, snapped[1], false), false);
        if (Arrays.equals(resolved, wanted)) return candidate;
        Edits moved = edits.with(button, edits.get(button).at(resolved[0], resolved[1]));
        return compute(width, height, dpScale, playerScale, preset, moved).edited ? moved : null;
    }

    private static float toPixels(Result layout, int offset, boolean horizontal) {
        float extent = horizontal ? layout.width : layout.height;
        return offset > 0 ? offset * layout.nativeDp : extent + offset * layout.nativeDp;
    }
}
