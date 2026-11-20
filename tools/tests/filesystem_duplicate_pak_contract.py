#!/usr/bin/env python3
"""Check that a pak reachable from two search roots is mounted only once.

An identical copy (same name, same game folder, same checksum) already in the
search list must be dropped quietly. A renamed identical copy, or one in another
game folder, must stay mounted: required retail paks are found by their own name
in q4base, so the canonical entry must never be freed in favour of a copy. The
pure list then names such a checksum once. A pak with the same name in the same
game folder but different contents must warn and stay mounted. Directory entries
and unrelated paks are left alone.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body

ROOT = Path(__file__).resolve().parents[2]
SUPPORT = r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <cctype>
static int NoCaseCmp(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        int d = std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
        if (d) return d;
    }
    return std::tolower((unsigned char)*a) - std::tolower((unsigned char)*b);
}
struct idStr {
    std::string s;
    idStr() {}
    idStr(const char *v) : s(v) {}
    const char *c_str() const { return s.c_str(); }
    int Icmp(const idStr &o) const { return NoCaseCmp(s.c_str(), o.s.c_str()); }
    void ExtractFileName(idStr &dest) const {
        size_t slash = s.find_last_of("/\\");
        dest.s = slash == std::string::npos ? s : s.substr(slash + 1);
    }
};
static bool FS_PakPathIsInGameDir(const char *pakFilename, const char *gameDir) {
    std::string path(pakFilename);
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return false;
    path.resize(slash);
    slash = path.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? path : path.substr(slash + 1);
    return NoCaseCmp(dir.c_str(), gameDir) == 0;
}
struct pack_t { idStr pakFilename; int checksum; };
struct directory_t { idStr path, gamedir; };
struct searchpath_t { pack_t *pack; directory_t *dir; searchpath_t *next; };
struct DebugCvar { int value = 0; int GetInteger() const { return value; } } fs_debug;
struct Common {
    int warnings = 0, prints = 0;
    void Warning(const char *, ...) { ++warnings; }
    void Printf(const char *, ...) { ++prints; }
} commonStorage, *common = &commonStorage;
struct idFileSystemLocal {
    searchpath_t *searchPaths = nullptr;
    int freed = 0;
    void FreePack(pack_t *pack) { ++freed; delete pack; }
    void DropMountedPakCopies(const pack_t *pak, const char *dir);
};
'''
MAIN = r'''
static searchpath_t *Dir(const char *path, searchpath_t *next) {
    return new searchpath_t{nullptr, new directory_t{path, "q4base"}, next};
}
static searchpath_t *Pak(const char *name, int checksum, searchpath_t *next) {
    return new searchpath_t{new pack_t{name, checksum}, nullptr, next};
}
static int Count(const idFileSystemLocal &fs) {
    int n = 0;
    for (searchpath_t *sp = fs.searchPaths; sp; sp = sp->next) ++n;
    return n;
}
static bool HasPak(const idFileSystemLocal &fs, const char *name) {
    for (searchpath_t *sp = fs.searchPaths; sp; sp = sp->next)
        if (sp->pack && sp->pack->pakFilename.s == name) return true;
    return false;
}
int main() {
    // Search order, highest first: the cdpath directory being populated, its
    // own pak023, then the basepath directory with copies of the patch paks.
    idFileSystemLocal fs;
    fs.searchPaths = Dir("/opt/openq4",
        Pak("/opt/openq4/q4base/pak023.pk4", 0x23,
        Dir("/games/quake4",
        Pak("/games/quake4/q4base/pak022.pk4", 0x22,
        Pak("/games/quake4/q4base/pak013.pk4", 0x13,
        Pak("/games/quake4/q4base/pak001.pk4", 0x01, nullptr))))));

    // Identical copy: the lower one goes, quietly, and nothing else moves.
    pack_t same{"/opt/openq4/q4base/pak022.pk4", 0x22};
    fs.DropMountedPakCopies(&same, "q4base");
    assert(fs.freed == 1 && common->warnings == 0 && common->prints == 0);
    assert(!HasPak(fs, "/games/quake4/q4base/pak022.pk4"));
    assert(HasPak(fs, "/games/quake4/q4base/pak013.pk4") && Count(fs) == 5);

    // Identical content under another name keeps the canonical entry, which
    // the required-pak check looks up by name.
    pack_t renamed{"/opt/openq4/q4base/patch013.pk4", 0x13};
    fs.DropMountedPakCopies(&renamed, "q4base");
    assert(fs.freed == 1 && common->warnings == 0 && Count(fs) == 5);
    assert(HasPak(fs, "/games/quake4/q4base/pak013.pk4"));

    // So does an identical copy in another game folder, such as a mod.
    pack_t modCopy{"/opt/openq4/mymod/pak013.pk4", 0x13};
    fs.DropMountedPakCopies(&modCopy, "mymod");
    assert(fs.freed == 1 && common->warnings == 0 && Count(fs) == 5);
    assert(HasPak(fs, "/games/quake4/q4base/pak013.pk4"));

    // Same name, same folder, different contents: warn and keep both.
    pack_t changed{"/opt/openq4/q4base/pak001.pk4", 0x7777};
    fs.DropMountedPakCopies(&changed, "q4base");
    assert(fs.freed == 1 && common->warnings == 1 && Count(fs) == 5);
    assert(HasPak(fs, "/games/quake4/q4base/pak001.pk4"));

    // A mod's pak named like a retail one is an intended override: no warning.
    pack_t mod{"/opt/openq4/mymod/pak001.pk4", 0x8888};
    fs.DropMountedPakCopies(&mod, "mymod");
    assert(fs.freed == 1 && common->warnings == 1 && Count(fs) == 5);

    // fs_debug reports the drop.
    fs_debug.value = 1;
    pack_t again{"/opt/openq4/q4base/pak001.pk4", 0x01};
    fs.DropMountedPakCopies(&again, "q4base");
    assert(fs.freed == 2 && common->prints == 1 && Count(fs) == 4);

    // Directory entries are never touched, including the head.
    assert(fs.searchPaths->dir && fs.searchPaths->next->pack);
    for (searchpath_t *sp = fs.searchPaths; sp;) {
        searchpath_t *next = sp->next;
        delete sp->pack; delete sp->dir; delete sp;
        sp = next;
    }
    puts("filesystem_duplicate_pak_contract: PASS (identical copies dropped, renamed copies kept, conflicts warned)");
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/framework/FileSystem.cpp")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    text = args.source.read_text(encoding="utf-8")
    body = function_body(text, "void idFileSystemLocal::DropMountedPakCopies(")
    # A renamed or cross-folder copy stays mounted, so the pure list itself
    # must name each checksum once: clients refuse a list with a repeat.
    pure = function_body(text, "void idFileSystemLocal::UpdatePureServerChecksums(")
    if "PackListHasChecksum( serverPaks, search->pack->checksum )" not in pure:
        raise SystemExit("UpdatePureServerChecksums must skip a checksum it already listed")
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="duplicate-pak-", dir=ROOT / ".tmp") as directory:
        source, binary = Path(directory) / "duplicate.cpp", Path(directory) / "duplicate.exe"
        source.write_text(SUPPORT + body + MAIN, encoding="utf-8")
        subprocess.run([compiler, *flags, str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
