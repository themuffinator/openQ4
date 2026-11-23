#!/usr/bin/env python3
"""Build the openQ4 PBR showcase map from locally downloaded real-world materials.

The showcase complements the procedural PBR laboratory (generate_pbr_validation_map.py):
the laboratory proves exact responses on calibrated flat data, the showcase shows how
authored, scanned materials look and behave under the shipped lighting, shadows and
probes. Its material list is pbr_showcase_materials.json.

The textures come from textures.com and are licensed to the person who downloads them.
Its terms forbid redistribution, so nothing they produce may enter Git or a pak: this
script reads a drop folder you fill yourself and writes only below an ignored runtime
root in the repository's .tmp directory.

    python tools/validation/generate_pbr_showcase_map.py --check --source-dir <drop>
    python tools/validation/generate_pbr_showcase_map.py --runtime-root .tmp/runtime/pbr-showcase --source-dir <drop>
    python tools/validation/generate_pbr_showcase_map.py --runtime-root .tmp/runtime/pbr-showcase --synthetic

--synthetic replaces every download with procedural stand-ins, so the pipeline, the
map and its captures can be tested without an account.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import re
import struct
import sys
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image

import generate_pbr_fixture as fixture
import generate_pbr_validation_map as lab

ROOT = fixture.ROOT
MANIFEST = Path(__file__).with_name("pbr_showcase_materials.json")
MAP = "maps/tools/openq4_pbr_showcase"
PREFIX = "textures/openq4/pbr_showcase"
LIGHT_PREFIX = "lights/openq4/pbr_showcase"
SPHERE_RADIUS = 48
UNITS_PER_METER = 39.37  # one id unit is an inch
DEFAULT_SOURCE = ROOT / ".tmp" / "pbr-showcase" / "downloads"
# Authoring bay (REN-05): the runner spawns one specimen at a time here, the way
# a mod would author it: a skin swapping a PBR material for another PBR material
# or for a classic one, and one model that carries a PBR and a classic material.
AUTHORING_ORIGIN = (360, -420, 96)
AUTHORING_CAMERA = (360, -720, 96, 0, 90, 0)
CLASSIC_SUFFIX = "_classic"
SKIN_PREFIX = "openq4_pbr_showcase"
MIXED_MODEL = "openq4_pbr_showcase_mixed"
MIXED_MESH = "models/openq4/pbr_showcase/mixed.md5mesh"
MIXED_ANIM = "models/openq4/pbr_showcase/mixed_idle.md5anim"
MIXED_RADIUS = 72  # generate_pbr_validation_map.skinned_sphere
# The fence stands at y = -260 between the projector and the stations.
FENCE_CLOSE_CAMERA = (-260, -302, 110, 0, 90, 0)
FENCE_SHADOW_CAMERA = (-260, -200, 150, 60, 90, 0)
# Authored probes (REN-06). The room probe is a box the size of the room,
# captured in the engine (bakeReflectionProbes); the stock probe is a sphere
# the runner spawns in a stock map. Their faces live in each run's save path.
PROBE_PREFIX = "env/openq4/pbr_showcase"
PROBE_FACES = ("px", "nx", "py", "ny", "pz", "nz")
# Authored decals: unlit blend stages over PBR receivers, aligned to the
# 128-unit texture grid so each quad shows its texture once.
DECAL_SIZE = 128
DECALS = {
    # name: (blend, brush lo, brush hi, camera)
    "wall": ("filter", (639.75, -640, 128), (640, -512, 256), (400, -576, 192, 0, 0, 0)),
    "floor": ("blend", (384, -768, 0), (512, -640, 0.25), (448, -704, 200, 90, 0, 0)),
}


def decal_textures() -> dict[str, bytes]:
    """Original procedural decal art: a scorch for 'blend filter', a stencil for 'blend blend'."""
    y, x = (np.mgrid[0:DECAL_SIZE, 0:DECAL_SIZE] + 0.5) / DECAL_SIZE - 0.5
    radius = np.hypot(x, y)
    scorch = 1.0 - 0.85 * np.exp(-(radius / 0.22) ** 2)
    scorch[radius > 0.47] = 1.0  # nothing reaches the quad's edge
    stripes = ((x + y) * 8 % 1.0 < 0.5) & (np.abs(x) < 0.4) & (np.abs(y) < 0.4)
    stencil = np.zeros((DECAL_SIZE, DECAL_SIZE, 4))
    stencil[..., 0], stencil[..., 1], stencil[..., 2] = 0.95, 0.55, 0.05
    stencil[..., 3] = stripes
    return {f"{PREFIX}/decal_filter_image.tga": tga(np.repeat(scorch[..., None], 3, axis=-1)),
            f"{PREFIX}/decal_blend_image.tga": tga(stencil)}


def decal_decl(blend: str) -> str:
    return "\n".join([f"{PREFIX}/decal_{blend}", "{", "polygonOffset", "noShadows", "nonsolid", "noimpact",
                      "sort decal", "{", f"blend {blend}", f"map {PREFIX}/decal_{blend}_image", "}", "}", ""])


def probe_decl(name: str, box: bool) -> str:
    return "\n".join([f"{LIGHT_PREFIX}/probe_{name}", "{", "openQ4SpecularProbe {", f"cubeMap {PROBE_PREFIX}/{name}",
                      "intensity 1", "blendFraction 0.1", "priority 8", *(["boxParallax"] if box else []), "}",
                      "{ map _white }", "}", ""])

# Map name -> file-name patterns, tried in this order on lower-cased stems.
MAP_PATTERNS = (
    ("ao", re.compile(r"ambient[ _-]?occlusion|(^|[ _.-])ao($|[ _.-])")),
    ("albedo", re.compile(r"albedo|base[ _-]?colou?r|diffuse")),
    ("normal", re.compile(r"normal")),
    ("roughness", re.compile(r"rough")),
    ("metallic", re.compile(r"metal(lic|ness)")),
    ("alpha", re.compile(r"alpha|opacity")),
    ("height", re.compile(r"height|displace")),
)
IMAGE_SUFFIXES = {".tif", ".tiff", ".png", ".tga", ".jpg", ".jpeg"}


def load_manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def classify(name: str) -> str | None:
    stem = Path(name).stem.lower()
    for kind, pattern in MAP_PATTERNS:
        if pattern.search(stem):
            return kind
    return None


def source_files(folder: Path) -> dict[str, list[tuple[str, bytes]]]:
    """Every recognised image in a material folder or the zips inside it."""
    found: dict[str, list[tuple[str, bytes]]] = {}
    if not folder.is_dir():
        return found
    for path in sorted(folder.rglob("*")):
        if path.suffix.lower() in IMAGE_SUFFIXES:
            members = [(path.name, path.read_bytes())]
        elif path.suffix.lower() == ".zip":
            with zipfile.ZipFile(path) as archive:
                members = [(Path(m).name, archive.read(m)) for m in archive.namelist()
                           if Path(m).suffix.lower() in IMAGE_SUFFIXES]
        else:
            continue
        for name, data in members:
            kind = classify(name)
            if kind:
                found.setdefault(kind, []).append((name, data))
    return found


def to_float(image: Image.Image) -> np.ndarray:
    """H x W x C float array in [0, 1], whatever the bit depth."""
    if image.mode in ("I;16", "I;16B", "I;16L", "I;16N"):
        return (np.asarray(image, dtype=np.float64) / 65535.0)[..., None]
    if image.mode == "I":
        data = np.asarray(image, dtype=np.float64)
        return (data / (65535.0 if data.max() > 255 else 255.0))[..., None]
    if image.mode == "F":
        return np.clip(np.asarray(image, dtype=np.float64), 0, 1)[..., None]
    if image.mode not in ("L", "RGB", "RGBA", "LA"):
        image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
    data = np.asarray(image, dtype=np.float64) / 255.0
    return data[..., None] if data.ndim == 2 else data


def best(entries: list[tuple[str, bytes]]) -> tuple[str, np.ndarray]:
    """The largest image of one map kind (downloads may hold several sizes)."""
    decoded = []
    for name, data in entries:
        try:
            image = Image.open(io.BytesIO(data))
            image.load()
        except Exception as error:  # noqa: BLE001 - report every unreadable file
            raise RuntimeError(f"cannot read {name}: {error} (16-bit RGB TIFFs need Pillow 10+)") from error
        decoded.append((image.width * image.height, name, to_float(image)))
    _, name, data = max(decoded, key=lambda item: item[0])
    return name, data


def resize(data: np.ndarray, size: int) -> np.ndarray:
    if data.shape[0] == size and data.shape[1] == size:
        return data
    channels = [np.asarray(Image.fromarray(data[..., c].astype(np.float32), "F").resize((size, size), Image.LANCZOS),
                           dtype=np.float64) for c in range(data.shape[2])]
    return np.clip(np.stack(channels, axis=-1), 0, 1)


def gray(data: np.ndarray) -> np.ndarray:
    return data[..., 0] if data.shape[2] < 3 else data[..., :3] @ np.array([0.2126, 0.7152, 0.0722])


def normal_orientation(normal: np.ndarray, height: np.ndarray | None) -> dict:
    """How the normal map's green channel relates to the height map.

    openQ4's tangent frame takes +Y along increasing t, which runs down the image,
    so a surface rising towards the top of the image must store a positive Y
    (n.y follows -dh/drow). Comparing the decoded normal with the height map's
    own slopes settles each download's convention instead of assuming one.
    """
    if height is None:
        return {"evidence": "none"}
    h = gray(height)
    dh_drow, dh_dcol = np.gradient(h)
    n = normal[..., :3] * 2.0 - 1.0
    def corr(a: np.ndarray, b: np.ndarray) -> float:
        a = a - a.mean(); b = b - b.mean()
        d = math.sqrt(float((a * a).sum() * (b * b).sum()))
        return float((a * b).sum() / d) if d > 0 else 0.0
    x, y = corr(n[..., 0], -dh_dcol), corr(n[..., 1], -dh_drow)
    return {"evidence": "height", "xCorrelation": round(x, 4), "yCorrelation": round(y, 4),
            "flipY": y < 0, "confident": abs(y) >= 0.05 and x > 0.05}


def tga(data: np.ndarray) -> bytes:
    """Uncompressed top-left TGA from an H x W x 3|4 array in [0, 1]."""
    height, width, channels = data.shape
    pixels = np.clip(np.rint(data * 255.0), 0, 255).astype(np.uint8)
    order = [2, 1, 0, 3] if channels == 4 else [2, 1, 0]
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height,
                         8 * channels, 0x28 if channels == 4 else 0x20)
    return header + pixels[..., order].tobytes()


def synthetic_sources(material: dict, size: int) -> dict[str, np.ndarray]:
    """Procedural stand-ins with the map set the real download has."""
    seed = int(hashlib.sha256(material["id"].encode()).hexdigest()[:8], 16)
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size] / size
    cells = 4 + seed % 5
    height = 0.5 + 0.25 * np.sin(2 * np.pi * cells * x) * np.sin(2 * np.pi * cells * y) \
        + 0.08 * rng.standard_normal((size, size))
    height = np.clip(height, 0, 1)
    dh_drow, dh_dcol = np.gradient(height * 6.0)
    normal = np.stack([-dh_dcol, -dh_drow, np.ones_like(height)], axis=-1)
    normal /= np.linalg.norm(normal, axis=-1, keepdims=True)
    if seed % 2:
        normal[..., 1] *= -1  # half the stand-ins use the other green convention
    tint = 0.25 + 0.6 * rng.random(3)
    maps = {"albedo": np.clip(tint * (0.75 + 0.5 * height[..., None]), 0, 1),
            "normal": normal * 0.5 + 0.5,
            "roughness": np.clip(0.2 + 0.6 * height, 0, 1)[..., None],
            "metallic": (height > 0.6).astype(np.float64)[..., None],
            "ao": np.clip(0.4 + 0.8 * height, 0, 1)[..., None],
            "alpha": ((np.sin(2 * np.pi * 8 * x) * np.sin(2 * np.pi * 8 * y)) > -0.2).astype(np.float64)[..., None],
            "height": height[..., None]}
    return {kind: maps[kind] for kind in material["maps"]}


def convert(material: dict, sources: dict[str, np.ndarray], size: int, default_flip: bool | None) -> tuple[dict[str, bytes], dict]:
    """Engine textures for one material, and the record of what was decided."""
    report: dict = {"maps": sorted(sources)}
    missing = [kind for kind in ("albedo", "normal", "roughness") if kind not in sources]
    if missing:
        raise RuntimeError(f"{material['id']}: missing {', '.join(missing)}")
    albedo = resize(sources["albedo"], size)
    albedo = albedo[..., :3] if albedo.shape[2] >= 3 else np.repeat(albedo[..., :1], 3, axis=-1)
    normal = resize(sources["normal"], size)[..., :3]
    height = resize(sources["height"], size) if "height" in sources else None
    orientation = normal_orientation(normal, height)
    flip = material.get("normalFlipY")
    if flip is None:
        flip = orientation["flipY"] if orientation.get("confident") else default_flip
    report["normal"] = {**orientation, "applied": "flipY" if flip else "asStored",
                        "decidedBy": "manifest" if "normalFlipY" in material else
                        "height" if orientation.get("confident") else "majority" if default_flip is not None else "default"}
    vector = normal * 2.0 - 1.0
    if flip:
        vector[..., 1] *= -1
    vector /= np.maximum(np.linalg.norm(vector, axis=-1, keepdims=True), 1e-6)
    roughness = gray(resize(sources["roughness"], size))
    metallic = gray(resize(sources["metallic"], size)) if "metallic" in sources else np.zeros((size, size))
    ao = gray(resize(sources["ao"], size)) if "ao" in sources else np.ones((size, size))
    orm = np.stack([ao, roughness, metallic], axis=-1)
    # The classic fallback needs a specular map: brightest on smooth metal,
    # tinted by the albedo there, dim and neutral on rough dielectrics.
    gloss = (1.0 - roughness)[..., None] ** 2
    spec = gloss * (0.25 * (1.0 - metallic[..., None]) + albedo * metallic[..., None])
    files = {"albedo": albedo, "normal": vector * 0.5 + 0.5, "orm": orm, "spec": spec}
    if "alpha" in sources:
        files["albedo"] = np.concatenate([albedo, gray(resize(sources["alpha"], size))[..., None]], axis=-1)
        report["cutout"] = True
    return {f"{PREFIX}/{material['id']}_{name}.tga": tga(data) for name, data in files.items()}, report


def material_decl(material: dict, cutout: bool) -> str:
    base = f"{PREFIX}/{material['id']}"
    # No twoSided on the cutout: the engine keeps the stock rule that twoSided
    # implies noShadows, and the fence is a thin brush whose two faces already
    # show it from both sides, so it can cast its cutout shadow.
    lines = [base, "{"]
    lines += [f"bumpmap {base}_normal"]
    if cutout:
        lines += ["{", "blend diffusemap", f"map {base}_albedo", "alphaTest 0.5", "}"]
    else:
        lines += [f"diffusemap {base}_albedo"]
    lines += [f"specularmap {base}_spec",
              "pbr {", "workflow metallicRoughness", f"albedoMap {base}_albedo",
              f"normalMap {base}_normal", "normalFormat tangentXYZ", "normalScale 1",
              f"ormMap {base}_orm", "metallic 1", "roughness 1", "ao 1", "}", "}", ""]
    return "\n".join(lines)


def classic_decl(material: dict) -> str:
    """The same textures authored the classic way: no pbr block, so never PBR."""
    base = f"{PREFIX}/{material['id']}"
    return "\n".join([base + CLASSIC_SUFFIX, "{", f"bumpmap {base}_normal", f"diffusemap {base}_albedo",
                      f"specularmap {base}_spec", "}", ""])


def alpha_coverage(blob: bytes) -> float:
    """Share of a converted RGBA albedo that passes the cutout's alphaTest 0.5."""
    pixels = np.frombuffer(blob, dtype=np.uint8, offset=18).reshape(-1, 4)
    return float((pixels[:, 3] >= 128).mean())


