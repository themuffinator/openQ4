# Multiplayer player identity, 23 October 2026

This bounded UI-03/UI-09 increment keeps Players selections and local
friend/mute marks attached to the connection that was selected. A replacement
occupant of the same client slot starts unmarked, and the statistics return to
the local player when the selected connection disappears or is replaced.
Death/respawn, score updates and name changes within the same connection retain
the marks. Friends are session marks, not persistent account identities.

## Contract

The game publishes the entity's packed spawn ID as unsigned decimal string
state beside each row's client number and beside the selected statistics.
Strings preserve all 32 bits, including IDs with the high bit set. The
multiplayer menu cover handshake was `mp.protocol 2` in this increment (now
`3`, including Admin target operands); engines and game
modules must match. An older module keeps the classic menu.

`mpSelectPlayer`, `mpMute` and `mpFriend` use the existing `session.menu`
operation with three typed arguments: `command`, numeric `client`, and string
`identity`. Event evaluation freezes these operands. Later changes to
`card.client` or `card.identity` cannot redirect queued requests. The adapter
requires an integer client from 0 to 31 and a canonical unsigned 32-bit decimal
identity. The session forwards bounded operands; the game checks membership
and exact connection identity immediately before selection or mutation.

Interactive rows and Mute/Friend buttons declare optional string `context`
expressions. A changed context cancels an armed press and invalidates queued
control actions. Returning to the old context does not revive them. Unchanged
context survives a score refresh; modal/document identity still guards restored
or replaced views. Context tokens are transient and are never saved. Buttons
without `context` retain their existing activation behavior.

The player object's friend/mute accessors also record the target's packed spawn
ID. They refuse stale generations, empty slots and non-player entities, including
replacement at the same address before any disconnect notification. Clearing a
mark remains valid after its target has disappeared. This adds no network or
save-format fields, display strings or artwork.

## Validation

The native schema/interaction and production roster tests cover typed contexts,
atomic failed updates, held keyboard/pointer input, queued activation, unchanged
score refresh, identity changes back to a prior value, restore and the actual
Escape and Summary documents. Adapter/session tests verify captured operands and
reject missing, malformed, fractional, out-of-range and injected arguments.
`tools/tests/ui_player_identity.py` compiles the actual player accessors and game
dispatch branch against bounded entity doubles for all 32 slots. Removing each
of the friend, mute and dispatch identity checks must fail an assertion.

`tools/ui/capture_player_identity.py` runs a hidden, windowed loopback match on
`mp/q4dm1`. Semantic engine commands select and mark a bot, replace it in the
same slot during a held press and after a queued row event, and then explicitly
select/mark its replacement. Engine render-target screenshots include gameplay,
each menu checkpoint and the return to gameplay. No OS input or capture is used.
The full Windows build/stage and 12 targeted native suites pass. The adapter
passes its production case and rejects 20 compiled mutations; player-state
checks reject three guard-removal mutations. Both live matches pass: OpenGL at
1280×720 and Vulkan at 1920×1080, with 14 reviewed engine screenshots, no
retained-UI warnings and no errors. Stock/bot asset warnings remain. Current
execution results and source/binary bindings are recorded in
`.tmp/ui-player-identity/validation-evidence.json`.

## Scope of the original evidence

The 23 October evidence qualifies local marks and the retained player menu
dispatch boundary. The following increment adds transport and Admin guards;
its evidence is recorded separately. Persistent friends, remote reconnect
timing, all input devices, other platforms and the whole UI corpus remain
unqualified. No product requirement, migration entry or final gate is accepted
by either increment alone.


## Mute, vote and Admin targets, 3 November 2026

Remote mute/unmute now sends the destination slot, an exact 0/1 flag and the
packed 32-bit spawn identity. The server accepts exactly six payload bytes
and checks the live source and destination players before changing the mark.
A delayed unmute cannot clear a separately acquired mark on a replacement.
`si_uiTargetProtocol=1` is a read-only server capability. Without it a new
client applies its local mute but sends no ambiguous slot-only request. New
servers ignore older slot-only requests; local muting on those clients still
works. The existing engine network protocol and voice audio payload are
unchanged.

Admin row selections and Kick, Ban and Force team switch use the same typed
`session.menu` operands as Players: `command`, `client`, `identity`. Row
contexts cancel held input on replacement, and action contexts track the
selected connection. The adapter freezes these operands; the session never
resolves a queued Admin request through the latest row or selection. The game
rejects stale selections and actions, clears the selected connection when it
leaves, and retains it across a name/team change. The engine/game menu-cover
handshake is `mp.protocol 3`; matching engine, game module and menu document
must be staged together.

Remote player administration still uses authenticated rcon. It now sends
`serverAdminTarget <kick|ban|switch> <slot> <identity>` and rechecks the target
on the server immediately before the original action. It never falls back
to slot-only rcon on older servers. Direct console administration remains
available. Managed matches reject this route, including Ban. The classic
menu's cached player choices also retain their original spawn identity.

When the server starts either a legacy or packed kick vote, it captures the
live target identity. Departure/replacement aborts the vote, including its
two-second execution delay. Packed votes remain atomic: none of their other
fields executes after losing the kick target. Both execution entry points
also guard the identity, independent of the normal vote polling path.

