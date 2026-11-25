#!/usr/bin/env python3
"""dmap marks the areas that see a "sky" side, and the .proc records it (#255).

Quake 4's .proc gives each area model a sky flag. The renderer draws a map's
portal sky only through flagged areas, and a skycaulk side, whose material is
marked "sky", draws nothing itself. openQ4's dmap wrote 0 for every area, so a
map it compiled drew a black sky. Retail Air Defense 1 flags _area1, _area5,
_area6 and _area39, and the fixed dmap flags the same four.

This compiles the production MarkSkyArea against stub types and checks that
both places dmap puts a side's fragment into an area mark it, and that the
area model header writes the flag.
"""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdio>
enum { MF_SKY = 1 << 7, MF_NOSHADOWS = 1 << 2 };
struct idMaterial {
    int flags = 0;
    bool TestMaterialFlag(int flag) const { return (flags & flag) != 0; }
};
struct side_t { const idMaterial* material = nullptr; };
struct uArea_t { void* groups = nullptr; bool sky = false; };
struct uEntity_t { uArea_t* areas = nullptr; int numAreas = 0; };
// @PRODUCTION@
int main() {
    uArea_t areas[3] = {};
    uEntity_t e; e.areas = areas; e.numAreas = 3;
    idMaterial caulk; caulk.flags = MF_SKY | MF_NOSHADOWS;
    idMaterial wall;
    side_t sky; sky.material = &caulk;
    side_t plain; plain.material = &wall;
    side_t generated;
    MarkSkyArea(&e, &plain, 0);
    MarkSkyArea(&e, &generated, 0);
    assert(!areas[0].sky);
    MarkSkyArea(&e, &sky, 1);
    assert(areas[1].sky && !areas[0].sky && !areas[2].sky);
    // outside areas (solid -1, crossing -2) and out-of-range numbers are ignored
    MarkSkyArea(&e, &sky, -1); MarkSkyArea(&e, &sky, -2); MarkSkyArea(&e, &sky, 3);
    assert(!areas[0].sky && !areas[2].sky);
    std::puts("ok");
    return 0;
}
'''


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


def main() -> None:
    usurface = (ROOT / "src/tools/compilers/dmap/usurface.cpp").read_text(encoding="latin-1").replace("\r\n", "\n")
    output = (ROOT / "src/tools/compilers/dmap/output.cpp").read_text(encoding="latin-1").replace("\r\n", "\n")
    header = (ROOT / "src/tools/compilers/dmap/dmap.h").read_text(encoding="latin-1").replace("\r\n", "\n")

    mark = function(usurface, "static void MarkSkyArea(")
    assert "MF_SKY" in mark, "MarkSkyArea must test the material's sky flag"
    put = function(usurface, "static void PutWindingIntoAreas_r(")
    # the single-area shortcut and the leaf both mark before making triangles,
    # which a skycaulk side never produces
    assert put.count("MarkSkyArea( e, side, area );") == 1, "the single-area shortcut must mark the area"
    assert put.count("MarkSkyArea( e, side, node->area );") == 1, "the leaf must mark its area"
    assert put.index("MarkSkyArea( e, side, area );") < put.index("TriListForSide( side, w );")
    assert "bool					sky;" in header or "bool sky;" in header, "uArea_t must carry the sky flag"
    write = function(output, "static void WriteOutputSurfaces(")
    assert '/* sky */ %i' in write and "area->sky ? 1 : 0" in write, "the area model header must write the sky flag"

    compiler = next((p for name in ("clang++", "g++", "c++") if (p := shutil.which(name))), None)
    if compiler is None:
        print("dmap_sky_areas: source contract ok (no C++ compiler for the unit check)")
        return
    tmp = ROOT / ".tmp"
    tmp.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="dmap-sky-", dir=tmp) as work:
        source = Path(work) / "sky.cpp"
        exe = Path(work) / "sky.exe"
        source.write_text(HARNESS.replace("// @PRODUCTION@", mark), encoding="utf-8")
        subprocess.run([compiler, "-std=c++17", str(source), "-o", str(exe)], check=True)
        result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        assert result.stdout.strip() == "ok", result.stdout
    print("dmap_sky_areas: ok")


if __name__ == "__main__":
    main()
