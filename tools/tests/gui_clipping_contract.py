#!/usr/bin/env python3
"""Execute production GUI clipping and glyph submission without a renderer."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

from filesystem_case_segments import function_body


ROOT = Path(__file__).resolve().parents[2]

SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
struct idRectangle {
    float x, y, w, h;
    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
};
template<class T> struct idList {
    std::vector<T> values;
    int Num() const { return static_cast<int>(values.size()); }
    T &operator[](int index) { return values.at(index); }
};
struct idMaterial {};
struct Quad { float x, y, w, h, s1, t1, s2, t2; };
struct idDeviceContext {
    bool enableClipping = true;
    idList<idRectangle> clipRects;
    std::vector<Quad> submitted;
    float xScale = 1, yScale = 1, xOffset = 0, yOffset = 0;
    bool ClippedCoords(float *, float *, float *, float *, float *, float *, float *, float *);
    void PaintChar(float, float, float, float, float, float, float, float, float, const idMaterial *);
    void AdjustCoords(float *, float *, float *, float *);
    void DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *) {
        submitted.push_back({x, y, w, h, s1, t1, s2, t2});
    }
};
'''

MAIN = r'''
static unsigned cases = 0;
static void near(float actual, float expected) {
    assert(std::isfinite(actual));
    assert(std::fabs(actual - expected) < 0.002f);
}
static void check(idDeviceContext &dc, const Quad &original) {
    ++cases;
    // Determine the visible intersection independently, then compare the
    // actual PaintChar output, including interpolated normal/reversed UVs.
    float left = original.x, top = original.y;
    float right = original.x + original.w, bottom = original.y + original.h;
    if (dc.enableClipping) {
        // Slot zero is the legacy base canvas and intentionally does not clip.
        for (int i = 1; i < dc.clipRects.Num(); ++i) {
            const auto &clip = dc.clipRects[i];
            left = std::max(left, clip.x); top = std::max(top, clip.y);
            right = std::min(right, clip.Right()); bottom = std::min(bottom, clip.Bottom());
        }
    }
    const bool visible = right > left && bottom > top;
    dc.submitted.clear();
    dc.PaintChar(original.x, original.y, original.w * 2, original.h * 2, .5f,
        original.s1, original.t1, original.s2, original.t2, nullptr);
    assert(dc.submitted.size() == (visible ? 1u : 0u));
    if (!visible) return;
    const Quad &actual = dc.submitted.front();
    near(actual.x, left * dc.xScale + dc.xOffset);
    near(actual.y, top * dc.yScale + dc.yOffset);
    near(actual.w, (right - left) * dc.xScale);
    near(actual.h, (bottom - top) * dc.yScale);
    near(actual.s1, original.s1 + (original.s2 - original.s1) * ((left - original.x) / original.w));
    near(actual.s2, original.s1 + (original.s2 - original.s1) * ((right - original.x) / original.w));
    near(actual.t1, original.t1 + (original.t2 - original.t1) * ((top - original.y) / original.h));
    near(actual.t2, original.t1 + (original.t2 - original.t1) * ((bottom - original.y) / original.h));
}
int main() {
    idDeviceContext dc;
    dc.clipRects.values = {{0, 0, 640, 480}, {100, 100, 50, 40}};
    // Fully outside each edge, exactly touching each edge, partial overlaps,
    // enclosing quads, empty/negative quads, and both atlas orientations.
    const std::vector<Quad> edges = {
        {70, 110, 10, 10}, {160, 110, 10, 10}, {110, 70, 10, 10}, {110, 150, 10, 10},
        {90, 110, 10, 10}, {150, 110, 10, 10}, {110, 90, 10, 10}, {110, 140, 10, 10},
        {90, 110, 20, 10}, {140, 110, 20, 10}, {110, 90, 10, 20}, {110, 130, 10, 20},
        {80, 80, 100, 100}, {110, 110, 10, 10}, {110, 110, 0, 10}, {110, 110, 10, 0},
        {110, 110, -10, 10}, {110, 110, 10, -10}
    };
    for (Quad quad : edges) for (int reversed = 0; reversed != 2; ++reversed) {
        quad.s1 = reversed ? .9f : .1f; quad.s2 = reversed ? .1f : .9f;
        quad.t1 = reversed ? .8f : .2f; quad.t2 = reversed ? .2f : .8f;
        check(dc, quad);
    }
    // No texture coordinates is also supported by the filled-rectangle path.
    float x = 110, y = 70, w = 10, h = 10;
    assert(dc.ClippedCoords(&x, &y, &w, &h, nullptr, nullptr, nullptr, nullptr));
    x = 90; y = 110; w = 20; h = 10;
    assert(!dc.ClippedCoords(&x, &y, &w, &h, nullptr, nullptr, nullptr, nullptr));
    near(x, 100); near(w, 10);
    // A wrapped chat history moves many old lines above its short viewport.
    // None may submit an inverted quad or sample outside its original glyph.
    dc.clipRects.values = {{0, 0, 640, 480}, {60, 0, 520, 480}, {94, 368, 438, 38}};
    dc.xScale = dc.yScale = 1.125f; dc.xOffset = 120;
    for (int row = -32; row < 8; ++row) for (int column = -2; column < 65; ++column) {
        check(dc, {98.f + column * 7, 372.f + row * 12, 8, 11, .3f, .4f, .35f, .46f});
    }
    // Overlapping nested clips trim all four sides, and disjoint/empty parent
    // viewports cannot resurrect a previously clipped child quad.
    dc.clipRects.values = {{0, 0, 640, 480}, {100, 100, 50, 40}, {120, 90, 50, 30}};
    check(dc, {80, 80, 100, 100, .8f, .9f, .1f, .2f});
    dc.clipRects.values[1] = {180, 180, 10, 10};
    check(dc, {80, 80, 200, 200, .1f, .2f, .8f, .9f});
    dc.clipRects.values[1] = {100, 100, 0, 40};
    check(dc, {80, 80, 200, 200, .1f, .2f, .8f, .9f});
    dc.clipRects.values[1] = {100, 100, 50, 0};
    check(dc, {80, 80, 200, 200, .1f, .2f, .8f, .9f});
    // Retain explicit clipping disable and the base-canvas skip, including
    // expanded widescreen backgrounds that are drawn beyond the base canvas.
    dc.enableClipping = false;
    check(dc, {-100, -100, 900, 700, 0, 0, 1, 1});
    dc.enableClipping = true;
    dc.clipRects.values = {{100, 100, 50, 40}};
    check(dc, {-100, -100, 900, 700, 0, 0, 1, 1});
    dc.clipRects.values.clear();
    check(dc, {-100, -100, 900, 700, 0, 0, 1, 1});
    std::printf("gui_clipping_contract: PASS (%u production glyph submissions, viewport bounds and UVs)\n", cases);
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "src/ui/DeviceContext.cpp")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    functions = "\n".join(function_body(source, signature) for signature in (
        "bool idDeviceContext::ClippedCoords(",
        "void idDeviceContext::AdjustCoords(",
        "void idDeviceContext::PaintChar(",
    ))
    compiler = next((path for name in ("clang++", "g++", "c++") if (path := shutil.which(name))), None)
    if compiler is None:
        raise RuntimeError("a C++ compiler is required")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wno-missing-field-initializers"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="gui-clipping-", dir=ROOT / ".tmp") as directory:
        source_path, binary = Path(directory) / "clipping.cpp", Path(directory) / "clipping.exe"
        source_path.write_text(SUPPORT + functions + MAIN, encoding="utf-8")
        subprocess.run([compiler, *flags, str(source_path), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
