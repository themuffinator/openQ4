#!/usr/bin/env python3
"""Build the original openQ4 parallax occlusion laboratory below .tmp (REN-11).

A floor and a wall each carry a PBR heightMap specimen beside its flat control,
the same material without a height source. Every texture is generated here
from one procedural height field (raised bevelled tiles with a dome in each),
so the normal map, the albedo pattern and the height agree exactly. Retail
PK4s supply only the game, player and engine's standard entities; nothing is
written to shipped content.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

import generate_pbr_fixture as fixture
import generate_pbr_validation_map as pbr_lab

ROOT = fixture.ROOT
MAP = "maps/tools/openq4_parallax_lab"
PREFIX = "textures/openq4/parallax_lab"
SIZE = 128
TILES = 4
HEIGHT_SCALE = 0.04
# The specimens and their flat controls, left and right of the centre line.
# A brush maps 128 units to one texture repeat (generate_pbr_validation_map).
FLOOR_POM = ((-384, 0, -16), (0, 768, 0))
FLOOR_FLAT = ((0, 0, -16), (384, 768, 0))
WALL_POM = ((-384, 768, 0), (0, 784, 384))
WALL_FLAT = ((0, 768, 0), (384, 784, 384))
# The retail Parallaxbump guide, between the start and the specimens.
RETAIL = ((-384, -380, -16), (384, -60, 0))
# Behind the start: authored ORM packing, and the two specimens that must
# draw flat (perforated coverage, emission).
STRIP = (("pom_orm", ((-384, -700, -16), (-128, -444, 0))),
         ("pom_cutout", ((-128, -700, -16), (128, -444, 0))),
         ("pom_emissive", ((128, -700, -16), (384, -444, 0))))


def tile_height(u: float, v: float) -> float:
    """Height in [0, 1] at texture coordinate (u, v) in [0, 1): bevelled
    tiles separated by grooves, each with a low dome on top."""
    cu, cv = (u * TILES) % 1.0, (v * TILES) % 1.0
    edge = min(cu, 1 - cu, cv, 1 - cv)
    bevel = min(1.0, max(0.0, (edge - 0.06) / 0.12))
    dome = max(0.0, 1.0 - ((cu - 0.5) ** 2 + (cv - 0.5) ** 2) / 0.12)
    return 0.15 + 0.6 * bevel + 0.25 * bevel * dome


def height_field() -> list[list[float]]:
    return [[tile_height((x + 0.5) / SIZE, (y + 0.5) / SIZE) for x in range(SIZE)] for y in range(SIZE)]


def normal_pixels(field: list[list[float]]) -> list[tuple[int, int, int]]:
    """Tangent-space XYZ normals of the displaced surface z = h * scale (in
    texture units); row 0 is the top of the image, where v is smallest."""
    pixels = []
    step = HEIGHT_SCALE * SIZE / 2
    for y in range(SIZE):
        for x in range(SIZE):
            du = (field[y][(x + 1) % SIZE] - field[y][(x - 1) % SIZE]) * step
            dv = (field[(y + 1) % SIZE][x] - field[(y - 1) % SIZE][x]) * step
            length = math.sqrt(du * du + dv * dv + 1)
            n = (-du / length, -dv / length, 1 / length)
            pixels.append(tuple(round(127.5 * (1 + c)) for c in n))
    return pixels


def albedo_pixels(field: list[list[float]]) -> list[tuple[int, int, int]]:
    """Alternating warm and cool tiles with dark grooves, so a texel that
    parallax moves is visible as colour as well as shading."""
    pixels = []
    for y in range(SIZE):
        for x in range(SIZE):
            tile = (x * TILES // SIZE + y * TILES // SIZE) % 2
            base = (210, 150, 90) if tile else (90, 150, 210)
            shade = 0.35 + 0.65 * min(1.0, max(0.0, (field[y][x] - 0.15) / 0.6))
            pixels.append(tuple(round(c * shade) for c in base))
    return pixels


def material(name: str, *, height: bool, orm: bool = False, cutout: bool = False, emissive: bool = False) -> str:
    color = f"{PREFIX}/{'cutout' if cutout else 'albedo'}"
    lines = [f"{PREFIX}/{name}", "{", "bumpmap _flat"]
    if cutout:
        lines += ["{", "blend diffusemap", f"map {color}", "alphaTest 0.5", "}"]
    else:
        lines += [f"diffusemap {color}"]
    lines += ["specularmap _black"]
    if emissive:
        lines += ["{", "blend add", f"map {color}", "rgb 0.5", "}"]
    lines += ["pbr {", "workflow metallicRoughness", f"albedoMap {color}",
              f"normalMap {PREFIX}/normal", "normalFormat tangentXYZ", "roughness 0.55", "metallic 0"]
    if orm:
        lines += [f"ormMap {PREFIX}/orm"]
    if height:
        lines += [f"heightMap {PREFIX}/height", f"heightScale {HEIGHT_SCALE:g}", "heightReference 1"]
    if emissive:
        lines += [f"emissiveMap {color}", "emissiveColor 0.5, 0.5, 0.5"]
    return "\n".join(lines + ["}", "}", ""])


def payloads() -> dict[str, bytes]:
    data: dict[str, bytes] = {}

    def add(qpath: str, value: str | bytes) -> None:
        data["baseoq4/" + qpath] = value.encode("utf-8") if isinstance(value, str) else value

    field = height_field()
    grey = [tuple(round(255 * h) for _ in range(3)) for row in field for h in row]
    add(f"{PREFIX}/height.tga", fixture.tga_bytes(SIZE, SIZE, grey))
    add(f"{PREFIX}/normal.tga", fixture.tga_bytes(SIZE, SIZE, normal_pixels(field)))
    albedo = albedo_pixels(field)
    add(f"{PREFIX}/albedo.tga", fixture.tga_bytes(SIZE, SIZE, albedo))
    add(f"{PREFIX}/cutout.tga", pbr_lab.rgba_tga(SIZE, [(*rgb, 255 if (i % SIZE) // 16 % 4 else 0)
                                                        for i, rgb in enumerate(albedo)]))
    # Authored ORM data: occlusion in the grooves, uniform roughness, no metal.
    add(f"{PREFIX}/orm.tga", fixture.tga_bytes(SIZE, SIZE, [(round(255 * (0.5 + 0.5 * h)), 140, 0)
                                                           for row in field for h in row]))
    add(f"{PREFIX}/dark.tga", fixture.tga_bytes(8, 8, [(60, 60, 60)] * 64))
    # Raven's RXGB layout for the retail Parallaxbump guide: the normal in
    # alpha/green/blue and the height in the otherwise unused red channel.
    normals = normal_pixels(field)
    heights = [h for row in field for h in row]
    add(f"{PREFIX}/retail_local.tga", pbr_lab.rgba_tga(SIZE, [(round(255 * h), n[1], n[2], n[0])
                                                              for h, n in zip(heights, normals)]))

    mats = [material("pom", height=True), material("flat", height=False),
            material("pom_orm", height=True, orm=True), material("flat_orm", height=False, orm=True),
            material("pom_cutout", height=True, cutout=True), material("pom_emissive", height=True, emissive=True),
            f"{PREFIX}/room\n{{\nbumpmap _flat\ndiffusemap {PREFIX}/dark\nspecularmap _black\n}}\n",
            # The retail guide's stage, as Raven's parallax_bump would expand
            # it: Vulkan draws it natively; OpenGL has no such program.
            "\n".join([f"{PREFIX}/retail", "{", "{", 'glslProgram "glsl/Parallaxbump.glsl"', "customLighting",
                       f"shaderParm vScaleBias {HEIGHT_SCALE:g}, {-HEIGHT_SCALE:g}",
                       f"shaderTexture DiffuseMap {PREFIX}/albedo", "shaderTexture SpecularMap _black",
                       f"shaderTexture NormalMap {PREFIX}/retail_local",
                       "shaderTexture SpecularTableImage specularTableImage",
                       "shaderTexture LightFalloffImage lightfalloffImage", "shaderTexture LightImage lightImage",
                       "}", "}", ""]),
            "lights/openq4/parallax_lab/ambient\n{\nambientLight\nnoShadows\nlightFalloffImage _white\n{\nmap _white\ncolored\n}\n}\n"]
    add("materials/openq4_parallax_lab.mtr", "\n".join(mats))

    room = [((-800, -800, -32), (800, 1200, -16)), ((-800, -800, 800), (800, 1200, 816)),
            ((-816, -800, -16), (-800, 1200, 800)), ((800, -800, -16), (816, 1200, 800)),
            ((-800, -816, -16), (800, -800, 800)), ((-800, 1200, -16), (800, 1216, 800))]
    level = 'Version 3\n{\n"classname" "worldspawn"\n'
    level += "".join(f'"mtr_parallax_{name}" "{PREFIX}/{name}"\n'
                     for name in ("pom_orm", "flat_orm", "pom_cutout", "pom_emissive"))
    level += "".join(pbr_lab.brush(lo, hi, f"{PREFIX}/room") for lo, hi in room)
    level += pbr_lab.brush(*FLOOR_POM, f"{PREFIX}/pom") + pbr_lab.brush(*FLOOR_FLAT, f"{PREFIX}/flat")
    level += pbr_lab.brush(*WALL_POM, f"{PREFIX}/pom") + pbr_lab.brush(*WALL_FLAT, f"{PREFIX}/flat")
    level += "".join(pbr_lab.brush(lo, hi, f"{PREFIX}/{name}") for name, (lo, hi) in STRIP)
    level += pbr_lab.brush(*RETAIL, f"{PREFIX}/retail")
    # A classic pillar on each half casts the key light's shadow across both
    # specimens, so shadowed lights draw through their receiver programs.
    level += "".join(pbr_lab.brush((x - 12, 340, 0), (x + 12, 364, 120), f"{PREFIX}/room") for x in (-200, 200))
    level += "}\n"
    level += pbr_lab.entity(classname="info_player_start", name="lab_start", origin="0 -400 0", angle="90")
    level += pbr_lab.entity(classname="light", name="key", origin="0 420 260", light_radius="900 900 700",
                            _color="1 0.95 0.9", noshadows="0")
    # A low raking light across the specimens: their tiles cast self-shadows.
    level += pbr_lab.entity(classname="light", name="rake", origin="-700 380 70", light_radius="1500 1500 700",
                            _color="0.7 0.65 0.6", noshadows="1")
    level += pbr_lab.entity(classname="light", name="fill", texture="lights/openq4/parallax_lab/ambient",
                            origin="0 300 300", light_radius="1000 1000 1000", _color="0.08 0.08 0.1", noshadows="1")
    add(MAP + ".map", level)
    return data


# setviewpos x y z pitch yaw roll; the grazing view looks across the floor
# toward the wall, the steep view straight down at the floor seam.
CAMERAS = {
    "grazing": [0, -60, 72, 10, 90, 0],
    "steep": [0, 380, 300, 80, 90, 0],
    "wall": [0, 300, 190, 0, 90, 0],
    "far": [0, -780, 760, 22, 90, 0],
    "strip": [0, -420, 260, 55, 270, 0],
    # Cost: the traced floor fills most of the frame at a grazing angle, the
    # case that takes the most samples; "timingFlat" frames its flat control.
    "retail": [0, -430, 70, 12, 90, 0],
    "timing": [-192, 40, 36, 14, 90, 0],
    "timingFlat": [192, 40, 36, 14, 90, 0],
}


def generate(root: Path, force: bool = False) -> dict:
    root = fixture.validate_runtime_root(root)
    files = payloads()
    manifest = {"schema": 1, "id": "openq4-parallax-laboratory", "map": MAP,
                "source": "Original procedural openQ4 test data; no incorporated external assets.",
                "heightScale": HEIGHT_SCALE, "cameras": CAMERAS,
                "specimens": {"floorPom": FLOOR_POM, "floorFlat": FLOOR_FLAT, "wallPom": WALL_POM, "wallFlat": WALL_FLAT},
                "sha256": {k: hashlib.sha256(v).hexdigest() for k, v in sorted(files.items())}}
    files["parallax-lab.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    paths = {name: root / name for name in files}
    fixture.validate_targets(root, paths, force)
    for name, payload in files.items():
        target = fixture.validate_contained_target(root, paths[name])
        if target.is_file() and target.read_bytes() == payload:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-root", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    manifest = generate(args.runtime_root, args.force)
    print(f"Generated {manifest['map']}: {len(manifest['sha256'])} original assets")
