# Focus visibility

Retained keyboard and semantic focus reveal the full control border with a
4 dp inset inside each eligible scroll area. Geometry follows the actual
accumulated transform, including nested and rotated scrollers. Scroll offsets
round outward to layout pixels and clamp to the authored range. The inset
shrinks when a control nearly fills the viewport; oversized controls retain
nearest-edge behavior without alternating between edges on subsequent frames.

Reveal runs on a focus change, an explicit focus request, and focused Number
validation that changes the field's size. Legacy documents also reveal on a
viewport or density change. Documents with [authored scrollbars](scrollbars.md)
instead preserve and clamp their logical scroll offsets across those changes.
Ordinary unchanged frames preserve deliberate wheel scrolling. Hidden or visible overflow
does not become a scroll area, and reveal creates no extra content range.
External labels and controls larger than their available region still need
appropriate page layout; this shared behavior does not qualify a complete page.

The [RmlUi patch attribution](../../../subprojects/packagefiles/rmlui/README.openq4.md)
documents the exact projected border-quad helper. It refreshes layout geometry
before measuring, rejects unavailable projections, and preserves the existing
bounding-box behavior. Focus-triggered layout runs with the owning view's clock.

The isolated actual Runtime/RmlUi suite checks 100%, 125% and 200% density,
nested and rotated scroll planes, horizontal overflow, resize, user scrolling,
singular and depth-clipped transforms, oversized controls and submitted clipped
geometry. The MSVC debug-runtime run passes 3,454 checks and rejects nine compiled
behavioral mutations; the adjacent Number Runtime suite passes 13,988 checks.
These counted Host checks do not create a window, operate a device, or establish
GPU, native input or complete product acceptance.

The integrated Windows build passes all 40 UI suites. Windowed SP/OpenGL at
125% and MP/Vulkan at 200% each reach gameplay before exercising SYSTEM and
record 38 successful semantic operations with 12 reviewed engine screenshots.
In the 200% run, the active ambient field's bottom is 464 pixels and its scroll
body's bottom is 472 pixels: the entire focused border has the intended 8-pixel
gap. Raw engine screenshots and their PNG previews have identical RGB pixels.
The immutable evidence is
`.tmp/ui/native-managed-bridge-integration/validation-evidence.json`.
