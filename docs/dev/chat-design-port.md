# Q2REX chat design adaptation

Reference inspected: Q2REX's `source/chat/kexUIChatWindow.cpp`,
`source/menu/menu_imgui.cpp`, `source/client/keys.c`, chat CVar registration,
and its player-facing chat changelog. The source repository identifies its
engine as closed-source. openQ4's engine is GPL-3.0 and openQ4-game uses the
Quake 4 SDK EULA. No Q2REX source or assets are incorporated: this is an original
implementation of the observed interaction design, with reference attribution
to the [Q2REX Project Team](https://github.com/themuffinator/Q2REX).

Q2REX retains 64 messages; the panel sizes to content, anchors above the lower
HUD, softens its oldest visible lines, and times out after a configurable hold
followed by a 750 ms fade. It exposes alpha, timeout and position offsets.
Message-mode commands select All/Team on entry and Tab cycles channels. Its
explicit follow-newest state and preservation of measured scroll state address
arrival, eviction and temporarily missing render frames.

openQ4 implements those behaviors with its existing edit widget and retail
Quake 4 materials. The native control is installed in the stock `mphud.gui`
and `mpmsgmode.gui` surfaces when loaded, so it also works without replacement
HUD GUI scripts. A complete message crosses the existing GUI state/event
interface once; the engine owns wrapping and transient presentation history.
No engine/game ABI extension, network message change, or platform UI library
is required. The game module continues to authorize all delivery.

The pure `ChatHistory.h` model records the top row by message serial and source
offset. Width/scale changes rebuild wrapped rows without making visibility a
prerequisite for recording incoming messages. The input adds bounded per-channel
sent-message recall and draft restoration. The public settings use openQ4's
`ui_` naming and the aspect-correct 640×480 canvas. Quake 4 has no equivalent
text-to-voice channel, lobby chat, or split-screen layout, so those Q2REX-specific
paths are not introduced.

Validation uses `tools/tests/chat_history.py` for the production model and
windowed multiplayer gameplay with the registered engine `screenshot` command.
No operating-system capture or keyboard/mouse automation is needed.

Wrapping uses the device context's draw path in measurement mode, including
per-glyph rounding and atomic UTF-8/escape handling. The older
`GetMaxTextIndex` helper rounds accumulated font units differently from drawing;
using it could clip the last word at fractional font scales.

See [player controls and settings](../user/multiplayer-chat.md).

## Validation — 2026-09-05

- Full Windows Meson build passed for client, dedicated server, SP/MP game
  modules and both renderer modules. Staged using the project wrapper and
  `install -C builddir --no-rebuild --skip-subprojects`.
- `tools/tests/chat_history.py`, `tools/tests/cmdargs_append_contract.py`,
  `tools/tests/network_security.py`, and the companion game's
  `tools/tests/mp_match_team_communication_contract.py` passed.
- `tools/tests/chat_gameplay_smoke.py --width 960 --height 720 --stock-gui --gametype DM`
  passed using both chat GUI surfaces extracted from the installed retail PK4s.
  The team-chat alias correctly selected All in this non-team game.
- `tools/tests/chat_gameplay_smoke.py --renderer vulkan --width 1600 --height 720`
  passed in Team DM. The team-chat alias selected Team and delivered the local
  team message with its distinct presentation. An earlier 1280×720 OpenGL run
  also passed.
- Fresh engine screenshots and status reports verified 64-message retention,
  arrival/eviction anchoring, font/width reflow, return to newest, long-wording
  wrapping, passive fading, and a 2× font/200-unit-width layout with extreme
  offsets clamped inside the screen. Final screenshots were inspected visually.
  Evidence is under `.tmp/chat-gameplay/`; the source test reproduces it.
- Tests use a hidden window in windowed mode with mouse/controller input
  disabled, an isolated save directory and a local multiplayer server. They
  issue engine commands, not keyboard/mouse events. Physical key interactions
  were not automated; editor event dispatch and command scripts were reviewed.
- All five new localized labels occur once in each of the six language tables.

## Other findings

- An unrelated `Cmd_OpenQ4MatchControl_f` edit in the companion game's
  `gamesys/SysCmds.cpp` was missing a loop-closing brace and blocked a later
  rebuild. The missing brace was restored; the subsequent full build passed.
- The SDL text-input path still deliberately rejects committed characters
  outside the stock single-byte range. This change preserves that existing
  input contract; rendering received Unicode does not expand typed input.
- The map tests still log pre-existing missing `q4dm1.aas32/48/96/128/250`
  navigation files, the `mp_buying_givecash` sound fallback, and non-pre-cached
  declarations. No native chat parser warnings remain.
