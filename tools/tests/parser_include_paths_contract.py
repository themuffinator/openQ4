#!/usr/bin/env python3
"""Execute both production include handlers against real OS-to-qpath conversion."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body


ROOT = Path(__file__).resolve().parents[2]
GAME_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game"))

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#define BASE_GAMEDIR "q4base"
#define BASE_MPGAMEDIR "q4mp"
#define OPENQ4_GAMEDIR "baseoq4"
static const int MAX_STRING_CHARS = 1024;
class idStr {
    std::string value;
public:
    idStr() = default;
    idStr(const char *text) : value(text) {}
    idStr(std::string text) : value(std::move(text)) {}
    const char *c_str() const { return value.c_str(); }
    operator const char *() const { return c_str(); }
    int Length() const { return static_cast<int>(value.size()); }
    static int Length(const char *text) { return static_cast<int>(std::strlen(text)); }
    char operator[](int index) const { return value.at(index); }
    void Clear() { value.clear(); }
    void CapLength(int length) { if (length < Length()) value.resize(length); }
    void BackSlashesToSlashes() { std::replace(value.begin(), value.end(), '\\', '/'); }
    void StripTrailing(char c) { while (!value.empty() && value.back() == c) value.pop_back(); }
    idStr Mid(int offset, int length) const { return value.substr(offset, length); }
    static int Icmpn(const char *a, const char *b, int length) {
        for (int i = 0; i < length; ++i) {
            const int left = std::tolower(static_cast<unsigned char>(a[i]));
            const int right = std::tolower(static_cast<unsigned char>(b[i]));
            if (left != right || left == 0) return left - right;
        }
        return 0;
    }
    static int Icmp(const char *a, const char *b) {
        return Icmpn(a, b, static_cast<int>(std::max(std::strlen(a), std::strlen(b))) + 1);
    }
    int Find(const char *needle, bool caseSensitive) const {
        for (int offset = 0; offset + Length(needle) <= Length(); ++offset) {
            if (caseSensitive ? std::strncmp(c_str() + offset, needle, Length(needle)) == 0 :
                    Icmpn(c_str() + offset, needle, Length(needle)) == 0) return offset;
        }
        return -1;
    }
    static void Copynz(char *out, const char *in, int capacity) {
        std::snprintf(out, capacity, "%s", in);
    }
    idStr &operator+=(const idStr &other) { value += other.value; return *this; }
    friend idStr operator+(const idStr &left, const idStr &right) { return left.value + right.value; }
    bool operator==(const char *other) const { return value == other; }
    bool operator!=(const char *other) const { return value != other; }
    idStr &StripFilename();
};
struct CVar {
    std::string value;
    explicit CVar(const char *text) : value(text) {}
    const char *GetString() const { return value.c_str(); }
    int GetInteger() const { return 0; }
} fs_game("baseoq4"), fs_game_base(""), fs_debug("0");
struct Common {
    int warnings = 0;
    template<class... T> void Warning(const char *, T...) { ++warnings; }
    template<class... T> void Printf(const char *, T...) {}
} commonValue, *common = &commonValue;
struct idFileSystemLocal {
    const char *OSPathToRelativePath(const char *path);
};
struct CVarSystem {
    const char *GetCVarString(const char *name) const {
        if (std::strcmp(name, "fs_game") == 0) return fs_game.GetString();
        assert(std::strcmp(name, "fs_game_base") == 0);
        return fs_game_base.GetString();
    }
};
struct idLib {
    static idFileSystemLocal *fileSystem;
    static CVarSystem *cvarSystem;
};
idFileSystemLocal fileSystemValue;
CVarSystem cvarSystemValue;
idFileSystemLocal *idLib::fileSystem = &fileSystemValue;
CVarSystem *idLib::cvarSystem = &cvarSystemValue;
enum {TT_STRING, TT_PUNCTUATION, LEXFL_NOBASEINCLUDES = 4};
struct idToken : idStr {
    using idStr::idStr;
    int linesCrossed = 0, type = TT_STRING;
};
struct idLexer {
    idStr filename;
    static std::vector<std::string> attempts;
    static std::map<std::string, std::string> files;
    const char *GetFileName() const { return filename; }
    bool LoadFile(const char *path, bool osPath) {
        assert(!osPath); // Exercise game-relative includes used by the script compiler.
        attempts.emplace_back(path);
        const auto found = files.find(path);
        if (found == files.end()) return false;
        filename = found->second;
        return true;
    }
    void SetFlags(int) {}
    void SetPunctuations(const void *) {}
};
std::vector<std::string> idLexer::attempts;
std::map<std::string, std::string> idLexer::files;
struct idParser {
    idLexer source;
    idLexer *scriptstack = &source;
    std::deque<idToken> tokens;
    idStr includepath;
    bool OSPath = false;
    int flags = 0, errors = 0, pushes = 0;
    const void *punctuations = nullptr;
    std::string loaded;
    bool ReadSourceToken(idToken *token) {
        if (tokens.empty()) return false;
        *token = tokens.front(); tokens.pop_front(); return true;
    }
    void UnreadSourceToken(idToken *token) { tokens.push_front(*token); }
    template<class... T> void Error(const char *, T...) { ++errors; }
    template<class... T> void Warning(const char *, T...) {}
    void PushScript(idLexer *script) { ++pushes; loaded = script->GetFileName(); delete script; }
    int Directive_include();
};
'''

CHECKS = r'''
static void Include(const char *source, const char *token, const char *expected,
        int expectedWarnings = 0, const char *includePath = "") {
    commonValue.warnings = 0; idLexer::attempts.clear(); idLexer::files.clear();
    idLexer::files[expected] = expected;
    idParser parser; parser.source.filename = source; parser.includepath = includePath;
    parser.tokens.emplace_back(token);
    assert(parser.Directive_include());
    assert(parser.errors == 0 && parser.pushes == 1 && parser.loaded == expected);
    assert(!idLexer::attempts.empty() && idLexer::attempts[0] == expected);
    assert(commonValue.warnings == expectedWarnings);
}
int main() {
    const char *roots[] = {
        "E:/Repositories/openQ4/.tmp/runtime/baseoq4/mapcycle.scriptcfg",
        "e:\\Repositories\\openQ4\\.tmp\\runtime\\baseoq4\\mapcycle.scriptcfg",
        "/home/Player/OpenQ4/baseoq4/mapcycle.scriptcfg",
        "\\\\Host\\Games\\baseoq4\\mapcycle.scriptcfg",
        "baseoq4/mapcycle.scriptcfg", "mapcycle.scriptcfg",
        "E:/Quake4/q4base/mapcycle.scriptcfg", "q4base/mapcycle.scriptcfg",
        "q4mp/mapcycle.scriptcfg", "/games/baseoq4/pak001.pk4/mapcycle.scriptcfg",
        "E:\\Quake4\\q4base\\pak001.PK4\\mapcycle.scriptcfg"
    };
    for (const auto *path : roots) Include(path, "scripts/defs.script", "scripts/defs.script");
    Include("E:/Games/baseoq4/scripts/main.script", "defs.script", "scripts/defs.script");
    Include("/home/Player/baseoq4/scripts/main.script", "scripts/defs.script", "scripts/defs.script");
    Include("scripts/main.script", "scripts/defs.script", "scripts/defs.script");
    Include("/games/q4base/pak002.pk4/scripts/maps/start.script", "actors.script", "scripts/maps/actors.script");
    Include("E:\\Games\\baseoq4\\scripts\\maps\\start.script", "actors.script", "scripts/maps/actors.script");
    fs_game.value = "coop"; fs_game_base.value = "baseoq4";
    Include("/home/Player/coop/mapcycle.scriptcfg", "scripts/defs.script", "scripts/defs.script");
    Include("/home/Player/baseoq4/mapcycle.scriptcfg", "scripts/defs.script", "scripts/defs.script");
    fs_game.value = "baseoq4"; fs_game_base.value.clear();
    // Failed/unmapped conversion must not silently become the valid VFS root.
    const char *unmapped[] = {"D:/External/mapcycle.scriptcfg", "/tmp/external/mapcycle.scriptcfg",
        "/games/baseoq4-extra/mapcycle.scriptcfg", "//Host/External/mapcycle.scriptcfg"};
    for (const auto *path : unmapped) {
        commonValue.warnings = 0;
        idStr original(path), normalized(path);
        Parser_NormalizeIncludeBase(normalized);
        assert(std::strcmp(normalized, original) == 0 && commonValue.warnings == 1);
        original.StripFilename(); original += "/scripts/defs.script";
        Include(path, "scripts/defs.script", original, 1);
    }
    // Missing local sibling still finds the ordinary explicit include path.
    idLexer::attempts.clear(); idLexer::files.clear();
    idLexer::files["shared/defs.script"] = "shared/defs.script";
    idParser fallback; fallback.source.filename = "/games/baseoq4/scripts/start.script";
    fallback.includepath = "shared/"; fallback.tokens.emplace_back("defs.script");
    assert(fallback.Directive_include());
    assert((idLexer::attempts == std::vector<std::string>{"scripts/defs.script", "defs.script", "shared/defs.script"}));
    assert(fallback.loaded == "shared/defs.script");
    std::puts("root, nested, archive, drive, UNC, POSIX and unmapped include paths: PASS");
}
'''


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++") or shutil.which("cl")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for parser include-path regressions")
    filesystem = (ROOT / "src/framework/FileSystem.cpp").read_text(encoding="utf-8")
    conversion = function_body(filesystem, "const char *idFileSystemLocal::OSPathToRelativePath(")
    # This legacy file contains a raw byte character table; the extracted
    # StripFilename definition is ASCII and must not depend on that table's codec.
    string_source = (ROOT / "src/idlib/Str.cpp").read_text(encoding="latin-1")
    strip_filename = function_body(string_source, "idStr &idStr::StripFilename(")
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="parser-include-", dir=ROOT / ".tmp") as directory:
        temporary = Path(directory)
        for name, tree in (("engine", ROOT), ("gamelibs", GAME_ROOT)):
            source = (tree / "src/idlib/Parser.cpp").read_text(encoding="utf-8")
            helpers = source[source.index("static bool Parser_IsAbsolutePath("):source.index("/*\n================\nidParser::Directive_include")]
            directive = function_body(source, "int idParser::Directive_include(")
            probe = temporary / f"{name}.cpp"
            probe.write_text(SUPPORT + strip_filename + conversion + helpers + directive + CHECKS, encoding="utf-8")
            executable = temporary / (f"{name}.exe" if os.name == "nt" else name)
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++17", "/EHsc", str(probe), f"/Fe:{executable}"]
            else:
                command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", str(probe), "-o", str(executable)]
                if os.environ.get("OPENQ4_TEST_SANITIZERS") == "1":
                    command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            subprocess.run(command, cwd=temporary, check=True)
            subprocess.run([str(executable)], cwd=temporary, check=True)
    print("parser_include_paths_contract: PASS (engine and GameLibs)")


if __name__ == "__main__":
    main()
