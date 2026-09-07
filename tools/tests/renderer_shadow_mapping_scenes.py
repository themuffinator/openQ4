#!/usr/bin/env python3
"""Write stock-asset console fixtures for windowed shadow-mapping tests.

Put the generated cfg under the test's fs_savepath/baseoq4, then execute it
after the relevant map has loaded. These commands manipulate engine state;
they do not inject mouse/keyboard input. Capture with the engine screenshot
command after the fixture completes. See docs/dev/shadowmapping-audit-2026-09-05.md.
"""

import argparse
from pathlib import Path


def scene_commands(scene: str) -> list[str]:
    if scene == "recomp-decal":
        # Stock lit slime decals explicitly disable shadows. A steady point
        # light makes their accidental solid stencil silhouette visible.
        return ["g_stopTime 0",
                'testPointLight 600 name shadow_recomp_probe origin "-2944 -1824 -16" '
                'texture "lights/round" _color "0.6 0.6 0.6"',
                "wait 30", "g_stopTime 1", "viewpos",
                "rendererShadowPlannerSelfTest", "rendererShadowProjectedDiagnosticSelfTest"]
    if scene == "point-origin":
        # Stock Air Defense 2's first corridor: this light exposed false
        # Vulkan radial depth from world triangles crossing the light plane.
        return ["r_singleLight 168", "r_shadowMapStaticCache 0",
                "g_renderFastNoPost 1", "g_renderFastNoPostDirect 1", "wait 30"]
    if scene in ("flashlight", "flashlight-budget"):
        commands = ["testFlashlight 1", "wait 120"]
        # Air Defense 2 starts with a flashlight-capable weapon. Retain its
        # actual view/world light and player suppression rules.
        if scene == "flashlight-budget":
            commands[:0] = ["r_shadowMapMaxUpdatesPerView 1", "r_shadowMapStaticCache 0"]
        return commands
    if scene != "stress":
        raise ValueError(f"Unknown shadow scene: {scene}")
    commands = [
        "ui_showGun 0", "g_showHud 0", "noclip",
        "setviewpos 0 -320 180 24 90 0",
        "r_shadowMapSize 256", "r_shadowMapPointSize 128",
        "r_shadowMapMaxUpdatesPerView 0",
    ]
    for y in range(4):
        for x in range(4):
            commands.append(
                f"spawn func_static name audit_crate_{y}_{x} "
                "model models/mapobjects/strogg/crates/crate1_small.lwo "
                f'origin "{-135 + x * 90} {-100 + y * 90} -5.7"'
            )
    for i in range(20):
        commands.append(
            f'testPointLight 650 origin "{-190 + (i % 5) * 95} '
            f'{-180 + (i // 5) * 90} 180" _color "0.025 0.025 0.025"'
        )
        commands.append(
            "testLight lights/squarelight1 texture lights/squarelight1 "
            f'origin "{-100 + (i % 5) * 50} -220 {130 + (i // 5) * 20}" '
            '_color "0.025 0.025 0.025"'
        )
    return commands + ["wait 120"]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scene", choices=("flashlight", "flashlight-budget", "point-origin", "stress", "recomp-decal"))
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(scene_commands(args.scene)) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
