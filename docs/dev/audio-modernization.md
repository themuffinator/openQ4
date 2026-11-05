# Audio Modernization (2026-10-06)

This records how openQ4's audio compares with the id Tech 5 and id Tech 6 generation, what
changed in the 2026-10-06 audio pass, the evidence behind each change, and what is
deliberately left for later. The retail parity work that came before it is in
[retail-audio-reverb.md](retail-audio-reverb.md).

The 2026-10-21 follow-up adds dialogue volume, byte-based
Vorbis admission and compressed streaming, the mod reverb reference-distance repair
and bounded multiplayer voice capture/codec/transport/playback with explicit consent. See
[audio and voice evidence](audio-voice-evidence.md) for its measured scope and
remaining roadmap gates; this page's October 6 measurements remain historical.
The [23 October round](#output-changes-and-memory-pressure-october-23) makes playback
survive output loss and layout changes, adds the low-memory response and streams Ogg
files without usable length metadata.

## Where openQ4 starts from

openQ4's sound core is the Doom 3 BFG Edition sound system (`snd_world.cpp`,
`snd_emitter.cpp`, `SoundVoice.cpp`; the files keep the BFG GPL header). BFG carried the id
Tech 5-era design: emitter channels sorted by volume into a fixed voice budget with a
cushion fade (`s_maxEmitterChannels`, `s_cushionFade*`), sound classes and fades, and an
XAudio2 mixer that blended a voice toward every speaker inside its inner radius. Robert
Beckebans' RBDOOM-3-BFG replaced XAudio2 with OpenAL (`src/sound/OpenAL`); openQ4 runs on
OpenAL Soft 1.25 and adds Quake 4's retail behaviour on top: the per-area EFX reverb, EAX
occlusion, portal spatialization and Quake 4's volume rules.

id Tech 6 games moved audio to middleware: DOOM (2016) is listed among Audiokinetic Wwise
titles, and its shipped audio is Wwise-encoded. The comparison below is with the class of
features such a pipeline provides (distance filtering, source spread, voice virtualization,
a protected master bus, streaming), not with any one game's tuning, which is not public.

## Comparison

| Area | id Tech 5 (BFG) | id Tech 6-class middleware | openQ4 before | openQ4 now |
| --- | --- | --- | --- | --- |
| Close sources | Blend to omni inside the inner radius | Spread / focus by distance | Hard point panning; the inner radius was stored but never applied | `AL_SOURCE_RADIUS` from `minDistance` (`s_sourceRadiusScale`) |
| Unpositioned sounds | Equal to every speaker | Non-spatialized | Rendered as a point straight ahead | Full spread around the listener |
| Stereo and music | Straight to left and right | Non-diegetic bus | Virtual speakers, so through HRTF on headphones | Direct channels (`s_directStereo`) |
| Speaker layout and HRTF | XAudio2 device channels | Endpoint layout, binaural option | 5.1 forced everywhere; `s_openALHRTF 1` ignored | Device layout by default; HRTF preference honoured; reopen keeps both |
| Resampling | XAudio2 SRC | High-quality SRC | OpenAL Soft cubic spline | 11th order sinc (`s_resampler`) |
| Master bus | XAudio2 mastering voice | Limiter / HDR | No limiter on float devices | OpenAL Soft limiter (`s_outputLimiter`) |
| Distance filtering | None | Attenuation low-pass | None | Air absorption from the room preset (`s_airAbsorption`) |
| Gain headroom | Unclamped sounds above unity | Bus headroom | Clamped to 1 by `AL_MAX_GAIN` | 16x headroom |
| Device loss | n/a (console first) | Device handling | Static voices stayed stopped after a reopen | Stopped voices restart at their offset |
| Shakes and rumble | Amplitude data | Meter / RTPC | Game query returned 0; amplitude stubbed at 1 | Authored camera envelope with retail strength/range; separate audible rumble envelope |
| Sample residency | Purged per level | Streaming banks | Every map's PCM kept for the session | Doom 3 purge rules restored |
| Sample decode | Offline-encoded, loaded per level | Asynchronous loading | Decoded on the main thread as each shader parsed | Decoded on job workers during the level load (`s_asyncSampleDecode`); streamed blocks decode on job workers during play (`s_asyncStreamDecode`) |
| Per-voice update cost | One matrix per voice | Batched | Filters and sends re-sent by every setter (5-6 times a frame) | Once a frame (`CommitMix`) |

## What changed

### Spatial

- **Source size.** `idSoundChannel::UpdateHardware` passes each sound's `minDistance` as the
  voice's inner radius, which only BFG's unused `CalculateSurround` read. The OpenAL voice now
  sends `minDistance * s_sourceRadiusScale` (default 0.5) as `AL_SOURCE_RADIUS`. OpenAL Soft
  spreads a source over the arc it subtends and over the whole sphere inside it; at half of
  `minDistance` the directional part follows BFG's linear blend to omni to within about 0.12
  up to `minDistance`, and becomes a point source beyond it.
- **Sounds at the listener.** Global, omnidirectional and listener-owned voices sit at the
  origin. With no radius OpenAL Soft renders that as a point straight ahead (front-centre on
  5.1, the frontal HRTF on headphones). Such voices always get a radius, so they surround.
