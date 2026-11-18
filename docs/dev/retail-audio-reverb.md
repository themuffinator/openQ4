# Retail Environmental Reverb

openQ4 plays Quake 4's per-area reverb the way the retail game does on its EAX path. Each map's
rooms, corridors and outdoor areas get the reverb the level designers assigned to them, and the
reverb changes as the player moves through portals. This page records the retail behaviour, where
it was verified, and how openQ4 maps it onto OpenAL EFX.

Before this work openQ4 read neither reverb file. It created one EAX reverb effect at OpenAL's
default (Generic) parameters, and no sound reached it: the send was gated by a sound-shader wet
level that defaults to 0 and that no shipped shader sets.

## Sources of truth

- **`Quake4.exe` 1.4.2** (Steam install), base `0x10000000`. Disassembled read-only with
  capstone, because `Quake4Decompiled-main` is a pre-1.1 build with reconstruction errors (see
  below). Addresses below are 1.4.2.
- `Quake4Decompiled-main/src/sound/default/` (`snd_efxfile.cpp`, `snd_reverb.cpp`,
  `snd_system.cpp`, `snd_world.cpp`), for structure only.
- OpenAL Soft 1.25.2 (`al/effects/reverb.cpp`, `al/source.cpp`, `alc/alu.cpp`): its EAX
  emulation is what the retail game sounds like on current Windows systems, because Creative's
  EAX hardware is gone. openQ4 uses the same EAX to EFX arithmetic.
- The shipped data: `pak001.pk4` `efxs/default.efx`; `maps/<map>.reverb` for all 29
  single-player and 21 multiplayer maps in `pak001`/`pak002`, with newer tables for patched
  multiplayer maps in `pak013` and `pak016`. The Awakening drop ships no `.reverb` tables.

## Data files

### `efxs/*.efx`

```
Version 1

reverb "Hallway" {
"environment" 12
"environment size" 1.8000
"room" -1000
...
"flags" 63
}
```

Each block is a full EAX 4 `EAXREVERBPROPERTIES` set: environment, environment size and
diffusion, room / room HF / room LF (mB), decay time and HF/LF ratios, reflections (mB), delay
and pan, reverb (mB), delay and pan, echo and modulation time/depth, air absorption HF (mB), HF
and LF reference, room rolloff factor and flags. `default.efx` holds Creative's 23 EAX
environments, Generic to Underwater, in EAX order.

Loading follows retail `idEFXFile::LoadFile` (`0x10169500`): the lexer runs with
`LEXFL_NOSTRINGCONCAT`; `Version` must be `1`; property names are matched case-sensitively; an
effect that fails to parse is skipped and loading continues (the decompile's "clear and fail" is a
reconstruction error). Retail's lexer errors are fatal; openQ4 adds `LEXFL_NOFATALERRORS`, so a
damaged mod file warns instead of ending the session.

`idSoundSystemLocal::EndLevelLoad( mapName )` loads `efxs/<map name without path>.efx`, then
`efxs/default.efx`, and prints `sound: found <file>` or `sound: missing <map file> and
efxs/default.efx`, as retail does. No stock map ships its own `.efx`.

### `maps/<map>.reverb`

```
reverb
{
	{ 0 Plain }
	{ 1 Mountains }
	...
}
```

Retail `LoadReverbData`: every portal area of the game render world starts on effect 0 (Generic in
`default.efx`); each entry names an effect case-insensitively. A map without a table plays effect
0 everywhere, with no message. Messages match retail: `Loaded reverb file '<file>'`,
`Malformed reverb file header in '<file>'`, `Malformed reverb file '<file>' line <n>`,
`Invalid reverb area number <n> [0,<areas>] in '<file>'` and `Unknown reverb type '<name>' in
'<file>'`.

Five stock tables name areas that their map no longer has, because they were written for an older
compile of it: `hub1` (2 entries), `mcc_landing` (2), `network1` (5), `process2` (22) and `q4ctf6`
(2). Retail warns about each on every load and skips them; openQ4 skips them too, but prints that
one message only with `developer 1`, since players cannot act on it. Every other stock table
matches its map, and every name in them is one of the 23 presets.

## The four reverb slots

Retail EAX 4/5 gives the game four FX slots. Each mixer update (`idSoundSystemLocal::MixLoop`,
`0x101742d0`, with the update inlined) does this:

1. **Gather.** Index 0 is the listener's area. For each portal of that area, in portal order,
   take the squared distance from the listener to the portal winding's center. Keep the three
   nearest, replacing the current farthest entry; a portal must be closer than that entry and at
   a nonzero distance.
