#!/usr/bin/env python3
"""A light freed while its frame draws leaves the interaction table at once.

FreeLightDef defers a light the current frame still references (or every light
while the render thread has a frame in flight) until FreeDeferredLightDefs runs
at the frame's end, and drops it from lightDefs straight away. Until 26 November
2026 its interactions stayed in the interaction table meanwhile. A table built
before the frame ended - the first game frame's GenerateAllInteractions after a
map or save load walks lightDefs - never listed them, and a light reusing the
handle could take their slots, so freeing them at the frame's end raised
"idInteraction::UnlinkAndFree: interactionTable wasn't set", a fatal error. The
Awakening's first level hit it on every save made at its opening, from 0.15.0 on.

The deferred light now clears its own table slots and is marked detached, and
every table read and write skips a detached light.
"""
from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing {signature}")
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[brace:index + 1]
    raise AssertionError(f"unterminated {signature}")


def require(text: str, token: str, why: str) -> None:
    if token not in text:
        raise AssertionError(f"missing {token!r}: {why}")


def main() -> int:
    header = read("src/rendergeo/RenderGeometry.h")
    require(header, "bool					interactionTableDetached;", "the light records that it left the table")
    require(read("src/rendergeo/RenderGeometryLocalDefs.cpp"), "interactionTableDetached	= false;",
            "a new light starts in the table")

    free = body(read("src/renderer/RenderWorld.cpp"), "void idRenderWorldLocal::FreeLightDef(")
    deferred = free.find("deferredFreeLightDefs.Append( light );")
    clear = free.find("interactionTable[index] = NULL;")
    detach = free.find("light->interactionTableDetached = true;")
    if min(deferred, clear, detach) < 0 or not clear < detach < deferred:
        raise AssertionError("a deferred light must clear its table slots and be marked detached before it is queued")
    require(free, "if ( interactionTable[index] == inter ) {", "only the light's own slots are cleared")

    interaction = read("src/renderer/Interaction.cpp")
    require(body(interaction, "void idInteraction::UnlinkAndFree("),
            "if ( renderWorld->interactionTable && !this->lightDef->interactionTableDetached ) {",
            "freeing a detached light's interaction leaves the table alone")
    require(body(interaction, "idInteraction *idInteraction::AllocAndLink("),
            "if ( renderWorld->interactionTable && !ldef->interactionTableDetached ) {",
            "an interaction of a detached light never enters the table")
    require(read("src/renderer/tr_light.cpp"),
            "if ( r_useInteractionTable.GetBool() && this->interactionTable && !ldef->interactionTableDetached ) {",
            "a detached light never looks itself up in the table")

    print("renderer_deferred_light_interaction_table: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
