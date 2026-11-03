#!/usr/bin/env python3
"""Regression checks for blocking-load pacifier pacing.

Commit 7858254e ("Add high-framerate presentation support") replaced a fixed
"~60 Hz UI refresh while loading" cap with one derived from com_maxfps. Because
the redraw's own cost was not charged against that budget, load wall clock scaled
as W / (1 - R/P) and stopped converging entirely once a single loading-screen
redraw cost more than the interval it was throttled by. Users reported map loads
hanging at com_maxfps 240 that completed normally at 60.

These checks pin the three properties that keep the loading screen's cost bounded
independently of the presentation cap.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    data = (ROOT / relative_path).read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def refuse(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        method = signature.split("(", 1)[0].split()[-1]
        start = source.find(f"idFileSystemLocal::{method}(")
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def require_order(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1:
        raise AssertionError(f"Missing ordered symbols {first!r} and/or {second!r} in {context}")
    if first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


def validate_interval_floor() -> None:
    """The loading-screen redraw rate must have a ceiling that a high com_maxfps
    cannot raise. A progress animation gains nothing above it, and every redraw is
    wall clock taken from the load."""
    source = read("src/framework/Session.cpp")
    body = function_body(source, "static float Session_GetBlockingLoadFrameIntervalMsec( void ) {")

    require(source, "SESSION_MAX_BLOCKING_LOAD_REDRAW_HZ", "Session.cpp blocking-load redraw ceiling")
    require(body, "SESSION_MAX_BLOCKING_LOAD_REDRAW_HZ", "blocking-load interval ceiling use")

    # Both the capped and uncapped returns must be floored by the ceiling, otherwise
    # a high cap (or a high user-cmd rate) drives the loading screen again.
    for fragment in ("Max( ceilingMsec, 1000.0f / static_cast<float>( presentationCap ) )",
                     "Max( ceilingMsec, common->GetUserCmdMsecFloat() )"):
        require(body, fragment, "blocking-load interval floor")

    # The unfloored form is what regressed; make sure it cannot come back.
    refuse(body, "return 1000.0f / static_cast<float>( presentationCap );",
           "blocking-load interval (unfloored com_maxfps divisor)")


def validate_draw_cost_is_charged() -> None:
    """PacifierUpdate must stamp lastPacifierTime AFTER the redraw and back the
    interval off by the measured redraw cost. Together these bound loading-screen
    presentation to a fixed fraction of load wall clock for any present cost."""
    source = read("src/framework/Session.cpp")
    body = function_body(source, "void idSessionLocal::PacifierUpdate() {")

    require(body, "SESSION_PACIFIER_DRAW_BUDGET_RATIO", "pacifier draw-cost backoff")
    require(body, "lastPacifierDrawMsec", "pacifier measured draw cost")

    # The gate must test the backed-off interval, not the raw one.
    require(body, "static_cast<float>( elapsedMs ) < pacifierIntervalMs", "pacifier gate uses backed-off interval")

    # The stamp must come after UpdateScreen(), or the redraw pays for its own interval.
    require_order(body, "UpdateScreen();", "lastPacifierTime = Sys_Milliseconds();",
                  "PacifierUpdate stamp ordering")

    # The pre-fix stamp read the presentation clock before the draw.
    refuse(body, "lastPacifierTime = presentationTime;", "PacifierUpdate (pre-draw stamp)")

    # Gate and stamp must read the same clock.
    require(body, "const int time = Sys_Milliseconds();", "pacifier gate clock")

    header = read("src/framework/Session_local.h")
    require(header, "int					lastPacifierDrawMsec;", "Session_local.h pacifier draw-cost member")


def validate_read_count_granularity() -> None:
    """AddToReadCount must not offer a redraw on every 64 KiB chunk; both callers
    read in 64 KiB chunks, which put thousands of offers per load inside asset
    decode."""
    source = read("src/framework/FileSystem.cpp")
    body = function_body(source, "virtual void			AddToReadCount( int c ) {")

    require(body, "READCOUNT_PACIFIER_INTERVAL_BYTES", "AddToReadCount pacifier granularity")
    require(body, "readCountPacifierBytes", "AddToReadCount byte accumulator")
    require(body, "std::lock_guard<std::mutex> lock( readCountMutex );",
            "AddToReadCount worker-safe accounting")
    require(body, "std::this_thread::get_id() == fileSystemMainThread",
            "AddToReadCount main-thread presentation")
    require_order(body, "readCount =", "session->PacifierUpdate();",
                  "AddToReadCount progress accounting before pacifier offer")
    # Startup is different: the initialization screen paces itself by time, and
    # its small script/decl/GUI reads are the only moments some steps offer.
    require_order(body, "session->PacifierUpdate();", "openQ4_UpdateInitialization();",
                  "AddToReadCount offers every main-thread startup read to the initialization screen")

    # Progress accuracy must not regress: readCount still advances every chunk.
    require(body, "readCount =", "AddToReadCount progress accounting")

    # The accumulator has to be cleared with the read count or the first load after a
    # reset offers at the wrong point.
    reset = function_body(source, "virtual void			ResetReadCount( void ) {")
    require(reset, "readCountPacifierBytes = 0;", "ResetReadCount accumulator clear")


def validate_presentation_cap_agreement() -> None:
    """The cap the pacifier budgets against must be the cap the throttle paces to,
    or admitted frames sleep away the difference."""
    session = read("src/framework/Session.cpp")
    cap = function_body(session, "static int Session_FindPresentationCap( void ) {")
    require(cap, "openQ4_GetRequestedPresentationCap()", "Session presentation cap source")
    refuse(cap, 'cvarSystem->GetCVarInteger( "com_maxfps" )', "Session presentation cap (raw com_maxfps)")

    common = read("src/framework/Common.cpp")
    require(common, "int openQ4_GetRequestedPresentationCap( void ) {", "exported presentation cap accessor")

    header = read("src/framework/Common.h")
    require(header, "int					openQ4_GetRequestedPresentationCap( void );", "Common.h cap accessor declaration")


def validate_throttle_spin_is_bounded() -> None:
    """The presentation throttle's sub-millisecond tail is a busy-spin; it must not
    be able to wedge the process if the clock stalls or runs backwards."""
    common = read("src/framework/Common.cpp")
    body = function_body(common, "static void Common_ThrottlePresentationFrame( void ) {")

    require(body, "waitDeadlineClock", "throttle spin deadline")
    require(body, "spinNowClock >= waitDeadlineClock", "throttle spin bail-out")
    require_order(body, "waitDeadlineClock", "while ( true ) {", "throttle deadline computed before spin")


def validate_underwater_reset() -> None:
    """SetUnderwaterView is the only writer of tr.underwaterAmount; a map change
    started while submerged must not leave the effect live across the load."""
    source = read("src/renderer/RenderSystem_init.cpp")
    body = function_body(source, "void idRenderSystemLocal::BeginLevelLoad( void ) {")

    require(body, "underwaterAmount = 0.0f;", "BeginLevelLoad underwater reset")
    require(body, "underwaterTint.Zero();", "BeginLevelLoad underwater tint reset")


def validate_cleared_alloc_safety() -> None:
    """Mem_ClearedAlloc runs outside the idSIMD lifetime, where SIMDProcessor is
    NULL, and the SIMD signature's int cast truncates allocations over 2 GB."""
    source = read("src/idlib/Heap.cpp")

    for signature in ("void *Mem_ClearedAlloc( const size_t size, byte tag ) {",
                      "void *Mem_ClearedAlloc( const size_t size, const char *fileName, "
                      "const int lineNumber, byte tag ) {"):
        body = function_body(source, signature)
        require(body, "memset( mem, 0, size );", "Mem_ClearedAlloc zeroing")
        # Match the call form, not the word: the explanatory comments name the old call.
        refuse(body, "SIMDProcessor->Memset( mem", "Mem_ClearedAlloc (SIMDProcessor deref)")


def main() -> None:
    checks = (
        validate_interval_floor,
        validate_draw_cost_is_charged,
        validate_read_count_granularity,
        validate_presentation_cap_agreement,
        validate_throttle_spin_is_bounded,
        validate_underwater_reset,
        validate_cleared_alloc_safety,
    )

    for check in checks:
        check()

    print(f"loading_pacifier_pacing: {len(checks)} checks passed")


if __name__ == "__main__":
    main()
