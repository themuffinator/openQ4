#!/usr/bin/env python3
"""Check production map-file discovery closes handles and preserves addon policy."""
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
#include <vector>
enum findFile_t { FIND_NO, FIND_YES, FIND_ADDON };
enum { FSFLAG_SEARCH_DIRS = 1, FSFLAG_SEARCH_PAKS = 2, FSFLAG_SEARCH_ADDONS = 4 };
struct idFile { static int live; idFile() { ++live; } ~idFile() { --live; } };
int idFile::live = 0;
struct pack_t { bool addon = false, addon_search = false; int checksum = 42; };
struct Checksums {
    std::vector<int> values;
    int FindIndex(int value) const {
        for (unsigned i = 0; i < values.size(); ++i) if (values[i] == value) return i;
        return -1;
    }
    void Append(int value) { values.push_back(value); }
};
struct idFileSystemLocal {
    bool present = true;
    pack_t *source = nullptr;
    Checksums addonChecksums;
    idFile *OpenFileReadFlags(const char *, int flags, pack_t **pak) {
        assert(flags == (FSFLAG_SEARCH_DIRS | FSFLAG_SEARCH_PAKS | FSFLAG_SEARCH_ADDONS));
        *pak = source;
        return present ? new idFile : nullptr;
    }
    findFile_t FindFile(const char *path, bool scheduleAddons);
};
'''
MAIN = r'''
int main() {
    idFileSystemLocal fs;
    for (int i = 0; i < 100; ++i) {
        assert(fs.FindFile("maps/mp/loose.map", false) == FIND_YES);
        assert(idFile::live == 0);
    }
    fs.present = false;
    assert(fs.FindFile("maps/mp/missing.map", false) == FIND_NO);
    assert(idFile::live == 0);
    fs.present = true;
    pack_t pak;
    fs.source = &pak;
    assert(fs.FindFile("maps/mp/packed.map", false) == FIND_YES);
    assert(idFile::live == 0);
    pak.addon = true;
    assert(fs.FindFile("maps/mp/addon.map", false) == FIND_ADDON);
    assert(idFile::live == 0 && fs.addonChecksums.values.empty());
    assert(fs.FindFile("maps/mp/addon.map", true) == FIND_ADDON);
    assert(idFile::live == 0 && fs.addonChecksums.values.size() == 1);
    assert(fs.FindFile("maps/mp/addon.map", true) == FIND_ADDON);
    assert(idFile::live == 0 && fs.addonChecksums.values.size() == 1);
    pak.addon_search = true;
    assert(fs.FindFile("maps/mp/addon.map", false) == FIND_YES);
    assert(idFile::live == 0 && fs.addonChecksums.values.size() == 1);
    puts("filesystem_find_file_contract: PASS (handles, discovery and addon scheduling)");
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/framework/FileSystem.cpp")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    body = function_body(args.source.read_text(encoding="utf-8"),
                         "findFile_t idFileSystemLocal::FindFile(")
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="find-file-", dir=ROOT / ".tmp") as directory:
        source, binary = Path(directory) / "find.cpp", Path(directory) / "find.exe"
        source.write_text(SUPPORT + body + MAIN, encoding="utf-8")
        subprocess.run([compiler, *flags, str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
