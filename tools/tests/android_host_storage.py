#!/usr/bin/env python3
"""Run the real Android Activity extraction code against temporary JVM storage.

Small Android/SDL API stand-ins replace the unavailable device, while the
production Activity handles extraction, retry, readiness and upgrade cleanup.
Requires a JDK; all fixture files remain in the repository's .tmp directory.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    java_home = os.environ.get("JAVA_HOME", "")
    parser.add_argument("--javac", default=shutil.which("javac") or
                        (str(Path(java_home) / "bin/javac") if java_home else None))
    parser.add_argument("--package-source", type=Path, help="also verify and repair actual staged pak0/pak1/pak2/mod data")
    args = parser.parse_args()
    if not args.javac:
        parser.error("a JDK is required (--javac or JAVA_HOME)")
    root = Path(__file__).resolve().parents[2]
    manifest = ET.parse(root / 'mobile/android/app/src/main/AndroidManifest.xml')
    android = '{http://schemas.android.com/apk/res/android}'
    activity = next(node for node in manifest.iter('activity') if node.get(android + 'name') == '.OpenQ4Activity')
    required_changes = {'layoutDirection', 'locale', 'grammaticalGender', 'fontScale', 'fontWeightAdjustment',
                        'orientation', 'uiMode', 'screenLayout', 'screenSize', 'smallestScreenSize',
                        'keyboard', 'keyboardHidden', 'navigation', 'density'}
    if not required_changes.issubset(set(activity.get(android + 'configChanges', '').split('|'))):
        raise AssertionError('SDL Activity must retain its native session through configuration and density changes')
    scratch_root = root / ".tmp"
    scratch_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="android-host-storage-", dir=scratch_root) as temporary:
        scratch = Path(temporary)
        sources = {
            "android/content/res/AssetManager.java": r'''
package android.content.res;
import java.io.*;
public final class AssetManager {
    public File root;
    public String fail;
    public int opens;
    public String[] list(String path) { return new File(root, path).list(); }
    public InputStream open(String path) throws IOException {
        if (path.startsWith("baseoq4/")) ++opens;
        InputStream source = new FileInputStream(new File(root, path));
        if (!path.equals(fail)) return source;
        return new FilterInputStream(source) {
            private boolean delivered;
            public int read(byte[] b, int off, int len) throws IOException {
                if (delivered) throw new IOException("Injected interrupted extraction");
                delivered = true;
                return super.read(b, off, Math.min(4, len));
            }
        };
    }
}''',
            "android/system/ErrnoException.java": r'''
package android.system;
public final class ErrnoException extends Exception {
    public ErrnoException(Exception cause) { super(cause); }
}''',
            "android/system/Os.java": r'''
package android.system;
import java.nio.file.*;
public final class Os {
    public static String failRename;
    public static final java.util.Map<String, String> environment = new java.util.HashMap<>();
    public static void setenv(String key, String value, boolean overwrite) throws ErrnoException {
        if (overwrite || !environment.containsKey(key)) environment.put(key, value);
    }
    public static void rename(String from, String to) throws ErrnoException {
        if (to.endsWith(String.valueOf(failRename))) throw new ErrnoException(new java.io.IOException("Injected storage commit failure"));
        try { Files.move(Path.of(from), Path.of(to), StandardCopyOption.REPLACE_EXISTING); }
        catch (Exception error) { throw new ErrnoException(error); }
    }
}''',
            "android/view/KeyEvent.java": r'''
package android.view;
public final class KeyEvent { public static final int KEYCODE_BACK = 4; }
''',
            "android/annotation/TargetApi.java": r'''
package android.annotation;
public @interface TargetApi { int value(); }
''',
            "android/os/Bundle.java": r'''
package android.os;
public final class Bundle {}
''',
            "android/os/Build.java": r'''
package android.os;
public final class Build { public static final class VERSION { public static int SDK_INT = 35; } }
''',
            "android/graphics/Color.java": r'''
package android.graphics;
public final class Color { public static final int BLACK = 0xff000000; }
''',
            "android/graphics/Insets.java": r'''
package android.graphics;
public final class Insets {
    public static final Insets NONE = of(0, 0, 0, 0);
    public final int left, top, right, bottom;
    private Insets(int l, int t, int r, int b) { left=l; top=t; right=r; bottom=b; }
    public static Insets of(int l, int t, int r, int b) { return new Insets(l,t,r,b); }
}
''',
            "android/view/WindowInsets.java": r'''
package android.view;
import android.graphics.Insets;
public final class WindowInsets {
    public final Insets cutout, bars, ime;
    public WindowInsets(Insets c, Insets b, Insets i) { cutout=c; bars=b; ime=i; }
    public Insets getInsets(int types) {
        if (android.os.Build.VERSION.SDK_INT < 30) throw new AssertionError("API 30 call on older Android");
        if (types != Type.displayCutout()) throw new AssertionError("only cutout padding is owned by the host");
        return cutout;
    }
    private static Insets subtract(Insets a, Insets b) {
        return Insets.of(Math.max(0,a.left-b.left),Math.max(0,a.top-b.top),
                         Math.max(0,a.right-b.right),Math.max(0,a.bottom-b.bottom));
    }
    public WindowInsets inset(Insets by) {
        return new WindowInsets(subtract(cutout,by),subtract(bars,by),subtract(ime,by));
    }
    public static final class Type { public static int displayCutout() { return 128; } }
}
''',
            "android/view/View.java": r'''
package android.view;
public class View {
    public int left, top, right, bottom, background, insetRequests;
    public OnApplyWindowInsetsListener listener;
    public interface OnApplyWindowInsetsListener { WindowInsets onApplyWindowInsets(View v, WindowInsets i); }
    public void setPadding(int l,int t,int r,int b) { left=l; top=t; right=r; bottom=b; }
    public void setBackgroundColor(int value) { background=value; }
    public void setOnApplyWindowInsetsListener(OnApplyWindowInsetsListener value) { listener=value; }
    public void requestApplyInsets() { ++insetRequests; }
}
''',
            "android/view/ViewGroup.java": r'''
package android.view;
public class ViewGroup extends View {}
''',
            "org/libsdl/app/SDLActivity.java": r'''
package org.libsdl.app;
import java.io.File;
import android.content.res.AssetManager;
public class SDLActivity {
    public static File files;
    public static AssetManager assets;
    public static boolean mBrokenLibraries, mActivityCreated = true;
    public static android.view.ViewGroup mLayout;
    protected void onCreate(android.os.Bundle savedInstanceState) {}
    protected void onDestroy() {}
    public static Runnable nativeWork;
    protected void main() { if (nativeWork != null) nativeWork.run(); }
    public static final java.util.List<Integer> keys = new java.util.ArrayList<>();
    public static int systemBack;
    public boolean finishing;
    public int finishes;
    public void onBackPressed() { ++systemBack; }
    public boolean isFinishing() { return finishing; }
    public void finish() { finishing = true; ++finishes; }
    public static void onNativeKeyDown(int key) { keys.add(key); }
    public static void onNativeKeyUp(int key) { keys.add(-key); }
    protected String[] getLibraries() { return new String[0]; }
    protected String[] getArguments() { return new String[0]; }
    public File getExternalFilesDir(String type) { return new File(files, "retail"); }
    public File getFilesDir() { return files; }
    public File getCacheDir() { return new File(files, "cache"); }
    public AssetManager getAssets() { return assets; }
    public AppInfo getApplicationInfo() { return new AppInfo(); }
    public android.content.Intent getIntent() { return new android.content.Intent(); }
    public static class AppInfo { public String nativeLibraryDir = "/fixture/native"; }
    public static String language;
    // SDL's user-message entry point and Activity/Context members.
    public static final int COMMAND_USER = 0x8000;
    public static final int MODE_PRIVATE = 0;
    public static final java.util.List<android.content.Intent> started = new java.util.ArrayList<>();
    public static boolean startFailure;
    protected boolean onUnhandledMessage(int command, Object param) { return false; }
    protected void onResume() {}
    public void startActivity(android.content.Intent intent) {
        if (startFailure) throw new android.content.ActivityNotFoundException();
        started.add(intent);
    }
    public static final android.content.SharedPreferences preferences = new android.content.SharedPreferences();
    public android.content.SharedPreferences getSharedPreferences(String name, int mode) {
        if (!"setup".equals(name) || mode != MODE_PRIVATE) throw new AssertionError("wrong setup preferences");
        return preferences;
    }
    public static final android.content.res.Resources resources = new android.content.res.Resources();
    public android.content.res.Resources getResources() { return resources; }
    // SDL's window-derived orientation request and the Activity's own.
    public int requestedOrientation = -1, orientationRequests;
    public void setOrientationBis(int w, int h, boolean resizable, String hint) { requestedOrientation = 13; }
    public int getRequestedOrientation() { return requestedOrientation; }
    public void setRequestedOrientation(int value) { requestedOrientation = value; ++orientationRequests; }
    public void runOnUiThread(Runnable action) { action.run(); }
    public static void setOrientationOverride(SDLActivity activity, int w, int h) { activity.setOrientationBis(w, h, true, ""); }
}''',
            "android/content/pm/ActivityInfo.java": r'''
package android.content.pm;
public final class ActivityInfo {
    public static final int SCREEN_ORIENTATION_SENSOR_LANDSCAPE = 6, SCREEN_ORIENTATION_FULL_USER = 13;
}
''',
            "android/content/Intent.java": r'''
package android.content;
public class Intent {
    public final Object context;
    public final Class<?> target;
    public Intent() { this(null, null); }
    public Intent(Object context, Class<?> target) { this.context = context; this.target = target; }
    public boolean getBooleanExtra(String key, boolean value) { return value; }
    public String getStringExtra(String key) { return org.libsdl.app.SDLActivity.language; }
    public final java.util.Map<String, String> extras = new java.util.HashMap<>();
    public Intent putExtra(String key, String value) { extras.put(key, value); return this; }
}''',
            "android/content/ActivityNotFoundException.java": r'''
package android.content;
public class ActivityNotFoundException extends RuntimeException {}
''',
            "android/content/SharedPreferences.java": r'''
package android.content;
public final class SharedPreferences {
    public final java.util.Map<String, String> values = new java.util.HashMap<>();
    public String getString(String key, String fallback) { return values.getOrDefault(key, fallback); }
}
''',
            "android/content/res/Configuration.java": r'''
package android.content.res;
public final class Configuration { public int smallestScreenWidthDp = 411; public float fontScale = 1; }
''',
            "android/content/res/Resources.java": r'''
package android.content.res;
public final class Resources {
    public final Configuration configuration = new Configuration();
    public Configuration getConfiguration() { return configuration; }
}
''',
            "android/util/Log.java": r'''
package android.util;
public final class Log {
    public static final java.util.List<String> lines = new java.util.ArrayList<>();
    public static int i(String tag, String message) { lines.add("I " + message); return 0; }
    public static int e(String tag, String message, Throwable error) { lines.add("E " + message); return 0; }
}
''',
            "android/widget/Toast.java": r'''
package android.widget;
public final class Toast {
    public static final int LENGTH_SHORT = 0, LENGTH_LONG = 1;
    public static final java.util.List<Integer> shown = new java.util.ArrayList<>();
    private final int message;
    private Toast(int message) { this.message = message; }
    public static Toast makeText(Object context, int message, int duration) { return new Toast(message); }
    public void show() { shown.add(message); }
}
''',
            "com/darkmatter/openq4/R.java": r'''
package com.darkmatter.openq4;
final class R { static final class string { static final int setup_open_failed = 0x7f010001; } }
''',
            "com/darkmatter/openq4/LauncherActivity.java": r'''
package com.darkmatter.openq4;
public final class LauncherActivity {
    static final String EXTRA_PAGE = "setup_page", PAGE_TOUCH_LAYOUT = "touch_layout";
    static String applied;
    static void languageApplied(String tag) { applied = tag; }
}
''',
            "com/darkmatter/openq4/StorageTest.java": r'''
package com.darkmatter.openq4;
import android.content.res.AssetManager;
import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.*;
import org.libsdl.app.SDLActivity;
public final class StorageTest {
    static void require(boolean value, String reason) {
        if (!value) throw new AssertionError(reason);
    }
    static void write(Path file, String data) throws IOException {
        Files.createDirectories(file.getParent());
        Files.writeString(file, data);
    }
    static File extract(OpenQ4Activity activity) throws Exception {
        Method method = OpenQ4Activity.class.getDeclaredMethod("extractPackage");
        method.setAccessible(true);
        try { return (File)method.invoke(activity); }
        catch (InvocationTargetException error) { throw (Exception)error.getCause(); }
    }
    static String manifest(Path assets) throws Exception {
        Path base = assets.resolve("baseoq4");
        List<Path> files;
        try (var paths = Files.walk(base)) { files = paths.filter(Files::isRegularFile).sorted().toList(); }
        StringBuilder text = new StringBuilder("openQ4-package-v1\n");
        for (Path file : files) {
            byte[] data = Files.readAllBytes(file);
            text.append(HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data)))
                .append('\t').append(data.length).append('\t').append(base.relativize(file).toString().replace('\\','/')).append('\n');
        }
        return setManifest(assets, text.toString());
    }
    static String setManifest(Path assets, String text) throws Exception {
        write(assets.resolve("openq4-package-manifest.txt"), text);
        String version = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(text.getBytes(java.nio.charset.StandardCharsets.US_ASCII)));
        write(assets.resolve("openq4-package-version.txt"), version);
        return version;
    }
    static void expectFailure(OpenQ4Activity activity, String reason) throws Exception {
        boolean failed = false;
        try { extract(activity); } catch (IOException | android.system.ErrnoException expected) { failed = true; }
        require(failed, reason);
    }
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        // Android's trusted app-storage prefix may itself be an alias. The
        // package parent must normalize before checking child symlink escapes.
        SDLActivity.files = root.resolve("files/../files").toFile();
        SDLActivity.assets = new AssetManager();
        SDLActivity.assets.root = root.resolve("assets").toFile();
        Path assets = SDLActivity.assets.root.toPath();
        write(assets.resolve("baseoq4/pak0.pk4"), "first pack");
        write(assets.resolve("baseoq4/pak1.pk4"), "second pack");
        write(assets.resolve("baseoq4/pak2.pk4"), "third pack");
        write(assets.resolve("baseoq4/mod.json"), "{\"version\":1}");
        write(assets.resolve("baseoq4/nested/data.txt"), "nested content");
        String first = manifest(assets);
        write(root.resolve("files/saves/baseoq4/test.save"), "player save");
        write(root.resolve("files/packages/keep/test.txt"), "unmanaged");
        OpenQ4Activity activity = new OpenQ4Activity();
        for (int api : new int[] {24, 29, 30, 35, 36}) {
            android.os.Build.VERSION.SDK_INT = api;
            android.view.ViewGroup layout = new android.view.ViewGroup();
            SDLActivity.mLayout = layout;
            activity.onCreate(null);
            if (api < 30) {
                require(layout.listener == null && layout.insetRequests == 0, "older Android must keep platform cutout handling");
                continue;
            }
            require(layout.listener != null && layout.insetRequests == 1 && layout.background == 0xff000000,
                "cutout handling must be installed and requested on SDL's layout");
            for (int[] side : new int[][] {{96,0,0,0},{0,0,96,0},{0,48,0,0},{0,0,0,48},{0,0,0,0}}) {
                android.graphics.Insets cutout = android.graphics.Insets.of(side[0],side[1],side[2],side[3]);
                android.view.WindowInsets insets = new android.view.WindowInsets(cutout,
                    android.graphics.Insets.of(12,24,12,32),android.graphics.Insets.of(0,0,0,640));
                android.view.WindowInsets remaining = layout.listener.onApplyWindowInsets(layout,insets);
                require(layout.left == side[0] && layout.top == side[1] && layout.right == side[2] && layout.bottom == side[3],
                    "cutout placement/removal kept stale padding");
                require(remaining.cutout.left == 0 && remaining.cutout.top == 0 && remaining.cutout.right == 0 && remaining.cutout.bottom == 0,
                    "SDL received the cutout a second time");
                require(remaining.bars.left == Math.max(0,12-side[0]) && remaining.bars.top == Math.max(0,24-side[1]) &&
                        remaining.bars.right == Math.max(0,12-side[2]) && remaining.bars.bottom == Math.max(0,32-side[3]) &&
                        remaining.ime.bottom == 640-side[3], "remaining system/IME insets lost their content coordinates");
                layout.listener.onApplyWindowInsets(layout,insets);
                require(layout.left == side[0] && layout.top == side[1] && layout.right == side[2] && layout.bottom == side[3],
                    "repeated insets accumulated padding");
            }
        }
        SDLActivity.mBrokenLibraries = true;
        SDLActivity.mLayout = new android.view.ViewGroup();
        activity.onCreate(null);
        require(SDLActivity.mLayout.listener == null, "failed SDL startup installed cutout handling");
        SDLActivity.mBrokenLibraries = false;
        SDLActivity.mLayout = null;
        activity.onCreate(null);
        android.os.Build.VERSION.SDK_INT = 35;
        activity.onBackPressed();
        require(SDLActivity.keys.equals(Arrays.asList(4, -4)) && SDLActivity.systemBack == 0 && !activity.finishing,
            "Back gesture must deliver one SDL Back press/release without closing the game");
        SDLActivity.keys.clear();
        activity.finishing = true;
        activity.onBackPressed();
        require(SDLActivity.keys.isEmpty() && activity.finishes == 0, "finishing Activity accepted Back");
        activity.finishing = false;
        for (boolean broken : new boolean[] { true, false }) {
            SDLActivity.mBrokenLibraries = broken;
            SDLActivity.mActivityCreated = broken;
            OpenQ4Activity failed = new OpenQ4Activity();
            failed.onBackPressed();
            failed.onBackPressed();
            require(failed.finishes == 1 && SDLActivity.keys.isEmpty() && SDLActivity.systemBack == 0,
                "unavailable SDL Back must finish without entering JNI");
        }
        SDLActivity.mBrokenLibraries = false;
        SDLActivity.mActivityCreated = true;
        // Settings > Android setup: one SDL user message opens the launcher
        // without finishing the game; repeats wait for the game to resume.
        require(!activity.onUnhandledMessage(1, null) && SDLActivity.started.isEmpty(), "unrelated SDL message consumed");
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 1 &&
            SDLActivity.started.get(0).target == LauncherActivity.class && SDLActivity.started.get(0).context == activity &&
            !activity.finishing, "Settings request did not open setup over the running game");
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 1,
            "repeated request opened setup twice");
        activity.onResume();
        SDLActivity.startFailure = true;
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 1 &&
            android.widget.Toast.shown.equals(List.of(R.string.setup_open_failed)), "unavailable setup was not reported");
        SDLActivity.startFailure = false;
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 2,
            "a failed request blocked the next one");
        activity.onResume();
        activity.finishing = true;
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 2,
            "finishing game opened setup");
        activity.finishing = false;
        SDLActivity.mBrokenLibraries = true;
        require(activity.onUnhandledMessage(SDLActivity.COMMAND_USER, null) && SDLActivity.started.size() == 2,
            "failed SDL startup opened setup");
        SDLActivity.mBrokenLibraries = false;
        // Applied setup records reach the engine thread; absence never does.
        SDLActivity.preferences.values.clear();
        require(activity.getTouchSetup() == null && activity.getInterfaceSetup() == null, "unconfigured setup reached the engine");
        SDLActivity.preferences.values.put(TouchSettings.SAVED, "1:2:15:8");
        float[] touch = activity.getTouchSetup();
        require(touch != null && touch.length == 3 && touch[0] == 2 && Math.abs(touch[1] - 1.5f) < 1e-6f &&
            Math.abs(touch[2] - 1.15f) < 1e-6f, "applied touch setup not delivered");
        SDLActivity.preferences.values.put(InterfaceSettings.SAVED, "1:0123456789abcdef0123456789abcdef:0:0:0");
        SDLActivity.resources.configuration.fontScale = 1.3f;
        require("1:0123456789abcdef0123456789abcdef:4:100:130".equals(activity.getInterfaceSetup()),
            "applied interface setup not resolved for this phone");
        SDLActivity.resources.configuration.smallestScreenWidthDp = 720;
        require("1:0123456789abcdef0123456789abcdef:5:100:130".equals(activity.getInterfaceSetup()),
            "applied interface setup not resolved for this tablet");
        SDLActivity.preferences.values.put(InterfaceSettings.SAVED, "1:damaged");
        require(activity.getInterfaceSetup() == null, "damaged interface record reached the engine");
        // The player's orientation choice, never SDL's: openQ4's resizable
        // window would make SDL request FULL_USER (13) at window creation.
        SDLActivity.preferences.values.clear();
        activity.onCreate(null);
        require(activity.getRequestedOrientation() == android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
            "unconfigured game did not request landscape");
        org.libsdl.app.SDLActivity.setOrientationOverride(activity, 1920, 1080);
        require(activity.getRequestedOrientation() == android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
            "SDL window creation unlocked portrait");
        int requests = activity.orientationRequests;
        activity.onResume();
        require(activity.orientationRequests == requests, "unchanged orientation requested again");
        SDLActivity.preferences.values.put(InterfaceSettings.SAVED, "2:0123456789abcdef0123456789abcdef:0:0:0:1");
        activity.onResume();
        require(activity.getRequestedOrientation() == android.content.pm.ActivityInfo.SCREEN_ORIENTATION_FULL_USER,
            "applied Landscape and portrait did not reach the returning game");
        SDLActivity.preferences.values.put(InterfaceSettings.SAVED, "1:0123456789abcdef0123456789abcdef:0:0:0");
        org.libsdl.app.SDLActivity.setOrientationOverride(activity, 1080, 1920);
        require(activity.getRequestedOrientation() == android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
            "version 1 setup record or a portrait window left portrait unlocked");
        SDLActivity.preferences.values.put(InterfaceSettings.SAVED, "2:damaged");
        activity.onResume();
        require(activity.getRequestedOrientation() == android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
            "damaged setup record unlocked portrait");
        SDLActivity.preferences.values.clear();
        File old = extract(activity);
        List<String> launch = Arrays.asList(activity.getArguments());
        require(!launch.contains("sys_lang"), "ordinary launch must preserve the game's saved language");
        SDLActivity.language = "fr";
        List<String> localized = Arrays.asList(activity.getArguments());
        require(localized.get(localized.indexOf("sys_lang") + 1).equals("french"), "setup language did not reach engine");
        require("fr".equals(LauncherActivity.applied), "prepared engine arguments did not acknowledge setup language");
        SDLActivity.language = "+quit";
        require(!Arrays.asList(activity.getArguments()).contains("sys_lang"), "unrecognized language became engine arguments");
        SDLActivity.language = null;
        int saveArgument = launch.indexOf("fs_savepath");
        require(saveArgument >= 0 && saveArgument + 1 < launch.size(), "host omitted save path");
        require(launch.get(saveArgument + 1).equals(android.system.Os.environment.get("OPENQ4_SAVE_ROOT")),
            "early native diagnostics and engine saves use different roots");
        require(new File(launch.get(saveArgument + 1)).isDirectory(), "save root must exist before native startup");
        require(new File(old, ".ready").isFile(), "successful extraction must mark ready");
        require(Files.readString(old.toPath().resolve("baseoq4/pak1.pk4")).equals("second pack"), "pack content");
        SDLActivity.assets.fail = "baseoq4/pak1.pk4";
        int opened = SDLActivity.assets.opens;
        require(extract(activity).equals(old), "ready package must avoid recopying");
        require(SDLActivity.assets.opens == opened, "healthy package must not recopy any APK data");
        android.system.Os.failRename = ".ready";
        require(extract(activity).equals(old), "healthy verified package must not require another metadata write");
        android.system.Os.failRename = null;
        Path extracted = old.toPath().resolve("baseoq4/pak0.pk4");
        boolean verifyCancelled = false;
        try {
            PackageStorage.extract(SDLActivity.assets, SDLActivity.files, (phase, name, bytes, total) ->
                !(phase == PackageStorage.Phase.VERIFYING && name.equals("pak1.pk4") && bytes > 0));
        } catch (InterruptedIOException expected) { verifyCancelled = true; }
        require(verifyCancelled && new File(old, ".ready").isFile(),
            "cancelling a healthy-package check must preserve readiness");
        for (int stopAt : new int[] {1, 2}) {
            write(extracted, "damaged");
            int[] completedCallbacks = {0};
            boolean copyCancelled = false;
            try {
                PackageStorage.extract(SDLActivity.assets, SDLActivity.files, (phase, name, bytes, total) -> {
                    require(bytes >= 0 && bytes <= total, "package progress outside file bounds");
                    if (phase == PackageStorage.Phase.COPYING && name.equals("pak0.pk4") && total > 0 && bytes == total) {
                        return ++completedCallbacks[0] != stopAt;
                    }
                    return true;
                });
            } catch (InterruptedIOException expected) { copyCancelled = true; }
            require(copyCancelled && completedCallbacks[0] == stopAt &&
                    Files.readString(extracted).equals("damaged") && !new File(old, ".ready").exists(),
                "cancellation during copy or after sync must precede promotion and invalidate repair readiness");
            extract(activity);
            require(Files.readString(extracted).equals("first pack") && new File(old, ".ready").isFile(),
                "cancelled preparation did not retry");
        }
        int beforeInterrupted = SDLActivity.assets.opens;
        Thread.currentThread().interrupt();
        try {
            try { extract(activity); throw new AssertionError("interrupted package preparation continued"); }
            catch (InterruptedIOException expected) {
                require(Thread.currentThread().isInterrupted() && SDLActivity.assets.opens == beforeInterrupted,
                    "interrupted preparation cleared its flag or opened assets");
            }
        } finally { Thread.interrupted(); }
        for (String corrupt : new String[] { "bad bytes!", "short", "first pack with trailing garbage" }) {
            write(extracted, corrupt);
            extract(activity);
            require(Files.readString(extracted).equals("first pack"), "repair corruption despite ready marker");
        }
        Files.delete(extracted);
        extract(activity);
        require(Files.readString(extracted).equals("first pack"), "repair missing file despite ready marker");
        write(extracted, "damaged");
        SDLActivity.assets.fail = "baseoq4/pak0.pk4";
        expectFailure(activity, "interrupted repair must not launch");
        require(Files.readString(extracted).equals("damaged"), "failed copy must not overwrite destination");
        require(!new File(old, ".ready").exists(), "failed repair must invalidate readiness");
        SDLActivity.assets.fail = null;
        android.system.Os.failRename = "pak0.pk4";
        expectFailure(activity, "failed commit must not launch");
        require(Files.readString(extracted).equals("damaged"), "failed commit must preserve destination");
        android.system.Os.failRename = null;
        extract(activity);
        require(Files.readString(extracted).equals("first pack"), "retry repair after failed commit");
        write(old.toPath().resolve("baseoq4/unexpected.cfg"), "unmanaged");
        expectFailure(activity, "unlisted package override must not reach engine");
        require(Files.readString(old.toPath().resolve("baseoq4/unexpected.cfg")).equals("unmanaged"), "unknown content preserved");
        Files.delete(old.toPath().resolve("baseoq4/unexpected.cfg"));
        // A poisoned APK source must also fail before atomic replacement.
        write(extracted, "damaged");
        for (String poisoned : new String[] { "other data", "", "longer than the declared first pack" }) {
            write(assets.resolve("baseoq4/pak0.pk4"), poisoned);
            expectFailure(activity, "incorrect APK hash or length must be rejected");
            require(Files.readString(extracted).equals("damaged"), "incorrect source must not replace destination");
        }
        write(assets.resolve("baseoq4/pak0.pk4"), "first pack");
        extract(activity);
        // A content revision must change when bytes change, not from the marker.
        write(assets.resolve("baseoq4/pak1.pk4"), "new second pack");
        String second = manifest(assets);
        require(!first.equals(second), "content addressed upgrade");
        SDLActivity.assets.fail = "baseoq4/pak1.pk4";
        boolean failed = false;
        try { extract(activity); } catch (IOException expected) { failed = true; }
        require(failed, "fixture must interrupt upgrade");
        require(new File(old, ".ready").isFile(), "failed upgrade must retain prior package");
        require(!root.resolve("files/packages/" + second + "/.ready").toFile().exists(), "partial upgrade must not be ready");
        SDLActivity.assets.fail = null;
        File current = extract(activity);
        require(new File(current, ".ready").isFile(), "retry must complete");
        require(!old.exists(), "completed upgrade must remove old generated package");
        require(Files.readString(root.resolve("files/saves/baseoq4/test.save")).equals("player save"), "save preservation");
        require(root.resolve("files/packages/keep/test.txt").toFile().isFile(), "unmanaged content preservation");
        // Execute the real preflight with an exhausted-volume result. A stale
        // generated temporary is reclaimable; the existing destination is not.
        File emptyVolume = new File(root.toString()) { public long getUsableSpace() { return 0; } };
        File pending = new File(current, ".space-probe.tmp");
        try {
            PackageStorage.requireRepairSpace(emptyVolume, pending, 10);
            throw new AssertionError("insufficient repair space must be rejected");
        } catch (PackageStorage.InsufficientSpace expected) {
            require(expected.requiredBytes == 10 && expected.availableBytes == 0,
                "localized low-space result must retain required and available bytes");
        }
        write(pending.toPath(), "0123456789");
        PackageStorage.requireRepairSpace(emptyVolume, pending, 10);
        Files.delete(pending.toPath());
        Path outside = root.resolve("outside");
        write(outside.resolve("sentinel"), "outside package tree");
        // The cleanup guard relies on getCanonicalFile() resolving symlinks, as
        // it does on Android. A Windows host JVM does not resolve them, so it
        // cannot model that guard; Linux and macOS hosts still exercise it.
        if (System.getProperty("os.name", "").startsWith("Windows")) {
            System.out.println("SKIP: a Windows JVM does not canonicalize through symlinks as Android does");
        } else try {
            Files.createSymbolicLink(root.resolve("files/packages/" + "c".repeat(64)), outside);
            extract(activity);
            require(outside.resolve("sentinel").toFile().isFile(), "cleanup must not follow symlinks");
            Path target = current.toPath().resolve("baseoq4/pak0.pk4");
            Files.delete(target);
            Files.createSymbolicLink(target, outside.resolve("sentinel"));
            expectFailure(activity, "active package file links must fail before read/write");
            require(Files.readString(outside.resolve("sentinel")).equals("outside package tree"), "linked file preservation");
            Files.delete(target);
            Path temporaryLink = current.toPath().resolve(".repair-2.tmp");
            Files.createSymbolicLink(temporaryLink, outside.resolve("sentinel"));
            expectFailure(activity, "temporary file links must fail before truncation");
            require(Files.readString(outside.resolve("sentinel")).equals("outside package tree"), "linked temporary preservation");
            Files.delete(temporaryLink);
            extract(activity);
            Path parent = current.toPath().resolve("baseoq4/nested");
            Files.delete(parent.resolve("data.txt"));
            Files.delete(parent);
            Files.createSymbolicLink(parent, outside);
            expectFailure(activity, "nested directory links must fail before creation");
            require(!Files.exists(outside.resolve("data.txt")), "no write through directory link");
            Files.delete(parent);
            extract(activity);
            Path linkedFiles = root.resolve("linked-files");
            Files.createDirectories(linkedFiles);
            Files.createSymbolicLink(linkedFiles.resolve("packages"), outside);
            try {
                PackageStorage.extract(SDLActivity.assets, linkedFiles.toFile());
                throw new AssertionError("package parent link must be rejected");
            } catch (IOException expected) {}
        } catch (UnsupportedOperationException | FileSystemException unsupported) {
            System.out.println("SKIP: this host cannot create directory symlinks");
        }
        String goodManifest = Files.readString(assets.resolve("openq4-package-manifest.txt"));
        for (String bad : new String[] {
                goodManifest.replace("pak0.pk4", "../saves/test.save"),
                goodManifest.replace("pak0.pk4", "/tmp/escape"),
                goodManifest.replace("pak0.pk4", "nested/../../escape"),
                goodManifest.replace("pak0.pk4", "pak1.pk4"),
                goodManifest.replace("\t10\t", "\t9999999999\t"),
                goodManifest.replace("openQ4-package-v1", "openQ4-package-v2"),
                goodManifest + "extra", "x".repeat(1024 * 1024 + 1) }) {
            setManifest(assets, bad);
            expectFailure(activity, "malformed manifest must be rejected");
            require(new File(current, ".ready").isFile(), "bad metadata preserves verified version");
        }
        manifest(assets);
        write(assets.resolve("openq4-package-manifest.txt"), goodManifest + "tamper");
        expectFailure(activity, "manifest revision mismatch must be rejected");
        write(assets.resolve("openq4-package-version.txt"), "a".repeat(65));
        expectFailure(activity, "revision read is bounded");
        write(assets.resolve("openq4-package-version.txt"), "../saves");
        failed = false;
        try { extract(activity); } catch (IOException expected) { failed = true; }
        require(failed, "invalid package revision must be rejected");
        require(new File(current, ".ready").isFile(), "invalid revision must not damage current package");
        java.util.concurrent.CountDownLatch entered = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.CountDownLatch exitNative = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.atomic.AtomicReference<Throwable> nativeError = new java.util.concurrent.atomic.AtomicReference<>();
        SDLActivity.nativeWork = () -> {
            entered.countDown();
            try { require(exitNative.await(10, java.util.concurrent.TimeUnit.SECONDS), "native fixture timed out"); }
            catch (InterruptedException error) { throw new RuntimeException(error); }
        };
        Thread nativeThread = new Thread(() -> { try { activity.main(); } catch (Throwable error) { nativeError.set(error); } });
        nativeThread.start();
        require(entered.await(10, java.util.concurrent.TimeUnit.SECONDS), "Activity did not enter native main");
        activity.onDestroy();
        require(StorageSession.INSTANCE.gameActive() && !StorageSession.INSTANCE.activityActive(),
                "Activity destruction prematurely released native storage");
        try { StorageSession.INSTANCE.beginSetup(); throw new AssertionError("setup overlapped live native main"); }
        catch (StorageSession.Busy expected) {}
        exitNative.countDown(); nativeThread.join(10000);
        require(!nativeThread.isAlive() && nativeError.get() == null && !StorageSession.INSTANCE.gameActive(),
                "native completion failed to release Activity storage owner");
        Object lease = StorageSession.INSTANCE.beginSetup(); StorageSession.INSTANCE.endSetup(lease);
        System.out.println("PASS: real Activity verified reuse, missing/truncated/corrupt repair, interrupted copy/commit, manifest bounds, upgrade cleanup, save preservation, matching native/engine save roots, Back routing, cutout/system/IME inset recovery, setup language selection, Settings setup re-entry, applied setup delivery and native storage ownership through delayed shutdown");
    }
}''',
        }
        sources["com/darkmatter/openq4/StoragePackageProbe.java"] = r'''
package com.darkmatter.openq4;
import android.content.res.AssetManager;
import java.io.*;
import java.nio.file.*;
public final class StoragePackageProbe {
    public static void main(String[] args) throws Exception {
        File fixture = new File(args[0]);
        AssetManager assets = new AssetManager();
        assets.root = new File(fixture, "assets");
        File files = new File(fixture, "files");
        File root = PackageStorage.extract(assets, files);
        int copies = assets.opens;
        for (int i = 0; i < 2; ++i) {
            PackageStorage.extract(assets, files);
            if (assets.opens != copies) throw new AssertionError("healthy large package was recopied");
        }
        File pack = new File(root, "baseoq4/pak0.pk4");
        try (RandomAccessFile output = new RandomAccessFile(pack, "rw")) {
            output.seek(17); int value = output.read(); output.seek(17); output.write(value ^ 255);
        }
        PackageStorage.extract(assets, files);
        if (assets.opens != copies + 1) throw new AssertionError("repair must copy only the damaged pack");
        PackageStorage.extract(assets, files);
        if (assets.opens != copies + 1) throw new AssertionError("repaired bytes failed verification");
        System.out.println("PASS: actual staged package extraction, two verified reuses, same-size PK4 corruption and single-file repair");
    }
}'''
        source_paths = []
        for relative, contents in sources.items():
            path = scratch / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
            source_paths.append(str(path))
        # Compile this fixture's production dependency closure; document-picker
        # and retail-import Android APIs have separate executable fixtures.
        source_paths.extend(str(root / "mobile/android/app/src/main/java/com/darkmatter/openq4" / name)
                            for name in ("OpenQ4Activity.java", "PackageStorage.java", "StorageSession.java", "SetupLanguage.java",
                                         "TouchSettings.java", "InterfaceSettings.java", "TouchLayout.java"))
        classes = scratch / "classes"
        classes.mkdir()
        javac = Path(args.javac).resolve()
        java = javac.with_name("java.exe" if os.name == "nt" else "java")
        subprocess.run([str(javac), "--release", "17", "-d", str(classes), *source_paths], check=True)
        subprocess.run([str(java), "-cp", str(classes), "com.darkmatter.openq4.StorageTest", str(scratch / "fixture")], check=True)
        if args.package_source:
            sys.path.insert(0, str(root / "tools/build"))
            from android_package import package_manifest
            assets = scratch / "full-package/assets"
            base = assets / "baseoq4"
            base.mkdir(parents=True)
            for name in ("pak0.pk4", "pak1.pk4", "pak2.pk4", "mod.json"):
                shutil.copyfile(args.package_source.resolve() / name, base / name)
            manifest = package_manifest(base)
            (assets / "openq4-package-manifest.txt").write_bytes(manifest)
            (assets / "openq4-package-version.txt").write_text(hashlib.sha256(manifest).hexdigest(), encoding="ascii")
            print("Real-package manifest:", manifest.decode("ascii"), flush=True)
            subprocess.run([str(java), "-cp", str(classes), "com.darkmatter.openq4.StoragePackageProbe",
                            str(scratch / "full-package")], check=True)


if __name__ == "__main__":
    main()