def brush(lo: tuple[float, float, float], hi: tuple[float, float, float], material: str, tile_units: float) -> str:
    planes = [(1, 0, 0, -hi[0]), (-1, 0, 0, lo[0]), (0, 1, 0, -hi[1]), (0, -1, 0, lo[1]), (0, 0, 1, -hi[2]), (0, 0, -1, lo[2])]
    scale = f"{1.0 / tile_units:.9g}"
    return "{\nbrushDef3\n{\n" + "\n".join(
        f'( {a} {b} {c} {d:g} ) ( ( {scale} 0 0 ) ( 0 {scale} 0 ) ) "{material}"' for a, b, c, d in planes) + "\n}\n}\n"


def tile_units(material: dict) -> float:
    return max(1.0, float(material.get("sizeMeters", 1.0)) * UNITS_PER_METER)


def payloads(materials: list[dict], textures: dict[str, bytes], authoring: dict | None = None) -> tuple[dict[str, bytes], list[dict], dict, dict]:
    data: dict[str, bytes] = {"baseoq4/" + path: blob for path, blob in textures.items()}
    def add(qpath: str, value: str | bytes) -> None:
        data["baseoq4/" + qpath] = value.encode("utf-8") if isinstance(value, str) else value
    by_role: dict[str, list[dict]] = {}
    for material in materials:
        by_role.setdefault(material["placement"], []).append(material)
    def role(name: str) -> dict:
        found = by_role.get(name)
        if not found:
            raise RuntimeError(f"manifest has no '{name}' material")
        return found[0]
    floor, strip, back, sides = role("floor"), role("floorStrip"), role("backWall"), role("sideWalls")
    fence = role("fence")
    stations = by_role.get("station", [])

    mats = [material_decl(m, m["id"] == fence["id"] or "alpha" in m["maps"]) for m in materials]
    extras: dict = {}
    precache = ""
    if authoring:
        by_id = {m["id"]: m for m in materials}
        base, substitute = by_id[authoring["base"]], by_id[authoring["substitute"]]
        base_shader, classic_shader = f"{PREFIX}/{base['id']}", f"{PREFIX}/{base['id']}{CLASSIC_SUFFIX}"
        mats.append(classic_decl(base))
        skins = {"pbr": f"{SKIN_PREFIX}/{base['id']}_to_{substitute['id']}",
                 "classic": f"{SKIN_PREFIX}/{base['id']}_to_classic"}
        add("skins/openq4_pbr_showcase.skin",
            f"skin {skins['pbr']}\n{{\n{base_shader} {PREFIX}/{substitute['id']}\n}}\n"
            f"skin {skins['classic']}\n{{\n{base_shader} {classic_shader}\n}}\n")
        # One skinned mesh, PBR above its equator and classic below.
        mesh, anim, _ = lab.skinned_sphere((base_shader, classic_shader))
        add(MIXED_MESH, mesh.replace("PBR laboratory", "PBR showcase"))
        add(MIXED_ANIM, anim.replace("PBR laboratory", "PBR showcase"))
        add("def/openq4_pbr_showcase.def", f"model {MIXED_MODEL} {{\nmesh {MIXED_MESH}\nanim idle {MIXED_ANIM}\n}}\n")
        precache = (f'"mtr_authoring_classic" "{classic_shader}"\n"model_authoring_mixed" "{MIXED_MODEL}"\n'
                    '"def_authoring_mixed" "func_animate"\n'
                    + "".join(f'"skin_authoring_{name}" "{skin}"\n' for name, skin in skins.items()))
        extras["authoring"] = {"origin": list(AUTHORING_ORIGIN), "camera": list(AUTHORING_CAMERA),
                               "base": base["id"], "substitute": substitute["id"], "classic": classic_shader,
                               "model": f"models/openq4/pbr_showcase/{base['id']}.ase", "radius": SPHERE_RADIUS,
                               "skins": skins, "mixedModel": MIXED_MODEL, "mixedRadius": MIXED_RADIUS}
    mats.append(f"{LIGHT_PREFIX}/projected\n{{\nlightFalloffImage _white\n{{\nmap _white\ncolored\n}}\n}}\n")
    mats.append(f"{LIGHT_PREFIX}/ambient\n{{\nambientLight\nnoShadows\nlightFalloffImage _white\n{{\nmap _white\ncolored\n}}\n}}\n")
    add("materials/openq4_pbr_showcase.mtr", "\n".join(mats))

    # Interior 1280 x 1440 x 512 units. Textures tile at their real size (one
    # unit is an inch), so a wall shows as many bricks as a real one would.
    x0, x1, y0, y1, z1, t = -640, 640, -940, 500, 512, 16
    level = 'Version 3\n{\n"classname" "worldspawn"\n' + precache
    level += brush((x0 - t, y0 - t, -t), (x1 + t, -720, 0), f"{PREFIX}/{strip['id']}", tile_units(strip))
    level += brush((x0 - t, -720, -t), (x1 + t, y1 + t, 0), f"{PREFIX}/{floor['id']}", tile_units(floor))
    level += brush((x0 - t, y0 - t, z1), (x1 + t, y1 + t, z1 + t), f"{PREFIX}/{sides['id']}", tile_units(sides))
    level += brush((x0 - t, y0 - t, 0), (x0, y1 + t, z1), f"{PREFIX}/{sides['id']}", tile_units(sides))
    level += brush((x1, y0 - t, 0), (x1 + t, y1 + t, z1), f"{PREFIX}/{sides['id']}", tile_units(sides))
    level += brush((x0, y0 - t, 0), (x1, y0, z1), f"{PREFIX}/{sides['id']}", tile_units(sides))
    level += brush((x0, y1, 0), (x1, y1 + t, z1), f"{PREFIX}/{back['id']}", tile_units(back))
    level += "}\n"

    records: list[dict] = []
    columns = 6
    for index, material in enumerate(stations):
        row, column = divmod(index, columns)
        count = min(columns, len(stations) - row * columns)
        x = (column - (count - 1) / 2) * 200
        panel_z = 330 - row * 170
        sphere = (x, 240 - row * 200, SPHERE_RADIUS + 16)
        shader = f"{PREFIX}/{material['id']}"
        # A flat panel on the back wall shows normal detail under grazing
        # light; the sphere shows every angle of incidence at once.
        level += ('{\n"classname" "func_static"\n' f'"name" "{material["id"]}_panel"\n' f'"model" "{material["id"]}_panel"\n'
                  + brush((x - 80, y1 - 8, panel_z - 70), (x + 80, y1, panel_z + 70), shader, tile_units(material)) + "}\n")
        model = f"models/openq4/pbr_showcase/{material['id']}.ase"
        circumference = 2 * math.pi * SPHERE_RADIUS
        # The sphere names its own material, so loading it resolves nothing missing.
        add(model, lab.sphere_ase(radius=SPHERE_RADIUS, uv_scale=circumference / tile_units(material)).replace(
            f"{lab.PREFIX}/neutral", shader))
        level += lab.entity(classname="func_static", name=material["id"], model=model, shader=shader,
                            origin=" ".join(f"{v:g}" for v in sphere), solid="0")
        records.append({"id": material["id"], "title": material["title"], "sphere": list(sphere),
                        "panel": [x, y1 - 8, panel_z]})

    # The fence stands between the projector and the floor so its cutout
    # shadow lands on wood in front of the stations.
    fx0, fx1, fy = -420, -100, -260
    level += ('{\n"classname" "func_static"\n"name" "fence"\n"model" "fence"\n'
              + brush((fx0, fy - 2, 0), (fx1, fy + 2, 288), f"{PREFIX}/{fence['id']}", tile_units(fence)) + "}\n")
    level += lab.entity(classname="info_player_start", name="showcase_start", origin="0 -820 0", angle="90")
    level += lab.entity(classname="light", name="key", origin="160 -420 440", light_radius="1600 1600 1600",
                        _color="1.5 1.42 1.32", noshadows="0")
    # Lights the stations from above and in front, so the panels show their
    # relief and the spheres throw contact shadows on the floor.
    level += lab.entity(classname="light", name="station_key", origin="-120 80 470", light_radius="1000 1000 1000",
                        _color="1.1 1.05 1", noshadows="0")
    level += lab.entity(classname="light", name="cool_fill", origin="-560 -600 300", light_radius="1300 1300 1300",
                        _color="0.18 0.24 0.34", noshadows="1")
    level += lab.entity(classname="light", name="warm_fill", origin="560 -600 300", light_radius="1300 1300 1300",
                        _color="0.32 0.22 0.14", noshadows="1")
    level += lab.entity(classname="light", name="ambient", texture=f"{LIGHT_PREFIX}/ambient", origin="0 -220 256",
                        light_radius="900 900 400", _color="0.12 0.12 0.13", noshadows="1")
    level += lab.entity(classname="light", name="fence_projector", texture=f"{LIGHT_PREFIX}/projected",
                        origin="-260 -720 420", light_target="0 760 -420", light_right="420 0 0",
                        light_up="0 210 380", _color="0.9 0.85 0.75")
    # The room probe: its light volume is the room itself (boxParallax).
    level += lab.entity(classname="light", name="probe_room", texture=f"{LIGHT_PREFIX}/probe_room",
                        origin=f"{(x0 + x1) / 2:g} {(y0 + y1) / 2:g} {z1 / 2:g}",
                        light_radius=f"{(x1 - x0) / 2:g} {(y1 - y0) / 2:g} {z1 / 2:g}", _color="1 1 1", noshadows="1")
    for name, (blend, lo, hi, _) in DECALS.items():
        level += ('{\n"classname" "func_static"\n' f'"name" "decal_{name}"\n"model" "decal_{name}"\n'
                  + brush(lo, hi, f"{PREFIX}/decal_{blend}", DECAL_SIZE) + "}\n")
    for path, blob in decal_textures().items():
        add(path, blob)
    add("materials/openq4_pbr_showcase_extras.mtr", "\n".join(
        [decal_decl("filter"), decal_decl("blend"), probe_decl("room", True), probe_decl("stock", False)]))
    extras["probes"] = {"room": {"entity": "probe_room", "cubeMap": f"{PROBE_PREFIX}/room"},
                        "stock": {"material": f"{LIGHT_PREFIX}/probe_stock", "cubeMap": f"{PROBE_PREFIX}/stock"},
                        "faces": list(PROBE_FACES), "lights": ["key", "station_key"]}
    extras["decals"] = {name: {"entity": f"decal_{name}", "blend": blend, "camera": list(camera)}
                        for name, (blend, _, _, camera) in DECALS.items()}
    add(MAP + ".map", level)
    cameras = {"overview": [0, -640, 190, 0, 90, 0],
               "fence": [-260, -440, 110, 0, 90, 0],
               "floor-grazing": [0, -900, 24, 0, 90, 0]}
    for record in records:
        x, y, z = record["sphere"]
        cameras["station-" + record["id"]] = [x, y - 300, z + 20, 0, 90, 0]
    albedo = textures.get(f"{PREFIX}/{fence['id']}_albedo.tga")
    if albedo is not None and albedo[16] == 32:
        # Cutout proof: the close camera sees nothing but fence, so the share of
        # the frame the fence covers is its alpha map's coverage; the shadow
        # camera looks at the floor the projector lights through the fence.
        extras["cutout"] = {"material": fence["id"], "entity": "fence", "projector": "fence_projector",
                            "coverage": round(alpha_coverage(albedo), 4),
                            "closeCamera": list(FENCE_CLOSE_CAMERA), "shadowCamera": list(FENCE_SHADOW_CAMERA)}
    return data, records, cameras, extras


