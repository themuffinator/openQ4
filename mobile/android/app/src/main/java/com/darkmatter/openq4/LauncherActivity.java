// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.text.BidiFormatter;
import android.text.format.Formatter;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.util.Locale;

/** Setup runs before SDL loads. Every game launch remains an explicit player action. */
public final class LauncherActivity extends Activity implements SetupTask.Observer<RetailData.Report> {
    private static final int PICK_FOLDER = 1, EXPORT_SAVES = 2, EXPORT_LOGS = 3;
    static final String EXTRA_PAGE = "setup_page", PAGE_TOUCH_LAYOUT = "touch_layout";
    /** Debuggable builds only: "sp" or "mp" starts the game once setup's own data check succeeds (device qualification). */
    static final String EXTRA_DIAGNOSTIC_LAUNCH = "diagnostic_launch";
    /** Debuggable builds only: "hold" keeps setup's screen on between qualification runs. */
    static final String DIAGNOSTIC_HOLD = "hold";
    private static Session retained;

    private static final class Session {
        final Handler delivery = new Handler(Looper.getMainLooper());
        final SetupTask<RetailData.Report> task = new SetupTask<>(callback -> delivery.post(callback));
        final SetupTask<DocumentExport.Result> export = new SetupTask<>(callback -> delivery.post(callback));
        final SharedPreferences prefs;
        Uri source;
        boolean consumed, exporting;
        int picking, notice;
        PrivateExport.Kind exportKind;
        Session(Context context) {
            prefs = preferences(context);
            String stored = prefs.getString("source", "");
            if (!stored.isEmpty()) source = Uri.parse(stored);
            exporting = prefs.getBoolean("exporting", false);
            exportKind = "logs".equals(prefs.getString("export_kind", "")) ? PrivateExport.Kind.LOGS : PrivateExport.Kind.SAVES;
        }
    }

