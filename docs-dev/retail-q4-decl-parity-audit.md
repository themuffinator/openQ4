# Retail Quake 4 Decl Parity Audit

This audit compares OpenQ4 decl behavior against the recovered retail Quake 4 decl implementation in `E:\Repositories\Quake4Decompiled-main`, plus the companion game-library registration path in `E:\Repositories\OpenQ4-GameLibs`.

The goal is not byte-for-byte nostalgia by default. The goal is to identify functional gaps that can break shipped assets, editor/tool workflows, packed decl loading, or OpenQ4's ability to behave like retail where compatibility matters.

## Executive Summary

OpenQ4's decl implementation is already close to retail across the concrete framework decl types. `table`, `skin`, `entityDef`, `materialType`, `lipSync`, `playerModel`, and the PDA/email/audio/video family mostly match retail parser behavior, defaulting, validation, and media touching.

After the AF content-mask, playback-finish, effect-allocator, packed-writer-policy, AF `fastEval` serialization, tool-state helper, late-load any-tool helper, and reload-progress tool print fixes, the core decl parsers/defaulting/media-touch paths are functionally close to retail. No further functional gaps are currently identified in this audit; the remaining differences called out below are non-functional or intentional policy choices.

Re-audit note, 2026-04-27:

- rechecked the retail decl manager against the live OpenQ4 manager for packed/loose load flow, guide expansion, validation bracketing, late-load warnings, reload progress, precache command emission, and game-type packed writer coverage;
- spot-checked the recovered retail bodies for `entityDef`, `table`, `skin`, `materialType`, `lipSync`, `playback`, `playerModel`, AF, and the PDA/email/video/audio family against the current OpenQ4 implementations;
- confirmed the current audit conclusion still holds: no new functional decl gaps were identified beyond the already documented non-functional differences and explicit OpenQ4 policy choices.

## Functional Gaps To Resolve

### Resolved P1: AF Content Masks Drop Quake 4 Collision Bits

Retail `DeclAF` supports these additional content tokens:

- `vehicleclip`
- `flyclip`
- `itemclip`

OpenQ4 now supports `none`, `solid`, `body`, `corpse`, `playerclip`, `monsterclip`, and the three Quake 4-specific masks above in both parsing and text regeneration.

Why it matters:

- stock or SDK-derived AF definitions using these contents will parse without preserving the intended collision behavior;
- editor/tool round-tripping can lose these flags;
- any gameplay or physics behavior relying on those masks will diverge from retail.

Resolution:

- `ContentsFromString` recognizes `vehicleclip`, `flyclip`, and `itemclip`;
- `ContentsToString` emits those names when the corresponding content bits are set;
- the implementation uses the existing engine `CONTENTS_*` constants rather than hardcoded values.

### Resolved P2: Playback Finish Missing Retail Tools Notification

Retail `rvDeclPlayback::Finish` resamples playback data and then calls the playback-finished tools hook. OpenQ4 previously performed finalization and resampling without notifying an equivalent tools callback.

Why it mattered:

- runtime playback data likely remains correct;
- editor and recording workflows could miss the completion signal;
- recovered retail code treats this as part of the finalization path, not an optional side channel.

Resolution:

- `rvDeclPlayback::Finish` now calls `DeclPlayback_ToolPlaybackFinished()` after resampling;
- the helper dispatches through the shared `rvDeclPlaybackEdit::PlaybackFinished()` bridge;
- the built-in adapter is a no-op, so non-editor/runtime builds keep the same behavior while tools can observe the finish event.

### Resolved P2: Effect Decl Allocator Can Silently Fall Back To Base `idDecl`

Retail registers `effect` with the BSE effect decl allocator. OpenQ4 routed this through `OpenQ4_AllocEffectDecl()`, which previously returned a real BSE decl only when `bseAllocDeclEffect` was installed. If not installed, it returned `new idDecl()`.

Why it mattered:

- if initialization order regressed, `DECL_EFFECT` could become a base decl with no effect parser;
- this failure mode was quiet and could surface later as missing or malformed effects;
- BSE is now first-party in-tree code, so the decl allocator should be a hard boot invariant.

Resolution:

