# Audio refinements (unreleased)

> **Detailed change record.** Everything recorded here up to 11 November 2026 shipped in [0.14.1](v0.14.1.md), whose notes are the player-facing summary. Later additions are unreleased development notes. See [release status](../../user/release-status.md).

## Highlights

- Read dialogue subtitles in both Classic and modern UI. Settings > Audio lets
  you enable them, enlarge the text, adjust the backing and show speaker names.
  Captions remain visible with dialogue muted and follow pause and save/load.
- Balance spoken dialogue independently of weapons and music with the new
  Dialogue Volume control in Settings > Audio. The setting is saved and also
  applies to The Awakening.
- Long dialogue and music use smaller playback buffers, reducing audio memory
  in tested stock and Awakening scenes. Playback recovers at the current scene
  time after an audio underrun.
- Streamed music, long dialogue and centre-speaker dialogue keep about a second
  of audio queued, so a brief stall such as a mid-level load no longer cuts
  them out. The audio memory budget is unchanged.
- Mods using distance-dependent room reverb retain the intended wet sound level
  instead of losing it too quickly, and sounds a mod plays from the centre
  speaker now lose reverb with distance like every other sound. Stock room
  reverb keeps its existing mix.
- Surround output now places mono radio and global dialogue toward the centre
  speaker. Nearby NPC voices keep their direction, and stereo/headphone output
  keeps its existing mix.
- Talk to other players with push-to-talk voice chat, receive-volume and mute
  controls. Microphone access starts off; enable it on the match's Voice page
  and bind Voice Chat in Settings > Controls > Other. The local microphone test
  stays private to your machine.
- Unplugging headphones or switching audio devices no longer cuts off the line
  of dialogue that was playing: speech and effects pick up where the scene is
  when audio returns, and a line paused in a menu continues from where it
  stopped instead of starting over. Voice chat from other players picks up
  again too, without a backlog of late speech.
- Loading a saved game resumes the dialogue, music and effects that were playing
  where they were when you saved, in step with the characters' lips. They used
  to skip ahead by however long the level took to load.
- Changing the speaker layout, headphone 3D audio or the output limiter now
  applies instantly without reloading the game's sounds, and dialogue that is
  playing moves to or from the centre speaker at its current point.
- On phones and tablets, a low-memory warning from the system now frees audio
  the current scene is not using; those sounds reload when they next play.
- Music and dialogue files whose length information is missing or damaged now
  stream like any other long track instead of being decoded whole, and level
  loads spend less time preparing long music tracks.

## Upgrade notes

- Subtitles start off and cover lines with usable text in your installed assets.
  Coverage is incomplete in both campaigns; lines without a verified transcript
  stay uncaptioned. In portrait windows, cinematic captions sit just below the
  picture, clear of title cards; at the very largest text sizes they show one
  line at a time there, and a second speaker's caption appears above the
  picture. A speaker name too wide for the line is left out rather
  than overlapping the words. This feature does not transcribe player voice chat
  or caption effects.
- Replace the engine and matching game modules together. Dialogue Volume starts
  at full authored volume; existing saves do not need conversion for this setting.
- Voice requires matching support on the server and clients. It uses narrowband
  speech and drops delayed audio during congestion to preserve gameplay traffic.
  Install matching releases and content for everyone in the match; release
  0.13.0 cannot join this candidate, or host its clients.
  Windows and Linux automated peer tests pass; physical microphone and macOS
  qualification is still pending.
- The managed audio budget defaults to 256 MiB. Under pressure, new sounds may
  be omitted while existing sounds continue; increasing the budget permits
  later playback requests to retry. Advanced decoder and active-stream limits
  remain 64 MiB and 32 MiB within that budget. Leave these defaults unless
  diagnosing memory use; the limit does not include the audio driver's private
  memory or the rest of the game.

## Change log

- Added dialogue-volume labels in all twelve supported menu languages.
- Audio loading now accounts for retained data and temporary copies together;
  large RoQ videos no longer require a full encoded-video copy for sound.
- Added decode backpressure and bounded handling of Ogg files without length
  metadata, with memory accounting for diagnostics.
- Streamed tracks are no longer limited to one hour.
- Subtitles in other menu languages use the English transcripts: the Steam
  release installs them in English only, and The Awakening writes them in English.
- New developer commands: `listSoundVoices` reports each sound's output routing
  and position, `s_trimAudioMemory` runs the low-memory response on demand, and
  `s_testDeviceInterruption` simulates a lost audio device for testing.
  `listSounds` also reports the game's total memory use, so its cost with sound
  on and off can be compared. `testLowMemoryNotice` sends the warning a phone or
  tablet gives when memory runs low, which now frees audio once instead of twice.
  `s_logSpeech 1` lists every line of speech as it starts, with whether Dialogue
  Volume and subtitles reach it.
- `s_dialogueVoices` identifies the current dialogue channels and their output
  gains, so developers can distinguish a muted conversation from an effect
  that reuses the same audio file.
- Lines whose transcript is declared for the voice file, rather than for the
  sound's name, now show their subtitle when a character speaks them.
