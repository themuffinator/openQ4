# Vulkan authored probe sources

`vk_ProbeSource` supplies the GPU-to-CPU source bridge for the native authored
probe atlas. Residency, shared CPU selection and shader consumption are now
connected and undergoing [rendering qualification](vulkan-pbr-probes.md).
The source-reader evidence below qualifies the bridge independently of that
consumer. See the complete
[Vulkan gap-closure requirements](archive/2026-10-18/plans/2026-09-20-vulkan-gap-closure.md).

## Source contract

`VK_ProbeSource_Read` accepts an immutable, loaded, non-defaulted square cubemap
up to 256 texels per face. Every base face must have a successful complete
upload. Scratch/render images, incomplete images, invalid storage, unsupported
encodings and nonfinite HDR radiance fail explicitly. No partial cube or valid
stamp is returned on failure.

The current authored material loader requests uncompressed `TD_HIGH_QUALITY`
RGBA8 cubemaps. The reader also supports RGBA8 sRGB storage and generated
RGBA16F sources. Other encodings, including compressed probe sources, remain
explicitly unsupported. Ordinary compressed texture uploads are separate from
probe-source admission.

The read copies mip zero in native `+X,-X,+Y,-Y,+Z,-Z` order. Ordinary color
bytes undergo the same sRGB-to-linear conversion as the GL probe atlas. The
transfer reads encoded storage even for an sRGB image, so conversion occurs
once. HDR values retain their finite positive range; negative values clamp to
zero. Alpha does not enter environment radiance.

The stamp binds image handle, Vulkan image generation, shared storage
generation, upload generation and face dimensions. An in-place image upload
invalidates the old stamp even when allocation and handle remain unchanged.
Consumers must verify `VK_ProbeSource_Matches` before publishing filtered data.

## Admission decisions, 6 November 2026 (REN-06)

- **Supported:** authored RGBA8 and sRGB RGBA8 cubemaps (the material loader's
  uncompressed `TD_HIGH_QUALITY` request) and renderer-generated RGBA16F
  cubemaps, square, up to 256 texels per face.
- **Excluded, explicitly:** compressed probe sources (BC1-BC7) are rejected as
  `MODERN_SPECULAR_PROBE_ATLAS_REJECT_INVALID_STORAGE`; author probes
  uncompressed. Sources over 256 texels per face are rejected as
  `..._REJECT_OVERSIZED`, not downsampled. Authored HDR cubemaps are not a
  supported input: the loader requests RGBA8, so authored high-range data is
  quantised before it reaches the reader. The RGBA16F path exists for the
  renderer's own generators and is not a general HDR loader.

Every exclusion clears the output and stamp (regression controls below), and
the probe falls back with the rest of its set.

## Cold-miss cost and budget, 6 November 2026 (REN-06)

`gfxInfo` now reports source-cache misses on the `Vulkan PBR probes:` line:
`missReads`, and the mean and maximum microseconds of the synchronous source
read (`missRead*`) and of the CPU prefilter plus atlas tile uploads
(`missFilter*`). The 56-control authored-probe suite
(`renderer_vulkan_pbr_probes.py --backend vk`, RTX 4060 Laptop GPU,
debugoptimized build) passes and records 21 misses:

| Stage | Mean | Maximum |
| --- | --- | --- |
| Source read (copy after pending uploads, fence wait, decode) | 2.9-5.9 ms | 21.3 ms after image reload |
| CPU prefilter (7 mips x 6 faces, 128 GGX samples) and tile uploads, serial | 489-538 ms | 577 ms |
| The same with the tiles spread over up to 8 threads (`openq4PBR::PrefilterTiles`) | 84-87 ms | 93 ms |

Memory: the atlas is a fixed 2048 x 2048 RGBA16F image with 7 mips
(about 44.7 MB) for 8 residents plus the analytic slot. A miss adds a
transient host readback buffer of up to 3.1 MB (256-texel RGBA16F faces) and a
CPU cube of about 4.7 MB plus prefilter scratch, released when the miss
completes.

