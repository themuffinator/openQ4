#!/usr/bin/env python3
"""Execute the live match event stream and replay it through the sample overlay.

tools/tests/native/LiveEventsFixture.cpp compiles the production header
src/mpgame/mp/LiveEvents.h alone: line building over hostile names, the state
diff, kills and overflow. It then writes a sample match as a stream. This test
parses every line of that stream as JSON against the schema in
docs/dev/mp-live-event-stream.md, replays it through tools/overlay/match_overlay.py
(the reducer, the file follower across a split write and a truncation, the
command line and the loopback page) and checks the match the overlay shows.
Single-reversal controls re-run the fixture against mutated copies of the
header and require each to fail. Source checks tie the header to the game:
where a stream is opened, fed and closed, and that it captures nothing a
scoreboard does not already show.
"""
from __future__ import annotations

import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = "src/mpgame/mp/LiveEvents.h"
FIXTURE = ROOT / "tools/tests/native/LiveEventsFixture.cpp"
OVERLAY = ROOT / "tools/overlay/match_overlay.py"

# type -> keys every line of that type carries (after v, seq, t, type)
SCHEMA = {
    "stream": ("game", "source", "map", "gameType", "phase", "teamGame", "players"),
    "phase": ("from", "to"),
    "player": ("event", "slot", "name", "team", "spectator", "score"),
    "score": ("team", "score", "was"),
    "playerScore": ("slot", "name", "score", "was"),
    "flag": ("flag", "state", "carrier", "carrierName", "wasCarrier", "wasCarrierName"),
    "powerup": ("event", "slot", "name", "powerup"),
    "kill": ("killer", "killerName", "victim", "victimName", "weapon", "cause", "suicide", "teamKill", "quad"),
    "standings": ("phase", "teamGame", "players"),
}

MUTATIONS = (
    ("a quote in a name ends the JSON string",
     "if ( *s == '\"' || *s == '\\\\' ) {", "if ( *s == '\\\\' ) {"),
    ("an overlong UTF-8 form is passed through",
     "if ( s[0] >= 0xc2 && s[0] <= 0xdf ) {", "if ( s[0] >= 0xc0 && s[0] <= 0xdf ) {"),
    ("a control byte reaches the line",
     "} else if ( *s < 0x20 || *s == 0x7f ) {", "} else if ( false ) {"),
    ("a table lookup past its end is read",
     "if ( index >= 0 && index < count ) {", "if ( index >= 0 ) {"),
    ("two events share a sequence number",
     "\t\tsequence++;\n\t\tcount++;", "\t\tcount++;"),
    ("a joining player reports the powerups they arrive with",
     "\t\tif ( !was.present || !now.present ) {\n\t\t\tcontinue;\n\t\t}\n\t\tconst std::uint32_t gained",
     "\t\tif ( !now.present ) {\n\t\t\tcontinue;\n\t\t}\n\t\tconst std::uint32_t gained"),
    ("free-for-all frags are reported as team scores",
     "\tif ( current.teamGame ) {\n\t\tfor ( int team = 0; team < MP_LIVE_EVENT_TEAMS; team++ ) {\n\t\t\tif ( current.teamScore",
     "\tif ( true ) {\n\t\tfor ( int team = 0; team < MP_LIVE_EVENT_TEAMS; team++ ) {\n\t\t\tif ( current.teamScore"),
    ("a kill of an enemy is a team kill",
     "state.players[ killer ].team == state.players[ victim ].team;", "state.players[ killer ].team != state.players[ victim ].team;"),
    ("a suicide names a killer",
     "const bool suicide = killer < 0 || killer == victim;", "const bool suicide = killer < 0;"),
    ("flags are compared before the mode has any",
     "if ( current.flags && previous.flags ) {", "if ( current.flags ) {"),
)

