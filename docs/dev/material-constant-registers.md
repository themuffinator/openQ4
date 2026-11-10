# Constant material registers

Status: restored on 10 November 2026 on both renderers. `r_useConstantMaterials 0`
gives the earlier behaviour. Roadmap item: FPS-08.

## What was wrong

Every material carries a small array of float registers. Literals fill most of
them when the material is parsed; the rest are written by its expressions
(`rgb sinTable[ time * 0.5 ]`, `if ( parm7 )`, a `scroll`) each time a surface
is drawn. Stock idTech 4 marks a material's registers constant "until shown
otherwise" when its parse starts, clears the mark when an expression reads
something that can change, and evaluates a constant material once. Each surface
of such a material is then handed that one block
(`idMaterial::ConstantRegisters()`) and skips its own allocation and
evaluation.

openQ4 never set the mark. The parse data is zeroed before each parse, so the
flag started false, `CheckForConstantRegisters()` returned early for every
material, and every draw surface and light interaction allocated and evaluated
its registers in every view, on OpenGL and on Vulkan. The upstream base this
engine grew from lacks the line too. It was found during the November 2026
shadow-mapping audit, which had to ask about registers one at a time
(`idMaterial::GetConstantRegisterValue`) because the whole-material answer was
always "no".

The repair is one line in `idMaterial::ParseMaterial`
(`pd->registersAreConstant = true;`). The rest of this page is the audit that
made the line safe, what it saves and how it is tested.

## Which materials are constant

A material is constant unless one of these appears in any of its expressions.

| Term | Reads | Clears the flag in |
| --- | --- | --- |
| `time` | the view's time | `idMaterial::ParseTerm` |
| `parm0` to `parm11` | the entity's shader parms | `ParseTerm` |
| `DecalLife`, `DecalSpawn` | parms 4 and 5 under their decal names | `ParseTerm` |
| `VertexRandomizer` | the entity's diversity parm | `ParseTerm` |
| `global0` to `global7` | the view's shader parms | `ParseTerm` |
| `sound` | the entity's sound emitter level (`OP_TYPE_SOUND`) | `ParseTerm` |
| `glslPrograms` | the live GLSL capability (`OP_TYPE_GLSL_ENABLED`) | `ParseTerm` |
| `colored` (stage keyword) | parms 0 to 3 as the stage colour, with no expression | `idMaterial::ParseStage` |

These are the only ways a register can depend on anything but literals:

- The 22 predefined registers (time, twelve parms, eight globals, the vertex
  randomizer) can be named only by the terms above and by `colored`.
- Of the sixteen expression operations, only `OP_TYPE_SOUND` and
  `OP_TYPE_GLSL_ENABLED` read state outside their operand registers. A table
  lookup reads its table and its index register, so `sinTable[ 0.25 ]` is
  constant and `sinTable[ time ]` is not, through `time`.
- `idMaterial::EvaluateRegisters` reads the view and the entity only to seed
  the predefined registers.

Six terms are folded to a literal while the material is parsed and leave it
constant: `fragmentPrograms`, `POTCorrectionX`, `POTCorrectionY`, `VideoWidth`,
`VideoHeight` and `IsMultiplayer`. A surface that evaluates its registers
copies that same literal, so the constant block is exactly as fresh as an
evaluation would be. A video restart or a session change does not reparse
materials, so neither path follows such a change; that was already so, and
clearing the flag for these terms would not alter it.

## What reads a surface's registers

`R_SetupDrawSurfShaderRegisters` (`tr_light.cpp`) is the one place draw
surfaces get their registers: ambient surfaces, light interactions, GUI
surfaces and decals. Lights evaluate their own material every view and never
use a constant block.

A constant block holds zeros in the predefined registers. Both back ends read
registers through the indices a stage stores, and a constant material has no
stage index that points at a predefined register, so nothing they draw reads
those zeros. One consumer did read them directly: the scene-packet instance
record copied the entity colour from registers `parm0` to `parm3`
(`idScenePacketFrame::FindOrAddInstanceRecord`), a field used by diagnostics
and self-tests. It now reads the entity when the registers are a shared block.

The block lives and dies with the material's stage array
(`idMaterial::FreeData`), which every draw already dereferences, so a draw
surface that points at it has the lifetime it always had for its stages.

## The switch

`r_useConstantMaterials` (default 1, not archived) is read each time a surface
asks. At 0 `ConstantRegisters()` returns NULL and each surface evaluates as
before the repair. It needs no restart.

## What it saves

Little: 5 to 22 microseconds of CPU per frame in the three scenes measured,
0.1 to 0.5 percent of the frame's CPU time. Most register set-ups become free,
but they were the cheap ones.

All figures are from hidden clients on the reference laptop at 2560x1440 with
the Quality preset, the view held still and the game running, with
`r_useConstantMaterials` alternated inside one process. A dozen other sessions
share the machine, so only such paired comparisons mean anything. The hidden
client presents about 62 frames a second.

### Inside the function

A temporary timer (not committed) around `R_SetupDrawSurfShaderRegisters`
counted the calls of each frame and timed them with the processor's time-stamp
counter, in blocks of eight frames. The switch changed every 0.6 s; a pair is
two adjacent windows, one in each state.

