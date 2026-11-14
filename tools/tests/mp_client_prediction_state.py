#!/usr/bin/env python3
"""Execute the production client prediction controller.

tools/tests/native/AsyncClientPredictionFixture.cpp receives the snapshot timing block of
idAsyncClient::ProcessUnreliableServerMessage - the resync and the clientPrediction /
clientPredictTime adjustment - spliced in verbatim from src/framework/async/AsyncClient.cpp,
together with idlib's INTSIGNBITSET, the client's PREDICTION_FAST_ADJUST, its prediction
cover and user-command count per packet, MAX_USERCMD_PACKET_COMMANDS and the whole of
src/framework/async/PredictionMargin.h.

It checks each step's direction and clamp, then closes the loop: a server and a client
trade commands and snapshots over 0, 30 and 75 ms of one-way delay, and once settled the
client's commands must reach the server before the frame that uses them, with no resyncs.
A loop stalls the client's frame again and again, as a hitch does, under delay and jitter,
with the 100 ms clamp idAsyncClient::UpdateTime gives a stalled frame: a few seconds after
the last hitch every command must reach the server in time again, and the hitches must
raise no margin. The long-frame loops then hold the client's frame back the way a busy
machine does: once the first long frame of a length has set the margin, none may leave a
late command; a client at 30 or 20 fps none at all; a random tail a tenth of the late
commands the median target alone leaves; and a smooth client no margin. A table of late
commands per long frame against its length measures the tail.

Single-reversal controls re-run it against mutated source. The first restores the
widened unsigned step that, from 19 August 2026, slammed a client behind its target to
full slowdown and left every command late. Another keeps the frame residual apart from the
catch-up, as before 13 November 2026: each hitch over 100 ms left the catch-up that much
higher until its clamp held it, and the client's commands then reached the server late for
good, so the server repeated the last one and its copy of the client's aim stood still.
The 13 November long-frame controls leave the margin out of the target, as before (long
frames and slow clients sent commands late, and a shot then took the old aim), let hitches
raise it, drop its hold, let a shorter long frame keep a longer one alive, let the target
jump with the cover's 16/17 ms swings, and send a burst's packets with only the backup
commands, so the link's reordering lost the first commands of a burst outright.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

START = "const int maximumPredictionMsec = AsyncClient_MaxPredictionMsec();"
END = "static_cast<int>( adjustedPredictTime ) );"

CLIENT = "src/framework/async/AsyncClient.cpp"
MARGIN = "src/framework/async/PredictionMargin.h"

# (label, file, original, mutated); every control must make the fixture fail.
MUTATIONS = (
    (
        "widened unsigned step (the 19 August regression)", CLIENT,
        "( delta / PREDICTION_FAST_ADJUST ) - deltaStep;",
        "( delta / PREDICTION_FAST_ADJUST ) - ( 1 - ( INTSIGNBITSET( delta ) << 1 ) );",
    ),
    (
        "catch-up step reversed", CLIENT,
        "const int deltaStep = ( delta < 0 ) ? -1 : 1;",
        "const int deltaStep = ( delta < 0 ) ? 1 : -1;",
    ),
    (
        "target step reversed", CLIENT,
        "clientPrediction += ( aheadOfServer < targetPredictionMsec ) ? 1 : -1;",
        "clientPrediction += ( aheadOfServer < targetPredictionMsec ) ? -1 : 1;",
    ),
    (
        "no resync for a snapshot ahead of the client", CLIENT,
        "if ( predictionResynced ) {",
        "if ( false ) {",
    ),
    (
        "residual kept apart from the catch-up (hitches wedge the client, the 13 November defect)", CLIENT,
        "static_cast<std::int64_t>( clientPredictTime ) + gameTimeResidual -\n"
        "\t\t\t\t( delta / PREDICTION_FAST_ADJUST ) - deltaStep;\n"
        "\t\t\tgameTimeResidual = 0;",
        "static_cast<std::int64_t>( clientPredictTime ) -\n"
        "\t\t\t\t( delta / PREDICTION_FAST_ADJUST ) - deltaStep;",
    ),
    (
        "residual joins the catch-up and is also kept", CLIENT,
        "- deltaStep;\n\t\t\tgameTimeResidual = 0;\n",
        "- deltaStep;\n",
    ),
    (
        "margin left out of the target (long frames and slow clients send late commands)", CLIENT,
        "configuredPredictionMsec + predictionMarginMsec );",
        "configuredPredictionMsec );",
    ),
    (
        "hitches the clamp cut raise the margin", MARGIN,
        "if ( msec > coverMsec && msec < clampMsec ) {",
        "if ( msec > coverMsec && msec <= clampMsec ) {",
    ),
    (
        "the margin is not held", MARGIN,
        "frames[count].heldUntil = clientTime + HOLD_MSEC;",
        "frames[count].heldUntil = clientTime;",
    ),
    (
        "a shorter long frame keeps a longer one alive (one spike at a join holds its margin)", MARGIN,
        "\t\t\twhile ( count > 0 && frames[count - 1].msec <= msec ) {\n"
        "\t\t\t\tcount--;\n"
        "\t\t\t}\n",
        "\t\t\tfor ( int i = 0; i < count; i++ ) {\n"
        "\t\t\t\tframes[i].heldUntil = clientTime + HOLD_MSEC;\n"
        "\t\t\t}\n",
    ),
    (
        "the rise follows the margin, so the cover's 16/17 ms swings push the target forward", MARGIN,
        "\t\tconst int before = appliedPeakMsec > coverMsec ? appliedPeakMsec - coverMsec : 0;\n"
        "\t\tconst int after = Margin( coverMsec );\n"
        "\t\tappliedPeakMsec = PeakMsec();\n",
        "\t\tconst int before = appliedPeakMsec;\n"
        "\t\tconst int after = Margin( coverMsec );\n"
        "\t\tappliedPeakMsec = after;\n",
    ),
    (
        "a burst's packets carry only the backup commands", CLIENT,
        "const int burst = requested + ( sentThisFrame > 0 ? sentThisFrame : 0 );",
        "const int burst = requested;",
    ),
)

# RunFrame is not spliced: the fixture's ClientFrame and its command loop do what these lines
# do, so they must stay in the production client.
RUNFRAME_ANCHORS = (
    "msec = UpdateTime( CLIENT_FRAME_CLAMP_MSEC );",
    "predictionMargin.Frame( clientTime, msec, CLIENT_FRAME_CLAMP_MSEC, AsyncClient_PredictionCoverMsec( gameFrame ) );",
    "usercmdsSentThisFrame = 0;\n\twhile ( gameTimeResidual + clientPredictTime >= AsyncClient_NextGameFrameMsec( gameFrame ) ) {",
    "AsyncClient_UsercmdsPerPacket( idAsyncNetwork::clientUsercmdBackup.GetInteger(),\n\t\tusercmdsSentThisFrame );",
    "const int CLIENT_FRAME_CLAMP_MSEC\t\t= 100;",
)


def snapshot_timing(client: str) -> str:
    start = client.index(START)
    end = client.index(END, start) + len(END)
    return client[start:end]


def function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def build_fixture(sources: dict[str, str]) -> str:
    client, margin = sources[CLIENT], sources[MARGIN]
    math_h = (ROOT / "src/idlib/math/Math.h").read_text(encoding="utf-8")
    network_h = (ROOT / "src/framework/async/AsyncNetwork.h").read_text(encoding="utf-8")
    sign = re.search(r"^#define INTSIGNBITSET\(i\).*$", math_h, re.M)
    adjust = re.search(r"^const int PREDICTION_FAST_ADJUST\s*=\s*\d+;$", client, re.M)
    packet = re.search(r"^const int MAX_USERCMD_PACKET_COMMANDS\s*=\s*\d+;$", network_h, re.M)
    if sign is None or adjust is None or packet is None:
        raise SystemExit("mp_client_prediction_state: INTSIGNBITSET, PREDICTION_FAST_ADJUST or "
                         "MAX_USERCMD_PACKET_COMMANDS not found")
    harness = (ROOT / "tools/tests/native/AsyncClientPredictionFixture.cpp").read_text(encoding="utf-8")
    return (harness.replace("// PRODUCTION_SIGN_MACRO", sign.group(0))
            .replace("// PRODUCTION_FAST_ADJUST", adjust.group(0))
            .replace("// PRODUCTION_PACKET_COMMANDS", packet.group(0))
            .replace("// PRODUCTION_PREDICTION_MARGIN", margin)
            .replace("// PRODUCTION_PREDICTION_COVER",
                     function(client, "static ID_INLINE int AsyncClient_PredictionCoverMsec( int gameFrame ) {"))
            .replace("// PRODUCTION_USERCMDS_PER_PACKET",
                     function(client, "static ID_INLINE int AsyncClient_UsercmdsPerPacket( int backupUsercmds, int sentThisFrame ) {"))
            .replace("// PRODUCTION_SNAPSHOT_TIMING", snapshot_timing(client)))


def compile_and_run(source: str, directory: Path, name: str, compiler: str) -> subprocess.CompletedProcess:
    cpp = directory / f"{name}.cpp"
    exe = directory / (f"{name}.exe" if os.name == "nt" else name)
    cpp.write_text(source, encoding="utf-8")
    command = [compiler, "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-variable", str(cpp), "-o", str(exe)]
    if os.environ.get("MP_MATCH_TEST_SANITIZERS") == "1":
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command, cwd=directory, check=True)
    return subprocess.run([str(exe)], cwd=directory, capture_output=True, text=True)


def main() -> None:
    sources = {name: (ROOT / name).read_text(encoding="utf-8").replace("\r\n", "\n") for name in (CLIENT, MARGIN)}
    missing = [anchor for anchor in RUNFRAME_ANCHORS if anchor not in sources[CLIENT]]
    if missing:
        raise SystemExit(f"mp_client_prediction_state: RunFrame no longer does what the fixture models: {missing}")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the client prediction fixture")

    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-client-prediction-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(sources), path, "prediction", compiler)
        print(result.stdout.strip())
        if result.returncode != 0:
            raise SystemExit(f"mp_client_prediction_state: FAILED\n{result.stderr}")

        for index, (label, name, original, mutated) in enumerate(MUTATIONS):
            if sources[name].count(original) != 1:
                raise SystemExit(f"mp_client_prediction_state: mutation anchor not unique: {label}")
            control = compile_and_run(build_fixture({**sources, name: sources[name].replace(original, mutated, 1)}),
                                      path, f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mp_client_prediction_state: control did not fail: {label}\n{control.stdout}")

    print(f"mp_client_prediction_state: ok ({len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
