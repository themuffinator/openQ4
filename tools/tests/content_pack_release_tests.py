#!/usr/bin/env python3
"""Unit tests for the content-pack release tooling (roadmap PKG-01, PKG-02).

Covers content_pack_release.py (build/reuse, notes, verify-update,
verify-identity), the prebuilt-pack path of build_openq4_pack.py, and the
packager's rootless update archive and Windows symbol split, on small
synthetic trees so it runs in CI without the real 800 MB of content.
"""
from __future__ import annotations

import importlib.util
import io
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "tools" / "build"
sys.path.insert(0, str(BUILD))

import openq4_pak  # noqa: E402


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[name] = module  # dataclasses look their module up while the class is built
    spec.loader.exec_module(module)
    return module


RELEASE = load("content_pack_release_under_test", BUILD / "content_pack_release.py")
PACKAGER = load("package_nightly_under_test", BUILD / "package_nightly.py")


def run(*args: object) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, *map(str, args)], capture_output=True, text=True, cwd=ROOT)


def write(path: Path, data: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode("utf-8") if isinstance(data, str) else data)


def make_sources(root: Path, salt: str = "") -> None:
    for spec in openq4_pak.OPENQ4_CONTENT_PACKS:
        for relative in sorted(spec.required_files) or ["placeholder.txt"]:
            write(root / spec.source / relative, f"{spec.name}:{relative}{salt if spec.bulk else ''}\n")


def make_zip(path: Path, files: dict[str, bytes], root_folder: str = "") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in files.items():
            archive.writestr(f"{root_folder}/{name}" if root_folder else name, data)
    return path


class ContentPackReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / ".tmp").mkdir(exist_ok=True)
        self.work = Path(tempfile.mkdtemp(prefix="content-pack-release-", dir=ROOT / ".tmp"))

    def tearDown(self) -> None:
        shutil.rmtree(self.work, ignore_errors=True)

    def build(self, source: Path, out: Path, tag: str, previous: Path | None = None) -> dict:
        args = [BUILD / "content_pack_release.py", "build", "--source-root", source, "--out-dir", out, "--version-tag", tag]
        if previous is not None:
            args += ["--previous-manifest", previous / "content-packs.json", "--previous-packs-dir", previous]
        result = run(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads((out / "content-packs.json").read_text(encoding="utf-8"))

    def test_build_reuses_unchanged_packs_and_rebuilds_changed_ones(self) -> None:
        source = self.work / "source"
        make_sources(source)
        first = self.build(source, self.work / "r1", "0.20.0")
        self.assertEqual([pack["name"] for pack in first["packs"]], list(openq4_pak.OPENQ4_PACK_NAMES))
        second = self.build(source, self.work / "r2", "0.20.1", previous=self.work / "r1")
        for before, after in zip(first["packs"], second["packs"]):
            self.assertEqual(before["sha256"], after["sha256"])
            self.assertEqual(after["first_published"], "0.20.0", "an unchanged pack keeps its first release")
        # A changed loadscreen source rebuilds only that pack.
        write(source / openq4_pak.OPENQ4_CONTENT_PACKS_BY_NAME["pak2.pk4"].source / "gfx/guis/loadscreens/new.tga", "new\n")
        third = self.build(source, self.work / "r3", "0.20.2", previous=self.work / "r2")
        states = {pack["name"]: pack["first_published"] for pack in third["packs"]}
        self.assertEqual(states, {"pak0.pk4": "0.20.0", "pak1.pk4": "0.20.0", "pak2.pk4": "0.20.2"})
        plan = run(BUILD / "content_pack_release.py", "plan", "--source-root", source,
                   "--previous-manifest", self.work / "r2" / "content-packs.json")
        self.assertIn("pak2.pk4 build", plan.stdout)
        self.assertIn("pak1.pk4 reuse", plan.stdout)

    def test_patch_release_rejects_changed_bulk_packs(self) -> None:
        source = self.work / "source"
        make_sources(source)
        self.build(source, self.work / "r1", "0.20.0")
        write(source / openq4_pak.OPENQ4_CONTENT_PACKS_BY_NAME["pak1.pk4"].source / "maps/mp/extra.lightgridpack", "rebake\n")
        result = run(BUILD / "content_pack_release.py", "build", "--source-root", source, "--out-dir", self.work / "r2",
                     "--version-tag", "0.20.1", "--previous-manifest", self.work / "r1" / "content-packs.json",
                     "--previous-packs-dir", self.work / "r1", "--forbid-bulk-changes")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("patch releases do not change bulk content", result.stderr)
        self.assertIn("pak1.pk4", result.stderr)
        workflow = (ROOT / ".github/workflows/manual-release.yml").read_text(encoding="utf-8")
        self.assertIn('build_args+=(--forbid-bulk-changes)', workflow)

    def test_notes_state_each_pack(self) -> None:
        source = self.work / "source"
        make_sources(source)
        first = self.build(source, self.work / "r1", "0.20.0")
        second = self.build(source, self.work / "r2", "0.20.1", previous=self.work / "r1")
        text = RELEASE.format_notes(second, first, "openq4-0.20.1-windows-x64-update.zip")
        self.assertIn("## Content Packs", text)
        self.assertIn("unchanged since 0.20.0", text)
        self.assertIn("download only the update archive", text)
        changed = json.loads(json.dumps(second))
        changed["packs"][2]["sha256"] = "0" * 64
        text = RELEASE.format_notes(changed, first, "x")
        self.assertIn("`pak2.pk4` changed in this release", text)
        self.assertIn("full download", RELEASE.format_notes(second, None, "x"))

    def test_verify_update_proves_previous_plus_update_equals_new(self) -> None:
        old = {"openQ4-client_x64.exe": b"v1", "VERSION.txt": b"1", "baseoq4/pak0.pk4": b"core1",
               "baseoq4/pak1.pk4": b"grids", "baseoq4/pak2.pk4": b"screens", "docs/old.html": b"old"}
        new = {"openQ4-client_x64.exe": b"v2", "VERSION.txt": b"2", "baseoq4/pak0.pk4": b"core2",
               "baseoq4/pak1.pk4": b"grids", "baseoq4/pak2.pk4": b"screens"}
        update = {name: data for name, data in new.items() if name not in ("baseoq4/pak1.pk4", "baseoq4/pak2.pk4")}
        previous = make_zip(self.work / "old.zip", old, "openq4-0.20.0-windows-x64")
        full = make_zip(self.work / "new.zip", new, "openq4-0.20.1-windows-x64")
        delta = make_zip(self.work / "new-update.zip", update)
        script = BUILD / "content_pack_release.py"
        result = run(script, "verify-update", "--previous-full", previous, "--update", delta, "--new-full", full)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"docs/old.html"', result.stdout, "a stale page is reported as harmless")

        # A changed bulk pack must come from its standalone asset.
        new_screens = dict(new, **{"baseoq4/pak2.pk4": b"screens2"})
        full2 = make_zip(self.work / "new2.zip", new_screens, "openq4-0.20.1-windows-x64")
        result = run(script, "verify-update", "--previous-full", previous, "--update", delta, "--new-full", full2)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("differs after update: baseoq4/pak2.pk4", result.stdout)
        write(self.work / "pak2.pk4", b"screens2")
        result = run(script, "verify-update", "--previous-full", previous, "--update", delta, "--new-full", full2,
                     "--pack", f"pak2.pk4={self.work / 'pak2.pk4'}")
        self.assertEqual(result.returncode, 0, result.stdout)

        # An update archive must never carry a bulk pack, and a stale pack it cannot remove fails.
        bad = make_zip(self.work / "bad-update.zip", dict(update, **{"baseoq4/pak1.pk4": b"grids"}))
        result = run(script, "verify-update", "--previous-full", previous, "--update", bad, "--new-full", full)
        self.assertIn("carries the bulk pack", result.stdout)
        stale_old = make_zip(self.work / "old-stale.zip", dict(old, **{"baseoq4/pak7.pk4": b"overlay"}), "openq4-0.20.0-windows-x64")
        result = run(script, "verify-update", "--previous-full", stale_old, "--update", delta, "--new-full", full)
        self.assertIn("left behind by the update and still loaded: baseoq4/pak7.pk4", result.stdout)

    def test_verify_identity_finds_packs_in_every_package_layout(self) -> None:
        source = self.work / "source"
        make_sources(source)
        manifest = self.build(source, self.work / "r1", "0.20.0")
        packs = {pack["name"]: (self.work / "r1" / pack["name"]).read_bytes() for pack in manifest["packs"]}
        windows = make_zip(self.work / "openq4-0.20.0-windows-x64.zip",
                           {f"baseoq4/{name}": data for name, data in packs.items()}, "openq4-0.20.0-windows-x64")
        apk = make_zip(self.work / "openq4-0.20.0-android-arm64.apk",
                       {f"assets/baseoq4/{name}": data for name, data in packs.items()})
        mac = make_zip(self.work / "openq4-0.20.0-macos-arm64.zip",
                       {f"openQ4.app/Contents/Resources/baseoq4/{name}": data for name, data in packs.items()}, "pkg")
        update = make_zip(self.work / "openq4-0.20.0-windows-x64-update.zip", {"baseoq4/pak0.pk4": packs["pak0.pk4"]})
        script = BUILD / "content_pack_release.py"
        result = run(script, "verify-identity", "--manifest", self.work / "r1" / "content-packs.json",
                     "--archive", windows, "--archive", apk, "--archive", mac, "--archive", update)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        other = make_zip(self.work / "openq4-0.20.0-linux-x64.zip",
                         {f"baseoq4/{name}": (data if name != "pak1.pk4" else b"rebuilt") for name, data in packs.items()}, "x")
        result = run(script, "verify-identity", "--manifest", self.work / "r1" / "content-packs.json", "--archive", other)
        self.assertIn("pak1.pk4 sha256", result.stdout)
        self.assertNotEqual(result.returncode, 0)

    def test_prebuilt_pack_is_copied_only_for_matching_sources(self) -> None:
        source = self.work / "source"
        make_sources(source)
        self.build(source, self.work / "r1", "0.20.0")
        spec = openq4_pak.OPENQ4_CONTENT_PACKS_BY_NAME["pak1.pk4"]
        out = self.work / "meson" / "pak1.pk4"
        args = [BUILD / "build_openq4_pack.py", "--pak-name", "pak1.pk4", "--source-dir", source / spec.source,
                "--out", out, "--prebuilt-dir", self.work / "r1"]
        result = run(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(out.read_bytes(), (self.work / "r1" / "pak1.pk4").read_bytes())
        write(source / spec.source / "maps/mp/extra.lightgridpack", "changed\n")
        result = run(*args)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("was built from other sources", result.stderr)

    def test_packager_update_archive_is_rootless_and_omits_bulk_packs(self) -> None:
        package = self.work / "openq4-0.20.0-windows-x64"
        for name in ("openQ4-client_x64.exe", "openQ4-client_x64.pdb", "baseoq4/game-sp_x64.dll",
                     "baseoq4/game-sp_x64.pdb", *(f"baseoq4/{pack}" for pack in openq4_pak.OPENQ4_PACK_NAMES)):
            write(package / name, name)
        symbols = PACKAGER.move_windows_symbols(package, self.work / "symbols")
        self.assertEqual({path.as_posix() for path in symbols}, {"openQ4-client_x64.pdb", "baseoq4/game-sp_x64.pdb"})
        self.assertFalse(list(package.rglob("*.pdb")), "PDBs leave the player package")
        archive = self.work / "openq4-0.20.0-windows-x64-symbols.zip"
        PACKAGER.create_windows_symbol_archive(package, self.work / "symbols", archive, symbols, "0.20.0", "x64")
        with zipfile.ZipFile(archive) as handle:
            manifest = handle.read("symbols/SYMBOLS.txt").decode("utf-8")
        self.assertIn("openQ4-client_x64.pdb\t", manifest)
        self.assertIn("\topenQ4-client_x64.exe\t", manifest)

        update = self.work / "openq4-0.20.0-windows-x64-update.zip"
        PACKAGER.create_release_archive(package, update, "zip", set(),
                                        excluded_relative_paths=PACKAGER.update_archive_excluded_paths(), rootless=True)
        with zipfile.ZipFile(update) as handle:
            names = set(handle.namelist())
        self.assertIn("openQ4-client_x64.exe", names, "update archives have no top-level folder")
        self.assertIn("baseoq4/pak0.pk4", names)
        for bulk in openq4_pak.OPENQ4_BULK_PACK_NAMES:
            self.assertNotIn(f"baseoq4/{bulk}", names)

    def test_release_workflow_ships_one_build_of_each_pack(self) -> None:
        workflow = (ROOT / ".github/workflows/manual-release.yml").read_text(encoding="utf-8")
        android = (ROOT / ".github/workflows/android-build.yml").read_text(encoding="utf-8")
        android_script = (BUILD / "build_android_release.sh").read_text(encoding="utf-8")
        for token in (
            "  content_packs:\n    name: Content Packs",
            "python tools/build/content_pack_release.py plan",
            "python tools/build/content_pack_release.py \"${build_args[@]}\"",
            "    needs: [metadata, content_packs]\n",
            "\"-Dprebuilt_content_packs=$(Join-Path $env:RUNNER_TEMP 'content-packs')\"",
            "\"-Dprebuilt_content_packs=${RUNNER_TEMP}/content-packs\"",
            "content_packs_artifact: openq4-content-packs-",
            "-windows-x64-update.zip\"",
            "-windows-x64-symbols.zip\"",
            "-linux-x64-update.tar.xz\"",
            "-content-packs.json\"",
            "mapfile -t bulk_packs < <(python tools/build/content_packs.py bulk-list)",
            "verify-identity --manifest",
            "python tools/build/content_pack_release.py verify-update",
            "--update-asset-example",
            "[[ ! \"${asset_name}\" =~ ^pak[0-9]\\.pk4$ ]]",
        ):
            self.assertIn(token, workflow)
        self.assertIn("OPENQ4_PREBUILT_CONTENT_PACKS", android)
        self.assertIn("-Dprebuilt_content_packs=${OPENQ4_PREBUILT_CONTENT_PACKS}", android_script)
        verify_offset = workflow.index("- name: Verify content packs and update archives")
        self.assertLess(workflow.index("- name: Verify platform archives"), verify_offset)
        self.assertLess(verify_offset, workflow.index("- name: Create or update release"))

    def test_installer_downloads_only_missing_or_different_bulk_packs(self) -> None:
        installer = load("build_windows_installer_under_test", BUILD / "build_windows_installer.py")
        package = self.work / "package"
        source = self.work / "source"
        make_sources(source)
        manifest = self.build(source, self.work / "r1", "0.20.0")
        for pack in manifest["packs"]:
            write(package / "baseoq4" / pack["name"], (self.work / "r1" / pack["name"]).read_bytes())
        base_url = installer.validate_content_pack_base_url("https://github.com/themuffinator/openQ4/releases/download/v0.20.0")
        packs = installer.read_download_packs(self.work / "r1" / "content-packs.json", package, base_url)
        self.assertEqual([pack["name"] for pack in packs], list(openq4_pak.OPENQ4_BULK_PACK_NAMES))
        template = (BUILD / "openQ4Installer.iss.in").read_text(encoding="utf-8")
        script = installer.render_installer_script(
            template, package_dir=package, output_dir=self.work / "out", version="0.20.0", version_tag="0.20.0",
            arch="x64", setup_icon_file=self.work / "icon.ico", download_packs=packs)
        self.assertNotIn("@@", script)
        self.assertIn('Excludes: "\\baseoq4\\pak1.pk4,\\baseoq4\\pak2.pk4"', script)
        for pack in packs:
            self.assertIn(f"'{pack['sha256']}'", script, "each download is pinned to its SHA-256")
            self.assertIn(f"'https://github.com/themuffinator/openQ4/releases/download/v0.20.0/{pack['name']}'", script)
            self.assertIn(f'[UninstallDelete]\nType: files; Name: "{{app}}\\baseoq4\\pak1.pk4"', script)
        self.assertIn("ContentPackDownloadPage.Add(ContentPackUrl(I), ContentPackName(I) + '.download', ContentPackSha256(I));", script)
        self.assertIn("if ContentPackMatches(ContentPackTarget(I), ContentPackSha256(I)) then", script, "a current pack is kept")
        self.assertIn("ExpandConstant('{app}\\baseoq4\\')", script)
        self.assertIn('Type: files; Name: "{app}\\baseoq4\\pak9.pk4"', script, "retired packs are deleted")
        self.assertNotIn('Name: "{app}\\baseoq4\\pak1.pk4"\n[Files]', script)
        self.assertIn('Type: files; Name: "{app}\\*.pdb"', script)
        # Without a manifest the installer embeds every pack, as before.
        embedded = installer.render_installer_script(
            template, package_dir=package, output_dir=self.work / "out", version="0.20.0", version_tag="0.20.0",
            arch="x64", setup_icon_file=self.work / "icon.ico")
        self.assertIn('Excludes: ""', embedded)
        self.assertNotIn("TDownloadWizardPage", embedded)
        for bad in ("http://example.com/", "file:///c:/packs/", "https://example.com/a b/"):
            with self.assertRaises(ValueError):
                installer.validate_content_pack_base_url(bad)
        installer.validate_content_pack_base_url("http://127.0.0.1:8765/")
        (package / "baseoq4" / "pak2.pk4").write_bytes(b"other bytes")
        with self.assertRaises(RuntimeError):
            installer.read_download_packs(self.work / "r1" / "content-packs.json", package, base_url)

    def test_appimage_update_information_names_its_zsync(self) -> None:
        sys.path.insert(0, str(ROOT))
        appimage = load("package_linux_appimage_under_test", BUILD / "package_linux_appimage.py")
        good = "gh-releases-zsync|themuffinator|openQ4|latest|openq4-*-x86_64.AppImage.zsync"
        self.assertEqual(appimage.validate_update_information(good, "openq4-0.20.0-x86_64.AppImage"), good)
        with self.assertRaises(appimage.AppImageError):
            appimage.validate_update_information(good, "openq4-0.20.0-aarch64.AppImage")
        with self.assertRaises(appimage.AppImageError):
            appimage.validate_update_information("zsync|https://example.com/x.zsync", "openq4-0.20.0-x86_64.AppImage")
        image = self.work / "openq4-0.20.0-x86_64.AppImage"
        write(image, b"\x7fELF" + good.encode() + b"payload")
        import hashlib
        write(Path(str(image) + ".zsync"),
              f"zsync: 0.6.2\nFilename: {image.name}\nLength: {image.stat().st_size}\nSHA-1: "
              f"{hashlib.sha1(image.read_bytes()).hexdigest()}\n\nblocks".encode())
        appimage.validate_embedded_update_information(image, good)
        workflow = (ROOT / ".github/workflows/manual-release.yml").read_text(encoding="utf-8")
        self.assertIn('--update-information "gh-releases-zsync|${GITHUB_REPOSITORY%%/*}|${GITHUB_REPOSITORY##*/}|latest|', workflow)
        self.assertIn("-x86_64.AppImage.zsync\"", workflow)

    def test_one_file_fix_ships_as_a_small_overlay_and_the_base_keeps_its_bytes(self) -> None:
        import content_generations as generations
        import random

        repo = self.work / "repo"
        repo.mkdir()
        git = lambda *args: subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, text=True).stdout.strip()
        git("init", "-q")
        git("config", "user.email", "test@example.invalid")
        git("config", "user.name", "test")
        source = repo / "content/baseoq4/pak2/gfx/guis/loadscreens"
        rng = random.Random(4)
        for name in ("generic.dds", "generic.tga", "a.tga", "b.tga"):
            write(source / name, bytes(rng.randrange(256) for _ in range(256 * 1024)))
        git("add", "-A")
        git("commit", "-q", "-m", "generation 1")
        commit = git("rev-parse", "HEAD")
        base = openq4_pak.ContentPackSpec(
            name="pak2.pk4", source="content/baseoq4/pak2", contents="loadscreens", contents_string="#str_231703",
            client_only=True, bulk=True, required_files=frozenset(), generation_number=1, generation_commit=commit)
        overlay = openq4_pak.ContentPackSpec(
            name="pak3.pk4", source=base.source, contents="loadscreens update", contents_string="#str_231703",
            client_only=True, bulk=False, required_files=frozenset(), overlay_of="pak2.pk4",
            generation_number=1, generation_commit=commit)

        published = self.work / "published-pak2.pk4"
        generations.build_content_pack(repo, base, published, specs=[base])
        # One file changes after the generation was published.
        write(source / "a.tga", bytes(rng.randrange(256) for _ in range(1024 * 1024)))
        plan = generations.plan_overlays(repo, [base], 25_000_000)[0]
        self.assertEqual((plan["action"], plan["changed"], plan["added"], plan["removed"]), ("overlay", ["gfx/guis/loadscreens/a.tga"], [], []))
        with self.assertRaises(generations.GenerationError):
            generations.build_content_pack(repo, base, self.work / "x.pk4", specs=[base])  # no overlay declared yet
        rebuilt = self.work / "pak2.pk4"
        fix = self.work / "pak3.pk4"
        generations.build_content_pack(repo, base, rebuilt, specs=[base, overlay])
        generations.build_content_pack(repo, overlay, fix, specs=[base, overlay])
        self.assertEqual(rebuilt.read_bytes(), published.read_bytes(), "the base keeps its published bytes")
        with zipfile.ZipFile(fix) as handle:
            self.assertEqual(handle.namelist(), ["gfx/guis/loadscreens/a.tga"])
            self.assertEqual(handle.read("gfx/guis/loadscreens/a.tga"), (source / "a.tga").read_bytes())
        self.assertLess(fix.stat().st_size, 5 * 1000 * 1000, "a one-file fix ships under 5 MB")
        self.assertEqual(generations.effective_source_digest(repo, base, [base, overlay]),
                         (openq4_pak.format_pack_source_digest("pak2.pk4", list(generations.generation_listing(repo, base, commit).items())), 4))

        # Over budget, or a removal, needs a new generation instead.
        self.assertEqual(generations.plan_overlays(repo, [base], 1000)[0]["action"], "new-generation")
        (source / "b.tga").unlink()
        self.assertEqual(generations.plan_overlays(repo, [base], 25_000_000)[0]["action"], "new-generation")
        with self.assertRaises(generations.GenerationError):
            generations.build_content_pack(repo, overlay, fix, specs=[base, overlay])

        # generation-bump records the new commit and folds the overlay.
        git("add", "-A")
        git("commit", "-q", "-m", "generation 2")
        table = json.loads((BUILD / "content_packs.json").read_text(encoding="utf-8"))
        for pack in table["packs"]:
            if pack["name"] == "pak2.pk4":
                pack["generation"] = {"number": 1, "commit": commit}
        table["packs"].append({"name": "pak3.pk4", "overlay_of": "pak2.pk4"})
        table_path = self.work / "content_packs.json"
        table_path.write_text(json.dumps(table), encoding="utf-8")
        self.assertEqual([spec.name for spec in openq4_pak.load_content_pack_specs(table_path)][-1], "pak3.pk4")
        result = run(BUILD / "content_pack_release.py", "generation-bump", "--table", table_path, "--source-root", repo,
                     "--pack", "pak2.pk4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("folded pak3.pk4", result.stdout)
        bumped = json.loads(table_path.read_text(encoding="utf-8"))
        self.assertEqual(next(p for p in bumped["packs"] if p["name"] == "pak2.pk4")["generation"],
                         {"number": 2, "commit": git("rev-parse", "HEAD")})
        self.assertFalse(any(p.get("overlay_of") for p in bumped["packs"]))

    def test_light_grid_trim_drops_what_the_runtime_never_reads(self) -> None:
        names = [
            "maps/mp/q4dm1.lightgridpack", "maps/mp/q4dm1.lightgrid",
            "env/maps/mp/q4dm1/area0_lightgrid_amb.tga", "env/maps/mp/q4dm1/area0_lightgrid_vis.tga",
            "maps/game/waste.lightgrid", "env/maps/game/waste/area0_lightgrid_amb.tga",
            "maps/game/core2.lightgrid",
        ]
        self.assertEqual(openq4_pak.light_grid_trim(names), {
            "env/maps/mp/q4dm1/area0_lightgrid_amb.tga", "env/maps/mp/q4dm1/area0_lightgrid_vis.tga",
            "maps/game/core2.lightgrid",
        })
        self.assertEqual(openq4_pak.OPENQ4_CONTENT_PACKS_BY_NAME["pak1.pk4"].rules, ("light-grid-trim",))
        # The rule is part of the source digest, so changing it never reuses an old pack.
        import hashlib
        expected = hashlib.sha256(
            f"{openq4_pak.PACK_SOURCE_DIGEST_FORMAT}\npak1.pk4\nrule\tlight-grid-trim\na\tb\n".encode("utf-8")).hexdigest()
        self.assertEqual(openq4_pak.format_pack_source_digest("pak1.pk4", [("a", "b")]), expected)

    def test_overlay_entries_are_validated(self) -> None:
        table = json.loads((BUILD / "content_packs.json").read_text(encoding="utf-8"))
        table["packs"] = [{key: value for key, value in pack.items() if key != "generation"} for pack in table["packs"]]
        path = self.work / "content_packs.json"
        for overlay, message in (
            ({"name": "pak3.pk4", "overlay_of": "pak2.pk4"}, "has no content generation"),
            ({"name": "pak2.pk4", "overlay_of": "pak1.pk4"}, "pak3.pk4 to pak9.pk4"),
            ({"name": "pak4.pk4", "overlay_of": "pak0.pk4"}, "bulk pack"),
        ):
            path.write_text(json.dumps(dict(table, packs=table["packs"] + [overlay])), encoding="utf-8")
            with self.assertRaises(RuntimeError) as caught:
                openq4_pak.load_content_pack_specs(path)
            self.assertIn(message, str(caught.exception))

    def test_release_asset_names_admit_standalone_packs(self) -> None:
        verifier = load("verify_release_asset_set_under_test", BUILD / "verify_release_asset_set.py")
        self.assertEqual(verifier.validate_asset_name("pak2.pk4"), "pak2.pk4")
        for bad in ("pak10.pk4", "../pak1.pk4", "pak1.pk4.exe"):
            with self.assertRaises(RuntimeError):
                verifier.validate_asset_name(bad)


if __name__ == "__main__":
    unittest.main()
