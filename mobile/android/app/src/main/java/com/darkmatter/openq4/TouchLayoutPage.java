// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.DisplayMetrics;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Section 13.6's touch layout editor: move, resize (75-150%), fade and hide
 * each control on an 8 dp grid, with a layout per orientation, the three
 * presets and a reset. The preview is TouchLayout, the engine's placement in
 * Java, at this device's screen size in each orientation. Every change is a
 * draft that survives process death; Apply commits the layout and the preset
 * together, and the engine reads them at its next foreground entry.
 */
final class TouchLayoutPage {
    private final Context context;
    private final SetupTheme theme;
    private final SharedPreferences prefs;
    private final float longDp, shortDp;
    private TouchLayout.Record record;
    private TouchSettings settings;
    private boolean portrait;
    private int selected = TouchLayout.FIRE;
    private boolean syncing;
    private Preview preview;
    private Spinner controls;
    private TextView size, opacity, note;
    private SeekBar sizeBar, opacityBar;
    private CheckBox hide;

    TouchLayoutPage(Activity activity, SetupTheme theme, SharedPreferences prefs) {
        this.context = activity; this.theme = theme; this.prefs = prefs;
        record = draftOrSaved(prefs);
        settings = TouchSettings.decode(prefs.getString(TouchSettings.DRAFT,
                prefs.getString(TouchSettings.SAVED, TouchSettings.DEFAULT.encode())));
        DisplayMetrics metrics = new DisplayMetrics();
        activity.getWindowManager().getDefaultDisplay().getRealMetrics(metrics);
        float density = metrics.density > 0 ? metrics.density : 1;
        longDp = Math.max(metrics.widthPixels, metrics.heightPixels) / density;
        shortDp = Math.min(metrics.widthPixels, metrics.heightPixels) / density;
    }

    static TouchLayout.Record draftOrSaved(SharedPreferences prefs) {
        TouchLayout.Record draft = TouchLayout.Record.decode(prefs.getString(TouchLayout.DRAFT, null));
        if (draft != null) return draft;
        TouchLayout.Record saved = TouchLayout.Record.decode(prefs.getString(TouchLayout.SAVED, null));
        return saved != null ? saved : TouchLayout.Record.DEFAULT;
    }

    /** One commit: the layout and the touch settings it was edited with, or neither. */
    static boolean saveApplied(SharedPreferences prefs, TouchLayout.Record layout, TouchSettings touch) {
        String previousLayout = prefs.getString(TouchLayout.SAVED, null), previousTouch = prefs.getString(TouchSettings.SAVED, null);
        if (prefs.edit().putString(TouchLayout.SAVED, layout.encode()).putString(TouchSettings.SAVED, touch.encode())
                .remove(TouchLayout.DRAFT).remove(TouchSettings.DRAFT).commit()) return true;
        // SharedPreferences updates memory even when its disk commit fails.
        // Restore the applied records before any game return, keeping the drafts.
        prefs.edit().putString(TouchLayout.SAVED, previousLayout).putString(TouchSettings.SAVED, previousTouch)
                .putString(TouchLayout.DRAFT, layout.encode()).putString(TouchSettings.DRAFT, touch.encode()).apply();
        return false;
    }

    private float width() { return portrait ? shortDp : longDp; }
    private float height() { return portrait ? longDp : shortDp; }
    private TouchLayout.Edits edits() { return record.get(portrait); }

    private TouchLayout.Result computeLayout() {
        return TouchLayout.compute(width(), height(), 1, settings.scale(), settings.preset, edits());
    }

