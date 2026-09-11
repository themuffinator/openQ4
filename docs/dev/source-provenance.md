# Source Provenance and Accompanying Terms

This is the authoritative engineering inventory for retained id Software source headers under openQ4's `src/` tree. It records what the tree says about its origin and makes the accompanying notices available; it is not a legal opinion or a conclusion about licence compatibility.

The repository-wide [GPLv3 text](../../LICENSE) remains the primary licence file. Two sets of tracked files also retain headers that expressly refer to a distinct set of Additional Terms:

| Header family | Tracked files at the 2026-09-11 audit | Audited official source snapshot | Accompanying terms in this tree |
|---|---:|---|---|
| Doom 3 GPL Source Code | 595 | [id-Software/DOOM-3 `a9c49da`](https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a) | [`LICENSES/DOOM-3-ADDITIONAL-TERMS.txt`](https://github.com/themuffinator/openQ4/blob/master/LICENSES/DOOM-3-ADDITIONAL-TERMS.txt) |
| Doom 3 BFG Edition GPL Source Code | 37 | [id-Software/DOOM-3-BFG `1caba19`](https://github.com/id-Software/DOOM-3-BFG/tree/1caba1979589971b5ed44e315d9ead30b278d8b4) | [`LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt`](https://github.com/themuffinator/openQ4/blob/master/LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt) |

The two Additional-Terms files preserve the wording published in the corresponding official `COPYING.txt`, including upstream spelling errors; only character encoding, line endings, and insignificant trailing whitespace are normalized for the repository. They are separate because their scope and published text identify different source releases. The machine-readable audit pins the official repositories, full commit object IDs, complete upstream `COPYING.txt` hashes, and canonical UTF-8 local notice hashes in [`source-provenance-manifest.json`](https://github.com/themuffinator/openQ4/blob/master/docs/dev/source-provenance-manifest.json). The local hash normalizes CRLF/CR to LF so Git's checkout policy cannot create a false mismatch; all other text changes fail.

The official Doom 3 BFG repository describes its release as GPL source with omissions for Steam integration, Bink playback, and the depth-fail stencil-shadow implementation. Those omissions are a boundary on what can be copied from that release; they are not an invitation to reconstruct excluded code from non-source binaries. Quake 4 retail assets are not included in either source release and are not relicensed by openQ4.

## Milestone B loading/cache reference boundary

The level-load manifest/envelope format, bounded read/framing pipeline and
immutable DTO, cache manager and filesystem substitution, render-model payloads,
and collision-model payload are original openQ4 designs and implementations. No
cache format, serializer, validation algorithm, pipeline code, or
collision/model codec was copied or adapted from Doom 3 BFG.

During the render-world payload work, the official GPLv3
[`neo/renderer/RenderWorld_load.cpp` at `1caba1979589971b5ed44e315d9ead30b278d8b4`](https://github.com/id-Software/DOOM-3-BFG/blob/1caba1979589971b5ed44e315d9ead30b278d8b4/neo/renderer/RenderWorld_load.cpp)
was consulted only as a field inventory for the finalized classic proc-world CPU
representation: inline world models, inter-area portals, and area BSP nodes.
The openQ4 wire format, bounds, graph and finite-value checks, transaction,
source-authoritative envelope, failure behavior, and finalization integration
were written independently; no BFG code or algorithm was copied or adapted.
Because no BFG-headered source file was added or changed for this work, the
audited Doom 3 BFG header-family inventory remains 37 files.

## Reproducible inventory

Run the offline audit from the repository root:

```text
python tools/validation/audit_source_provenance.py --check
python tools/validation/audit_source_provenance.py --family doom3_bfg
python tools/validation/audit_source_provenance.py --format json --output .tmp/source-provenance.json
python tools/validation/audit_source_provenance.py --check --doom3-source E:\_SOURCE\_CODE\DOOM-3-master --doom3-bfg-source E:\_SOURCE\_CODE\DOOM-3-BFG-master --rbdoom3-bfg-source E:\_SOURCE\_CODE\RBDOOM-3-BFG-master
```

The JSON report is the exact current file inventory. The check fails when a tracked header referring to Additional Terms is unknown, either accompanying notice is missing or differs from its canonical local SHA-256, or a family count changes without an explicit manifest review. When official source roots are supplied, the audit also checks the raw published bytes of each complete `COPYING.txt` against its separate official hash; the UTF-8/newline normalization applies only to the extracted local notice files. A header-family match establishes only that the local file retains that notice. It does not claim byte identity with the pinned official snapshot or reconstruct the complete chain of intermediate forks.

The 37 BFG-marked files currently occupy these groups:

| Classification | Files | Audited path source |
|---|---:|---|
| Path exists in official Doom 3 BFG snapshot `1caba19` | 31 | `neo/`; local `src/imagetools/` corresponds to official `neo/renderer/` |
| Path absent from the official snapshot; BFG-header lineage is present in RBDOOM-3-BFG snapshot `ea29c00` | 6 | `src/sound/OpenAL/AL_Sound{Hardware,Sample,Voice}.{cpp,h}` at `neo/sound/OpenAL/` |

The audit prints every file with its classification. For the six OpenAL files, the pinned RBDOOM tree is an intermediate lineage reference, not proof of the exact commit from which openQ4 first received the file. The optional source-root arguments verify that every configured path actually exists in the identified reference tree. Most Doom 3-marked files came through the historical idTech 4/Quake 4 lineage and do not necessarily have a one-to-one path in the official Doom 3 snapshot, so the inventory intentionally does not invent such a mapping.

## Import policy

Before incorporating more external code:

1. Use an official or otherwise auditable source revision and record its repository, immutable commit, path, and licence family.
2. Verify that every required licence and notice is present. Do not remove or rewrite an upstream header merely to make the tree look uniform.
3. Mark the openQ4 version as altered in the change and its documentation; never represent it as the original upstream program.
4. Add the new local path to the reproducible inventory by updating the expected count after reviewing the audit diff.
5. Add an elegant upstream credit in the relevant documentation or README.
6. Do not import excluded third-party code, game data, proprietary SDK binaries, or source recovered from a retail executable.

The companion `openQ4-game` repository is a separate provenance boundary: its Quake4SDK-derived game-library source remains subject to the Quake 4 SDK EULA. This document inventories this engine repository only.
