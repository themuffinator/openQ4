#!/usr/bin/env python3
"""Static symbol closure of the OpenGL ES renderer module.

The renderer-gles module (Android, and desktop -Dbuild_renderer_gles=enabled)
compiles the shared renderer minus RENDERER_GLES_EXCLUDED_SOURCES and links
with -z defs, but no push or commit CI lane builds it. A translation unit the
module compiles that calls a free function defined only in an excluded
translation unit therefore breaks the Android build unseen (25 November 2026:
R_SetDrawInteraction from the modern executor, RB_DesktopStereo_Composite from
the VR presenter).

This test approximates that link closure from source: every external free
function defined in an excluded translation unit must, when a module
translation unit calls it outside an OPENQ4_RENDERER_GLES_MODULE exclusion,
also be defined by a source the module builds or links (gles_Backend.cpp
supplies several such replacements). Calls through members (".", "->", "::")
and class methods are out of scope.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "catch", "defined", "else", "do"}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def strip_comments_and_strings(text: str) -> str:
    # Keep line structure so preprocessor lines and line numbers survive.
    def blank(match: re.Match[str]) -> str:
        token = match.group(0)
        if token.startswith("/"):
            return re.sub(r"[^\n]", " ", token)
        return token[0] + re.sub(r"[^\n]", " ", token[1:-1]) + token[-1]

    pattern = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)
    return pattern.sub(blank, text)


# Every OpenGL ES module build compiles with these, and never with the
# other renderer modules' macros. The platform decides whether the VR
# presenter is built (Android) or compiled out (desktop ES).
MODULE_MACROS = {
    "OPENQ4_RENDERER_MODULE": True,
    "OPENQ4_RENDERER_GLES_MODULE": True,
    "OPENQ4_RENDERER_GL_MODULE": False,
    "OPENQ4_RENDERER_VK_MODULE": False,
    "OPENQ4_SDL3_CONTEXT_IMPL": True,
}
CONFIGURATIONS = {
    "android": {**MODULE_MACROS, "__ANDROID__": True, "__linux__": True, "_WIN32": False, "__APPLE__": False},
    "desktop": {**MODULE_MACROS, "__ANDROID__": False},
}


class _Undecided(Exception):
    pass


def _evaluate(expression: str, macros: dict[str, bool | None]) -> bool | None:
    # Kleene evaluation of a #if condition: None when it depends on a macro
    # this scan does not know, so both branches count as compiled.
    tokens = re.findall(r"defined\s*\(\s*\w+\s*\)|defined\s+\w+|&&|\|\||!=|==|<=|>=|[!()<>]|\w+|\S", expression)
    position = 0

    def peek() -> str | None:
        return tokens[position] if position < len(tokens) else None

    def take() -> str:
        nonlocal position
        position += 1
        return tokens[position - 1]

    def primary() -> bool | None:
        token = take()
        if token == "!":
            value = primary()
            return None if value is None else not value
        if token == "(":
            value = disjunction()
            if take() != ")":
                raise _Undecided
            return value
        if token.startswith("defined"):
            return macros.get(re.findall(r"\w+", token)[-1], None)
        if token.isdigit():
            if peek() in ("==", "!=", "<", ">", "<=", ">="):
                raise _Undecided
            return int(token) != 0
        if re.fullmatch(r"[A-Za-z_]\w*", token):
            if peek() in ("==", "!=", "<", ">", "<=", ">="):
                raise _Undecided
            value = macros.get(token, None)
            return value
        raise _Undecided

    def conjunction() -> bool | None:
        value = primary()
        while peek() == "&&":
            take()
            right = primary()
            value = False if value is False or right is False else (None if value is None or right is None else True)
        return value

    def disjunction() -> bool | None:
        value = conjunction()
        while peek() == "||":
            take()
            right = conjunction()
            value = True if value is True or right is True else (None if value is None or right is None else False)
        return value

    try:
        value = disjunction()
    except (_Undecided, IndexError):
        return None
    return value if position == len(tokens) else None


def module_view(text: str, configuration: str = "android") -> str:
    # The text as one OpenGL ES configuration compiles it: lines in a branch
    # whose condition is decidably false there are blanked. A macro the file
    # itself #defines starts undefined and follows its #define and #undef.
    macros: dict[str, bool | None] = dict(CONFIGURATIONS[configuration])
    for name in re.findall(r"^\s*#\s*define\s+(\w+)", text, re.M):
        macros.setdefault(name, False)
    # each frame: [state of the current branch, whether an earlier branch may have been taken, was taken]
    frames: list[list] = []
    out = []

    def compiled() -> bool | None:
        state: bool | None = True
        for frame in frames:
            if frame[0] is False:
                return False
            if frame[0] is None:
                state = None
        return state

    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith("#"):
            out.append("" if compiled() is False else line)
            continue
        out.append("")
        directive = stripped[1:].strip()
        word = re.match(r"\w*", directive).group(0)
        rest = directive[len(word):].strip()
        if word in ("if", "ifdef", "ifndef"):
            if word == "if":
                value = _evaluate(rest, macros)
            else:
                value = macros.get(rest.split()[0], None) if rest else None
                if word == "ifndef" and value is not None:
                    value = not value
            frames.append([value, value is not False, value is True])
        elif word == "elif" and frames:
            frame = frames[-1]
            if frame[2]:
                frame[0] = False
            else:
                value = _evaluate(rest, macros)
                frame[0] = value if not frame[1] else (False if value is False else None)
                frame[1] = frame[1] or value is not False
                frame[2] = value is True
        elif word == "else" and frames:
            frame = frames[-1]
            frame[0] = False if frame[2] else (True if not frame[1] else None)
        elif word == "endif" and frames:
            frames.pop()
        elif word in ("define", "undef") and rest:
            state = compiled()
            if state is not False:
                name = rest.split("(")[0].split()[0]
                defined = word == "define"
                macros[name] = defined if state is True else None
    return "\n".join(out)


DEFINITION = re.compile(
    r"^(?P<head>[A-Za-z_][\w\s\*&:<>,]*?)\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^;{}]*?)\)\s*(?:const\s*)?\{",
    re.M,
)


def free_function_definitions(text: str) -> tuple[set[str], set[str]]:
    # (external names, static names) of free functions defined at column 0.
    external: set[str] = set()
    internal: set[str] = set()
    for match in DEFINITION.finditer(text):
        head = match.group("head")
        name = match.group("name")
        if name in KEYWORDS or "::" in head or head.rstrip().endswith("::"):
            continue
        words = head.split()
        if not words or words[0] in ("class", "struct", "union", "enum", "namespace", "typedef", "using"):
            continue
        if "static" in words:
            internal.add(name)
        else:
            external.add(name)
    return external, internal


def calls_to(text: str, names: set[str]) -> dict[str, list[int]]:
    found: dict[str, list[int]] = {}
    if not names:
        return found
    call = re.compile(r"(?<![\w.:>])(" + "|".join(sorted(map(re.escape, names))) + r")\s*\(")
    for number, line in enumerate(text.splitlines(), 1):
        for match in call.finditer(line):
            before = line[:match.start()].rstrip()
            # a declaration or definition, not a call: a type word or pointer
            # declarator right before the name
            previous = re.search(r"\b([A-Za-z_]\w*)\s*[\*&]?$", before)
            if previous and previous.group(1) not in KEYWORDS | {"case", "throw", "co_return"}:
                continue
            found.setdefault(match.group(1), []).append(number)
    return found


def emit(module: str) -> list[str]:
    listing = subprocess.run([sys.executable, str(ROOT / "tools/build/meson_sources.py"), "--emit", module],
                             cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    if not listing:
        raise AssertionError(f"source discovery listed nothing for {module}")
    return listing


def excluded_sources() -> list[str]:
    sources = read(ROOT / "tools/build/meson_sources.py")
    block = sources.split("RENDERER_GLES_EXCLUDED_SOURCES = (", 1)[1].split(")", 1)[0]
    return re.findall(r'"([^"]+\.cpp)"', block)


def closure_violations(module_sources: dict[str, str], excluded: dict[str, str],
                       linked: dict[str, str]) -> list[str]:
    # module_sources/excluded/linked: path -> source text (raw). A call is a
    # violation when some ES configuration compiles it and nothing that
    # configuration builds or links defines the function.
    excluded_names: set[str] = set()
    for text in excluded.values():
        excluded_names |= free_function_definitions(strip_comments_and_strings(text))[0]
    stripped = {path: strip_comments_and_strings(text) for path, text in {**linked, **module_sources}.items()}
    violations: dict[str, str] = {}
    for configuration in CONFIGURATIONS:
        provided: set[str] = set()
        views = {}
        for path, text in stripped.items():
            view = module_view(text, configuration)
            if path in module_sources:
                views[path] = view
            provided |= free_function_definitions(view)[0]
        missing = excluded_names - provided
        for path, view in sorted(views.items()):
            local = free_function_definitions(view)[1]
            for name, lines in sorted(calls_to(view, missing - local).items()):
                violations.setdefault(f"{path}:{name}", f"{path}:{lines[0]} calls {name}, defined only in a "
                                      f"translation unit the OpenGL ES module excludes ({configuration}, "
                                      f"lines {lines})")
    return [violations[key] for key in sorted(violations)]


def test_negative_controls() -> None:
    excluded = {"x.cpp": "void RB_Only( int a ) {\n}\nstatic void RB_Private( void ) {\n}\n"
                         "void RB_Replaced( void ) {\n}\nvoid idThing::Method( void ) {\n}\n"}
    linked = {"stub.cpp": "void RB_Replaced( void ) {\n}\n"}
    bad = {"a.cpp": "void f() {\n\tif ( !RB_Only( 1 ) ) {}\n\tRB_Replaced();\n\tobj.RB_Only( 2 );\n}\n"}
    if closure_violations(bad, excluded, linked) != [
            "a.cpp:2 calls RB_Only, defined only in a translation unit the OpenGL ES module excludes "
            "(android, lines [2])"]:
        raise AssertionError(f"the closure scan must flag an unguarded call: {closure_violations(bad, excluded, linked)}")
    good = {"b.cpp": "void RB_Only( int a );\n#if !defined( OPENQ4_RENDERER_GLES_MODULE )\nvoid f() { RB_Only( 1 ); }\n"
                     "#endif\n#ifdef OPENQ4_RENDERER_GLES_MODULE\nint g;\n#else\nint h = RB_Only( 2 );\n#endif\n"
                     "// RB_Only( 3 ) in a comment\nconst char *s = \"RB_Only( 4 )\";\n"}
    if closure_violations(good, excluded, linked):
        raise AssertionError(f"the closure scan must accept guarded calls: {closure_violations(good, excluded, linked)}")
    # a module translation unit's own definition, when the module compiles it, provides the symbol
    own = {"c.cpp": "void RB_Only( int a ) {\n}\nvoid f() { RB_Only( 1 ); }\n"}
    if closure_violations(own, excluded, linked):
        raise AssertionError("a definition the module builds must satisfy the closure")
    hidden = {"d.cpp": "#if !defined( OPENQ4_RENDERER_GLES_MODULE )\nvoid RB_Only( int a ) {\n}\n#endif\n"
                       "void f() { RB_Only( 1 ); }\n"}
    if not closure_violations(hidden, excluded, linked):
        raise AssertionError("a definition the module compiles out must not satisfy the closure")
    # Derived macros and platforms: gl_VRPresentation.cpp's shape. A call only
    # the GL module compiles passes; one Android's ES module compiles, or one
    # behind a macro the scan cannot decide, is flagged.
    derived = {"e.cpp": "#if defined( OPENQ4_RENDERER_GL_MODULE ) && !defined( OPENQ4_RENDERER_GLES_MODULE )\n"
                        "#define LOCAL_GL 1\n#elif defined( OPENQ4_RENDERER_GLES_MODULE ) && defined( __ANDROID__ )\n"
                        "#define LOCAL_GL 1\n#define LOCAL_ES 1\n#endif\n#if defined( LOCAL_GL )\n"
                        "#if defined( LOCAL_ES )\nint a;\n#else\nvoid g() { RB_Only( 1 ); }\n#endif\n"
                        "void h() { RB_Only( 2 ); }\n#endif\n#if defined( SOME_BUILD_FLAG )\nvoid k() { RB_Only( 3 ); }\n"
                        "#endif\n#if 0\nvoid m() { RB_Only( 4 ); }\n#endif\n"}
    flagged = closure_violations(derived, excluded, linked)
    if len(flagged) != 1 or "(android, lines [13, 16])" not in flagged[0]:
        raise AssertionError(f"the closure scan must follow derived macros: {flagged}")


def test_gles_module_symbol_closure() -> None:
    listing = emit("renderer_gles")
    excluded_paths = excluded_sources()
    for path in ("src/renderer/tr_render.cpp", "src/renderer/draw_common.cpp"):
        if path not in excluded_paths or path in listing:
            raise AssertionError(f"{path} is expected to stay out of the OpenGL ES module")
    module_sources = {path: read(ROOT / path) for path in listing if path.endswith(".cpp")}
    excluded = {path: read(ROOT / path) for path in excluded_paths if (ROOT / path).is_file()}
    linked = {path: read(ROOT / path) for module in ("render_geo", "imagetools")
              for path in emit(module) if path.endswith(".cpp")}
    violations = closure_violations(module_sources, excluded, linked)
    if violations:
        raise AssertionError("OpenGL ES module symbol closure:\n  " + "\n  ".join(violations))
    # The helpers moved for this closure stay where the module builds them.
    for name, home in (("void R_SetDrawInteraction( const shaderStage_t *surfaceStage,", "src/renderer/tr_backend.cpp"),
                       ("void RB_LightGridBakedParams( const LightGrid &grid,", "src/renderer/ModernGLExecutor.cpp")):
        owners = [path for path, text in module_sources.items() if name in text]
        if owners != [home]:
            raise AssertionError(f"{name.split('(')[0]} must be defined once in {home}, found {owners}")


def main() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    for test in tests:
        test()
    print("renderer_gles_symbol_closure: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