Budget: a cold or reload miss may add at most 4 ms to the frame that takes it
on the reference desktop, and residency may not hold more than the fixed
atlas plus one in-flight source. Every (mip, face) tile is independent, so
both backends (`vk_PBRProbes.cpp`, `ModernSpecularProbeAtlas.cpp`) now compute
them in parallel and upload them in the original order; the native
`openq4-pbr-math` test requires the result to equal the serial filter exactly,
and the authored-probe suites still pass (Vulkan 56/56, OpenGL 50/50). That
alone still stalled a newly resident source about 85 ms; the background
prefilter below meets the budget. Reducing sample counts would have changed
the GL-matched result and was not the fix.

## Background prefilter, 6 November 2026 (REN-06)

`r_pbrProbeAsyncPrefilter` (default 1) moves a cache miss off the frame that
takes it. Stages, each on a later frame than the last:

1. **Read.** OpenGL reads the six faces synchronously (`glGetTexImage`, about
   1.4 ms). Vulkan records the copy into the frame's open upload batch without
   submitting or waiting; the frame's own submission carries it.
2. **Collect (Vulkan).** Once a later batch has retired it, or its fence reports
   signalled, the encoded texels are copied out and the buffer is released. A
   source whose stamp changed meanwhile is dropped.
3. **Filter.** A background job (`openq4PBR::AsyncPrefilter`, four tile
   workers) decodes the texels (Vulkan), prefilters every tile and packs it to
   RGBA16F. The result equals the synchronous filter bit for bit.
4. **Publish.** The finished tiles upload in a fixed order
   (`ModernSpecularProbeAtlas_TileTarget`), spending at most
   `MODERN_SPECULAR_PROBE_PUBLISH_BUDGET_US` (2 ms) of each frame after its first
   tile; source reads share that budget, so further misses wait a frame. Packing
   on the job matters: the drivers' own float-to-half conversion cost 3-6 ms
   per 256-texel tile on OpenGL.

Until the last tile lands the slot is unpublished and the whole set keeps its
analytic fallback (`MODERN_SPECULAR_PROBE_ATLAS_REJECT_PREFILTER_PENDING`
on Vulkan, `pendingUpload` on OpenGL), the same whole-set rule as any other
miss. A slot reassigned or reloaded mid-job, an atlas reload, a source
change, a failed upload or renderer shutdown joins or discards the job and
releases any in-flight readback (deferred to the frame slot while its batch is
busy). Screenshot frames finish every pending stage at once, so captures show
the settled probes; `pbrProbeSettle` does the same for the next few frames
(it expires), which the probe suite uses before reading telemetry. With the
cvar at 0 a miss filters synchronously on its frame, as before.

`gfxInfo` reports `prefilterStarted`, `Published`, `Pending`/`Running`,
`Cancelled`, `Waits` (capture waits), the start-to-publish latency, and the most
probe work any non-capture frame did (`publishFrameMaxUs` on Vulkan,
`frameFilterMaxUs` with `sourceRead*` on OpenGL).

Measured on the authored-probe suites (`renderer_vulkan_pbr_probes.py`, RTX
4060 Laptop GPU, debugoptimized; Vulkan 56/56, OpenGL 50/50, each also requiring
that the background path ran):

| | Vulkan | OpenGL |
| --- | --- | --- |
| Most probe work in one gameplay frame | 2.1 ms | 3.3 ms |
| Probe visible after first need | 0.15-0.31 s | 0.14-0.37 s |
| Frame-thread publish per probe (spread over frames) | about 12 ms | about 11 ms |

Both meet the 4 ms budget. Screenshot and settle frames are excluded from the
frame maximum by design; a Vulkan read straight after an image reload still
waits for that reload's uploads (up to 34 ms), but only a capture does that
synchronously. `renderer-vk-clear-startup` and
`renderer-vk-probe-source-lifecycle` still pass with the same source-test
counts after the read was split. Not covered: other GPUs and platforms, and a
quiet-machine distribution of frame times rather than these maxima.

## Transfer ownership

