# Display settings

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

This guide covers how openQ4 looks and how smoothly it runs: performance presets, fullscreen and windowed play, multiple monitors, resolution scale, anti-aliasing, V-Sync and frame rate, and choosing a renderer and graphics card. Almost everything here is on **Settings → Display** (the classic menus call this page **System**). Console settings come after the menu route in each section, and the [console reference](#console-reference) at the end lists the rest.

## Quick start

- **Pick a preset.** Open **Settings → Display → Graphics**, choose a **Performance Preset**, or press **Auto-Detect** to let openQ4 choose one for your hardware. Then press **Apply Changes**.
- **Fullscreen or window.** Press `Alt+Enter` to switch at any time, or set it on **Settings → Display → Screen**.
- **Changes apply straight away.** You don't need to restart the game; only a new renderer or graphics card needs **Restart Renderer**. Resolution, window, fullscreen, monitor and MSAA changes ask you to keep or revert them; if you don't answer, your previous display comes back.

The Display page has three tabs:

| Tab | What it holds |
|---|---|
| **Graphics** | Performance Preset, Auto-Detect, Rendering Backend, Rendering Device, Restart Renderer, UI Scale, Text Size, UI Aspect |
| **Screen** | Display Device, Expand Across Displays, Fullscreen, Borderless Window, Fullscreen Policy, MSAA, V-Sync, window size, Display Resolution, custom fullscreen size, Refresh Rate |
| **Image Quality** | Brightness, Resolution Scale, shadows, specular, bump maps, detailed sky, Bloom, SSAO, HDR Tonemap, Post AA Mode, ambient light, CRT Emulation, Irradiance Volumes, Preload Light Grids |

## Performance presets

A preset sets resolution scale, anti-aliasing, shadows, effects, texture sizes, a frame-rate cap and a few sound options in one step. Pick the one that matches your hardware, then adjust single settings if you like.

| Preset | Best for | What it turns on |
|---|---|---|
| `minimum` | Software renderers and systems below the minimum requirements | 50% render scale, no anti-aliasing, no shadows, smallest textures, 30 FPS cap |
| `lowpower` | Raspberry Pi-class boards, low-memory phones and saving battery | 75% scale, no anti-aliasing, no shadows, reduced textures, 30 FPS cap |
| `low` | PCs at the [minimum requirements](getting-started.md#system-requirements) | 75% scale, no anti-aliasing, the original game's shadows, reduced textures, 60 FPS cap |
| `performance` | Modest desktops, laptops and handhelds such as the Steam Deck | 85% scale, SMAA medium, original shadows, full-size textures, 60 FPS cap |
| `balanced` | Most desktops; the default | Full resolution, 2x MSAA with SMAA medium, original shadows, 120 FPS cap |
| `enhanced` | High-refresh play with modern effects | Full resolution, SMAA high, shadow maps for spotlights and sunlight (other lights keep the original shadows), bloom, SSAO, tone mapping, 144 FPS cap |
| `quality` | Soft shadow maps on every light | Full resolution, 2x MSAA with SMAA high, soft shadow maps for every light, bloom, SSAO, tone mapping, 144 FPS cap |
| `ultra` | The best image that still plays smoothly on a strong graphics card | Full resolution, 4x MSAA with SMAA ultra, larger and softer shadow maps, more SSAO detail, 240 FPS cap |
| `maximum` | Everything on, with no frame-rate target | As Ultra, plus 8x MSAA (not in 0.14.1, which asks for 16x), the largest shadow maps, cascaded sun shadows and motion blur |

What the effects are:

- **Shadow maps** give soft, detailed shadows; see [Shadow mapping](shadow-mapping.md). Low, Performance and Balanced keep the original game's sharp-edged shadows. Minimum and Low Power draw no shadows at all; on integrated graphics that saves about as much as lowering the render scale from 75% to 50%.
- **Bloom** adds a glow around bright lights. **SSAO** (ambient occlusion) adds soft shading in corners and creases. **Tone mapping** balances very bright and dark areas. Presets below Enhanced leave these off.
- Ultra and Maximum draw SSAO at full resolution. Enhanced and Quality draw it at half resolution, which is cheaper and looks the same in play. On OpenGL, half resolution is not in 0.14.1: there, 0.14.1 draws it at full resolution on every preset.
- The flashlight casts shadows on every preset that draws shadows. Lights carried by projectiles cast them only from Quality up, as on the original game's top setting.

Brightness, exposure, CRT emulation and the experimental rendering modes are separate controls that presets leave alone. Choosing a lower preset restores all of its values, including those for effects you have switched off.

### Frame-rate targets

Enhanced, Quality and Ultra are each built to hold a frame rate on the project's reference machine, a laptop with an RTX 4060 Laptop GPU and an i7-13700H, at 2560x1440 fullscreen on both OpenGL and Vulkan:

| Preset | Average | Slowest moments (1% low) |
|---|---:|---:|
| Enhanced | 144 FPS | 90 FPS |
| Quality | 100 FPS | 60 FPS |
| Ultra | 75 FPS | 45 FPS |

Maximum has no target. Faster or slower hardware, other resolutions and heavier scenes change these numbers; they describe that machine, not a promise.

### Applying a preset

Choose the preset on **Settings → Display → Graphics** and press **Apply Changes**:

- Effects switch at once, and textures reload in place, which takes a moment. No preset restarts the renderer.
- A preset that changes MSAA asks you to keep or revert. When OpenGL draws MSAA into the window itself (the default on macOS, or with `r_glWindowMultisample 1`), an MSAA change restarts the renderer.
- The sound device restarts when the speaker layout or reverb setting changes. The per-sound sample limit only changes fully when the game next starts; the page tells you when that applies.
- Anisotropic filtering (which keeps textures sharp at an angle) and MSAA are capped at what your graphics card supports.
- Presets turn bump maps, specular highlights and the detailed sky back on, clear any `image_picmip` reduction, and every preset from Low up turns shadows back on. [Texture replacement packs](texture-replacements.md) work at every preset.

**Updating an older configuration.** Select the preset again to adopt its current values; setting `com_performancePreset` on its own does not reapply it. A configuration saved with the earlier everything-on Ultra keeps those settings until you choose again: pick **Maximum** for that look, or **Ultra** for the 75 FPS recipe.

From the console, `applyPerformancePreset <name>` applies a preset by its name from the table. Afterwards run `vid_restart` so video and texture changes take effect, and `s_restart` for sound changes, as the console tells you.

### Auto-Detect

**Auto-Detect** recommends and applies a starting preset from your active renderer and graphics card, system memory, CPU cores, power hints and current resolution. It does not run a benchmark or promise a frame rate, and it never picks Maximum. It leaves your resolution, fullscreen, refresh rate, V-Sync, renderer choice, controls and interface settings alone.

Typical results:

- **Minimum:** software rendering, graphics too limited for the original materials, 2 GB of RAM or less, or a graphics card with 512 MB of memory or less.
- **Low:** a PC at the minimum requirements, with 4 GB of RAM or less, or a graphics card with 1 GB of memory or less.
- **Low Power:** a Raspberry Pi, or when you set the environment variable `OPENQ4_LOWPOWER=1` or `OPENQ4_RASPBERRYPI=1`.
- **Performance:** a Steam Deck, or a graphics driver that doesn't report its memory. Fewer than four CPU threads also limit any recommendation to Performance.
- **Phones and tablets:** at most Performance. Phones with about 12 GB of RAM or more render a 1080p-class scene, smaller ones a 720p-class scene, and those with 4 GB or less get Low Power.
- **Integrated graphics, including Apple Silicon:** up to Balanced at 1080p-class resolutions with enough memory. On Apple's older OpenGL 2.1 mode, Auto-Detect leaves SMAA off because it cannot run there.
- **Graphics cards with their own memory:** by memory and resolution, when the system has 16 GB of RAM and the renderer can run the modern effects:

  | Graphics memory | Up to 1440p | Up to 4K | Above 4K |
  |---|---|---|---|
  | 8 GB | Enhanced | Balanced | Performance |
  | 12 GB | Quality | Enhanced | Balanced |
  | 16 GB | Ultra | Quality | Enhanced |
  | 24 GB or more | Ultra | Ultra | Quality |

  A card counts in a class if it reports at least 15/16 of that size, because drivers report slightly less than the label (a card reporting 7.5 GB counts as 8 GB). Quality and Ultra also need eight CPU threads; cards with less memory get Performance or Balanced. Resolution classes are named by their 16:9 size and include taller shapes, so 1920x1200 counts as 1080p and 2560x1600 as 1440p; a 3440x1440 ultrawide counts as 4K. Only the active card's own memory counts, never shared system memory or a second graphics adapter.

Auto-Detect also:

- **Caps the frame rate at your display's refresh rate**, rounded to whole hertz (a 59.94 Hz display caps at 60), so a fresh installation doesn't draw frames the display can't show. If the rate is unknown or outside 30 to 1000 Hz, the preset's own cap applies. Phones, the Steam Deck, a Raspberry Pi and low-power requests never go above their preset's cap. Choosing a preset by name always uses the table cap.
- **May lower the render scale on large displays.** For example, integrated graphics at 1440p get Performance at 75%, and at 4K Performance at 50% (a 1080p scene). The window and menus stay at full resolution, and the scale never goes above the preset's own. Choosing a preset by name restores its table value. VR has its own [headset scale](vr.md).

On a fresh installation openQ4 starts with a small profile, detects your hardware once the renderer is running and applies the recommendation before you play. The console's `Quality profile:` line reports the result, including the video memory it found. A preset you name at launch takes precedence, and existing settings are kept.

The classic menus' four quality choices use the same recipes: Low is Low, Medium is Performance, High is Balanced and Ultra is Ultra. **Recommended** runs Auto-Detect. These keep your display mode and V-Sync setting.

## Fullscreen, windows and monitors

### Fullscreen

- **Desktop fullscreen** (the default, **Fullscreen Policy: Desktop**) fills the screen at your desktop resolution without switching display modes. It is the recommended choice. To render the 3D scene below or above your screen's resolution, use [Resolution Scale](#resolution-scale) rather than changing the display mode.
- **Exclusive fullscreen** (**Fullscreen Policy: Exclusive**) switches the display to the mode you choose:
  - **Display Resolution** lists Desktop Native first, then the fullscreen modes your selected display reports (with aspect labels such as 16:9 or 21:9), then Custom.
  - **Custom FS Width** and **Custom FS Height** set a custom size; editing them selects Custom.
  - **Refresh Rate** offers Auto plus the rates your display reports for that resolution. Auto keeps your desktop's refresh rate, or the nearest rate that resolution offers, and openQ4 picks the same rate every time. Leave it on Auto unless you need a specific rate. On macOS, Auto uses the desktop rate rather than the display's highest; this has not yet been confirmed on a Mac.
- On Windows, fullscreen minimizes when you switch away, so Alt+Tab and the Snipping Tool can come to the front cleanly. `PrintScreen` opens the Windows snipping tool; press `F12` for an openQ4 screenshot, or set `win_printScreenToSystemTool 0` to bind `PrintScreen` in game.

### Windowed and borderless

- Turn **Fullscreen** off to play in a window, and set its size with **Window Width** and **Window Height**.
- **Borderless Window** is on by default on Windows, because bordered OpenGL windows can stutter there. Turn it off for a normal, resizable window. Resizing a bordered window updates its saved size; hold `Shift` while resizing to snap to common shapes such as 4:3, 16:9, 16:10 and 21:9.
- openQ4 remembers the window's position. Leaving fullscreen brings back your window size rather than a fullscreen-sized window, and a window you take out of fullscreen or borderless from the Display page is centred on its display.
- If you unplug or rearrange monitors and the saved position ends up off-screen, openQ4 moves the whole window back onto a display.
- On Linux desktops that use Wayland, the desktop decides where windows go, so openQ4 doesn't save a position there.

### Multiple monitors

- **Display Device** chooses the monitor. **Auto** uses the current display. Choosing a specific monitor keeps the whole window, title bar included, on that monitor, shrinking it if needed.
- **Expand Across Displays** stretches the game across all your monitors. It needs a borderless window or desktop fullscreen, and the page tells you if your displays can't be spanned.
- When the game spans several monitors, the HUD, menus, console and loading screens stay on one display (the one you chose, or the primary display on Auto), and the mouse follows them. The 3D world still spans every monitor.
- In the console, `listDisplays` lists monitor numbers for `r_screen`, and `listDisplayModes [display]` lists the exclusive fullscreen modes a monitor supports.

### Aspect ratio and field of view

openQ4 follows whatever shape your window or screen is, so there is no aspect-ratio setting (`r_aspectRatio` is ignored). Weapon zoom keeps the same framing at every aspect ratio, and scoped views in multiplayer don't bob while you move.

**UI Aspect** (on the Graphics tab) keeps the menus, HUD and console in the original 4:3 framing when on, the default, or stretches them across the screen when off (`ui_aspectCorrection 0`).

## Resolution scale

**Settings → Display → Image Quality → Resolution Scale** renders the 3D world at a percentage of your screen's resolution: 10%, 25%, 50%, 75%, 85%, 100%, 125%, 150% or 200%. Below 100% is faster and softer; above 100% renders more pixels than your screen has and scales them down, which is sharper and slower. The HUD and menus always stay at full resolution.

Below 100%, [temporal anti-aliasing](temporal-presentation.md) upscales the scene by default, which keeps edges smooth and recovers detail a plain stretch loses.

The console setting `r_resolutionScaleMode` chooses how a scene below 100% is stretched to your screen, on OpenGL and Vulkan, in single-player and multiplayer:

- `1`: smooth (the default).
- `2`: sharpened; `r_resolutionScaleSharpness` sets the strength.
- `3`: nearest-neighbour, a crisp pixelated look.
- `0`: the old cropped view for fill-rate testing. In multiplayer it draws the world in the bottom-left corner without stretching it.

Temporal AA works with modes 1 and 2; modes 0 and 3 keep their own look. With the [experimental screen-space lighting](advanced-screen-space-lighting.md) on, every mode looks like mode 1.

<a id="anti-aliasing-settings-new"></a>

## Anti-aliasing

Anti-aliasing smooths jagged edges. openQ4 offers three kinds, which you can combine:

- **MSAA** (multisample anti-aliasing, on **Settings → Display → Screen → MSAA**) smooths the edges of geometry. Each step costs frame time:
  - **2x** is a cheap improvement for modest graphics cards.
  - **4x** is a good balance; Ultra uses it with SMAA.
  - **8x** looks better again and costs noticeably more. It is the most the Vulkan renderer draws, and what Maximum uses (not in 0.14.1).
  - **16x** works on OpenGL only, where your graphics card supports it. On the reference laptop it costs 6 to 11 ms a frame more than 4x at 2560x1440, more than all the post-processing effects together. Vulkan draws a 16x request at 8x.
- **Post AA** (**Settings → Display → Image Quality → Post AA Mode**) is SMAA, a cheap filter that smooths edges in the finished image, including those MSAA misses. SMAA Medium is the recommended default.
- **Temporal AA** combines several frames. It switches on automatically when the scene renders below your screen's resolution; see [Temporal AA and dynamic resolution](temporal-presentation.md).

MSAA changes apply to the 3D scene from the next frame, on OpenGL and Vulkan, and the Display page asks you to keep or revert them. When OpenGL draws MSAA into the window itself (the default on macOS, or with `r_glWindowMultisample 1`), an MSAA change restarts the renderer instead. Post AA changes apply at once.

If the renderer draws fewer MSAA samples than you chose, because of your graphics card or the 8x limit on Vulkan, the page says so under MSAA (not in 0.14.1). `gfxInfo` in the console also shows the requested and actual counts.

Notes:

- **Alpha-to-coverage** (`r_msaaAlphaToCoverage`, on by default) makes see-through textures such as fences and foliage smoother when MSAA is on.
- Under OpenGL, the window itself keeps its MSAA setting until `vid_restart`. That only affects the few 3D views drawn straight to the window, such as a model shown inside a menu.
- On Vulkan, graphics cards that can't move their sample positions (Intel integrated graphics, for example) use their own standard pattern, which can shift edge smoothing by a fraction of a pixel compared with OpenGL.
- **Macs using OpenGL's older 2.1 mode** can only apply MSAA while the 3D scene is drawn straight to the window. Temporal AA, bloom, tone mapping, SSAO, motion blur and resolution scaling each turn MSAA off for the scene. Post AA can't run in that mode, so the Balanced to Ultra presets keep their MSAA there.

## V-Sync and frame rate

**Settings → Display → Screen → V-Sync** locks frames to your display's refresh to stop tearing. It applies as soon as you press **Apply Changes**, with no restart and no keep-or-revert question.

Each preset sets a **frame-rate cap**; there is no menu control for it. In the console, `com_maxfps` sets the cap (`0` removes it). With V-Sync on and a variable-refresh display (G-Sync, FreeSync), cap a few frames under the refresh rate, for example `com_maxfps 237` on a 240 Hz display, so frames never wait for the next refresh. A minimized or hidden window draws at most 60 frames a second.

On a Steam Deck, openQ4 applies a Deck-friendly cap while `com_maxfps` is still at its default of 240; any other value you set is kept. See the [Steam Deck guide](steam-deck.md#performance).

`com_showFramePacing 1` shows frame times, the display's refresh rate and what is limiting the frame rate on screen; `2` also logs them.

### Smooth motion between game ticks

Quake 4's game runs 60 times a second. Above 60 FPS, openQ4 draws the camera, your weapon, moving objects, ragdolls, debris, cutscene and security cameras, third person and the death camera between those ticks, so motion stays smooth instead of stepping. Gameplay, networking, saves and demos keep their original timing. `g_presentationInterpolation 0` returns everything to the 60 Hz tick.

**Late look** (`in_presentationView 1`, off by default) also turns the view with your mouse, gyro, look keys and stick on every drawn frame, instead of up to a tick later. Your weapon, its muzzle flash and its lights turn with the view, while shots, hit detection and the network still use the game's own aim. Late look stands down while mouse smoothing or filtering is on (`m_smooth` above `1`, `m_filter` above `0`), in menus, the weapon and quick-chat wheels, vehicles, cutscenes and camera views, third person, VR, while dead and during demo playback. On very fast turns, tracers and projectiles can appear a few units from the drawn barrel for their first frame.

<a id="renderer-backend-automatic-selection-vulkan-stable-on-windows"></a>

## Rendering backend and device

openQ4 has two desktop renderers, Vulkan and OpenGL, and draws the same game with either. On Windows, Vulkan is supported and the default for new installations. On Linux and macOS it is experimental. Android uses OpenGL ES.

### Choosing a renderer and graphics card

On **Settings → Display → Graphics**:

- **Rendering Backend** chooses **Automatic**, **OpenGL** or **Vulkan** (**OpenGL ES** on Android). Automatic tries Vulkan first and falls back to OpenGL if Vulkan can't start. It uses OpenGL while PC [VR](vr.md) is on; choose Vulkan yourself to play VR on Vulkan. A renderer you chose earlier stays chosen.
- **Rendering Device** chooses **Automatic (high performance)**, which prefers a compatible dedicated graphics card over integrated and software ones, or a named graphics card. Choosing a named card also selects Vulkan, because OpenGL leaves the choice of card to the operating system and driver. A saved card that is no longer present is shown as unavailable, and unsupported cards can't be chosen. When two dedicated cards qualify, Automatic picks the one with more memory; that is not a speed test.
- **Restart Renderer** applies the change. At the main menu it restarts the renderer at once. During a single-player game it saves, restarts the renderer and puts you back where you were, which takes about as long as loading a save; the save stays in the Load list as Restart Renderer and is replaced next time. In multiplayer, during a cinematic or after dying, return to the main menu first.

The page shows the renderer and card in use while a change waits for the restart. Auto-Detect becomes available once the chosen hardware is running. A restart from this page replaces renderer choices given on the command line for this session, but a launcher that passes them again next time still wins.

From the console, `rendererDevices` lists your graphics cards and their IDs. Set `r_renderDevice "<id>"` and `r_renderApi vulkan`, then restart openQ4; `r_renderDevice auto` returns to automatic choice. Device IDs don't change when the driver lists cards in a different order.

### Vulkan

On Windows, Vulkan draws the stock game with all of OpenGL's effects, including heat haze, glass and water, both kinds of shadows, post-processing, cel shading and multiplayer player outlines, and it runs mods' own material programs, with the few exceptions listed below. It has been tested on every stock single-player map and 20 multiplayer maps with NVIDIA and Intel graphics. AMD graphics have not been tested yet, so please report problems.

If something goes wrong:

- **Vulkan can't start.** If there is no Vulkan driver, no graphics card meets Vulkan 1.3 requirements, or the window or device can't be created, openQ4 starts with OpenGL in the same launch. `gfxInfo` gives the reason on its `Renderer API fallback reason` line, and `r_actualRenderApi` reports `gl`. Your Vulkan choice is kept, so the next launch tries again, for example after a driver update.
- **The driver resets during play.** openQ4 restarts the renderer by itself and carries on with the same game, up to three times a session. If it keeps failing after that, openQ4 stops with an error and switches to OpenGL for the next launch. A failure during a full `vid_restart` does the same.
- **Going back to OpenGL.** Choose OpenGL on the Graphics tab and restart the renderer, or set `r_renderApi gl` and restart openQ4. If you started the game with `+set r_renderApi vulkan`, remove that option.

When you report a Vulkan problem, check the list below first, then include `openq4.log`, the output of `gfxInfo`, and your graphics card and driver version.

### What Vulkan does not do yet

- **Some mod material programs.** A mod's program that relies on old fixed-function state, fog or texture-generation state, values the engine never sets for materials, unusual texture types, or NVIDIA-only options is skipped and logged once. OpenGL would draw it with leftover state rather than a defined result.
- **Clustered decal routing.** `r_rendererClusteredDecals` changes only how OpenGL's modern path handles decals, not how they look, so it does nothing on Vulkan. (Both renderers draw [PBR materials](pbr-materials.md) the same way.)
- **Pixel readbacks in debug views.** The overdraw counts from `r_showLightCount 3` and `r_showShadowCount 2` to `4` arrive a frame or two late, and `r_showIntensity` and `r_showDepth` recolour a copy of the view. `r_showDepth` shows depth as grey.

### Vulkan on macOS (through MoltenVK)

Apple doesn't provide a Vulkan driver, so on macOS openQ4's Vulkan renderer runs on **MoltenVK**, a translation layer from Vulkan to Apple's Metal graphics. It is bundled inside both macOS packages; there is nothing extra to download or enable, and the OpenGL renderer stays available.

- New macOS configurations use Automatic: Vulkan through MoltenVK when it meets the renderer's requirements, otherwise OpenGL. A saved OpenGL choice stays on OpenGL.
- To try it, choose Vulkan on **Settings → Display → Graphics → Rendering Backend**, apply, then press **Restart Renderer** from the main menu or a single-player game; or set `r_renderApi vulkan` and relaunch. The active renderer readout and `gfxInfo` confirm what started.
- **Expect problems.** Vulkan is experimental on macOS. One player has reported it working (openQ4 0.12.0 on an M4 Max MacBook Pro), and nobody has compared its output with OpenGL there. Missing effects, wrong shading, poor performance or a refusal to start are all possible.
- **If it can't start**, openQ4 uses OpenGL and `gfxInfo` shows why. Common causes are a GPU that doesn't meet the Vulkan 1.3 requirements, or a package whose bundled MoltenVK was stripped by a copy. If a later failure stops the game starting, launch it once with OpenGL from Terminal: `/Applications/openQ4.app/Contents/MacOS/openQ4 +set r_renderApi gl`.
- **To go back**, set `r_renderApi gl` and restart. The choice is saved.
- When you report a macOS Vulkan problem, include `openq4.log` (it records which MoltenVK library loaded), the `gfxInfo` output, and your Mac model and macOS version.

### Experimental: Vulkan render thread

`r_vkRenderThread` lets Vulkan draw on a separate thread. `0`, the default, draws on the main thread; `1` draws the previous frame while the next game tick runs; `2` also prepares the next frame while drawing the previous one. Mode 2 is experimental: it costs extra CPU time and memory, and a speed-up has not been shown. `renderThreadStats` reports how much the threads overlapped. OpenGL always draws on the main thread.

## Texture quality and memory

Presets set texture sizes for you. Two console settings let you trade texture detail for memory and speed yourself.

**`image_downSize`** settings set a ceiling: no texture of that kind may be larger than a given size. This is how the original Quake 4 lowered texture quality, and presets use it.

**`image_picmip`** drops whole steps of detail, so each step halves a texture however large it started, like `r_picmip` in Quake 3. openQ4 applies it **only to the diffuse layer of materials** (the colour texture). Normal maps, specular maps, lighting, skies, decals, fonts and the HUD keep their full resolution, so surfaces stay correctly lit and the interface stays sharp while most texture memory still comes down.

| Setting | Default | What it does |
|---|---:|---|
| `image_picmip` | `0` | Detail steps to drop from colour textures. `0` is full resolution; each step halves the texture. |
| `image_picmipFilter` | `1` | Which textures `image_picmip` reduces: `0` all colour textures, `1` world surfaces (`textures/*`), `2` characters, weapons and props (`models/*`), `4` everything else. Add values to combine. |
| `image_picmipMinSize` | `32` | Stops reducing once a texture's longest side reaches this size, so small textures never turn to mush. |
| `image_downSize` | `0` | Turns on the general size ceiling. |
| `image_downSizeLimit` | `0` | That ceiling in pixels. `0` means no limit. |
| `image_downSizeBump` / `image_downSizeBumpLimit` | `0` / `256` | A separate ceiling for normal maps. |
| `image_downSizeSpecular` / `image_downSizeSpecularLimit` | `0` / `64` | A separate ceiling for specular maps. |

The ceiling applies first and `image_picmip` reduces from there, so raising `image_picmip` always halves the result. A material that declares `nopicmip` opts out of both. Textures reload by themselves when you change these settings; no `vid_restart` is needed.

For example, this halves world and model colour textures twice while leaving lighting detail, the HUD and menus untouched:

```
image_picmip 2
image_picmipFilter 3
```

[Texture replacement packs](texture-replacements.md) in DDS format can only shrink by dropping detail levels the file already contains, so a `.dds` saved without a full set of smaller versions stays larger than requested. `image_showPrecompressedTextures 1` names any replacement that couldn't reach the requested size.

### Texture filtering

`image_filter` controls how textures are smoothed. Changes apply to loaded textures at once, on OpenGL and Vulkan, and are saved. Materials that ask for a particular filter, such as fonts, HUD elements and the renderer's own textures, keep it.

| Value | Look |
|---|---|
| `GL_LINEAR_MIPMAP_LINEAR` | The default: smoothest, with soft transitions into the distance. |
| `GL_LINEAR_MIPMAP_NEAREST` | Smooth textures with sharper steps between distance levels. |
| `GL_NEAREST_MIPMAP_NEAREST` | Pixelated textures that stay stable in the distance. |
| `GL_NEAREST_MIPMAP_LINEAR` | Pixelated textures with softer distance transitions. |
| `GL_NEAREST` | Fully pixelated; may shimmer in the distance. |
| `GL_LINEAR` | Smooth, without distance levels. |

For a pixelated look, enter:

```
seta image_filter GL_NEAREST_MIPMAP_NEAREST
```

The pixelated modes skip anisotropic filtering so that `image_anisotropy` can't blur the pixel edges.

## Motion blur

Motion blur (`r_motionBlur 1`, on in the Maximum preset) smears the scene along its own motion: the camera turning or moving, and moving characters, vehicles and objects. It works with or without temporal AA, resolution scaling, MSAA, bloom, tone mapping and SSAO, on OpenGL and Vulkan.

- **The same blur at any frame rate.** By default the blur matches the motion of one 60 Hz frame (`r_motionBlurShutterMsec 16.667`), so 144 Hz and uncapped play look like 60 Hz play. `r_motionBlurShutterMsec 0` restores the original behaviour, where blur shrinks as the frame rate rises.
- **Your weapon stays sharp.** It blurs only with its own animation (sway, recoil, reloads), never because you turn. Characters blur with their animation as well as their movement. `r_motionBlurObjectVectors 0` blurs by camera motion alone.
- **No blur across cuts and pauses.** Camera cuts, loading, `vid_restart`, resizing and teleports start afresh instead of smearing between unrelated views, and pausing stops the blur. Mirrors, portals and security-camera screens don't interrupt it.
- **Below 10 FPS it switches off**, so very slow frames stay sharp rather than smeared.
- `r_motionBlurStrength` (0.45), `r_motionBlurMaxPixels` (10) and `r_motionBlurSamples` (8, or 16 on Maximum) set the length, the longest smear and the quality.
- On a Mac using OpenGL's older 2.1 mode, motion blur turns MSAA off for the 3D scene.

## Interface size and text

**UI Scale** and **Text Size** on **Settings → Display → Graphics** set the size of the new menus and their text (`ui_retainedScale`, `ui_retainedTextScale`); **Reset UI Sizes** puts them back. The HUD has its own size under **Settings → Game Options → HUD → HUD Size** (`ui_hudScale`).

### Television viewing (Couch profile)

Playing on a television from across the room? Launching openQ4 from Steam's Big Picture mode selects the Couch profile automatically (a Steam Deck keeps its own profile). On a fresh configuration it turns controller input on and makes the new menus, their text and the HUD larger:

| Setting | Couch default | What it does |
|---|---:|---|
| `ui_retainedScale` | `1.5` | Size of the new menus. |
| `ui_retainedTextScale` | `1.25` | Text size in the new menus. |
| `ui_hudScale` | `1.25` | HUD size. |

Your own size choices always win over the profile. To choose it yourself, start the game with `+set com_platformProfile couch`, or set the environment variable `OPENQ4_COUCH=1`; `OPENQ4_COUCH=0` stops Big Picture selecting it.

### Sharp text

Menu and HUD text is drawn from scalable fonts traced from the game's own lettering, so it stays sharp at 1440p and 4K instead of being a magnified low-resolution image. The developer console and loading screen are sharpened the same way. If the scalable font files are missing, for example in a mod that replaces the fonts, openQ4 falls back to the original ones.

| Setting | Default | What it does |
|---|---:|---|
| `r_useTrueTypeFonts` | `1` | `1` uses the scalable fonts; `0` returns to the original bitmap fonts. |
| `r_ttfFontResolution` | `1.0` | How finely letters are drawn. Higher is slightly sharper and uses more video memory: at 1440p a font uses about 21 MB at `1.0`, and `0.75` roughly halves that. |

Changes take effect the next time fonts load: restart the game or run `vid_restart`. One visible difference: the original `chain` font had a zero-width space that ran words together, and the rebuilt font gives it a normal width.

### Text background (accessibility)

Quake 4 draws much of its text straight over the world and busy panel art. A solid backing behind each line of menu and HUD text keeps it readable whatever is behind it. Both settings apply at once and work with either font. The developer console isn't affected.

| Setting | Default | What it does |
|---|---:|---|
| `gui_textBackground` | `0` | How opaque the backing is: `0` off, `1` fully opaque black. `0.6` to `0.8` is usually comfortable while still showing the art. |
| `gui_textBackgroundPadding` | `2` | How far the backing extends past the text, in 640x480 screen units. |

```
seta gui_textBackground 0.75
seta gui_textBackgroundPadding 2
```

## View weapon position and field of view

These settings move and frame the weapon you hold on screen. They only change what you see, never gameplay. They are on **Settings → Game Options → General**, under **View Weapon**.

| Setting | Default | What it does |
|---|---:|---|
| `cl_gunfov` | `0` | Weapon field of view. `0` follows your view's field of view; other values are kept within a safe range. |
| `cl_gunfov_adjust` | `1` | `1` keeps the classic 4:3 weapon framing on any screen shape; `0` converts the field of view directly for your screen. |
| `cl_gun_x` | `0` | Moves the weapon right. |
| `cl_gun_y` | `0` | Moves the weapon forward. |
| `cl_gun_z` | `0` | Moves the weapon up. |

`cl_gun_x/y/z` add to the original `g_gunX/Y/Z` offsets; use the `cl_gun_*` settings in your own config. openQ4 keeps `g_gunX` at `1` and `g_gunZ` at `-1` so the weapon stays clear of the screen edge on widescreen displays.

```cfg
// Classic framing, slightly lower
seta cl_gunfov 90
seta cl_gunfov_adjust 1
seta cl_gun_z -1.15
```

## Multiplayer player visibility and item styles

### Player outlines, rim light and bright skins

These optional effects make other players easier to see in multiplayer. They only change what you see; they don't affect hit detection or what the server decides. All are off by default.

Set them on the main menu's **Multiplayer → Player Setup** page, under the **Opponents** and **Teammates** tabs, with a live preview. **Opponents** also has **Force Model**, which shows every other player as one model in free-for-all modes (`g_forceModel`). The classic menus have them under **Multiplayer → Settings → Appearance**, on the **Enemy** and **Teammate** tabs. Colours and outline width are preset choices in the menus; the console settings accept any value in range.

| Setting | Default | What it does |
|---|---:|---|
| `cl_player_outline_enemy` | `0` | Opponent outline strength (`0` to `1`). |
| `cl_player_outline_team` | `0` | Teammate outline strength (`0` to `1`). Teammate outlines show through walls. |
| `cl_player_outline_width` | `2.0` | Outline width in screen pixels (`0.5` to `6.0`). |
| `cl_player_rimlight_enemy` | `0` | Opponent rim-light strength (`0` to `1`): a glow around the edge of the body. |
| `cl_player_rimlight_team` | `0` | Teammate rim-light strength (`0` to `1`). |
| `cl_player_visibility_enemy_color` | `1 0.12 0.05` | Opponent outline and rim-light colour (red, green, blue from `0` to `1`). |
| `cl_player_visibility_team_color` | `0.1 0.85 0.25` | Teammate outline and rim-light colour. |
| `cl_player_brightskin_enemy` | `0` | Opponent bright-skin strength (`0` to `1`). |
| `cl_player_brightskin_team` | `0` | Teammate bright-skin strength (`0` to `1`). |
| `cl_player_brightskin_enemy_color` | `1 0.05 0.02` | Opponent bright-skin colour. |
| `cl_player_brightskin_team_color` | `0.05 1 0.22` | Teammate bright-skin colour. |
| `r_playerRimlightPower` | `2.0` | Rim-light falloff (`0.25` to `8.0`); higher keeps the glow tight to the outline. Not in the menus. |
| `r_playerRimlightFloor` | `0` | Tints the whole body by this fraction of the rim strength (`0` to `1`), so a player facing you is still tinted. Not in the menus. |

```cfg
seta cl_player_outline_enemy 0.85
seta cl_player_rimlight_enemy 0.5
seta cl_player_outline_team 0.45
seta cl_player_rimlight_team 0.25
seta cl_player_outline_width 2.0
```

Notes:

- The outline is a ring of constant width just outside the player's silhouette, at any distance and on any screen shape.
- Only teammate outlines show through walls, so you can find allies anywhere on the map. Rim light and bright skins only appear where you can actually see the player.
- The rim light needs GLSL shader support; without it the console says so once, and the outline and bright skin still work. Without GLSL the outline width is approximate.
- Bots are treated like any other player.
- `r_skipPlayerVisibilityEffects 1` turns all three effects off, for clean screenshots or comparisons. If a setting seems to do nothing, `g_showPlayerVisibilityEffects 1` prints whether the effects reached each player, or why they were skipped.

### Pickup and opponent-weapon style

**Settings → Game Options → General** has two multiplayer display options. Both default to the original look, change only what you see, and apply without reconnecting.

| Setting | Default | What it does |
|---|---:|---|
| `g_simpleItems` | `0` | **Pickup Style**: `0` original models, `1` the old simple icons, `2` flat colour matching the item's icon, `3` flat colour with a soft light sweeping up each item. |
| `g_mpFlatOpponentWeapons` | `0` | **Opponent Weapon Style**: `1` gives weapons held by opponents the same flat icon colour, without the sweep. |

Flat styles change only an item's colour layer; its lighting detail, effects and transparent parts (such as the bubble around health pickups) stay as they were. Weapon and ammo pickups count as items, so style `3` gives them the sweep too. Your own first-person weapon is never changed. In team modes the opponent-weapon colour applies only to the other team; in free-for-all it applies to every other player.

## Troubleshooting

- **A display change from the console didn't apply.** Menu changes apply by themselves; after changing window or fullscreen settings in the console, run `vid_restart`.
- **The game is on the wrong monitor, or the window opened off-screen.** Choose the monitor under **Display Device**, or run `listDisplays`, set `r_screen` to the right number and run `vid_restart`. openQ4 also moves an off-screen window back by itself.
- **Menus look boxed in on a wide screen.** Turn **UI Aspect** off (`ui_aspectCorrection 0`).
- **MSAA seems to do nothing.** Check the note under MSAA on the Display page, or run `gfxInfo` to compare the requested and actual counts. Vulkan draws at most 8x, and on a Mac in OpenGL's 2.1 mode most post-processing effects turn MSAA off.
- **Post AA turns the 3D view black.** Set `r_postAA 0` and report it with `openq4.log` and the output of `gfxInfo`.
- **Vulkan won't start.** openQ4 falls back to OpenGL by itself; `gfxInfo` explains why. Update your graphics driver and try again.
- **Stutter or uneven frames.** `com_showFramePacing 1` shows what limits the frame rate. On Windows with OpenGL, a borderless window paces better than a bordered one.

When reporting a display problem, include `openq4.log`, the output of `gfxInfo`, and your graphics card and driver version.

## Console reference

Settings ("console variables") can be typed in the console or added to your config with `seta`. Commands run in the console.

### Presets

| Setting or command | Default | What it does |
|---|---:|---|
| `com_performancePreset` | `balanced` | The selected preset. Setting it alone doesn't reapply it. |
| `applyPerformancePreset [name]` | | Applies a preset. Without a name it reapplies the stored one (or Balanced if that is invalid); unknown names are refused. |
| `autoDetectPerformancePreset` | | Runs Auto-Detect. |
| `execMachineSpec` | | Applies the preset matching the classic quality level. |

### Screen and window

| Setting or command | Default | What it does |
|---|---:|---|
| `r_fullscreen` | `1` | `1` fullscreen, `0` windowed. |
| `r_fullscreenDesktop` | `1` | `1` desktop fullscreen (recommended); `0` exclusive fullscreen at the size from `r_mode` or `r_customWidth`/`r_customHeight`. |
| `r_borderless` | `1` on Windows, `0` elsewhere | Borderless window when `r_fullscreen 0`. |
| `r_windowWidth` / `r_windowHeight` | `1280` / `720` | Window size, not counting the title bar and borders. |
| `win_xpos` / `win_ypos` | (automatic) | Window position, including the title bar; updated when you move the window. |
| `r_mode` | `-2` | Exclusive fullscreen size: `-2` desktop, `-1` custom, `0` and up a size from `listModes`. |
| `r_customWidth` / `r_customHeight` | `1920` / `1080` | Custom exclusive fullscreen size, used when `r_mode -1`. |
| `r_displayRefresh` | `0` | Exclusive fullscreen refresh rate in hertz; `0` is Auto. |
| `r_screen` | `-1` | Monitor number from `listDisplays`; `-1` is Auto. |
| `r_multiScreen` | `0` | `1` spans all monitors (Expand Across Displays). |
| `ui_aspectCorrection` | `1` | `1` keeps menus and HUD at 4:3; `0` stretches them. |
| `win_printScreenToSystemTool` | `1` | Windows: `1` lets `PrintScreen` open the snipping tool; `0` frees it for in-game binds. |
| `listDisplays` | | Lists monitors and their numbers. |
| `listDisplayModes [display]` | | Lists a monitor's exclusive fullscreen modes, with scale and exact refresh rates. |
| `listModes` | | Lists the sizes `r_mode` accepts. |
| `vid_restart` | | Restarts the renderer to apply console display changes. `vid_restart partial` changes only the window mode, which is quicker. |

### Anti-aliasing and scale

| Setting | Default | What it does |
|---|---:|---|
| `r_multiSamples` | `0` | MSAA samples: `0` off, `2`, `4`, `8` or `16`. Other values round up to the next step, and `1` means off. |
| `r_multiSamplesEffective` | (read-only) | **Not in 0.14.1:** the MSAA count the 3D scene last drew with. |
| `r_postAA` | `0` | Post AA mode; see the values below. |
| `r_msaaAlphaToCoverage` | `1` | Smoother fences and foliage when MSAA is on. |
| `r_msaaResolveDepth` | `0` | Also resolves depth with MSAA. Leave off unless asked to test it. |
| `r_glWindowMultisample` | (platform) | OpenGL: `1` creates the window itself with MSAA, so an MSAA change restarts the renderer; `0` applies MSAA to the 3D scene only. On by default on macOS. |
| `r_screenFraction` | `100` | Resolution scale in percent (`10` to `200`). |
| `r_resolutionScaleMode` | `1` | How a scene below 100% is stretched; see [Resolution scale](#resolution-scale). |
| `r_resolutionScaleSharpness` | `0.4` | Sharpening for scale mode `2` (`0` to `1.5`). |
| `r_ssaoResolution` | `0` | SSAO resolution: `0` automatic (full at the highest SSAO sample count, as on Ultra and Maximum, half below), `1` full, `2` half. On OpenGL, not in 0.14.1. |

`r_postAA` values:

- `0`: off.
- `1`: SMAA medium, the recommended default.
- `2`: SMAA high, which searches further along each edge.
- `3`: SMAA ultra, which also finds fainter edges.
- `4`: color-edge prototype, which detects edges by colour instead of brightness, for comparison screenshots.

### Frame rate and motion

| Setting | Default | What it does |
|---|---:|---|
| `r_swapInterval` | `0` | V-Sync: `0` off, `1` on. |
| `com_maxfps` | `240` | Frame-rate cap; `0` removes it. |
| `com_steamDeckAutoFrameCap` | `1` | On a Steam Deck, applies a Deck-friendly cap while `com_maxfps` is still `240`. `0` turns this off. |
| `com_steamDeckFrameCap` | `0` | The Steam Deck cap; `0` uses the display's refresh rate within a Deck-friendly range. |
| `com_preciseFrameWait` | `1` | `1` holds the cap with a precise timer; `0` uses the older whole-millisecond wait. |
| `com_showFramePacing` | `0` | `1` shows frame times, refresh rate and what limits the frame rate; `2` also logs them. |
| `g_presentationInterpolation` | `1` | Draws motion between game ticks; `0` returns to the 60 Hz tick. |
| `in_presentationView` | `0` | `1` turns on late look. |
| `com_latencyFlash` | `0` | Draws a white bar down the left edge on the first frame after you fire, for click-to-screen latency testers such as NVIDIA LDAT or OSLTT. Leave off for play. |

### Renderer

| Setting or command | Default | What it does |
|---|---:|---|
| `r_renderApi` | `best` (desktop), `gles` (Android) | The renderer; see the values below. Takes effect when openQ4 restarts, not on `vid_restart`; **Restart Renderer** on the Display page applies a choice made there. |
| `r_renderDevice` | `auto` | `auto` for automatic choice, or a graphics card's ID from `rendererDevices`. A named card needs Vulkan. |
| `r_actualRenderApi` | (read-only) | The renderer that actually started; `gl` after a fallback from Vulkan. |
| `r_vkDevice` | | A temporary graphics card number for testing. Remove it before using the menu's card choice. |
| `r_vkPresentationRecoveries` | `3` | How many times a session Vulkan restarts itself after a driver reset. |
| `r_vkRenderThread` | `0` | Experimental Vulkan render thread mode (`0` to `2`). |
| `rendererDevices` | | Lists graphics cards and their IDs. |
| `gfxInfo` | | Prints the renderer, graphics card, anti-aliasing state and any fallback reason. |
| `rendererMotionBlurStatus` | | Prints what motion blur did and why any views were skipped; `reset` clears the counts. |
| `renderThreadStats` | | Reports render-thread overlap. |

`r_renderApi` values:

| Value | Also accepts | What it selects |
|---|---|---|
| `best` | | Vulkan with OpenGL fallback on desktop; OpenGL ES on Android. |
| `gl` | `opengl` | OpenGL. |
| `vulkan` | `vk` | Vulkan: supported on Windows, experimental on Linux and macOS. |
| `gl-module` | | OpenGL loaded as a separate module; for testing, and draws the same as `gl`. |
| `gles` | | OpenGL ES; the Android default. |

Anything else is refused with a warning, and openQ4 uses the platform's automatic choice.

### Example configurations

```cfg
// Borderless window on the second monitor
seta r_fullscreen 0
seta r_borderless 1
seta r_screen 1
vid_restart
```

```cfg
// Exclusive fullscreen at 2560x1440
seta r_fullscreen 1
seta r_fullscreenDesktop 0
seta r_mode -1
seta r_customWidth 2560
seta r_customHeight 1440
vid_restart
```

```cfg
// 4x MSAA with SMAA medium (applies without vid_restart)
seta r_multiSamples 4
seta r_postAA 1
```