- `OpenQ4_AllocEffectDecl()` now fatals when the BSE decl allocator is missing, returns null, or returns a non-BSE decl instance;
- decl startup probes the effect allocator before registering the `effect` type, making boot order regressions immediate;
- the integrated BSE decl allocator remains installed even if the runtime BSE manager is disabled after initialization failure;
- dedicated-server builds now link the integrated BSE decl code and install the decl allocator while keeping the disabled BSE runtime manager path.

### Resolved P2/P3: Packed `.decls` Writer Type Coverage Is Now Explicit

Recovered retail writer evidence indicates the game packed-decl section includes:

- `entityDef`
- `mapDef`
- `camera`
- `articulatedFigure`
- `export`

OpenQ4's writer additionally includes:

- `model`
- `playerModel`

Why it mattered:

- this may be an intentional OpenQ4 improvement, because OpenQ4 owns its game modules and unified packaging;
- it is still a parity divergence for byte-for-byte writer behavior;
- tooling that compares packed files against retail expectations needs to know this is policy, not drift.

Resolution:

- OpenQ4 now keeps the extended game-section writer as the default project policy, so `model` and `playerModel` stay available in normal OpenQ4 packed exports;
- exact-retail packed output is available through the explicit `com_singleDeclFileWriteMode` setting or `writeDeclFile retail`, which omits `model` and `playerModel` from the game section;
- `writeDeclFile` accepts named `openq4` or `retail` mode arguments, and exported logs report which policy was used;
- runtime decl loading behavior stays unchanged, so this remains an export/tooling choice rather than a compatibility regression in normal startup.

### Resolved P3: AF `fastEval` Serialization Now Matches Retail Text

Retail AF parsing treats bare `fastEval` as true and retail writing emits bare `fastEval` only when true. OpenQ4 previously accepted both bare and explicit boolean forms, but wrote `fastEval 0` or `fastEval 1`, and its default definition included `fastEval 0`.

Why it mattered:

- runtime behavior is probably safe because OpenQ4 accepts the explicit boolean form;
- editor/exported AF text will not match retail format;
- generated output can become noisier and less compatible with tools expecting retail-style text.

Resolution:

- the parser still accepts explicit `0`/`1` and `false`/`true` values for OpenQ4 tooling;
- bare `fastEval` now behaves like retail anywhere in the settings block instead of only at the end of the block;
- rebuilt AF text emits bare `fastEval` only when true;
- the built-in default AF definition no longer writes `fastEval 0`.

### Resolved P3: EntityDef Tool-State Checks Now Share One Retail-Like Helper

Retail checks active tool state through `rvTools::IsToolActive(mask)`. OpenQ4 previously used `com_editors & mediaCacheToolMask` directly in the entityDef caching path.

Why it matters:

- if `com_editors` is a complete mirror of active tool state, behavior is equivalent;
- if any editor state is represented outside that bitfield, entityDef media caching can diverge during AAS, spawn GUI, or decl validation tool runs.

Resolution:

- active tool-flag masking now goes through shared OpenQ4 helpers in `Common.*` rather than open-coded `com_editors` tests in decl code;
- `idDeclEntityDef` now asks `OpenQ4_ShouldCacheEntityDefMedia(noCaching)` instead of carrying its own raw editor-mask check;
- `idCommonLocal::DoingDeclValidation()` now uses the same masked-tool helper, so decl validation and entityDef caching share one source of truth while preserving current `com_editors` behavior.

### Resolved P3: Decl Validation Tool Bit Bracketing Is Centralized Again

Recovered retail notes called out a validation path bracketed by an editor/decl-validation tool bit. OpenQ4 now keeps that state change in one RAII scope around `idDeclManagerLocal::Validate(...)`, and `idCommonLocal::DoingDeclValidation()` plus entityDef cache suppression both read through the same helper family in `Common.*`.

Why it mattered:

- entityDef cache suppression depends on the validation state;
- startup validation can otherwise produce different cache behavior from retail;
- this is the sort of coupling that regresses quietly.

Resolution:

