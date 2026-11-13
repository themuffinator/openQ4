#!/usr/bin/env python3
"""Execute the out-of-band reply budget against the server's real server info.

src/framework/async/OutOfBandReply.h keeps the connectionless replies a client
needs to list or join a server within one packet on a 1280-byte path (1232
bytes of UDP payload), and idConnectRoster::EncodeWithin trims the connect
roster to what is left. tools/tests/native/OutOfBandReplyFixture.cpp checks
both against a server-info dictionary built from every CVAR_SERVERINFO
declaration in the source, at its defaults, with a long server name, 0 to 32
players, names up to the longest allowed and 40 further mod keys. This driver
generates that dictionary, builds the cases with ConnectRoster.cpp and also
builds them against single-reversal copies of the sources, each of which must
fail. Production wiring (that AsyncServer writes both replies this way and the
client asks for the deferred reply) is guarded by oob_reply_contract.py.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = "src/framework/async/OutOfBandReply.h"
ROSTER = "src/framework/async/ConnectRoster.cpp"
ROSTER_HEADER = "src/framework/async/ConnectRoster.h"
FIXTURE = "tools/tests/native/OutOfBandReplyFixture.cpp"
# Class-qualified names and non-literal defaults (a macro or an array entry)
# count too; a non-literal default stands in as a short placeholder value.
CVAR = re.compile(r'^\s*idCVar\s+[\w:]+\(\s*"([A-Za-z_0-9]+)",\s*(?:"([^"]*)"|[^,"]+),[^\n]*CVAR_SERVERINFO', re.M)

CONTROLS = (
    (HEADER, "the fill ignores the budget", "used + PairBytes( keys[i], values[i] ) <= budget", "true"),
    (HEADER, "browser keys are not kept first", "keep[i] = InList( keys[i], required, requiredCount );", "keep[i] = false;"),
    (HEADER, "names are never cut", "if ( whole <= available ) {", "if ( true ) {"),
    (HEADER, "the budget is IPv4's 1400-byte packet", "BUDGET_BYTES = 1232;", "BUDGET_BYTES = 1400;"),
    (HEADER, "a dictionary's end is not counted", "DICT_END_BYTES = 2;", "DICT_END_BYTES = 0;"),
    (HEADER, "the browser loses the map", '"si_name", "si_map", "si_gameType", "si_maxPlayers", "si_privatePlayers",',
     '"si_name", "si_gameType", "si_maxPlayers", "si_privatePlayers",'),
    (ROSTER, "the roster is never trimmed", "roster.count--;", "return 0;"),
    (ROSTER, "a malformed roster is trimmed instead of refused",
     "if ( Encode( roster, out, capacity ) == 0 ) {\n\t\treturn 0;\n\t}", ""),
)


def server_info() -> list[tuple[str, str]]:
    found: dict[str, str] = {}
    for folder in ("src/framework", "src/mpgame", "src/sys"):
        for path in sorted((ROOT / folder).rglob("*.cpp")):
            for match in CVAR.finditer(path.read_text(encoding="utf-8", errors="replace")):
                found.setdefault(match[1], match[2] if match[2] is not None else "default")
    if len(found) < 40 or any(key not in found for key in ("si_name", "si_map", "si_gameType", "fs_game",
                                                            "net_serverDedicated")):
        raise SystemExit(f"oob_reply_budget_state: found only {len(found)} server-info CVars")
    return sorted(found.items())


def generated(entries: list[tuple[str, str]]) -> str:
    rows = ",\n".join(f'\t{{ "{key}", "{value}" }}' for key, value in entries)
    return f"static const char *const SERVERINFO_DEFAULTS[][2] = {{\n{rows}\n}};"


def build_and_run(scratch: Path, name: str, sources: dict[str, str], compiler: str) -> int:
    for rel, text in sources.items():
        target = scratch / name / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")
    exe = scratch / name / ("cases.exe" if os.name == "nt" else "cases")
    command = [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", f"-I{scratch / name}",
               str(scratch / name / FIXTURE), str(scratch / name / ROSTER), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    built = subprocess.run(command, capture_output=True, text=True)
    if built.returncode != 0:
        raise SystemExit(f"oob_reply_budget_state: {name} does not build\n{built.stderr}")
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    if name == "baseline" and result.returncode != 0:
        print(result.stdout + result.stderr)
    return result.returncode


def main() -> None:
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("a C++ compiler is required for the out-of-band reply cases")
    read = lambda rel: (ROOT / rel).read_text(encoding="utf-8").replace("\r\n", "\n")
    entries = server_info()
    sources = {HEADER: read(HEADER), ROSTER: read(ROSTER), ROSTER_HEADER: read(ROSTER_HEADER),
               FIXTURE: read(FIXTURE).replace("// GENERATED_SERVERINFO", generated(entries))}
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="oob-reply-", dir=output) as directory:
        scratch = Path(directory)
        if build_and_run(scratch, "baseline", sources, compiler) != 0:
            raise SystemExit("oob_reply_budget_state: FAILED (the cases fail against the real sources)")
        for index, (rel, label, original, mutated) in enumerate(CONTROLS):
            if sources[rel].count(original) != 1:
                raise SystemExit(f"oob_reply_budget_state: control anchor not unique: {label}")
            mutated_sources = dict(sources)
            mutated_sources[rel] = sources[rel].replace(original, mutated, 1)
            if build_and_run(scratch, f"control{index}", mutated_sources, compiler) == 0:
                raise SystemExit(f"oob_reply_budget_state: control did not fail: {label}")
    print(f"oob_reply_budget_state: ok ({len(entries)} server-info CVars, {len(CONTROLS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