    void build(LinearLayout column, Runnable back) {
        column.addView(theme.text(R.string.setup_layout, true));
        column.addView(theme.text(R.string.setup_layout_help, false));
        Spinner orientation = spinner(R.string.setup_layout_orientation, R.array.setup_layout_orientations);
        column.addView(theme.text(R.string.setup_layout_orientation, true));
        column.addView(orientation);
        column.addView(theme.text(R.string.setup_touch_preset, true));
        Spinner presets = spinner(R.string.setup_touch_preset, R.array.setup_touch_presets);
        presets.setSelection(settings.preset);
        column.addView(presets);
        preview = new Preview(context);
        column.addView(preview, new LinearLayout.LayoutParams(-1, -2));
        column.addView(theme.text(R.string.setup_layout_control, true));
        controls = spinner(R.string.setup_layout_control, R.array.setup_layout_controls);
        column.addView(controls);
        note = theme.text(R.string.setup_layout_pause_note, false);
        column.addView(note);
        size = theme.text(R.string.setup_layout_size, true); column.addView(size);
        sizeBar = slider((TouchLayout.SIZE_MAX - TouchLayout.SIZE_MIN) / TouchLayout.STEP, R.string.setup_layout_size_label);
        column.addView(sizeBar);
        opacity = theme.text(R.string.setup_layout_opacity, true); column.addView(opacity);
        opacityBar = slider((TouchLayout.OPACITY_MAX - TouchLayout.OPACITY_MIN) / TouchLayout.STEP, R.string.setup_layout_opacity_label);
        column.addView(opacityBar);
        hide = new CheckBox(context);
        hide.setText(R.string.setup_layout_hide); hide.setTypeface(theme.body); hide.setTextSize(18);
        hide.setTextColor(SetupTheme.TEXT); hide.setMinHeight(theme.dp(48));
        column.addView(hide);
        column.addView(theme.button(R.string.setup_layout_reset_control, () ->
            change(edits().with(selected, edits().get(selected).unmoved().sized(TouchLayout.SIZE_DEFAULT)
                                .faded(TouchLayout.OPACITY_DEFAULT).hiding(false)))));
        column.addView(theme.button(R.string.setup_layout_reset, () -> change(TouchLayout.Edits.DEFAULT)));
        column.addView(theme.button(R.string.setup_layout_apply, () -> {
            android.util.Log.i("openQ4", "layout editor: apply " + record.encode());
            if (saveApplied(prefs, record, settings)) {
                Toast.makeText(context, R.string.setup_layout_applied, Toast.LENGTH_SHORT).show(); back.run();
            } else {
                Toast.makeText(context, R.string.setup_layout_save_failed, Toast.LENGTH_LONG).show();
            }
        }));
        column.addView(theme.button(R.string.setup_touch_discard, () -> {
            prefs.edit().remove(TouchLayout.DRAFT).remove(TouchSettings.DRAFT).apply(); back.run();
        }));

        orientation.setOnItemSelectedListener(selection(position -> {
            if ((position == 1) != portrait) { portrait = position == 1; update(); }
        }));
        presets.setOnItemSelectedListener(selection(position -> {
            if (position == settings.preset) return;
            TouchSettings next = new TouchSettings(position, settings.sensitivityStep, settings.sizeStep);
            // A preset is a new starting point: positions placed for another
            // arrangement would be kept, so check that this one still fits.
            if (!fitsAll(record, next)) { refuse(); presets.setSelection(settings.preset); return; }
            settings = next;
            prefs.edit().putString(TouchSettings.DRAFT, settings.encode()).apply();
            update();
        }));
        controls.setOnItemSelectedListener(selection(position -> { selected = position; update(); }));
        sizeBar.setOnSeekBarChangeListener(seek(value ->
            change(edits().with(selected, edits().get(selected).sized(TouchLayout.SIZE_MIN + value * TouchLayout.STEP)))));
        opacityBar.setOnSeekBarChangeListener(seek(value ->
            change(edits().with(selected, edits().get(selected).faded(TouchLayout.OPACITY_MIN + value * TouchLayout.STEP)))));
        hide.setOnCheckedChangeListener((view, checked) -> {
            if (!syncing && selected != TouchLayout.PAUSE) change(edits().with(selected, edits().get(selected).hiding(checked)));
        });
        update();
    }

    private boolean fitsAll(TouchLayout.Record candidate, TouchSettings touch) {
        for (boolean tall : new boolean[] {false, true}) {
            float w = tall ? shortDp : longDp, h = tall ? longDp : shortDp;
            if (TouchLayout.compute(w, h, 1, touch.scale(), touch.preset, candidate.get(tall)).editsRejected) return false;
        }
        return true;
    }

    /** Every edit is checked against the preview; one that cannot fit is refused, never stored. */
    private void change(TouchLayout.Edits next) {
        if (syncing) return;
        if (TouchLayout.compute(width(), height(), 1, settings.scale(), settings.preset, next).editsRejected) {
            android.util.Log.i("openQ4", "layout editor: change refused (does not fit)");
            refuse(); update(); return;
        }
        record = record.with(portrait, next);
        prefs.edit().putString(TouchLayout.DRAFT, record.encode()).apply();
        update();
    }

    private void refuse() { Toast.makeText(context, R.string.setup_layout_no_fit, Toast.LENGTH_SHORT).show(); }

    private void update() {
        syncing = true;
        TouchLayout.Edit edit = edits().get(selected);
        if (controls.getSelectedItemPosition() != selected) controls.setSelection(selected);
        sizeBar.setProgress((edit.size - TouchLayout.SIZE_MIN) / TouchLayout.STEP);
        opacityBar.setProgress((edit.opacity - TouchLayout.OPACITY_MIN) / TouchLayout.STEP);
        size.setText(context.getString(R.string.setup_layout_size, edit.size));
        opacity.setText(context.getString(R.string.setup_layout_opacity, edit.opacity));
        sizeBar.setContentDescription(size.getText()); opacityBar.setContentDescription(opacity.getText());
        hide.setChecked(edit.hidden);
        hide.setEnabled(selected != TouchLayout.PAUSE);
        note.setVisibility(selected == TouchLayout.PAUSE || TouchLayout.multiplayerOnly(selected) ? View.VISIBLE : View.GONE);
        note.setText(selected == TouchLayout.PAUSE ? R.string.setup_layout_pause_note : R.string.setup_layout_multiplayer_note);
        syncing = false;
        preview.requestLayout(); preview.invalidate();
    }

