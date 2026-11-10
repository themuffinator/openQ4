# VR headset qualification checklist

These are the VR gates that need a person wearing a headset: all of VR-02, and
the hardware halves of VR-01, VR-03, VR-04 and VR-05. Automated fixtures may
never synthesize controller, keyboard or mouse input, and the OpenXR test
runtime cannot show whether tracking feels right, text is readable or a frame
is comfortable. Everything those items can establish without a headset has
simulated-runtime evidence; see
[the roadmap](roadmap.md#vr-task-evidence-and-acceptance) and the
[XR integration evidence](platform-recovery-review.md#vulkan-egl-and-world-gui-xr-integration-6-november-2026).

Each table names what to do and what counts as a pass. Record one line per
step: pass, fail (with what you saw) or not run, plus the headset, the
OpenXR runtime and its version, and the GPU. Quest standalone is not covered
here: it waits on the AND-05 distribution decision.

## Common setup

- Stage the current build into `.install/` and launch it from there:
  `tools/build/meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects`.
- Note the build banner at the top of the log (`<version>-dev+g<sha>`) so the
  result binds to a build. The log is `<fs_savepath>\baseoq4\logs\openq4.log`
  (`.home\baseoq4\logs\openq4.log` with the VS Code launches).
- Launch with `+set vr_debug 1` so the log records session, space and focus
  decisions. `vr_status` prints the session, render API, eye size, play space
  and refresh rate.
- Run every section twice: once with `r_renderApi gl` and once with
  `r_renderApi vulkan`. Each runtime available counts separately: SteamVR,
  Meta Quest Link (or Air Link), Monado on Linux, and a Windows Mixed Reality
  headset through SteamVR.
- On Linux, run once on an X11 session and once on a native Wayland session
  (Vulkan, and OpenGL where the runtime binds EGL); with SteamVR on Wayland,
  OpenGL needs `OPENQ4_FORCE_X11=1`.
- Single-player steps use `game/airdefense1` unless they say otherwise;
  multiplayer steps host `mp/q4dm1` with `+set si_gameType DM +spawnServer mp/q4dm1`
  and join it from a second, flat client.

## 1. Session, submission and recovery (VR-01, VR-06)

| # | Do this | Pass when |
| --- | --- | --- |
| 1.1 | Set **VR Mode** to Enabled in Settings > Game Options > Virtual Reality. | The headset shows the floating menu screen within a few seconds; on Vulkan the renderer restarts once, then stays. |
| 1.2 | Start a map, then minimize the desktop window, cover it with another window, and restore it. | The headset never stops or stutters; the desktop mirror resumes when restored. |
| 1.3 | Quit the runtime (SteamVR: close it) during gameplay; start it again; enter `vr_restart`. | The game carries on in the desktop window, then returns to the headset with the controllers working. |
| 1.4 | Take the headset off until the runtime idles it, then put it back on. Repeat by opening the runtime's own menu (SteamVR dashboard, Meta menu) and closing it. | Rendering pauses and resumes; no stuck movement or fire. In single player the pause menu is up when you return and Resume continues; in multiplayer the match carries on. |
| 1.5 | Run `vid_restart` during VR gameplay. | VR resumes without a restart of the game; `vr_status` shows a running session. |
| 1.6 | Set **VR Mode** to Disabled, then back to Enabled. | Desktop play is unchanged while off; VR returns when on. |
| 1.7 | Step aside, turn, and use the headset's own recenter (for example hold the Meta button). | You end up at the origin facing forward in one clean step: no jump, slide or second correction a moment later. |
| 1.8 | Resize and maximize the desktop window during VR gameplay, then set **VR Mode** to Disabled. | The mirror fills the window at each size; the flat view afterwards fits the window. |

## 2. Refresh, latency and frame cost (VR-02, VR-04)

| # | Do this | Pass when |
| --- | --- | --- |
| 2.1 | In the runtime's performance overlay, watch frame timing in the airdefense1 battle and in a dark indoor map (`game/hangar1`). | Frames hold the headset's refresh rate (90, 80 or 72 Hz) with no repeating reprojection in ordinary play. Note any sustained drops and where. |
| 2.2 | Run `python tools/tests/openxr_vr_stereo_cost.py --basepath <Quake 4> --system-runtime --render-api gl`, then `--render-api vulkan`, with the headset on. Repeat both with `--preset quality` (shadow maps on). | Report the printed stereo CPU and GPU p95/p99 against the budget for the headset's rate: 11.1 ms at 90 Hz, 12.5 ms at 80 Hz, 13.9 ms at 72 Hz. Attach all four `report.json` files. |
| 2.3 | Turn your head quickly left and right while looking at a doorway edge. | The world stays fixed in space; no lag, swim or judder. |

## 3. Aim, hands and weapons (VR-02)

| # | Do this | Pass when |
| --- | --- | --- |
| 3.1 | With **Aim** set to Controller, aim at targets near and far and fire. | Shots land on the laser dot at every distance. |
| 3.2 | Switch **Weapon Hand** to left, repeat 3.1, switch back. | The gun, laser and fire button follow the chosen hand. |
| 3.3 | Hold the foregrip of a two-handed weapon (machine gun, rocket launcher) with the off hand. | The gun steadies along both hands; releasing returns to one-handed aim without a jump. |
| 3.4 | Fire, take damage and pick up items. | Each produces controller vibration, scaled by `vr_hapticStrength`. |
| 3.5 | Zoom with a scoped weapon. | Each eye magnifies by the weapon's own zoom; the dot marks the aim. |
| 3.6 | Cycle every weapon, including the Awakening weapons in `q4xbase`. | Every gun sits in the hand at full size, gripped where its own model holds it. |
| 3.7 | Aim at a mark with the controller tilted well down, then in turn: click the weapon-hand stick, load a quick save, leave a vehicle, and sit through a cutscene. Fire at the mark after each. In multiplayer, repeat after a respawn and a teleporter. | Shots keep landing on the laser dot; it never ends up a fixed angle above or below them. |
| 3.8 | Squeeze the off-hand grip away from the gun, turn the weapon hand towards a weapon on the wheel and let go. | The wheel highlights the weapon you point towards and switches to it. |

## 4. Body, space and movement (VR-02, VR-03)

| # | Do this | Pass when |
| --- | --- | --- |
| 4.1 | Set **Play Space** to Standing; stand, crouch in the room, sit on a chair, stand again. | Your character's eye height follows yours from the floor; crouching in the room crouches the character; sitting does not lower the standing height after a recenter. |
| 4.2 | Recenter (`vr_recenter`) while crouched in Standing. | Standing up again puts you at your standing height, not above it. |
| 4.3 | Set **Play Space** to Seated, sit, recenter. | The view sits at the character's eye height wherever you recentered. |
| 4.4 | Run `vr_calibrateHeight` standing, then compare with your real height. | The measured height (in the log) is within a few centimetres of yours. |
| 4.5 | With room scale on, walk into a wall and around a crate. | The character stops at the wall; walking around the crate does not drift the view from the body. |
| 4.6 | Walk with the stick (both **Walk Direction** settings), snap-turn and smooth-turn. | Movement follows the chosen direction; turning is comfortable; the comfort vignette darkens only while the stick moves or turns you. |
| 4.7 | In multiplayer, walk in the room and around the arena with a flat client watching. | The flat client sees you move once (no doubled movement) and your own view does not correct back. |

## 5. Vehicles, cinematics and transitions (VR-02)

| # | Do this | Pass when |
| --- | --- | --- |
| 5.1 | Ride the hover tank in `game/convoy1` and the walker in `game/walker`, aiming with the head. | The seated view is stable, the HUD hangs on the line of sight, and the vehicle aims where you look. |
| 5.2 | Watch a cinematic and a loading screen. | They play on the floating screen at a comfortable distance. |
| 5.3 | Die, respawn, and load a save. | Each transition is free of flashes or a view stuck at the wrong height. |
| 5.4 | Snap-turn several times, then finish a level, and separately load a save. | The next level starts facing the way its start faces, and the save loads facing the way it was saved, not turned by the earlier snap turns. |

## 6. Menus, HUD and screens in the world (VR-01, VR-02)

| # | Do this | Pass when |
| --- | --- | --- |
| 6.1 | Point at every main-menu item and every Settings tab and row, and pull the trigger. | Every one is reachable and responds; the weapon-hand and off-hand grips step between tabs. |
| 6.2 | Read the HUD, objective text and a terminal screen. | Text is sharp and readable at the default HUD distance and width. |
| 6.3 | In single player, point the weapon hand at a door panel and a lift call screen and pull the trigger. | The cursor sits where the laser meets the screen and the screen responds. |
| 6.4 | In multiplayer, look at the objective markers in CTF (`mp/q4ctf1`). | Markers sit at HUD depth, follow your head and stay inside the HUD. |

## 7. Temporal anti-aliasing by eye (VR-05)

| # | Do this | Pass when |
| --- | --- | --- |
| 7.1 | Set `r_temporalAA 1`, look at railings and distant edges, standing still. | Edges settle in both eyes with no shimmer and no ghosting between the eyes. |
| 7.2 | Turn your head fast, and strafe past a fence. | No smearing trails or double images; detail returns within a moment. |
| 7.3 | Set `r_temporalAA 0`. | The image is unchanged apart from edges, with no flashes when switching. |
