# Depth of Field for Screenshots and Video

openQ4 has a camera-style depth of field for promotional shots: the subject
sharp, the foreground and background melting into soft, round (or bladed)
bokeh. It is a capture tool, not a setting: it has no menu entry, and its
console variables are cheats. They work in single player, and in multiplayer
only on a server with cheats on; joining a server without cheats resets them.

The weapon wheel and map scripts use the same effect for players: while the
wheel is open, your weapon stays raised, it and everything within about
three metres stay sharp, the distance falls out of focus, and the crosshair
hides; a screen you are using (a terminal, a door panel) stays sharp while
the room behind it softens; cutscenes can pull focus between characters. That needs no cheats: the
**Depth of Field** setting (Settings → Display → Effects, `r_depthOfField`, on
from the Enhanced preset up) turns it on or off. With it off, the wheel blurs
the whole scene as before.

## Quick start

Open the console and type:

```
r_dof 1
```

The camera focuses on whatever is under the middle of the screen and eases
to it as you look around. To light up the bokeh, narrow the field of view and
open the lens:

```
fov 40
r_dofFStop 1.4
```

Take the shot with `screenshot`, or record with `recordVideo` (see
[Record Video](video-capture.md)). `r_dof 0` turns it off.

## How the lens behaves

The effect is a thin-lens camera on a full-frame (36 mm) sensor. Unless you
set a focal length, it uses the lens that gives your current field of view,
so it behaves the way a real camera does:

- A **narrower field of view** (a longer lens) gives a shallower depth of
  field. At the default 90 degree view the lens is a wide 18 mm, which keeps
  most of a scene sharp; at `fov 40` it is about a 50 mm, at `fov 20` about
  100 mm.
- A **smaller f-number** opens the aperture and blurs more. Halving
  `r_dofFStop` doubles the blur.
- A **closer focus** blurs the background more.

One world unit counts as one inch.

## Settings

| Variable | Default | What it does |
| --- | --- | --- |
| `r_dof` | 0 | Turns the camera on |
| `r_dofFocusDistance` | 0 | Focus distance in world units; 0 focuses on the depth under the focus point |
| `r_dofFocusX`, `r_dofFocusY` | 0.5, 0.5 | The autofocus point, from the left and top edges (0 to 1) |
| `r_dofFocusSpeed` | 4 | How quickly autofocus pulls to a new distance; 0 snaps at once |
| `r_dofFStop` | 2.8 | Aperture as an f-number |
| `r_dofFocalLength` | 0 | Focal length in millimetres; 0 follows the field of view |
| `r_dofFocusRange` | 0 | Depth kept fully sharp on each side of the focus distance, in world units |
| `r_dofBlurScale` | 1 | Multiplies the lens blur, for a look beyond a real lens |
| `r_dofNearBlur`, `r_dofFarBlur` | 1, 1 | Scale the blur in front of and behind the focus plane separately |
| `r_dofMaxBlur` | 32 | The largest blur radius, in pixels of a 1080-line picture |
| `r_dofQuality` | 2 | 0, 1 and 2 take 32, 64 and 128 samples at half resolution; 3 takes 192 at full resolution |
| `r_dofBlades` | 0 | Aperture blades: 0 for round bokeh, 3 to 12 for a polygon |
| `r_dofBladeRotation` | 0 | Turns the polygon, in degrees |
| `r_dofBladeRoundness` | 0.25 | Curves the blades, from straight (0) to round (1) |
| `r_dofHighlights` | 1 | How strongly bright points bloom into bokeh discs |
| `r_dofHighlightThreshold` | 0.7 | The brightness above which a point counts as a highlight |
| `r_dofDebug` | 0 | Shows the blur instead of the picture: orange in front of the focus, blue behind, green in focus, and a white ring at the autofocus point |

Blur sizes follow the picture's height, so a shot looks the same at 1080p
and 4K.

## Recipes

A character portrait, focus held on the subject however the camera moves:

```
r_dof 1
fov 30
r_dofFocusDistance 160
r_dofFStop 2
r_dofQuality 3
```

Stop the action first if you want the subject to hold still: `g_stopTime 1`
freezes the world, and `noclip` and `notarget` let you place the camera.
Hide your own weapon with `ui_showGun 0`, or leave it in shot: in front of
the focus plane it blurs like any other close object.

Cinematic bokeh with a six-bladed aperture:

```
r_dofBlades 6
r_dofBladeRotation 15
r_dofBladeRoundness 0.1
r_dofHighlights 2
```

A rack focus for a video: set `r_dofFocusSpeed 1.5` and move the focus
point with `r_dofFocusX` and `r_dofFocusY`, or change `r_dofFocusDistance`
between two values while recording.

To check what is in focus before you shoot, turn on `r_dofDebug 1`.

## For map and mod authors: scripted depth of field

Map scripts drive the same lens for cinematics, with four events on `sys`:

```
sys.setDepthOfField( float focusDistance, float fStop, float blendTime );
sys.setDepthOfFieldFocus( entity focus, float fStop, float blendTime );
sys.setDepthOfFieldLens( float focalLength, float focusRange, float maxBlur );
sys.clearDepthOfField( float blendTime );
```

- `focusDistance` is in world units along the view (0 autofocuses on the
  centre of the view); `blendTime` is in seconds and eases from wherever the
  focus stands now, so calls chain into smooth focus pulls.
- `setDepthOfFieldFocus` follows an entity as it and the camera move,
  focusing on an actor's eyes. Passing `$null_entity` autofocuses.
- `setDepthOfFieldLens` sets the focal length in millimetres (0 follows the
  camera's field of view, so a zoomed cinematic camera gets a shallower
  focus), a depth kept fully sharp each side of the focus, and the largest
  blur in pixels of a 1080-line picture (0 keeps the default, 24).
- `clearDepthOfField` fades the effect out.

A rack focus from a marine to the door behind him:

```
sys.setDepthOfFieldLens( 85, 0, 0 );
sys.setDepthOfFieldFocus( $marine_cortez, 2, 0.5 );
sys.wait( 3 );
sys.setDepthOfField( 400, 2, 1.5 );
```

The events need no declaration: the script compiler knows them, so they work
in any openQ4 script without a `scriptEvent` line and leave Quake 4's own
scripts, and their saved games, unchanged. The state is kept in saved games.
The player's Depth of Field setting decides whether it is drawn, and the
console `r_dof` camera, when on, takes over from a script.

## Notes

- The effect runs on OpenGL and Vulkan. The OpenGL ES renderer (Android) does
  not draw it, and VR headsets never get it.
- It sits in front of motion blur and bloom, so blurred highlights still
  glow. The HUD, menus and console stay sharp.
- `r_skipPostProcess 1` turns it off, along with the other post effects.
- `r_dofQuality 3` with a large blur is the most expensive post effect in the
  game. That is fine for captures; turn it off for play.