- `EDITOR_DECL | EDITOR_DECL_VALIDATING` is now asserted only in one validation entry/exit scope inside the decl manager;
- `DoingDeclValidation()` and entityDef media-cache suppression both use shared tool-state helpers instead of open-coded checks;
- no other framework/game-library call sites are currently mutating `EDITOR_DECL_VALIDATING`, which keeps the invariant easy to audit.

### Resolved P3: DeclManager Late-Load Warning Now Uses One Retail-Like Any-Tool Helper

Retail `idDeclLocal::ParseLocal` suppresses its late-load `Loading non pre-cached ...` warning through a dedicated helper that asks the tools layer whether *any* tool is active. OpenQ4 now centralizes that decision in one shared helper instead of open-coding a focus-plus-bitfield check in the decl manager.

Why it mattered:

- the old condition was only equivalent if tool focus and every active tool bit were perfectly mirrored by `IsToolActive()` plus `com_editors`;
- any future tool-state change can reintroduce noisy late-load warnings during decl-editor, spawn-GUI, validation, or similar tool-driven parses;
- this duplicates the same tool-state drift risk that already motivated the entityDef cache helper fix.

Resolution:

- `Common.*` now exposes `OpenQ4_IsAnyToolActive()` alongside the existing masked-tool helpers;
- `idDeclLocal::ParseLocal` now uses that shared helper for its late-load warning suppression instead of open-coding the focus/bitfield check;
- the helper currently resolves against `com_editorActive` plus the active editor-bit mask, keeping the retail-like policy centralized with entityDef cache and validation helpers.

### Resolved P4: Decl Reload Progress Now Uses The Retail Tool Print Surface

Retail `DeclManager_ShowReloadProgress(...)` routes reload progress through the tools print surface. OpenQ4 now routes the same `N/M: fileName` progress line through one shared tool-print helper instead of printing directly to the normal console.

Why it matters:

- decl reload feedback can bypass tool-specific output panes or decl-browser consoles;
- tool-driven reload workflows become harder to track than in retail;
- the current behavior is harmless for normal runtime users, but it is still a real tool-workflow divergence.

Resolution:

- `Common.*` now exposes `OpenQ4_ToolPrint()` so tool-routed output policy lives in one place alongside the existing decl/tool helpers;
- decl reload progress now goes through that shared helper, which sends output to the active tool surface when available and falls back to the normal console otherwise;
- the decl browser now exposes a status-bar print hook, so reload progress lands in the same tool-facing UI path instead of disappearing into the generic runtime console.

### P4: Non-Functional Parity Differences

These do not currently look like compatibility bugs, but are worth documenting so future audits do not rediscover them:

- `skin` `FreeData` clears associated model names in OpenQ4, while retail only clears mappings there and clears associations during parse. This is cleaner lifetime behavior unless a tool relies on associations surviving a raw `FreeData`.
- some `Size()` methods account for allocation more completely than retail. This improves diagnostics and should not affect runtime behavior.
- several OpenQ4 parsers add null guards or safer IO checks around retail behavior. These should be preserved unless exact crash-for-crash parity is explicitly required.

## Confirmed Aligned Areas

These areas appear functionally aligned with retail after source comparison:

- decl manager registration order for framework decl types;
- loose and packed decl loading, including stub expansion and stored-index preservation;
- `FindDeclWithoutParsing` returning only already allocated decl objects;
- `BeginLevelLoad` and `EndLevelLoad` purge/default/material-use behavior, allowing for local refactors;
- decl validation bracketing now lives in one RAII scope around `idDeclManagerLocal::Validate`, with shared helper-based reads in `DoingDeclValidation()` and entityDef media-cache suppression;
- `table` parsing, lookup, snap/clamp behavior, wrapping, and validation;
- `skin` material remapping, wildcard/default behavior, and implicit material-backed skins;
- `entityDef` inheritance, duplicate handling, classname insertion, defaults, and validation structure;
- `materialType` parsing, hit-image cache behavior, and closest-color lookup;
- `lipSync` parsing and percent-character rejection;
- `playerModel` fields, media-cache dictionary, and validation;
- PDA, email, video, and audio decl parsing/defaulting/media touching;
- companion game-library registration of `model`, `export`, `camera`, `entityDef`, and `articulatedFigure` decl types.

## Suggested Resolution Order

No unresolved functional gaps are currently identified in this audit.