    private Spinner spinner(int label, int items) {
        Spinner spinner = new Spinner(context);
        spinner.setMinimumHeight(theme.dp(48));
        spinner.setContentDescription(context.getString(label));
        ArrayAdapter<CharSequence> adapter = new ArrayAdapter<CharSequence>(context, android.R.layout.simple_spinner_item,
                context.getResources().getTextArray(items)) {
            private View style(View view) {
                TextView text = (TextView) view; text.setTypeface(theme.body);
                text.setTextSize(18); text.setTextColor(SetupTheme.TEXT); text.setMinimumHeight(theme.dp(48)); return view;
            }
            @Override public View getView(int position, View recycled, ViewGroup parent) {
                return style(super.getView(position, recycled, parent));
            }
            @Override public View getDropDownView(int position, View recycled, ViewGroup parent) {
                return style(super.getDropDownView(position, recycled, parent));
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spinner.setAdapter(adapter);
        return spinner;
    }

    private SeekBar slider(int max, int label) {
        SeekBar view = new SeekBar(context); view.setMax(max);
        view.setMinimumHeight(theme.dp(48)); view.setContentDescription(context.getString(label)); return view;
    }

    private interface Choice { void value(int value); }
    private static AdapterView.OnItemSelectedListener selection(Choice choice) {
        return new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) { choice.value(position); }
        };
    }
    private static SeekBar.OnSeekBarChangeListener seek(Choice choice) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar view) {}
            @Override public void onStopTrackingTouch(SeekBar view) {}
            @Override public void onProgressChanged(SeekBar view, int value, boolean user) { if (user) choice.value(value); }
        };
    }

    /** The screen in the chosen orientation, scaled to fit; drag a control to place it. */
    private final class Preview extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final String[] names;
        private int dragging = -1, pointer = -1;
        private float dragX, dragY, startX, startY, scale = 1, left, top;
        private boolean moved;

        Preview(Context context) {
            super(context);
            names = context.getResources().getStringArray(R.array.setup_layout_controls);
            setContentDescription(context.getString(R.string.setup_layout_preview));
            setFocusable(true);
        }

        @Override protected void onMeasure(int widthSpec, int heightSpec) {
            int available = MeasureSpec.getSize(widthSpec);
            int maxHeight = Math.round(getResources().getDisplayMetrics().heightPixels * .7f);
            float s = Math.min(available / width(), maxHeight / height());
            setMeasuredDimension(available, Math.max(theme.dp(120), Math.round(height() * s)));
        }

        private void frame() {
            scale = Math.min(getWidth() / width(), getHeight() / height());
            left = (getWidth() - width() * scale) * .5f;
            top = (getHeight() - height() * scale) * .5f;
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            frame();
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                TouchLayout.Result layout = computeLayout();
                float x = (event.getX() - left) / scale, y = (event.getY() - top) / scale;
                int hit = TouchLayout.hit(layout, x, y, (1 << TouchLayout.COUNT) - 1);
                pointer = event.getPointerId(0);
                if (hit >= 0) {
                    selected = hit; dragging = hit; dragX = layout.x[hit]; dragY = layout.y[hit];
                    startX = x; startY = y; moved = false;
                    getParent().requestDisallowInterceptTouchEvent(true);
                    performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY);
                    update();
                }
                return true;
            }
            int index = event.findPointerIndex(pointer);
            if (dragging >= 0 && index >= 0 && action == MotionEvent.ACTION_MOVE) {
                float x = (event.getX(index) - left) / scale, y = (event.getY(index) - top) / scale;
                // A finger that stays within 8 dp selects; only a drag places.
                moved = moved || Math.hypot((x - startX) * scale, (y - startY) * scale) > theme.dp(8);
                dragX = Math.max(0, Math.min(width(), x));
                dragY = Math.max(0, Math.min(height(), y));
                invalidate();
            } else if (dragging >= 0 && index >= 0 && action == MotionEvent.ACTION_UP) {
                int button = dragging; dragging = -1; pointer = -1;
                getParent().requestDisallowInterceptTouchEvent(false);
                if (moved) {
                    TouchLayout.Edits next = TouchLayout.drop(width(), height(), 1, settings.scale(), settings.preset,
                                                              edits(), button, dragX, dragY);
                    if (next == null) android.util.Log.i("openQ4", "layout editor: drop of control " + button + " refused");
                    if (next == null) { refuse(); update(); } else change(next);
                } else {
                    invalidate();	// a tap selects; it never pins the control where it is
                }
                performClick();
            } else if (action == MotionEvent.ACTION_CANCEL || (index < 0 && dragging >= 0)) {
                android.util.Log.i("openQ4", "layout editor: gesture " + (action == MotionEvent.ACTION_CANCEL ? "cancelled" : "lost its pointer")
                                   + " while dragging " + dragging);
                dragging = -1; pointer = -1; getParent().requestDisallowInterceptTouchEvent(false); invalidate();
            }
            return true;
        }
        @Override public boolean performClick() { return super.performClick(); }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            frame();
            TouchLayout.Result layout = computeLayout();
            canvas.save();
            canvas.translate(left, top);
            paint.setPathEffect(null);
            paint.setStyle(Paint.Style.FILL); paint.setColor(0xff11160e);
            canvas.drawRect(0, 0, width() * scale, height() * scale, paint);
            // The snapping grid: every 8 dp where that stays legible, else 16 or 32.
            float step = TouchLayout.GRID_DP;
            while (step * scale < theme.dp(6)) step *= 2;
            paint.setStrokeWidth(1); paint.setColor(0x1fb3d06e);
            for (float x = step; x < width(); x += step) canvas.drawLine(x * scale, 0, x * scale, height() * scale, paint);
            for (float y = step; y < height(); y += step) canvas.drawLine(0, y * scale, width() * scale, y * scale, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(theme.dp(1)); paint.setColor(0xff565c3f);
            canvas.drawRect(0, 0, width() * scale, height() * scale, paint);
            // The crosshair, which no control may cover, and in portrait the thumb deck.
            float cx = width() * .5f * scale, cy = height() * .5f * scale;
            paint.setColor(SetupTheme.AMBER);
            canvas.drawLine(cx - theme.dp(6), cy, cx + theme.dp(6), cy, paint);
            canvas.drawLine(cx, cy - theme.dp(6), cx, cy + theme.dp(6), paint);
            if (layout.stickTop > 0) {
                paint.setColor(0x80b3d06e); paint.setPathEffect(new DashPathEffect(new float[] {theme.dp(4), theme.dp(4)}, 0));
                canvas.drawLine(0, layout.stickTop * scale, width() * scale, layout.stickTop * scale, paint);
                paint.setPathEffect(null);
            }
            if (layout.available) {
                paint.setColor(0x59b3d06e);
                canvas.drawCircle(layout.stickHomeX * scale, layout.stickHomeY * scale, layout.stickBaseRadius * scale, paint);
                for (int i = 0; i < TouchLayout.COUNT; i++) {
                    boolean moving = i == dragging;
                    plate(canvas, i, moving ? dragX : layout.x[i], moving ? dragY : layout.y[i], layout.radius[i],
                          layout.opacity(i), (layout.hidden & (1 << i)) != 0, i == selected);
                }
            }
            canvas.restore();
        }

        private void plate(Canvas canvas, int button, float x, float y, float radius, float alpha, boolean hidden, boolean chosen) {
            float half = radius * .70710678f * scale, cut = half * .5f, px = x * scale, py = y * scale;
            Path outline = new Path();
            outline.moveTo(px - half, py - half); outline.lineTo(px + half - cut, py - half);
            outline.lineTo(px + half, py - half + cut); outline.lineTo(px + half, py + half);
            outline.lineTo(px - half, py + half); outline.close();
            if (!hidden) {
                paint.setStyle(Paint.Style.FILL); paint.setColor(0x171c12 | (Math.round(alpha * 255) << 24));
                canvas.drawPath(outline, paint);
            }
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(theme.dp(chosen ? 2 : 1));
            paint.setPathEffect(hidden ? new DashPathEffect(new float[] {theme.dp(3), theme.dp(3)}, 0) : null);
            int rail = chosen ? 0xffff9900 : 0xb3d06e | (Math.round(Math.max(alpha, hidden ? .3f : alpha) * 255) << 24);
            paint.setColor(rail);
            canvas.drawPath(outline, paint);
            paint.setPathEffect(null);
            // Labels show only while editing (section 13.6).
            paint.setStyle(Paint.Style.FILL); paint.setTypeface(theme.body);
            paint.setTextSize(Math.max(theme.dp(9), Math.min(theme.dp(13), half * .6f)));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setColor(hidden ? 0x99cccccc : chosen ? 0xffff9900 : SetupTheme.TEXT);
            canvas.drawText(names[button], px, py + half + paint.getTextSize() * 1.1f, paint);
        }
    }
}
