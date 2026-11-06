# Audio and voice evidence — 21 October 2026

This is a supporting evidence record for AUD-01 through AUD-07 in the
[roadmap](roadmap.md#audio-and-voice), not a separate work plan. The seven items
remain open until their own acceptance gates are satisfied.

## Tested implementation

Windows x64, MSVC debugoptimized, `builddir`, staged through the project Meson
wrapper into `.install`; base revision `cbe967f2b61935da283a873a731fe975926cf214`
plus the audio changes described here (the engine log reports
`0.14.000-dev+g4085caf9.dirty`, the branch tip before its last test-only and
documentation commits). Every runtime report named below was rerun on
21 October on these final staged binaries:

| Binary | SHA-256 |
| --- | --- |
| `openQ4-client_x64.exe` | `b21f7cd6243a582269d11a60135b45a078891f0af5fabdd750369a278605a1b8` |
| `openQ4-ded_x64.exe` | `dd7e5ef0691b1506be716e55cf8597eac814a6ac3f2d7f44cd51320eaefe3759` |
| `baseoq4/game-mp_x64.dll` | `4cc4c11d3fa510813b2e671d94db9a74d1910e730d3f53814274fd96af3b17fd` |
| `baseoq4/game-sp_x64.dll` | `1773b0729b1701ede1f9db872cc58b248265305d6afde721ec7f5fb4009b96b9` |
| `baseoq4/pak0.pk4` | `de78cd747b9e235e958417a821615d38aa26236514da4136656be45c146a6deb` |
| `OpenAL32.dll` (OpenAL Soft 1.25.2) | `c7905985c3a48cf88b0365879501cbe7b65b3ccf47ff1be3937b0edc079ba6c6` |

Earlier report directories under `.tmp/audio` remain as development history;
only the `final3-*` reports below, and the `final-*` OpenAL loopback reports,
count as evidence for these binaries.

Tests use the installed Steam 1.4.2 PK4s and the user's read-only Awakening
`q4xbase` content under the main checkout's `.home`. Writable profiles, generated
tones, engine screenshots, OpenAL WAV output and logs stay in this worktree's
`.tmp/audio`. No physical microphone, host input injection, desktop capture or fullscreen
launch is used. Another worktree can run concurrently; the harness explicitly
allows multiple instances and never stops a process it did not launch.

## Dialogue volume and pause

`s_dialogueVolume` is archived, defaults to 1 and accepts 0–1. The Audio page has
a slider and numeric value with labels in all twelve shipped language tables.
The player guide describes the setting. The central sound-channel mix applies
it to the sample actually playing, including a leadin/loop transition:

- Explicit `SSF_IS_VO` and `SSF_VO_FOR_PLAYER` speech, including NPC lip-sync.
- Canonical `sound/vo/` and localized `sound/vo_<language>/` samples, independent
  of the calling NPC, script, BSE source or radio channel.
- Music routing has precedence. Neither `SND_CHANNEL_VOICE` (also used by the
  Dark Matter weapon) nor broad `SSF_VO` (also applied to GUI sounds) alone
  classifies an effect as dialogue.

No new sound-save fields are introduced. Master gain, authored distance,
occlusion, reverb and voice priorities keep their existing roles. Mixed speech
and music in a single recording cannot be separated by this control.

`tools/tests/audio_dialogue.py` enters `game/airdefense1` or Awakening's
`game/m01_stranarus_trench1`. Generated 440 Hz speech and 880 Hz effect fixtures
overlap on independent channels. Actual OpenAL source reports show speech gains
1, 0.25, muted, then 0.6 while the effect stays at 1. The 0.6 ratio survives
pause/resume, time scale 0.5 (pitch also 0.5), `s_restart` and save/load. The
shared campaign configuration archives 0.6. English and Turkish text with
English audio fallback pass. OpenAL renders to WAV; the screenshot command
captures the Audio page through its normal semantic menu actions.

Final reports: `.tmp/audio/final3-dialogue-stock/result.json` (28 checks:
compressed fixtures, 5.1 output, 16 MiB decode and 192 MiB aggregate budgets)
and `.tmp/audio/final3-dialogue-awakening/result.json` (25 checks: PCM fixtures,
5.1, the same budgets), plus the Turkish-text/English-audio fallback run
`.tmp/audio/final3-dialogue-turkish/result.json` (23 checks, stereo). The
English Audio-page capture was visually inspected. This proves fixture isolation in running maps, not a
complete listening pass over every authored NPC/BSE/script/cinematic scene.

## Decode admission before streaming

Vorbis admission now bounds compressed input plus pending PCM in bytes, including
synchronous fallback. `s_decodeMemoryMB` defaults to 64 MiB on all platforms;
the existing 48-job cap remains. Admission joins older work before allocating
more. A reservation remains owned until join/cancellation even if its worker
has finished. Failed probes and cancellation release it. Ogg files lacking a
usable length use a fixed 16 KiB counting buffer and rewind before allocating
the final PCM, replacing the unbounded doubling helper.

`listSounds` additionally reports retained CPU sample data, queried OpenAL
buffer sizes, conservatively reserved compressed input, their aggregate and
the admission peak/wait count. At the final gameplay probes:

| Run | Transient peak | Admission limit | CPU PCM | OpenAL PCM |
| --- | ---: | ---: | ---: | ---: |
| Stock map, Turkish text | 30,087 KiB | 64 MiB | 0 KiB | 198,184 KiB |
| Awakening m01 | 47,635 KiB | 64 MiB | 0 KiB | 265,340 KiB |

Both finish with zero outstanding reservation and no budget rejection. A
deliberately lower 16 MiB test exercised backpressure but rejected three long
Awakening tracks; it is not evidence of an intact scene mix. The harness now
requires zero rejections. Header inventory finds no installed Ogg above 64 MiB
input+PCM: the largest stock pair is 40,333,330 bytes and the largest Awakening
pair 47,258,520 bytes. Both exceed 32 MiB, so a 32 MiB mobile default would be a
compatibility regression before streaming exists.

These earlier measurements excluded stb decoder metadata, non-Vorbis loading,
resident CPU/OpenAL data, driver internals and voice queues. The follow-up below
adds fixed decoder arenas and compressed streaming. Android low-memory
qualification remains open. `tools/tests/audio_decode_budget.py` executes production admission,
reservation, join and cancellation methods, including failed decodes and a
runtime budget reduction.

## Compressed streaming

Long Ogg samples now retain their metadata and 60 Hz amplitude envelope, with no
complete compressed file or decoded PCM resident. Above 512 KiB decoded size,
each active voice opens its own VFS/PK4 stream with a 64 KiB read cache, fixed
2 MiB Vorbis arena and three PCM queue buffers (4,096 frames when first
measured; now about a third of a second each, see below). The separate
`s_streamMemoryMB` limit defaults to 32 MiB, reserving 2,304 KiB per voice including
scratch, queued PCM and conservative VFS bookkeeping. Stop and purge release
decoders, file handles and queue buffers. Short-track decode jobs now include
their fixed 2 MiB arenas in `s_decodeMemoryMB` reservations as well.

`tools/tests/audio_streaming.py` generates original mono/stereo Ogg fixtures at
22.05/44.1/48/96 kHz (ffmpeg or `imageio_ffmpeg` required), then compiles the real
bounded reader and production queue transitions. Full and streamed PCM compare
exactly, including sample seeks, independent overlapping cursors, short reads,
EOF, compressed leadin/loop, mixed static/streamed transitions and a self-loop
from a restored offset. Upload failure releases the decoder. A stopped streaming
source requests recovery at the game's sound clock instead of replaying stale
PCM. `audio_stream_budget.py` executes production admission/ownership through
saturation, stop/retry, missing input, decoder failure and a reduced live budget.
`audio_payload_restore.py` also proves compressed streams never restore full PCM.

The final Windows stock and Awakening runs use `audio_dialogue.py --compressed
--decode-memory 16`, enter the same maps as the earlier dialogue tests, and pass
all 24 checks. They exercise mute, pause, time scale 0.5, restart and save/load
(correction, 24 October 2026: the Awakening run kept its opening cinematic, which
refused the save, so only the stock run loaded a saved game; see
[real saves](#real-saves-in-the-subtitle-and-dialogue-harnesses));
actual rendered 440/880 Hz tone amplitudes exceed 0.05 in each output. The stock
save-game stall also exercises underrun recovery at a nonzero dialogue offset.

| Final run | Decode peak / limit | OpenAL sample PCM | Stream peak / limit | Reported aggregate payload bound |
| --- | ---: | ---: | ---: | ---: |
| Stock airdefense1 | 15,447 / 16,384 KiB | 118,853 KiB | 9,216 / 32,768 KiB | 128,242 KiB |
| Awakening m01 | 16,352 / 16,384 KiB | 148,274 KiB | 4,608 / 32,768 KiB | 153,055 KiB |

Both finish with zero outstanding decode reservation and no rejected streams.
The aggregate includes sample CPU/OpenAL data, envelopes, pending input/arenas
and stream reservations; it excludes driver internals and other engine objects.
Reports: `.tmp/audio/stream-stock-final/result.json` and
`.tmp/audio/stream-awakening-final/result.json`. Client SHA-256:
`01a735014944a4efb233b3b4d57a8de52eabb6d4a36a697f68a16a64b1f7fe81`.
On the final binaries, the compressed stock run
`.tmp/audio/final3-dialogue-stock/result.json` again streams both fixtures with no
rejection (decode peak 14,577 of 16,384 KiB, stream peak 9,792 of 32,768 KiB).
The reports retain runtime and harness identities. The `stream-awakening` run
used a stock map in the expansion game directory; only the final m01 run is
counted as Awakening scene coverage.

The bundled MIT/public-domain stb_vorbis receives a small read/seek/tell callback
extension, with matching canonical/external copies and normal/no-stdio/push-only
compilation checked. No extracted audio is added to Git. Streaming currently
requires usable length metadata and supports tracks up to one hour. Its initial
envelope pass is bounded but synchronous; load-time profiling, mixed-format
leadin/loop behavior, an enforced joint residency limit including WAV/RoQ and
Android/device breadth remain open under AUD-03. The decoder and queue limits
alone are not a total audio-memory guarantee.

### Queue depth across long frames, 22 October 2026

On the merged A+B build (`9255c199c`) `audio_dialogue.py --compressed` failed
4 of 4 runs with about 25 underruns each, and `audio_memory_runtime.py` failed
`retry_and_restarts_play_both`. Earlier passing reports on the same audio code
came from before 23:51 on 21 October. From then on, the laptop sat in Modern
Standby with the display off (Kernel-Power event 506, no 507 afterwards), and
every failing run fell in that period. With `com_speeds 1` the hidden GL window
ran at p50 139 ms with 49 frames over 200 ms and a 2.5 s maximum; the time was
outside the game, front end and back end, in presentation. With
`r_swapInterval 0` the median returned to 17 ms, but periodic ~450 ms present
stalls remained. Vulkan, which renders hidden windows offscreen and never
presents (90ed5029), paced at p50 17 ms with no stalls apart from the first
frame, `s_restart` and save/load, and the same dialogue run passed. CPU load
stayed at 8-30%, with no build processes and over 36 GB of free commit. The
~500 ms CPU p99 in the same day's GL stock baseline has the same cause.

The trigger was environmental, but the failures exposed a real limit. Every
bounded queue (streamed Ogg, CPU-resident PCM without an uploaded buffer,
centre-routed mono) held three 4,096-frame buffers and was refilled once per
main-thread frame. That is 512 ms at 24 kHz and 279 ms at 44.1 kHz. Each
underrun followed a frame of about 450 ms or longer, so a single loading hitch
of that size cut out music or dialogue until the channel restarted it at its
clock. The 64 MiB fallback WAV in `audio_memory_runtime.py` ran dry the same way
before its probe.

Queue buffers are now sized by duration within the bytes the reservations
already count (`src/sound/SoundQueue.h`). A buffer holds about a third of a
second, up to one 4,096-frame 5.1 block (48 KiB). Mono therefore keeps about
1.0 s queued at rates up to 48 kHz, stereo 1.0 s at up to 24 kHz and at least
768 ms at 44.1/48 kHz. Centre-routed mono uploads 5.1 frames, so eight routed
voices at a time take deep slots of up to 16,384 frames per buffer for the
same depth. Further routed voices keep 4,096 frames. The fixed 20 MiB
reservation now also asserts the slots' extra bytes and their routing scratch
(about 19.1 MiB in total). Stream reservations, aggregate admission and the
reported memory numbers are unchanged. The memory runtime still reports
214,246 KiB reserved through both restarts. `audio_streaming.py` now proves the
one-second depth, the per-buffer byte bounds, continuity across multi-read
buffers, and that deep slots are bounded, reused and returned. Against the
previous voice code it fails.

The post-fix runs were made in the same display-off state, with machine load
recorded for each run (`.tmp/mrt/*.jsonl` in that worktree):

| Suite | Before | After |
| --- | --- | --- |
| `audio_dialogue.py --compressed --speakers 6 --decode-memory 16 --audio-memory 192` | 0/2 (25 and 30 underruns) | 3/3 |
| `audio_dialogue.py --compressed` (2 speakers) | 0/4 | 3/3 |
| `audio_memory_runtime.py` | 0/1 | 3/3 |

A frame-timed run of the fixed build had the same stall profile as the failing
one (p50 172 ms, 15 frames over 300 ms, two over 1 s) and logged no underruns,
against 30 before. A hitch longer than the queue still recovers at the channel's
sound clock, as before. Refilling from a worker thread was not attempted: voice
queue state has no locking and is touched only on the main thread.
On the final build (branch tip `ec89ff31c`, client SHA-256 recorded in each
report) the native Meson suite passed 276 of 276, and so did the audio,
sound-settings, recovery, payload-restore, portability and validation-hardening
static tests.

## Mod room rolloff

The voice now sets `AL_REFERENCE_DISTANCE` from the authored minimum distance,
in the same map units as the source. This operates independently of the optional
source-radius extension. A zero/invalid minimum has a small positive reference;
the base setter already sanitizes nonfinite/negative input. The dry path still
uses engine attenuation and `AL_ROLLOFF_FACTOR 0`.

`tools/tests/audio_mod_reverb.py` renders deterministic noise with the staged
OpenAL provider through `ALC_SOFT_loopback`, measuring PCM rather than merely
checking API calls. With minimum 100 and distance 200, room rolloff 1 gives wet
RMS 0.0068158555, exactly half the near-reference RMS 0.0136317110. The old
one-unit reference gives 0.0000681586. Scaling distance and reference together
leaves the result unchanged. Stock room rolloff 0 and dry RMS are unchanged.
Final report: `.tmp/audio/final-mod-reverb/result.json` (6/6).

The [EFX guide](https://openal-soft.org/misc-downloads/EffectsExtensionGuide.pdf)
and the pinned provider's `alc/alu.cpp` define the reference-distance behavior.
The local retail reconstruction also sets the source minimum, but is a
structural reference with known reconstruction errors, not standalone retail
parity evidence. No external implementation was copied. Global distance
defaults, 5.1 centre placement and full retail audible-distance parity were not
changed or qualified by this measurement.

## Aggregate payload admission

`s_audioMemoryMB` now defaults to 256 MiB (range 32–2048). One byte ledger
admits sample CPU data, compressed input, pending decoder arenas, amplitude
envelopes, OpenAL sample uploads and stream reservations together. A fixed
20 MiB conservative allowance covers all 96 hardware voices' three 48 KiB
queue buffers (one 4,096-frame 5.1 block each), the deep centre-routed slots,
voice-chat rings and scratch. Stream reservations also include their
queues; this intentionally overcounts them. This is a managed-payload ceiling,
not a provider-process/RSS limit: driver internals, resource metadata and general
filesystem caches are outside its claim.

Admission joins pending work and may reclaim inactive, unreferenced old-map
samples. Current-map, never-purge and live-channel references remain. Worker
completion can release compressed input; decoder arenas stay reserved until
joined or cancelled. WAV/ADPCM allocations and temporary decode overlap count
before allocation. RoQ now scans chunk headers, then decodes audio directly into
one admitted PCM buffer through 4 KiB input scratch, skipping video data. It no
longer loads the complete video or grows a second PCM container. Envelope
allocations and failed uploads release reservations on all tested exits.

A failed static upload retains CPU PCM for three bounded fallback buffers;
each holds at most one 4,096-frame 5.1 block of bytes (about a third of a second
of mono or stereo PCM since 22 October) for ordinary PCM as well as centre
routing. Failed sample loads stay retryable and never become cached replacement
beeps. Exhaustion can omit new audio when it cannot fit; it does not evict active
sounds. Reducing the budget during playback blocks new admissions without
revoking already admitted data, so existing usage and historical peak can
exceed the newly selected limit until data is released. Restart/map changes
release owned allocations; `listSounds` reports current/peak reservation and
refusal count. Its decoded-size totals now use 64-bit arithmetic.

`audio_memory.py` compiles the production admission/allocator, envelope ownership,
RoQ loader and ADPCM decoder. It checks arithmetic overflow, exact saturation,
concurrent worker release, allocator failure, budget reduction, mono/stereo PCM,
chunk boundaries, truncated/mixed data, failed admission and retry. Existing
production decode-join/cancel, stream lifetime and PCM-queue tests pass, now
including aggregate stream refusal and resident fallback queue continuity.

Loaded-map results at a 192 MiB aggregate budget and a 16 MiB decode budget:

| Scene | Peak managed reservation | Refusals | Scope |
| --- | ---: | ---: | --- |
| Stock `game/airdefense1` | 149,475 KiB | 0 | Compressed dialogue/effect, 5.1, mute, pause, half speed, restart, save/load |
| Awakening `game/m01_stranarus_trench1` | 180,190 KiB | 0 | PCM dialogue/effect, 5.1 and the same lifecycle checks |

Reports: `.tmp/audio/final3-dialogue-stock/result.json` and
`.tmp/audio/final3-dialogue-awakening/result.json` (harness SHA-256
`8fc4b57342564c648d4626e86b4c38326a07f3cdba515c5f0c2fe5167a442b40`); each
identifies its exact engine, modules, staged content and harness. A preceding stock run was interrupted by a
natural campaign radio line and failed its fixture gain checks. The mixer harness
now uses a real saved fixture entity with its own radio channel; the campaign
continues running and cannot replace the fixture on the player's emitter.

`audio_memory_runtime.py` then loads a real map, reduces the budget to 32 MiB and
requests an original 64 MiB WAV. Existing audio keeps playing; the refused sample
remains unloaded. Restoring 256 MiB permits its CPU payload but refuses the extra
full OpenAL copy. Actual rendered output contains both fixture tones through the
bounded fallback. Current reservation is 214,246 KiB before and after two sound
restarts, with no replacement beep or reservation growth. The final report
`.tmp/audio/final3-memory-pressure/result.json` passes all 13 checks, including
that a budget refusal is no longer reported as corrupt ADPCM/PCM media.

These tests do not establish whole-process memory bounds, Android OS low-memory
notification handling, all-map residency, loading-time budgets or physical-device
qualification. AUD-03 remains open for those gates and lengthless streaming.

## Centre-channel placement

`SoundSurround.h` routes eligible mono samples through bounded 5.1 queue
buffers (4,096 frames, or about a third of a second for the eight voices holding
a deep slot since 22 October) only when the provider reports a centre-capable output and supports
multichannel/direct-channel playback. Explicit `SSF_CENTER` uses fraction 1;
global/radio VO uses `s_centerFractionVO` (default 0.75). Centre amplitude is
the fraction; the remaining power is shared equally by FL/FR/SL/SR. LFE stays
silent. Positional NPC speech retains the original mono directional path.
Stereo and active HRTF also retain that path. Streaming reservations now include
the larger 5.1 conversion scratch and queues; the earlier streaming reports
above retain their original binary and reservation measurements.

`tools/tests/audio_center.py` passes 26 checks, including every signed 16-bit
sample at four fractions and real OpenAL loopback PCM on 5.1, 6.1, 7.1, quad,
stereo and HRTF outputs. Its extension gating matches the engine, including
`AL_SOFT_direct_channels_remix`. At fraction 1 on 5.1, centre RMS is 0.08838810
and every other channel is zero. At 0.75, centre RMS is 0.06629362, four
surrounding channels are 0.02923290 and LFE is zero. On 6.1 and 7.1 the centre
stays exclusive for explicit sounds, VO keeps the configured fraction with unit
power and LFE stays silent. Quad (no centre speaker) keeps the original path,
and stereo and active-HRTF comparisons have identical PCM hashes. The production queue
tests cover centre-converted static and streamed leadin/loop combinations,
partial static buffers and underrun recovery.

Loopback report: `.tmp/audio/final-center-layouts/result.json` (26/26). The
gameplay reports `.tmp/audio/final3-dialogue-stock/result.json` (28 checks) and
`.tmp/audio/final3-dialogue-awakening/result.json` (25 checks) pass with six-channel
OpenAL wave output and a 16 MiB decode budget. Both measure stronger 440 Hz
dialogue in FC than the surrounding channels, with an independent 880 Hz effect,
and cover gain/mute, pause, slow motion, restart and save/restore. The harness
compares channels within the windows where the speech fixture is loud: the
median centre-to-surround ratio is 2.27 in Awakening's quiet scene (the
theoretical value at fraction 0.75) and 1.72 in airdefense1, whose own ambience
adds 440 Hz energy to a surround channel. An earlier whole-recording peak
comparison failed a correct mix on a fast run for that reason.

These are rendered-PCM checks, not physical-speaker listening tests. Live output
layout changes, physical devices and explicitly centred positional mod sounds'
wet-send behavior remain unqualified.

## Multiplayer voice

The former disabled voice path is implemented. Engine/game API **53** appends
dedicated voice callbacks; matching SP/MP modules are required. The existing
gameplay protocol stays at 2.41, with explicit `si_voiceProtocol` and
`ui_voiceProtocol` capability opt-in. Unsupported peers do not receive the new
message type. The old reliable VOICEDATA path is discarded.

- **Capture and consent:** SDL's default recording endpoint, native signed
  16-bit mono at 8 kHz. `s_voiceChatCapture` starts off separately from archived
  Send Voice. Only PTT or an explicit local test opens input. Releasing PTT,
  disabling consent, losing the active session, restart and shutdown destroy
  the stream and clear queued capture. One-second retries apply to failures,
  not quick PTT presses; a stream stalled for two seconds is reopened. SDL
  callback draining and main-thread access share the stream lock. Each queue
  holds four 20 ms frames; overflow replaces old audio, never grows a container.
- **Codec:** first-party mu-law companding, 160 samples per independently
  decodable frame, 64 kbit/s speech payload. A version/sequence/capture-time
  header makes each frame 169 bytes. No codec history crosses packet loss;
  gaps remain gaps rather than retransmitted or time-stretched speech.
- **Admission:** exact lengths/version, increasing wrapping sequence numbers,
  a three-frame burst and 50-frame/s sustained limit. Capture older than 120 ms
  is discarded. The server and receiver reject frames more than 120 ms later
  than their learned clock offset; that baseline survives PTT gaps. This is
  an **added-delay bound**, not proof of absolute cross-machine one-way latency.
  The learned offset tracks sender clock drift: the baseline may slew towards a
  later offset by at most 2 ms per second of accepted active traffic, so
  independent monotonic clocks cannot silence a healthy talker after hours,
  while a backlog delayed by a PTT gap is still rejected and rejected frames
  never train the clock. Initial skew and real multi-hour sessions still need
  qualification outside simulation.
- **Transport:** authenticated unreliable messages carry no routing authority
  from the sender. Exact server recipients retain team/arena/spectator and
  managed-match policy, receive preference and mute. No following-spectator
  fan-out, demo or repeater forwarding occurs. A fixed mailbox keeps at most
  one current frame per three talkers per recipient. It is flushed after
  snapshots, expires entries after 100 ms, respects a maximum 20 ms rate burst
  and refuses existing fragments or a reliable backlog that would fragment it.
  Failed sends are discarded; no voice retry queue can block later snapshots.
- **Playback:** three talkers, four OpenAL buffers each, real-time pitch 1 and
  master × receive-volume gain. Speech is independent of dialogue/music gain.
  Generation changes flush the old occupant. Recipient mute/receive changes
  stop already queued speech; connection cleanup clears mute/moderation state.
- **Controls:** both retained Voice and Classic Voice Config expose consent,
  Send/Receive, levels, echo and local-only testing. Localized status reports
  off, ready, recording, input unavailable or session unavailable. Leaving the
  test tab/menu stops testing; revoking access also clears its activation.
  macOS bundles declare microphone usage, and Android declares optional
  microphone hardware plus its recording permission. These declarations are
  implementation coverage, not physical OS-permission qualification.

`tools/tests/voice_chat_runtime.py` launches real hidden clients and either a
listen host or dedicated server on `mp/q4dm1`. It sends a synthetic 400 Hz tone
through the actual codec, net channel, policy and OpenAL playback without
opening microphones. A bounded loopback UDP proxy introduces loss, reorder and
250 ms delay. Both final reports pass all 20 checks:

- `.tmp/audio/final3-voice-listen/result.json`
- `.tmp/audio/final3-voice-dedicated/result.json`

Checks cover both directions, recipient mute/unmute, receive-off, gain 0.125
at master 0.5/receive 0.25, pitch 1 during time scale 0.5, server moderation,
sound restart, packet loss/reorder, late rejection, bandwidth recovery,
reconnect/slot reuse, no spectator leakage, bounded playback and clean exits.
At 2,000 B/s, current voice is dropped while the dedicated server continues
15–25 snapshots/s in steady measurement intervals. Rendered 400 Hz amplitudes
in the dedicated WAVs reach 0.100 (host) and 0.039 (peer), above the harness's
0.02 floor. The proxy reports 74 dropped packets and at most 51 pending
datagrams in its 256-entry bound. Both reports name the final client, dedicated
server and MP module listed above, and harness SHA-256
`ed55dd97f254c26d6f0c7520672e1d8743dba42a400e74e05cca5e55ad9ac950`.

The harness drives each hidden client through a console script it rewrites
between phases. The final listen and dedicated runs passed at the first
attempt, but on the previous build, on this busy shared machine, nine attempts
were needed. Of those, two lost a client when the engine's `exec` of a script failed while Windows was
replacing it (`couldn't exec voice-wait-*.cfg`); an earlier development run
hit the same race once. Two began the reconnect phase during the restarted
match's map restart, which the harness now prevents by waiting for the server
and both talking clients to report a running match after their own restart.
One exceeded the default 360-second budget while a client needed 25 s to load
the map (the final runs pass `--timeout 900`), and one starved the clients of
CPU until too few synthetic frames were captured. The harness also still writes
its report when a child cannot be stopped. These are harness and host-load
limitations, not voice defects; rerun a failed attempt before reading it as a
product failure.

`tools/ui/capture_voice_settings.py` then exercises the real settings adapter,
SDL callbacks and local OpenAL playback using **SDL's explicitly selected
dummy driver**, which supplies silence and never opens a physical microphone.
The vendored SDL Meson build now includes that opt-in driver; SDL never selects
it as a physical-device fallback. The first test captures 59 frames and plays
58, stops on tab exit, then verifies consent revocation. No VOICE_SEND message is
emitted despite echo being enabled. The four English engine-render-target
screenshots were inspected: off, ready, active test and Classic Voice Config.
Reports on the final binaries: `.tmp/audio/final3-voice-ui-english/result.json`
and `.tmp/audio/final3-voice-ui-turkish/result.json` each pass all eleven checks.
The English and Turkish captures were inspected again. In English the retained
"Local microphone test" label wraps onto two lines inside its row, and the
Classic page draws its two new rows without the row plate the older rows use;
both are cosmetic and remain UI-01 polish.

Native `VoicePacketTest` exhaustively tests mu-law error bounds, achieves
37.74 dB fixture SNR and checks malformed packets, wrapping sequence/clock,
idle-delayed backlog, a simulated 24-hour ±1000 ppm clock drift with jitter,
loss and PTT gaps, a rejected backlog that must not train the clock,
burst/flood limits, capture overflow, mailbox freshness and whole-envelope
rejection. `voice_capture_lifecycle.py` compiles production
capture code against fault-injected SDL interfaces; `voice_transport.py`
compiles the production net-channel admission gate. Native retained-settings
tests cover consent, disabled testing, actual peak binding and test shutdown
on tab changes. Focused API, networking, team-policy, macOS packaging and
settings regression checks pass.

These tests qualify Windows process-to-process playback and controlled capture
lifecycle. Physical microphones, default-device migration, permissions/hotplug
on target OSes, real multi-hour sessions (drift is covered only in simulation)
and WAN intelligibility remain AUD-05 qualification work. Narrowband quality
and silence for lost frames are explicit tradeoffs, not an Opus-quality or
voice-processing claim.

## Installed transcript inventory

`tools/tests/audio_asset_inventory.py` reads installed PK4/loose declarations,
with later names overriding earlier ones. It records asset names, hashes,
localization keys and coverage counts, never extracted transcripts. `--sizes`
adds header-based allocation estimates, not a decoder validation.

| Content | Speech shader candidates | Same-name lip-sync/text mappings | Unmapped | Unmapped with a description |
| --- | ---: | ---: | ---: | ---: |
| Retail | 3,672 | 2,268 | 1,404 | 1,298 |
| Awakening | 1,584 | 1,512 | 72 | 8 |

Reports: `.tmp/audio/stock-inventory.json` and
`.tmp/audio/awakening-inventory.json`. Each game directory is inventoried
separately; the expansion report is not a merged runtime filesystem inventory.
Some missing candidates are multiplayer announcer sounds. A description is not
automatically a subtitle; a key existing in any language table does not establish
every locale's coverage. These candidates are broader than the runtime's verified
single-variant/full-path mapping policy below; they are not coverage claims.

## Dialogue subtitles

The shared SP/MP overlay derives cues from actual sound channels and their
integrated sample clock. It uses `lipSync.text` only: a same-name shader mapping
is valid for its single selected entry; otherwise the full sample path without
its extension supplies the mapping. Descriptions, ambiguous random variants,
basename guesses and unresolved localization keys are excluded. Text remains in
installed assets; no extracted transcript or invented alias ships in this repo.
The normal language dictionary supplies English fallback when a selected locale
has no matching text. Quiet exact-key lookup avoids per-frame missing-key spam.

At most the two newest eligible utterances display. Authored volume, fades,
private/anti-private audience and distance still constrain cues; user master and
dialogue mute and hardware voice allocation do not remove captions. The selected
leadin/loop sample owns its text. Stops, replacement, completion and missing text
clear the cue. Speaker labels require a matching live entity/emitter and its
`npc_name`, with a localized radio label where applicable.

The Lowpixel overlay defaults to 17 dp, white text, green speaker labels and a
0.60 black backing. Long text wraps using actual font advances and pages in
proportion to utterance duration; the data does not establish word-aligned times.
It draws through the common HUD path in either UI mode, with persistent localized
size/background/speaker controls in Settings > Audio. Two simultaneous utterances
get one line each. The four controls have labels in all twelve menu languages.

`audio_subtitles.py` executes production cue selection and bounded pagination,
including missing/ambiguous mappings, loops, UTF-8, privacy, distance and overlap.
The `openq4-ui-subtitles` native runtime test validates the document and one/two
line geometry at 1280×720, 1024×768, 3440×1440 and 720×1280. Both pass.

`audio_subtitles_runtime.py` uses original tone/text fixtures plus an existing
installed dialogue shader in actual loaded campaign maps. No microphone or host
input is used. The following reports pass all 16 checks, including selected-text
English fallback, mute, pause, half speed, restart, overlap, interruption,
save/restore, installed transcript selection, archived controls and real Audio
page scrolling. Reports retain executable, module, content and harness hashes.
(Correction, 24 October 2026: both maps open in a cinematic, which refuses a save, so
these runs' save/restore phase showed captions kept through a refused save, not a
save and load. The harness now saves and loads for real; see
[real saves](#real-saves-in-the-subtitle-and-dialogue-harnesses).)

- `.tmp/audio/final3-subtitles-wide/result.json`: stock, English, 1280×720.
- `.tmp/audio/final3-subtitles-portrait/result.json`: Awakening
  `game/m01_stranarus_trench1`, Turkish, 720×1280.
- `.tmp/audio/final3-subtitles-classic/result.json`: stock, Classic UI, 1024×768.

All three use the final client and harness SHA-256
`fe9a2070f85d66a6339cfb72fb2f8f094cfc945e26c33d8cca5cc40f542176ef`.

Engine-render-target captures were inspected for captions and the new controls.
The final portrait caption is inside the cinematic picture, but Awakening's
existing narrow-layout title card can overlap enlarged captions. The scene also
shows letterbox bands with world pixels outside them. These are retained UI-02/
UI-04 qualification gaps, not a full portrait presentation pass. Arbitrary custom
cinematic HUDs, complete authored scene/locale coverage and physical headset
stereo placement remain unqualified. Missing transcripts stay an explicit AUD-07
gap; this does not provide effect captions or speech recognition for voice chat.

## Long-session voice evidence, 22 October 2026

The interrupted Claude session left a completed dedicated-server/two-client
run in worktree `1388`, `.tmp/soak/soak-2h-final/`. Its `result.json` reports
7,168.9 seconds (1:59:29), not the requested 7,200 seconds. The original harness
accepted a duration one interval short and included shutdown time. Preserve
that report unchanged: its `duration_met: true` is not a strict two-hour pass.
`voice_chat_soak.py` now takes a sample at or beyond the requested endpoint,
stops its duration clock before shutdown, and requires the full live duration.
The user cancelled the repeat long run; no replacement two-hour run is claimed.

All 120 measured intervals pass. Synthetic 400 Hz PCM traverses the real codec,
net channel, server policy and OpenAL wave backend in `mp/q4dm1`; microphone
opens and host input are zero. The server admits 19,390 frames, rejects 1,261
and refuses zero sends. Host/peer playback totals are 8,261/8,983 frames.
Delayed-backlog probes reject 1,168 frames at the peer. Maximum admitted added
delay is 120 ms and the maximum observed playback queue is three frames.
Admission baseline shifts span -48 to +6 ms. A reconnect after interval 60
changes the peer generation from 356 to 359, clears mute/moderation and resumes
admission in 16.8 seconds. No console step runs twice.

Private-memory growth from the warmed first quarter to the final quarter is
-18.2 MiB for the server, -34.3 MiB for the host and +4.1 MiB for the peer
(peer comparison excludes reconnect warmup). The recorded rendered tone remains
present late in the session. Machine CPU load ranges from 27.8% to 100%; free
commit falls from an initial 14.62 GiB to a minimum 1.32 GiB. This is useful
loaded-host stability evidence, not an uncontended performance measurement.
All processes share one machine clock; the native simulated 24-hour drift test
remains the independent-clock drift evidence. Physical capture, WAN peers,
endpoint changes and target-OS breadth remain unqualified.

The saved run binds these SHA-256 identities:

| Artifact | SHA-256 |
| --- | --- |
| Client | `6c3a4dfeaaf5e8e98473d739e1eddc7598de724b2e828d1184bce6fadca724ff` |
| Dedicated server | `14744541d29cc00b6584c89b3fd5080a693b801f3e47f9a34465a6d8d8f4e0d8` |
| MP module | `ea2079fa810e93602757018111c8cfaee012f4447d49e4f43245a21f42e19112` |
| Original harness | `d665d5b8cf3071ce17eba89b1a2f0ea4ef97edda11a7245a4b530a2cf6b89e6f` |

The recovered implementation is in `bf6b50800` (per-sender admission telemetry)
and `c9ca2e315` (session and mixed-release harnesses). Do not transfer the old
runtime result to a newly built executable solely because these commits match.

Worktree `2993` verifies the corrected endpoint with a new 220-second request:
`.tmp/outstanding/voice-short/result.json` records 232.4 seconds, twelve passing
intervals and all thirteen checks passing, including reconnect, delayed backlog,
exactly-once steps and rendered tone. The server admits 2,454 frames and rejects
132; clients play 973/1,043 frames. Maximum admitted added delay is 101 ms and
the queue peaks at four frames. This short run qualifies the harness correction
and current staged runtime, not another long session. The rebuilt native suite
passes 276/276, and transport, capture lifecycle and capability-gating checks pass.

## Mixed-release voice check, 22 October 2026

The saved `.tmp/soak/mixed2/result.json` in worktree `1388` covers published
0.13.0 against the voice-enabled candidate, in both server/client directions,
with `si_pure` both 1 and 0. All fourteen harness checks pass. Pure connections
are explicitly refused for missing packages; non-pure connections are refused
for declaration-checksum mismatch. No unknown unreliable voice messages appear.

No mixed-release peer joins and the talker counters stay at zero. These results
establish explicit refusal for this release pair, not voice interoperability or
runtime capability negotiation with an admitted old peer. The existing
`ui_voiceProtocol`/`si_voiceProtocol` gates and appended message identities are
covered separately by `voice_protocol_gating_contract.py`; production transport
and capture-lifecycle tests pass. The player guide recommends matching releases
and content without asserting that every different release pair is incompatible.

## Output recovery, layout changes and memory pressure, 23 October 2026

Implementation: [audio modernization](audio-modernization.md#output-changes-and-memory-pressure-october-23).
Windows x64, MSVC debugoptimized, built from a detached worktree of `2f6c628a3`
plus these changes and staged into that worktree's own `.install`, so peers'
uncommitted work in the shared checkout is not in the binaries. Client SHA-256
`e20c20f499df2bfdb01215dd34e27e51d24aebae5b06436e59b105b2339379b8`; each
report records the modules, paks and harness hashes. The runs use the installed
Steam 1.4.2 PK4s, generated tone fixtures in an isolated save path and OpenAL
Soft's wave writer. No physical device, microphone or host input is used.

### Output loss and paused resume

`tools/tests/audio_output_recovery_runtime.py` plays a 60 s dialogue fixture
(radio channel of a saved fixture entity) and an effect fixture on stereo
output in `game/airdefense1`, then runs `s_testDeviceInterruption 1500`: every
source stops, as OpenAL Soft does on disconnect, and no voice is allocated
until the outage ends. During the outage `listSoundVoices` reports
`connected=0` and no audible voice. Afterwards both fixtures play again; the
dialogue's own OpenAL position moved 2,635 ms while the scene clock moved
2,633 ms and stands at 5,152 ms, not at its start. Before this change the
static, non-looping voices ended at the outage. The rendered WAV shows the
440 Hz line, a silent gap of more than a second, then the line again. The
dialogue was then paused and the output interrupted again. After the resume it
continues from its paused position (5,173 ms, then 5,845 ms after 700 ms of
scene time) instead of replaying from its first sample.

The same run then plays two 60 s Ogg fixtures on separate channels: one whose
final page lost its end-of-stream flag, which stb_vorbis still measures, and
one whose last two pages carry no position, which the engine counts
("counted; no length metadata" in the log). Both stream through bounded queues
and both play again after a third, 700 ms outage. The run's only stream
underruns are the queues stopped inside the three outages.

### Live layout transitions

The surround run starts the dialogue on 5.1 output, then changes
`s_numberOfSpeakers` 6→2→6 and `s_openALHRTF` 0→2→0 while it plays. Each change
logs "device reset in place" and an output change; no sound restart or sample
reload occurs after the map has loaded. `listSoundVoices` reports the
dialogue routed to the centre on 5.1 and unrouted on stereo and HRTF, playing
in every phase, and the unrouted phases report its position following the scene
clock (3.65 s, then 6.89 s, 3,237 ms apart over 3,233 ms of scene time). OpenAL
reports HRTF enabled in the HRTF phase. The wave writer restarts its file at
each reset, so the final recording is the last 5.1 phase. Its 440 Hz
centre-to-surround ratio, compared within the windows where the line is loud,
is 2.11 (2.27 is the value for `s_centerFractionVO` 0.75). The profile's own
startup layout change, which used to trigger a second full sound restart, now
resets in place too.

### Low-memory trim

`s_trimAudioMemory 0` in the same stereo run freed 1,128 of the level's samples
(96.3 MB) and kept the 95 that channels referenced, including the playing
fixtures and both streams. The blaster's `fire01`, loaded by the level and idle, reads
`(PURGED)` after the trim, plays again on `testSound` and reads loaded. The
operating-system path (`SDL_EVENT_LOW_MEMORY` from the lifecycle watch or the
event loop) is covered by the Android lifecycle native test on the Android and
desktop branches: one notice trims once, on the main thread. Delivery by a real
Android or iOS device is not qualified.

### Streams without length metadata

`tools/tests/audio_streaming.py` derives four fixtures from original Ogg files:
the last two pages without a position, and the file followed by 70,000 other
bytes, each at 22.05 kHz mono and 48 kHz stereo. stb_vorbis reports no length
for any of them. The production reader counts each one, finds its last
positioned page and then decodes and seeks (start, first sample, middle, last
sample, quarter) with exactly the PCM of a complete decode, for two
independent decoders at once. The production queue test then runs leadin/loop,
offset, routing and mixed static/streamed transitions over two of them. A
stream with a length keeps it, and an uncounted one refuses to seek.

### Envelope pass during level loads

The airdefense1 load streams 38 tracks (21 cinematic sounds, five music tracks
and stingers, nine ambience loops and two weapon sounds) and now builds their
envelopes on job workers ("envelope on a job worker" in the log). Offline, a
complete stb_vorbis decode of those 38 files (18.5 million frames) takes
475–510 ms on this machine, the main-thread work that pass previously did
inside the load. This is an offline measurement of the moved work, not an
end-to-end load-time benchmark on an idle machine.

### Native and harness coverage

The new `openq4-sound-output-recovery` (201 checks) and
`openq4-sound-memory-trim` (1,536 checks) tests cover every combination of the
two decisions. The stream-queue test checks that a static voice stopped by a lost
endpoint recovers while one stopped with its output present ends. The
decode-budget harness checks that an envelope job joins without an upload or a
reload and that a damaged one reloads. `sound_settings.py` (35,807 checks, all
35 mutants rejected) and `sound_recovery.py` (42,348 cumulative checks, all 11
mutants rejected), which extract the hardware update and render bodies, still
pass. The Android lifecycle, OpenAL pause, stream budget, payload restore, memory,
subtitle, screen-shake, reverb, macOS provider and Steam Deck harnesses pass.
The full native Meson suite passes 278 of 279 tests on the rebased build. The
remaining failure, the match-control projection contract, looks for the engine
checkout at `../openQ4`, which a worktree under `.tmp` does not have; pointed at
the worktree with `OPENQ4_ENGINE_REPO`, it passes.

On the final build, the stock regression runs that exercise the voice update
path also pass. `audio_dialogue.py --compressed --speakers 6 --decode-memory 16
--audio-memory 192` passes 28 of 28 checks with no stream underrun and a centre
ratio of 1.74. `audio_memory_runtime.py` passes 13 of 13 checks.

## Dialogue routing audit

`tools/tests/audio_dialogue_coverage.py` reads installed declarations in
filesystem order, finds every reference to a speech shader (a same-name
lip-sync declaration, a radio line or a `sound/vo` sample) in entity
definitions, maps, scripts, BSE effects and GUIs, and classifies it with
`SoundIsDialogue` compiled from the engine header. It writes identifiers, kinds
and counts, never transcripts.

| Content | Speech shaders | By sample path | Shader/source-kind pairs | Not dialogue |
| --- | ---: | ---: | ---: | ---: |
| Retail `q4base` | 3,672 | 3,672 | 3,495 | 0 |
| `q4base` + Awakening `q4xbase` | 5,153 | 5,153 | 4,912 | 0 |

Retail pairs: 2,780 NPC lip-sync keys, 146 other definitions, 484 map entities
and 85 scripts; with Awakening layered, 3,830, 146, 533 and 403. No BSE effect
or GUI names a speech shader, and 343
retail and 811 combined speech shaders are not named by any scanned source (some
are reached through computed names). Because every speech sample lives under
`sound/vo`, the Dialogue Volume control reaches it whichever path plays it.
This is a static routing result; audible scene coverage is still open.

### Transcript languages

The asset inventory now reports which installed language tables carry each
mapped line. The Steam install's 2,268 mapped lines exist only in English
tables (its French, Italian and Spanish tables hold three map-pack strings and
no lip-sync text). Awakening's 1,512 mapped lines are literal English in their
declarations. Other menu languages therefore show English captions; the player
guide states this.

## Authored scenes, centred reverb and portrait captions, 23 October 2026

Implementation: [audio modernization](audio-modernization.md#authored-scenes-centred-reverb-and-portrait-captions-october-23).
Windows x64, MSVC debugoptimized, built from a detached worktree of `8cb58aa4a`
plus this round's changes and staged into that worktree's own `.install`. Client
SHA-256 `347e6e15b9f15e48a49d2c176a61cef1bfd54c464b9763c4efd0f2389e68a50d`;
each report records the modules, paks and harness hashes. Runs use the installed
Steam 1.4.2 PK4s (Awakening from its separate content root), isolated save paths
and OpenAL Soft's wave writer. No physical device, microphone or host input is
used, and no report contains transcript text.

### Centred sounds in mod reverbs

`tools/tests/audio_mod_reverb.py` now also renders a centre-routed source, its
noise in FC of a 5.1 buffer through direct channels, on a 5.1 loopback context.
With room rolloff 1, OpenAL alone leaves its reverb send at the near-distance level
at two and four times the minimum distance (ratio 1.0000), where a mono source
falls to 0.5000 and 0.2500. With the engine's send gain the centre-routed source
falls to 0.5000 and 0.2500 too, and to 0.1667 at room rolloff 2.5 and three times
the minimum, matching the mono source to four places. The same holds with EAX
reverb, and a stock preset (no room rolloff) keeps the centre send unchanged.
All 20 checks pass, including the production source pins; the native reverb-core
test checks the gain function's values and edge cases.

### Streams on recycled voices

The first gameplay run of this round found that a streamed track started on a
voice whose source had played before never played. The stereo output run's second
simulated outage left all four streams it was playing (two fixtures and two
airdefense1 ambience loops) starting and stopping 39 times in 1.5 s. Its first
outage had recovered only because those streams restarted on voices that had never
played. An OpenAL loopback probe confirms the cause: a source that has played and
been flushed reports `AL_STOPPED` with every newly queued buffer already processed,
while a rewound one reports `AL_INITIAL` with none processed and then plays. The
production queue test now models that state (a voice is played, flushed, and a
stream queued on it must be playable). It fails without the rewind and passes with
it.

### Output recovery and Dialogue Volume

The rerun of `tools/tests/audio_output_recovery_runtime.py` on this build passes all
19 stereo and 12 surround checks. At Dialogue Volume 0.25 the line keeps that gain
through a 600 ms outage and restarts on the new output serial, and both stream
fixtures play again on the new serial. In the surround run Dialogue Volume 0.5
holds through both in-place resets that follow it: HRTF on, then HRTF off, where
the line moves back to the centre speaker (gain 1.0 before, then 0.5 and 0.5). The dialogue moved 2,645 ms over 2,633 ms of scene time across
the first outage. The paused line moved 704 ms over 699 ms after its resume. The
trim freed 1,128 samples and kept 95, and the only stream underruns (12) are the
four streams' queues inside the three outages.

### Portrait captions

`tools/tests/audio_subtitles_runtime.py` (Awakening `game/m01_stranarus_trench1`,
Turkish menus with English transcripts, 720×1280) passes all 16 checks on this
build. Its captures were inspected. Default and enlarged two-line captions hang
below the intro's lower letterbox band, clear of the "Planet Stroggos" title text
the cinematic draws in its picture; before this change, enlarged captions covered
it. The harness now waits up to 600 s by default, because the map took 110 s to
load on the busy machine in a first attempt. The native placement test checks the
rule at 9:16, 10:16 and 3:4 for four text sizes, and the unchanged placement at
4:3, widescreen, in gameplay and for an unusable pitch.

### Speech in authored scenes

`tools/tests/audio_scene_audit.py` plays the first 30 s of each campaign map after it
loads, chaining maps in one hidden client, with cinematics, AI and scripts running and
god mode so the player survives. There is no other input, so scripted lines that wait
for the player to move are not reached. `s_logSpeech` reports each start that may be
speech. Every 3 s the harness lists the audible voices and subtitle cues, mutes
Dialogue Volume for 150 ms, lists the voices again and restores it. After each opening
it records the audio residency and the voices playing on bounded stream queues.

All 29 stock maps run in three chains and all 13 Awakening maps in one, and every
check passes in each:

| Measure | Stock (29 maps) | Awakening (13 maps) |
| --- | ---: | ---: |
| Starts that may be speech | 256 | 633 |
| Dialogue starts (distinct shaders) | 93 (62) | 55 (45) |
| Declared or flagged speech not following Dialogue Volume | 0 | 0 |
| Probe pairs; muted probes with a dialogue voice audible | 261; 0 | 117; 0 |
| Dialogue starts with a transcript; without one | 26; 67 | 39; 16 |
| Probes catching a transcribed line: cue shown; cue missing with a free slot | 24; 0 | 34; 0 |
| Maps with streams playing after the opening | 27 | 12 |
| Reserved audio after an opening: smallest, median, largest | 65.0, 124.6, 193.8 MiB | 71.7, 151.1, 242.1 MiB |
| Managed-payload peak (admission budget 256 MiB); refusals | 255.4 MiB; 0 | 255.7 MiB; 0 |

The largest openings are `mcc_landing`, `building_b` and `mcc_1`, and Awakening's
`m09_valkaryne`. The managed-payload peak includes level changes, where the admission
reclaims the previous level's samples as it approaches the budget. The only
radio-channel starts that are not speech are two stock effect shaders. The
voice-over-flagged starts that are not speech are radio static and interface
clicks, 506 of them an NPC typing at a console in `m06_mcc_invasion`. None of them
carries a lipSync declaration. The Strogg sentry's chatter counts as dialogue: it
plays through the actor lipSync path, which flags its sound as voice-over.

The first Awakening attempt did not complete. The machine was saturated that hour:
the first map took 340 s to load where the rerun took 18 s, and OpenAL
Soft's wave writer reported failed writes to its output file. The engine reopened
the device after each one, as it would for a lost endpoint. The chain stalled on the
second map waiting for a first gameplay frame and reached its 45-minute limit. The
rerun on an idle machine, identical except for its output folder, passed in 552 s
with no device loss.

These are openings sampled without player input, not complete playthroughs. They
show that the authored speech they reach routes as dialogue, mutes with Dialogue
Volume and captions only from transcripts, but not that every line in both
campaigns is audible or captioned.

## Linux, voice output loss and long sessions, 24 October 2026

### Linux x64

The audio round's harnesses ran on Linux for the first time. That covers output
recovery, live layout changes, the trim, both stream fixtures, subtitles and the
scene audit. Ubuntu 24.04 ran under WSL2 (kernel 6.6.87.2), with the client built
there from the same commit (debugoptimized, without native tests, the multiplayer
module, Vulkan, OpenXR or GLES, and with a minimal `pak1`). It ran hidden under Xvfb
on Mesa llvmpipe (OpenGL 4.5 compatibility). The system OpenAL Soft 1.23.1, not the
1.25.1 the Windows package bundles, rendered through its wave writer. Retail paks were
copied into RAM first. The two Ogg stream fixtures came from the Windows encoder
(`--write-fixtures`), since that host has no ffmpeg; the fixtures are deterministic.
Client SHA-256 `78fcaf93f69b6a636228f1c29ac414de4ae686bcdcb02c370dac768649cd7744`.

| Harness | Checks | Notes |
| --- | ---: | --- |
| `audio_output_recovery_runtime.py`, stereo | 19 of 19 | dialogue moved 2,636 ms over 2,597 ms of scene time after an outage; the paused line 707 ms over 719 ms; trim freed 1,127 samples and kept 96 |
| `audio_output_recovery_runtime.py`, surround | 12 of 12 | in-place layout and HRTF resets, centre re-routing, Dialogue Volume held; 5.1 centre ratio 2.10 |
| `audio_subtitles_runtime.py`, stock 1280×720 English | 16 of 16 | |
| `audio_scene_audit.py`, `airdefense1`, `hangar2`, `medlabs`, `dispersal` | 10 of 10 | 10 dialogue starts, streams playing in all four, residency 107.7–150.6 MiB |

Building it found a Linux-only break: the dedicated server links the OpenAL sound
code against `src/sys/stub/stub_openal.cpp`, which lacked the `alSourceRewind` that
the recycled-voice fix added on 23 October. Windows links the real library and never
showed it. The stub now provides the call, and the Linux dedicated server links.
These are automated Linux runs on one virtual machine with software rendering, not
physical Linux audio devices or distributions other than Ubuntu.

### Voice chat across output loss

Windows x64, MSVC debugoptimized, built from `35a1e919c` plus this round's changes;
client SHA-256 `de29bfd2fa9b48d94cc2d14da76a356d449957dc9097b6e88d732c4f25e3fcbc`.
`tools/ui/capture_voice_settings.py` now simulates an output loss during the local
microphone test, with SDL's dummy input and an OpenAL wave writer. All 13 checks
pass. Before the outage, 58 frames had played and none were dropped. Halfway through
the 1,000 ms outage, 58 had still played and 26 were dropped. After it, playback
continued: 105 played in all, while capture never stopped. Neither the dummy input,
which delivers frames in bursts, nor any source state is the signal: the harness
reads the frame counters. The first run of the new checks also required a source
in the playing state, which this bursty input rarely shows even without an outage;
that criterion was dropped. Physical capture and playback devices remain
unqualified.

### Long sessions

`audio_scene_audit.py --repeat 3` played `airdefense1`, `hangar2`, `medlabs`,
`dispersal`, `storage1` and `core1` three times each in one client, 18 openings in
671 s. All 11 checks pass. Reserved audio after each opening is flat across the
passes: at most 83 KiB more on the second pass than the first, and identical on the
third. Voices in use and stream reservations do not grow. The managed-payload peak
is 205.7 MiB. A first attempt labelled the passes with `#`, which the console's `echo`
splits into separate words, so no opening could be told apart. The labels are now
`passN/<map>`.

## Scripted scenes, process memory and the largest captions, 24 October 2026

Windows x64, MSVC debugoptimized, built from `87444682f` plus this round's changes;
client SHA-256 `1cc1399e446bd25449f4101e609185ed2b918520fe19402d6f6084d496045daf`.
Implementation: [audio modernization](audio-modernization.md#scripted-scenes-process-memory-and-the-largest-captions-october-24).

### Scripted scenes

`audio_scene_audit.py --walk` played every campaign map's first 9 s and then fired
its scripted speech scenes in authored order, 4 s apart, with a probe after each and
15 s of probes at the end: the 29 stock maps in three chains (19, 23 and 12 minutes)
and the 13 Awakening maps in one. Every check passes in each chain, including the
three new ones: every planned trigger ran, no scene changed the level, and the scenes
started speech the openings did not.

| Measure | Stock (29 maps) | Awakening (13 maps) |
| --- | ---: | ---: |
| Scene triggers fired (absent when fired) | 541 (37) | 148 (1) |
| Dialogue starts, of them after the opening | 900, 855 | 426, 408 |
| Distinct dialogue lines started | 711 | 364 |
| Of them with a transcript | 373 | 355 |
| Lines not started in any opening | 681 | 348 |
| Probes at full and muted Dialogue Volume | 715 | 226 |
| Dialogue heard while muted; declared speech off the control | 0; 0 | 0; 0 |
| Cues without a transcript; transcribed lines missing a cue with a free slot | 0; 0 | 0; 0 |

The stock map scripts and map files name 1,470 distinct `vo_` lines, and the walk
started 695 of them, 47%. The rest belong to scenes whose speakers had not spawned,
had already gone or were never reached in this order, or to branches a trigger does
not take. Sixteen further lines came from AI barks and definitions. Awakening's scripts
and maps name 1,808 `vo_` strings, retired scripts left in its folder included, and the
walk started 312 of them, 17%; its other 52 lines are AI barks. An absent trigger
is one the map declares but the level no longer held when the walk reached it:
`storage1` accounts for 29, entities an earlier scene had already removed. The
opening sweep of 23 October counted 148 dialogue starts in all 42 openings; the walk
starts 1,326 and reaches 1,050 distinct lines.

The scenes run out of their gameplay order, so this is coverage of the authored
dialogue routing and captions, not a playthrough: lines a scene plays only after the
player moves, fights or waits, and the speech of cinematics played by entities this
selection does not reach, stay unchecked. Few warnings follow from the order; an
`airdefense1` smoke run logged five non-precached lipSync loads and one scripted move
that could not start.

### Process memory

`audio_scene_audit.py --silent-baseline` played the first 20 s of six stock maps with
sound and then again with `s_noSound 1`, where no sample loads, in one client each.
`listSounds` reports the process's private memory (the Windows commit charge) after
each opening. The difference between the runs is what audio adds; what remains after
the managed payloads is the provider's mixer, effects and device buffers, sample
metadata and the sound system's own structures. The pair ran twice, the second time
on the build with the restore fix, and all 11 checks pass in both.

| Map | Audio adds (first; second) | Managed payloads | Beyond the payloads (first; second) |
| --- | ---: | ---: | ---: |
| `airdefense1` | 185.4; 145.8 MiB | 138.6 MiB | 46.8; 7.2 MiB |
| `hangar2` | 161.1; 121.7 MiB | 107.7 MiB | 53.3; 14.0 MiB |
| `medlabs` | 183.0; 165.4 MiB | 138.5 MiB | 44.5; 27.0 MiB |
| `dispersal` | 165.0; 116.2 MiB | 119.3 MiB | 45.7; −3.1 MiB |
| `storage1` | 207.0; 170.6 MiB | 147.0 MiB | 59.9; 23.6 MiB |
| `core1` | 182.4; 130.6 MiB | 124.4 MiB | 58.0; 6.3 MiB |

The runs with sound repeated within 27 MiB, but the silent runs came out 11–76 MiB
higher the second time, so the difference carries the variation of the rest of a
3.3–4.0 GiB process. What the table supports is a bound: the managed payloads account
for most of what audio costs the process, and the remainder outside the 256 MiB budget
is at most about 60 MiB in these openings. This method does not resolve it more
finely. The working set (resident) difference was only 7–30 MiB, because Windows trims
a hidden window's working set; the commit charge is the measure. Earlier evidence
called the admission's payload peak a "process peak"; those passages now name it the
managed-payload peak.

On Linux the same comparison is less clean. The private figure there is anonymous
resident memory plus swap (`/proc/self/status`). For `airdefense1` and `hangar2` it
rose by 95.0 and 156.2 MiB with sound, against managed payloads of 138.6 and
107.7 MiB. The first is below its own payloads, so memory outside audio varied
between those two runs by more than audio's overhead; the GL driver's mappings under
WSL are one likely source. The second leaves 48.5 MiB beyond the payloads, within the
Windows bound. All 11 checks pass. Device and mobile figures remain open.

### The largest captions

`audio_subtitles_runtime.py` now also captures a lone caption at the largest text,
subtitle and interface text scales both at 2 (`largest_text_caption`). At 720×1280 in
Awakening's `m01_stranarus_trench1` intro, only one line of that size fits below the
cinematic artwork. Before this change the caption returned into the picture, over the
"Planet Stroggos" title; it now pages one line at a time below the artwork, clear of
the title (captures inspected). The same capture showed the radio label "Telsiz:"
running under the first word, because a speaker name's indent stopped at 30% of the
line; the words now start after the whole name, and a name wider than half the line
is left out, as it is at this size. Enlarged two-line captions at 1.6 are unchanged.
The native placement test now covers one-line fits at 9:16, 10:16, 3:4 and 0.9, the
largest text at 9:16 and 3:4, and the speaker indent's bounds.

### Real saves in the subtitle and dialogue harnesses

Until this round the subtitle runs saved inside the opening cinematic of
`airdefense1` and `m01_stranarus_trench1`, and a cinematic refuses a save ("Can't save
during a cinematic"). Their restore phase therefore showed captions kept through a
refused save, not a save and load. The Awakening dialogue run did the same; the stock
one skipped cinematics and did load. The subtitle harness now takes its cinematic
captures first, loads the map again with cinematics skipped, starts both speakers,
saves and loads. The dialogue harness skips cinematics on both campaigns. Both check
`saved_and_loaded`: the save's `Saved` line followed by a level load.

Real saves showed a defect. A line restored from a save resumed as far into itself as
the rest of the load had taken: a caption saved 267 ms in read 3,842 ms after a
3,309 ms load and a 500 ms wait, and in the portrait run 333 ms read 2,180 ms after a
1,309 ms load. Pausing the world as the saved sound state is read changed nothing on
its own (316 ms read 1,722 ms after a 1,334 ms load), because the sound clock advances
once per mixed frame and no frame is mixed during a load. With the pause and unpause
bringing the clock up to date, the stock, portrait and Classic runs read 318, 370 and
317 ms before the save and 1,281, 810 and 1,199 ms after loads of 1,306, 1,673 and
1,091 ms. That is 0.4–1.0 s of play after the harness's 500 ms wait, not the load
time. `restore_resumes_in_place` first allowed at most 1.1 s, which the fastest load seen,
1.3 s, would exceed; on a busier machine on 25 October a correct restore took 1.3 s to
settle after a 3.5 s load, and a bound on the caption alone cannot tell a fast load from a
slow settle; it now checks the game sound world's clock instead (see
[saves on Linux](#saves-on-linux)). Each subtitle run passes all 19 checks; both dialogue runs pass
all 27, with Dialogue Volume's 0.6 holding across a real restore. These runs use the
client with the restore fix, SHA-256
`d488e79abaf95606ea764766fa90a62a019ea4181273392092c0c4b2d42d12d2`; the scene walks
and the first memory pair ran on the build before it, which differs only in that fix
and in how a sound world pauses.

### Linux voice chat

Ubuntu 24.04 under WSL2 (kernel 6.6.87.2) ran the client, dedicated server and both
game modules, built there from the same tree with the multiplayer module (debugoptimized,
without native tests, Vulkan, OpenXR or GLES, and with a minimal `pak1`). The clients
ran hidden under Xvfb with SDL's dummy audio driver, and OpenAL Soft 1.23.1 rendered
through its wave writer. Client SHA-256
`8a0176edef7ffd52a5ca7e7939c780aa6e4893c97b3fa1b399ecc2625d589d92`, dedicated server
`f9773b574faf760fd4af416f34161e7cb316715f3f986ee1d4996ccbb0c54d2f`.

`voice_chat_runtime.py --size 480x270` passes all 20 checks against a listen server
with three client processes, and all 20 against the dedicated server with three
clients. They cover both directions of rendered voice, mute and unmute, receive off,
volume, real-time pitch, server moderation, restart, loss and reorder through the
impairment proxy, late frames, congestion (voice dropped, snapshots kept, recovery
after it), reconnect identity, no spectator leak, bounded playback and a clean exit;
no microphone is opened. In the two-way phase each sender captured 83–85 frames, and
the recipients played 60–76 of them.

The first two attempts rendered with Mesa's software rasterizer (llvmpipe) and failed
5 and then 7 checks. The clients reached about 3 and then 9 frames a second. The
synthetic voice source makes one frame per client frame once frames are more than
60 ms apart, and the 80 ms voice rings need a client faster than that, so those runs
measured the rasterizer. The passing runs use WSL's GPU paravirtualization through
Mesa's D3D12 driver (OpenGL on the Intel Iris Xe). Physical capture and playback
devices, other distributions and separate machines remain open.

### Native and source checks

The `openq4-arena-sound`, `openq4-sound-reverb-core`, `openq4-sound-dialogue`,
`openq4-sound-output-recovery`, `openq4-sound-memory-trim`, `openq4-voice-packet`,
`openq4-ui-sound-recovery` and `openq4-ui-subtitles` native tests pass on the final
build. `arena_sound_pause.py` compiles the real pause and unpause, now with the
clock refresh, and passes 1,000 pause/resume cycles with a frozen clock.
`audio_subtitles.py` (both native subtitle tests), `demo_playback.py`,
`savegame_corruption_contract.py` and the other sources these changes touch pass.

## Two speakers at the largest text, the OS low-memory notice and voice clock drift, 25 October 2026

Windows x64, MSVC debugoptimized, built from `4d12fb9c8` plus this round's changes. The
final Windows runs used client SHA-256
`2dce2ce7511d743a4cdce947212f01fc007d3f214bb5fef69c4f0809ea66e614`; the stock and Classic
subtitle runs used the build before the Linux start fix (`bc8cb67f…`), and the landed
build differs only in a restore error message. Linux x64 ran under WSL2 as before, on
Mesa's D3D12 driver; final client `de87df33…`, dedicated server `23dfcbc4…`.
Implementation: [audio modernization](audio-modernization.md#two-speakers-at-the-largest-text-the-os-low-memory-notice-and-voice-clock-drift-october-25).

### Two speakers at the largest text

`audio_subtitles_runtime.py` now also captures two speakers at the largest text in
Awakening's portrait intro (`largest_text_two_speakers`). The newer caption sits on its
own line above the cinematic artwork and the older one below it, both clear of the
picture and its "Planet Stroggos" title (captures inspected); before, the pair returned
into the picture. The run passes all 20 checks on the final build, as do the stock and
Classic runs (20 each). The native subtitle document test adds the split layout at
720×1280 (5,959 checks), and the placement test checks that the room above matches the
room below.

### The operating system's low-memory notice

`audio_output_recovery_runtime.py` now trims through `testLowMemoryNotice`, which pushes
`SDL_EVENT_LOW_MEMORY` through SDL's queue and event watchers, and checks that the event
reached the engine and trimmed exactly once (`trim_follows_os_low_memory_event`). On
Windows the notice freed 1,127 idle samples (96.7 MB) and kept the 96 in use; on Linux
1,128 and 95. Wiring it up showed that each notice had trimmed twice: the lifecycle
watcher's pending pass answered it at the top of the event pump and the queued event
trimmed again. Stereo passes all 20 checks and surround all 12 on both systems. On Windows
two of four runs each missed one check while other work loaded the machine: one paused
line restarted 670 ms late with a build running, and one HRTF re-route was still pending
at its 1.6 s probe with three Linux clients running. Both passed in the other runs and in a
final rerun on the quiet machine.

### Voice clock drift between real peers

`voice_chat_runtime.py` runs the peer's voice clock 0.1% slow and then 0.1% fast
(`s_voiceTestClockRate`), through 10 s of speech each, against the host. On Linux, with
a listen server, all 22 checks pass: the host admitted all 388 frames from the slow sender
while its baseline rose 8 ms, and all 407 from the fast one while it fell 9 ms. Through the
dedicated server, all 22 pass on the final Linux build: 436 frames each way with the
baseline rising 9 ms and falling 7 ms, and nothing rejected; the listen server repeats at
+9 and −10 ms over 410 and 411 frames. The added delay above the baseline peaked at
47–104 ms, under the 120 ms limit, which is scheduling jitter on this host rather than
drift.
The checks require at least 200 frames, so a slow client's lower frame rate does not
count as a drift failure; the first dedicated run delivered 285 against an earlier limit
of 300. This is drift simulated on one machine; independent machines remain open.

### Saves on Linux

Linux saves load. A first conclusion that they did not, recorded on 25 October in
`137bd8d4b`, was wrong: the failures came from the test tree's build number. The build
number is the Git commit count, and the exported tree first had one commit, so it built
as build 1, below the savegame floor of 640, and refused even its own saves. Padded to
701 commits, it wrote saves whose joint modifiers it then read with the shorter layout
of builds before 1582, misreading the player's animator ("`'player1' saved joint count
0 does not match model 'model_player_marine' joint count 65`", or a garbage count). Given
main's own count, 1,831, the same Linux build saves and loads `airdefense1` 3 s and 15 s
after the map is live. Stable builds already refuse to build without full history. The
restore error now names the entity and model, which is how the player was found.

With that build, the Linux dialogue run passes all 24 checks, its real save and load
included, with Dialogue Volume's 0.6 holding across it. The Linux subtitle run passes all
20 once `restore_resumes_in_place` compares clocks: it now reads `s_soundWorlds` beside
the cues and requires the game sound world's clock to have been held through the load
(245 ms while the wall clock ran on), with the caption advancing no further than the
world clock. A bound on the caption alone could not tell a fast load from a slow settle:
the earlier check failed this correct restore by 77 ms. The unfixed build was not rerun
with the new check; with nothing holding its clock, the gap is one or two stale frames.
A failed load reloads the level, which the dialogue harness had counted as a load; both
harnesses now also reject the load's own failure warnings.

### Arena pause on Linux

`arena_sound_runtime.py` passes on Linux for a won and a lost Arena match, with OpenAL
Soft's null output and engine commands only: the game sound world's clock held at
64,785 ms through a 3.6 s pause while the menu's world ran on, resumed from there, did
the same through a second pause, and the result ledgers were recorded. Resume took
31–42 ms. Whether the pause is audible on Linux remains a listening check.

### A longer tail in the scene walk

The walk now keeps probing for 90 s after each map's last scene instead of 15 s, so a
conversation whose lines wait on one another can finish. Every check passes in all four
chains (89 minutes stock, 34 Awakening):

| Measure | Stock, 15 s → 90 s | Awakening, 15 s → 90 s |
| --- | ---: | ---: |
| Distinct dialogue lines started | 711 → 750 | 364 → 455 |
| Of them with a transcript | 373 → 399 | 355 → 444 |
| Dialogue starts | 900 → 1,209 | 426 → 557 |
| Probes at full and muted Dialogue Volume | 715 → 1,411 | 226 → 538 |
| Dialogue heard while muted; declared speech off the control; cues without a transcript | 0; 0; 0 | 0; 0; 0 |

Across both campaigns the walk now starts 1,179 distinct lines, 842 of them transcribed.
The longer tail helped where conversations are long (`airdefense1` 25 → 37 lines,
Awakening overall +25%) and little elsewhere: most lines still unstarted wait on the player
moving, fighting or reaching a place, which only a playthrough can provide.

### A Linux start crash on main

The Linux client and dedicated server built from main crashed before `main`. The global
multiview demo, built with `AsyncNetwork.cpp`'s statics, cleared itself through
`mvd_seeking.SetBool`, a cvar from another file that GCC's link order had not yet
constructed. Windows hid it. The constructor now resets its members without the cvar,
`Clear()` still publishes it, and `mvd_seek_debris_contract.py` rejects a constructor path
that touches it (six controls).

### Native and source checks, 25 October

The subtitle document test (5,959 checks, now with the split layout), the paging test,
`openq4-arena-sound` and the other sound native tests pass. `arena_sound_pause.py`,
`voice_capture_lifecycle.py` (with stubs for the test cvar), `mvd_seek_debris_contract.py`
(six controls) and all 34 source tests that read the SDL backend pass.

## Progression-audit recovery, 26 October 2026

The unfinished `--progress` experiment from the previous session completed its
four chains, but three failed. Only stock chain B passed every check. Chain A
changed from `recomp` to `putra`; chain C changed maps inside `tram1b`, `process2`
and `putra`. The audit had excluded exit trigger chains but a teleport could
still touch an overlapping exit or start an indirect exit sequence. Its raw
2,625 dialogue starts are therefore not accepted as complete campaign coverage.
The original reports and engine logs are retained under
`.tmp/audio-voice-continuation/initial/`, including their failed checks and
runtime hashes; they have not been rewritten as passes.

The other failures labelled `grunt/rush1`, `grunt/rush2` and
`network_guardian/alert` as dialogue whenever any channel had used that sample
as dialogue earlier in the map. A sample is not a routing identity. The new
`s_dialogueVoices` diagnostic reports each channel's current sample,
production dialogue classification and OpenAL source. Probes join the audible
sources to those current channels; missing attribution fails the check rather
than silently counting an unknown voice as an effect. The diagnostic prints
identifiers and transcript status, never transcript text. It retains allocated
voices through the final frame before the mixer retires them.

The progression planner now rejects malformed/nonfinite origins and triggers
that cannot react to the player, and removes `target_endlevel` entities only in
the disposable test session. This also blocks overlapping exit volumes.
Attempted teleports and missing entities remain separate counts: this does not
establish that every trigger activated or reproduce normal gameplay order.

Eleven harness regression tests cover shared samples, reused source IDs, real mute
leaks, missing attribution, probe boundaries, route ordering, exit suppression
streams that stop before the final residency snapshot, and entity names beginning
with a digit (which must be quoted for the engine console).
The native subtitle harness also executes the production diagnostic, covering
music exclusion, leadin/loop changes, sample reuse, transcript privacy and
hardware-voice retirement. Client/dedicated builds and staging pass on Windows.

An initial diagnostic probe timed out before finishing; it remains failed
evidence. A complete two-map rerun (`mcc_landing`, `network2`) then passed the
dialogue checks: 110 full/muted probe pairs, no unattributed source and no mute
leak. Both `grunt_rush` variants were effects, and `network_guardian_alert`
appeared with both dialogue and effect routing on different channels. That
rerun still failed its streaming gate because `network2` had stopped its last
stream before the final snapshot. Stream observations are now taken throughout
each scene, still requiring a playing stream on a later map's recycled voices.
The failed report is retained rather than reinterpreted as a complete pass.

The next six-map stock run passed every audio and map-transition check, including
streams observed on later maps, but missed one planned step: the console split
`2wavfinal_berserker_trig` into `2` and `wavfinal_berserker_trig` in both `echo` and
`teleport`. Entity arguments and log markers are now quoted. The six-map report
remains failed on traversal completeness; the focused `recomp` rerun records the
correction independently.

The corrected `recomp` run passes all 14 checks (53 planned steps, 58 full/muted
probe pairs). Both Awakening cases, `m02_trianfac` and `m08_cryofac`, pass all 14
checks together (133 steps, 143 probe pairs). Taking the five complete stock maps
from the six-map run, replacing only `recomp` with its corrected rerun, and adding
those two Awakening maps gives this explicitly limited follow-up:

| Measure | Eight affected maps |
| --- | ---: |
| Attempted teleports | 466 |
| Targets absent when visited | 23 |
| Dialogue starts | 761 |
| Distinct dialogue shaders / with transcripts | 168 / 114 |
| Full/muted probe pairs | 506 |
| Mute leaks / unattributed sources / unexpected map changes | 0 / 0 / 0 |

These per-map results do not turn the failed six-map report into a pass. Reports,
configs, engine logs, build/stage logs, hashes and the per-map selection are kept
under `.tmp/audio-voice-continuation/`; `summary.json` states the selection and
`evidence-manifest.json` identifies the retained copies. The runtime is the prior
session's `009f5815e` checkout with this diagnostic added (Windows x64,
debugoptimized, OpenAL Soft wave output, hidden windowed SP). The final executable
SHA-256 is `41f31c039570eac10f8070b074e96a6a3edf5df254109a2bb9ddacca43f52b7e`.

The planner inventories 1,362 stock and 605 Awakening candidate triggers and 46
level-end targets across all 42 maps; only the eight affected maps were rerun.
Teleportation can telefrag nearby actors or doors and provoke script null-entity
warnings, so these are routing probes, not clean gameplay or an ordered
playthrough. Awakening also retains its existing duplicate material declarations.
No host input or microphone was used. Physical-device, mobile, macOS,
independent-machine voice and VR subtitle gates remain open, as does missing
transcript and full campaign coverage.

## Decode cancellation and loading measurements, 3 November 2026

AUD-03 still contained an implementation gap: `CancelDecode` cancelled the job
list and waited, but a running PCM or stream-envelope decode never checked its
token. The wait therefore scaled with the whole track. Production workers now
check before codec setup and between blocks. An interrupted job retains its
cursor, input and reservation until joined; finalizing a retained sample resumes
it, while freeing it discards the partial data. Codec setup and one block remain
indivisible, so this is not a hard real-time cancellation guarantee.

The low-memory traversal previously finalized every decode, including OpenAL
uploads, before deciding which samples to free. It now cancels disposable samples
without uploading them and leaves active, looping, never-purge and loading
samples protected. Admission reclamation and level finalization also discard
stale jobs before joining retained work.

`audio_decode_budget.py` now compiles the production worker, join, cancel, trim,
reclaim and level-finalization functions. A real portable job worker pauses at a
counted codec boundary; the test cancels it and checks that it stops after one
block, keeps reservations until join and either frees everything once or resumes
to the exact expected PCM/envelope. Pre-start cancellation and header corruption
are separate cases. These checks pass with Windows Clang and Linux GCC under
WSL, including `-Werror`. Separate platform scratch directories prevent a WSL
test from replacing a Windows test executable. An initial Linux harness build
overlapped a test-source edit and failed; that log remains separate from the
completed rerun.

`audio_scene_audit.py` now records complete, consistent map-load and media-phase
reports before each scene. `--decode-mode worker|inline`, `--decode-memory-mb`
and `--audio-memory-mb` pin the configuration. Its sound-disabled baseline also
reports signed load-time differences. A negative difference is retained as
cache/scheduling noise, not converted into a speedup. Sound finalization is the
main-thread join/upload/reverb phase, not all audio work, which can overlap game
initialization. Fourteen harness checks pass, including missing/inconsistent
timers, map boundaries and mismatched baseline maps.

Other completed checks in this round: actual Vorbis PCM/seek/loop/lengthless-stream
tests; aggregate memory, stream budget and payload restore; voice capture consent
and lifecycle, transport and protocol gates; subtitle selection/layout; Arena
pause ownership; retail reverb and screen-shake contracts; 35,807 counted settings
checks, 42,348 cumulative recovery checks and eight distance-harness checks.
OpenAL loopback centre routing and mod room rolloff pass using the main checkout's
existing provider (`c5b7aa0c5237c4d202f798fe7825f59af5bb4b9a1ac1e654e7011908843fbb83`).
No physical microphone or host input was used.

The Windows x64 debugoptimized client, dedicated server, SP/MP modules and OpenGL
renderer build and stage successfully; all nine selected Meson sound, voice,
subtitle, Arena and job-system tests pass. In stock gameplay,
`audio_output_recovery_runtime.py` passes all 32 checks: output interruption,
paused resume, both lengthless-stream fixtures, live stereo/surround/HRTF
transitions, the SDL low-memory notice and replay of a purged sample. The trim
reduces managed residency from 154,939 to 55,497 KiB while keeping 93 referenced
samples. `audio_memory_runtime.py` passes all 13 checks, including continued
active playback under deliberate exhaustion, retry after budget recovery and
two restarts with identical 214,246 KiB reservations and no fallback beeps.
The stock reference inventory again finds all 3,672 speech shaders and all
3,495 scanned references covered, with no routing gaps; this is static coverage,
not an ordered campaign playthrough.

Two passes through `game/airdefense1`, `game/mcc_landing` and `game/network2`
also pass in each decode mode with a 16 MiB pending-decode budget and a 256 MiB
aggregate payload budget: 15 checks with workers and a sound-disabled comparison,
13 with inline decoding. Each mode starts eight dialogue lines, preserves mute
and transcript checks, plays streams on recycled voices and refuses no audio
allocation. Per-map residency grows by at most 8 KiB between passes; stream
reservations stay at 2,448, 2,448 and 9,792 KiB respectively. The managed-payload
peak is 252,703 KiB with workers and 240,025 KiB inline.

| Map | Worker load, passes 1 / 2 (ms) | Inline load, passes 1 / 2 (ms) | Sound disabled, passes 1 / 2 (ms) |
| --- | ---: | ---: | ---: |
| `game/airdefense1` | 7,803 / 5,076 | 8,798 / 5,915 | 4,749 / 4,071 |
| `game/mcc_landing` | 8,959 / 4,652 | 5,776 / 5,576 | 4,137 / 3,535 |
| `game/network2` | 3,512 / 3,168 | 4,578 / 3,006 | 3,332 / 2,499 |

Sound finalization takes 3–37 ms across these runs. Other builds were active on
the host, so these are successful measurements, not a controlled speed comparison
or a device loading budget. The worker sound-on/off private-memory differences
span 142,012–352,844 KiB, with up to 158,373 KiB beyond tracked payloads; silent
process memory itself varies substantially. This run does not establish a new
provider-memory bound. Opening probes do not qualify dialogue reached later
through combat or player movement.

The tested build is `908b2c012` plus this round's two sound-source changes, Windows
x64 `debugoptimized`, with the OpenGL renderer and native tests enabled. Local
evidence is retained under `.tmp/aud-roadmap/`: `evidence-manifest.json` records
source, runtime and report hashes; `output-recovery/result.json`,
`memory-pressure/result.json`, `scenes-worker/result.json` and
`scenes-inline/result.json` retain configurations and results. Their engine logs,
rendered WAV output and captures remain alongside them. Generated test binaries,
fixtures and image caches are disposable and are removed after verification.

This does not close the AUD checkboxes. AUD-01 still requires retail listening;
AUD-02 physical endpoint/lifecycle and target-OS listening; AUD-03 mobile provider
memory, device loading budgets and real low-memory notices; AUD-04 physical
surround; AUD-05 physical capture/hotplug and independent-machine clocks;
AUD-06 full gameplay-order dialogue/device coverage; and AUD-07 missing licensed
transcripts, full gameplay-order coverage and physical stereo-HUD placement.

## Local-test isolation and partial capture age, 4 November 2026

AUD-05 had two further implementation gaps in the production capture path:

- Leaving the local microphone test while push-to-talk was already held reused
  the capture stream. The test drains at most three frames per update from a
  four-frame ring; its last frame and partially assembled frame could therefore
  be sent over the network after the mode change. Switching modes now closes
  the old stream, joins its callback, discards SDL resampler/input state and the
  engine ring/partial frame, and stops the local playback channel before opening
  a fresh stream. A failed reopen cannot release old test data either.
- Incomplete frames had no age check. A later callback could finish a stale
  fragment and stamp it with the current time. The partial frame now records its
  first callback's time and expires after the same 120 ms limit as queued capture.
  Repeated shorter gaps cannot refresh its age; a completed frame retains the
  existing completion timestamp.

Both failures were reproduced before their fixes in `voice_capture_lifecycle.py`.
The expanded production-code harness passes with Windows Clang and Linux GCC,
including queued and partial test audio, both transition directions, failure to
reopen, long and accumulated short callback gaps, unsigned clock wrap and the
exact 120 ms boundary. Distinct PCM values prove the returned frame contains only
fresh audio after a discard. These tests use deterministic SDL interfaces and
never open a microphone.

The Windows x64 debugoptimized client/dedicated build and staging pass. The
transport and protocol-gating harnesses pass, as do the three selected Meson
voice-packet, output-recovery and sound-settings recovery tests.

`voice_chat_runtime.py --dummy-capture` now checks consent, local playback,
closure, reopening, revocation and peer isolation in real multiplayer gameplay.
It explicitly selects SDL's dummy input for every child process; that input
provides silence and cannot fall back to a physical microphone. No keyboard or
mouse input is injected. The native PCM regression proves the mode boundary;
dummy capture qualifies callback, playback-submission and peer-isolation
integration, not microphone quality or physical hotplug.

The first peer run passed the new capture checks but hit the previously recorded
Windows control-script replacement race during reconnect. Its host's polling
chain ended at a failed `exec`; it also missed the bidirectional cadence and
fast-clock thresholds at 960×540 on the shared host. The voice harness now keeps
its polling driver immutable and appends each phase and the next driver call in
order. A failed phase-file read returns to that driver and retries; selecting the
next phase before its commands prevents replay. A deliberate missing-file probe
checks recovery in the actual engine. The first driver attempt exposed filename
tokenization in console variables; generated control filenames now use
underscores, and `vstr`'s append semantics are explicit. That failed startup is
retained too. A subsequent driver attempt exposed reconnect replay; the peer
now selects its idle step before disconnecting and avoids starting a second
driver on the next map. Every phase is checked for exactly one execution.
Subsequent peer runs use 480×270 windows to reduce render contention.

The first complete dedicated run passed 30 of 31 checks. Its fast-clock phase
delivered 475 frames, rejected none and added at most 22 ms latency, but the
receiver baseline shifted only −3 ms against a −5 ms threshold. That baseline
also contains network jitter; its sign alone cannot prove a 10 ms sender drift.
`listVoiceChat` now reports the last encoded frame's source clock and packet
timestamp. The audit checks their elapsed-time difference against the injected
±0.1% rate, allowing 2 ms rounding, and still requires over 200 delivered frames,
at most 2% rejection and at most 120 ms added latency. Receiver baseline movement
remains visible. Native tests verify that both clock observations match actual
packet headers for slow/fast clocks, including 32-bit wrap. Admission and the
wire format are unchanged.

The final listen and dedicated runs each pass all 31 checks, including the
missing-control-file recovery probe, exactly one execution of every phase,
capture consent/local playback/revocation, no test audio sent to peers,
bidirectional voice, mute/moderation, loss/reorder, congestion recovery,
reconnect identity, bounded playback and clean exit. Both sender-clock probes
measure −11 ms slow and +11 ms fast, consistent with their source-clock
intervals within the 2 ms tolerance. Neither drift phase rejects a frame;
maximum added latency is 21 ms on listen and 28 ms on dedicated. Rendered 400 Hz
voice amplitudes range from 0.0349 to 0.0887, above the 0.02 threshold.

The tested build is `39aa329a9` plus this round's `VoiceChat.cpp` changes;
client SHA-256 is `923d7faa5292fae249baee815cc31caab0ad78e749eb7bda10ab263e2df172a0`.
Final reports are `voice-listen-retry/result.json` and
`voice-dedicated/result.json` under the evidence directory, with harness
SHA-256 `06e1c34ba105ec348c39a05cb3fda71d66f35b9d1590509504a6a72a4db16cf9`.
`evidence-manifest.json` records source, runtime and retained report/audio hashes.

Evidence is retained under `.tmp/aud-followup/`, including the two failing
pre-fix capture logs, the passing Windows/Linux logs, build/staging output and
runtime reports. An early staging attempt overlapped the final link and failed
to copy the locked executable; staging was rerun after compilation completed,
before launching any game. Physical capture/hotplug, independent-machine clocks
and OS/device breadth remain open under AUD-05. The other AUD gates retain their
existing limits; neither issue changes the subtitle or dialogue-coverage claims.

## Fights in the progression audit, 6 November 2026

The 26 October progression audit moved the player through every trigger volume
but never fought, so a conversation that a script holds until enemies die never
started. Only eight maps had been rerun after its recovery. This round adds
combat and runs the corrected planner over all 42 campaign maps.

`openq4_killHostiles [all]` (cheat, game library) kills every living, visible
AI on another team than the player that has an enemy. The damage comes from the
player through the ordinary damage path (`damage_suicide`), so death animations,
death targets and the scripts waiting on those deaths run as they do in play.
AI that cannot take damage or are scripted not to die are left alone and counted
as protected; an AI still alive after the hit is counted as a survivor. Friendly
marines share the player's team (0) and are never touched. `audio_scene_audit.py
--walk --progress --fight` runs it halfway through each teleport step and once
before the tail, and reports each map's kills, survivors and protected classes.

### A level could end when the client entity table filled

The first `airdefense2` run ended its level at the 24th step with `ERROR:
rvClientMoveable '-1': cannot load collision model`. Activating the second
trench there starts about 4,000 short-lived tracer effects at once; a census
after each later kill pass found 4,079 `rvClientEffect` entries in the 4,096-slot
client entity table, falling back to about 100 two steps later. A client entity
the full table refuses is queued for removal, but the refusal also skips handing
it its spawn arguments. A refused debris piece therefore spawned with no model,
and `rvClientMoveable::Spawn` raised a fatal error that ended the map. The
Quake 4 1.4.2 SDK has the same code, so retail is exposed to it too.

A refused moveable now returns before reading its spawn arguments, and
`SpawnClientEntityDef` reports the refusal to its caller instead of handing back
the half-spawned object; every caller already handles a failed spawn by dropping
the piece. Both game libraries carry the change. `client_entity_registration.py`
checks the guard in both trees and fails when it is removed. On the fixed build,
`airdefense2` fills the table again (100 and 89 refusals in two runs) and
completes all 40 steps with no error.

### Harness corrections found by the sweep

- `process2` prints `colour swap left` without a newline, so the next probe
  marker began mid-line and two probes merged; its routing check failed although
  every source was attributed. The harness's own markers are now matched
  anywhere on a line.
- The player died in `walker` (step 11) and `m07_race` (step 10), and the game
  then ended the level. A dead player's refused commands are now reported per
  map (`player_dead_at_step`, check `player_survives`), and a map that shuts down
  inside its own scene counts as an unexpected level change. In `m07_race` the
  player was riding a speeder: the `teleport` cheat moved only the driver, who
  died apart from the vehicle. It now moves the vehicle with its driver, and
  `m07_race` then completes all 55 steps. That change does not help everywhere:
  in `m07_race1`, which had completed, the speeder teleported into a pipe was
  destroyed at step 6 and the player died. In `walker` the player was on foot
  (no vehicle teleport ran) and still dies at step 11; the cause is not
  identified. `walker`'s later steps remain uncovered.
- Awakening's race maps fail an objective when a lockdown gate closes before the
  player reaches it. A teleport route cannot keep that pace, and the failed
  mission ended `m07_race1` and `m07_race2` partway. Like level exits,
  `item_objectivefailed` entities are now removed in the disposable session;
  both maps then complete all of their steps.

### Results

Every group's report keeps its own failed checks. Four of the nine groups passed
as run (`stock-02`, `stock-03`, `stock-05`, `awakening-00`); every rerun report
still fails on the one map in it that lost the player. The totals below take
each map from its latest run that completed every step (no dead player, no
early map end, a kill pass per step), and otherwise from its latest run, in the
order sweep groups, then `stock-rerun` and `awakening-race`, then the
vehicle-teleport reruns `vehicle-stock` and `vehicle-awakening`. That takes
`process2`, `m07_race1` and `m07_race2` from the first reruns, `airdefense2`,
`walker` and `m07_race` from the vehicle-teleport reruns, and every other map
from its group.

| Measure | Stock (29 maps) | Awakening (13 maps) |
| --- | ---: | ---: |
| Planned teleport steps | 1,362 | 605 |
| Targets absent when visited | 110 | 35 |
| Hostiles killed / survived / protected | 680 / 24 / 75 | 372 / 11 / 16 |
| Dialogue starts | 973 | 445 |
| Distinct dialogue shaders / with transcripts | 637 / 359 | 356 / 320 |
| Not reached by any earlier retained walk or progression report | 55 (35 transcribed) | 33 (30 transcribed) |
| Distinct dialogue shaders, all retained reports | 954 | 592 |
| Full/muted probe pairs | 1,855 | 826 |
| Mute leaks / cues without a transcript | 0 / 0 | 0 / 0 |
| Maps that lost the player | 1 (`walker`, step 11) | 0 |

The newly reached lines cluster where fights gate the script: `mcc_2` (20),
`tram1` (12), `storage2` (10), `core2` (7), and Awakening's `m04_prison` and
`m08_cryofac` (13 each) and `m05_bio` (6).
The earlier corpus comes from `--walk` runs and eight progression maps, so these
counts combine the effect of the fights with that of running progression on
every map; this round did not run a fightless control. Protected AI are the
scripted-invulnerable harvesters, the gladiator, iron maidens and Makron
phases; survivors are convoy ground vehicles (23 in Convoy, 4 in the Awakening
races), Awakening's pain lord (7 passes) and one Makron phase.

Declared or flagged speech always followed Dialogue Volume, muting it silenced
every dialogue voice while effects continued, and every cue shown had a
transcript. No mute leak, misrouted line or untranscribed cue appeared anywhere.
The overlay showed 330 transcribed lines while they played. Two transcribed lines
were heard with a subtitle slot free and no cue, both in their last moments:
`hub1`'s `vo_1_2_14_52_1` voice had already stopped and was being retired, and
`m04_prison`'s `vo_tak_dekletmstrg` was 4.06 s into its 4.32 s sample. Cues
follow the channel's logical clock, not OpenAL's offset, so a cue ends with the
logical sample while the voice's last fraction of a second still plays; the
probe did not record the logical clock, so that reading of the second case is
inferred from the code, not measured. The audible list counts a voice until the
mixer retires it, so the check reports both; neither is a line left uncaptioned
for its duration.

Reports, console output, engine logs, the census, `final-compare.json` (the
per-map selection and comparison) and `evidence-manifest.json` (executable,
game-library and harness hashes per run) are under `.tmp/aud-fight/` in the
AUD worktree. The stock groups and `awakening-00` ran build `53adceae3f9511de…`,
the later groups and the first reruns the client-entity fix `1eed5d0d49e2ba59…`,
and the vehicle-teleport reruns `5203ee4494a142eb…` (Windows x64,
debugoptimized, OpenAL Soft wave output, hidden windowed SP). A report's
harness hash is read when it is written, so `awakening-00`'s names a harness
edited while it ran. `stock-rerun` predates the shutdown check, which is why
its own report does not list `walker`'s early end; the selected `walker` run has it.

These are still routing probes, not a playthrough: nothing is aimed, dodged or
driven, teleports can telefrag actors and script null-entity warnings remain.
Ordered human play, physical listening and devices stay open.

## Other verification and limitations

### PR #212 merge qualification, 4 November 2026

The audio head's first Commit Validation run failed in unrelated validation
paths. The MVD mutation harness gave its Unix executable the same path as its
header overlay; the Vulkan resize mutation still expected the code before the
render-thread synchronization call. Both harnesses now exercise their intended
mutations. The save-codec fixture reports an unexpected exception as a failed
test instead of leaving Windows CI at an unhandled-exception dialog. The two
largest retained page matrices get 300 seconds under sanitizers, retaining their
120-second ordinary limit and every assertion after both hit the sanitizer limit.

The retained Demos document also varied by eight last-bit scale values between
Python 3.11 and 3.12+, whose floating-point sums differ. Its total tag width now
uses the same tenth-unit precision as each tag before computing the scale.
The canonical generator produces matching output on Windows Python 3.11 and
Linux Python 3.12. MVD's 15 mutation controls and the save codec's eight controls
pass on both platforms; the renderer metadata baseline and all mutations pass
on Windows. Meson reconfiguration passes and preserves the ordinary timeouts.
Local evidence is under `.tmp/aud-merge/`; these focused passes do not substitute
for the fresh GitHub validation run required before merging.

The next Windows x64 run passed its Python validation and 309 native tests,
but the same `pages` and `mp-pages` matrices also reached their 120-second limit
without sanitizers. The 300-second allowance therefore applies to those two
groups on Windows as well. Both complete locally without assertion failures:
45,230,007 checks in 49.1 seconds for `pages`, and 48,003,884 checks in 23.8 seconds
for `mp-pages`. Meson introspection verifies 300 seconds for those groups and
120 seconds for an unaffected group. CI failure excerpts and the measured
commands, executable identity and results are retained with the merge evidence.

- `sound_settings.py --no-mutations`: 35,807 checks pass.
- `sound_recovery.py --no-mutations`: 42,348 cumulative checks pass. These are
  counted API tests, not physical endpoint-loss tests.
- Dialogue classification, settings-menu coverage, audio payload restore,
  screen-shake and retail reverb contract tests pass; client and dedicated
  builds and staging pass.
- At the earlier audio/subtitle checkpoint, the native Meson suite passed
  187 of 187 tests. Its
  imported game regressions caught a voice reset placed above a pinned comment
  in `ClearMap`, fixed before the final runs.
- The audio, voice and subtitle static/native-compile tests, the API pin
  contracts touched by API 53, `build_retained_screens.py --check` and
  `build_subtitles.py --check` pass on Windows.
- The validation script's full Python sweep (`openq4_validate.py push
  --skip-build`, 357 checks) found three test-side gaps, now fixed: unwired
  new tests, a missing subtitle-reset stub in the view-effects harness, and
  the retained gate rejecting the Voice page's deliberate test-off write. The
  three checks and the 84 workflow/contract tests that read the CI files pass
  afterwards. The product-requirements structural audit against
  `cbe967f2b` passes.
- Linux: every changed engine sound, network, menu and language source and the
  touched SP/MP game sources pass `-fsyntax-only` with GCC 13 and Clang 18 in
  WSL Ubuntu, and the production-extraction harnesses (decode, aggregate
  memory, stream budget, payload restore, subtitles, voice transport and
  capture lifecycle) compile with `-Werror` and pass under both. GCC exposed one
  misleading-indentation error in the aggregate-memory harness, now fixed.
  `audio_streaming.py` was not run there (no ffmpeg in that environment), and
  no full Linux, macOS or Android build was made.
- `sound_settings.py` now resolves its own path before recording source hashes,
  fixing a Windows junction/real-path mismatch exposed by this worktree.
- The supplied Awakening scene still reports existing duplicate declarations,
  entities in solid, a missing `engine_righ` effect joint and an unknown
  `walker_trench_unit1_seq_2` script function. These content/gameplay findings
  are outside the current audio repairs and are not counted as clean startup.

Full subtitle coverage, whole-process/mobile memory qualification, physical device
lifecycle and target-OS breadth remain open. Voice and compressed streaming are qualified only
within the automated scope recorded above. The 23 October output recovery, layout and
trim results come from a simulated outage, OpenAL Soft's wave writer and a desktop
console trigger; physical endpoints, real low-memory notices and other operating
systems remain the qualification gates.
