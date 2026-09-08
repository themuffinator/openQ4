// Standalone host for emileb's Android/GLES support in openQ4.
// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.content.res.AssetManager;
import android.system.ErrnoException;
import android.system.Os;
import org.libsdl.app.SDLActivity;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/** SDL owns the Activity lifecycle; Meson owns all native compilation. */
public final class OpenQ4Activity extends SDLActivity {
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
            File contentRoot = extractPackage();
            Os.setenv("OPENQ4_NATIVE_LIBS", getApplicationInfo().nativeLibraryDir, true);
            Os.setenv("OPENQ4_CONTENT_ROOT", contentRoot.getAbsolutePath(), true);
            boolean multiplayer = getIntent().getBooleanExtra("multiplayer", false);
            return new String[] {
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
            };
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
        String version;
        try (InputStream input = getAssets().open("openq4-package-version.txt")) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[128];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            version = new String(output.toByteArray(), StandardCharsets.US_ASCII).trim();
        }
        if (!version.matches("[0-9a-f]{64}")) {
            throw new IOException("Invalid packaged content revision");
        }
        // Android may expose /data/user/0 through an alias of /data/data.
        // Normalize that trusted parent once so cleanup can reject symlinks
        // inside packages without mistaking the app storage alias for one.
        File packages = new File(getFilesDir(), "packages").getCanonicalFile();
        File root = new File(packages, version);
        File ready = new File(root, ".ready");
        if (!ready.isFile()) {
            requireDirectory(root);
            copyAssetTree(getAssets(), "baseoq4", new File(root, "baseoq4"));
            if (!ready.createNewFile() && !ready.isFile()) {
                throw new IOException("Cannot mark content package complete");
            }
        }
        // Retain the previous version until extraction succeeds. Once the new
        // package is complete, old generated copies must not consume another
        // ~640 MB on every upgrade. Saves live in a different directory.
        File[] previousPackages = packages.listFiles();
        if (previousPackages != null) {
            for (File previous : previousPackages) {
                if (!previous.getName().equals(version) &&
                        previous.getName().matches("[0-9a-f]{64}")) {
                    deleteGeneratedPackage(previous);
                }
            }
        }
        return root;
    }

    private static void deleteGeneratedPackage(File file) {
        // Never follow a symlink out of the app-owned package directory. An
        // unexpected entry is left alone; cleanup must not prevent startup.
        try {
            if (!file.getCanonicalFile().equals(file.getAbsoluteFile())) return;
        } catch (IOException error) {
            return;
        }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) return;
            for (File child : children) deleteGeneratedPackage(child);
        }
        // A failed delete can be retried on the next launch. It is safe to use
        // the new package even if an old generated file remains.
        file.delete();
    }

    private static void copyAssetTree(AssetManager assets, String source, File target)
            throws IOException, ErrnoException {
        String[] children = assets.list(source);
        if (children != null && children.length > 0) {
            requireDirectory(target);
            for (String child : children) {
                copyAssetTree(assets, source + "/" + child, new File(target, child));
            }
            return;
        }
        requireDirectory(target.getParentFile());
        File temporary = new File(target.getPath() + ".tmp");
        try (InputStream input = assets.open(source);
             FileOutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[65536];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            output.getFD().sync();
        }
        Os.rename(temporary.getAbsolutePath(), target.getAbsolutePath());
    }
}
