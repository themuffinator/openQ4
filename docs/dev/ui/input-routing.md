# Retained menu ownership and platform input

8 September 2026. Retained documents can now acquire application input through
`ui_retainedOpen "path.q4ui"`. This advances the [full replacement plan](../plans/idtech5-ui.md)
from semantic button fixtures to an engine menu host. The host closes the console,
receives SDL input, owns the session's GUI state and inhibits gameplay controls.
`ui_retainedPreview` remains a passive renderer. Neither command accepts a stock
GUI as a finished replacement; the game action bridge, other widgets, complete
corpus and native editor remain required.

## Input and ordering

SDL supplies absolute window-coordinate pointer positions from motion and button
events. The retained runtime converts window density and viewport origin once,
then uses the presented document's transformed hit geometry. Mouse movement does
not pass through the legacy 640×480 cursor conversion. An initial click supplies
its own position even if no motion preceded it. Pointer leave clears hover.
The menu uses the native cursor during this integration phase; it does not warp
or poll the cursor to synchronize it with a legacy GUI position.

Keyboard, pointer and controller input enter the existing ordered system-event
queue as `SE_RETAINED_UI`, with a fixed engine-owned payload and document
generation. The session consumes this event before legacy GUI/game bindings.
The enum is appended in the engine and companion headers; existing event values,
`sysEvent_t` layout, game API 47 and renderer API 13 are unchanged. Payloads are
copied into queue-owned memory and freed by the event loop. Malformed payloads
and stale generations cannot target a replacement document. Queued key releases
still update physical key tracking after their original document closes.
Broad event-journal/replay qualification remains open.

Shared-header qualification also repaired the companion copy of
`engineWindowState_t`, which lacked the engine's three density fields. All CI
and candidate/release workflow defaults now pin the matching published companion
revision; the parity test checks the pinned files as well as the working tree.

| Input | Menu action |
| --- | --- |
| Tab / Shift+Tab | Next / previous eligible control |
| Arrow keys / D-pad | Spatial navigation |
| Enter, keypad Enter, Space / gamepad South | Accept on paired release |
| Escape / gamepad East, Start, Back | Pop the current modal scope, then close the root menu |
| Primary pointer | Focus, arm and release the hit button |
| Movement stick | Dominant-axis spatial navigation after neutral |

SDL's gamepad South/East buttons map to engine `K_JOY3`/`K_JOY4`; the shoulders
are distinct. Keyboard scancodes, controller buttons and pointer buttons have
separate source IDs. Multiple sources mapped to Accept hold one logical press;
releasing the first source cannot activate while another remains down. Key-up
releases the action captured at key-down, including when Shift changes first.

The device-independent `Input` adapter owns navigation repeat: immediate first
navigation, 320 ms initial delay, then 110 ms between repeats. The newest held
navigation source owns repeat. OS repeated downs cannot duplicate it. Accept,
Back and pointer activation never repeat. A presentation stall produces at most
one navigation step per subsequent frame. Stick navigation uses 50/38 press/
release thresholds in the existing normalized -127..127 range and requires
neutral after opening or resuming input.

Cancellation reaches the runtime before logical releases. Document replacement
keeps held sources blocked until release; focus loss forgets physical sources
whose releases may happen outside the application and rejects orphan OS repeats.
Logical arms supplied by another adapter or a developer command are released too.
Controller disconnect, disable and replacement queue cancellation before their
artificial button/hat releases, so removing a device cannot activate a held
Accept button. The adapter releases its virtual stick source during cancellation
and requires physical neutral before rearming it; replacing a document while
the stick is held cannot leave navigation permanently blocked.
Focus loss, console ownership and invalid output suspend interaction. Video/font
recreation cancels input, advances the generation, resamples window focus and
keeps the presentation clock monotonic. A failed recreation closes the host.

The existing console toggle and console editing paths retain ownership. Other
menu keys cannot execute gameplay bindings. Platform hotkeys remain in their
existing SDL path. When a legacy GUI explicitly takes ownership or a map unloads,
the retained host closes. Temporary legacy coexistence remains part of migration.

## Gameplay handoff

`RetainedUI_IsOpen()` is atomic for the async usercmd reader. Opening/closing
changes ownership under the usercmd critical section, records held keys/direct
buttons/cached axes, clears their accumulated gameplay actions and mouse filter,
and discards queued poll samples belonging to the old owner. Other subsystems'
inhibit bits are preserved. During ownership the usercmd generator emits neutral
gameplay input. After closing, held buttons need release and axes need neutral
before they can affect gameplay. A key release already observed in the ordered
event path clears an obsolete poll-path block during the handoff, preserving
the next fresh press. SDL also suppresses old-owner key repeats so
they cannot execute a bound console command after closing the menu.

The session includes the retained host in its active-GUI query, input inhibition
and SP pause branch. UI repeat and motion run on presentation time while SP is
paused. The asynchronous multiplayer simulation continues with local controls
inhibited. Legacy controller repeat and GUI frame events do not run behind an
owned retained menu.

Back is handled by the host's modal/root lifetime. Other button activations are
bounded, owned document/node/action requests available through
`ui_retainedEvents`; they are not executed as command strings. Application/game
dispatch and source bindings are still pending. Opening a fixture containing
`menu.controls` therefore demonstrates the request and its feedback, not a
completed settings page.

## Qualification

The native retained suite runs the real layout/runtime, fixture and `Input`
adapter. It checks source aggregation, release pairing, repeat cadence and stall
behavior, modifier changes, focus/replacement cancellation, orphan repeats,
pointer sources and logical arms inherited from another adapter.