- **Direct stereo.** Multichannel voices set `AL_DIRECT_CHANNELS_SOFT` (with
  `AL_REMIX_UNMATCHED_SOFT` where available). Rendered through OpenAL Soft's wave backend with
  HRTF forced on, the main-menu music measured an L/R correlation of 0.95 and lost about 5 dB
  through virtual speakers, against 0.68 with direct channels, which matches the source.
  Without HRTF the two paths measure the same.

### Device

- **One attribute list.** `openQ4_BuildDeviceAttributes` carries the HRTF preference, the
  output mode and the limiter, and is used by `alcCreateContext`, `alcReopenDeviceSOFT` and the
  checked settings reset. The old HRTF reset ran before the context existed, and context
  creation rebuilt the device without it: `s_openALHRTF 1` still reported HRTF enabled on
  headphones. It now reports "disabled, basic stereo". A hot-plug reopen passed no attributes
  and dropped the layout.
- **Output limiter.** OpenAL Soft's log showed "Output limiter disabled" on a Float32
  shared-mode WASAPI device; it now asks for `ALC_OUTPUT_LIMITER_SOFT` (`s_outputLimiter`).
- **Resampler.** 4,762 of the 5,737 stock sounds are 22.05 kHz, so nearly every voice is
  upsampled about 2.2x. `s_resampler auto` picks the runtime's "11th order Sinc"; `default`
  keeps OpenAL Soft's own (cubic spline in 1.25); `listResamplers` lists them.
- **Units.** `AL_SPEED_OF_SOUND` is set in game units (343.3 / 0.0254). Stock content uses no
  Doppler, but a `useDoppler` sound faster than about 9 m/s would have run away in pitch.
- **Headroom.** `AL_MAX_GAIN` is raised to the voice's 16x ceiling. 57 stock map speakers are
  authored `s_unclamped` at +1 to +12 dB and were flattened to unity.

### Current distance audit, 20 October 2026

The current mixer does not turn Quake 4's authored ranges into metres.
`s_soundDistances [exact shader name]` now reports the last completed mix's
listener/emitter positions, direct and portal distances, authored minimum/maximum,
channel gain and actual OpenAL gain, position, rolloff, reference distance and source
radius. It only reads state; it does not move an emitter or change playback.

`tools/tests/sound_distance_runtime.py` spawns three original retail looping sounds
in a real Hangar 1 session and checks nine distances per sound. It reads and hashes
the effective installed PK4 shader sources, isolates writable data under `.tmp/`,
captures through the engine, and records the tested binary/source identities.
The default OpenAL null output qualifies the mixer/backend values, not audibility.
`--audio-output physical` uses the current device at −12 dB for a separate human
check; `--quadratic` checks the existing optional curve without changing defaults.

The Windows GL runs against client SHA-256
`fbce6a142404a565d6ec9c07bf9cf2ec2ccb472cc23357a1ccf3560b1b33aa6b`
pass all 27 default-linear and all 27 quadratic samples. Evidence is retained in
`.tmp/stage-two/audio-distance-linear-r2/` and
`.tmp/stage-two/audio-distance-quadratic-r1/`, including logs, reports, runner
snapshots and the reviewed 960×540 in-game captures.

| Original shader | Authored range (map units) | Inner radius / OpenAL radius | Near gain, before master |
| --- | --- | --- | --- |
| `weapon_lightninggun_fireloop` | 40–1000 | 40 / 20 | 0.3250000 |
| `weapon_lightninggun_hum` | 80–900 | 80 / 40 | 0.4596194 |
| `ambient_light_hum01` | 40–250 | 40 / 20 | 0.0255921 |