SOURCE_CONTRACTS = (
    ("src/mpgame/MultiplayerGame.cpp", (
        "\t// openQ4: servers and clients both publish the match they are in\n\tliveEvents.Update();",
        "\tif( !gameLocal.isClient ) {\n\t\tliveEvents.Kill( attacker, victim, methodOfDeath, quadKill );",
        "\tif ( !gameLocal.isServer ) {\n\t\tliveEvents.Kill( attacker, victim, methodOfDeath, quadKill );",
        "\tliveEvents.Close();\n\tClear();\n\tstatManager->Shutdown();",
        "\t// the next map starts its own event stream\n\tliveEvents.Close();\n\tClear();",
    )),
    ("src/mpgame/MultiplayerGame.h", (
        "mpLiveEventStream liveEvents;",
    )),
    ("src/mpgame/mp/LiveEventStream.cpp", (
        'idCVar g_matchEventStream( "g_matchEventStream", "0", CVAR_GAME | CVAR_ARCHIVE | CVAR_BOOL | CVAR_NOCHEAT,',
        'static const char *const MP_LIVE_EVENT_FILE = "match-events/live.ndjson";',
        # the live file is started again; the match log (MP-18, g_matchLog) is appended
        'file = fileSystem->OpenFileWrite( Path(), "fs_savepath" );',
        'return kind == MATCH_LOG ? MP_MATCH_LOG_FILE : MP_LIVE_EVENT_FILE;',
        "gameLocal.GetDemoState() != DEMO_PLAYING && !gameLocal.isRepeater &&",
        "MP_LIVE_EVENT_PHASES == STATE_COUNT && INACTIVE == 0",
        "MP_LIVE_FLAG_AT_BASE == FS_AT_BASE",
        "MP_LIVE_EVENT_POWERUPS == 18",
        "current.teamScore[ TEAM_MARINE ] = gameLocal.mpGame.GetScoreForTeam( TEAM_MARINE );",
        "current.teamScore[ TEAM_STROGG ] = gameLocal.mpGame.GetScoreForTeam( TEAM_STROGG );",
        # one clock for every line: a client's kills and scores come off two
        "sequence, Stamp(), line,",
        "MPLiveEventKillLine( line, sequence, Stamp(), state,",
        "\tif ( gameLocal.time > lastTime ) {\n\t\tlastTime = gameLocal.time;\n\t}\n\treturn lastTime;",
        "\tfile->Flush();",
    )),
)


def body(text: str, signature: str) -> str:
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    raise SystemExit(f"mp_live_event_stream_contract: unbalanced body for {signature}")


def compile_fixture(include_root: Path, directory: Path, name: str, compiler: str, strict: bool = True) -> Path:
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    command = [compiler, "-std=c++17", f"-I{include_root}", "-Wall", "-Wextra"] + (["-Werror"] if strict else []) + [
        str(FIXTURE), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True, capture_output=not strict)
    return exe


def check_stream(lines: list[bytes]) -> list[dict]:
    events = []
    for number, raw in enumerate(lines):
        try:
            event = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as error:
            raise SystemExit(f"mp_live_event_stream_contract: line {number} is not UTF-8 JSON: {error}")
        if list(event)[:4] != ["v", "seq", "t", "type"] or event["v"] != 1 or event["seq"] != number:
            raise SystemExit(f"mp_live_event_stream_contract: line {number} has a bad envelope: {raw[:120]!r}")
        if number > 0 and event["t"] < events[-1]["t"]:
            raise SystemExit(f"mp_live_event_stream_contract: time runs backwards at line {number}")
        required = SCHEMA.get(event["type"])
        if required is None:
            raise SystemExit(f"mp_live_event_stream_contract: undocumented event type {event['type']!r}")
        missing = [key for key in required if key not in event]
        if missing:
            raise SystemExit(f"mp_live_event_stream_contract: {event['type']} line {number} lacks {missing}")
        events.append(event)
    if not events or events[0]["type"] != "stream":
        raise SystemExit("mp_live_event_stream_contract: a stream must open with its stream line")
    return events


def expect(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"mp_live_event_stream_contract: {message}")


