# Display Settings and Multi-Screen Guide

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

This guide covers openQ4 display/window settings for end users, including multi-monitor behavior and modern fullscreen/window handling.

## Quick Start

- Press `Alt+Enter` to toggle fullscreen/windowed mode (fast path uses `vid_restart partial`).
- Run `listDisplays` in the console to list monitor indices for `r_screen`.
- On SDL3 builds, run `listDisplayModes [displayIndex]` to list available exclusive fullscreen modes, including SDL-reported content scale, pixel density, and exact refresh details when available.
- The in-game `Settings -> System` menu exposes display resolution, fullscreen policy, borderless/window sizing, custom exclusive fullscreen sizing, refresh rate, UI aspect behavior, display target, multi-screen, and resolution scale controls.
- `Settings -> System` groups **Rendering Backend**, **Rendering Device**, **Performance Preset** and **Auto-Detect** above the display controls in the retained interface.
- After changing video cvars, run `vid_restart` (or `vid_restart partial` for quick window/fullscreen transitions).

## Rendering Backend and Device

The retained **Settings -> System** page puts backend and GPU selection beside
the preset and Auto-Detect controls. **Automatic** prefers a compatible discrete
GPU over integrated and software devices. Fresh desktop configurations try
Vulkan, then fall back to OpenGL if it cannot initialize. Android keeps OpenGL
ES as its default. Automatic keeps OpenGL when PC OpenXR is enabled, because
the implemented VR graphics binding requires it. Existing saved backend
choices are preserved. Selecting Vulkan explicitly does not enable VR.

Choose a named GPU to select that device and Vulkan together. Choose
**Automatic (high performance)** to restore automatic backend/device selection.
Apply the changes, then use **Restart Renderer** at the main menu. During a
game, first return to the main menu through the normal game flow. The active
backend and device remain visible while changes await restart; Auto-Detect is
available after the chosen hardware starts. This restart supersedes backend
and GPU overrides supplied for the current launch. Launchers that supply those
overrides again on the next launch still take precedence over saved settings.

Device choices use persistent Vulkan IDs, so changing driver enumeration order
does not select a different GPU. Missing saved devices are shown explicitly;
choose Automatic or another available device. Unsupported devices are disabled.
OpenGL and OpenGL ES use operating-system/driver GPU routing, so a named GPU
selection switches to Vulkan. Among compatible discrete GPUs, automatic choice
uses dedicated memory capacity as a tie-breaker; this is not a speed benchmark.

For console or stock-menu use, `rendererDevices` lists the detected devices and
their IDs. Set `r_renderDevice "<id>"` and `r_renderApi vulkan`, then restart the
engine process. `r_renderDevice auto` restores automatic GPU selection.
`r_vkDevice` remains a temporary diagnostic index override; remove that override
before using the menu's GPU selector.

## Performance Presets

The `com_performancePreset` cvar stores the selected preset. Use the Settings menu dropdown, or run `applyPerformancePreset [name]` from the console.
Running `applyPerformancePreset` without a name applies the stored `com_performancePreset`; if that stored value is invalid, openQ4 falls back to `balanced`. Explicit unknown names are rejected without changing the current preset.