`python tools/tests/ui_retained_input.py` compiles and executes the production
key mapping, SDL queue helpers, engine transport/clock dispatcher and usercmd
release gates with counted in-memory services. It checks South/East mapping,
console routing, exact fractional coordinates, stick thresholds/neutral,
payload/generation rejection, focus loss/recovery, actual SDL device-release
ordering through the dispatcher, stick recovery after cancellation, close-time key releases,
held movement/buttons/impulses/direct actions and axis recovery. The runtime sink
is counted in that boundary test; the native retained suite separately uses the
real renderer and controls.

`python tools/tests/ui_background_cursor.py` runs the production cursor routing
functions for Windows and POSIX branches across 64 state combinations. Retained
menus do not warp or poll the cursor; disabled input, hidden windows and lost
focus remain guarded. `python tools/tests/sdl3_input_parity.py` also passes.
These tests never control or inject an OS device.
The input dispatcher and cursor regression scripts now run in the CI script
smoke jobs. Shared-header/pinned-revision and release-tooling checks also pass.
Generated offline documentation includes the linked UI fixtures, verification
scripts, migration manifest and dependency notices; its link check passes.

The capture harness's `--retained-open` mode keeps the game hidden/windowed with
mouse/controller input disabled, runs named semantic operations, and records
`ui_retainedOwnership` snapshots around the open interval and after close.
`gameEdit->GetGameTime()` supplies a read-only game clock. The verifier requires
SP time to remain constant while open, MP time to advance, and time to advance
after closing, alongside exact action/state traces and rendered pixel checks.
It captures through the registered engine screenshot command.

Reproduce from a staged package and use fresh output directories for each run:

```powershell
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode sp --renderer gl --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --retained-open --density 1.25 --video-restart --profile-frames 60 --output .tmp/ui/input/sp-gl-125-r8
python tools/ui/capture_legacy_baseline.py --assets 'C:\Program Files (x86)\Steam\steamapps\common\Quake 4' --mode mp --renderer vulkan --retained-document tools/ui/fixtures/interaction-smoke.q4ui --retained-script tools/ui/fixtures/interaction-smoke.cfg --retained-open --density 2 --width 1920 --height 1080 --profile-frames 60 --output .tmp/ui/input/mp-vulkan-200-r8
python tools/ui/verify_interaction_capture.py .tmp/ui/input/sp-gl-125-r8
python tools/ui/verify_interaction_capture.py .tmp/ui/input/mp-vulkan-200-r8
```

The final Windows package passes both capture verifiers:

| Capture | Output / scale | Game time while open (ms) | After close (ms) | Semantic / pixels |
| --- | --- | --- | --- | --- |
| SP `game/airdefense1`, OpenGL, video restart | 1280×720 / 125% | 190224 → 190224, including restart | 190336 | Pass |
| MP `mp/q4dm1`, Vulkan | 1920×1080 / 200% | 3120 → 4336 | 4432 | Pass |

The host reports active session GUI ownership while open and releases it on
close. Input is suspended in these hidden runs, as intended; the script invokes
semantic controls directly. Each script repetition yields `[0,1,0]` action
batches. SP has two activations/ten state observations across restart; MP has one
activation/five observations. The disabled-plate error is 0.55 of 255 across
186 SP pixels and 600 MP pixels. Pressed orange rail coverage is 1,835 and 3,070
pixels respectively. Both final TGAs are byte-identical to the preceding
visually reviewed interaction captures; their matching vector rails, disabled
plate/label fade and panel chamfers were reviewed again. Static orange Controls
plates are fixture art, not implemented selection state or an accepted screen.

The complete Windows engine/renderer/dedicated and canonical SP/MP build succeeds.
All three native retained/document/vector suites pass; the final production
input, 64-state cursor and SDL parity checks pass. The companion revision is
`300aedd9c56e20666ca7eacc0d71db70502660b9`. Final client SHA-256:
`80a0f7ea94e5b59d19dc2d55f13e301c3d6d2f101ffbfa906f49268da0917aa4`.
Engine screenshot SHA-256 values are
`fd953b5b29811af1f1b46578a5faf0ef5c7453980531e6137df38441a6d1f99b`
(SP/OpenGL) and
`15439fc3c3dc21362b6efd0e3d002b2d9665d3954ff795897148ce6970d50a28`
(MP/Vulkan). Full source/module/log hashes, traces and verifier results are
retained in `.tmp/ui/input/summary.json` and each final `capture.json`.

The 60-frame MSVC Debug draw/submission CPU samples measure 2.580/3.447 ms p50/p95
on OpenGL, 2.408/3.188 ms after restart, and 3.525/4.220 ms on Vulkan. These samples
start after warm-up and are not optimized-build, GPU or total input-processing
measurements. Shared GUI ownership was disabled. SP has zero warnings/errors;
MP has zero errors and the same 93 stock warnings (86 unique messages with
identical multiplicities) as the preceding interaction checkpoint. There are
no retained UI diagnostics. Those existing MP issues remain open.

## Remaining completion work

Interactive device testing and supported-platform qualification, touch/scroll/
text editing and IME, gamepad touchpad behavior, text input/caret ownership,
accessibility exposure, sounds, game action dispatch, bindings, other widgets,
the full editor and all GUI translations remain open. Non-SDL pointer routing
has not been qualified. The separate transformed-overflow clip-mask gap remains
recorded in [semantic controls](interaction.md). This work does not promote the
replacement as the default player interface.
