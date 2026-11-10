# Virtual reality (OpenXR)

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

openQ4 can play Quake 4 on a PC VR headset through OpenXR, the cross-vendor
VR standard. You see the game in stereo at your headset's resolution, look
around with your head and aim with a motion controller. The HUD floats in
front of you, and menus, loading screens and cinematics appear on a floating
screen.

VR mode is **experimental**. It works with the single-player campaign and
multiplayer, and needs no extra content beyond your Quake 4 install.

## What you need

- A PC VR headset and an **OpenXR runtime that supports OpenGL or Vulkan**,
  set as the system's active OpenXR runtime:
  - **SteamVR** (Valve Index, HTC Vive, Pimax, Bigscreen and many others);
  - the **Meta Quest Link** PC app for Quest headsets over Link or Air Link;
  - **Monado** on Linux.

  Windows Mixed Reality's own runtime does not support OpenGL. On a WMR
  headset, run the game through SteamVR instead.
- Either renderer: **OpenGL** (the default) or **Vulkan** (`r_renderApi
  vulkan`). On Vulkan the runtime picks the GPU your headset is attached to,
  so turning VR on for the first time restarts the renderer once.
- On Linux, either renderer works on an X11 desktop. On a Wayland desktop,
  use Vulkan, or OpenGL with a runtime that can bind it there (Monado can;
  SteamVR cannot, so with SteamVR start openQ4 with the environment variable
  `OPENQ4_FORCE_X11=1` to run it through XWayland).

## Turning VR on

1. Start your headset and its OpenXR runtime (for example SteamVR).
2. In openQ4, open **Settings > Game Options**, choose the **Virtual
   Reality** tab, and set **VR Mode** to Enabled. VR starts at once. From the
   console, `vr_enable 1` does the same.

   The setting is saved, so the next launch starts in VR on its own. You can
   also launch with `+set vr_enable 1`.

The same tab sets aiming, turning, movement, the laser sight, vibration, the
two-handed grip, the comfort vignette, physical crouching and the play space.
In the headset, point at a tab and pull the trigger to open it, or squeeze
the weapon-hand grip to step to the next tab.

When VR is on, the desktop window shows what your left eye sees. To turn VR
off again, set VR Mode back to Disabled (you can point at it in the headset),
or enter `vr_enable 0`.

The same Virtual Reality section holds the settings players change most:
aim with the controller or your head, weapon hand, snap or smooth turning,
the snap angle, walk direction, room scale, the laser sight and vibration.
The tables below list everything, including the console-only settings.

If the headset isn't connected yet, openQ4 waits for it and starts VR as soon
as it appears.

In single player, taking the headset off or opening your runtime's own menu
(the SteamVR dashboard, the Meta menu) pauses the game on the pause menu, so
nothing happens to you while you can't see or steer. Resume from the menu when
you're back. Set `vr_pauseOnFocusLoss 0` to keep the game running instead.
Multiplayer can't pause, so there the game carries on.

If you quit SteamVR, or the runtime closes VR some other way, the game carries
on in its desktop window (paused on the menu in single player). Enter
`vr_restart` to go back into VR; it looks for the runtime and the headset at
once. `vr_enable` stays on, so the next launch starts in VR as usual.

If an XR frame or session fails, openQ4 clears held VR controls and returns to
desktop rendering. Session failures retry after three seconds; a stopped or lost
runtime waits for `vr_restart`. This recovery has been checked with a simulated
OpenXR runtime; real-headset qualification remains open.

The ordinary `screenshot` command captures a desktop view during VR gameplay.
On OpenGL, for a developer capture of the last rendered eye, use
`screenshot image _vrEyeColor0 "screenshots/vr-left.tga"` or `_vrEyeColor1` for the
right eye. These are engine render-target captures.
Use `screenshot image _vrScreenColor "screenshots/vr-hud.tga"` to capture the
separate HUD surface. The Vulkan renderer keeps its eye targets to itself, so
those image names exist only on OpenGL.

If the desktop window is minimized or hidden behind the headset's own
display, the headset keeps rendering; only the desktop mirror waits for the
window to come back.

## Controls

Controllers act like a gamepad, so the default gamepad bindings apply and you
can rebind them in the controls menu like any gamepad button.

| Control | Gameplay | Menus |
| --- | --- | --- |
| Weapon-hand trigger | Fire | Click where you point |
| Off-hand trigger | Zoom | - |
| Off-hand stick | Move | Navigate |
| Weapon-hand stick left/right | Turn | Navigate |
| Weapon-hand stick up/down | Next / previous weapon | Navigate |
| A | Jump | Select |
| B | Crouch | Back |
| X | Reload | - |
| Y | Flashlight | - |
| Off-hand grip | Weapon wheel, or hold the gun in both hands | Previous tab |
| Weapon-hand grip | Last weapon | Next tab |
| Off-hand stick click | Run / walk | - |
| Menu button | Menu | Close menu |

The weapon hand is the right hand. Set `vr_leftHanded 1` to hold the weapon
in your left hand; the triggers, grips and sticks swap with it.