    private Session session;
    private SetupTheme theme;
    private ScrollView scroll;
    private TextView status, detail, amount, sourceNotice;
    private ProgressBar progress;
    private Button choose, check, retry, cancel, sp, mp, returnGame, saves, logs;
    private boolean started;
    private boolean controlsPage;
    private boolean interfacePage;
    private boolean layoutPage;
    private Boolean diagnosticLaunch;	// requested multiplayer flag, or null
    private boolean diagnosticCheckStarted;
    private boolean diagnosticHold;
    private final Handler refresh = new Handler(Looper.getMainLooper());
    private final Runnable refreshState = () -> { if (started) changed(session.task.state()); };
    private final SetupTask.Observer<DocumentExport.Result> exportObserver = ignored -> changed(session.task.state());
    private SetupTask.Phase measuredPhase;
    private String measuredName;
    private long measuredAt, measuredBytes;

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences("setup", MODE_PRIVATE);
    }

    static void languageApplied(String tag) {
        Session current = retained;
        if (current == null) return;
        // SDL asks for arguments on its worker. A failed library load never
        // acknowledges the choice; a newer UI choice must not be cleared by
        // an older launch arriving here after the player changes language.
        current.delivery.post(() -> {
            if (tag.equals(current.prefs.getString("language", SetupLanguage.systemDefault())))
                current.prefs.edit().putBoolean("language_pending", false).apply();
        });
    }

    @Override protected void attachBaseContext(Context base) {
        String tag = preferences(base).getString("language", SetupLanguage.systemDefault());
        Configuration configuration = new Configuration(base.getResources().getConfiguration());
        configuration.setLocale(Locale.forLanguageTag(SetupLanguage.TAGS[SetupLanguage.index(tag)]));
        super.attachBaseContext(base.createConfigurationContext(configuration));
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (retained == null) retained = new Session(getApplicationContext());
        session = retained;
        if (state != null) {
            session.picking = state.getInt("picking", session.picking);
            controlsPage = state.getBoolean("controls_page", false);
            interfacePage = state.getBoolean("interface_page", false);
            layoutPage = state.getBoolean("layout_page", false);
        } else {
            layoutPage = PAGE_TOUCH_LAYOUT.equals(getIntent().getStringExtra(EXTRA_PAGE));
            diagnosticLaunch = diagnosticLaunch(getIntent(), getApplicationInfo().flags);
            diagnosticHold = diagnosticHold(getIntent(), getApplicationInfo().flags);
        }
        theme = new SetupTheme(this);
        build();
        if (state != null) scroll.post(() -> scroll.scrollTo(0, state.getInt("scroll", 0)));
    }

    /** singleTask: the game's request for the layout editor reaches a running setup here. */
    /**
     * The device-qualification launch request: a debuggable build only, "sp" or
     * "mp" only. Release builds ignore it, so the exported setup entry point can
     * never start the game without the player's choice.
     */
    static Boolean diagnosticLaunch(Intent intent, int applicationFlags) {
        if ((applicationFlags & ApplicationInfo.FLAG_DEBUGGABLE) == 0 || intent == null) return null;
        String mode = intent.getStringExtra(EXTRA_DIAGNOSTIC_LAUNCH);
        return "sp".equals(mode) ? Boolean.FALSE : "mp".equals(mode) ? Boolean.TRUE : null;
    }

    static boolean diagnosticHold(Intent intent, int applicationFlags) {
        return (applicationFlags & ApplicationInfo.FLAG_DEBUGGABLE) != 0 && intent != null
                && DIAGNOSTIC_HOLD.equals(intent.getStringExtra(EXTRA_DIAGNOSTIC_LAUNCH));
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        Boolean requested = diagnosticLaunch(intent, getApplicationInfo().flags);
        diagnosticHold = diagnosticHold(intent, getApplicationInfo().flags);
        if (requested != null) { diagnosticLaunch = requested; diagnosticCheckStarted = false; }
        changed(session.task.state());
        if (PAGE_TOUCH_LAYOUT.equals(intent.getStringExtra(EXTRA_PAGE)) && !layoutPage) {
            controlsPage = interfacePage = false; layoutPage = true;
            build(); changed(session.task.state());
        }
    }

    @Override protected void onStart() {
        super.onStart(); started = true; session.task.bind(this); session.export.bind(exportObserver);
    }
    @Override protected void onStop() {
        started = false; refresh.removeCallbacks(refreshState);
        session.task.unbind(this); session.export.unbind(exportObserver); super.onStop();
    }
    @Override protected void onSaveInstanceState(Bundle state) {
        state.putInt("picking", session.picking);
        state.putInt("scroll", scroll.getScrollY());
        state.putBoolean("controls_page", controlsPage);
        state.putBoolean("interface_page", interfacePage);
        state.putBoolean("layout_page", layoutPage);
        super.onSaveInstanceState(state);
    }

    private void build() {
        status = null;
        scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setMotionEventSplittingEnabled(false);
        scroll.setBackgroundColor(SetupTheme.BACKGROUND);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);
        // Keep all setup controls clear of bars/cutouts/IME, including enforced
        // edge-to-edge on recent Android. Scroll instead of clipping large text.
        scroll.setOnApplyWindowInsetsListener((view, insets) -> {
            int left = insets.getSystemWindowInsetLeft(), top = insets.getSystemWindowInsetTop();
            int right = insets.getSystemWindowInsetRight(), bottom = insets.getSystemWindowInsetBottom();
            if (android.os.Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
            }
            view.setPadding(left, top, right, bottom);
            return insets.consumeSystemWindowInsets();
        });
        FrameLayout frame = new FrameLayout(this);
        frame.setMotionEventSplittingEnabled(false);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setMotionEventSplittingEnabled(false);
        column.setPadding(theme.dp(20), theme.dp(16), theme.dp(20), theme.dp(24));
        frame.addView(column, new FrameLayout.LayoutParams(-1, -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        frame.addOnLayoutChangeListener((view, l, t, r, b, oldL, oldT, oldR, oldB) -> {
            int width = Math.min(r - l, theme.dp(760));
            if (width > 0 && column.getLayoutParams().width != width) {
                column.getLayoutParams().width = width; column.requestLayout();
            }
        });
        scroll.addView(frame);
        column.addView(theme.text(R.string.setup_title, true));
        // Setup can be reopened before a language choice has been committed
        // (for example after a direct restore from Recents). Every page must
        // offer the same route back to the existing single-instance game.
        returnGame = theme.button(R.string.setup_return, () -> {
            if (StorageSession.INSTANCE.activityActive()) startActivity(new Intent(this, OpenQ4Activity.class));
        });
        returnGame.setVisibility(StorageSession.INSTANCE.activityActive() ? View.VISIBLE : View.GONE);
        addButton(column, returnGame);
        if (!session.prefs.getBoolean("language_chosen", false)) {
            languagePage(column);
        } else if (layoutPage) {
            new TouchLayoutPage(this, theme, session.prefs).build(column, () -> {
                layoutPage = false; build(); changed(session.task.state());
            });
        } else if (controlsPage) {
            new ControlsPage(this, theme, session.prefs).build(column, () -> {
                controlsPage = false; build(); changed(session.task.state());
            }, () -> {
                controlsPage = false; layoutPage = true; build(); changed(session.task.state());
            });
        } else if (interfacePage) {
            new InterfacePage(this, theme, session.prefs).build(column, () -> {
                interfacePage = false; build(); changed(session.task.state());
            });
        } else dataPage(column);
        setContentView(scroll);
        scroll.requestApplyInsets();
    }

    private void addButton(LinearLayout column, Button button) {
        LinearLayout.LayoutParams layout = new LinearLayout.LayoutParams(-1, -2);
        layout.topMargin = theme.dp(8);
        column.addView(button, layout);
    }

    private void languagePage(LinearLayout column) {
        column.addView(theme.text(R.string.setup_language, true));
        Spinner languages = new Spinner(this);
        languages.setMinimumHeight(theme.dp(48));
        languages.setContentDescription(getString(R.string.setup_language));
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item,
                getResources().getStringArray(R.array.setup_language_names)) {
            private View style(View view) {
                TextView text = (TextView) view;
                text.setTypeface(theme.body); text.setTextSize(18); text.setTextColor(SetupTheme.TEXT);
                text.setMinimumHeight(theme.dp(48));
                return text;
            }
            @Override public View getView(int position, View convertView, ViewGroup parent) {
                return style(super.getView(position, convertView, parent));
            }
            @Override public View getDropDownView(int position, View convertView, ViewGroup parent) {
                return style(super.getDropDownView(position, convertView, parent));
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        languages.setAdapter(adapter);
        int selected = SetupLanguage.index(session.prefs.getString("language", SetupLanguage.systemDefault()));
        languages.setSelection(selected);
        languages.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onNothingSelected(AdapterView<?> parent) {}
            @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position == selected) return;
                session.prefs.edit().putString("language", SetupLanguage.TAGS[position]).putBoolean("language_pending", true).apply();
                recreate();
            }
        });
        column.addView(languages);
        column.addView(theme.text(R.string.setup_sample, false));
        addButton(column, theme.button(R.string.setup_continue, () -> {
            session.prefs.edit().putString("language", SetupLanguage.TAGS[languages.getSelectedItemPosition()])
                .putBoolean("language_chosen", true).putBoolean("language_pending", true).apply();
            build(); changed(session.task.state());
        }));
    }

    private void dataPage(LinearLayout column) {
        column.addView(theme.text(R.string.setup_data, true));
        column.addView(theme.text(R.string.setup_data_help, false));
        status = theme.text(R.string.setup_idle, false);
        status.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        column.addView(status);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        column.addView(progress, new LinearLayout.LayoutParams(-1, theme.dp(16)));
        amount = theme.text(R.string.setup_idle, false); column.addView(amount);
        sourceNotice = theme.text(R.string.setup_idle, false); column.addView(sourceNotice);
        choose = theme.button(R.string.setup_choose, this::chooseFolder); addButton(column, choose);
        check = theme.button(R.string.setup_check, () -> run(false)); addButton(column, check);
        retry = theme.button(R.string.setup_retry, () -> {
            if (session.exporting) chooseExport(session.exportKind);
            else run(session.prefs.getBoolean("import", false));
        }); addButton(column, retry);
        cancel = theme.button(R.string.setup_cancel, () -> {
            if (session.exporting) session.export.cancel(); else session.task.cancel();
        }); addButton(column, cancel);
        sp = theme.button(R.string.setup_play_sp, () -> launch(false)); addButton(column, sp);
        mp = theme.button(R.string.setup_play_mp, () -> launch(true)); addButton(column, mp);
        addButton(column, theme.button(R.string.setup_controls, () -> { controlsPage = true; build(); }));
        addButton(column, theme.button(R.string.setup_interface, () -> { interfacePage = true; build(); }));
        column.addView(theme.text(R.string.setup_exports, true));
        column.addView(theme.text(R.string.setup_export_help, false));
        saves = theme.button(R.string.setup_export_saves, () -> chooseExport(PrivateExport.Kind.SAVES)); addButton(column, saves);
        logs = theme.button(R.string.setup_export_logs, () -> chooseExport(PrivateExport.Kind.LOGS)); addButton(column, logs);
        addButton(column, theme.button(R.string.setup_change_language, () -> {
            session.prefs.edit().putBoolean("language_chosen", false).apply();
            status = null; build();
        }));
        // A missing installation can report many archives. Keep recovery
        // actions before that list so choosing a folder stays easy to reach.
        detail = theme.text(R.string.setup_idle, false); detail.setTextIsSelectable(true); column.addView(detail);
    }

    private boolean available() {
        return !session.task.state().busy() && !session.export.state().busy() && session.picking == 0 && !StorageSession.INSTANCE.gameActive();
    }

    private void run(boolean importing) {
        if (!available()) return;
        if (importing && session.source == null) { chooseFolder(); return; }
        session.consumed = false; session.notice = 0; session.exporting = false;
        session.prefs.edit().putBoolean("pending", true).putBoolean("import", importing).putBoolean("exporting", false).apply();
        session.task.start(new SetupWork(getApplicationContext(), importing ? session.source : null));
        scroll.smoothScrollTo(0, 0);
    }

    @SuppressWarnings("deprecation")
    private void chooseFolder() {
        if (!available()) return;
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            session.picking = PICK_FOLDER;
            startActivityForResult(picker, PICK_FOLDER);
        } catch (ActivityNotFoundException unavailable) {
            session.picking = 0; session.notice = R.string.setup_no_picker;
        }
        changed(session.task.state());
    }

    @SuppressWarnings("deprecation")
    private void chooseExport(PrivateExport.Kind kind) {
        if (!available()) return;
        Intent picker = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/zip").addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            .putExtra(Intent.EXTRA_TITLE, getString(kind == PrivateExport.Kind.SAVES ? R.string.setup_saves_filename : R.string.setup_logs_filename));
        try {
            session.picking = kind == PrivateExport.Kind.SAVES ? EXPORT_SAVES : EXPORT_LOGS;
            startActivityForResult(picker, session.picking);
        } catch (ActivityNotFoundException unavailable) {
            session.picking = 0; session.notice = R.string.setup_no_document_picker;
        }
        changed(session.task.state());
    }

    private void exportResult(int request, int result, Intent data) {
        if (result != RESULT_OK) { session.notice = R.string.setup_export_picker_cancelled; changed(session.task.state()); return; }
        Uri uri = data == null ? null : data.getData();
        if (uri == null || !"content".equals(uri.getScheme()) || (data.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) == 0) {
            session.notice = R.string.setup_export_no_grant; changed(session.task.state()); return;
        }
        if (!available()) { session.notice = R.string.setup_game_active; changed(session.task.state()); return; }
        session.exporting = true; session.notice = 0;
        session.exportKind = request == EXPORT_SAVES ? PrivateExport.Kind.SAVES : PrivateExport.Kind.LOGS;
        session.prefs.edit().putBoolean("pending", true).putBoolean("exporting", true)
            .putString("export_kind", session.exportKind == PrivateExport.Kind.LOGS ? "logs" : "saves").apply();
        session.export.start(new DocumentExport(getApplicationContext(), uri, session.exportKind));
        scroll.smoothScrollTo(0, 0);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != session.picking || request < PICK_FOLDER || request > EXPORT_LOGS) return;
        session.picking = 0;
        if (request != PICK_FOLDER) { exportResult(request, result, data); return; }
        if (result != RESULT_OK) { session.notice = R.string.setup_picker_cancelled; changed(session.task.state()); return; }
        Uri uri = data == null ? null : data.getData();
        if (uri == null || !"content".equals(uri.getScheme()) || !DocumentsContract.isTreeUri(uri) ||
                (data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) == 0) {
            session.notice = R.string.setup_no_grant; changed(session.task.state()); return;
        }
        String previous = session.prefs.getString("source", "");
        boolean persistent = false;
        if ((data.getFlags() & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
            try {
                getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                persistent = true;
            } catch (SecurityException lostGrant) { Log.w("openQ4", "Folder grant is temporary", lostGrant); }
        }
        session.source = uri;
        session.prefs.edit().putString("source", persistent ? uri.toString() : "").apply();
        if (!previous.isEmpty() && !previous.equals(uri.toString())) {
            try { getContentResolver().releasePersistableUriPermission(Uri.parse(previous), Intent.FLAG_GRANT_READ_URI_PERMISSION); }
            catch (SecurityException alreadyRevoked) { /* No remaining access to release. */ }
        }
        run(true);
        session.notice = persistent ? 0 : R.string.setup_temporary_grant;
        changed(session.task.state());
    }

    private void launch(boolean multiplayer) {
        SetupTask.State<RetailData.Report> state = session.task.state();
        if (!available() || session.consumed || state.result == null || !state.result.ready() || state.status != SetupTask.Status.SUCCEEDED) return;
        Intent game = new Intent(this, OpenQ4Activity.class).putExtra("multiplayer", multiplayer);
        boolean languagePending = session.prefs.getBoolean("language_pending", false);
        if (languagePending) game.putExtra("setup_language", session.prefs.getString("language", SetupLanguage.systemDefault()));
        try {
            session.consumed = true;
            startActivity(game);
        } catch (ActivityNotFoundException unavailable) {
            session.consumed = false; session.notice = R.string.setup_launch_failed;
        }
        changed(state);
    }

    private boolean refreshGameReturn() {
        boolean game = StorageSession.INSTANCE.gameActive();
        if (returnGame != null) returnGame.setVisibility(StorageSession.INSTANCE.activityActive() ? View.VISIBLE : View.GONE);
        // A timed-out SDL join can outlive its Activity. Keep observing that
        // lease on language/controls pages too, which have no data-status view.
        refresh.removeCallbacks(refreshState);
        if (game && started) refresh.postDelayed(refreshState, 500);
        return game;
    }

    @Override public void changed(SetupTask.State<RetailData.Report> state) {
        if (isFinishing()) return;
        boolean game = refreshGameReturn();
        if (status == null) return;
        SetupTask.State<?> operation = session.exporting ? session.export.state() : state;
        boolean busy = operation.busy();
        // A check, import or export reads gigabytes; the screen stays on until it
        // ends, as it does for a pending diagnostic launch, so it never pauses
        // behind the lock screen. Play keeps its own (SDL) wake state.
        if (busy || diagnosticLaunch != null || diagnosticHold) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        boolean ready = !session.consumed && state.status == SetupTask.Status.SUCCEEDED && state.result != null && state.result.ready();
        if (!busy && operation.status != SetupTask.Status.IDLE && session.prefs.getBoolean("pending", false))
            session.prefs.edit().putBoolean("pending", false).apply();
        choose.setEnabled(available()); check.setEnabled(available()); retry.setEnabled(available());
        saves.setEnabled(available()); logs.setEnabled(available());
        retry.setVisibility(operation.status == SetupTask.Status.IDLE && !session.prefs.getBoolean("pending", false) ? View.GONE : View.VISIBLE);
        cancel.setVisibility(busy ? View.VISIBLE : View.GONE);
        cancel.setEnabled(operation.status == SetupTask.Status.RUNNING);
        sp.setVisibility(ready && !game ? View.VISIBLE : View.GONE); mp.setVisibility(sp.getVisibility());
        if (diagnosticLaunch != null && ready && !game && available()) {
            boolean multiplayer = diagnosticLaunch; diagnosticLaunch = null;
            Log.i("openQ4", "Diagnostic launch: " + (multiplayer ? "multiplayer" : "single player"));
            refresh.post(() -> launch(multiplayer));
        } else if (diagnosticLaunch != null && !diagnosticCheckStarted && !busy && !game && available()
                && state.status != SetupTask.Status.SUCCEEDED) {
            // The ordinary installed-data check, once; the launch follows its success.
            diagnosticCheckStarted = true;
            refresh.post(() -> run(false));
        }
        sp.setEnabled(available()); mp.setEnabled(available());
        progress.setVisibility(busy && !game ? View.VISIBLE : View.GONE);
        amount.setVisibility(progress.getVisibility());
        int heading = R.string.setup_idle;
        if (game) heading = R.string.setup_game_active;
        else if (operation.status == SetupTask.Status.STOPPING) heading = R.string.setup_stopping;
        else if (busy) heading = phaseText(operation.progress == null ?
            (session.exporting ? SetupTask.Phase.PACKING_EXPORT : SetupTask.Phase.CHECKING_DATA) : operation.progress.phase);
        else if (operation.status == SetupTask.Status.CANCELLED) heading = session.exporting ? R.string.setup_export_cancelled : R.string.setup_cancelled;
        else if (ready) heading = R.string.setup_ready;
        else if (!session.consumed && state.result != null) heading = R.string.setup_needs_data;
        else if (session.prefs.getBoolean("pending", false)) heading = session.exporting ? R.string.setup_export_interrupted : R.string.setup_interrupted;
        String title = getString(heading);
        if (!game && operation.status == SetupTask.Status.FAILED)
            title = session.exporting ? exportFailure(operation.failure) : failure(operation.failure);
        else if (!game && session.exporting && operation.status == SetupTask.Status.SUCCEEDED) {
            DocumentExport.Result result = session.export.state().result;
            title = getString(R.string.setup_export_done, result.files, Formatter.formatFileSize(this, result.bytes));
        }
        if (!title.contentEquals(status.getText())) status.setText(title);
        StringBuilder findings = new StringBuilder();
        if (!busy && !session.consumed && state.result != null) {
            for (RetailData.Issue issue : state.result.issues) {
                if (findings.length() > 0) findings.append("\n\n");
                findings.append(getString(issue.blocking ? R.string.setup_issue_problem : R.string.setup_issue_warning,
                    getString(issueText(issue.code), displayName(issue.name))));
            }
        }
        detail.setText(findings); detail.setVisibility(findings.length() == 0 ? View.GONE : View.VISIBLE);
        sourceNotice.setVisibility(session.notice == 0 ? View.GONE : View.VISIBLE);
        if (session.notice != 0) sourceNotice.setText(session.notice);
        if (busy && operation.progress != null) showProgress(operation.progress);
        else amount.setText("");
    }

    private void showProgress(SetupTask.Progress value) {
        progress.setIndeterminate(value.totalBytes <= 0);
        if (value.totalBytes > 0) progress.setProgress((int) Math.min(1000, (double) value.bytes * 1000 / value.totalBytes));
        StringBuilder text = new StringBuilder(displayName(value.name));
        if (value.files > 0) text.append('\n').append(getString(value.phase == SetupTask.Phase.PACKING_EXPORT ?
            R.string.setup_export_file_count : R.string.setup_file_count, value.completed, value.files));
        text.append('\n').append(value.totalBytes >= 0 ? getString(R.string.setup_bytes,
            Formatter.formatFileSize(this, value.bytes), Formatter.formatFileSize(this, value.totalBytes)) : Formatter.formatFileSize(this, value.bytes));
        long now = android.os.SystemClock.elapsedRealtime();
        if (measuredPhase != value.phase || !value.name.equals(measuredName) || value.bytes < measuredBytes) {
            measuredPhase = value.phase; measuredName = value.name; measuredAt = now; measuredBytes = value.bytes;
        }
        long elapsed = now - measuredAt, done = value.bytes - measuredBytes;
        if (elapsed >= 2000 && done > 0 && value.totalBytes > value.bytes) {
            long seconds = (long) Math.ceil((double) (value.totalBytes - value.bytes) * elapsed / done / 1000);
            if (seconds < 86400) text.append('\n').append(getString(R.string.setup_eta, seconds));
        }
        amount.setText(text);
    }

    private String displayName(String name) {
        String clean = name.replaceAll("[\\p{Cntrl}\\p{Cf}]", "");
        return BidiFormatter.getInstance().unicodeWrap(clean.substring(0, Math.min(240, clean.length())));
    }

    private static int phaseText(SetupTask.Phase phase) {
        switch (phase) {
            case CHECKING_EXISTING: return R.string.setup_checking_existing;
            case COPYING_DATA: return R.string.setup_copying_data;
            case VERIFYING_DATA: return R.string.setup_verifying_data;
            case CHECKING_PACKAGE: return R.string.setup_checking_package;
            case COPYING_PACKAGE: return R.string.setup_copying_package;
            case PACKING_EXPORT: return R.string.setup_packing_export;
            case EXPORTING: return R.string.setup_exporting;
            default: return R.string.setup_checking_data;
        }
    }

    private static int issueText(RetailData.Code code) {
        switch (code) {
            case MISSING_REQUIRED: return R.string.setup_missing_required;
            case MISSING_OPTIONAL: return R.string.setup_missing_optional;
            case MISSING_LANGUAGE: return R.string.setup_missing_language;
            case CHECKSUM_MISMATCH: return R.string.setup_wrong_version;
            case UNRECOGNIZED_ARCHIVE: return R.string.setup_unknown_archive;
            case DUPLICATE_ARCHIVE: return R.string.setup_duplicate;
            case CHANGED_FILE: return R.string.setup_changed;
            case UNREADABLE: return R.string.setup_unreadable;
            case LIMIT: return R.string.setup_limits;
            case DIRECTORY_UNAVAILABLE: return R.string.setup_no_data;
            default: return R.string.setup_damaged_archive;
        }
    }

    private static int failureText(Exception error) {
        if (error instanceof StorageSession.Busy) return R.string.setup_game_active;
        if (error instanceof PackageStorage.InsufficientSpace) return R.string.setup_no_space;
        if (error instanceof RetailImport.Failure) {
            switch (((RetailImport.Failure) error).code) {
                case SOURCE_UNAVAILABLE: return R.string.setup_no_grant;
                case INVALID_NAME: return R.string.setup_invalid_name;
                case DUPLICATE_NAME: return R.string.setup_duplicate;
                case LIMIT: return R.string.setup_limits;
                case DESTINATION_UNAVAILABLE: return R.string.setup_storage_unavailable;
                case INSUFFICIENT_SPACE: return R.string.setup_no_space;
                case SOURCE_CHANGED: return R.string.setup_changed;
                case INVALID_ARCHIVE: return R.string.setup_damaged_archive;
                case WRONG_VERSION: return R.string.setup_wrong_version;
                case COPY_FAILED: return R.string.setup_copy_failed;
                case COMMIT_FAILED: return R.string.setup_commit_failed;
            }
        }
        return R.string.setup_prepare_failed;
    }

    private String failure(Exception error) {
        if (error instanceof PackageStorage.InsufficientSpace) {
            PackageStorage.InsufficientSpace space = (PackageStorage.InsufficientSpace) error;
            return getString(R.string.setup_no_space, exactSize(space.requiredBytes), exactSize(space.availableBytes));
        }
        if (error instanceof RetailImport.Failure) {
            RetailImport.Failure failure = (RetailImport.Failure) error;
            if (failure.code == RetailImport.Code.INSUFFICIENT_SPACE) return getString(R.string.setup_no_space,
                exactSize(failure.requiredBytes), exactSize(failure.availableBytes));
            return getString(failureText(error), displayName(failure.name));
        }
        return getString(failureText(error));
    }

    private String exactSize(long bytes) { return getString(R.string.setup_size_bytes, Math.max(0, bytes)); }

    private String exportFailure(Exception error) {
        if (error instanceof StorageSession.Busy) return getString(R.string.setup_game_active);
        int message = R.string.setup_export_failed;
        String name = "";
        if (error instanceof PrivateExport.Failure) {
            PrivateExport.Failure failure = (PrivateExport.Failure) error;
            name = displayName(failure.name);
            switch (failure.code) {
                case EMPTY: message = R.string.setup_export_empty; break;
                case SOURCE_UNAVAILABLE: message = R.string.setup_export_unreadable; break;
                case UNSAFE_PATH: message = R.string.setup_export_unsafe; break;
                case LIMIT: message = R.string.setup_export_limit; break;
                case CHANGED_FILE: message = R.string.setup_export_changed; break;
                case CACHE_UNAVAILABLE: message = R.string.setup_export_cache; break;
                case OUTPUT_UNAVAILABLE: message = R.string.setup_export_no_grant; break;
                case WRITE_FAILED: message = R.string.setup_export_write; break;
                case INSUFFICIENT_SPACE: return getString(R.string.setup_export_no_space,
                    exactSize(failure.requiredBytes), exactSize(failure.availableBytes));
            }
        }
        return getString(message, name);
    }
}
