#!/usr/bin/env python3
"""Run the real Android Activity extraction code against temporary JVM storage.

Small Android/SDL API stand-ins replace the unavailable device, while the
production Activity handles extraction, retry, readiness and upgrade cleanup.
Requires a JDK; all fixture files remain in the repository's .tmp directory.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    java_home = os.environ.get("JAVA_HOME", "")
    parser.add_argument("--javac", default=shutil.which("javac") or
                        (str(Path(java_home) / "bin/javac") if java_home else None))
    args = parser.parse_args()
    if not args.javac:
        parser.error("a JDK is required (--javac or JAVA_HOME)")
    root = Path(__file__).resolve().parents[2]
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
    public String[] list(String path) { return new File(root, path).list(); }
    public InputStream open(String path) throws IOException {
        InputStream source = new FileInputStream(new File(root, path));
        if (!path.equals(fail)) return source;
        return new FilterInputStream(source) {
            public int read(byte[] b, int off, int len) throws IOException {
                throw new IOException("Injected interrupted extraction");
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
    public static void setenv(String key, String value, boolean overwrite) throws ErrnoException {}
    public static void rename(String from, String to) throws ErrnoException {
        try { Files.move(Path.of(from), Path.of(to), StandardCopyOption.REPLACE_EXISTING); }
        catch (Exception error) { throw new ErrnoException(error); }
    }
}''',
            "org/libsdl/app/SDLActivity.java": r'''
package org.libsdl.app;
import java.io.File;
import android.content.res.AssetManager;
public class SDLActivity {
    public static File files;
    public static AssetManager assets;
    protected String[] getLibraries() { return new String[0]; }
    protected String[] getArguments() { return new String[0]; }
    public File getExternalFilesDir(String type) { return new File(files, "retail"); }
    public File getFilesDir() { return files; }
    public File getCacheDir() { return new File(files, "cache"); }
    public AssetManager getAssets() { return assets; }
    public AppInfo getApplicationInfo() { return new AppInfo(); }
    public Intent getIntent() { return new Intent(); }
    public static class AppInfo { public String nativeLibraryDir = "/fixture/native"; }
    public static class Intent { public boolean getBooleanExtra(String key, boolean value) { return value; } }
}''',
            "com/darkmatter/openq4/StorageTest.java": r'''
package com.darkmatter.openq4;
import android.content.res.AssetManager;
import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
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
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        // Android's trusted app-storage prefix may itself be an alias. The
        // package parent must normalize before checking child symlink escapes.
        SDLActivity.files = root.resolve("files/../files").toFile();
        SDLActivity.assets = new AssetManager();
        SDLActivity.assets.root = root.resolve("assets").toFile();
        Path assets = SDLActivity.assets.root.toPath();
        String first = "a".repeat(64), second = "b".repeat(64);
        write(assets.resolve("openq4-package-version.txt"), first);
        write(assets.resolve("baseoq4/pak0.pk4"), "first pack");
        write(assets.resolve("baseoq4/pak1.pk4"), "second pack");
        write(root.resolve("files/saves/baseoq4/test.save"), "player save");
        write(root.resolve("files/packages/keep/test.txt"), "unmanaged");
        OpenQ4Activity activity = new OpenQ4Activity();
        File old = extract(activity);
        require(new File(old, ".ready").isFile(), "successful extraction must mark ready");
        require(Files.readString(old.toPath().resolve("baseoq4/pak1.pk4")).equals("second pack"), "pack content");
        SDLActivity.assets.fail = "baseoq4/pak1.pk4";
        require(extract(activity).equals(old), "ready package must avoid recopying");
        write(assets.resolve("openq4-package-version.txt"), second);
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
        Path outside = root.resolve("outside");
        write(outside.resolve("sentinel"), "outside package tree");
        try {
            Files.createSymbolicLink(root.resolve("files/packages/" + "c".repeat(64)), outside);
            extract(activity);
            require(outside.resolve("sentinel").toFile().isFile(), "cleanup must not follow symlinks");
        } catch (UnsupportedOperationException | FileSystemException unsupported) {
            System.out.println("SKIP: this host cannot create directory symlinks");
        }
        write(assets.resolve("openq4-package-version.txt"), "../saves");
        failed = false;
        try { extract(activity); } catch (IOException expected) { failed = true; }
        require(failed, "invalid package revision must be rejected");
        require(new File(current, ".ready").isFile(), "invalid revision must not damage current package");
        System.out.println("PASS: real Activity extraction, ready reuse, failed upgrade recovery, cleanup and save preservation");
    }
}''',
        }
        source_paths = []
        for relative, contents in sources.items():
            path = scratch / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
            source_paths.append(str(path))
        source_paths.append(str(root / "mobile/android/app/src/main/java/com/darkmatter/openq4/OpenQ4Activity.java"))
        classes = scratch / "classes"
        classes.mkdir()
        javac = Path(args.javac).resolve()
        java = javac.with_name("java.exe" if os.name == "nt" else "java")
        subprocess.run([str(javac), "--release", "17", "-d", str(classes), *source_paths], check=True)
        subprocess.run([str(java), "-cp", str(classes), "com.darkmatter.openq4.StorageTest", str(scratch / "fixture")], check=True)


if __name__ == "__main__":
    main()
