#!/usr/bin/env python3
"""Clip-world queries skip work their results never used (both game trees).

- idClip::Contents gathers only clip models carrying the requested contents
  bits. id's code gathered every model (-1) and filtered them afterwards, which
  defeated the clip sectors' contents test for every point query: liquid probes
  for each projectile, particle and AI every frame.
- idClipModel::Unlink marks a sector's contents summary stale instead of
  rescanning every link in the sector for each link a moving model leaves; a
  query that would scan the sector anyway refreshes it first. The summary stays
  a superset of the contents linked there, so no query skips a sector it needs.
- g_clipFastQueries 0 restores both original paths for comparison; results are
  the same either way.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    for tree in ("src/game", "src/mpgame"):
        clip = (ROOT / tree / "physics/Clip.cpp").read_text(encoding="utf-8", errors="surrogateescape")
        cvars = (ROOT / tree / "gamesys/SysCvar.cpp").read_text(encoding="utf-8", errors="surrogateescape")
        header = (ROOT / tree / "gamesys/SysCvar.h").read_text(encoding="utf-8", errors="surrogateescape")
        check('"g_clipFastQueries",\t\t"1"' in cvars and "extern idCVar\tg_clipFastQueries;" in header,
              f"{tree}: g_clipFastQueries must exist and default on")
        check("bool\t\t\t\t\tdynamicContentsStale;" in clip, f"{tree}: clip sectors need a stale flag")
        unlink = clip[clip.index("void idClipModel::Unlink( void ) {"):clip.index("void idClipModel::Link( void ) {")]
        check("link->sector->dynamicContentsStale = true;" in unlink, f"{tree}: Unlink must mark the sector stale")
        touching = clip[clip.index("int idClip::ClipModelsTouchingBounds("):clip.index("int idClip::GetTraceClipModels(")]
        skip = touching.index("if( !( sector->dynamicContents & contentMask ) ) {")
        refresh = touching.index("if( sector->dynamicContentsStale ) {")
        scan = touching.index("for ( clipLink_t* link = sector->clipLinks;")
        check(skip < refresh < scan, f"{tree}: a stale sector must be refreshed before it is scanned")
        update = clip[clip.index("void idClip::UpdateDynamicContents( clipSector_t* sector ) {"):]
        check("sector->dynamicContentsStale = false;" in update[:400], f"{tree}: a refresh must clear the stale flag")
        link = clip[clip.index("void idClipModel::Link( void ) {"):clip.index("void idClipModel::Link( idEntity *ent")]
        check("sector->dynamicContents |= GetContents();" in link, f"{tree}: Link must keep the summary a superset")
        contents = clip[clip.index("int idClip::Contents( const idVec3 &start"):]
        check("GetTraceClipModels( traceBounds, g_clipFastQueries.GetBool() ? contentMask : -1," in contents[:2500],
              f"{tree}: Contents must gather by its content mask")
        check("if ( ( touch->contents & contentMask ) == 0 ) {" in contents[:3500],
              f"{tree}: Contents keeps its own mask filter (the gather only removes models it rejects)")

    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("clip_query_fast_path_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