`tools/tests/ui_player_targets.py` compiles the production mute sender,
reliable-message branch, receiver, player mute storage, vote lifecycle and
Admin selection/dispatch/rcon methods with engine doubles. It tests malformed
and old mute payloads, high-bit identities, stale mute and unmute, replacement
at the same entity address, mixed-vote cancellation, current Admin actions and
capability fallback. Guard-removal controls must fail. The existing adapter,
session-gate, electorate and production-screen suites cover the changed
contracts. `tools/ui/capture_player_identity.py` also probes held/queued Admin
replacement and a fresh kick in windowed gameplay using engine screenshots.

Validation on Windows x64 used a fresh debugoptimized client, dedicated server
and both game modules, staged with matching packages. The four native multiplayer
screen suites pass, including the production Admin document's held-control
contexts. The session gate passes 1,032 checks plus its server-card, loading and
prompt suites; electorate, legacy-admin and vote-security regressions pass.
The adapter passes the frozen Admin operands and malformed-argument cases and
rejects all twenty compiled mutation controls.
Player-state and target tests reject three and five guard-removal controls,
respectively. Localized label-fit checks pass in all twelve languages.
OpenGL at 1280x720 and Vulkan at 1920x1080 each complete the live map/menu
probe and return to gameplay; all 22 engine screenshots were reviewed. Neither
run reports retained-UI warnings or errors. Stock/bot asset-precache warnings
remain. Exact source/binary hashes, commands, logs and capture bindings are in
`.tmp/ui-roadmap/validation-evidence.json`. These are semantic engine probes,
not physical-device or remote-network qualification.

This is bounded UI-03/UI-09 implementation. A kick request can still outlive
its subject before server admission; retained kick-choice drafts and Match
Control rows still need identity-bound catalog operands. Ban-list refresh,
map-transition identity lifetimes, remote timing, physical devices and other
platforms require further qualification. No product requirement, migration
resource or final gate is accepted by this change.

## Kick choice, draft and request identity, 4 November 2026

The retained kick choice publishes an ordered catalog of client slots and
32-bit spawn identities. Its string `context` closes the popup and cancels
held or queued input when that catalog changes, including a replacement with
the same player name. `session.menuValue` freezes both the proposed row and
the catalog. The session validates their syntax; the game refreshes the catalog
and requires an exact match before resolving the row. A row without a catalog
cannot select a kick target.

The draft retains the selected identity. Departure or replacement clears the
selection and blocks the entire draft, including other changed fields, with
a localized instruction to select again. Explicitly choosing a player or
No one acknowledges that change. Returning to the page with a new draft also
resets it. The menu-cover handshake is now `mp.protocol 4`; engine, multiplayer
module and menu content must be installed together.

The classic menu also rejects its whole draft when the cached kick subject is
gone, rather than silently dropping the kick and submitting the other fields.

Both legacy and packed kick requests append the selected 32-bit identity.
Server admission requires exactly those four remaining bytes and a live
matching player before starting any part of the vote. Missing, truncated,
trailing or stale identity payloads reject the entire request. The previous
guards still protect voting, the passed-vote delay and execution. Ordinary
non-kick requests retain their wire shape. Console calls capture the target
at invocation; menu callers pass the identity already held by their draft.

`si_uiTargetProtocol=2` advertises kick-request identity support. Current clients
retain version-1 mute/Admin support, but refuse remote kick requests to older
servers; the retained Vote page explains that a server update is required.
New servers reject older slot-only kick requests. No proprietary-DLL or retail
network interoperability is implied.

Native tests cover choice contexts, same-label replacement, held semantic and
pointer selection, queued proposals, ABA changes, restoration, and the actual
production Vote document. The compiled game harness exercises the real sender
methods and admission guards, malformed identity payloads, old-server fallback,
ordered catalog selection and draft invalidation. It isolates the kick portion
of catalog refresh from map enumeration. The reusable windowed probe is
`tools/ui/capture_vote_targets.py`; its engine captures and logs qualify menu
selection and loopback slot reuse, not physical input or delayed remote traffic.

Validation uses a freshly built and staged Windows x64 debugoptimized client
and multiplayer module. All ten affected native suites pass; the adapter rejects
all twenty mutation controls and the player-target harness rejects all ten.
The session gate passes 1,033 checks plus its server-card, loading and prompt
suites, including a punctuated catalog kept intact through the command boundary.
Generation/label-fit checks cover all twelve languages; encoding, path and
validation-workflow checks pass.

OpenGL at 1280x720 in English and Vulkan at 1920x1080 in French complete the
same-name replacement, stale mixed draft, explicit No one acknowledgement and
fresh kick flow, then return to gameplay. All 22 final engine screenshots were
reviewed. Neither run reports retained-UI warnings or errors. Each reports 179
stock/bot late-precache warnings and one missing `models/monsters/burn_misc_sm`
image warning; those asset issues remain outside this increment. Exact source,
binary, report and capture hashes, failed attempts and cleanup status are in
`.tmp/ui-vote-targets/validation-evidence.json`.

Match Control rows, ban-list refresh, map-transition identity lifetimes,
remote timing, physical devices and other platforms still need qualification.
No product requirement, migration resource or final gate is accepted here.
