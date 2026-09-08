# Retained runtime integration checkpoint

8 September 2026. Stage 2 is in progress. This checkpoint establishes an engine
rendering path and tests for the layout-library candidate. It does not replace a
GUI, establish the canonical document format, or constitute a finished menu.
The [complete plan](../plans/idtech5-ui.md) and [visual specification](../ui-visual-design.md)
continue to define completion.

## Dependency and ownership

RmlUi **6.3** is pinned by the [Meson wrap](../../../subprojects/rmlui.wrap).
The release archive SHA-256 is
`d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede`.
Its [MIT licence](../../licenses/RmlUi.txt) permits this GPLv3 integration; the
notice is retained in source, credited in README and installed into the runtime
package's `licenses/` directory. The native Meson overlay compiles 184 core,
element and layout sources from that archive. It does not invoke CMake or add a
second dependency workflow.

FreeType, Lua, SVG/LunaSVG, Lottie, debugger, samples, profiling and third-party
containers are disabled. The enabled core has no new external runtime library
dependency. In particular, no SVG rasterizer is being substituted for editable
vector furniture. The upstream sources and headers remain in the downloaded
subproject with their notices. No upstream sample implementation was copied.

The core lives in `src/ui/retained/`, without engine precompiled headers or
platform/graphics includes. `Host` supplies VFS reads, translation, font metrics,
glyphs, image materials and indexed drawing. `RetainedUI.cpp` provides the engine
implementation. The client links the retained library; dedicated code uses
empty engine entry points and does not link RmlUi.

## Current capabilities

- A single owner of RmlUi's global services, with explicit initialization,
  document close/reload and shutdown. Competing global owners are rejected.
- Retained layout, source-element IDs, runtime style/text changes and bounds
  queries. Text mutation escapes markup. These APIs are integration tools, not
  a promise that raw RML is the editor's canonical serialization.
- Context dimensions use physical UI viewport pixels. `dp` is multiplied by
  SDL display scale and the independent player scale once. Window coordinates
  use pixel density and viewport origin separately. Engine triangle positions
  convert to its submission space only at the drawing boundary.
- Full-width aspect expansion and anchors, instead of a stretched 640x480 layout.
- Cached indexed geometry, two-dimensional transforms, rectangular clipping
  with interpolated UVs and premultiplied vertex colours, and native horizontal/
  vertical geometry gradients. The existing engine renderer consumes the result.
- Untextured vector triangles retain premultiplied blending through composition.
  Generated font images use straight coverage and a uniform text tint converted
  at the final boundary. The original materials are not modified. Intrinsic
  retained materials are implicit, process-local declarations.
- Existing scalable Quake 4 font pages and language-table lookups. Font/material
  geometry is recreated after a renderer restart or language-generation change.
- A steady presentation clock and CSS animation sampling between simulation
  ticks. This is not yet the specified timeline system; see the limit below.

## Evidence

`openq4-retained-ui-test` runs the same RmlUi core and adapter used by the client,
with an in-memory host. It checks 100%/200% density, player scale arithmetic,
window/pixel coordinate separation, 16:9 and ultrawide anchors, mutation,
triangle indices, overflow clipping of a gradient, lifetime ownership/restart,
and transform samples at 144 Hz. These are synthetic presentation samples,
not a measurement of a running game's display refresh rate.

Windows client, OpenGL module, Vulkan module and dedicated builds use the normal
Meson wrapper. The existing Linux high-DPI mouse contract and both platform
branches of the background-cursor test are also checked. The latter execute
production functions against stubs, without touching the host pointer.

The diagnostic fixture is `tools/ui/fixtures/runtime-smoke.rml`. Its rectangular
test rows exercise layout, text, colour and a gradient; they are not proposed
Quake 4 component artwork and do not satisfy the chamfered component gate.
It uses existing localization keys. It is not staged as shipped content.

Capture through `tools/ui/capture_legacy_baseline.py`, adding
`--retained-document tools/ui/fixtures/runtime-smoke.rml` and an optional
`--density` override. The script copies it into an isolated savepath, invokes
`ui_retainedPreview "retained-smoke.rml"` after active gameplay begins, settles
30 frames, then calls the engine's registered `screenshot` command. The window
is hidden and windowed, with mouse/controller input disabled. Capture reports
retain binary/fixture/image hashes. Visual review is still required.

Reviewed captures at 1280x720, all in actual `airdefense1` gameplay:

| Evidence directory under `.tmp/ui/retained/` | Check | Result |
| --- | --- | --- |
| `sp-gl-100-03` | OpenGL, 100% density | All fixture elements visible; exit 0, zero warnings/errors |
| `sp-vulkan-200-02` | Vulkan, 200% density | Correct doubled geometry/text and anchors; exit 0, two non-UI warnings |
| `sp-gl-200-restart` | OpenGL, 200%, full `vid_restart windowed` | Font pages and geometry return correctly; exit 0, zero warnings/errors |

The engine's `gfxInfo` confirms the requested backend is active, not a fallback.
The UI's 200% geometry and typography agree visually between these GL/Vulkan
captures. Different game frames/backgrounds prevent a whole-image pixel equality
claim. The Vulkan log reports `vertex array range in virtual memory (SLOW)` and
`post-process surfaces skipped because feedback capture failed` before the
retained preview loads; these renderer issues remain open. Previous MP reference
warnings are recorded in the [inventory report](inventory-report.md).

Screenshot SHA-256, in table order:

```text
c614460dfdbc2e584de3bd8459e21cf21a6325563a86c9e1dc850855dfbcdcc9
8dcb3ee575f6e5474fdbc35ec7d664462bb4cfdb3745645a5cbd5f8142bf6dcf
acfbd591759a7cb413211e99becfb225511b872529c2e902aadb02bfb73b6ff4
```

The tested client SHA-256 is
`811642e2d228b3ed2337815aced5a6b6d00943ff78b35be5604c7b72c99b3480`.
The companion source revision remains
`9a2d8715fc40d4f5b1ca1cfd7f90230baf3192f2`; this checkpoint does not change the
game bridge or ABI. Raw screenshots and logs stay out of Git.

Earlier capture attempts are retained as failures: the first did not load its
unquoted hyphenated filename; the next exposed native triangle culling. Both
were corrected. A simultaneous Vulkan launch exited before initialization under
the engine's single-instance protection; successful captures ran sequentially.
UI intrinsic materials are now two-sided, including mirrored transforms. Only
implicit, generated retained solid/font resources bypass the asset-precache
warning; ordinary or explicitly authored materials keep that diagnostic.

## Open requirements and selection findings

- RmlUi supplies no browser user-agent stylesheet. The fixture explicitly makes
  `div` a block and the body fill its viewport. Generated layout styles must
  establish these defaults deliberately.
- RCSS is not browser CSS. For example, its linear tween spelling is `linear-in`.
  A raw `linear` token can be interpreted as a keyframe name instead. The editor
  compiler must validate generated style/motion references.
- RmlUi's `ElementAnimation::UpdateAndGetProperty` clamps each time step to
  **100 ms**. Its default CSS clock therefore loses elapsed motion time after a
  stall. The canonical timeline must own timing, interruption, easing, pause,
  cancellation and reduced-motion behavior instead of adopting that policy.
- Shader-based linear/radial/conic gradients, clip masks, layers, filters and
  generated RGBA textures are not implemented by this adapter. Horizontal/
  vertical geometry gradients are supported. Unsupported features remain
  qualification failures, not silent claims of approximate equivalence.
- Arbitrary curved vector paths, holes, stroke joins/caps, full coverage
  antialiasing, editor path sources and density-dependent tessellation remain.
- The text adapter uses the existing 48-point engine metrics/pages. It does not
  yet provide arbitrary-size rasterization, shaping, kerning, font effects,
  inline game colour/icon escapes, IME or a separate player text scale. Textured
  vertex gradients with varying alpha need the full premultiplied texture path.
- Direct image/font-page lookup is supported; multi-stage game materials,
  cinematics, model previews and world-surface instances require their own
  behavior-preserving operations. Live DPI font-density invalidation needs the
  new font backend, beyond the restart/language invalidation already present.
- Input/focus/navigation, game semantic events and commands, save restoration,
  schemas, the editor and translation of all 271 inventory entries remain open.
  The preview does not capture or route user input and its rows are not controls.
- Actual mixed-DPI monitor transitions, high-refresh playback, all renderer
  modes and Linux/macOS/Android runtime checks remain unqualified.

An unrelated build issue was observed: rebuilding byte-identical `pak1.pk4`
preserves an old output timestamp, so newer input timestamps can trigger the
same expensive packing step on subsequent builds. The content was verified by
the normal pack build; only the ignored build-output timestamps were refreshed
locally for this investigation. No packaging implementation change is included.