| Preset | Display and AA | Texture and audio profile | Intended use |
|---|---|---|---|
| `minimum` | 50% scale, no AA, no shadows, 30 FPS cap | Aggressive texture downsizing, 1x anisotropy, one sound sample per shader, automatic speaker layout, room reverb, lower emitter budget | The floor: software or limited renderers, and systems below the minimum requirements. |
| `lowpower` | 75% scale, no AA, no shadows, 30 FPS cap | Texture downsizing, 1x anisotropy, one sound sample per shader, automatic speaker layout, room reverb, lower emitter budget | Raspberry Pi-class boards, low-memory phones and battery saving. |
| `low` | 75% scale, no AA, the original stencil shadows, 60 FPS cap | Texture downsizing, 2x anisotropy, one sound sample per shader, automatic speaker layout, room reverb, lower emitter budget | PCs at the [minimum requirements](getting-started.md#system-requirements). |
| `performance` | 85% scale, SMAA medium, 60 FPS cap | Full-size textures, 2x anisotropy, automatic speaker layout, room reverb, moderate emitter budget | Modest desktops and handhelds aiming for smoother frame pacing. |
| `balanced` | 100% scale, 2x MSAA, SMAA medium, 120 FPS cap | Full-size textures, 4x anisotropy, automatic speaker layout, room reverb, full emitter budget | General desktop default. |
| `enhanced` | 100% scale, SMAA high (no MSAA), 144 FPS cap, 1024 projected shadow maps; point lights keep stencil shadows | 16x anisotropy, full-size textures, automatic speaker layout, room reverb | High-refresh play with modern effects: holds 144 FPS on the reference laptop. |
| `quality` | 100% scale, 2x MSAA, SMAA high, 144 FPS cap, 2048 projected / 512 point shadows | 16x anisotropy, DDS replacements enabled, larger upload budget, automatic speaker layout, room reverb | Every light shadow-mapped; holds 100 FPS on the reference laptop. |
| `ultra` | 100% scale, 4x MSAA, SMAA Ultra, 240 FPS cap, 4096 projected / 512 point shadows | 16x anisotropy, full-size textures and DDS replacements enabled, high-end benchmark tag, automatic speaker layout, room reverb | The best image that holds 75 FPS on the reference laptop. Manual choice. |
| `maximum` | 100% scale, 16x MSAA request, SMAA Ultra, 240 FPS cap, 4096 projected / 2048 point shadows | As Ultra | Everything on. No frame-rate target. Manual choice. |

`enhanced`, `quality`, `ultra` and `maximum` enable [shadow maps](shadow-mapping.md), bloom, SSAO and tone mapping. Enhanced shadow-maps projected and parallel lights with 5-tap fixed filtering in a 4096 atlas, keeps the original stencil shadows for point lights, and uses 12 SSAO samples. Quality shadow-maps every light: 2048 projected maps with 9-tap PCSS, 512 point cubes, a 4096 atlas and 20 SSAO samples. Ultra raises projected maps to 4096 with 13-tap PCSS in an 8192 atlas and uses 32 SSAO samples; its point cubes stay at 512 because larger cubes cost more than every other upgrade together. None of these three uses cascades or motion blur. Maximum requests every supported maximum: 2048 point cubes, 13-tap projected and point filtering, four stabilized cascades, a 16384 shadow atlas, 32 SSAO samples and 16-sample motion blur with object vectors. All modern-effects tiers keep unlimited shadow updates and five bloom levels; reducing the level count changes the glow's appearance, not just sampling quality. Low, Performance and Balanced retain the original stencil shadows and leave these post effects and the HDR scene target off. The weapon flashlight casts shadows on every preset that draws them; projectile lights cast them only from Quality up, as on the original game's top setting (`g_dynamicLightShadows`). Minimum and Low Power draw no shadows at all, so that the two presets below the minimum requirements remove rendering work rather than only resolution. On Intel Iris Xe graphics that took 12 to 16 percent off each frame in the hub and combat scenes we measured, about as much as lowering the scene from 75% to 50%. Changing down from Maximum restores the lower tier's sample counts and shadow sizes, including settings for effects currently disabled.

### Frame-rate targets

Enhanced, Quality and Ultra are each built to hold a frame-rate class on the project's reference machine: an RTX 4060 Laptop GPU with an i7-13700H, at 2560x1440 fullscreen, on both OpenGL and Vulkan, in every scene of the performance suite.

| Preset | Average | 1% low |
|---|---:|---:|
| Enhanced | 144 FPS | 90 FPS |
| Quality | 100 FPS | 60 FPS |
| Ultra | 75 FPS | 45 FPS |

Maximum carries no target. Faster or slower hardware, other resolutions and heavier scenes move these numbers; they describe the reference machine, not a guarantee. The measured results are in the [performance report](../dev/performance-report.md).

MSAA is clamped to the highest sample count supported by the active device and render-target formats; inspect `gfxInfo` for requested and effective counts. Every preset from Balanced up renders at native resolution (100% scale). Brightness, exposure, effect strength and experimental renderer modes are separate controls; CRT filtering stays off. Presets restore full material detail and clear any diffuse `image_picmip` reduction; every preset from Low up also turns shadows back on. DDS replacement packs remain available at every tier.

To adopt the revised settings on an existing configuration, select the preset again or run `applyPerformancePreset ultra`. Merely storing `com_performancePreset=ultra` does not reapply the profile. A configuration saved with the earlier Ultra keeps those settings until you select a preset again; select **Maximum** to keep that everything-on image, or **Ultra** for the new 75 FPS recipe.

Performance presets write video, texture-allocation, texture-sampling, and audio backend cvars. Anisotropy is capped to the maximum supported by the active GPU. Run `vid_restart` after applying one from the console so renderer, texture allocation, and sampler changes take effect; run `s_restart` as well if you want speaker/EAX/emitter-budget changes to rebuild the active sound backend immediately.

In the modern interface, choose the preset on **Settings > Display** and press **Apply Changes**. That restarts the renderer once and, when the speaker layout or reverb setting changed, the sound device. A preset that changes anti-aliasing asks you to keep or revert. High-quality texture flags and the per-sound sample limit fully change when the game next starts; the page tells you when your changes include one of them.

`autoDetectPerformancePreset` takes no arguments. It recommends and applies a starting point using the **active renderer and GPU**, system RAM, CPU concurrency, platform power hints and current drawable resolution. It does not run a benchmark or promise a particular frame rate. Every preset but Maximum can be recommended; Maximum has no frame-rate target and remains a manual choice.

- Software rendering, insufficient classic material/texture capabilities and systems with at most 2 GiB RAM select Minimum. A PC at the minimum requirements, with at most 4 GiB RAM or a discrete GPU with at most 1 GiB of verified dedicated memory, selects Low. Raspberry Pi and explicit `OPENQ4_LOWPOWER=1` / `OPENQ4_RASPBERRYPI=1` select Low Power unless the Minimum constraint applies.
- Steam Deck selects Performance. Shared-memory GPUs, including Apple Silicon, can select Balanced at up to the 1080p class with sufficient RAM and classic-rendering capabilities. Apple's OpenGL 2.1 path is not forced to Minimum just because it cannot run modern post effects; Auto-Detect leaves unsupported SMAA off. ARM and x86 use the same graphics policy.
- Android is limited to Performance automatically to respect mobile power budgets. A phone with about 12 GB of RAM or more renders a 1080p-class scene; smaller phones render a 720p-class scene, and one with at most 4 GiB selects Low Power. Known renderer limits can still choose Minimum. Discrete GPUs with at most 512 MiB of verified dedicated memory select Minimum, regardless of platform hints.
- Discrete GPUs are placed by their **verified active-device dedicated memory**, with 16 GiB RAM and modern effects. The reference 8 GB laptop GPU holds Quality at 100 FPS and Ultra at 75 FPS at 1440p, and a larger card is at least that fast, so each memory class takes the preset above; each resolution class above 1440p costs one preset:

  | Dedicated memory | Up to 1440p | Up to 4K | Above 4K |
  |---|---|---|---|
  | 8 GB | Enhanced | Balanced | Performance |
  | 12 GB | Quality | Enhanced | Balanced |
  | 16 GB | Ultra | Quality | Enhanced |
  | 24 GB or more | Ultra | Ultra | Quality |

  A class starts at 15/16 of its nominal size, because drivers report slightly less than the card's label. Quality and Ultra also need eight logical CPUs. Smaller dedicated-memory budgets select Performance or Balanced, and fewer than four logical CPUs lower any recommendation to Performance.
- A resolution class is named by its 16:9 size and includes the taller shapes beside it, so 1920x1200 counts as 1080p and 2560x1600 as 1440p. A 3440x1440 ultrawide has a third more pixels than 2560x1440 and counts as the 4K class.
- Unknown, implausible or unavailable observations never manufacture extra VRAM. Drivers without active-device memory reporting conservatively select Performance; choose a higher preset manually after checking gameplay. Shared RAM is not counted as dedicated VRAM, and another adapter's memory is not used on hybrid systems. Supported SDL3 builds also keep failed system-memory queries unknown instead of guessing a small RAM size.
- Detection before renderer initialization leaves settings unchanged. Resolution, fullscreen policy, refresh, VSync, backend selection, input and interface preferences remain yours. Preset FPS caps are power/pacing ceilings, not performance guarantees.
- An automatic recommendation caps the frame rate at the refresh rate of the display the window is on, in whole hertz (a 59.94 Hz display caps at 60), so a fresh installation never renders frames the display cannot show. When the refresh rate is unknown or outside 30-1000 Hz, the chosen preset's own cap applies. A power-limited device (a phone, Steam Deck, Raspberry Pi or an explicit low-power request) never caps above its preset: a 120 Hz phone stays at Performance's 60. Selecting a preset by name always uses its table cap.

Auto-Detect can lower the **scene render scale** below the preset's table value
on large displays. For example, shared-memory graphics at 1440p use Performance
at 75%, and at 4K use Performance at 50% (a 1080p scene). The window and UI keep
their native resolution. Minimum, Low Power, Low and most phones use smaller
scene budgets (a 1080p display renders 1440x810 on Low); verified discrete
memory permits larger ones. Automatic scale
uses the existing menu steps and never exceeds the chosen preset's scale.
Selecting a named preset manually restores its table values. VR headset scale
remains a separate [VR setting](vr.md); desktop detection is not a headset
performance qualification.

The stock four-choice quality controls and `execMachineSpec` now use these same
recipes: Low maps to Low, Medium to Performance, High to Balanced, and
Ultra to Ultra. Stock **Recommended** runs Auto-Detect even if the old quality
classification has not changed. These paths preserve your display mode and
VSync preference.

On a fresh installation, openQ4 starts with a small bootstrap profile, observes
the initialized GPU and applies the same automatic recommendation before entering
gameplay. The console's `Quality profile:` line reports the bootstrap preset as a first launch, then prints the detected profile, including the measured video memory, once detection has applied it. A named preset supplied at launch takes precedence. Existing saved
settings are preserved, including when their old detection marker is missing
or their legacy `com_machineSpec` classification is unset.

For package or platform validation, `performancePresetSelfTest` checks that the preset commands are registered, every preset is known to the menu-facing cvar and command completion lists, auto-detect returns and applies a supported preset other than Maximum, command arguments behave correctly, all preset targets are registered and covered by backup/restore, rejected target normalization rolls back atomically, the preset progression stays coherent, the preset cvar mappings apply correctly, and the test restores touched cvar values, flags, and modified-state bookkeeping before finishing.

## Core Display Settings

| Setting | Default | What it does |
|---|---:|---|
| `r_fullscreen` | `1` | `1` = fullscreen, `0` = windowed. |
| `r_fullscreenDesktop` | `1` | `1` = native desktop fullscreen (recommended). `0` = exclusive fullscreen using `r_mode`/`r_custom*`. |
| `r_borderless` | `1` | Borderless window mode when `r_fullscreen 0`. |
| `r_windowWidth` | `1280` | Windowed width. |
| `r_windowHeight` | `720` | Windowed height. |
| `win_xpos` | (auto) | Outer window-frame X position (updated automatically when you move the window). |
| `win_ypos` | (auto) | Outer window-frame Y position (updated automatically when you move the window). |
| `r_mode` | `-2` | Fullscreen sizing selector (`-2` = desktop native/current display, `-1` = custom, `0+` = legacy preset index). |
| `r_customWidth` | `1920` | Custom exclusive-fullscreen width used when `r_mode -1`. |
| `r_customHeight` | `1080` | Custom exclusive-fullscreen height used when `r_mode -1`. |
| `r_displayRefresh` | `0` | Exclusive fullscreen refresh rate in whole hertz. `0` (Auto) keeps your desktop's refresh rate when the resolution offers it, otherwise the nearest rate it does offer. |
| `r_screen` | `-1` | SDL3 monitor target (`-1` auto/current, `0..N` explicit index). |

## Texture Quality (Picmip and Downsizing)

openQ4 has two independent ways to spend less memory and bandwidth on textures.

**`image_downSize*`** is retail Quake 4's ceiling: no texture of that kind is
allowed to exceed a given size. Performance presets drive these.

**`image_picmip`** is the Quake 3 style relative reduction familiar from
`r_picmip`: it drops whole mip levels, so every step halves a texture no matter
how large it started. Unlike the Quake 3 version, openQ4 applies it **only to the
diffuse layer of materials** â€” the color texture of a `diffusemap` stage. Normal
maps, specular maps, lighting, skies, decals, fonts, and every 2D/HUD surface
keep their authored resolution, so surfaces stay correctly lit and the interface
stays sharp while the bulk of texture memory still comes down.

| Setting | Default | What it does |
|---|---:|---|
| `image_picmip` | `0` | Mip levels to drop from diffuse textures. `0` = full resolution. Each step halves the texture. |
| `image_picmipFilter` | `1` | Which image paths `image_picmip` may reduce. `0` = every diffuse texture, `1` = `textures/*` (world surfaces), `2` = `models/*` (characters, weapons, props), `4` = any other namespace. Add values to combine. |
| `image_picmipMinSize` | `32` | Reduction stops once a texture's longest axis reaches this size, so small textures never turn to mush. |
| `image_downSize` | `0` | Enables the general texture size ceiling. |
| `image_downSizeLimit` | `0` | That ceiling, in pixels. `0` = no limit. |
| `image_downSizeBump` / `image_downSizeBumpLimit` | `0` / `256` | Separate ceiling for normal maps. |
| `image_downSizeSpecular` / `image_downSizeSpecularLimit` | `0` / `64` | Separate ceiling for specular maps. |

The ceiling is applied first and `image_picmip` reduces from there, so raising
`image_picmip` always halves the result even when a preset has already clamped a
texture. A material that declares `nopicmip` opts out of both.

These settings change the pixels a texture is built from, so openQ4 reloads
images automatically when you change one â€” no `vid_restart` needed. The generated
texture cache is keyed by the active reduction, so switching back and forth does
not leave stale sizes behind.

One limitation applies to DDS replacement packs. Compressed data can only be
reduced by discarding mip levels the file already contains, so a `.dds` exported
without a full mip chain stays larger than the same texture would on the normal
path. Set `image_showPrecompressedTextures 1` to have openQ4 name any replacement
that could not reach the requested size.

```
image_picmip 2
image_picmipFilter 3
```

That halves world and model diffuse textures twice while leaving lighting detail,
the HUD, and menus untouched.

## Texture Sampling

`image_filter` controls how ordinary material textures are sampled. It is saved
in your configuration and updates loaded textures immediately, without a
`vid_restart`. Material stages that explicitly request linear or nearest
sampling keep their authored behavior. That protects explicitly filtered fonts,
HUD elements, shadow data, and renderer-internal textures from the global
choice.

| Value | Texels | Mip levels | Typical use |
|---|---|---|---|
| `GL_LINEAR_MIPMAP_LINEAR` | Linear | Smooth blend | Default, smoothest general-purpose sampling. |
| `GL_LINEAR_MIPMAP_NEAREST` | Linear | Nearest level | Smooth texels with sharper mip transitions. |
| `GL_NEAREST_MIPMAP_NEAREST` | Nearest | Nearest level | Pixelated textures with stable distance scaling. |
| `GL_NEAREST_MIPMAP_LINEAR` | Nearest | Smooth blend | Pixelated texels with softer distance transitions. |
| `GL_NEAREST` | Nearest | Disabled | Fully point-sampled; may shimmer in the distance. |
| `GL_LINEAR` | Linear | Disabled | Bilinear sampling without mip levels. |

For a pixelated texture style, enter:

```
seta image_filter GL_NEAREST_MIPMAP_NEAREST
```

Nearest-texel modes automatically avoid anisotropic filtering so
`image_anisotropy` cannot blur the requested pixel edges. The same values work
with the OpenGL and Vulkan renderers.

## Renderer Backend (automatic selection; Vulkan stable on Windows)

Fresh desktop configurations use **Automatic**, which prefers Vulkan with
OpenGL fallback and a compatible high-performance GPU. On Windows x64
the **Vulkan renderer is stable and supported**;
on Linux and macOS it remains experimental. It draws the stock game with OpenGL's effects: world
and model materials, including the stock heat-haze, glass, water, and other
program effects, interaction lighting, baked light grids, stencil and mapped
shadows, fog, decals, soft particles, GUIs, and cinematics, plus brightness
and gamma, MSAA alpha-to-coverage, the classic post-processing chain, cel
shading, the underwater view, multiplayer player outlines, and the `r_show*`
debug views, `r_showShadows` included. Mods' own material programs run too:
GLSL programs compile as written, and ARB assembly programs are translated to
GLSL. `r_enhancedMaterials`, the sharpened and nearest `r_resolutionScaleMode`
upscales and the experimental colored translucent shadows
(`r_shadowMapTranslucentMoments`) work as they do on OpenGL. On the development
laptop, a Windows machine with an NVIDIA RTX 4060 and Intel Iris Xe graphics,
all 29 stock single-player maps load and draw their opening view, and 20 stock
multiplayer maps host matches with bots, clean under the Vulkan validation
layers on both GPUs. That real-hardware evidence is why it is supported on
Windows. AMD graphics have not been tested yet, so please report problems on
them. On Linux and macOS it stays experimental until hardware runs there exist;
Linux CI runs software-Vulkan startup, render-target, recovery and fallback
checks on every push. The differences under
[What Vulkan does not do yet](#what-vulkan-does-not-do-yet) remain, and OpenGL
remains available on desktop systems. Existing explicit OpenGL choices stay on
OpenGL; changing the default does not extend physical platform qualification.
While [VR](vr.md) is on, Automatic uses OpenGL; set `r_renderApi vulkan` to
play VR on Vulkan.

| Setting | Default | What it does |
|---|---:|---|
| `r_renderApi` | `best` (desktop), `gles` (Android) | Renderer backend. Desktop Automatic tries Vulkan with OpenGL fallback; Android uses OpenGL ES. Vulkan is **supported on Windows, experimental on Linux and macOS**. Takes effect on **engine restart**, not `vid_restart`. |
| `r_renderDevice` | `auto` | Compatible high-performance GPU preference, or a 32-character lowercase Vulkan device ID. A named device requires Vulkan. Applied on engine restart. |
| `r_actualRenderApi` | (read-only) | Reports the backend that actually initialized. If loading or initializing Vulkan fails at startup, the engine **falls back to OpenGL** and this reports `gl`. |

### All `r_renderApi` values

| Value | Aliases | What it selects |
|---|---|---|
| `best` | - | Vulkan with OpenGL fallback on desktop; OpenGL ES on Android. |
| `gl` | `opengl` | OpenGL with operating-system/driver GPU routing. |
| `vulkan` | `vk` | The Vulkan renderer module: supported on Windows, experimental on Linux and macOS. |
| `gl-module` | â€” | Always loads the OpenGL renderer as a module instead of using a statically linked copy. This is a diagnostic option; it renders identically to `gl`. |
| `gles` | - | OpenGL ES renderer module; the Android default. |

Anything else is rejected with a warning, and openQ4 uses the platform's
automatic selection.

Notes:

- The `vulkan` selection is archived to your config and applied at the next
  engine start; restart openQ4 fully (not just `vid_restart`) to switch.
- Before it switches to Vulkan, openQ4 loads the Vulkan renderer module and
  tries to create a Vulkan device with it. If the module is missing, there is no
  Vulkan driver, no GPU meets the renderer's Vulkan 1.3 feature floor, or the
  device cannot be created, openQ4 logs a warning and renders with OpenGL.
  `gfxInfo` gives the reason on its `Renderer API fallback reason` line, and
  `r_actualRenderApi` reports `gl`. Your `vulkan` selection is kept, so the next
  launch tries Vulkan again, for example after a driver update.
- Startup failures while creating the window, Vulkan surface, swapchain, or
  mandatory renderer resources also recover to OpenGL in the same launch.
  openQ4 releases the failed startup's renderer objects before loading OpenGL,
  and keeps the failure reason in the log. Your Vulkan selection is preserved.
- If the GPU or driver resets during play (Vulkan reports a lost device),
  openQ4 restarts the renderer by itself and carries on with the same game.
  It allows three such restarts per session (`r_vkPresentationRecoveries`).
  If presentation keeps failing after that, openQ4 stops with an error and
  saves `r_renderApi gl` for the next launch. A window whose surface is lost,
  for example after a display change, gets a new surface without a restart.
- A device failure during a later full `vid_restart` still stops openQ4 and
  saves `r_renderApi gl` for the next launch. Switching renderer modules during
  gameplay requires a full engine restart. If you supplied
  `+set r_renderApi vulkan`, remove that launch option to select OpenGL.
- Vulkan reports are welcome. Check the list below first, then include
  `openq4.log`, `gfxInfo`, and your GPU and driver version.

### What Vulkan does not do yet

- **Some mod material programs.** Mods' GLSL and ARB programs run, but a
  program that relies on state a material stage does not have is skipped and
  logged once: fixed-function lighting, fog or texture-generation state,
  program environment values the engine never sets for material stages,
  ARB `RECT`, `1D` or `3D` textures, the `ARB_fog_*` options, or NVIDIA program
  options. OpenGL would draw those with leftover state rather than a defined
  result.
- **Clustered decal routing.** `r_rendererClusteredDecals` only changes how
  OpenGL's modern path routes decals, not how they look; Vulkan draws the same
  decals, so the setting changes nothing there. (PBR materials are not on this
  list: both renderers draw them by the same rules; see
  [PBR materials](pbr-materials.md).)
- **Pixel readbacks in the debug views.** A Vulkan frame cannot stop halfway
  to read pixels back, so the overdraw averages that `r_showLightCount 3` and
  `r_showShadowCount 2` to `4` print arrive a frame or two late, and
  `r_showIntensity` and `r_showDepth` recolour a copy of the view instead.
  `r_showDepth` shows depth as grey, where OpenGL writes the raw depth bits as
  colours.

### Vulkan on macOS (through MoltenVK)

Apple does not ship a Vulkan driver. On macOS, openQ4's Vulkan renderer runs on
top of **MoltenVK**, a Vulkan-on-Metal translation layer that is bundled inside
both macOS packages. It is a translation layer, not a Metal renderer, and it
does not replace or remove the OpenGL renderer.

- Fresh macOS configurations use Automatic: Vulkan through MoltenVK when it
  meets the renderer requirements, with OpenGL fallback. Saved OpenGL selections
  stay on OpenGL. Vulkan's experimental qualification is unchanged.
- Vulkan is a **runtime option, not a separate download**. There is no third
  macOS package to install and nothing to enable at install time.
- Choose Vulkan in the System menu and Apply, then Restart Renderer from the
  main menu; or set `r_renderApi vulkan` in the console and relaunch openQ4.
  Check the active renderer readout or `gfxInfo` to confirm what started.
- **Expect problems.** The Vulkan renderer is experimental on macOS,
  and on macOS only one player has reported it working (openQ4 0.12.0 on an
  M4 Max MacBook Pro); nothing has checked its output against OpenGL there.
  Missing effects, wrong shading, poor performance, or a refusal to start are
  all plausible.
- **To go back:** run `r_renderApi gl` and restart. The setting is saved to your
  config, so it stays on OpenGL after that.
- **If it cannot start**, openQ4 renders with OpenGL instead and logs why, and
  `gfxInfo` shows the reason. Common reasons on a Mac are a GPU that does not
  meet the renderer's Vulkan 1.3 feature floor, or a package whose bundled
  translation layer is missing or was stripped by a copy. Window, surface,
  swapchain and mandatory-resource failures during startup also fall back to
  OpenGL. If a later failure prevents recovery, start it once with
  `+set r_renderApi gl`, for example from Terminal with
  `/Applications/openQ4.app/Contents/MacOS/openQ4 +set r_renderApi gl`.
- When reporting a macOS Vulkan problem, include `openq4.log` (it records which
  translation-layer library was loaded), the `gfxInfo` output, and your Mac
  model and macOS version.

## Frame Cap

| Setting | Default | What it does |
|---|---:|---|
| `com_maxfps` | `240` | Presentation frame cap (`0` = uncapped). |
| `com_steamDeckAutoFrameCap` | `1` | When the Steam Deck profile is active, applies a Deck-friendly cap only while `com_maxfps` is still `240`. |
| `com_steamDeckFrameCap` | `0` | Steam Deck default cap override (`0` = use detected display refresh clamped to the Deck-oriented range). |
| `com_preciseFrameWait` | `1` | `1` waits out the cap on a high-resolution timer and busy-waits only as long as that timer needs; `0` restores the older whole-millisecond wait. |
| `com_showFramePacing` | `0` | `1` shows frame times (to 0.01 ms), the display's refresh rate and what limits the frame rate; `2` also logs them. With a 240 cap it shows whether pacing meets the 240 Hz target. |

The Steam Deck auto cap preserves custom `com_maxfps` values. Set `com_steamDeckAutoFrameCap 0` to opt out, or set `com_maxfps` directly for a fixed cap.

A minimized or hidden window draws at most 60 frames a second, whatever the
cap, so the game does not run flat out while you cannot see it. With vsync on
and a variable-refresh display (G-Sync, FreeSync), cap a few frames under the
refresh rate (for example `com_maxfps 237` on a 240 Hz display) so frames
never wait for the next refresh.

### Smooth motion between game ticks

Quake 4's game runs 60 times a second. Above 60 FPS, openQ4 draws the camera,
your weapon, moving objects, ragdolls, flying debris, cutscene and security
cameras, third person and the death camera between those ticks, so motion stays
smooth instead of stepping. Gameplay, networking, saves and demos keep their
original timing.

| Setting | Default | What it does |
|---|---:|---|
| `g_presentationInterpolation` | `1` | Draw the world and alternate views between game ticks. `0` returns everything to the 60 Hz tick. |
| `in_presentationView` | `0` | **Late look (opt-in).** Turns the view with your mouse, gyro, look keys and stick on every drawn frame, instead of up to a tick later. Your weapon, its muzzle flash and its lights turn with the view. Shots, hit detection and the network still use the game's own 60 Hz aim. |
| `com_latencyFlash` | `0` | **Latency measurement aid.** Draws a white bar down the left edge on the first frame after you press fire, for click-to-photon sensors such as NVIDIA LDAT or OSLTT. Leave it off for normal play. |

Late look stands down, and the view behaves exactly as before, while mouse
smoothing or filtering is on (`m_smooth` above `1`, `m_filter` above `0`), in a
menu, the weapon or quick-chat wheel, a vehicle, a cutscene or camera view,
third person, VR, while dead, or while playing back a recorded demo. Tracers,
impacts and projectiles still start where the shot actually left, so on very
fast turns they can appear a few units from the drawn barrel for their first
frame; that gap is smaller than without late look.

## Anti-Aliasing Settings (New)

| Setting | Default | What it does |
|---|---:|---|
| `r_multiSamples` | `0` | MSAA sample count for the main scene render target (`0`, `2`, `4`, `8`, `16`; `0` = off). |
| `r_postAA` | `0` | Post AA mode (`0` = off, `1` = SMAA medium, `2` = SMAA high, `3` = SMAA ultra, `4` = color-edge prototype). |
| `r_msaaAlphaToCoverage` | `1` | Enables alpha-to-coverage for perforated/alpha-tested materials when MSAA is active. Helps foliage/fences look cleaner. |
| `r_msaaResolveDepth` | `0` | Also resolves depth during MSAA resolve. Usually leave this off unless debugging a depth-dependent edge case. |

`r_multiSamples` value guide:
- `0`: disabled (fastest, most aliasing).
- `2`: low-cost MSAA uplift for modest GPUs.
- `4`: recommended default quality/performance balance.
- `8`: high quality, noticeably higher GPU cost.
- `16`: enthusiast/high-end setting where supported.
- `1` usually provides no meaningful benefit and is not recommended.

`r_postAA` value guide:
- `0`: disabled.
- `1`: SMAA medium; luma-edge detection with a `0.10` threshold and 8-step search, recommended as the post-AA default.
- `2`: SMAA high; luma-edge detection with the same `0.10` threshold and a 16-step search.
- `3`: SMAA ultra; luma-edge detection with a lower `0.05` threshold and a 32-step search.
- `4`: color-edge prototype; color-edge detection with a `0.10` threshold and a 16-step search for comparison captures.

Notes:
- `r_multiSamples` is hardware-limited and may be clamped by the driver/GPU.
- Unsupported `r_multiSamples` values are normalized to the supported ladder before video startup (`1` becomes off; odd/intermediate values step up to `2`, `4`, `8`, or `16`).
- On SDL3 builds, video startup retries lower MSAA requests if the window or GL context rejects the requested sample count (`16 -> 8 -> 4 -> 2 -> off`) and logs the requested, selected, and driver-reported multisample attributes.
- The game scene target is validated separately from the window framebuffer. If a driver rejects the RGBA8 + depth/stencil offscreen target at the selected sample count, openQ4 retries lower samples down to `0`; if even the single-sample target is unavailable, it keeps running through the direct-render fallback and logs the exact framebuffer status and attachment details.
- `gfxInfo` reports the active AA summary, including requested/effective MSAA, `GL_MAX_SAMPLES`, alpha-to-coverage, post AA mode, screen fraction, and supersampling state. Once a map is drawing, the effective MSAA count is the one the 3D scene actually rendered with, so after an `r_multiSamples` change it keeps showing the old count until `vid_restart` or the next map load applies the new one.
- On Vulkan, MSAA applies to the 3D scene, not the window, and stops at 8x: `16` renders at 8x, or at the highest count your GPU supports. `gfxInfo` shows the count in use. GPUs that can move their sample positions per pipeline place them where OpenGL does; the others, such as Intel integrated graphics, keep their own standard pattern, which can shift edge antialiasing by a fraction of a pixel.
- On a Mac using OpenGL's 2.1 compatibility mode, offscreen render targets cannot use MSAA, so MSAA reaches the 3D scene only while it is drawn straight to the window. Temporal AA, bloom, tone mapping, SSAO, motion blur, resolution scaling and the other post-processing effects each route the scene through an offscreen target, and MSAA then no longer applies. Post AA cannot run in that mode and leaves the scene on the window, so the `balanced`, `enhanced`, `quality` and `ultra` presets, which turn it on, keep their MSAA there. `gfxInfo` shows which route the scene took: `reason=default-framebuffer` with the window's sample count, or `effective=0 reason=texture-msaa-unavailable`.
- The Post AA startup/runtime log records the active SMAA edge mode, threshold, search steps, and local contrast scale so quality captures can be compared without guessing which shader contract was active.
- Changing `r_multiSamples` should be followed by `vid_restart`.
- `r_postAA`, `r_msaaAlphaToCoverage`, and `r_msaaResolveDepth` can be changed at runtime, but a `vid_restart` is still safe if behavior looks stale.

## Motion Blur

Motion blur (`r_motionBlur 1`, on in the `ultra` preset) smears the scene along its own motion: the camera turning or moving, and moving characters, vehicles and objects. It keeps its own short history of the camera, so it works the same with or without temporal AA, resolution scaling, supersampling, MSAA, bloom, tone mapping and SSAO, on OpenGL and Vulkan.

- **The same blur at any frame rate.** The blur length follows how fast things move, not how many frames you draw: by default it matches the motion of one 60 Hz frame (`r_motionBlurShutterMsec 16.667`), so 144 Hz and uncapped play look like 60 Hz play. `r_motionBlurShutterMsec 0` restores the original behaviour, where blur shrinks as the frame rate rises.
- **Your weapon stays sharp.** The view weapon only blurs with its own animation (sway, recoil, reloads), never because you turn. Skinned characters blur with their animation as well as their movement. `r_motionBlurObjectVectors 0` blurs by camera motion alone.
- **No blur across cuts and pauses.** Cinematic camera cuts, loading a map, `vid_restart`, resizing and teleports start a fresh history instead of smearing between unrelated views. Pausing stops the blur. Mirrors, portals, security-camera screens and portal skies never interrupt it.
- **Below 10 FPS it switches off.** A frame that takes longer than 100 ms is treated as a discontinuity, so very slow frames are left sharp rather than smeared.
- `r_motionBlurStrength` (0.45), `r_motionBlurMaxPixels` (10) and `r_motionBlurSamples` (8, 16 on `ultra`) tune the length multiplier, the longest smear and its quality. `r_motionBlurDebug 1` shows the motion vectors instead of the blur.
- `rendererMotionBlurStatus` prints what motion blur actually did: how many views it drew, why any were skipped, whether temporal AA's jitter was cancelled, and the strength, length and sample count in use. `rendererMotionBlurStatus reset` clears the counters.
- Motion blur needs an offscreen scene target, so on a Mac using OpenGL's 2.1 compatibility mode it turns MSAA off for the 3D scene (see the MSAA note above). On Vulkan, MSAA stops at 8x with or without motion blur.

## Multiplayer Visibility Effects

These optional client-side cvars add player outlines, rim lighting, and bright skins in multiplayer. Defaults keep the effects off; set one or more strength values above `0` to enable them. They do not change hit detection, snapshots, or server authority.

All of them are exposed in `Multiplayer -> Settings -> Appearance`, under the **Enemy** and **Teammate** tabs, with a live preview of the selected model. Colors and outline width are preset dropdowns there; the cvars accept any value in range if you prefer to set them from a config.

| Setting | Default | What it does |
|---|---:|---|
| `cl_player_outline_enemy` | `0` | Enemy player outline strength (`0..1`), used as the outline opacity. |
| `cl_player_outline_team` | `0` | Teammate player outline strength (`0..1`). Teammate outlines ignore depth so allies stay readable through geometry. |
| `cl_player_outline_width` | `2.0` | Outline width in screen pixels (`0.5..6.0`). |
| `cl_player_rimlight_enemy` | `0` | Enemy player rimlight strength (`0..1`). |
| `cl_player_rimlight_team` | `0` | Teammate player rimlight strength (`0..1`). |
| `cl_player_visibility_enemy_color` | `1 0.12 0.05` | Enemy outline/rimlight RGB color, using float components. |
| `cl_player_visibility_team_color` | `0.1 0.85 0.25` | Teammate outline/rimlight RGB color, using float components. |
| `cl_player_brightskin_enemy` | `0` | Enemy player bright skin strength (`0..1`). |
| `cl_player_brightskin_team` | `0` | Teammate player bright skin strength (`0..1`). |
| `cl_player_brightskin_enemy_color` | `1 0.05 0.02` | Enemy bright skin RGB color, using float components. |
| `cl_player_brightskin_team_color` | `0.05 1 0.22` | Teammate bright skin RGB color, using float components. |

Two renderer cvars shape the rimlight itself. They are not in the menu, and their defaults reproduce the fixed falloff the pass used before they existed, so an untouched config looks the same:

| Setting | Default | What it does |
|---|---:|---|
| `r_playerRimlightPower` | `2.0` | Rimlight falloff exponent (`0.25..8.0`). Higher tightens the band to the silhouette; lower spreads it across the body. |
| `r_playerRimlightFloor` | `0` | Rimlight floor (`0..1`). Lifts the whole body by this fraction of the rim strength, so a player facing you head on is still tinted. At `1` the rimlight becomes a flat additive wash. |

Example:

```cfg
seta cl_player_outline_enemy 0.85
seta cl_player_rimlight_enemy 0.5
seta cl_player_outline_team 0.45
seta cl_player_rimlight_team 0.25
seta cl_player_outline_width 2.0
```

Notes:
- The outline is a shell drawn just outside the player silhouette and masked against the silhouette itself, so it stays a constant-width ring at any distance instead of tinting the whole body. Overlapping body, head, and weapon surfaces are each painted once, so the outline does not darken where they meet.
- The ring holds its requested pixel width in every direction, including on ultrawide displays.
- Only the **outline** is ever drawn through geometry. Teammate outlines ignore depth and are drawn across the whole level, so an ally anywhere on the map reads as a ring at their position. The rimlight and the bright skin stay depth tested in every case â€” a ring marks a position, while a shaded body seen through a wall is a different thing entirely.
- See-through and depth-tested outlines are masked separately, so an ally behind geometry never erases the ring off an enemy standing in front of them.
- The rimlight needs GLSL support (`glprogs/player_rimlight.*`). On a driver without it the rimlight is skipped and the console says so once; outline and bright skin still work.
- Without GLSL the outline falls back to a scaled shell, which approximates the requested pixel width instead of matching it exactly.
- The overlays are drawn after the ambient floor and light-grid passes, so `r_forceAmbient` and indirect lighting no longer wash them out.
- `r_skipPlayerVisibilityEffects 1` disables all three overlays engine-side, for clean captures or A/B comparisons.
- Bots are ordinary players for this feature: they occupy real client slots, so they receive the same overlays as human opponents and teammates. Nothing in the decision keys on being a bot.
- `g_showPlayerVisibilityEffects 1` prints one line per player whenever the answer changes â€” whether the overlays were applied and which render entities were reached (body, head, world weapon), or the reason they were skipped, and whether the client is a bot. Use it when a strength cvar looks like it is doing nothing. Steady states print once, not every frame.

## Multiplayer Pickup and Opponent-Weapon Style

`Settings -> Game Options -> General` includes two client-side presentation
controls for multiplayer. Stock appearance remains the default, and neither
control changes game state or what anybody else sees.

| Setting | Default | What it does |
|---|---:|---|
| `g_simpleItems` | `0` | Pickup style: `0` original models, `1` legacy simple icons, `2` icon-coloured flat diffuse, or `3` flat diffuse with a soft light sweep moving up each world item. |
| `g_mpFlatOpponentWeapons` | `0` | `1` gives weapons held by opponents the matching icon-coloured flat diffuse. Held weapons never receive the moving sweep. |

Flat pickup styles change only the diffuse colour layer of a model. Authored
normal maps, specular highlights, effects, emissive/ambient layers, and alpha
coverage remain intact; transparent shells such as the bubbles around health
pickups therefore keep their original appearance. Weapon and ammo pickups in
the world are items, so style `3` gives them the upward sweep too.

The first-person view weapon is always excluded, even when opponent-weapon
colour is enabled. In team modes the optional held-weapon colour applies only
to the opposing team; in free-for-all it applies to every other player.
Changes take effect immediately without reconnecting.

## Resolution Scale

| Setting | Default | What it does |
|---|---:|---|
| `r_screenFraction` | `100` | Main-scene resolution scale percentage (`10..200`). Values below `100` reduce scene resolution for performance; values above `100` supersample the scene before resolving it back to the native back buffer. |
| `r_resolutionScaleMode` | `1` | How a scene below `100%` is stretched to your screen: `1` smooth (bilinear), `2` sharpened, `3` nearest-neighbour, a crisp pixelated look, or `0` the legacy cropped viewport for fill-rate testing, which draws the world in the bottom-left corner without stretching it in multiplayer (the campaign stretches its scene to fill the screen). The HUD and menus stay at native resolution in every mode. |
| `r_resolutionScaleSharpness` | `0.4` | Sharpening strength for mode `2` (`0..1.5`). |

The Display menu exposes curated presets: `10%`, `25%`, `50%`, `75%`, `85%`, `100%`, `125%`, `150%`, and `200%`.

Modes `2` and `3` apply in the campaign and in multiplayer, on OpenGL and Vulkan.
With modes `1` and `2`, a scene below `100%` is upscaled by temporal anti-aliasing
by default (`r_temporalAA 2`), which keeps edges smooth and recovers detail a plain
stretch loses; see [Temporal AA](temporal-presentation.md). Modes `0` and `3` keep
their own look. The experimental screen-space effects do their own upscale, so with
them on every mode looks like mode `1`.

## Fullscreen Policy (Desktop vs Exclusive)

- Default behavior is **desktop-native fullscreen** (`r_fullscreenDesktop 1`): fullscreen matches your current desktop resolution and does not change Windows display mode.
- For **exclusive fullscreen** (explicit mode switch), set `r_fullscreenDesktop 0`. In this mode, `r_mode`/`r_customWidth`/`r_customHeight` control the requested fullscreen resolution.
- In `Settings -> System`, **Display Resolution** lists Desktop Native first, then only the fullscreen preset resolutions SDL3 reports for the selected display, followed by Custom. Preset entries include compact aspect labels such as `16:9` or `21:9`. Choosing an unusual reported mode writes exact custom width/height when no legacy `r_mode` index exists.
- The console `listModes` command shows the expanded legacy `r_mode` preset catalog for configs and command-line use, covering common desktop, laptop, ultrawide, HiDPI, 4K, 5K, 6K, and 8K resolutions. The Settings dropdown still hides static presets that the selected display does not report.
- **Refresh Rate** lists Auto plus SDL3-reported refresh rates for the currently selected fullscreen resolution. Auto keeps your desktop's refresh rate, or the nearest rate that resolution offers, and openQ4 picks the same rate for the same resolution at startup, after `vid_restart` and after Alt+Enter. Leave it on Auto unless you specifically need an exclusive-mode refresh request. On macOS, Auto now uses the desktop rate instead of the display's highest rate; this has been checked in automated tests, not yet on a Mac.
- On Windows, fullscreen windows minimize on focus loss so system UI such as Alt+Tab and the Snipping Tool overlay can take foreground cleanly.
- On Windows, `PrintScreen` yields to the system snipping UI by default (`win_printScreenToSystemTool 1`). Use `F12` for the built-in openQ4 screenshot command, or set that cvar to `0` if you explicitly want `PrintScreen` available for in-engine binds again.

Notes:
- When `r_fullscreenDesktop 1`, `r_mode` and `r_custom*` are ignored for fullscreen sizing (they still exist for legacy configs and exclusive mode). Use `r_screenFraction` for below-native scaling or supersampling while staying in desktop-native fullscreen.
- Use `listDisplayModes` to see what your monitor actually supports in exclusive mode. On SDL3/Wayland, display diagnostics also report scale, orientation, pixel density, and exact refresh details that help diagnose compositor scaling behavior.

## Windowed Sizing and Placement

- `Settings -> System -> Display Sizing` exposes `r_windowWidth`, `r_windowHeight`, `r_customWidth`, `r_customHeight`, and `r_displayRefresh`. Leaving Refresh Rate on `Auto` writes `r_displayRefresh 0`.
- New Windows installs, and legacy Windows configs migrated from the old default, use borderless windowed presentation when `r_fullscreen 0` to avoid OpenGL bordered-window frame pacing stalls. Set `r_borderless 0` and run `vid_restart` if you specifically want a resizable bordered window.
- When bordered windowed mode is active (`r_fullscreen 0`, `r_borderless 0`), resizing updates `r_windowWidth`/`r_windowHeight` automatically.
- Moving the window updates `win_xpos`/`win_ypos` automatically. These coordinates describe the outer frame, including the title bar and resize borders, while `r_windowWidth`/`r_windowHeight` continue to describe the drawable client area.
- When switching fullscreen -> windowed, openQ4 restores the last remembered windowed size/position (it should not come back as a fullscreen-sized window).
- If you unplug/rearrange monitors and the saved window position becomes off-screen, openQ4 will recover by clamping/recentering the complete window frame back onto a valid display.
- If you set `r_screen` to an explicit display index (`0..N`), the complete window frame is constrained to that display's usable area. If necessary, the client area is reduced so the title bar and resize borders also fit. With `r_screen -1`, placement is respected unless it becomes invalid/off-screen.
- Native Wayland compositors own absolute placement. openQ4 requests the selected display and client size there, then accepts the compositor's decorated-window placement instead of persisting unavailable global coordinates.
- SDL3 tip: hold `Shift` while resizing to snap the window aspect ratio to common targets (4:3, 16:9, 16:10, 21:9, etc.).

## Aspect Ratio and FOV

- `r_aspectRatio` is **deprecated/ignored**. Aspect ratio and FOV behavior are derived automatically from the current render size, so the game follows any aspect ratio without manual selection.
- The Display menu no longer exposes a manual Aspect Ratio selector; Display Resolution and the live window size drive aspect behavior automatically.
- Weapon gameplay zoom uses the same gameplay FOV conversion path as normal view FOV, so authored weapon zoom values keep consistent framing/magnification across aspect ratios.
- In multiplayer, zoomed first-person view suppresses view bob while scoped so reticle tracking stays stable during movement.
- Scope GUI yaw tracking for zoom overlays follows the weapon/player view axis path, improving scope alignment while turning.

## View Weapon FOV and Placement (New)

These settings control first-person viewmodel rendering (the weapon on screen). They are client-side tuning controls and are not gameplay/network authority cvars.

The in-game menu exposes these under `Settings -> Game Options -> View Weapon`.

| Setting | Default | What it does |
|---|---:|---|
| `cl_gunfov` | `0` | View-weapon FOV override (`0` = follow current view FOV). |
| `cl_gunfov_adjust` | `1` | Aspect policy for `cl_gunfov`: `1` keeps classic 4:3-style weapon framing across screen ratios, `0` uses direct viewport-based FOV conversion. |
| `cl_gun_x` | `0` | Client weapon X/right offset. |
| `cl_gun_y` | `0` | Client weapon Y/forward offset. |
| `cl_gun_z` | `0` | Client weapon Z/up offset. |

Notes:
- `cl_gunfov` values above `0` are clamped to a safe range internally for weapon projection.
- Weapon projection is handled in renderer weapon-depth path, so narrow/wide aspect changes are handled consistently.
- `cl_gun_x/y/z` are additive with legacy `g_gunX/Y/Z` offsets. Prefer `cl_gun_*` for user config.
- openQ4's legacy baseline keeps `g_gunX` at `1` and `g_gunZ` at `-1` so the default widescreen viewmodel framing stays out of the viewport edge.

## UI Aspect Correction (New)

This controls 2D UI layout behavior (menu, HUD, console, loading/initializing screens):

The in-game menu exposes this as `Settings -> System -> Display Sizing -> UI Aspect`.

| Setting | Default | What it does |
|---|---:|---|
| `ui_aspectCorrection` | `1` | `1` keeps classic 4:3-style correction for all 2D UI. `0` stretches 2D UI to the full 2D draw region. |

## Television Viewing (Couch Profile)

Playing on a television from across the room? Launching openQ4 from Steam's Big
Picture mode selects the Couch profile automatically (a Steam Deck keeps its own
profile). On a fresh configuration it turns controller input on and starts the
new menus, their text and the HUD larger:

| Setting | Couch default | What it does |
|---|---:|---|
| `ui_retainedScale` | `1.5` | Size of the new menus. |
| `ui_retainedTextScale` | `1.25` | Text size in the new menus. |
| `ui_hudScale` | `1.25` | HUD size (`Settings -> Game Options -> HUD -> HUD Size`). |

Your own size choices always win over the profile. To choose it yourself, start
the game with `+set com_platformProfile couch`, or set the environment variable
`OPENQ4_COUCH=1`; `OPENQ4_COUCH=0` keeps Big Picture from selecting it.

## Text Rendering (New)

Menu and HUD text is drawn from scalable font files rather than the original
fixed-size bitmap ones. The letterforms are the same â€” the fonts were rebuilt by
tracing the game's own artwork â€” but they are now rasterised at whatever
resolution your display is actually running at, instead of a 640x480-era atlas
being magnified. Text stays sharp at 1440p and 4K rather than softening.

| Setting | Default | What it does |
|---|---:|---|
| `r_useTrueTypeFonts` | `1` | `1` draws text from the scalable fonts. `0` returns to the original bitmap fonts. |
| `r_ttfFontResolution` | `1.0` | Multiplies the resolution the glyphs are rasterised at. Raise for slightly sharper text at the cost of texture memory; lower to save memory. |

Changing either takes effect the next time fonts are loaded, so restart the game
or run `vid_restart` to see the change.

One visible difference: the original `chain` font shipped with a zero-width
space character, which ran words together in that face. The rebuilt font gives it
a normal space, so some text is spaced differently â€” correctly â€” than it used to be.

This covers the developer console and the loading screen too. Those draw from a
separate fixed-size character sheet, which is rebuilt at your display's
resolution the same way.

Raising `r_ttfFontResolution` costs texture memory quadratically: at 1440p a font
uses roughly 21 MB across its three sizes at the default of `1.0`. Lowering it to
`0.75` roughly halves that if you are short on video memory.

If the font files are missing (for example in a mod that replaces them), the game
falls back to the original bitmap fonts automatically.

## Text Background (Accessibility)

Quake 4 draws much of its text directly over the world and over busy panel
artwork, which can leave very little contrast. This draws a solid black backing
behind each line of menu and HUD text, so the text stays readable regardless of
what is behind it.

| Setting | Default | What it does |
|---|---:|---|
| `gui_textBackground` | `0` | How opaque the backing is. `0` is off, `1` is fully opaque black. `0.6`â€“`0.8` is usually enough to read comfortably while still showing the artwork. |
| `gui_textBackgroundPadding` | `2` | How far the backing extends past the text, in 640x480 virtual units. Raise it if the text feels cramped against the edge. |

Both apply immediately â€” no restart needed â€” and work with either font path.

```
seta gui_textBackground 0.75
seta gui_textBackgroundPadding 2
```

The backing covers text drawn by the menus, HUD and in-game GUIs. The developer
console draws on its own path and is not affected.

## Multi-Monitor Behavior (New)

When the render surface spans multiple monitors:

- 2D elements (console, HUD, menus, loading/initializing UI) are constrained to the selected display region. With `r_screen -1`, this defaults to the primary display.
- 2D aspect behavior inside that region is controlled by `ui_aspectCorrection`.
- Menu cursor mapping follows the same 2D region so mouse interaction stays aligned.

3D world rendering is unchanged by these UI cvars.

## Useful Console Examples

### Recommended Modern Defaults

```cfg
seta r_screen -1
seta r_fullscreenDesktop 1
seta r_fullscreen 1
seta r_multiSamples 4
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
seta ui_aspectCorrection 1
vid_restart
```

### Borderless Window on a Specific Monitor

```cfg
seta r_fullscreen 0
seta r_borderless 1
seta r_screen 1
vid_restart
```

### Custom Fullscreen Resolution

```cfg
seta r_fullscreen 1
seta r_fullscreenDesktop 0
seta r_mode -1
seta r_customWidth 2560
seta r_customHeight 1440
vid_restart
```

### Stretch Menu + HUD (No 4:3 Correction)

```cfg
seta ui_aspectCorrection 0
```

### View Weapon: Classic Framing + Slight Lowering

```cfg
seta cl_gunfov 90
seta cl_gunfov_adjust 1
seta cl_gun_z -1.15
```

### View Weapon: Match World FOV, Personal Position Offset

```cfg
seta cl_gunfov 0
seta cl_gun_x 0.5
seta cl_gun_y -0.5
seta cl_gun_z -0.5
```

### Performance-Focused AA Preset

```cfg
seta r_multiSamples 2
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

### Maximum Clarity (Higher GPU Cost)

```cfg
seta r_multiSamples 8
seta r_postAA 1
seta r_msaaAlphaToCoverage 1
vid_restart
```

## Experimental Vulkan overlap

Development builds provide three `r_vkRenderThread` modes:

| Value | Behavior |
| --- | --- |
| `0` | Default: render on the main thread. |
| `1` | Draw the previous frame while running the next game tic. |
| `2` | Also prepare the next frame while drawing the previous one. |

Use `r_vkRenderThread 2` in the console to try full overlap, and
`r_vkRenderThread 0` to return to the default. `renderThreadStats` reports
submitted/completed frames and how often front ends started or finished while
the backend was busy. Captures and resource changes still wait when needed.

Mode 2 is experimental: its geometry copies add CPU and memory cost, and a
performance improvement has not been qualified. Install matching engine and
renderer modules from the same build (renderer API 31). OpenGL remains
synchronous. See the [qualification record](../dev/presentation-owners.md#full-front-end-overlap-4-november-2026).

## Troubleshooting

- If a display change does not apply, run `vid_restart`.
- If monitor targeting looks wrong, run `listDisplays`, then set `r_screen` to the correct index and restart video.
- If UI appears too centered/boxed on wide displays, set `ui_aspectCorrection 0`.
- If the window opens off-screen after a monitor change, set `r_screen` explicitly to the target monitor and restart video; openQ4 will also attempt to recover automatically.
- If AA settings seem unchanged, check values with `r_multiSamples`, `r_postAA`, and `r_msaaAlphaToCoverage`, then run `vid_restart`.
- If enabling `r_postAA 1` turns the 3D viewport black on an older build, set `r_postAA 0`, run `vid_restart`, and attach `openq4.log` plus the output of `gfxInfo`. Current builds use a three-pass GLSL SMAA path and should no longer hit the old feedback-loop failure. RenderDoc capture is not yet supported on the current openQ4 renderer.