Positions sent to OpenAL retain the source/listener separation in map units.
`AL_ROLLOFF_FACTOR` is 0, so OpenAL does not apply a second distance curve. Since
the October 21 [mod reverb room rolloff](#mod-reverb-room-rolloff-october-21) repair,
`AL_REFERENCE_DISTANCE` carries the authored minimum distance (the table's inner
radius) instead of 1; with zero rolloff it affects only an EFX wet send's room
rolloff, not the dry gain this audit measures. A default-linear rerun on the
combined audio/Stage 2 build (client SHA-256
`1d1bfd2fcb201028c02a317459b4619a8815e2ec8428378340231a48c82e7cc9`) passed all
27 samples and reported that reference distance (40/80/40). The engine applies the authored curve and the
retail speaker fraction of 0.65. Its existing gain convention is `2^(dB/6)`.
`LengthFast` can underestimate Euclidean distance by about 0.17%; the checks compare
that distance with independently measured coordinates and then check the resulting
gain. Channels below the existing 1/1024 gain threshold release their hardware
voice, including these fixtures at their authored maximum.

The first run, `.tmp/stage-two/audio-distance-linear-r1/`, remains a failed harness
calibration record: `setviewpos`/teleport raised the eye by 0.5 units, the first
expectation used a different dB convention, and a stock speaker shared the ambient
shader. The corrected test pins the intended eye position and identifies its
emitter by origin. Eight parser/assessment tests include negative controls for
unit conversion, extra attenuation, changed gain/radius, stopped/missing voices,
ambiguous emitters and incomplete execution.

These are controlled dry-path measurements with portal occlusion, air filtering
and reverb disabled in the isolated profile. They do not replace the existing
reverb/HRTF evidence or qualify different endpoints. No attenuation, source-radius,
HRTF, EFX or reverb defaults changed. The physical distance check is still pending.

### Mixing and timing

- **Recycled channels.** `idBlockAlloc` hands back channels without constructing them, and
  `StartSound` did not reset the fade. After a map script faded a sound to -80 dB and removed
  it (11 stock map scripts, among them hangar1, hub1, tram1, walker and putra), the next sound to get that
  channel played silent. `AllocSoundChannel` now resets the channel.
- **Slow motion.** The weapon wheel runs the game at 0.18x and the voices pitch down with it,
  but channel completion ran on wall-clock time, so a line of dialogue stopped about a third of
  the way in. The channel clock now integrates the voice's whole playback rate, shared with the
  pitch (`SoundChannelPlaybackRate`). Music keeps its tempo through slow motion.
- **Silent dialogue.** Voice-over cannot be muted without losing its lip sync, and a silent
  unmutable channel was skipped by the update, so it kept playing at its last gain: on through
  alt-tab with `s_muteUnfocused 1`, and through a door that had just closed. Such channels now
  update at zero gain.
- **Inferred VO.** The BFG rule that marks `sound/vo/` and `sound/guis/` shaders as voice-over
  also set retail's `SSF_IS_VO`, which retail reserves for lip-synced speech and which skips
  `s_speakerFraction`. Map speakers playing VO were 3.7 dB louder than retail. Only openQ4's own
  `SSF_VO` is inferred now.
- **no_dups.** Retail's fixed channel slots kept a finished sound's sample, so `no_dups` also
  skipped the variant that had just ended. Each emitter now remembers its last eight starts.
- **Cushion fade.** `s_cushionFadeRate` is applied per second of sound time instead of per
  1/60 s update, which ran four times too fast at 240 fps.
- **Global channels at range.** An emitter beyond the `maxDistance` of its positional channels
  returned before updating its global channels too; they play at any distance.
- **One commit per frame.** The OpenAL voice's mix setters only record their value and
  `CommitMix` sends the filters and sends once a frame. It still runs every frame, so the
  sends follow the listener's reverb slot between areas.

### Shakes, rumble and sound-driven lights

`idSoundSystem::CurrentShakeAmplitudeForPosition`, which `idPlayerView::CalculateShake` and
`idEarthQuake` call, previously returned 0. The first restoration reused the material/light
amplitude and mixer falloff, producing excessive shake, particularly in Air Defense. Camera
shake now follows its own retail calculation: the authored 30 Hz `shakeData` envelope uses
600 units per letter instead of the light-flicker scale of 2800, and direct distance is
measured in Quake 4 map units. Shader volume, `shakes` and distance falloff are applied before
the arctangent; channels on one emitter take their maximum, and independent emitters add.
The query uses the supplied position and the current playback clock, independently of the
master volume, hardware voice budget and audio mix. Missing `shakeData` and `no_flicker`
do not invent camera motion. `s_constantAmplitude` holds lights constant and suppresses shake.

The stock `airdefense_cannon_fire` (`shakes 1`, -4 dB) has a nearby peak of about 0.358
degrees, about 0.183 at 2750 map units, and zero at its 5000-unit maximum. Rumble and
sound-driven lights retain their existing envelope, including the measured PCM fallback.
`s_showVoices 1` prints the separate shake and rumble totals. See
[screen-shake regression evidence](screen-shake-regression.md) for the retail verification
and tests.

### Memory and loading

- **Level purge.** `LoadSample` marked every sample never-purge. It now follows Doom 3's sound
  cache: samples first referenced outside a level load stay resident, `BeginLevelLoad` clears
  the level marks without freeing, `EndLevelLoad` frees what the new level did not reference,
  and a purged sample reloads when referenced again. `airdefense1` then `airdefense2` held 233
  MB of PCM and now hold 121 MB after "582 sound samples from earlier levels freed (111.8 MB)".
  `s_restart` reloads only the resident set.
- **Decode on the job workers.** During a level load `LoadSample` reads and opens each Ogg on
  the main thread, which fixes the sample's length, rate and channels at once, and a
  low-priority job decodes the PCM while the rest of the level loads. Whatever reads the PCM
  first waits for it (`FinishDecode` from the upload, `FreeData` and `EnsureCpuPayload`), and
  `EndLevelLoad` uploads the rest. At most 48 decodes are outstanding; beyond that, and with
  `jobs_enable 0`, the main thread decodes as before. A failed decode takes the normal
  synchronous probe, so fallbacks to other variants and the default are unchanged.
  The October 21 follow-up also reserves compressed input, fixed decoder arenas and pending PCM against
  `s_decodeMemoryMB` (64 MiB by default). Admission joins older work before growing
  a reservation. Reservations survive worker completion until join/cancellation;
  oversized individual samples warn and follow normal loader fallback. This is a
  transient Vorbis allocation bound, not a limit on resident OpenAL data. Long
  tracks now take the bounded streaming path described below; tested stock and
  Awakening scenes also pass a deliberately lower 16 MiB decode budget.
  The main thread only reads the file and the page headers: the channels and rate from the
  identification header and the length from the end-of-stream granule
  (`SoundSample_PeekOggInfo`, which matches stb_vorbis on all 5,690 stock files); the job opens
  the stream and checks all three before decoding. In the optimized build the sound share of
  `airdefense1`'s game media precache (`com_showLevelLoadTimes 1`) fell from about 970 ms to
  270 ms of main-thread time, and an earlier cut that still parsed the headers on the main
  thread measured the whole warm load at 12.40 s against 13.10 s (medians of three).
- **Ogg decode.** `LoadOgg` opens the stream, sizes the buffer from its length and decodes
  straight into it. `stb_vorbis_decode_memory` grew a scratch buffer by doubling and copied it
  out: two to three times a sound's PCM at peak and a full copy of every sound. A stream that
  decodes short now warns. Offline, every stock Ogg (5,690 files, 1.23 GB of PCM) decodes
  byte-identical to `stb_vorbis_decode_memory` and slightly faster (10.9 s against 11.1 s on
  one core).
- **Robust loading.** A failed loader attempt frees itself instead of uploading the default
  beep, which a later language variant then found in place of its own audio. Out-of-range
  ADPCM predictors, XMA2 and more-than-stereo PCM are refused at load with a warning; they
  used to end the session (`common->Error`) or play as noise. `s_noSound 1; s_restart;
  s_noSound 0` keeps the restart pending instead of starting voices on emptied samples.

### Compressed streaming (October 21)

Ogg tracks above 512 KiB of decoded PCM now use the engine filesystem directly,
including PK4 members and localized voice fallback. Each playing voice owns a
2 MiB decoder arena, 64 KiB input cache and three 4,096-frame PCM buffers. The
`s_streamMemoryMB` admission limit defaults to 32 MiB and includes conservative
VFS and scratch reservations. Stop, cancellation and sample purge release them.
`s_streamThresholdKB` controls selection on load; zero restores full decoding.

Sample-exact seek supports virtualized sounds, restart and save restore. Queues
preserve leadin/loop transitions and pitch; an underrun discards stale playback
and recreates the voice at the channel's current sound time. A bounded initial
decode builds the existing 60 Hz amplitude envelope without retaining full PCM;
since October 23 it runs on a job worker during level loads, and files without
usable length metadata stream too (see [below](#output-changes-and-memory-pressure-october-23)).
Whole-track `GetNonCacheData` access returns null for streamed samples instead of
defeating the memory limit.

Since 5 November each streamed block decodes on a job worker
(`s_asyncStreamDecode`, default 1). The main thread admits the stream and opens
its file, because the VFS open walks shared search paths and pak handles. The
worker parses the Vorbis headers, checks them against the sample's load, seeks
and decodes one queue buffer. The voice queues that buffer on a later update and
requests the next one at once, so the decoder stays one block ahead. OpenAL
calls stay on the main thread. A voice stopped while its block is decoding
cancels the block and parks the stream until the worker lets go, without
waiting. `s_restart` and shutdown join what is still parked. The first buffer
arrives on the next frame instead of the same one. With no accepting worker the
block decodes inline as before.

The repository's MIT/public-domain stb_vorbis has a small read/seek/tell callback
extension; both checked-in copies match, and Meson pins this bundled variant.
Full and streamed PCM compare exactly in tests, including seek and mixed
static/streamed loop seams. Stock and Awakening gameplay checks cover gain,
pause, slow motion, restart and save/load. See the [current evidence](audio-voice-evidence.md#compressed-streaming)
for memory measurements and remaining device limits. The subsequent
[aggregate admission](audio-voice-evidence.md#aggregate-payload-admission) also
covers CPU/OpenAL payloads and non-Vorbis loading under `s_audioMemoryMB`.

### Mod reverb room rolloff (October 21)

The missing source reference distance, rather than a required global metres
conversion, caused excessive attenuation. The OpenAL source now receives the
authored minimum distance in the same units as its position. Real loopback
rendering preserves stock zero rolloff and dry gain and gives the expected half
wet gain at twice the minimum distance with room rolloff 1. Centre-channel
routing and its physical-device qualification are described below.

### Output changes and memory pressure (October 23)

**Output recovery.** The hardware counts every event that can stop, move or re-lay-out
the sources (`GetOutputSerial`): a reopen, an in-place reset, a centre speaker
appearing or going away, a simulated outage ending. Each voice keeps the serial it
started on and checks it every update (`SoundOutputRecovery.h`). A source stopped
by a lost endpoint, or stopped across a reopen, hands itself back and its channel
restarts it at the scene clock, exactly as after a bounded-queue underrun
(`RecoversAtClock`). Before, only loops and bounded queues came back: a static
non-looping voice that OpenAL Soft stopped on disconnect ended, cutting off the line
of dialogue that was playing. While the endpoint is gone (`ALC_CONNECTED` false)
`AllocateVoice` refuses voices, so channels keep their clocks without churning
sources. A paused voice keeps its serial; `UnPause` does not resume one whose output
changed, because OpenAL Soft replays a paused source that lost its voice from its
first sample. The channel restarts it at the paused scene time instead.

**Layout changes.** `s_numberOfSpeakers`, `s_openALHRTF` and `s_outputLimiter`
changes reset the open device in place with the same attribute list as creation
(`TryResetDevice`, `alcResetDeviceSOFT`); a full restart, which freed and decoded
every sample again, is now only the fallback. The output check also runs every
frame for the centre speaker (`HasCenterChannelOutput`, now from extension flags
cached per context): a voice whose centre routing no longer matches the output,
after a reset or a reopen onto another endpoint, re-routes at its scene clock.

**Test hooks.** `s_testDeviceInterruption [msec]` (cheat) does to the context what a
disconnect does, without hardware: every source stops and no voice is allocated
until the outage ends, which then raises the serial as a reopen would.
`listSoundVoices` prints `SOUND_OUTPUT` (serial, connected, centre, interrupted) and
one `SOUND_VOICE` line per voice in use (state, routing, queue, serial, position).

**Low-memory response.** `S_TrimMemory`, called from SDL's `SDL_EVENT_LOW_MEMORY`
on the main thread (Android and iOS deliver it), frees what a scene can do without
(`SoundMemoryTrim.h`): earlier levels' samples, CPU copies of samples OpenAL also
holds (even ones restored for a reader), and this level's samples no sound has
started for `s_trimIdleSeconds` (30; -1 keeps them). Nothing a channel references
in any world is touched, nor menu/HUD samples or streamed tracks, and nothing during
a level load. A freed sample keeps its shader references and reloads when a sound
next starts it; `StartSound` already retried unloaded samples. Last-played times are
now wall-clock. `s_trimAudioMemory [seconds]` runs the same response on demand and
prints `AUDIO_TRIM`.

**Ogg files without usable length metadata.** The strict header peek needs a final
end-of-stream page that ends the file. When it fails (a truncated file, trailing
bytes, a final page without a position) the stream selection now asks stb_vorbis
itself, over the VFS with the fixed arena, and with no length at all judges by four
times the compressed size, so a long track never decodes whole. A stream with no
length is counted by its envelope pass; `stb_vorbis_openq4_find_last_granule` then
finds the last page that carries a position and `stb_vorbis_openq4_set_length`
gives each voice's decoder that page and the count, so seeking bisects up to it and
decodes forward (sample-exact in `audio_streaming.py`). A stream with no positioned
page at all keeps the bounded full decode. The one-hour limit on streamed tracks is
gone; the bound is the int-sized PCM bookkeeping, about three hours of 48 kHz
stereo.

**Envelope pass off the main thread.** During a level load a streamed track's
compressed bytes go to a job worker, which fills the 60 Hz envelope sized before
submission (`BeginOggStreamEnvelope`); the full pass had cost the main thread a tenth
to a third of a second per music track. Voices can stream the track before the
envelope is complete; until `FinishDecode` joins the job the envelope reads silent.
Outside level loads, with jobs synchronous, or for a stream without length metadata
the pass stays synchronous.

### Authored scenes, centred reverb and portrait captions (October 23)

**Centred sounds in mod reverbs.** A voice routed to the centre speaker plays its
mono sample in FC of a 5.1 buffer through direct channels. OpenAL spatializes no
multichannel buffer, so it applies no distance attenuation to it, and that includes
a reverb's room rolloff on the sends, which a mono voice gets from OpenAL itself. With
a mod preset's `room rolloff factor` above zero, an explicitly centred positional
sound therefore kept its full reverb at any distance. `ApplyWetDryRouting` now gives
such a voice the attenuation OpenAL computes for a mono source
(`SoundReverb_RoomRolloffGain`: the reference distance over the reference plus the
rolloff times the distance beyond it), from the reverb in the slot each send feeds
(`GetReverbSlotRoomRolloff`). Stock presets have no room rolloff, so stock content is
unchanged.

**Speech audit.** `s_logSpeech 1` prints a `SOUND_SPEECH` line for every start that
may be speech: classified dialogue, flagged voice-over, the radio channel, or a sample
or shader a lipSync declares. Each line carries the routing the mixer gives it and the
subtitle transcript status (`text`, `undeclared`, `notext`, `unresolved` or `variant`),
identifiers only. The overlay and the audit share one transcript lookup
(`SoundTranscriptText`), so the status is the one the overlay acts on.
`tools/tests/audio_scene_audit.py` plays the opening of every campaign map in one hidden
client, cinematics included, and probes the audible voices and subtitle cues at full
and muted Dialogue Volume. After each opening it records the map's audio residency.

**Streams on recycled voices.** The voice pool hands out its least recently used
voice, so after the first minute or so of play every new sound reuses a source that
has played. OpenAL keeps such a source `AL_STOPPED` and counts every buffer queued on
a stopped source as processed. A streamed track (music, a long line, a late ambience
loop) started there therefore had its first blocks unqueued as fast as they were
queued, found nothing to play, and was stopped and retried every frame. Bounded
streaming shipped in this cycle, so no release is affected. Earlier checks started
their streams early, on voices that had never played. `FlushSourceBuffers` now
rewinds the source to `AL_INITIAL`, the state a new source starts in.

**Made-up lipSync declarations.** Looking up a lipSync that no content declares (actor
speech and map precache do) makes the declaration manager create an empty implicit
default. The subtitle lookup took that default as the shader's declaration, found no
text and stopped, so a transcript declared for the full sample path never showed.
Implicit declarations are now ignored there and by the speech audit, whose first sweep
had counted one robot's chatter as declared speech for that reason.

**Voice chat across output loss (October 24).** Multiplayer voice plays on its own
OpenAL sources, which a lost endpoint stops like every other. `Play` now drops a
frame while the output is gone (`IsOutputConnected`, counted as a playback drop)
rather than queuing speech that would be late when it returns, and the next frame
afterwards restarts the stopped source. `s_testDeviceInterruption` stops these
sources too (`SoundVoiceChat_OutputInterrupted`), as a real disconnect does.

**Linux dedicated server (October 24).** The dedicated server links the OpenAL sound
code against `src/sys/stub/stub_openal.cpp` on Linux, which lacked the
`alSourceRewind` the recycled-voice fix calls, so that build failed to link. The stub
now provides it.

**Long sessions.** `audio_scene_audit.py --repeat N` plays its map list N times in one
client and checks that an opening played again holds no more audio, voices or stream
reservations than its first pass. A first attempt found that the console's `echo`
splits `#` into separate words, so pass labels are `passN/<map>`.

**Portrait captions.** On screens narrower than 4:3 a cinematic's picture is a short
band, and enlarged captions at its bottom covered the title text Awakening's intro
draws in it. When two lines fit under the 640x480 artwork, captions now hang from just
below it; otherwise, and on wider screens, they keep their place
(`OpenQ4Subtitles::CanvasBottom`).

### Scripted scenes, process memory and the largest captions (October 24)

**Scripted scenes.** `audio_scene_audit.py --walk` continues each map past its
opening. It reads the map and every script from the installed paks, and Awakening's
loose files, and picks the entities whose `call` key reaches a function that makes a
character speak, directly or through the functions it calls: the actor `speak` event,
which `aiSpeak` and its wrappers use, or a `lipsync_` or `vo_` shader name. Every
`func_radiochatter` is picked too. Anything that reaches a `target_endlevel`, through
entity targets, script calls or the entities a function names, is left out. The
picked entities are fired in authored order with the console `trigger` command, with
a probe after each, and a tail of probes lets the last conversations finish. The
scenes start out of their gameplay order, so a line whose speaker has not spawned
yet stays silent; the audit counts what plays. Each map's command file disarms
`g_autoExecAfterMapLoad` as it starts, so a level change that no step should make
cannot run it again; the audit reports any map load inside a scene.

With `--walk --progress` the player is teleported to candidate contact triggers,
nearest first from the player start. This exercises doors, spawns and conversations
that wait for the player. Activation-only counters, non-player triggers, invalid
origins and known exit chains are excluded. The audit also removes each map's
`target_endlevel` entities in the disposable session: an otherwise safe destination
can overlap an exit volume or start a delayed exit script. Installed assets stay
unchanged. Steps count attempted teleports, not confirmed trigger activations;
entities already removed or not yet spawned are reported separately. This is
additional scene coverage, not an ordered playthrough: no route is walked and no
enemy is fought. Teleports can telefrag nearby actors or doors and cause script
warnings; the probe does not certify those scenes as normal gameplay.
Entity names are quoted in commands and log markers so the console keeps names
beginning with digits intact.

Each full/muted probe now pairs `listPlayingSounds` with `s_dialogueVoices`, which
reports current channel/sample classification and its OpenAL source without
transcript text. Classification belongs to the current channel: an AI effect can
reuse a sample that another channel played as dialogue. The audit joins by source
and sample at the probe, fails on missing attribution, and still rejects an
audible dialogue channel while muted. The diagnostic retains channels with an
allocated hardware voice until the mixer releases them, including sounds whose
logical duration just ended. This avoids losing their last audible frame.
`listSoundVoices` runs at each full probe too, so the recycled-voice stream
check does not depend on a track still playing at the map's final memory
snapshot after a scene has deliberately stopped its music.

**Process memory.** `listSounds` now ends with `Process memory: private=… KiB
resident=… KiB` from `Sys_GetProcessMemoryUsage`. On Windows that is the commit
charge and working set (`K32GetProcessMemoryInfo`, looked up at run time so no psapi
import is needed). On Linux and Android it is the anonymous resident memory plus
swap, and `VmRSS`, from `/proc/self/status`. On macOS it is the physical footprint
and resident size (`TASK_VM_INFO`). `audio_scene_audit.py --silent-baseline` replays
the openings with `s_noSound 1`, where no sample loads, and reports per map what audio
adds to the process and how much of that lies beyond the managed payloads: the
provider's mixer, effects and device buffers, and the sound system's own structures.
The harness's earlier `process_peak_kib` was the admission's peak of managed
payloads, not process memory; it is now `payload_peak_kib`.

**Saved games resume sounds in place.** A loaded game restores each sound relative to
the sound clock at the moment its state is read, which is partway through the level
load. The session expects the game sound world to be paused while a saved game loads
and unpauses it when the map change completes, but nothing paused it, so the clock
ran on through the rest of the load. Every restored sound then resumed as far into
itself as that took, 1.4–3.6 s in the subtitle runs, ahead of the lip sync the game
clock drives. A pause could not have held it either: the sound clock advances once per
mixed frame, no frame is mixed during a load, and a pause and an unpause inside one
read the same stale time. Pausing and unpausing a sound world now bring the clock up to
date (`RefreshSoundTime`), and reading a saved sound state pauses the world at that
point (`ReadSoundState`; render demos are unchanged). Finding it took real saves: the
subtitle harness, and the dialogue harness on Awakening, had saved inside an opening
cinematic, which refuses to save, so their restore phases had measured the same
session. Both now check `saved_and_loaded`: the subtitle harness saves on a second
load with cinematics skipped, and the dialogue harness skips them on both campaigns.

**The largest captions.** At the largest text (subtitle and interface text scales both
at 2), only one line fits below a portrait cinematic's artwork at 9:16 or 3:4, so
captions moved back into the picture, over the title text. Where one line fits
(`OpenQ4Subtitles::LinesBelowArtwork`), a lone caption is now paged a line at a time
and stays below the artwork; two overlapping captions still return to the picture.
The same captures showed a speaker name running under the words: its indent was
capped at 30% of the line whatever the name measured. The words now start after the
whole name, which is left out where it would take more than half the line
(`OpenQ4Subtitles::SpeakerIndent`).

### Two speakers at the largest text, the OS low-memory notice and voice clock drift (October 25)

**Two speakers at the largest text.** Where only one line of caption fits below a
portrait cinematic's artwork, two overlapping captions had returned into the picture.
The room above the artwork is the same as below it, so the subtitle document gains a
one-line panel there (`abovePanel`, generated by `tools/ui/build_subtitles.py`): the
newer caption takes it, the older stays below, and both keep clear of the picture
(`OpenQ4Subtitles::CanvasAboveBottom`). The panel stays hidden in every other case.

**The operating system's low-memory notice.** `testLowMemoryNotice` pushes
`SDL_EVENT_LOW_MEMORY` through SDL's queue and event watchers, the way Android and iOS
deliver it, so the response is testable on any platform; the output-recovery harness now
trims through it instead of calling the trim directly. Doing so showed every notice
trimmed twice: the lifecycle watcher flags the event as SDL queues it, the pending pass at
the top of each event pump answers the flag, and the queued event then trimmed again. The
queued event now trims only if the pending pass has not answered it
(`SDL3_TakeLifecyclePendingFlag`), or if no watcher is installed.

**Voice clock drift between real peers.** `s_voiceTestClockRate` (test only, cheat,
0.98–1.02) makes a sender's voice timestamps advance at that rate against real time, as a
machine with a fast or slow clock would send them, continuing from the last stamp with the
fraction carried. `voice_chat_runtime.py` now runs the peer 0.1% slow and then 0.1% fast,
ten times a typical crystal's drift, through 10 s of speech each, and checks that the
host's admission baseline follows the sender up and down without rejecting frames.

### Presets

Every performance preset keeps room reverb on (`s_useEAXReverb 1`); see
`Common_MigrateLegacyAudioDefaults` in `src/framework/Common.cpp` for the one-time step that
restores it on profiles still on the minimum, lowpower or performance preset.

### Decode cancellation and loading measurements (November 3)

PCM and stream-envelope jobs now poll their job cancellation token before setup
and between codec blocks. The worker retains its decoder cursor, compressed input
and partial output until the main thread joins it. `CancelDecode` then discards
them; `FinishDecode` resumes a retained job at that cursor after a global job
cancellation. Neither path releases a reservation while a worker still owns it.
The codec's setup and an individual block remain indivisible; this is bounded
cooperative cancellation, not a hard real-time deadline.

`TrimMemory` determines which samples the scene needs before joining anything.
It cancels disposable jobs through `FreeData`, leaves active/looping and loading
samples alone, and drops already-uploaded CPU copies. It no longer uploads all
pending PCM in response to a low-memory notice. Admission reclamation and
`EndLevelLoad` likewise discard unused earlier-level jobs before finalizing
retained samples.

`audio_scene_audit.py` pins `--decode-mode worker|inline`, `--decode-memory-mb`
and `--audio-memory-mb`, records the existing map-load and media-phase timers,
and adds per-map load-time differences to `--silent-baseline`. Sound finalization
includes the main-thread join/upload/reverb work; decoding also overlaps game
initialization, so this timer is not the total audio cost. Sound-on/off wall-time
differences retain negative values and must be interpreted alongside cache and
host-load variation. Repeat the same map set for warm-load comparisons; do not
promote these desktop timings into a mobile loading-time budget.

### Local microphone test isolation and capture age (November 4)

Changing between local testing and network capture destroys the old SDL stream
before opening the new one. Destruction joins the callback, drops SDL's queued
input/resampler state and clears the engine's capture ring and partial frame.
The local playback channel stops at the same transition. This also holds when
push-to-talk is already pressed on test exit or reopening fails: test audio never
becomes an outgoing frame.

While assembling a frame, `partial.time` records the first callback's time.
If assembly takes more than the queue's 120 ms age limit, the partial frame is
discarded before reading fresh samples. A completed frame retains the existing
completion timestamp. Unsigned elapsed-time arithmetic handles clock wrap;
several individually short callback gaps cannot keep an old partial frame alive.

`voice_chat_runtime.py --dummy-capture` adds consent, local playback, closure,
reopening, revocation and peer-isolation checks to the existing listen/dedicated
voice run. SDL's explicitly selected dummy driver supplies silence, so this
exercises real callbacks and OpenAL submission without opening a physical input.
The production capture harness uses distinguishable PCM to prove that no old
samples cross a mode transition or survive an expired partial frame. Neither
test replaces physical microphone/hotplug or independent-clock qualification.

`listVoiceChat` also reports `VOICE_CLOCK`: the source clock and packet timestamp
of the most recently encoded frame. The peer audit checks the injected ±0.1%
clock rate against those values, within 2 ms of integer rounding, while requiring
continued delivery, at most 2% rejected frames and at most 120 ms added latency.
Receiver baseline movement remains in the report, but its sign is not a clock
test: varying network delay can exceed the 10 ms drift injected over ten seconds.
This diagnostic observes the existing timestamp; it does not alter admission or
the wire protocol.

## Deferred

- **Audible distance units.** Current `DOOM_TO_METERS` and `METERS_TO_DOOM` are both
  `1.0f` in `src/sound/sound.h`; the earlier claim that the mixer necessarily converts
  map units to metres is stale. The current distance audit above establishes the
  dry-path attenuation and source radius on Windows; the source reference distance
  is the authored minimum and matters only to mod room rolloff. Human
  audibility remains pending under AUD-01. Reverb and HRTF retain their separate
  qualifications; camera shake already has its separate game-unit path.
- **Whole-process/mobile audio memory.** A 256 MiB managed-payload budget now
  jointly admits retained CPU/OpenAL data, WAV/ADPCM/RoQ loading, decoder arenas,
  envelopes and conservative queues, and an operating-system low-memory notice
  trims what the scene can do without. Whole-process cost is measured on Windows
  and Linux: in two pairs of runs over six stock openings on Windows, audio adds
  116–207 MiB of private memory, and at most 60 MiB of it lies outside the managed
  payloads. The rest of the process varied by up to 76 MiB between runs, so that
  overhead is bounded, not resolved more finely. A notice delivered by a
  real Android/iOS device, provider memory on those devices and device
  loading-time budgets remain under AUD-03/CORE-03. Lowering the limit preserves
  active allocations while stopping new admissions until usage fits.
- **Surround device qualification.** Mono global/radio dialogue now honours
  `s_centerFractionVO`, and `SSF_CENTER` uses the physical centre on a detected
  surround output through direct 5.1 buffers. Positional NPC speech retains its
  direction; stereo and active HRTF keep the mono path, and playing voices re-route
  when a reset or reopen changes the layout. Automated loopback and stock/Awakening
  gameplay pass, and explicitly centred positional sounds now take a mod reverb's
  room rolloff on their sends (loopback-qualified). Physical devices still need
  qualification; stock content uses no `center` shader or `s_center` speaker.
- **HDR audio, ducking, geometric obstruction.** These change the authored mix or need new
  per-sound data; they are not defaults for stock content.
- **Voice budget.** 48 voices stay (retail mixed 24 channels).

## Verifying

- The native `openq4-sound-output-recovery` and `openq4-sound-memory-trim` tests (every
  output-change and trim decision), `tools/tests/audio_streaming.py` (streams without length
  metadata) and the gameplay run `tools/tests/audio_output_recovery_runtime.py` (output loss,
  paused resume, live layout re-routing, the trim and replay after it).
- `tools/tests/audio_mod_reverb.py` (room rolloff for mono and centre-routed sources on
  OpenAL loopback), `tools/tests/audio_subtitles.py` (transcript status and cinematic
  caption placement) and the gameplay sweep `tools/tests/audio_scene_audit.py` (authored
  speech routing, Dialogue Volume and subtitle cues in every campaign map's opening, and
  its audio residency). Its `--walk` option continues through each map's scripted speech
  scenes, `--repeat N` replays the maps in one client, and `--silent-baseline` measures what
  audio adds to the process against a run with `s_noSound 1`.
- `tools/tests/sound_settings.py` and `tools/tests/sound_recovery.py` (the device-layout and
  limiter paths), `tools/tests/sound_retail_reverb_contract.py`,
  `tools/tests/sound_distance.py` (distance-harness failure controls),
  `tools/tests/sound_screen_shake.py` (camera strength, range and aggregation),
  `tools/tests/macos_openal_provider_policy.py`, `tools/tests/ui_performance_presets.py` and the
  in-engine `performancePresetSelfTest`.
- The log reports `OpenAL voices: source radius on, direct stereo on (remixed), resampler 11th
  order Sinc` and `OpenAL output limiter: on`.
- Audible checks without a device: point `ALSOFT_CONF` at a config with `drivers=wave` and a
  `[wave] file=` path, as in [retail-audio-reverb.md](retail-audio-reverb.md).