def gather(materials: list[dict], source_dir: Path, synthetic: bool, size: int) -> tuple[dict[str, bytes], dict]:
    sources = {}
    for material in materials:
        if synthetic:
            sources[material["id"]] = synthetic_sources(material, size)
            continue
        found = source_files(source_dir / material["id"])
        sources[material["id"]] = {kind: best(entries)[1] for kind, entries in found.items()}
    # Without height evidence, follow what the material's siblings showed:
    # one source uses one convention throughout its catalogue.
    votes = []
    for material in materials:
        maps = sources[material["id"]]
        if "normal" in maps and "height" in maps:
            o = normal_orientation(resize(maps["normal"], 256)[..., :3], resize(maps["height"], 256))
            if o.get("confident"):
                votes.append(o["flipY"])
    majority = None if not votes else sum(votes) * 2 > len(votes)
    textures: dict[str, bytes] = {}
    reports = {"normalVotes": {"flipY": sum(votes), "asStored": len(votes) - sum(votes)}, "materials": {}}
    for material in materials:
        files, report = convert(material, sources[material["id"]], size, majority)
        textures.update(files)
        reports["materials"][material["id"]] = report
    return textures, reports


def check(manifest: dict, source_dir: Path) -> int:
    missing_total = 0
    for material in manifest["materials"]:
        found = source_files(source_dir / material["id"])
        missing = [kind for kind in material["maps"] if kind not in found]
        missing_total += len(missing)
        state = "ready" if not missing else "missing " + ", ".join(missing)
        print(f"{material['id']:16s} {material['code']:9s} {state:40s} {material['url']}")
    print(f"\n{source_dir}: {missing_total} map(s) still to download")
    return 1 if missing_total else 0