Menus open on a floating screen in front of you, and the game world stays
around you while the game is paused. Point your weapon-hand controller at the
screen: a dot shows where it points, and the trigger clicks there like a
mouse. The sticks and the A and B buttons also work in menus.

Screens in the world, such as door panels, lift calls and terminals, answer
your weapon hand the same way in single player: point at one, and its cursor
appears where your laser meets it. Characters you can talk to and things you
can use take focus where you point, too. In multiplayer the server decides
what you are looking at, so there they follow your view instead.

You aim with the controller in your weapon hand, and the gun follows it. A
red laser dot sits where your shots will land, at the real distance of what
you're pointing at, so it stays sharp whether the target is near or far. It
dims when something between you and the target hides it. Set `vr_aimMode 0`
to aim with your head instead; the HUD crosshair only shows when you aim with
your head.

To steady a gun, hold it in both hands: put your off hand in front of your
weapon hand, where a rifle's foregrip would be, and squeeze its grip. The
off-hand controller gives a short buzz, and the gun then points from your
rear hand through your front hand until you let go. Squeezing the off-hand
grip anywhere else opens the weapon wheel: turn your weapon hand towards the
weapon you want and let go of the grip to switch to it.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_aimLaser` | 1 | 0 no marker, 1 laser dot, 2 laser dot and a beam from the gun |
| `vr_twoHanded` | 1 | 1 the off-hand grip on the foregrip holds the gun in both hands, 0 it always opens the weapon wheel |

The off-hand trigger zooms with a scoped weapon, such as the machinegun or
the railgun: your whole view magnifies as far as the scope would, and the
laser dot is your crosshair. Keep zoom short if magnified head movement
makes you uneasy.

The controllers vibrate: your weapon hand with every shot, and both hands
when you're hit, harder for bigger hits. Set `vr_hapticStrength` between 0
(off) and 1 (full, the default).

## Comfort

openQ4 avoids moving your view in ways your body doesn't feel:

- there is no view bob, weapon kick or screen shake in VR;
- turning is done in steps (snap turn) by default;
- while the stick moves you or turns you smoothly, the edges of your view
  darken around a clear centre (the comfort vignette), because that motion is
  what your body doesn't feel; walking about your room never triggers it;
- in-game cinematics play on the floating screen instead of moving your view.

You can walk around your room: step and your character steps with you,
sliding along walls and up stairs, so your view never ends up inside a
wall. Lean a little and only your head moves. In single player your
character won't follow you off a ledge; you can lean over it instead. In
multiplayer the server decides where everyone is, so your character walks
after you the way the stick would move it: it takes a moment to catch up,
and while you hold the stick your steps stay leans, up to
`vr_headOffsetLimit` from your character.

Crouch in your room and your character crouches too, so you can duck
behind cover and under low openings: it happens once your head is about
40 cm below where you recentred. Your view stays where your head really is
rather than dropping a second time, and stays low enough that you can't
see over what your crouching character hides behind. The B button still
crouches you too, lowering your view, which is handy when seated.

You can change these settings:

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_turnMode` | 0 | 0 snap turn, 1 smooth turn |
| `vr_snapTurnAngle` | 45 | degrees per snap turn |
| `vr_smoothTurnSpeed` | 120 | smooth turn speed, degrees per second |
| `vr_moveDirection` | 0 | 0 walk where you look, 1 walk where the off-hand controller points |
| `vr_stickDeadzone` | 0.2 | thumbstick deadzone |
| `vr_comfortVignette` | 0.5 | how strongly the comfort vignette narrows your view, from 0 (off) to 1 (a black edge around a 60 degree clear centre) |
| `vr_roomScale` | 1 | 1 your character follows your steps about the room, 0 it stays put |
| `vr_physicalCrouch` | 1 | 1 crouching in your room crouches your character, 0 only the button does |
| `vr_headOffsetLimit` | 16 | how far you can lean from your character before the view stops (game units) |

Use `vr_recenter` to make your current position and direction the front. It
helps to bind it to a key, for example `bind F12 vr_recenter`. Your
headset's own recenter (for example holding the Oculus button) does the same,
at the moment the headset applies it, so the view doesn't jump.

### Seated or standing

**Play Space** in the Virtual Reality tab (`vr_playSpace`) decides where your
height is measured from:

- **Standing** measures your height from the floor your headset knows. Your
  character's eyes stand for your eyes when you stand up straight, so
  recentering while you crouch, sit or lean only turns and moves the front:
  it never moves the floor. openQ4 learns your standing eye height the first
  time it recenters; to measure it again, stand up straight and enter
  `vr_calibrateHeight`, or set it in metres with `vr_standingHeight`.
- **Seated** measures your height from where you recentered: wherever your
  head is then becomes standing height, which suits playing in a chair.
- **Automatic**, the default, stands when your headset knows where the
  floor is and sits when it doesn't.

