# Audio volume parity with Quake 4 1.4.2 (22 November 2026)

This records how openQ4's volume settings and gain stages compare with retail Quake 4
1.4.2, what changed on 22 November 2026, and the differences kept on purpose. Distance
falloff, the speaker and radio fractions and the area reverb were qualified earlier: see
[audio-modernization.md](audio-modernization.md) and
[retail-audio-reverb.md](retail-audio-reverb.md).

## Sources

- **Retail CVars** come from the static `idCVar` objects in the 1.4.2 `Quake4.exe`
  (`E:\SteamLibrary\steamapps\common\Quake 4`). Each object holds the name, default,
  description and flags, followed by the minimum and maximum. A minimum of 1 with a
  maximum of -1 is idTech 4's marker for an unbounded value.
- **Retail menus** come from the stock `guis/mainmenu.gui` and `guis/mpmain.gui`.
- **Retail mixing** comes from the reconstructed engine in `Quake4Decompiled-main`
  (`DetermineChannelContribution`, `idSoundSystemLocal::Frame`, `FadeSoundClasses`). That
  is a build from before 1.1. The CVar values above were read from 1.4.2 itself.
- **Content** comes from every stock `.sndshd`, map and map script, and from the
  Awakening's `q4xbase`.

## Retail's volume settings

| Setting | Retail 1.4.2 | openQ4 |
| --- | --- | --- |
| `s_volume` | `0.5`, archived, unbounded. The stock slider runs from 0 to 2 in steps of 0.1 | Same. Both openQ4 menus offer 0 to 2 (the retained page shows 0 to 200%) |
| `s_musicVolume` | `0.5`, 0 to 1. The stock menu's slider is hidden | Same values. openQ4's menus show the slider |
| `s_voiceVolume` | `1.0`, 0 to 1 | Same |
| `s_speakerFraction`, `s_radioChatterFraction` | `0.65`, `0.9` | Same (since 6 October) |
| `s_doorDistanceAdd` | `150`, archived | Same; it is now archived |
| `s_dialogueVolume` | Not present | openQ4 only. Its default of 1 leaves the retail mix unchanged |
| `s_volume_dB` | Not present | Removed: it came from BFG and nothing read it |

## Gain stages

Retail multiplies a channel's volume by its leadin volume or its shader/entity volume,
then `s_volume` (linearly), its fade and its sound-class fade. For a positional sound it
then applies the linear distance falloff and, except for voice-over and the exempt
channels, `s_speakerFraction` or `s_radioChatterFraction`. openQ4 follows the same
order: `idSoundChannel::UpdateVolume` in `snd_emitter.cpp`. Retail's OpenAL path sends
the result as `AL_GAIN`, clamped to 1. Its software mixer clamps each speaker gain to 1
unless the sound is `unclamped`. openQ4 clamps a channel to 1 unless it is `unclamped`,
and gives `unclamped` sounds headroom.

**Music.** Retail applies `s_musicVolume` as the fade on sound class 3, which every
stock score shader sets. When the setting changes, `idSoundSystemLocal::Frame` fades the
class to `ScaleToDb(s_musicVolume)`. openQ4 multiplies the setting into every class-3
channel instead. It also counts a sample under `sound/musical/` as music (all stock
ones are class 3 anyway), and one under `sound/music/`, because the Awakening's 35 score
shaders never set a class.

## What changed

- **Master volume reaches 2 again.** openQ4 had clamped `s_volume` to 0 to 1, and both of
  its menus stopped at 1, so a retail setting above 1 dropped back to full scale and no
  menu could reach the extra 6 dB. The CVar is unbounded again and both menus offer 0 to
  2. The mixer still clamps each channel's gain to its headroom (16x, and 1 for sounds
  that are not `unclamped`).
- **Ambient drones are speakers again.** openQ4 also counted samples under
  `sound/ambience/musical/` as music. In retail, the `atmospheric01` to `atmospheric05`
  drones are ordinary class-0 speakers on 14 stock maps (among them hub1, hub2, tram1,
  core1, dispersal, putra and waste), and they ignore the music volume. openQ4 played
  them 6 dB quieter than retail at the default settings, moved them with the Music slider
  and kept their pitch through slow motion. Now they follow retail.
- **`s_volume_dB` removed.** BFG used it as the master volume. openQ4 replaced that with
  `s_volume` and left this CVar defined but unread. Two test harnesses set it to -12 for a
  quieter physical output, which did nothing. Those harnesses now set `s_volume 0.25`.
- **`s_doorDistanceAdd` archived**, as in retail.

## Differences kept on purpose

- **Map fades of the music class.** core2 fades class 3 out and back in to 0 dB, and tram1
  lowers it by 16 dB and then raises it to +16 dB. Retail's fade replaces the music
  volume, so after core2's fade-in its score plays at full scale whatever the player
  chose. openQ4 multiplies the map's fade into the player's setting, so the setting
  holds. tram1's +16 dB raise hits the per-channel clamp of 1 in both games, so both
  play it the same.
- **Software-mixer panning.** Retail starts on its DirectSound software mixer
  (`s_useOpenAL 0`), which pans with `s_dotbias2`/`s_minVolume2` and sends non-spatialized
  sounds at `s_globalFraction` 0.8. openQ4 renders like retail's OpenAL path, which the
  [rendered distance comparison](audio-modernization.md#rendered-retail-comparison-13-november-2026)
  measures against. A mono sound straight ahead is about 2.6 dB louder than the software
  mixer plays it, and a stereo sound about 1.9 dB louder. On the OpenAL path the two
  games match.
- **Voice chat** is `s_voiceVolume` times `s_volume`, capped at 1. Retail's voice chat
  playback could not be traced in the reconstructed engine, so this is not
  retail-verified.
- **Cushion.** BFG's voice-budget cushion (`s_cushionFade*`) only fades sounds below
  about -20 dB once more than 46 channels compete, which a stock scene rarely reaches.

## Verifying

- `tools/tests/sound_volume_retail_contract.py` pins the CVars, both menus' ranges and
  the music rule.
- `tools/tests/settings_menu_coverage.py` checks the stock-style slider against
  `docs/dev/settings-menu-registry.json`. The native `openq4-ui-retained-screens` test
  checks the retained row's 0 to 200% range.
