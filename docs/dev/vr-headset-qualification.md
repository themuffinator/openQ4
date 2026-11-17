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
| 1.9 | Load the runtime with another application or a heavy desktop task while in VR gameplay, so the compositor stalls for a moment. | The session survives a short stall (the console may say a swapchain image was waited for again); only a stall of more than a second returns VR to the desktop. |

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
| 3.9 | Hold the gun close to your chest, at arm's length, overhead and across your body. Watch the weapon arm. | A forearm and an upper arm join your shoulder to the hand on the grip at every reach, bending at the elbow; no gap, kink or stray second forearm. |
| 3.10 | Move the off hand about freely, then squeeze its grip on the foregrip, release, and reload. | The off hand follows its controller; it goes to the gun for the two-handed grip and for the reload, and comes back without a snap. |
| 3.11 | Cycle every weapon, including the Awakening weapons in `q4xbase`, looking at both arms. | Every gun keeps both arms attached; a gun whose model has no arms of its own shows the character's own hands instead. |
| 3.12 | Switch **Weapon Hand** to left and look at both hands. | The character's own (simpler) hands hold the gun and follow the right controller; no modelled right hand sits on a left arm. |
| 3.13 | Put the gun hand behind a door frame, a crate and a wall corner. | The gun is hidden by what the hand is behind, at the depth both eyes agree on. |
| 3.14 | On Valve Index controllers, hold both controllers normally without squeezing. | No weapon switch, no weapon wheel and no two-handed grip until you squeeze. |
| 3.15 | On Valve Index controllers, squeeze the left trackpad firmly, then rest a thumb on it without pressing. | The firm squeeze toggles the flashlight; a resting thumb does nothing. |
| 3.16 | On HTC Vive wands, press each trackpad up, down, left, right and in the middle; then rest a thumb on the right pad without pressing. | Right: up crouches, down jumps, left and right turn, the middle recentres when held. Left: up toggles the flashlight, down reloads, the middle runs. A resting thumb on the right pad neither turns nor changes weapon; the left pad walks as before. |
| 3.17 | On Windows Mixed Reality controllers, press each touchpad in its upper and lower half; click both thumbsticks. | Upper half: crouch (right), flashlight (left). Lower half: jump (right), reload (left). The thumbstick clicks keep their own actions. |
| 3.18 | With a mirror or your shadow in view, point the gun straight up, then straight down, rolling your wrist. | Your character in the mirror or shadow keeps facing one way; it no longer spins on the spot. |

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
| 4.8 | With room scale on, take a few slow steps about the room, in single player and in multiplayer, watching a nearby edge. | The view glides with your head while the character follows: no shimmer or step back each moment, and in multiplayer the comfort vignette stays open. |
| 4.9 | Look down at yourself standing, then at your feet. | A chest, belt, legs and feet stand under you at your own size, feet on the floor where yours are; looking down does not show the inside of the collar. |
| 4.10 | Glance left and right, then turn your whole body, then walk with the stick while looking sideways. | The body stays put for a glance, comes round when you turn, and faces the way you walk; it never follows the gun hand. |
| 4.11 | Bend your knees slowly into a full crouch and stand again. | The knees bend with you and the feet stay planted; the chest never rises into view or sinks through the floor. |
| 4.12 | Stand 30 cm to one side of where you recentred and snap-turn several times; repeat with smooth turning. | The world turns about your head: no sideways slide with each turn. |
| 4.13 | Start VR with the headset lying on the desk, then put it on and hold the weapon-hand stick in for a second. | The view is at your character's height and facing forwards after the hold (the controllers buzz); `vr_standingHeight` did not take a height from the desk. |
| 4.14 | With the gun hanging at your side pointing well off to one side, start a level, load a save and step through a teleporter. | You face the way the level intends each time, whatever the hand does. |

## 5. Vehicles, cinematics and transitions (VR-02)

| # | Do this | Pass when |
| --- | --- | --- |
| 5.1 | Ride the hover tank in `game/convoy1` and the walker in `game/walker`, aiming with the head. | The seated view is stable, the HUD hangs on the line of sight, and the vehicle aims where you look. |
| 5.2 | Watch a cinematic and a loading screen. | They play on the floating screen at a comfortable distance. |
| 5.3 | Die, respawn, and load a save. | Each transition is free of flashes or a view stuck at the wrong height. |
| 5.4 | Snap-turn several times, then finish a level, and separately load a save. | The next level starts facing the way its start faces, and the save loads facing the way it was saved, not turned by the earlier snap turns. |
| 5.5 | Save in the walker's seat facing somewhere other than straight ahead, load the save, and look around. | The view loads facing the way you saved, along the walker's sights; nothing swings round after the load. |
| 5.6 | Climb onto an open gun (airdefense1's anti-aircraft gun) and look down and around. | Your character stands at the controls under you, hands on the handles, at your height; no body sits inside the walker or the hovertank. |
| 5.7 | In multiplayer on someone else's server, with the gun hanging well off to one side, respawn and go through a teleporter. | You face the way the spawn point or teleporter sends you, whatever the hand does. |

## 6. Menus, HUD and screens in the world (VR-01, VR-02)

| # | Do this | Pass when |
| --- | --- | --- |
| 6.1 | Point at every main-menu item and every Settings tab and row, and pull the trigger. | Every one is reachable and responds; the weapon-hand and off-hand grips step between tabs. |
| 6.2 | Read the HUD, objective text and a terminal screen. | Text is sharp and readable at the default HUD distance and width. |
| 6.3 | In single player, point the weapon hand at a door panel and a lift call screen and pull the trigger. | The cursor sits where the laser meets the screen and the screen responds. |
| 6.4 | In multiplayer, look at the objective markers in CTF (`mp/q4ctf1`). | Markers sit at HUD depth, follow your head and stay inside the HUD. |
| 6.5 | In multiplayer, hit another player (or `testHudOverlays`) and watch the damage number. | The number rises from the player you hit, not below or beside them. |

## 7. Temporal anti-aliasing by eye (VR-05)

| # | Do this | Pass when |
| --- | --- | --- |
| 7.1 | Set `r_temporalAA 1`, look at railings and distant edges, standing still. | Edges settle in both eyes with no shimmer and no ghosting between the eyes. |
| 7.2 | Turn your head fast, and strafe past a fence. | No smearing trails or double images; detail returns within a moment. |
| 7.3 | Set `r_temporalAA 0`. | The image is unchanged apart from edges, with no flashes when switching. |
