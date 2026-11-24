# Licensing

openQ4 contains components under different licences. Source availability does
not make every component GPL-licensed or permit relicensing SDK-derived code.

| Component | Terms |
| --- | --- |
| Engine and engine-authored support code | [GNU GPL version 3](LICENSE), retained per-file notices and the applicable [Doom 3](LICENSES/DOOM-3-ADDITIONAL-TERMS.txt) or [Doom 3 BFG](LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt) Additional Terms |
| SDK-derived game library in `src/game/`, including the independently implemented Awakening additions | [Quake 4 SDK Limited Use License Agreement](LICENSES/QUAKE-4-SDK-EULA.rtf), with original notices and any explicit per-file third-party terms preserved |
| Recovered game regression scripts/probes identified in [the coverage map](docs/dev/game-test-coverage.json) and marked with import comments | The same [Quake 4 SDK EULA](LICENSES/QUAKE-4-SDK-EULA.rtf) as their source repository; not relicensed as engine code |
| Third-party dependencies | Their own retained licences; see [dependency notices](LICENSES/) and the individual source trees |
| Retail Quake 4 and Awakening content | Not included or relicensed; users supply their own content |

The game libraries require the full version of Quake 4. The SDK agreement
restricts commercial use and distribution and must accompany copies covered by
it. Read the complete agreement, rather than treating this overview as a grant
of additional rights.

The engine is open source; the SDK-derived game libraries are source available
under the SDK EULA. Neither licence is applied as an alternative to the other,
and the EULA is not imposed on independently GPL-covered engine source.

The [source provenance inventory](docs/dev/source-provenance.md) and
[game import record](docs/dev/game-source-provenance.md) identify the sources
and accompanying notices. Consolidating their repositories does not settle the
compatibility of a linked binary distribution. No SDK linking exception,
rights-holder approval or legal compatibility opinion is asserted by this
documentation. Applicable distribution permissions remain a separate question.
