# In-tree game regression coverage

GOV-02 review, 18 October 2026. The imported
[openQ4-game revision 4d87dbdb](https://github.com/themuffinator/openQ4-game/tree/4d87dbdb6934307988b371d3c794190a1380f322/tools/tests)
contains 83 test/probe paths. The [coverage map](game-test-coverage.json) records
every historical path, its current owner and why it was recovered or superseded.
Existing tests and the native bot fixture are reused. Companion-only source
layout/build assertions are replaced by the unified engine's current source,
packaging and platform checks, rather than reinstating companion builds.

Recovered scripts/probes retain the **Quake 4 SDK EULA** scope of their source
repository; see [the import record](game-source-provenance.md) and
[licensing](../../LICENSING.md). They are not relicensed as engine GPL code.
The per-file import comments and coverage map preserve that provenance.

The recovered tests now use `tools/build/list_sources.py` and canonical
`src/game` (one tree since CORE-07). Fixtures include current directional spectator controls,
input ownership masks, team-assignment state, bot-aware duel rotation and frozen
terminal-result identity. The summary fixture expects a neutral draw for a tied
casual match. A broken Unicode snowman in a historical test expectation is
spelled as a Unicode escape and still compares actual multilingual output.
These adaptations retain executable tests of production functions and classes.

`game_regression_suite.py` discovers 74 Python checks from the explicit mapping,
pins their game source root to this checkout, requires a fixture compiler and
fails on missing scripts or silently skipped native checks. Meson registers it
as `openq4-imported-game-regressions`. The push/PR validation profiles check its
discovery, and their normal Meson test step executes it. No archived checkout or
Git fetch is needed to run the suite.

The actual `push-verification.yml` build jobs invoke `validate_push`, and
`commit-validation.yml` build jobs invoke `validate_pr`, without `--skip-build`.
Their common runner enables `build_native_tests` and executes `meson test` after
compilation. The separate script-smoke dry runs are not the execution evidence.
This source audit establishes CI wiring; local passes do not claim a completed
remote Actions run for the final commit.

Meson also runs the string-allocation and unsigned-char probes on all hosts,
and Linux portability, LP64/random and MD4/MD5/CRC probes on Linux. Their native
platform restriction is preserved; a Windows result is not Linux ABI evidence.
The new shared clock policy has its own native test and the production SP pause
fixture described in [clock ownership](presentation-clock.md).

Run the suite with `python tools/tests/game_regression_suite.py`; its logs stay
under `.tmp/game-regressions/`. Normal validation also executes the registered
native probes. The initial recovery failures and successful reruns for this
work are retained in `.tmp/stage-one/`; the final evidence record states the
tested source/build identity. Source-token checks establish integration
contracts, not full gameplay or performance qualification.
