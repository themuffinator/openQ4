# Playback-driven audio graphs

7 October 2026. The Remastered radio display now draws decoded sample peaks
at the radio channel's playback cursor. It retains the Marine/Strogg mask,
color, HUD placement and vehicle ownership. Forty 60 Hz samples cover 650 ms;
the square height response keeps dense speech legible. Reduced motion uses a
stationary current-level band. Classic retains its original trace.

The sound query is independent of authored camera-shake envelopes,
`no_flicker`, `s_constantAmplitude`, other logical channels and other emitters.
It reads cached PCM peaks without decoding audio in the drawing path. It is
pre-volume metering. Pause holds the cursor; a new, missing, stopped or finished
sample cannot leave another line's activity on screen. The adapter covers both
full and simplified GUI windows in the on-foot, Strogg and shared vehicle HUDs.

The MP windows accept independent `voice_audio_emitter`/`voice_audio_channel`
and `voice_self_audio_emitter`/`voice_self_audio_channel` state bindings. Until
the voice backend publishes real streams, these displays have a flat core.
`src/game/mp/VoiceComms.cpp` currently discards received voice data and never
captures/transmits microphone data, and `CommonRun` reports no active talker.
This change does **not** restore or qualify live microphone voice chat. The
complete retained talker stack, additive composition and non-Windows/GLES/VR
qualification remain part of the full HUD migration.

The atlas's tracked `atlas/audio-waveforms.js` replaces its synthetic voice
driver. Local file controls provide three independent sources: radio, received
voice (Kaz) and your own transmission. Audio is decoded locally, and the graph
reads the audio element's playback cursor, including pause, seek and ending.
No file is uploaded and no microphone is opened. Rebuild the existing complete
atlas with `tools/ui/update_design_atlas.py`.

Validation uses `tools/tests/audio_graphs.py` against the staged client and
installed assets. It generates independent PCM fixtures under the test
savepath, runs a hidden window with input disabled and OpenAL's null output,
enters campaign gameplay and exercises silence, two amplitudes, pause/resume,
channel isolation, the fake-amplitude override, source replacement, stopping,
completion and a retail dialogue sample. Images come from the engine's
`screenshot` command. Reports and captures are under `.tmp/audio-graphs/`.
`testRadioChatter <shader>|show|probe|stop|pause|resume|distract <shader>` is the
shared SP/MP developer command for reproducing these checks.

`tools/tests/audio_graph_atlas.cjs` runs the real atlas in an independent
headless browser and checks decoding, independent sources, silence, pause,
seeking and the actual SVG graph bindings. It does not control host input.

The staged Windows build passes ten playback/capture checks in each of
`gl-final/report.json` (Marine, 1280x720, Air Defense) and
`vulkan-portrait/report.json` (Strogg, 720x1280, Recomposition), under
`.tmp/audio-graphs/`. Fourteen engine screenshots were reviewed. The atlas
browser check and repeated-updater determinism pass. The register revision
audit passes with no new acceptance claims. Expected on-demand fixture sound
warnings are recorded in the logs; these runs establish no additional game
failure beyond the existing missing live voice backend.
