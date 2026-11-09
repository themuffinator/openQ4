// Standalone host for emileb's Android/GLES support in openQ4.
// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.annotation.TargetApi;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.KeyEvent;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.util.Log;
import android.widget.Toast;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** SDL owns the Activity lifecycle; Meson owns all native compilation. */
public final class OpenQ4Activity extends SDLActivity {
    private boolean setupOpening;

    /** Called by the engine thread at foreground entry; never calls native code on the UI thread. */
    public float[] getTouchSetup() {
        String saved = getSharedPreferences("setup", MODE_PRIVATE).getString(TouchSettings.SAVED, null);
        return saved == null ? null : TouchSettings.decode(saved).engineValues();
    }

    /** The layout editor's applied record (TouchLayout), read with getTouchSetup; null before the first Apply. */
    public String getTouchLayout() {
        TouchLayout.Record saved = TouchLayout.Record.decode(
                getSharedPreferences("setup", MODE_PRIVATE).getString(TouchLayout.SAVED, null));
        return saved == null ? null : saved.encode();
    }

    /** SDL user message arguments: which setup page the game asked for. */
    static final int SETUP_PAGE_HOME = 0, SETUP_PAGE_TOUCH_LAYOUT = 1;

    /** An unchanged applied revision never overwrites later in-game scale choices. */
    public String getInterfaceSetup() {
        InterfaceSettings saved = InterfaceSettings.decodeOrNull(
                getSharedPreferences("setup", MODE_PRIVATE).getString(InterfaceSettings.SAVED, null));
        Configuration config = getResources().getConfiguration();
        return saved == null ? null : saved.engineValues(config.smallestScreenWidthDp >= 600, config.fontScale);
    }

    /** Landscape either way up, or with the player's Interface choice also portrait (rotation lock respected). */
    static int screenOrientation(InterfaceSettings saved) {
        return saved != null && saved.orientation == InterfaceSettings.ORIENTATION_AUTOMATIC
                ? ActivityInfo.SCREEN_ORIENTATION_FULL_USER : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
    }

    private void applyScreenOrientation() {
        int requested = screenOrientation(InterfaceSettings.decodeOrNull(
                getSharedPreferences("setup", MODE_PRIVATE).getString(InterfaceSettings.SAVED, null)));
        if (getRequestedOrientation() != requested) setRequestedOrientation(requested);
    }

