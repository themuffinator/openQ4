#!/usr/bin/env python3
"""Pins the BSE behaviours that were checked against the retail Quake 4 1.4.2 binary.

Each of these once drifted to a plausible-looking alternative that changed how stock
effects look or sound, with no warning anywhere:

  * idDeclTable::TableLookup returns 1.0 for a single-value table.  The default
    definition of a missing table is "{ { 0 } }", so returning values[0] instead
    froze every envelope or material that names a missing table at its start
    value (the grunt blood burst stayed half size).
  * The reference sound emitter belongs to rvClientEffect.  BSE never stops or
    frees it, the client effect frees it once in its destructor, and a restored
    client effect keeps its saved handle.  Clearing the handle on restore
    orphaned the restored emitter, so every load stacked another copy of each
    looping effect sound.
  * Sound segments start with a random diversity, rvRandom::flrand(0, 1), for
    one-shot and looping sounds alike.  A fixed 0.0 made every multi-sample
    shader play its first sample, so bullet impacts, ricochets, blaster hits
    and rocket and grenade explosions sounded the same every time.
  * Particle envelopes run at the rate fixed at spawn.  Infinite-duration
    segments push mEndTime forward every frame, so reading the live duration
    slowed their envelopes to a crawl.
  * Vertex colours pack the retail way: additive particles fade through RGB and
    stay opaque without reading the owner's alpha, and channel bytes wrap rather
    than clamp (stock wastesplash.fx authors a fade of 100).
  * Looping ambient effects start with two loops of history, effects are
    submitted farthest first, and spawning looks 0.016 s ahead.
  * Spawn domains use retail's cube-face order and model-to-effect axes.
  * Model particles transform only vertex positions.  Retail rvModelParticle::Render
    (0x101c8d90) and rvPrimBatch::TransformVerts copy the model's normals and
    tangents unrotated; the ETQW-derived source rotated them.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
    return (ROOT.joinpath(*parts)).read_text(encoding="utf-8", errors="replace")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def forbid(haystack: str, needle: str, context: str, why: str) -> None:
    if needle in haystack:
        raise AssertionError(f"{context} contains {needle!r}: {why}")


def body_of(source: str, signature: str, context: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing {signature!r} in {context}")
    end = source.find("\n}", start)
    if end == -1:
        raise AssertionError(f"Unterminated {signature!r} in {context}")
    return source[start:end]


def validate_single_value_tables() -> None:
    table = read("src", "framework", "DeclTable.cpp")
    lookup = body_of(table, "float idDeclTable::TableLookup", "DeclTable.cpp")
    if not re.search(r"if \( domain <= 1 \) \{\s*return 1\.0f;", lookup):
        raise AssertionError(
            "idDeclTable::TableLookup must return 1.0 for a single-value table; missing tables "
            "default to \"{ { 0 } }\" and would otherwise freeze envelopes at their start value"
        )


def validate_sound_emitter_ownership() -> None:
    effect = read("src", "bse", "BSE_Effect.cpp")
    destroy = body_of(effect, "void rvBSE::Destroy()", "BSE_Effect.cpp")
    for needle in ("FreeSoundEmitter", "StopSound"):
        forbid(destroy, needle, "rvBSE::Destroy",
               "the client effect owns the reference emitter and keeps it across effect restarts")

    for tree in ("game", "mpgame"):
        source = read("src", tree, "client", "ClientEffect.cpp")
        context = f"src/{tree}/client/ClientEffect.cpp"
        dtor = body_of(source, "rvClientEffect::~rvClientEffect( void ) {", context)
        require(dtor, "soundSystem->FreeSoundEmitter( SOUNDWORLD_GAME, renderEffect.referenceSoundHandle, true );",
                f"{context} rvClientEffect::~rvClientEffect")
        restore = body_of(source, "void rvClientEffect::Restore( idRestoreGame *savefile ) {", context)
        forbid(restore, "referenceSoundHandle = -1", f"{context} rvClientEffect::Restore",
               "the sound world restores emitters at their saved indices; dropping the handle "
               "orphans the restored emitter and doubles looping effect sounds after every load")


def validate_sound_segment_diversity() -> None:
    segment = read("src", "bse", "BSE_Segment.cpp")
    check = body_of(segment, "bool rvSegment::Check(rvBSE* effect, float time, float offset) {", "BSE_Segment.cpp")
    start = check.find("case SEG_SOUND: {")
    if start == -1:
        raise AssertionError("Missing the SEG_SOUND case in rvSegment::Check")
    end = check.find("\n\tcase ", start + 1)
    sound = check[start:end if end != -1 else len(check)]
    for kind, flags in (("one-shot", "0"), ("looping", "SSF_LOOPING")):
        needle = f"emitter->StartSound(st->mSoundShader, channel, rvRandom::flrand(0.0f, 1.0f), {flags}, false);"
        if needle not in sound:
            raise AssertionError(
                f"rvSegment::Check must start {kind} sound segments with {needle!r}; retail 1.4.2 passes "
                "rvRandom::flrand(0, 1) as the diversity, and a fixed value makes every multi-sample shader "
                "(impacts, ricochets, explosions) repeat one sample"
            )


def validate_envelope_rate() -> None:
    header = read("src", "bse", "BSE_Particle.h")
    require(header, "mOneOverDuration;", "BSE_Particle.h")
    if not re.search(r"#define BSE_FUTURE\s+\( 0\.016f \)", header):
        raise AssertionError("BSE_FUTURE must stay retail's 0.016 s spawn look-ahead")

    particle = read("src", "bse", "BSE_Particle.cpp")
    require(particle, "mOneOverDuration = 1.0f / duration;", "rvParticle::FinishSpawn")
    for match in re.finditer(r"\n(bool|int|void) (rv\w+Particle)::(Render|Update|InitLight|PresentLight|RenderMotion)\(", particle):
        name = f"{match.group(2)}::{match.group(3)}"
        body = body_of(particle, match.group(0).lstrip("\n"), "BSE_Particle.cpp")
        if "Evaluate" in body and "GetDuration()" in body:
            raise AssertionError(
                f"{name} evaluates envelopes against the live duration; infinite segments extend "
                "mEndTime every frame, so envelopes must use mOneOverDuration fixed at spawn"
            )


def validate_vertex_colour() -> None:
    particle = read("src", "bse", "BSE_Particle.cpp")
    to_byte = body_of(particle, "ID_INLINE byte ToByte(float x) {", "BSE_Particle.cpp")
    require(to_byte, "lrintf(x * 255.0f)", "ToByte")
    forbid(to_byte, "Clamp", "ToByte", "retail packs the low byte of the rounded value without clamping")

    tint = body_of(particle, "dword rvParticle::HandleTint(", "BSE_Particle.cpp")
    require(tint, "GetAdditive() ? 1.0f : effect->GetAlpha()", "rvParticle::HandleTint")


def validate_model_particle_vertices() -> None:
    particle = read("src", "bse", "BSE_Particle.cpp")
    render = body_of(particle, "bool rvModelParticle::Render(", "BSE_Particle.cpp")
    require(render, "dst = src->verts[i];", "rvModelParticle::Render")
    for needle in ("dst.normal = ", "dst.tangents[0] = ", "dst.tangents[1] = "):
        forbid(render, needle, "rvModelParticle::Render",
               "retail keeps model-particle normals and tangents as copied from the model")


def validate_effect_timing_and_order() -> None:
    effect = read("src", "bse", "BSE_Effect.cpp")
    segments = body_of(effect, "void rvBSE::UpdateSegments(float time)", "BSE_Effect.cpp")
    require(segments, "if (GetLooping() && GetAmbient() && mDuration != 0.0f) {", "rvBSE::UpdateSegments")
    require(segments, "mSegments[i].Advance(this);", "rvBSE::UpdateSegments")
    require(segments, "mSegments[i].Rewind(this);", "rvBSE::UpdateSegments")

    light = read("src", "renderer", "tr_light.cpp")
    add = body_of(light, "void R_AddEffectSurfaces(void) {", "tr_light.cpp")
    require(add, "qsort( pending, numPending, sizeof( pending[0] ), R_CompareEffectSubmitDistance );",
            "R_AddEffectSurfaces")


def validate_spawn_domains() -> None:
    domains = read("src", "bse", "BSE_SpawnDomains.cpp")
    normals = body_of(domains, "const idVec3 bseCubeNormals[6] = {", "BSE_SpawnDomains.cpp")
    order = re.findall(r"idVec3\(([-0-9., f]+)\)", normals)
    expected = [
        "-1.0f, 0.0f, 0.0f", "0.0f, -1.0f, 0.0f", "0.0f, 0.0f, -1.0f",
        "1.0f, 0.0f, 0.0f", "0.0f, 1.0f, 0.0f", "0.0f, 0.0f, 1.0f",
    ]
    if order != expected:
        raise AssertionError(f"bseCubeNormals must keep retail face order {expected}, found {order}")
    if not re.search(r"const idMat3 bseModelToEffect\(\s*0(\.0f)?, 1(\.0f)?, 0(\.0f)?,\s*0(\.0f)?, 0(\.0f)?, 1(\.0f)?,\s*1(\.0f)?, 0(\.0f)?, 0(\.0f)?\)",
                     domains):
        raise AssertionError("bseModelToEffect must keep retail rows (0,1,0) (0,0,1) (1,0,0)")


def validate_manager_surface() -> None:
    manager = read("src", "bse", "BSE_Manager.cpp")
    require(manager, 'idCVar bse_showBounds("bse_showBounds", "0", CVAR_INTEGER,', "BSE_Manager.cpp")
    require(manager, 'idCVar bse_scale("bse_scale", "1", CVAR_FLOAT | CVAR_ARCHIVE,', "BSE_Manager.cpp")
    require(manager, 'cmdSystem->AddCommand("bseStats",', "rvBSEManagerLocal::Init")
    require(manager, 'cmdSystem->AddCommand("bseLog",', "rvBSEManagerLocal::Init")


def main() -> int:
    try:
        validate_single_value_tables()
        validate_sound_emitter_ownership()
        validate_sound_segment_diversity()
        validate_envelope_rate()
        validate_vertex_colour()
        validate_model_particle_vertices()
        validate_effect_timing_and_order()
        validate_spawn_domains()
        validate_manager_surface()
    except AssertionError as error:
        print(f"bse_retail_parity: FAILED - {error}")
        return 1

    print("bse_retail_parity: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