If you choose Standing and your runtime knows no floor, openQ4 says so and
plays seated. `vr_status` shows the play space in use, your standing eye
height and the size of your play area.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_playSpace` | 0 | 0 automatic, 1 seated, 2 standing |
| `vr_standingHeight` | 0 | standing play: your eye height above the floor in metres; 0 learns it at the next recenter |

## Display

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_renderScale` | 1.0 | multiplies the headset's recommended resolution (0.5-2.0; apply with `vr_restart`) |
| `vr_worldScale` | 39.37 | game units per metre; lower makes the world feel bigger |
| `vr_hudDistance` | 1.4 | HUD distance in metres |
| `vr_hudWidth` | 1.2 | HUD width in metres |
| `vr_hudHeightOffset` | -0.1 | HUD height relative to your eyes, in metres, when you aim with the controller (head aiming centres the HUD so its crosshair lines up) |
| `vr_screenDistance` | 2.5 | menu and cinematic screen distance in metres |
| `vr_screenWidth` | 3.0 | menu and cinematic screen width in metres |
| `vr_mirror` | 1 | desktop window: 0 black, 1 left eye, 2 the floating screen |

The floating screen and the HUD take the shape of the game window, so menus
look the same as on your monitor.

Mirrors and other reflective surfaces show each eye its own reflection.
Screens in the world are drawn once per frame for both eyes, which saves CPU
time without changing what you see; `r_vrStereoGuiShare 0` draws them for
each eye separately.

Multiplayer objective markers show flag bases and DeadZone zones on the stereo
HUD. Their symbols express status, with distance as the only text. They follow
your head independently of controller aiming and respect the HUD's size,
distance and height offset. Off-screen arrows stay inside the HUD; markers
remain at HUD depth. `hud_objectiveMarkers 0` hides them. Their projection and
gameplay rendering have automated coverage through a simulated OpenXR runtime;
real-headset readability and comfort still need testing.

Temporal anti-aliasing works in VR when you ask for it with `r_temporalAA 1`:
each eye keeps its own history, so edges settle in both eyes without the
eyes blurring into each other, and a fast head turn starts the history again
instead of smearing. The automatic setting (`r_temporalAA 2`) leaves it off in
VR, because the headset already renders at its own resolution. Dynamic
resolution stays off in VR. MSAA (`r_multiSamples`) and SMAA work as well.

## Weapon position

Each gun appears full size in your hand, gripped where its own model holds it,
with its barrel pointing along your controller's aim and the laser. If a gun
still doesn't sit naturally, nudge it relative to the controller:

| Setting | Default | Direction |
| --- | --- | --- |
| `vr_weaponOffsetX` | 0 | along the controller's aim, in game units |
| `vr_weaponOffsetY` | 0 | to the controller's left |
| `vr_weaponOffsetZ` | 0 | up |
| `vr_weaponPitch` | 0 | tilt in degrees; positive tilts down |

If you played an earlier VR build, its offsets of -5 and -3 would push the gun
out of your hand, so openQ4 resets that pair to 0 once at startup.

## Vehicles and turrets

In single player the hovertank, the walker and the mounted guns surround you
in stereo like everything else. You sit at the point the turret turns about,
and it turns after your head: look where you want to shoot, and the gun or
the walker's cockpit follows at its own speed while you stay put. Pull the
trigger to fire. The laser dot shows where the shots will land, and each shot
kicks in your weapon hand. The left stick drives the way the vehicle faces.
A vehicle that turns under you, such as a walker turning on the spot, turns
your view with it, and the comfort vignette darkens the edges of your view
while a vehicle moves you. Scripted rides seat you the same way, from the
tram's rear gun to the drop pods. Your view stays level whatever the ride
does, so a pod swung nose-up into its launch tube turns around you instead
of tipping you onto your back.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_vehicleStereo` | 1 | 1 vehicles and turrets in stereo, turned by your head; 0 on the floating screen, aimed with the controller |

## Multiplayer

VR players can join any openQ4 server. Your aim reaches the server the same
way a mouse's does. In multiplayer, shots leave from your eyes along your
controller's aim; in single player they leave from the gun in your hand.

A server built before the current development build can leave your shots a
fixed angle above or below the laser after you respawn or teleport, because
it adds the offset a mouse would need. Servers on the current build keep
shots on the laser.

## Troubleshooting

Enter `vr_status` in the console to see the runtime, headset, session state,
resolution and refresh rate openQ4 is using. `vr_debug 1` logs session events.

- **"no runtime is available"**: no OpenXR runtime is installed or active.
  Set one as active in its own settings, for example in SteamVR under
  Settings > OpenXR.
- **"supports neither OpenGL nor Vulkan"**: the active runtime can't render
  either. Switch to SteamVR or Meta Quest Link.
- **"does not support Vulkan"** or **"does not support OpenGL"**: switch
  `r_renderApi` to the other renderer and restart.
- **"cannot bind it ... native Wayland"**: your runtime can't use OpenGL on a
  Wayland desktop. Use `r_renderApi vulkan`, or start openQ4 with
  `OPENQ4_FORCE_X11=1`.
- **"the runtime did not create the Vulkan renderer's device"**: the runtime
  refused to make the Vulkan device for your headset, so VR stays off;
  `vr_restart` tries again.
- **The image looks washed out**: the runtime didn't offer an sRGB image
  format. Report your headset and runtime.
