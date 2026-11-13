#!/usr/bin/env python3
"""Execute the production client prediction controller.

tools/tests/native/AsyncClientPredictionFixture.cpp receives the snapshot timing block of
idAsyncClient::ProcessUnreliableServerMessage - the resync and the clientPrediction /
clientPredictTime adjustment - spliced in verbatim from src/framework/async/AsyncClient.cpp,
together with idlib's INTSIGNBITSET and the client's PREDICTION_FAST_ADJUST.

It checks each step's direction and clamp, then closes the loop: a server and a client
trade commands and snapshots over 0, 30 and 75 ms of one-way delay, and once settled the
client's commands must reach the server before the frame that uses them, with no resyncs.
A last loop stalls the client's frame again and again, as a hitch does, under delay and
jitter, with the 100 ms clamp idAsyncClient::UpdateTime gives a stalled frame: a few seconds
after the last hitch every command must reach the server in time again.

Single-reversal controls re-run it against mutated source. The first restores the
widened unsigned step that, from 19 August 2026, slammed a client behind its target to
full slowdown and left every command late. Another keeps the frame residual apart from the
catch-up, as before 13 November 2026: each hitch over 100 ms left the catch-up that much
higher until its clamp held it, and the client's commands then reached the server late for
good, so the server repeated the last one and its copy of the client's aim stood still.
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

MUTATIONS = (
    (
        "widened unsigned step (the 19 August regression)",
        "( delta / PREDICTION_FAST_ADJUST ) - deltaStep;",
        "( delta / PREDICTION_FAST_ADJUST ) - ( 1 - ( INTSIGNBITSET( delta ) << 1 ) );",
    ),
    (
        "catch-up step reversed",
        "const int deltaStep = ( delta < 0 ) ? -1 : 1;",
        "const int deltaStep = ( delta < 0 ) ? 1 : -1;",
    ),
    (
        "target step reversed",
        "clientPrediction += ( aheadOfServer < configuredPredictionMsec ) ? 1 : -1;",
        "clientPrediction += ( aheadOfServer < configuredPredictionMsec ) ? -1 : 1;",
    ),
    (
        "no resync for a snapshot ahead of the client",
        "if ( predictionResynced ) {",
        "if ( false ) {",
    ),
    (
        "residual kept apart from the catch-up (hitches wedge the client, the 13 November defect)",
        "static_cast<std::int64_t>( clientPredictTime ) + gameTimeResidual -\n"
        "\t\t\t\t( delta / PREDICTION_FAST_ADJUST ) - deltaStep;\n"
        "\t\t\tgameTimeResidual = 0;",
        "static_cast<std::int64_t>( clientPredictTime ) -\n"
        "\t\t\t\t( delta / PREDICTION_FAST_ADJUST ) - deltaStep;",
    ),
    (
        "residual joins the catch-up and is also kept",
        "- deltaStep;\n\t\t\tgameTimeResidual = 0;\n",
        "- deltaStep;\n",
    ),
)


def snapshot_timing(client: str) -> str:
    start = client.index(START)
    end = client.index(END, start) + len(END)
    return client[start:end]


def build_fixture(client: str) -> str:
    math_h = (ROOT / "src/idlib/math/Math.h").read_text(encoding="utf-8")
    sign = re.search(r"^#define INTSIGNBITSET\(i\).*$", math_h, re.M)
    adjust = re.search(r"^const int PREDICTION_FAST_ADJUST\s*=\s*\d+;$", client, re.M)
    if sign is None or adjust is None:
        raise SystemExit("mp_client_prediction_state: INTSIGNBITSET or PREDICTION_FAST_ADJUST not found")
    harness = (ROOT / "tools/tests/native/AsyncClientPredictionFixture.cpp").read_text(encoding="utf-8")
    return (harness.replace("// PRODUCTION_SIGN_MACRO", sign.group(0))
            .replace("// PRODUCTION_FAST_ADJUST", adjust.group(0))
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
    client = (ROOT / "src/framework/async/AsyncClient.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("A C++ compiler is required for the client prediction fixture")

    output = ROOT / ".tmp"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-client-prediction-", dir=output) as directory:
        path = Path(directory)
        result = compile_and_run(build_fixture(client), path, "prediction", compiler)
        print(result.stdout.strip())
        if result.returncode != 0:
            raise SystemExit(f"mp_client_prediction_state: FAILED\n{result.stderr}")

        for index, (label, original, mutated) in enumerate(MUTATIONS):
            if client.count(original) != 1:
                raise SystemExit(f"mp_client_prediction_state: mutation anchor not unique: {label}")
            control = compile_and_run(build_fixture(client.replace(original, mutated, 1)), path,
                                      f"control{index}", compiler)
            if control.returncode == 0:
                raise SystemExit(f"mp_client_prediction_state: control did not fail: {label}\n{control.stdout}")

    print(f"mp_client_prediction_state: ok ({len(MUTATIONS)} single-reversal controls fail)")


if __name__ == "__main__":
    main()
