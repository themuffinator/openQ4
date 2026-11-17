#!/usr/bin/env python3
"""The one-pass light-interaction cull bits equal the per-plane SSE2 passes, bit for bit.

R_CalcInteractionCullBits (src/rendergeo/RenderGeometryInteraction.cpp) uses
R_CullBitsOnePass (src/rendergeo/InteractionCullBits.h) on x64 while the SSE2
SIMD processor is active. This builds tools/tests/native/InteractionCullBitsTest.cpp
with the engine's own idSIMD_SSE2::Dot( float *, idPlane, idDrawVert * ) and
CmpLT( byte *, bitNum, ... ) pasted in as the reference, and compares every
vertex for random planes, vertices placed on the epsilon, every skip mask and
vertex counts 0-52. It also checks the call site keeps the per-plane passes for
other processors and for MD5R sil-trace vertices.
"""
from __future__ import annotations

import platform
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:i]
    raise ValueError(signature)


def main() -> int:
    simd = (ROOT / "src/idlib/math/Simd_SSE2.cpp").read_text(encoding="utf-8")
    call = (ROOT / "src/rendergeo/RenderGeometryInteraction.cpp").read_text(encoding="utf-8", errors="surrogateescape")
    fn = call[call.index("void R_CalcInteractionCullBits("):]
    fn = fn[:fn.index("\n}\n")]
    for token in ("silTraceVerts == NULL &&", "R_SIMDIsSSE2() ) {", "R_CullBitsOnePass( cullInfo.cullBits, cullInfo.localClipPlanes, frontBits,",
                  "SIMDProcessor->Dot( planeSide, cullInfo.localClipPlanes[i], tri->verts, tri->numVerts );"):
        if token not in fn:
            print(f"FAIL: R_CalcInteractionCullBits lost '{token}'")
            return 1
    if fn.index("R_CullBitsOnePass(") > fn.index("SIMDProcessor->Dot("):
        print("FAIL: the one-pass kernel must be tried before the per-plane passes")
        return 1

    if platform.machine().lower() not in ("amd64", "x86_64"):
        print("interaction_cull_bits_test: static checks ok (kernel is x64-only; not run on this host)")
        return 0
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        print("FAIL: clang++ or g++ required")
        return 1

    dot = body(simd, "void VPCALL idSIMD_SSE2::Dot( float * RESTRICT dst, const idPlane &constant, const idDrawVert * RESTRICT src, const int count )")
    pack = simd[simd.index("ID_INLINE static __m128i SSE2_PackCmpMasks"):]
    pack = pack[:pack.index("\n}\n") + 3]
    macro = simd[simd.index("#define SSE2_CMP_CONSTANT_BITNUM( CMP_PS, SCALAR_OP )"):]
    macro_lines = []
    for line in macro.splitlines()[1:]:
        macro_lines.append(line.rstrip("\\").rstrip())
        if not line.rstrip().endswith("\\"):
            break
    cmplt = "\n".join(macro_lines).replace("CMP_PS", "_mm_cmplt_ps").replace("SCALAR_OP", "<")

    fixture = (ROOT / "tools/tests/native/InteractionCullBitsTest.cpp").read_text(encoding="utf-8")
    fixture = fixture.replace("/*@DOT@*/", dot).replace("/*@PACK@*/", pack).replace("/*@CMPLT@*/", cmplt)
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "tools/tests/native/InteractionCullBitsTest.cpp"
        source.parent.mkdir(parents=True)
        source.write_text(fixture, encoding="utf-8")
        # the fixture includes ../../../src/rendergeo/InteractionCullBits.h
        header = Path(tmp) / "src/rendergeo"
        header.mkdir(parents=True)
        shutil.copy(ROOT / "src/rendergeo/InteractionCullBits.h", header / "InteractionCullBits.h")
        exe = Path(tmp) / ("cullbits.exe" if sys.platform == "win32" else "cullbits")
        flags = ["-std=c++17", "-O2", "-ffp-contract=off"]
        build = subprocess.run([compiler, *flags, str(source), "-o", str(exe)], capture_output=True, text=True)
        if build.returncode:
            print(build.stdout + build.stderr)
            print("FAIL: build")
            return 1
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout.strip())
        if run.returncode:
            print("FAIL: mismatch")
            return 1
    print("interaction_cull_bits_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
