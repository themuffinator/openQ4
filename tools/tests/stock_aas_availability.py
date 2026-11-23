#!/usr/bin/env python3
"""Which stock AAS files load, and what an AI without one is told (Q4-02).

Contract (always): the AAS loader tells a finished compile that kept no
navigation areas apart from damaged data, and idAI::SetAAS in both game
modules names the file that did not load instead of implying a load-order
problem. The 0.16.0 release sweep read the old "no such AAS is loaded" on
tram1b, under a log line "loading maps/game/tram1b.aas32", as an AI defect.

Retail audit (--basepath, a Quake 4 install root holding q4base): reads only
the retail PK4s, never loose files, so the Steam install's loose recompiled
maps do not shadow the shipped data. It classifies every shipped
maps/game/<map>.aas<N> as navigation, placeholder (dummy), empty or invalid,
resolves `use_aas` for every entity placed in the shipped .map files through
the entityDef inheritance chain, and checks that

  * exactly tram1b.aas32 and mcc_1.aas48 are finished compiles with no
    navigation areas (retail 1.4.2 refuses them as well: Quake4.exe
    0x101d575e checks the vertex, edge and face counts after IsDummyFile);
  * every placed entity whose AAS will not load is hidden, passive or
    notarget, so no placed AI the campaign relies on loses its pathing.

AI spawned later (vehicle occupants such as tram1b's tramcar driver and
gunners, spawners) are not placed entities; the ledger records them from the
runtime log.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AAS_TYPES = ("aas32", "aas48", "aas96", "aas250", "aas128")
CORE_SECTIONS = ("planes", "vertices", "edges", "edgeIndex", "faces", "faceIndex",
                 "areas", "nodes", "portals", "portalIndex", "clusters")
EXPECTED_EMPTY = {"game/tram1b.aas32", "game/mcc_1.aas48"}
SECTION = re.compile(r"^(\w+) (\d+) \{", re.M)
KEYVALUE = re.compile(r'^"([^"]+)" "([^"]*)"', re.M)


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_bytes().decode("windows-1252")


def function_body(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Missing {signature!r} in {context}")
    opening = source.find("{", start + len(signature))
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"Unterminated body for {signature!r} in {context}")


def check_sources() -> None:
    loader = function_body(read("src/aas/AASFile.cpp"), "bool idAASFileLocal::Load(", "AASFile.cpp")
    empty_branch = loader.find("areas.Num() <= 1")
    if empty_branch < 0 or "has no navigation areas" not in loader:
        raise AssertionError("idAASFileLocal::Load must report a compile with no navigation areas by name")
    if "(invalid data)" not in loader:
        raise AssertionError("idAASFileLocal::Load must still report damaged files as invalid data")
    if loader.find("IsDummyFile( mapFileCRC )") > empty_branch:
        raise AssertionError("placeholder (dummy) files must be recognised before the empty-compile check")

    for module in ("game", "mpgame"):
        context = f"src/{module}/ai/AI_Move.cpp"
        body = function_body(read(context), "void idAI::SetAAS( void )", context)
        if "but no such AAS is loaded; this AI cannot path" in body:
            raise AssertionError(f"{context}: the ambiguous 'no such AAS is loaded' warning is back")
        for token in ('FindEntityDefDict( "aas_types", false )', "GetMapName()", "SetFileExtension( use_aas )",
                      "did not load", "see [Load AAS]", "which is not an aas_types entry"):
            if token not in body:
                raise AssertionError(f"{context}: idAI::SetAAS is missing {token!r}")


class Packs:
    """The retail q4base PK4s, later packs overriding earlier ones."""

    def __init__(self, q4base: Path) -> None:
        self.owner: dict[str, zipfile.ZipFile] = {}
        self.zips = []
        for pack in sorted(q4base.glob("*.pk4"), key=lambda p: p.name.lower()):
            archive = zipfile.ZipFile(pack)
            self.zips.append(archive)
            for name in archive.namelist():
                self.owner[name.lower()] = archive
        if not self.owner:
            raise SystemExit(f"no PK4 files under {q4base}")

    def names(self, prefix: str, suffix: str) -> list[str]:
        return sorted(n for n in self.owner if n.startswith(prefix) and n.endswith(suffix))

    def text(self, name: str) -> str | None:
        archive = self.owner.get(name.lower())
        if archive is None:
            return None
        for info in archive.infolist():
            if info.filename.lower() == name.lower():
                return archive.read(info).decode("latin-1")
        return None


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def entity_defs(packs: Packs) -> dict[str, dict[str, str]]:
    defs: dict[str, dict[str, str]] = {}
    pattern = re.compile(r"\bentityDef\s+(\S+)\s*\{", re.I)
    for name in packs.names("def/", ".def"):
        text = strip_comments(packs.text(name) or "")
        for match in pattern.finditer(text):
            depth, end = 1, match.end()
            while end < len(text) and depth:
                depth += {"{": 1, "}": -1}.get(text[end], 0)
                end += 1
            body = text[match.end():end - 1]
            defs[match.group(1).lower()] = {k.lower(): v for k, v in re.findall(r'"([^"]+)"\s+"([^"]*)"', body)}
    return defs


def resolve(defs: dict[str, dict[str, str]], classname: str, key: str) -> str | None:
    seen = set()
    name = classname.lower()
    while name and name not in seen:
        seen.add(name)
        entry = defs.get(name)
        if entry is None:
            return None
        if key in entry:
            return entry[key]
        name = entry.get("inherit", "").lower()
    return None


def classify(text: str | None) -> str:
    if text is None:
        return "missing"
    counts = {name: int(count) for name, count in SECTION.findall(text) if name in CORE_SECTIONS}
    if all(counts.get(name, 0) == 0 for name in CORE_SECTIONS):
        return "dummy"
    if counts.get("vertices", 0) and counts.get("edges", 0) and counts.get("faces", 0):
        return "navigation"
    if counts.get("areas", 0) <= 1:
        return "empty"
    return "invalid"


def audit(basepath: Path) -> dict:
    q4base = basepath / "q4base"
    packs = Packs(q4base if q4base.is_dir() else basepath)
    defs = entity_defs(packs)
    maps = [n[len("maps/"):-len(".map")] for n in packs.names("maps/game/", ".map")]
    files: dict[str, str] = {}
    refused_users: list[dict] = []
    placed_ai = 0
    for mapname in maps:
        for aas_type in AAS_TYPES:
            files[f"{mapname}.{aas_type}"] = classify(packs.text(f"maps/{mapname}.{aas_type}"))
        source = packs.text(f"maps/{mapname}.map") or ""
        for block in re.split(r"\n// entity \d+\s*\n", source):
            keys = {k.lower(): v for k, v in KEYVALUE.findall(block)}
            classname = keys.get("classname", "")
            use_aas = keys.get("use_aas") or resolve(defs, classname, "use_aas") or ""
            if not use_aas:
                continue
            placed_ai += 1
            if files.get(f"{mapname}.{use_aas}", "missing") == "navigation":
                continue
            flags = {flag: keys.get(flag, resolve(defs, classname, flag) or "0") == "1"
                     for flag in ("hide", "passive", "notarget")}
            refused_users.append({"map": mapname, "entity": keys.get("name", "?"), "classname": classname,
                                  "use_aas": use_aas, "file": files.get(f"{mapname}.{use_aas}", "missing"),
                                  **flags})
    empty = {name for name, kind in files.items() if kind == "empty"}
    invalid = sorted(name for name, kind in files.items() if kind == "invalid")
    active = [user for user in refused_users if not (user["hide"] or user["passive"] or user["notarget"])]
    failures = []
    if len(maps) < 29 or placed_ai < 500:
        failures.append(f"read {len(maps)} maps and {placed_ai} placed AI: not the retail packs, or the parser broke")
    if empty != EXPECTED_EMPTY:
        failures.append(f"empty AAS files {sorted(empty)} differ from the recorded {sorted(EXPECTED_EMPTY)}")
    if invalid:
        failures.append(f"shipped AAS files with damaged sections: {invalid}")
    if active:
        failures.append(f"placed AI that would lose pathing: {active}")
    return {"maps": len(maps), "entityDefs": len(defs), "placedAi": placed_ai,
            "kinds": {kind: sum(1 for v in files.values() if v == kind)
                      for kind in ("navigation", "dummy", "empty", "invalid", "missing")},
            "empty": sorted(empty), "placedAiWithoutAas": refused_users, "failures": failures}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--basepath", type=Path, help="Quake 4 install root (or q4base) holding the retail PK4s")
    parser.add_argument("--json", type=Path, help="write the retail audit report here")
    args = parser.parse_args()

    check_sources()
    print("stock_aas_availability: source contract passed")
    if args.basepath is None:
        print("stock_aas_availability: retail audit skipped (no --basepath)")
        return 0

    report = audit(args.basepath)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"stock_aas_availability: {report['maps']} maps, {report['entityDefs']} entityDefs, "
          f"{report['placedAi']} placed AI, "
          f"AAS files {report['kinds']}")
    print(f"stock_aas_availability: no navigation areas: {', '.join(report['empty'])}")
    for user in report["placedAiWithoutAas"]:
        flags = ",".join(flag for flag in ("hide", "passive", "notarget") if user[flag]) or "active"
        print(f"  {user['map']}: {user['entity']} ({user['classname']}) use_aas {user['use_aas']} "
              f"-> {user['file']} [{flags}]")
    for failure in report["failures"]:
        print(f"FAIL: {failure}")
    return 1 if report["failures"] else 0


if __name__ == "__main__":
    sys.exit(main())