def check_overlay(sample: Path, events: list[dict], directory: Path) -> None:
    spec = importlib.util.spec_from_file_location("match_overlay", OVERLAY)
    overlay = importlib.util.module_from_spec(spec)
    sys.modules["match_overlay"] = overlay
    spec.loader.exec_module(overlay)

    state = overlay.MatchState()
    for event in events:
        expect(state.apply(event), f"the overlay refused a {event['type']} line")
    shown = state.snapshot()
    expect(shown["map"] == "mp/q4ctf1" and shown["gameType"] == "CTF" and shown["source"] == "server", "match identity")
    expect(shown["phase"] == "review" and shown["teamGame"] is True, "phase")
    expect(shown["teams"] == {"marine": 1, "strogg": 0}, f"team scores {shown['teams']}")
    by_slot = {row["slot"]: row for row in shown["players"]}
    expect(sorted(by_slot) == [0, 1, 2, 4], f"roster {sorted(by_slot)}")
    expect(by_slot[2]["name"] == "Zoë" and by_slot[2]["score"] == 5, "a UTF-8 name and its score")
    expect(by_slot[1]["name"] == 'Voss "the\\Wall"' and by_slot[1]["score"] == -1 and by_slot[1]["bot"] is True, "an escaped name")
    expect(by_slot[0].get("powerups") == [], "a powerup that ended")
    expect(shown["flags"]["strogg"]["state"] == "base" and shown["flags"]["strogg"]["carrier"] is None, "flag state")
    expect([kill["victimName"] for kill in shown["feed"]] == ['Voss "the\\Wall"', 'Voss "the\\Wall"', "Late"], "kill feed")
    expect(shown["feed"][0]["killerName"] == "Kane" and shown["feed"][0]["quad"] is True, "kill detail")
    expect(shown["feed"][1]["suicide"] is True and shown["feed"][2]["cause"] == "world", "suicide and world deaths")
    expect(shown["gaps"] == 0 and shown["rejected"] == 0 and shown["streams"] == 1 and shown["sequence"] == len(events) - 1, "stream bookkeeping")
    expect(shown["players"][0]["slot"] == 2, "players are ranked by score")

    # Unknown types and fields are ignored, other schema versions and junk are refused, a gap is counted.
    tolerant = overlay.MatchState()
    tolerant.apply(events[0])
    expect(tolerant.apply({"v": 1, "seq": 1, "t": 5, "type": "somethingNew", "extra": [1, 2]}), "an unknown type must be tolerated")
    for junk in (None, [], "text", {"v": 2, "seq": 2, "t": 5, "type": "phase"}, {"v": 1, "seq": "2", "t": 5, "type": "phase"},
                 {"v": 1, "seq": 2, "type": "phase"}):
        expect(not tolerant.apply(junk), f"accepted {junk!r}")
    tolerant.apply({"v": 1, "seq": 9, "t": 6, "type": "phase", "from": "warmup", "to": "live"})
    expect(tolerant.snapshot()["gaps"] == 1 and tolerant.snapshot()["phase"] == "live" and tolerant.rejected == 4 + 2, "gap and reject counts")
    # A new stream line starts the match again.
    tolerant.apply(events[0])
    expect(tolerant.snapshot()["phase"] == "warmup" and tolerant.snapshot()["streams"] == 2 and tolerant.snapshot()["gaps"] == 0, "stream restart")

    # The follower sees whole lines only, and starts over when the game truncates the file.
    data = sample.read_bytes()
    growing = directory / "growing.ndjson"
    follower = overlay.Follower(growing)
    expect(follower.read() == [], "a missing file is not an error")
    cut = data.index(b"\n", len(data) // 2) + 20
    growing.write_bytes(data[:cut])
    first = follower.read()
    with growing.open("ab") as handle:
        handle.write(data[cut:])
    second = follower.read()
    expect(first + second == data.splitlines() and len(first) < len(events), "split writes must yield every line once")
    expect(all(overlay.decode(line) is not None for line in first + second), "every followed line decodes")
    growing.write_bytes(data[:data.index(b"\n") + 1])
    expect(follower.read() == data.splitlines()[:1], "a truncated file is read from the top")
    expect(overlay.decode(b"{\"v\":1,") is None and overlay.decode(b"\xff\xfe") is None, "damaged lines decode to nothing")

    # The command line and the loopback page show the same match.
    once = subprocess.run([sys.executable, str(OVERLAY), "--file", str(sample), "--once"], capture_output=True, check=True)
    expect(json.loads(once.stdout.decode("utf-8")) == shown, "--once must print the reducer's state")
    feed = [overlay.feed_line(event) for event in events]
    expect(feed[0] == " 0:01  CTF on mp/q4ctf1 (server)" and " 0:09  Kane > Voss \"the\\Wall\" (rocketlauncher)" in feed, "feed lines")
    server = overlay.serve(state, threading.Lock(), "127.0.0.1", 0)
    try:
        base = f"http://127.0.0.1:{server.server_address[1]}"
        with urllib.request.urlopen(base + "/state.json", timeout=10) as response:
            expect(json.loads(response.read().decode("utf-8")) == shown, "/state.json must serve the state")
        with urllib.request.urlopen(base + "/", timeout=10) as response:
            page = response.read().decode("utf-8")
        expect("state.json" in page and "textContent" in page and "innerHTML" not in page, "the page must build its rows as text")
        try:
            urllib.request.urlopen(base + "/../match_overlay.py", timeout=10)
            expect(False, "the server must serve nothing but its two pages")
        except urllib.error.HTTPError as error:
            expect(error.code == 404, f"unexpected status {error.code}")
    finally:
        server.shutdown()
        server.server_close()
    expect('parser.add_argument("--host", default="127.0.0.1"' in OVERLAY.read_text(encoding="utf-8"), "the overlay must default to loopback")


def main() -> None:
    for relative, tokens in SOURCE_CONTRACTS:
        text = (ROOT / relative).read_text(encoding="utf-8").replace("\r\n", "\n")
        for token in tokens:
            if token not in text:
                raise SystemExit(f"mp_live_event_stream_contract: {relative} is missing {token!r}")
    # The stream carries scoreboard state only: no position, vitals or timers.
    stream = (ROOT / "src/mpgame/mp/LiveEventStream.cpp").read_text(encoding="utf-8")
    capture = body(stream, "void mpLiveEventStream::Capture(")
    for private in ("health", "armor", "GetPhysics", "GetOrigin", "viewAngles", "ammo", "weapon", "itemTim", "matchSession"):
        if private in capture:
            raise SystemExit(f"mp_live_event_stream_contract: the stream captures {private!r}")
    header_text = (ROOT / HEADER).read_text(encoding="utf-8")
    for row in ("struct mpLiveEventPlayer", "struct mpLiveEventState"):
        fields = body(header_text, row).lower()
        for private in ("health", "armor", "armour", "origin", "ammo", "weapon", "angle"):
            if private in fields:
                raise SystemExit(f"mp_live_event_stream_contract: {row} holds {private!r}")
    documented = (ROOT / "docs/dev/mp-live-event-stream.md").read_text(encoding="utf-8")
    for kind, keys in SCHEMA.items():
        for word in (kind,) + keys:
            if f"`{word}`" not in documented:
                raise SystemExit(f"mp_live_event_stream_contract: docs/dev/mp-live-event-stream.md does not describe `{word}`")

    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the live events fixture")
    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="live-events-", dir=output) as directory:
        path = Path(directory)
        sample = path / "sample.ndjson"
        result = subprocess.run([str(compile_fixture(ROOT, path, "live", compiler)), str(sample)], capture_output=True, text=True)
        if result.returncode != 0:
            raise SystemExit(f"mp_live_event_stream_contract: FAILED\n{result.stdout}{result.stderr}")
        data = sample.read_bytes()
        expect(data.endswith(b"\n") and b"\r" not in data, "lines end with a single newline")
        events = check_stream(data.splitlines())
        expect({event["type"] for event in events} == set(SCHEMA), "the sample must exercise every event type")
        check_overlay(sample, events, path)

        for index, (label, original, mutated) in enumerate(MUTATIONS):
            mirror = path / f"overlay{index}"
            target = mirror / HEADER
            target.parent.mkdir(parents=True, exist_ok=True)
            text = (ROOT / HEADER).read_text(encoding="utf-8").replace("\r\n", "\n")
            if text.count(original) != 1:
                raise SystemExit(f"mp_live_event_stream_contract: mutation anchor not unique: {label}")
            target.write_text(text.replace(original, mutated, 1), encoding="utf-8")
            try:
                control = compile_fixture(mirror, path, f"control{index}", compiler, strict=False)
                failed = subprocess.run([str(control)], capture_output=True).returncode != 0
            except subprocess.CalledProcessError:
                failed = False  # a control must compile and then fail at run time
            if not failed:
                raise SystemExit(f"mp_live_event_stream_contract: control did not fail: {label}")

    print(f"mp_live_event_stream_contract: ok - {len(events)} sample events in {len(SCHEMA)} types replayed through the "
          f"overlay; {len(MUTATIONS)} single-reversal controls fail")


if __name__ == "__main__":
    main()