A cache miss appends the six-face copy after pending image uploads on the
existing graphics/upload queue. It restores the original shader-readable
layout, waits for the upload fence, invalidates mapped host memory and checks
the source stamp again before publishing the complete decoded cube. The host
readback buffer is owned by this operation; the upload batch does not release
it. A failed wait retains normal deferred destruction.

This synchronous operation must be restricted to probe cache misses during
resource preparation. It does not submit, close, reset or replace the current
scene command buffer. It is not a general framebuffer readback interface, and
does not promise recovery from device loss. The native atlas calls it only
for a newly resident or changed authored source.

The shared Vulkan image uploader now rejects invalid mip/layer indices,
negative offsets, out-of-bounds rectangles and invalid row pitches before
allocation or command recording. Compressed uploads retain block padding in
their byte payload while copying only the physical mip extent. This preserves
the BinaryImage convention for padded 2x2/1x1 cubemap mip tails. Compressed
offsets and interior extents must meet block alignment.

## Regression controls

`rendererVulkanRenderTargetsSelfTest` includes real source reads for RGBA8,
sRGB RGBA8 and RGBA16F at 1, 8 and 256 texels per face. Asymmetric face/row/column
patterns distinguish face order, flips and transposes; distinct lower mips
detect the wrong mip being read. Expected linear values are independent
constants, including 0.25, 2, 4096 and 65504 for HDR and negative-HDR clamping.
The test repeats after image reload and purge/load, verifies an in-place
overwrite, and reads while a scene command buffer remains open.

Negative controls require cleared output for null, purged, 2D, oversized,
mutable, incomplete, unsupported and failed-upload sources, plus positive
infinity, negative infinity and NaN in HDR data. Malformed upload controls
also require unchanged queue counts and content generations. On devices with
BC support, eight further GPU readbacks verify every face and mip of BC1/BC3
cubes, including padded 2x2/1x1 tails; compressed probe decoding stays rejected.

The mandatory startup case requires the source-test result. The separate
`renderer-vk-probe-source-lifecycle` case repeats it around partial and full
video restart with an MSAA change, requires ordered completion markers and
active Vulkan validation, and rejects a partial restart that silently falls
back to full. Both hosted Vulkan workflow selections include that case.
Hosted execution and other physical GPUs/platforms remain unqualified here.

## Local qualification

The v15 candidate passes `renderer-vk-clear-startup` and
`renderer-vk-probe-source-lifecycle` with active validation on Windows,
NVIDIA RTX 4060 Laptop GPU, Vulkan 1.4.325. Each source-test execution passes
29 pattern controls covering 3,543,222 texels, 32 rejection controls and
eight BC1/BC3 mip readbacks. The lifecycle case executes the source test
three times and proves the ordered partial/full restart sequence. The
surrounding attachment, MRT, MSAA, scratch reload and emission tests also pass.

Evidence root: `.tmp/vulkan-gap-closure/probe-source/`. The final candidate's
Vulkan module SHA-256 is
`5d50c7f7918aedc2246c4eca76bf54a59a3322b2c741270f3b936c42e09407aa`.
The client, GL module and other runtime inputs remain those of the retained
v12 package. These resource tests do not prove native authored-probe lighting.

Both final 27-control environment gameplay suites pass at 0x and 4x MSAA,
including image/video lifecycle and transparency. All 54 complete engine TGA
captures are byte-identical to v12. `checkpoint-v15.json` pins those reports,
reference comparisons, the GPU reports, all 50 runtime files, the current
source worktree and the unchanged companion revision. Builddir, staging and
the independent v15 runtime contain the same tested renderer binary.

The earlier v13/v14 diagnostic patterns allowed duplicate faces. V15 assigns
separate RGB face bits and mixes spatial coordinates; independent pattern
checks prove six distinct faces at all tested extents and detect nine flip,
transpose, rotation and stride mutations. `pattern-quality.json` retains both
the old aliases and the new negative-control counts. V14's production source
reader and upload implementation are unchanged in v15; the diagnostic pattern
and test allocation guard are the only native-code differences.
