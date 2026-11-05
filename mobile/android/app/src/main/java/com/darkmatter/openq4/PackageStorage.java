// SPDX-License-Identifier: GPL-3.0-or-later
package com.darkmatter.openq4;

import android.content.res.AssetManager;
import android.system.ErrnoException;
import android.system.Os;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Verified, repairable copies of APK-owned overlays. Never owns retail data or saves. */
final class PackageStorage {
    private static final int MAX_MANIFEST_BYTES = 1024 * 1024;
    private static final int MAX_FILES = 4096;
    private static final long MAX_FILE_BYTES = 2L * 1024 * 1024 * 1024;
    private static final long MAX_PACKAGE_BYTES = 4L * 1024 * 1024 * 1024;

    enum Phase { VERIFYING, COPYING }
    interface Progress {
        boolean update(Phase phase, String name, long bytes, long totalBytes);
    }

    static final class InsufficientSpace extends IOException {
        final long requiredBytes, availableBytes;
        InsufficientSpace(long requiredBytes, long availableBytes) {
            super("Insufficient storage for package repair");
            this.requiredBytes = requiredBytes;
            this.availableBytes = availableBytes;
        }
    }

    private static void progress(Progress observer, Phase phase, String name, long bytes, long total)
            throws InterruptedIOException {
        if (Thread.currentThread().isInterrupted() ||
                (observer != null && !observer.update(phase, name, bytes, total))) {
            throw new InterruptedIOException("Package preparation cancelled");
        }
    }

    private static final class Entry {
        final String name, hash;
        final long size;
        Entry(String name, String hash, long size) {
            this.name = name;
            this.hash = hash;
            this.size = size;
        }
    }