    /**
     * SDL calls this from its thread whenever it creates the window. For a
     * resizable window without SDL_HINT_ORIENTATIONS it would request FULL_USER,
     * unlocking portrait regardless of the manifest; the player's choice decides.
     */
    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        runOnUiThread(this::applyScreenOrientation);
    }

    @Override
    protected boolean onUnhandledMessage(int command, Object param) {
        if (command != COMMAND_USER) return super.onUnhandledMessage(command, param);
        // SDL delivers user messages on the Android UI thread. Reuse the
        // single-task launcher; never finish/recreate the live game here.
        if (setupOpening || isFinishing() || mBrokenLibraries || !mActivityCreated) return true;
        setupOpening = true;
        try {
            Intent setup = new Intent(this, LauncherActivity.class);
            // The game's Settings > Controls > Touch layout opens the editor itself.
            if (param instanceof Integer && (Integer) param == SETUP_PAGE_TOUCH_LAYOUT)
                setup.putExtra(LauncherActivity.EXTRA_PAGE, LauncherActivity.PAGE_TOUCH_LAYOUT);
            startActivity(setup);
            Log.i("openQ4", "Opened Android setup from the game");
        } catch (ActivityNotFoundException unavailable) {
            setupOpening = false;
            Log.e("openQ4", "Could not reopen Android setup", unavailable);
            Toast.makeText(this, R.string.setup_open_failed, Toast.LENGTH_LONG).show();
        }
        return true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        setupOpening = false;
        // A new Apply in setup reaches the running game on its return.
        applyScreenOrientation();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        StorageSession.INSTANCE.activityCreated(this);
        super.onCreate(savedInstanceState);
        applyScreenOrientation();
        if (!mBrokenLibraries && mLayout != null && Build.VERSION.SDK_INT >= 30) {
            keepOutOfDisplayCutout(mLayout);
        }
    }

    @Override
    protected void onDestroy() {
        try { super.onDestroy(); }
        finally { StorageSession.INSTANCE.activityDestroyed(this); }
    }

    @Override
    protected void main() {
        try {
            if (!StorageSession.INSTANCE.beginEngine(this)) return;
        } catch (InterruptedException cancelled) {
            Thread.currentThread().interrupt();
            return;
        }
        try { super.main(); }
        finally { StorageSession.INSTANCE.endEngine(this); }
    }

    @TargetApi(30)
    private static void keepOutOfDisplayCutout(ViewGroup layout) {
        // SDL allows the surface under display cutouts on Android 11+. The
        // stock HUD and in-world GUIs still use the complete render target.
        // Keep that target inside the cutout, preserving one coordinate space
        // for rendering, retained UI and SDL input instead of clipping controls.
        layout.setBackgroundColor(Color.BLACK);
        layout.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets cutout = insets.getInsets(WindowInsets.Type.displayCutout());
            view.setPadding(cutout.left, cutout.top, cutout.right, cutout.bottom);
            // Children, including SDL's surface and text input, receive the
            // remaining system/IME insets relative to this padded content.
            return insets.inset(cutout);
        });
        layout.requestApplyInsets();
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if (isFinishing()) return;
        if (mBrokenLibraries || !mActivityCreated) {
            // There is no native UI to navigate after a failed SDL load.
            // SDL's default handler calls JNI, so do not enter it here.
            finish();
            return;
        }
        // Gesture navigation reaches this callback without a hardware key.
        // Use the same SDL Back event as a physical button: the engine routes
        // it through its current menu, gameplay pause or fatal-error console.
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "quake4" };
    }

    @Override
    protected String[] getArguments() {
        // SDL invokes this on SDLThread, so first-run asset extraction does not
        // block the Android UI thread or alter input owned by the Activity.
        try {
            File retailRoot = getExternalFilesDir(null);
            if (retailRoot == null) {
                throw new IOException("App-specific external storage is unavailable");
            }
            requireDirectory(new File(retailRoot, "q4base"));
            File saveRoot = new File(getFilesDir(), "saves");
            requireDirectory(saveRoot);
            // Early native fatal diagnostics run before fs_savepath is parsed.
            Os.setenv("OPENQ4_SAVE_ROOT", saveRoot.getAbsolutePath(), true);
            File contentRoot = extractPackage();
            Os.setenv("OPENQ4_NATIVE_LIBS", getApplicationInfo().nativeLibraryDir, true);
            Os.setenv("OPENQ4_CONTENT_ROOT", contentRoot.getAbsolutePath(), true);
            boolean multiplayer = getIntent().getBooleanExtra("multiplayer", false);
            List<String> arguments = new ArrayList<>(Arrays.asList(
                "+set", "fs_basepath", retailRoot.getAbsolutePath(),
                "+set", "fs_savepath", saveRoot.getAbsolutePath(),
                "+set", "fs_cachepath", getCacheDir().getAbsolutePath(),
                "+set", "fs_game", "baseoq4",
                "+set", "si_gameType", multiplayer ? "DM" : "singleplayer",
                "+set", "ui_autoJoin", "0",
                "+set", "r_renderApi", "gles",
                "+set", "r_renderer", "glesd3",
                "+set", "r_fullscreen", "0",
                "+set", "in_tty", "0",
                "+set", "logFile", "2",
                "+set", "logFileName", "logs/openq4.log"
            ));
            String language = SetupLanguage.engine(getIntent().getStringExtra("setup_language"));
            if (language != null) {
                arguments.addAll(Arrays.asList("+set", "sys_lang", language));
                LauncherActivity.languageApplied(getIntent().getStringExtra("setup_language"));
            }
            return arguments.toArray(new String[0]);
        } catch (IOException | ErrnoException error) {
            // SDL/logcat retains the actionable cause; never run with partial
            // package data or fall back to an unintended writable directory.
            throw new IllegalStateException("Could not prepare openQ4 storage", error);
        }
    }

    private static void requireDirectory(File directory) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("Cannot create directory: " + directory);
        }
    }

    private File extractPackage() throws IOException, ErrnoException {
        return PackageStorage.extract(getAssets(), getFilesDir());
    }
}
