# Memory-file optimisation and robustness

The September 2026 pass addresses `idFile_Memory`, the engine's byte stream for
generated declarations, compressed buffers, and generated-cache payloads.

## Behaviour

- An overwrite after seeking advances the cursor and extends the file only if
  the write passes the previous end. It no longer inflates the length or places
  the trailing terminator beyond the allocation.
- Writes reserve the terminator and check the signed size limit before adding
  lengths. Fixed caller-provided buffers still reject writes that do not fit.
- Owned buffers grow geometrically, rounded to the requested granularity when
  that rounding fits. Growth normally copies only the logical file contents.
- Writes from the same file's buffer survive reallocation and overlapping
  source/destination ranges. Slices that extend outside the allocation fail
  before changing the file.
- Reads clamp the byte count before forming an address. Seeks clamp integer
  offsets before pointer arithmetic, including `LONG_MIN` / `LONG_MAX` on both
  Windows and Linux. The historical memory-file convention is preserved:
  positive `FS_SEEK_END` offsets move backward from the end, and failed seeks
  clamp to the nearest endpoint.
- `Clear(true)` frees owned storage and detaches borrowed storage without
  freeing the caller's buffer. `Clear(false)` keeps capacity and terminates an
  empty writable file. Neither operation changes the open mode.
- Replacing read data releases previously owned storage; selecting a slice of
  owned storage retains ownership and moves that slice to the start of the
  allocation. Empty or invalid writable-buffer constructor arguments start an
  empty growable file and never adopt the supplied pointer.

There are no file-format changes, new assets, dependencies, or settings.

## Reproducible regression checks

With `-Dbuild_native_tests=true`, the normal Meson build includes
`openq4-file-memory-test`. On Windows:

```powershell
tools/build/meson_setup.ps1 compile -C builddir
tools/build/meson_setup.ps1 test -C builddir --no-rebuild --print-errorlogs openq4-file-memory
```

The test compiles the production memory-file declaration and implementations
verbatim, extracted from `File.h` and `File.cpp` at build time. Only surrounding
engine services are substituted: string/base-file types, errors, and an
allocator that detects invalid frees and overwritten allocation guards. Changes
to either production file regenerate the test include. The suite exercises
overwrites, growth, aliased writes, borrowed and owned storage, extreme seeks,
oversized reads/writes, EOF, failed allocation without partial mutation, and
10,000 mixed writes/seeks compared against an independent string model.

## Measured result

For an 8 MiB stream appended in 64-byte writes with the default 16 KiB granularity:

| Measurement | Before | After |
| --- | ---: | ---: |
| Buffer allocations | 513 | 16 |
| Cumulative bytes requested from the allocator | 2,160,082,944 | 34,734,080 |

These are deterministic allocation counts and cumulative allocation volume,
not peak memory, elapsed-time measurements, or an FPS claim. The new regression
test bounds allocation count and total volume to detect a return to repeated
whole-buffer growth. Run the executable with `--growth` for this case alone.

Validation on September 5, 2026: Windows x64 full build and all 12 native tests
passed. The memory-file suite also passed with Clang AddressSanitizer and
UndefinedBehaviorSanitizer on Linux x64 under WSL, covering the LP64 seek limits.

The stock-asset gameplay checks use `renderer_gameplay_benchmark.py` with the
`sp-airdefense1` and `mp-q4dm1-listen` cases, hidden windowed OpenGL at 1280x720,
mouse/joystick input disabled, explicit MP auto-join, and a three-second sample.
Only engine-written screenshots are used for visual inspection. This is a
functional check; timing budgets are disabled for these hidden-window runs.

The first SP run passed without warnings. The first MP run reached gameplay,
but failed the harness display check because config reload changed `r_mode`
from `-1` to `5`, even though the actual window stayed at 1280x720. Reapplying
`r_mode -1` in the post-load script made the MP rerun pass. The MP logs also
report unrelated missing `q4dm1` AAS files, late precaching of declarations, and
`ammo_shotgun_3` placed in solid geometry. These remain outside this file-I/O
change.

Evidence is retained locally under `.tmp/memory-file-round/`. A concurrent
shadow-rendering test locked `.install/baseoq4/pak0.pk4` during final staging,
so the final binaries are staged separately in
`.tmp/stock-runtime/memory-file-round/` for validation without interrupting it.
Both final SP and MP cases passed in that isolated runtime; see
`.tmp/memory-file-round/final-gameplay/renderer_gameplay_benchmark_report.md`.
Visual inspection of the final MP client capture also shows distorted
first-person arm geometry. This needs a separate rendering/animation
investigation; passing the functional harness does not establish visual parity.
The live `.install` tree should be refreshed with the normal install command
after those tests finish.

The staging failure also exposed a Windows tooling bug: the retry path called
the game-process shutdown routine even with `OPENQ4_INSTALL_CLOSE_RUNNING=0`.
The retry now honors the same flag as the first attempt. Its enabled, disabled,
non-install, successful-install, and retry-disabled branches were checked with
stubbed install/process calls rather than launching or stopping games.