    private static MessageDigest sha256() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException unavailable) { throw new AssertionError(unavailable); }
    }

    private static String hex(byte[] bytes) {
        char[] result = new char[bytes.length * 2];
        char[] digits = "0123456789abcdef".toCharArray();
        for (int i = 0; i < bytes.length; ++i) {
            result[i * 2] = digits[(bytes[i] & 255) >>> 4];
            result[i * 2 + 1] = digits[bytes[i] & 15];
        }
        return new String(result);
    }

    private static byte[] readAsset(AssetManager assets, String name, int limit) throws IOException {
        try (InputStream input = assets.open(name)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (count > limit - output.size()) throw new IOException("Package metadata exceeds limit: " + name);
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        }
    }

    private static List<Entry> parseManifest(byte[] bytes) throws IOException {
        // This is a canonical ASCII format shared with the APK verifier. No
        // platform separators, escapes, traversal, aliases or duplicate paths.
        for (byte value : bytes) {
            if (value < 0 || value == 0 || value == '\r') throw new IOException("Invalid package manifest encoding");
        }
        String[] lines = new String(bytes, StandardCharsets.US_ASCII).split("\n", -1);
        if (lines.length < 5 || lines.length > MAX_FILES + 2 ||
                !lines[0].equals("openQ4-package-v1") || !lines[lines.length - 1].isEmpty()) {
            throw new IOException("Invalid package manifest header or file count");
        }
        List<Entry> entries = new ArrayList<>();
        Set<String> names = new HashSet<>();
        String previous = "";
        long total = 0;
        for (int i = 1; i < lines.length - 1; ++i) {
            String[] fields = lines[i].split("\t", -1);
            if (fields.length != 3 || !fields[0].matches("[0-9a-f]{64}") ||
                    !fields[1].matches("0|[1-9][0-9]{0,9}")) {
                throw new IOException("Invalid package manifest entry");
            }
            String name = fields[2];
            if (name.length() > 240 || !name.matches("[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*") ||
                    name.compareTo(previous) <= 0) throw new IOException("Invalid package path: " + name);
            String[] components = name.split("/");
            if (components.length > 16) throw new IOException("Package path is too deep");
            for (String part : components) {
                if (part.equals(".") || part.equals("..")) throw new IOException("Invalid package path: " + name);
            }
            long size = Long.parseLong(fields[1]);
            total += size;
            if (size > MAX_FILE_BYTES || total > MAX_PACKAGE_BYTES) throw new IOException("Package size exceeds limit");
            entries.add(new Entry(name, fields[0], size));
            names.add(name);
            previous = name;
        }
        // Keep in step with tools/build/content_packs.json (tools/tests/android_build.py checks it).
        for (String required : new String[] { "mod.json", "pak0.pk4", "pak1.pk4", "pak2.pk4" }) {
            if (!names.contains(required)) throw new IOException("Missing package manifest entry: " + required);
        }
        for (String name : names) {
            int slash = name.indexOf('/');
            while (slash >= 0) {
                if (names.contains(name.substring(0, slash))) throw new IOException("Package file/directory conflict");
                slash = name.indexOf('/', slash + 1);
            }
        }
        return entries;
    }

    private static void requireUnlinked(File path) throws IOException {
        if (!path.getAbsoluteFile().equals(path.getCanonicalFile())) {
            throw new IOException("Unexpected package link: " + path);
        }
    }

    private static void requireDirectory(File directory) throws IOException {
        requireUnlinked(directory);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("Cannot create package directory: " + directory);
        requireUnlinked(directory);
    }

    private static boolean matches(File file, Entry entry, Progress observer) throws IOException {
        progress(observer, Phase.VERIFYING, entry.name, 0, entry.size);
        requireUnlinked(file);
        if (!file.isFile() || file.length() != entry.size) return false;
        MessageDigest digest = sha256();
        long count = 0;
        try (InputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) != -1) {
                count += read;
                if (count > entry.size) return false;
                digest.update(buffer, 0, read);
                progress(observer, Phase.VERIFYING, entry.name, count, entry.size);
            }
        }
        return count == entry.size && hex(digest.digest()).equals(entry.hash);
    }

    private static void validateTree(File directory, String prefix, Set<String> files, Set<String> directories)
            throws IOException {
        requireUnlinked(directory);
        File[] children = directory.listFiles();
        if (children == null || children.length > MAX_FILES) throw new IOException("Cannot enumerate package content");
        for (File child : children) {
            requireUnlinked(child);
            String name = prefix + child.getName();
            if (child.isDirectory() && directories.contains(name)) {
                validateTree(child, name + "/", files, directories);
            } else if (!child.isFile() || !files.contains(name)) {
                // Never erase unexpected content to make a check pass.
                throw new IOException("Unexpected extracted package content: " + name);
            }
        }
    }

    private static void repair(AssetManager assets, File root, File target, Entry entry, int index, Progress observer)
            throws IOException, ErrnoException {
        requireDirectory(target.getParentFile());
        // Keep incomplete writes outside baseoq4, which the engine enumerates.
        File temporary = new File(root, ".repair-" + index + ".tmp");
        requireUnlinked(temporary);
        if (temporary.exists() && !temporary.isFile()) throw new IOException("Invalid package repair target");
        // Keep the previous package and damaged destination until a full,
        // authenticated replacement has reached durable storage.
        requireRepairSpace(root, temporary, entry.size);
        progress(observer, Phase.COPYING, entry.name, 0, entry.size);
        MessageDigest digest = sha256();
        long count = 0;
        try (InputStream input = assets.open("baseoq4/" + entry.name);
             FileOutputStream output = new FileOutputStream(temporary)) {
            byte[] buffer = new byte[65536];
            int read;
            while ((read = input.read(buffer)) != -1) {
                count += read;
                if (count > entry.size) throw new IOException("Packaged asset exceeds declared size: " + entry.name);
                digest.update(buffer, 0, read);
                output.write(buffer, 0, read);
                progress(observer, Phase.COPYING, entry.name, count, entry.size);
            }
            if (count != entry.size || !hex(digest.digest()).equals(entry.hash)) {
                throw new IOException("Packaged asset failed verification: " + entry.name);
            }
            output.getFD().sync();
        }
        progress(observer, Phase.COPYING, entry.name, count, entry.size);
        requireUnlinked(target);
        Os.rename(temporary.getAbsolutePath(), target.getAbsolutePath());
    }

    static void requireRepairSpace(File directory, File temporary, long bytes) throws IOException {
        // This is an early estimate, not a reservation. Writes, sync and rename
        // still have to succeed if available space changes during the copy.
        long required = Math.max(0, bytes - temporary.length());
        long available = directory.getUsableSpace();
        if (available < required) throw new InsufficientSpace(required, available);
    }

    static File extract(AssetManager assets, File files) throws IOException, ErrnoException {
        return extract(assets, files, null);
    }

    static synchronized File extract(AssetManager assets, File files, Progress observer) throws IOException, ErrnoException {
        progress(null, Phase.VERIFYING, "", 0, 0);
        String version = new String(readAsset(assets, "openq4-package-version.txt", 64), StandardCharsets.US_ASCII);
        if (!version.matches("[0-9a-f]{64}")) throw new IOException("Invalid packaged content revision");
        byte[] manifest = readAsset(assets, "openq4-package-manifest.txt", MAX_MANIFEST_BYTES);
        if (!hex(sha256().digest(manifest)).equals(version)) throw new IOException("Package manifest revision mismatch");
        List<Entry> entries = parseManifest(manifest);

        // Normalize only Android's trusted /data/user/0 storage alias; reject
        // links anywhere beneath it before reading, writing or cleaning up.
        File packages = new File(files.getCanonicalFile(), "packages");
        requireDirectory(packages);
        File root = new File(packages, version);
        requireDirectory(root);
        File ready = new File(root, ".ready");
        requireUnlinked(ready);
        boolean invalidated = false;
        Set<String> fileNames = new HashSet<>(), directoryNames = new HashSet<>();
        for (int i = 0; i < entries.size(); ++i) {
            Entry entry = entries.get(i);
            fileNames.add(entry.name);
            for (int slash = entry.name.indexOf('/'); slash >= 0; slash = entry.name.indexOf('/', slash + 1)) {
                directoryNames.add(entry.name.substring(0, slash));
            }
            File target = new File(root, "baseoq4/" + entry.name);
            if (!matches(target, entry, observer)) {
                if (!invalidated && ready.exists() && !ready.delete()) throw new IOException("Cannot invalidate content readiness");
                invalidated = true;
                repair(assets, root, target, entry, i, observer);
            }
        }
        validateTree(new File(root, "baseoq4"), "", fileNames, directoryNames);
        // A ready marker is a completion record, never a substitute for the
        // per-file checks above. A crash before rename is harmless on retry.
        byte[] readyBytes = version.getBytes(StandardCharsets.US_ASCII);
        if (!matches(ready, new Entry(".ready", hex(sha256().digest(readyBytes)), readyBytes.length), null)) {
            File temporary = new File(root, ".ready.tmp");
            requireUnlinked(temporary);
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                output.write(readyBytes);
                output.getFD().sync();
            }
            progress(null, Phase.VERIFYING, "", 0, 0);
            Os.rename(temporary.getAbsolutePath(), ready.getAbsolutePath());
        }
        File[] previousPackages = packages.listFiles();
        if (previousPackages != null) {
            for (File previous : previousPackages) {
                if (!previous.getName().equals(version) && previous.getName().matches("[0-9a-f]{64}")) {
                    deleteGeneratedPackage(previous);
                }
            }
        }
        progress(null, Phase.VERIFYING, "", 0, 0);
        return root;
    }

    private static void deleteGeneratedPackage(File file) {
        if (Thread.currentThread().isInterrupted()) return;
        try { requireUnlinked(file); }
        catch (IOException error) { return; }
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children == null) return;
            for (File child : children) deleteGeneratedPackage(child);
        }
        // Cleanup failures do not invalidate the newly verified package.
        file.delete();
    }
}