| Scene | Renderer | Set-ups per frame | Take the block when on | Evaluated, on / off | Time in the function, on / off | Saved per frame (95% interval) | Pairs that agree |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `game/hub2` | OpenGL | 389 | 267 (69%) | 95 / 318 | 30.9 / 38.5 µs | 7.7 µs (5.5 to 9.8) | 18 of 19 |
| `game/hub2` | Vulkan | 389 | 267 (69%) | 95 / 318 | 22.6 / 27.8 µs | 5.0 µs (3.5 to 6.6) | 17 of 19 |
| `game/convoy2` | OpenGL | 1347 | 1173 (87%) | 74 / 1063 | 43.6 / 57.6 µs | 14.7 µs (11.1 to 18.3) | 19 of 19 |
| `game/convoy2` | Vulkan | 1347 | 1172 (87%) | 74 / 1064 | 45.6 / 66.1 µs | 19.2 µs (16.0 to 22.4) | 19 of 19 |
| `game/airdefense1`, arena view | OpenGL | 794 | 652 (82%) | 129 / 614 | 44.3 / 56.1 µs | 15.2 µs (10.9 to 19.5) | 19 of 19 |
| `game/airdefense1`, arena view | Vulkan | 794 | 652 (82%) | 129 / 612 | 54.3 / 71.3 µs | 22.2 µs (17.3 to 27.1) | 19 of 19 |

The set-ups not listed as evaluated or as taking the block reused an
evaluation made earlier in the same view for the same entity and material.

An avoided evaluation is worth about 15 to 45 nanoseconds: a constant material
has no expression to run, so evaluating it was a frame allocation and a copy
of its literals. The materials that still evaluate are the ones with
expressions, table lookups and sound levels, and they keep most of the
function's cost.

### Whole frame

The same alternation was timed at the frame level from the renderer's own
per-frame trace: 150 pairs of 0.6 s windows per run (about 33 frames each, the
four after a toggle dropped), the median frame of each window, and pairs
dropped whole when another session stalled a window (under 20 frames, or a
median over twice the run's).

| Scene | Renderer | CPU per frame | CPU saved, mean (95% interval) | Pairs faster when on | Frame build saved, mean (95% interval) | Pairs faster when on |
| --- | --- | --- | --- | --- | --- | --- |
| `game/hub2` | OpenGL | 5.04 ms | 32 µs (-69 to 133) | 76 of 150 | -1 µs (-55 to 53) | 76 of 150 |
| `game/hub2` | Vulkan | 3.63 ms | 97 µs (9 to 186) | 81 of 150 | 60 µs (-6 to 126) | 83 of 150 |
| `game/convoy2` | OpenGL | 6.00 ms | 166 µs (22 to 309) | 74 of 134 | 63 µs (16 to 110) | 82 of 134 |
| `game/convoy2` | Vulkan | 4.10 ms | 9 µs (-74 to 92) | 70 of 147 | 21 µs (-34 to 75) | 72 of 147 |
| `game/airdefense1`, arena view | OpenGL | 7.11 ms | 181 µs (6 to 355) | 70 of 126 | 111 µs (31 to 192) | 69 of 126 |
| `game/airdefense1`, arena view | Vulkan | 5.06 ms | -10 µs (-96 to 76) | 75 of 149 | 3 µs (-58 to 64) | 71 of 149 |

**The whole-frame difference is not resolved.** Windows differ from their
neighbours by 0.3 to 1.0 ms (standard deviation of the pair differences), the
pairs split 47 to 61 percent, and the intervals are 0.05 to 0.18 ms wide on
each side. Five of the twelve sit just above zero, but the amounts they
suggest are several times what the function costs in total, and the median of
the same pairs stays above zero in only one of the five (frame build in
convoy2 on OpenGL: 47 µs, 10 to 81). That is not a saving to claim on a
machine where other clients move a window by more than the effect. What can be
said is that no run shows the frame slower, and that the measured saving is
the 5 to 22 µs above.

A first pass with 12 pairs of 2.5 s windows was discarded. Most of it ran
beside another session's compile, with windows moving by 1 to 2 ms; its one
quiet run suggested 0.17 ms in hub2 on OpenGL, which 150 pairs did not
reproduce.

## The picture does not change

Paused captures were compared in one hidden client per renderer and scene, at
2560x1440 with the Quality preset: game time stopped (`g_stopTime 1`),
sound-reactive shaders held (`s_constantAmplitude 1`, because they keep moving
while the game is paused), the HUD hidden, and four captures taken in the order
on, off, on, off.

| Scene | OpenGL: on against off | Vulkan: on against off | Repeat controls |
| --- | --- | --- | --- |
| `game/hub2` | 0 of 3,329,856 pixels differ | 0 differ | 0 differ |
| `game/convoy2` | 0 differ | 0 differ | 0 differ |
| `game/airdefense1`, arena view | 0 differ | 0 differ | 0 differ |

Both on-against-off pairs and both controls (on against on, off against off)
are identical in every pixel and channel. With the HUD shown, its animated
parts (the transmission waveform, the health trace) differ between any two
captures, repeats included, because interface time is not paused; no pixel
farther than 6 px from those differs between on and off.

The log of each run prints the value of `r_useConstantMaterials` after every
change, and the same binary passes the self-test below, which requires a block
when the switch is on and none when it is off.

## Tests

- `tools/tests/material_constant_registers_contract.py` pins the
  initialisation, the exact list of terms that clear the flag and of terms
  folded while parsing, the list of expression operations and predefined
  registers, the switch, the consumer in `tr_light.cpp`, the instance-record
  guard, and the set of renderer sources that name a predefined register. A
  new term, operation or register fails it until it is classified.
- `rendererPBRMaterialSelfTest` (run by the renderer validation matrix) parses
  one material per varying term and requires no block; parses one per folded
  term and per literal-only expression and requires a block equal to a fresh
  evaluation under a non-zero time, entity and view; and checks that
  `r_useConstantMaterials 0` withholds the block.
