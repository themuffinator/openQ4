// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Viewing profile, UI size and text size (ui-visual-design.md section 13.2),
 * and whether play may turn to portrait (section 13.6).
 * Setup edits an isolated draft; the engine applies each applied transaction
 * once at foreground entry, so later in-game size changes survive a return.
 */
final class InterfacePage {
    private final Context context;
    private final SetupTheme theme;
    private final SharedPreferences prefs;
    private InterfaceSettings draft;
    private TextView sampleTitle, sampleBody, effective;

    InterfacePage(Context context, SetupTheme theme, SharedPreferences prefs) {
        this.context = context; this.theme = theme; this.prefs = prefs;
        draft = InterfaceSettings.decode(prefs.getString(InterfaceSettings.DRAFT,
                prefs.getString(InterfaceSettings.SAVED, null)));
    }

    void build(LinearLayout column, Runnable back) {
        column.addView(theme.text(R.string.setup_interface, true));
        column.addView(theme.text(R.string.setup_interface_help, false));
        sampleTitle = theme.text(R.string.setup_interface_sample_title, true);
        sampleBody = theme.text(R.string.setup_interface_sample_body, false);
        effective = theme.text(R.string.setup_interface_effective, false);
        column.addView(theme.text(R.string.setup_viewing_profile, false));
        Spinner profiles = spinner(context.getResources().getStringArray(R.array.setup_viewing_profiles), draft.profile,
                R.string.setup_viewing_profile);
        column.addView(profiles);
        profiles.setOnItemSelectedListener(select(position -> {
            if (position != draft.profile) change(draft.select(position, draft.uiPercent, draft.textPercent));
        }));
        column.addView(theme.text(R.string.setup_interface_size, false));
        Spinner ui = scaleSpinner(draft.uiPercent, 75, R.string.setup_profile_size, R.string.setup_interface_size);
        column.addView(ui);
        ui.setOnItemSelectedListener(select(position -> {
            int value = position == 0 ? 0 : 75 + (position - 1) * 5;
            if (value != draft.uiPercent) change(draft.select(draft.profile, value, draft.textPercent));
        }));
        column.addView(theme.text(R.string.setup_text_size, false));
        Spinner text = scaleSpinner(draft.textPercent, 100, R.string.setup_profile_text_size, R.string.setup_text_size);
        column.addView(text);
        text.setOnItemSelectedListener(select(position -> {
            int value = position == 0 ? 0 : 100 + (position - 1) * 5;
            if (value != draft.textPercent) change(draft.select(draft.profile, draft.uiPercent, value));
        }));
        column.addView(theme.text(R.string.setup_orientation, false));
        Spinner orientation = spinner(context.getResources().getStringArray(R.array.setup_orientations), draft.orientation,
                R.string.setup_orientation);
        column.addView(orientation);
        orientation.setOnItemSelectedListener(select(position -> {
            if (position != draft.orientation) change(draft.withOrientation(position));
        }));
        column.addView(theme.text(R.string.setup_orientation_help, false));
        column.addView(effective);
        column.addView(sampleTitle);
        column.addView(sampleBody);
        column.addView(theme.button(R.string.setup_interface_apply, () -> {
            if (saveApplied(prefs, draft)) {
                Toast.makeText(context, R.string.setup_interface_applied, Toast.LENGTH_SHORT).show(); back.run();
            } else Toast.makeText(context, R.string.setup_interface_save_failed, Toast.LENGTH_LONG).show();
        }));
        column.addView(theme.button(R.string.setup_touch_discard, () -> {
            prefs.edit().remove(InterfaceSettings.DRAFT).apply(); back.run();
        }));
        update();
    }

    static boolean saveApplied(SharedPreferences prefs, InterfaceSettings draft) {
        String previous = prefs.getString(InterfaceSettings.SAVED, null);
        String next = draft.applied().encode();
        if (prefs.edit().putString(InterfaceSettings.SAVED, next).remove(InterfaceSettings.DRAFT).commit()) return true;
        // A failed disk commit can still change SharedPreferences memory.
        prefs.edit().putString(InterfaceSettings.SAVED, previous).putString(InterfaceSettings.DRAFT, draft.encode()).apply();
        return false;
    }

    private interface Selection { void value(int position); }
    private AdapterView.OnItemSelectedListener select(Selection action) {
        return new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) { action.value(position); }
        };
    }
    private Spinner scaleSpinner(int value, int minimum, int automatic, int label) {
        String[] choices = new String[2 + (200 - minimum) / 5];
        choices[0] = context.getString(automatic);
        for (int i = 1; i < choices.length; ++i)
            choices[i] = context.getString(R.string.setup_interface_percent, minimum + (i - 1) * 5);
        return spinner(choices, value == 0 ? 0 : 1 + (value - minimum) / 5, label);
    }
    private Spinner spinner(String[] choices, int selected, int label) {
        Spinner view = new Spinner(context);
        view.setMinimumHeight(theme.dp(48));
        view.setContentDescription(context.getString(label));
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(context, android.R.layout.simple_spinner_item, choices) {
            private View style(View view) {
                TextView text = (TextView) view; text.setTypeface(theme.body); text.setTextColor(SetupTheme.TEXT);
                text.setTextSize(18); text.setMinimumHeight(theme.dp(48)); return view;
            }
            @Override public View getView(int position, View recycled, ViewGroup parent) { return style(super.getView(position,recycled,parent)); }
            @Override public View getDropDownView(int position, View recycled, ViewGroup parent) { return style(super.getDropDownView(position,recycled,parent)); }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        view.setAdapter(adapter); view.setSelection(selected);
        return view;
    }
    private void change(InterfaceSettings value) {
        draft = value; prefs.edit().putString(InterfaceSettings.DRAFT, draft.encode()).apply(); update();
    }
    private void update() {
        Configuration config = context.getResources().getConfiguration();
        boolean tablet = config.smallestScreenWidthDp >= 600;
        int ui = draft.resolvedUi(tablet), text = draft.resolvedText(tablet, config.fontScale);
        effective.setText(context.getString(R.string.setup_interface_effective, ui, text));
        // The resolved text percentage already includes the system font preference.
        float scale = ui / 100.0f * text / 100.0f;
        sampleTitle.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 24 * scale);
        sampleBody.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 17 * scale);
    }
}