2. **Keep pinned slots.** For each slot, any gathered area it already holds is reconfigured and
   removed from the list. 1.4.2 keeps scanning after a match, so an area reached through two
   portals takes the later portal's direction.
3. **Fill free slots.** The remaining areas take the free slots in order.
4. **Primary.** The first slot holding the listener's area becomes the context's primary slot.
   If none does, the primary does not change.

A slot is configured with the area's effect: the effect index from the area table, looked up by
name, else an effect named `default`; with no effect the area gets no slot. The slot's
reflections and late-reverb pans both take the environment direction:

- for a nearby area, toward the portal leading to it, with magnitude `(d + 1) * 0.181 + 0.637`
  (constants at `0x102e6d20`/`0x102e6d24`), between 0.637 for a portal that fills the view and
  about 1 for a distant, narrow one;
- for the listener's own area, toward its nearest gathered portal with magnitude
  `(d + 1) * 0.3185 - 0.637` (`0x102e6d28`), which is zero or negative: the room's reverb pulls
  away from the doorway as the listener approaches it;

where `d` is the dot product of the unit vectors from the listener to the two corners of the
portal winding's bounds. The direction is the unit vector to the winding center in listener space
(x forward, y left, z up), converted to EAX space as `(-y, z, x)`.

`s_muteEAXReverb 1` sets the listener slot's room level to -10000 mB.

Pinning means a slot keeps its area while the area stays near: walking from a hallway into a
hangar moves the primary to the slot that already holds the hangar, so no slot changes its preset
mid-tail.

## Sources

Retail sets two EAX source properties for every **mono** source each mix (`0x10173780`), when EAX
reverb is on and a preset file loaded. Stereo sources take no reverb, as EAX 5 defines for 2D
sources.

- **Active FX slots**: the slot holding the source's own area (`lastValidPortalArea`, or none if
  no slot holds it), and the primary slot.
- **Room** (`EAXSOURCE_ROOM`): -10000 mB on the radio channel (10), -500 mB for voice-over
  (`SSF_IS_VO`), otherwise 0.
- **Occlusion** (`EAXSOURCE_OCCLUSION`): -1500 mB for each blocking (`PS_BLOCK_VIEW |
  PS_BLOCK_AIR`) portal on the path the portal trace resolved, unless the shader has
  `no_occlusion` or `s_useOcclusion` is 0; occlusion LF ratio 0, room ratio 1.5, direct ratio
  1.0. Retail turns occlusion on whenever it finds EAX 4 or 5, so it only exists with EAX reverb.

Every emitter in range tracks its area (retail `Spatialize`), including global sounds and the
player's own; global mono sounds are occluded like any other.

Retail sets `AL_ROLLOFF_FACTOR` 0 on every source (`0x1016e6a9`) and computes distance
attenuation itself, as openQ4 does, so OpenAL applies no distance-based attenuation to either the
direct path or the reverb sends: both follow the source gain.

Before a map has loaded, retail leaves EAX's default routing (mono sources feed the primary slot,
which starts on Generic); after that the per-source state above applies to every sound world, the
menus included. Retail has a single listener, which only the game places, so mono menu sounds
reverberate with the slot the game last left primary. openQ4 does the same: its menu sound world
shares the game render world but never places a listener, so only the game sound world plans the
slots.

## EAX to EFX

openQ4 drives four OpenAL EFX auxiliary effect slots with `AL_EFFECT_EAXREVERB` effects (or
`AL_EFFECT_REVERB` on devices without EAX reverb, which lacks the LF, echo, modulation and pan
controls). The arithmetic is OpenAL Soft's EAX emulation, in `src/sound/SoundReverbCore.h`:

- **Reverb**: density = min(size³ / 16, 1); gains are `10^(mB / 2000)`, with -10000 mB as
  silence; pans, times, ratios and references pass through; `flags & 0x20` sets the decay HF
  limit. This reproduces `efx-presets.h` for all 23 presets, except that `efx-presets.h` sets the
  decay HF limit on Plain where Creative's EAX table, and so Quake 4's file, clears it.
- **Direct path**: low-pass gain `occ * max(Rd * Rlf, Rd + Rlf - 1)` mB and relative HF gain
  `occ * Rd` minus that; with retail's ratios, highs only, -15 dB per portal.
- **Send into the primary slot**: `occ * max(Rr * Rlf, Rr + Rlf - 1) + room` mB, relative HF
  `occ * Rr` minus the occlusion part; -7.5 dB and a further -15 dB in the highs per portal.
