# Virtual reality

> This guide matches openQ4 0.15.0. Anything new in 0.15.0 is marked **New in 0.15.0**. See [release status](release-status.md) for what each download includes.

openQ4 can play Quake 4 on a PC VR headset through OpenXR, the cross-vendor VR
standard. You see the game in 3D at your headset's resolution, look around with
your head and aim with a motion controller. The HUD floats in front of you, and
menus, loading screens and cinematics appear on a floating screen. VR works in
the single-player campaign and in multiplayer, and needs nothing beyond your
Quake 4 game files.

VR is **experimental**: it has not yet been fully tested on real headset
hardware, so please report how it runs on yours (see
[Reporting a problem](#reporting-a-problem)).

**In this guide:** [what you need](#what-you-need) ·
[quick start](#quick-start) · [turning VR on](#turning-vr-on) ·
[VR Mode and headset detection](#vr-mode-and-headset-detection) ·
[menus in VR](#menus-in-vr) · [controls](#controls) · [aiming and weapons](#aiming-and-weapons) ·
[your body](#your-body) · [comfort](#comfort) ·
[seated or standing](#seated-or-standing) · [display](#display) ·
[performance](#performance) · [weapon position](#weapon-position) ·
[vehicles and turrets](#vehicles-and-turrets) · [multiplayer](#multiplayer) ·
[The Awakening](#the-awakening) · [Meta Quest without a PC](#meta-quest-without-a-pc) ·
[troubleshooting](#troubleshooting-and-diagnostics) ·
[commands and settings](#commands-and-settings-reference)

## Quick start

1. Install Quake 4 and openQ4 as usual, and check the game runs on your
   monitor.
2. Start your headset's software (for example SteamVR or the Meta Quest Link
   app) and make it your system's active OpenXR runtime.
3. Start openQ4, open **Settings → Game Options → Virtual Reality** and set
   **VR Mode** to **On** or **Automatic**. The headset shows the menu on a
   floating screen. (**Not in 0.15.0:** if a VR runtime is installed, the title
   screen asks once whether to play in VR by itself; **Yes** sets Automatic.)
4. Stand (or sit) where you want to play, face forwards, and hold the
   weapon-hand thumbstick in for a second to recentre.
5. Point at the menu with your right controller: a dot shows where it points,
   and the trigger clicks there. Start or load a game as usual.

If the view feels too fast or too smooth, the [Comfort](#comfort) settings are
on the same tab. If nothing appears in the headset, see
[Troubleshooting](#troubleshooting-and-diagnostics).

## What you need

- A PC VR headset and an **OpenXR runtime that supports OpenGL or Vulkan**, set
  as your system's active OpenXR runtime:
  - **SteamVR** (Valve Index, HTC Vive, Pimax, Bigscreen and many others);
  - the **Meta Quest Link** PC app, for Quest headsets over Link or Air Link;
  - **Monado** on Linux.

  Windows Mixed Reality's own runtime does not support OpenGL; with a WMR
  headset, play through SteamVR instead.

  **New in 0.15.0:** a standalone build for Meta Quest is being prepared; see
  [Meta Quest without a PC](#meta-quest-without-a-pc).
- Either renderer: **OpenGL** or **Vulkan**. On Vulkan the runtime picks the
  graphics card your headset is connected to, so the first time you turn VR on
  the renderer restarts once.
- On Linux, both renderers work on an X11 desktop. On a Wayland desktop (the
  default on many recent distributions), use Vulkan, or OpenGL with a runtime
  that supports it there. Monado does; SteamVR does not, so with SteamVR start
  openQ4 with the environment variable `OPENQ4_FORCE_X11=1` to run it through
  XWayland, Wayland's X11 compatibility layer.

## Turning VR on

1. Start your headset and its OpenXR runtime (for example SteamVR).
2. In openQ4, open **Settings → Game Options → Virtual Reality** and set
   **VR Mode** to **On** (or **Automatic**, below). VR starts at once. The
   setting is saved, so the next launch starts in VR too.

In the headset, point at a tab and pull the trigger to open it, or squeeze the
weapon-hand grip to step to the next tab. The Virtual Reality tab holds the
settings players change most: aiming with the controller or your head, weapon
hand, snap or smooth turning and the snap angle, walk direction, room scale,
the laser sight, vibration, the two-handed grip, the comfort vignette, physical
crouching, the play space and (**not in 0.14.1**) your own body. The tables in
this guide list every setting, including those you can only set in the console.

When VR is on, the desktop window shows what your left eye sees, or the
floating screen while a menu is up (**not in 0.15.0**). To turn VR off, set
**VR Mode** to **Off** again (you can point at it in the headset).

From the console, `vr_enable 1` and `vr_enable 0` do the same, `vr_enable 2`
is Automatic, and you can launch with `+set vr_enable 1`.

## VR Mode and headset detection

**Not in 0.15.0.** VR Mode has three choices:

- **Off** never asks your VR software for anything, so openQ4 starts exactly
  as a desktop game.
- **On** plays in VR whenever a headset is there, and waits for one that isn't
  connected yet. This is what turning VR Mode on always did.
- **Automatic** asks your VR runtime for a headset as the game starts. If one
  answers, the game starts in VR (on OpenGL, when the renderer is left to
  choose itself); if none does, it plays on the desktop. A headset you switch
  on later doesn't take over in the middle of a fight: the menus show
  **VR headset found** with **Play in VR**, and you switch when you're ready
  (`vr_start` does the same).

openQ4 finds your VR runtime by reading which one is set as active, without
starting it: `XR_RUNTIME_JSON`, then on Windows the Khronos registry key, on
Linux the `openxr/1/active_runtime.json` files. Only On, Automatic or **Look
for Headset** actually ask the runtime, because asking can start it (SteamVR
opens when a game first asks for it). With Automatic and SteamVR, expect
SteamVR to open with openQ4.

The first time openQ4 finds a VR runtime installed and you've never chosen a
VR Mode, the title screen asks once: **play in VR by itself whenever a headset
is connected?** **Yes** sets Automatic; **No** keeps VR off. Either answer is
remembered and the question doesn't come back; `vr_offer reset` asks again.

**Look for Headset** on the Virtual Reality tab (or `vr_detect`) asks the
runtime now, whatever VR Mode is, and tells you what it found. Every decision
is written to the log as a `VR_RUNTIME`, `VR_MODE`, `VR_OFFER` or `VR_DETECT`
line, which helps when you report a problem.

## Menus in VR

**Not in 0.15.0.** While you play in the headset the menus adapt to it:

- **VR SETUP** opens over the title or pause menu the first time VR starts:
  seated or standing, **Measure My Height**, **Recentre View**, snap or smooth
  turning, your weapon hand and the comfort vignette. Open it again from
  **VR SETUP** next to EXIT on the pause menu, **Comfort Setup** on the
  Virtual Reality tab, or `vr_setup`.
- **Settings → Game Options** opens on the Virtual Reality tab.
- Settings that only change the desktop view leave the page while you're in
  VR: the flat gun's position and field of view, and stereoscopic 3D.
- Menu text is at least 25% larger (`ui_retainedVRTextScale`, from 1 to 2),
  so it reads at the floating screen's distance.
- Typing a name or a server address opens the game's own on-screen keyboard,
  which you point at and click, instead of your desktop's.
- Prompts show the controller's buttons, and the laser dot is the pointer.

### When the headset or runtime goes away

- If the headset isn't connected yet, openQ4 waits and starts VR as soon as it
  appears.
- In single player, taking the headset off or opening your runtime's own menu
  (the SteamVR dashboard, the Meta menu) pauses the game on the pause menu, so
  nothing happens to you while you can't see. Resume from the menu when you are
  back. Set `vr_pauseOnFocusLoss 0` to keep playing instead. Multiplayer can't
  pause, so the game carries on.
- If you quit SteamVR, or the runtime ends VR some other way, the game carries
  on in its desktop window (paused in single player). Enter `vr_restart` to go
  back into VR. VR Mode stays on, so the next launch starts in VR as usual.
- If VR fails during play, openQ4 releases any controls you were holding and
  carries on in the desktop window. It tries again after three seconds, or
  waits for `vr_restart` if the runtime itself stopped.
- **New in 0.15.0:** a runtime that stalls for a moment costs a frame, not
  your VR session: openQ4 waits up to a second before giving up. A failure in
  something VR can do without, such as vibration or a headset's refresh-rate
  setting, is reported in the console and VR carries on.
- If the desktop window is minimized or hidden, the headset keeps rendering;
  only the desktop view waits for the window to come back.

## Controls

The controllers act like a gamepad, so the default gamepad bindings apply and
you can rebind them in **Settings → Controls** like any gamepad button.

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
| Weapon-hand stick click, held for a second | Recentre (**not in 0.14.1**) | - |
| Menu button | Menu | Close menu |

The weapon hand is the right hand. Set `vr_leftHanded 1` (**Weapon Hand** on
the Virtual Reality tab) to hold the weapon in your left hand. The triggers,
grips and sticks swap with it: the left trigger fires, the right stick moves
you, the left stick turns, and holding the left stick in recentres. The face
buttons stay where they are, so A still jumps and Y still toggles the
flashlight.

The table shows Meta Quest Touch controllers. Controllers with fewer buttons
use these instead (**not in 0.14.1**; before, some had no flashlight or jump):

| Controller | Differences |
| --- | --- |
| Valve Index | Left B opens the menu. Squeeze the left trackpad firmly for the flashlight. |
| HTC Vive wand | The trackpad is the stick. Press it to use it as buttons. Right pad: up crouches, down jumps, left and right turn, centre is the stick click. Left pad: up toggles the flashlight, down reloads, centre runs. The right menu button also crouches. A thumb resting on the right pad does nothing until you press. |
| Windows Mixed Reality | Press the touchpad. Upper half: crouch (right) or flashlight (left). Lower half: jump (right) or reload (left). The thumbsticks work as on Touch. |

Menus open on a floating screen in front of you, and the game world stays around
you while the game is paused. Point your weapon-hand controller at the screen:
a round dot shows where it points, and the trigger clicks there like a mouse. The
sticks and the A and B buttons work in menus too. Button prompts name your
controller's buttons while you point (**after 0.15.0**; in 0.15.0 pointing
switched them to keyboard keys, and the dot was square).

In single player, screens in the world, such as door panels, lift calls and
terminals, answer your weapon hand the same way: point at one and its cursor
appears where your laser meets it. Characters you can talk to and things you
can use also respond where you point. In multiplayer the server decides what
you are looking at, so there they follow your view instead.

### Aiming and weapons

You aim with the controller in your weapon hand, and the gun follows it. A red
laser dot sits where your shots will land, at the real distance of what you are
pointing at, so it stays sharp near or far. It dims when something between you
and the target hides it. Set `vr_aimMode 0` to aim with your head instead; the
HUD crosshair only shows when you aim with your head.

**New in 0.15.0:**

- The gun is where your hand is: put your hand behind a door frame or a crate
  and the gun is hidden behind it, instead of being drawn over everything.
- In single player the dot stays while your weapon is put away, as a pointer
  for the people you can talk to and the things you can use.
- Aimed by your head, shots leave from where your head really is, so you can
  duck behind cover and shoot over it the way it looks.
- A level, a loaded save or a teleporter leaves you *facing* the way the game
  intends, wherever your gun hand hangs; they used to point the gun that way
  instead, which could start you facing a wall.

To steady a gun, hold it in both hands: put your off hand in front of your
weapon hand, where a rifle's foregrip would be, and squeeze its grip. The
off-hand controller buzzes briefly, and the gun then points from your rear hand
through your front hand until you let go. Squeezing the off-hand grip anywhere
else opens the weapon wheel: turn your weapon hand towards the weapon you want
and let go of the grip to switch to it. **After 0.15.0** you can instead push
the weapon-hand stick towards the weapon, so your gun stays on its target while
you choose; the stick doesn't turn you while the wheel is open.

The off-hand trigger zooms with a scoped weapon, such as the machinegun or the
railgun: your whole view magnifies as the scope would, and the laser dot is
your crosshair. Keep zooming short if magnified head movement makes you uneasy.

The controllers vibrate: your weapon hand with every shot, and both hands when
you are hit, harder for bigger hits. **New in 0.15.0:** the lightning gun's
beam and the gauntlet's blade hum in your hand too.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_aimMode` | 1 | 1 aim with the controller, 0 aim with your head |
| `vr_aimLaser` | 1 | 0 no marker, 1 laser dot, 2 laser dot and a beam from the gun |
| `vr_twoHanded` | 1 | 1 the off-hand grip on the foregrip holds the gun in both hands, 0 it always opens the weapon wheel |
| `vr_hapticStrength` | 1 | vibration strength, from 0 (off) to 1 (full) |
| `vr_leftHanded` | 0 | 1 holds the weapon in your left hand |

### Your body

> **New in 0.15.0.**

Look down and you see yourself: a chest, legs and feet standing where you
stand, and arms that run from your shoulders to your hands.

- Your body turns with your torso, not with your gun. Glance over your
  shoulder and it stays put; turn further, or walk, and it comes round with
  you.
- Duck, lean or crouch in your room and your knees bend to follow, with your
  feet on the floor.
- Your weapon hand stays on the gun's grip, and your arm bends at the elbow to
  reach it wherever you hold it. **After 0.15.0** the hands are the size of
  your body's (see [weapon position](#weapon-position)).
- Your other hand follows its own controller. It goes back to the gun when you
  take the two-handed grip, and while the gun needs it: to reload, and to draw
  a weapon or put it away.
- With no weapon in your hands, both hands follow their controllers.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_body` | 2 | 2 legs, torso and arms that reach your hands; 1 legs and torso only, with the gun's forearms floating as before; 0 no body |

The same choice is **Your Body** on the Virtual Reality tab: With Arms, Legs
and Torso, or Off.

Good to know:

- The arms need controller aiming (`vr_aimMode 1`). When you aim with your head
  the gun rides with your view as on a flat screen, and you get the legs and
  torso only.
- The guns are modelled for a right hand on the grip. If you hold the weapon in
  your left hand (`vr_leftHanded 1`), the gun's own hands are drawn as their
  mirror image, so a left hand grips the gun and the right hand supports it
  (**not in 0.14.1**, where your character's simpler hands held it). The gun
  itself is not mirrored, so its displays still read the right way round.
- At an open gun or turret, where your character stands at the controls, you
  see your body there with its hands on the controls (**not in 0.14.1**).
  Closed vehicles, such as the walker and the hovertank, hide you inside, so
  you have no body there, nor while a cinematic plays.
- Only you see this body. Mirrors, your shadow and other players show your
  character as before.
- The body has only been checked with openQ4's simulated headset. If it sits
  wrongly on a real one, please report it; `vr_body 0` turns it off.

## Comfort

openQ4 avoids moving your view in ways your body doesn't feel:

- there is no view bob, weapon kick or screen shake in VR;
- turning is done in steps (snap turn) by default, and a turn pivots about
  your head, so it never slides your view sideways (**not in 0.14.1**, where a
  turn pivoted about your character and shifted the view if you stood off
  centre);
- while the stick moves you or turns you smoothly, the edges of your view
  darken around a clear centre (the comfort vignette); walking about your room
  never triggers it;
- in-game cinematics play on the floating screen instead of moving your view.

You can walk around your room: step and your character steps with you, sliding
along walls and up stairs, so your view never ends up inside a wall. Lean a
little and only your head moves. In single player your character won't follow
you off a ledge; you can lean over it instead. In multiplayer the server
decides where everyone is, so your character walks after you the way the stick
would move it: it takes a moment to catch up, and while you hold the stick your
steps only lean your view, up to `vr_headOffsetLimit` from your character.

Crouch in your room and your character crouches too, so you can duck behind
cover and under low openings. It happens once your head is about 40 cm below
where you recentred. Your view stays where your head really is rather than
dropping a second time, and low enough that you can't see over what your
crouching character hides behind. The B button still crouches too, which is
handy when seated.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_turnMode` | 0 | 0 snap turn, 1 smooth turn |
| `vr_snapTurnAngle` | 45 | degrees per snap turn |
| `vr_smoothTurnSpeed` | 120 | smooth turn speed, degrees per second |
| `vr_moveDirection` | 0 | 0 walk where you look, 1 walk where the off-hand controller points |
| `vr_stickDeadzone` | 0.2 | thumbstick dead zone |
| `vr_comfortVignette` | 0.5 | how strongly the comfort vignette narrows your view, from 0 (off) to 1 (a black edge around a 60 degree clear centre) |
| `vr_roomScale` | 1 | 1 your character follows your steps about the room, 0 it stays put |
| `vr_physicalCrouch` | 1 | 1 crouching in your room crouches your character, 0 only the button does |
| `vr_headOffsetLimit` | 16 | how far you can lean from your character before the view stops (game units) |

To make your current position and direction the front, recentre: **hold the
weapon-hand stick in for a second** (the controllers buzz when it is done), or
enter `vr_recenter`. Your headset's own recenter (for example holding the
Oculus button) does the same, at the moment the headset applies it, so the
view doesn't jump. openQ4 also recentres by itself the first time your headset
hands it the controllers and at the start of every level, so a level begins
facing the way you face. **New in 0.15.0:** the stick hold and the automatic
recentre; there, use `vr_recenter` (it helps to bind it to a key, for example
`bind F12 vr_recenter`).

### Seated or standing

**Play Space** on the Virtual Reality tab (`vr_playSpace`) decides where your
height is measured from:

- **Standing** measures your height from the floor your headset knows. Your
  character's eyes match yours when you stand up straight, so recentring while
  you crouch, sit or lean only turns and moves the front; it never moves the
  floor. openQ4 learns your standing eye height the first time it recentres
  with your head at least a metre above the floor, so a headset still lying on
  the desk teaches it nothing (**not in 0.14.1**, which could learn a height
  from the desk; if your view sits too high there, use `vr_calibrateHeight`).
  To measure it again, stand up straight and enter `vr_calibrateHeight`, or set
  it in metres with `vr_standingHeight`.
- **Seated** measures your height from where you recentred: wherever your head
  is then becomes standing height, which suits playing in a chair.
- **Automatic**, the default, plays standing when your headset knows where the
  floor is and seated when it doesn't.

If you choose Standing and your runtime knows no floor, openQ4 tells you and
plays seated. `vr_status` shows the play space in use, your standing eye height
and the size of your play area.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_playSpace` | 0 | 0 automatic, 1 seated, 2 standing |
| `vr_standingHeight` | 0 | standing play: your eye height above the floor in metres; 0 learns it at the next recenter |

## Display

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_renderScale` | 1.0 | multiplies the headset's recommended resolution (0.5-2.0); VR restarts by itself to apply it |
| `vr_worldScale` | 39.37 | game units per metre; lower makes the world feel bigger |
| `vr_hudDistance` | 1.4 | HUD distance in metres |
| `vr_hudWidth` | 1.2 | HUD width in metres |
| `vr_hudHeightOffset` | -0.3 | HUD height relative to your eyes, in metres, when you aim with the controller (head aiming centres the HUD so its crosshair lines up) |
| `vr_screenDistance` | 2.5 | menu and cinematic screen distance in metres |
| `vr_screenWidth` | 3.0 | menu and cinematic screen width in metres |
| `vr_mirror` | 1 | desktop window: 0 black, 1 left eye, 2 the floating screen |
| `vr_refreshRate` | 0 | **New in 0.15.0:** headset refresh rate in Hz, where the headset lets a game choose (Meta Quest, also over Link): the fastest rate it offers up to this; 0 keeps the headset's own |
| `vr_performanceLevel` | 3 | **New in 0.15.0:** CPU and GPU clocks the headset holds, where it lets a game choose (Meta Quest): 1 power savings, 2 sustained low, 3 sustained high, 0 leaves it to the headset from the next `vr_restart` |

**New in 0.15.0:** a faster refresh rate is smoother but leaves less time for
each frame; if the game can't keep up, the headset fills in frames and motion
looks less steady. `vr_status` lists the rate in use and any heat warning the
headset reports. Most PC runtimes offer neither setting, and then both do
nothing.

The floating screen and the HUD take the shape of the game window, so menus
look the same as on your monitor. Mirrors and other reflective surfaces show
each eye its own reflection.

**After 0.15.0**, when you aim with the controller, the HUD hangs below your
line of sight, its top edge at about eye level: glance down for your health
and ammunition, and the middle of your view stays clear for what you are
shooting at. In 0.15.0 it sat 0.1 m below eye level, with its panels around
the middle of your view. If your configuration still holds that old height,
openQ4 moves it to the new one once; any other height you chose stays. For the
old placement, set `vr_hudHeightOffset -0.1`.

Multiplayer objective markers show flag bases and DeadZone zones on the HUD.
Their symbols show status, with distance as the only text. They follow your
head rather than your aim, keep to the HUD's size, distance and height, and
point from the HUD's edge when off-screen. `hud_objectiveMarkers 0` hides them.

Temporal anti-aliasing works in VR when you turn it on with `r_temporalAA 1`:
each eye keeps its own history, so edges settle in both eyes without blurring
into each other, and a fast head turn starts afresh instead of smearing. The
automatic setting (`r_temporalAA 2`) leaves it off in VR, because the headset
already renders at its own resolution. Dynamic resolution stays off in VR. MSAA
(`r_multiSamples`) and SMAA work as well.

## Performance

A headset needs a new frame for every refresh, 72 to 120 times a second, and
openQ4 draws the world once for each eye. If motion looks uneven or the
headset's performance overlay shows dropped frames:

- Lower `vr_renderScale` (for example 0.8). It scales the resolution the
  headset recommends, costs the most of any setting, and VR restarts by itself
  to apply it.
- Pick a lower refresh rate in your headset's own software (SteamVR's video
  settings, the Meta Quest Link app), or with `vr_refreshRate` on headsets that
  let a game choose.
- Use a lighter graphics preset in **Settings → Display**; shadow maps and the
  heavier lighting options cost twice in VR, once per eye.
- Try the other renderer (`r_renderApi gl` or `r_renderApi vulkan`); which is
  faster depends on your graphics card and driver.

Screens in the world, such as door panels, are drawn once per frame for both
eyes, and dynamic resolution stays off, so the image stays steady.

## Weapon position

Each gun appears in your hand gripped where its own model holds it, with its
barrel pointing along your controller's aim and the laser. Quake 4 builds its
first-person guns, and the hands holding them, at about half life size, which
a flat screen never shows. **After 0.15.0** openQ4 draws them 1.75 times as
large in VR, so the hands match your own body's arms and the machinegun is
about as long as a real rifle; in 0.15.0 the hands looked like a child's.
A long gun reaches further back too: held close to your chest, the rocket
launcher's rear comes up to your face, as a real one would rest on your
shoulder, so hold it a little further out. `vr_weaponScale` sets the size,
and 1 draws them as modelled. If a gun
still doesn't sit naturally, nudge it relative to the controller:

| Setting | Default | Direction |
| --- | --- | --- |
| `vr_weaponOffsetX` | 0 | along the controller's aim, in game units |
| `vr_weaponOffsetY` | 0 | to the controller's left |
| `vr_weaponOffsetZ` | 0 | up |
| `vr_weaponPitch` | 0 | tilt in degrees; positive tilts down |
| `vr_weaponScale` | 1.75 | size of the gun and the hands holding it, from 0.5 to 3; 1 is the size Quake 4 models them at (**after 0.15.0**) |

If you played an earlier openQ4 VR build, its old offsets of -5 and -3 push the
gun out of your hand, so openQ4 resets that pair to 0 once at startup.

## Vehicles and turrets

In single player the hovertank, the walker and the mounted guns surround you in
3D like everything else. You sit at the point the turret turns about, and it
turns after your head: look where you want to shoot, and the gun or the
walker's cockpit follows at its own speed while you stay put. Pull the trigger
to fire. The laser dot shows where shots will land, and each shot kicks in your
weapon hand. The left stick drives the way the vehicle faces.

A vehicle that turns under you, such as a walker turning on the spot, turns
your view with it, and the comfort vignette darkens the edges of your view while
a vehicle moves you. Scripted rides seat you the same way, from the tram's rear
gun to the drop pods. Your view stays level whatever the ride does, so a pod
swung nose-up into its launch tube turns around you instead of tipping you onto
your back. A save made in a vehicle or turret seat loads with you facing along
its sights, as when you climbed in.

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_vehicleStereo` | 1 | 1 vehicles and turrets in 3D, turned by your head; 0 on the floating screen, aimed with the controller |

## Multiplayer

VR players can join any openQ4 server running the same version. Your aim
reaches the server the way a mouse's does. In multiplayer, shots leave from your
eyes along your controller's aim; in single player they leave from the gun in
your hand. Damage numbers rise on the HUD from the player you hit.

Servers older than 0.14.1 can leave your shots a fixed angle above or below the
laser after you respawn or teleport. 0.14.1 servers keep shots on the laser.

**New in 0.15.0:** after you respawn or go through a teleporter, your head
faces the way the game sends you, as in single player; before, your gun did,
wherever your hand hung. Your character in other players' view no longer spins
on the spot while you point your gun straight up or down.

## The Awakening

[Quake 4: The Awakening](awakening.md) (`q4xbase`) plays in VR the same way as
the original campaign: its new weapons sit in your hand with their own grips, and its
turrets and vehicles seat you as described in
[Vehicles and turrets](#vehicles-and-turrets).

## Meta Quest without a PC

> **Not published yet.** The Quest build can only be built from source for now.

openQ4's Android version can also be built as a **standalone Meta Quest app**
(Quest 2, Quest Pro, Quest 3 and 3S). Setup, where you add your Quake 4 files,
opens as a flat window in the headset; the game then starts in VR with the
touch controls off, and you play with the Touch controllers as described above.

This build is **not published yet** and **no headset has run it**: it has only
been tried in Android's emulator with openQ4's simulated headset. Expect rough
edges once it reaches real headsets, especially in frame rate, since the Quest
draws both eyes on a phone-class graphics chip.

## Troubleshooting and diagnostics

Enter `vr_status` in the console to see the runtime, headset, session state,
resolution and refresh rate openQ4 is using. `vr_debug 1` logs session events.

- **"no runtime is available"**: no OpenXR runtime is installed or active. Set
  one as active in its own settings, for example in SteamVR under
  **Settings → OpenXR**.
- **"supports neither OpenGL nor Vulkan"**: the active runtime can't draw with
  either. Switch to SteamVR or Meta Quest Link.
- **"does not support Vulkan"** or **"does not support OpenGL"**: switch to
  the other renderer in **Settings → Display → Rendering Backend** (or set
  `r_renderApi gl` or `r_renderApi vulkan`) and restart.
- **"cannot bind it ... native Wayland"**: your runtime can't use OpenGL on a
  Wayland desktop. Use `r_renderApi vulkan`, or start openQ4 with
  `OPENQ4_FORCE_X11=1`.
- **"the runtime did not create the Vulkan renderer's device"**: the runtime
  refused to set up Vulkan for your headset, so VR stays off; `vr_restart` tries
  again.
- **The image looks washed out**: the runtime didn't offer the colour format
  openQ4 needs (sRGB). Report your headset and runtime.
- **Valve Index: weapons switch or the weapon wheel opens while you only hold
  the controllers** (0.14.1): the grip there answered your fingers resting on
  it. **New in 0.15.0:** it now takes a real squeeze.
- **Everything is too high, too low or off to one side**: recentre by holding
  the weapon-hand stick in for a second, standing where and facing how you
  want to play.
- **You stand too tall or too short in standing play**: stand up straight and
  enter `vr_calibrateHeight`.
- **You feel queasy**: keep snap turning (`vr_turnMode 0`), raise
  `vr_comfortVignette` towards 1, play seated, and take short breaks. Walking
  about your room never causes it, so room scale is the gentlest way to move.
- **The world feels too big or too small**: change `vr_worldScale` (lower makes
  the world bigger).
- **The HUD is hard to read, too close, or in the way**: change `vr_hudDistance`,
  `vr_hudWidth` and `vr_hudHeightOffset` (more negative hangs it lower).
- **The gun or the hands look too big or too small**: change `vr_weaponScale`.
- **One arm hangs at your side**: its controller has lost tracking (asleep,
  out of the sensors' view, or its battery is flat). It follows again as soon
  as the runtime tracks it.
- **HTC Vive: you turned when you meant to jump or crouch**: a pad press is read
  where your thumb is as the press starts. Put your thumb on the top or bottom
  of the pad first, then press.
- **A gun sits oddly in your hand**: nudge it with the
  [weapon position](#weapon-position) settings.

Screenshots, saves and demos:

- The ordinary `screenshot` command captures the desktop view during VR play.
  On OpenGL only, you can capture the last frame of one eye with
  `screenshot image _vrEyeColor0 "screenshots/vr-left.tga"` (`_vrEyeColor1` for
  the right eye), or the HUD with
  `screenshot image _vrScreenColor "screenshots/vr-hud.tga"`.
- A checkpoint's picture in the load menu shows your left eye's view, whatever
  the desktop window shows.
- A demo recorded in VR (`recordDemo`) plays back flat, the way the desktop
  window shows VR: your left eye and the HUD, without the laser dot or the
  comfort vignette. Screen fades are not recorded.

Screens in the world are drawn once per frame for both eyes, which saves
processor time without changing what you see; `r_vrStereoGuiShare 0` draws them
for each eye separately if you suspect a problem there.

## Reporting a problem

Please report how VR runs on your headset, good or bad, on the project's
[issue tracker](https://github.com/themuffinator/openQ4/issues). The
[VR headset report form](https://github.com/themuffinator/openQ4/issues/new?template=vr-headset-report.yml)
asks for everything below. A report that it all worked is as useful as a bug.
Include:

- your headset, controllers and OpenXR runtime (with its version), and whether
  you used OpenGL or Vulkan;
- the output of `vr_status`, entered while the problem shows;
- `openq4.log`, the same log file the [display settings](display-settings.md)
  guide asks for; enter `vr_debug 1` before reproducing the problem so the log
  records VR's session events;
- what you did, what you expected, and what happened instead.

## Commands and settings reference

| Command | What it does |
| --- | --- |
| `vr_status` | prints the runtime, headset, session state, play space, standing height, resolution and refresh rate |
| `vr_restart` | restarts VR, for example after the runtime stopped or to apply `vr_renderScale` |
| `vr_recenter` | makes where you stand and face now the front (holding the weapon-hand stick in does the same) |
| `vr_calibrateHeight` | standing play: measures your standing eye height now |

General settings (the others are in the sections above):

| Setting | Default | What it does |
| --- | --- | --- |
| `vr_enable` | 0 | **VR Mode** on the Virtual Reality tab: 0 off, 1 on (plays in VR whenever a headset is there), 2 automatic (**not in 0.15.0**: VR when a headset answers at launch, else the desktop, offering one that connects later); takes effect at once |
| `vr_offerAnswered` | 0 | **Not in 0.15.0.** 1 once you answered the title screen's one-time question about playing in VR |
| `vr_setupDone` | 0 | **Not in 0.15.0.** 1 once VR SETUP opened at your first VR entry |
| `vr_detect` | | **Not in 0.15.0.** Command: asks your VR runtime for a headset now and reports it |
| `vr_start` | | **Not in 0.15.0.** Command: with Automatic, plays in VR on a headset it found after launch |
| `vr_setup` | | **Not in 0.15.0.** Command: opens VR SETUP over the title or pause menu |
| `vr_offer` | | **Not in 0.15.0.** Command: `status`, `yes`, `no`, `open`, or `reset` to ask the one-time question again |
| `ui_retainedVRTextScale` | 1.25 | **Not in 0.15.0.** Menu text size while in VR, at least your usual text size |
| `vr_pauseOnFocusLoss` | 1 | single player: 1 opens the pause menu when the headset comes off or its own menu takes the controllers |
| `vr_mirror` | 1 | desktop window: 0 black, 1 left eye, 2 the floating screen |
| `vr_debug` | 0 | 1 logs VR session events, 2 also logs every frame (for problem reports) |

Every setting this guide lists is saved with your configuration except
`vr_debug`.
