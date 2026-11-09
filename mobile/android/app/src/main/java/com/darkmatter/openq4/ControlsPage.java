// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.text.BidiFormatter;
import android.text.TextUtils;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import java.util.ArrayList;
import java.util.List;

/** Setup edits an isolated draft. The engine reads applied values on its own thread at resume. */
final class ControlsPage {
    private final Context context;
    private final SetupTheme theme;
    private final SharedPreferences prefs;
    private TouchSettings draft;
    private TextView sensitivity, size, description;
    private SeekBar speed, scale;
    private LookTest preview;

    ControlsPage(Context context, SetupTheme theme, SharedPreferences prefs) {
        this.context = context; this.theme = theme; this.prefs = prefs;
        draft = TouchSettings.decode(prefs.getString(TouchSettings.DRAFT,
                prefs.getString(TouchSettings.SAVED, TouchSettings.DEFAULT.encode())));
    }

    void build(LinearLayout column, Runnable back, Runnable editLayout) {
        column.addView(theme.text(R.string.setup_controls, true));
        column.addView(theme.text(R.string.setup_controls_help, false));
        TextView devices = theme.text(R.string.setup_controller_none, false);
        List<String> names = new ArrayList<>();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() && (device.supportsSource(InputDevice.SOURCE_GAMEPAD) ||
                    device.supportsSource(InputDevice.SOURCE_JOYSTICK)))
                names.add(BidiFormatter.getInstance().unicodeWrap(device.getName()));
        }
        if (!names.isEmpty()) devices.setText(context.getString(R.string.setup_controller_detected, TextUtils.join(", ", names)));
        column.addView(devices);
        column.addView(theme.text(R.string.setup_touch_preset, true));
        Spinner presets = new Spinner(context);
        presets.setMinimumHeight(theme.dp(48));
        presets.setContentDescription(context.getString(R.string.setup_touch_preset));
        ArrayAdapter<CharSequence> adapter = new ArrayAdapter<CharSequence>(context, android.R.layout.simple_spinner_item,
                context.getResources().getTextArray(R.array.setup_touch_presets)) {
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
        presets.setAdapter(adapter); presets.setSelection(draft.preset); column.addView(presets);
        description = theme.text(R.string.setup_touch_default_help, false); column.addView(description);
        // Section 13.6's editor: move, resize, fade and hide each control per orientation.
        column.addView(theme.button(R.string.setup_layout_edit, editLayout));
        sensitivity = theme.text(R.string.setup_touch_sensitivity, true); column.addView(sensitivity);
        speed = slider(draft.sensitivityStep - 1, 39, R.string.setup_touch_sensitivity_label);
        speed.setOnSeekBarChangeListener(listener(value -> changed(new TouchSettings(draft.preset, value + 1, draft.sizeStep))));
        column.addView(speed);
        size = theme.text(R.string.setup_touch_size, true); column.addView(size);
        scale = slider(draft.sizeStep, 15, R.string.setup_touch_size_label);
        scale.setOnSeekBarChangeListener(listener(value -> changed(new TouchSettings(draft.preset, draft.sensitivityStep, value))));
        column.addView(scale);
        column.addView(theme.text(R.string.setup_look_test_help, false));
        preview = new LookTest(context, theme);
        column.addView(preview, new LinearLayout.LayoutParams(-1, theme.dp(180)));
        presets.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position != draft.preset) changed(new TouchSettings(position, draft.sensitivityStep, draft.sizeStep));
            }
        });
        column.addView(theme.button(R.string.setup_touch_apply, () -> {
            if (saveApplied(prefs, draft)) {
                Toast.makeText(context, R.string.setup_touch_applied, Toast.LENGTH_SHORT).show(); back.run();
            } else {
                Toast.makeText(context, R.string.setup_touch_save_failed, Toast.LENGTH_LONG).show();
            }
        }));
        column.addView(theme.button(R.string.setup_touch_discard, () -> {
            prefs.edit().remove(TouchSettings.DRAFT).apply(); back.run();
        }));
        update();
    }

    static boolean saveApplied(SharedPreferences prefs, TouchSettings draft) {
        String previous = prefs.getString(TouchSettings.SAVED, null);
        if (prefs.edit().putString(TouchSettings.SAVED, draft.encode()).remove(TouchSettings.DRAFT).commit()) return true;
        // SharedPreferences updates memory even when its disk commit fails.
        // Restore the applied record before any game return, retaining the draft.
        prefs.edit().putString(TouchSettings.SAVED, previous).putString(TouchSettings.DRAFT, draft.encode()).apply();
        return false;
    }

    private interface Change { void value(int value); }
    private SeekBar slider(int value, int max, int label) {
        SeekBar view = new SeekBar(context); view.setMax(max); view.setProgress(value);
        view.setMinimumHeight(theme.dp(48)); view.setContentDescription(context.getString(label)); return view;
    }
    private SeekBar.OnSeekBarChangeListener listener(Change change) {
        return new SeekBar.OnSeekBarChangeListener() {
            @Override public void onStartTrackingTouch(SeekBar view) {}
            @Override public void onStopTrackingTouch(SeekBar view) {}
            @Override public void onProgressChanged(SeekBar view, int value, boolean user) { if (user) change.value(value); }
        };
    }
    private void changed(TouchSettings value) {
        draft = value; prefs.edit().putString(TouchSettings.DRAFT, draft.encode()).apply(); update();
    }
    private void update() {
        int[] help = {R.string.setup_touch_default_help, R.string.setup_touch_compact_help, R.string.setup_touch_left_help};
        description.setText(help[draft.preset]);
        sensitivity.setText(context.getString(R.string.setup_touch_sensitivity, draft.sensitivity()));
        size.setText(context.getString(R.string.setup_touch_size, Math.round(draft.scale() * 100)));
        speed.setContentDescription(sensitivity.getText()); scale.setContentDescription(size.getText());
        preview.sensitivity = draft.sensitivity(); preview.invalidate();
    }

    /** A bounded look-speed exercise, never an input source for gameplay. */
    private static final class LookTest extends View {
        final SetupTheme theme;
        final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float sensitivity = 1, yaw, pitch, lastX, lastY;
        int pointer = -1;
        LookTest(Context context, SetupTheme theme) {
            super(context); this.theme = theme;
            setContentDescription(context.getString(R.string.setup_look_test_help));
            setBackgroundColor(SetupTheme.BACKGROUND);
        }
        @Override public boolean onTouchEvent(MotionEvent event) {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) {
                pointer = event.getPointerId(0); lastX = event.getX(); lastY = event.getY();
                getParent().requestDisallowInterceptTouchEvent(true);
            }
            int index = event.findPointerIndex(pointer);
            if (index >= 0 && action == MotionEvent.ACTION_MOVE) {
                float density = getResources().getDisplayMetrics().density;
                yaw = (yaw - TouchSettings.lookDegrees(event.getX(index) - lastX, density, sensitivity)) % 360;
                pitch = Math.max(-89, Math.min(89, pitch + TouchSettings.lookDegrees(event.getY(index) - lastY, density, sensitivity)));
                lastX = event.getX(index); lastY = event.getY(index); invalidate();
            }
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL || index < 0 ||
                    (action == MotionEvent.ACTION_POINTER_UP && event.getPointerId(event.getActionIndex()) == pointer)) {
                pointer = -1; getParent().requestDisallowInterceptTouchEvent(false);
            }
            if (action == MotionEvent.ACTION_UP) performClick();
            return true;
        }
        @Override public boolean performClick() { return super.performClick(); }
        @Override public void onWindowFocusChanged(boolean focused) {
            super.onWindowFocusChanged(focused); if (!focused) pointer = -1;
        }
        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float unit = theme.dp(2), cx = getWidth() * .5f, cy = getHeight() * .5f;
            paint.setStrokeWidth(theme.dp(1)); paint.setColor(0xff565c3f);
            float spacing = 30 * unit;
            for (float x = cx + (yaw % 30) * unit - getWidth(); x < getWidth(); x += spacing)
                canvas.drawLine(x, 0, x, getHeight(), paint);
            for (float y = cy - pitch * unit - 180 * unit; y < getHeight(); y += spacing)
                canvas.drawLine(0, y, getWidth(), y, paint);
            paint.setColor(SetupTheme.AMBER); paint.setStrokeWidth(theme.dp(2));
            canvas.drawLine(cx - theme.dp(10), cy, cx + theme.dp(10), cy, paint);
            canvas.drawLine(cx, cy - theme.dp(10), cx, cy + theme.dp(10), paint);
        }
    }
}