- **Send into the source's own area slot**: the room level only.

Each mono voice uses send 0 for the primary slot and send 1 for its own area's slot when that is a
different slot: retail's two active sends. OpenAL Soft provides at least two sends, and four in
its EAX-enabled Windows builds; the log reports the count (`OpenAL EFX reverb send enabled (<n>
auxiliary sends)`). With one send, only the primary is fed; with one slot, that slot follows the
listener alone.

## openQ4 specifics

- **Slot ownership.** Slot 0 is the `auxEffectSlot`/`auxReverbEffect` pair that the checked
  settings path (`SoundSettings.cpp`) creates, verifies and deletes. Slots 1-3 exist while slot 0
  holds its reverb. Turning EFX off through settings first detaches every voice's sends and frees
  slots 1-3 (`ReleaseAreaReverbSlots`), because OpenAL refuses to delete a slot a source still
  feeds. The checked routing path feeds send 0 into the current primary slot.
- **Wet level.** Retail has no `reverb` sound-shader keyword. openQ4 keeps the keyword it
  inherited, as a scale on the send, with a shader default of 1 so every sound reaches the reverb
  as in retail. Channels restored from saves or demos written before this change carry the old
  default 0 and are given their shader's level back.
- **Liquids and the enviro suit** keep openQ4's own muffling, multiplied with the retail
  filters; submerged and cross-surface sounds still take a reduced send.
- **Cvars.** `s_useEAXReverb` (archived, default 1) enables the reverb; `s_useEAXOcclusion`
  (archived, default 1) and `s_muteEAXReverb` (default 0) are the retail cvars, with retail's
  flags. Unlike retail, which forces `s_useEAXOcclusion 1` whenever it finds EAX, openQ4 honours
  0. `s_showReverb 1` prints the slot assignment whenever it changes, and `listReverbs` prints
  the presets, the map's area table and the current slots.
- **Idle slots and pan steps.** A slot no playing voice has fed for longer than its reverb rings
  holds the null effect, which OpenAL Soft skips. The hold is the reflection and late delays,
  1.5 times the slowest band's decay and 0.25 s, from `SoundReverb_TailHoldMs`. Voices call
  `NoteReverbSend` before they attach a send or play, which binds the reverb again and
  publishes it straight away, even inside the frame's deferred updates. While a slot's request
  keeps changing only in panning, it is resent once it has moved `s_reverbPanStep` (0.05),
  and exactly when it settles. `s_reverbIdleSlots 0` and `s_reverbPanStep 0` restore the
  previous behaviour, and `listReverbs` marks idle slots. The measurements are in
  [performance report section 15](performance-report.md#follow-up-idle-reverb-slots-and-the-turning-view).
- **Interface.** `idSoundSystem::EndLevelLoad( mapName )` takes the session's map path, as in
  the SDK, and `GetReverbName`, `GetNumAreas`, `GetReverb` and `SetReverb` provide the SDK's
  reverb editor interface. The retail reverb editor (`editReverb`) itself is a Windows tool and
  is not implemented.
- **Not reverb.** Retail's `s_reverbTime` and `s_reverbFeedback` only drive the comb filters of
  Doom 3's enviro-suit software effect (`DoEnviroSuit`), which Quake 4 never turns on; they play
  no part in the room reverb.

## Verification

- `tools/tests/native/SoundReverbCoreTest.cpp` (`openq4-sound-reverb-core`): the conversion
  against `efx-presets.h` for the 23 retail presets and the EFX ranges in `efx.h`; slot gathering,
  pinning, the duplicate-portal quirk, missing effects, one-slot devices and muting; portal pans;
  room and occlusion send levels.
- `tools/tests/sound_settings.py`: the checked settings path still passes every case and
  mutant, and a new mutant proves that disabling EFX releases the area slots.
- `tools/tests/sound_retail_reverb_contract.py` pins the retail constants, file names, messages
  and cvars in the engine sources.
- In game, `s_showReverb 1` and `listReverbs` show the tables loading without warnings on every
  stock map and the slots following the listener.
- Impulse responses rendered by OpenAL Soft's wave writer on `airdefense1`, one blaster shot with
  the listener in each kind of area: the tail 1.5-3 s after the shot sits 40 dB below the peak in
  a Sewerpipe area, 56 dB in Stone Room, 66 dB in Bathroom, 72 dB in Hallway, 75 dB in Mountains
  and 85 dB in Plain. Room's short decay, `s_useEAXReverb 0` and `s_muteEAXReverb 1` leave only
  the dry shot.