def generate(root: Path, source_dir: Path, synthetic: bool, force: bool, size: int | None = None) -> dict:
    root = fixture.validate_runtime_root(root)
    manifest = load_manifest()
    size = size or int(manifest.get("resolution", 1024))
    if size < 64 or size > 4096 or size & (size - 1):
        raise ValueError("--resolution must be a power of two from 64 through 4096")
    textures, reports = gather(manifest["materials"], source_dir, synthetic, size)
    files, records, cameras, extras = payloads(manifest["materials"], textures, manifest.get("authoring"))
    record = {"schema": 2, "id": manifest["id"], "map": MAP, "synthetic": synthetic, **extras,
              "source": "procedural stand-ins" if synthetic else f"{manifest['source']} downloads in {source_dir}",
              "licence": manifest["licence"], "resolution": size, "stations": records, "cameras": cameras,
              "conversion": reports, "sha256": {k: hashlib.sha256(v).hexdigest() for k, v in sorted(files.items())}}
    files["pbr-showcase.json"] = (json.dumps(record, indent=2, sort_keys=True) + "\n").encode()
    paths = {name: root / name for name in files}
    fixture.validate_targets(root, paths, force)
    for name, payload in files.items():
        target = fixture.validate_contained_target(root, paths[name])
        if target.is_file() and target.read_bytes() == payload:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--runtime-root", type=Path)
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--synthetic", action="store_true", help="procedural stand-ins instead of downloads")
    parser.add_argument("--check", action="store_true", help="list what is still to download, then stop")
    parser.add_argument("--resolution", type=int)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if args.check:
        return check(load_manifest(), args.source_dir)
    if args.runtime_root is None:
        parser.error("--runtime-root is required unless --check")
    record = generate(args.runtime_root, args.source_dir, args.synthetic, args.force, args.resolution)
    for material_id, report in record["conversion"]["materials"].items():
        normal = report["normal"]
        print(f"{material_id:16s} normal {normal['applied']:8s} by {normal['decidedBy']:8s} "
              f"(x {normal.get('xCorrelation', '-')}, y {normal.get('yCorrelation', '-')})"
              + ("  cutout" if report.get("cutout") else ""))
    print(f"Generated {record['map']}: {len(record['stations'])} stations, {len(record['sha256'])} files"
          + (" (synthetic stand-ins)" if record["synthetic"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
