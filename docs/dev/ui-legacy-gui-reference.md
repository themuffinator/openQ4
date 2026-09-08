# openQ4 Legacy GUI Reference

This historical bitmap/640x480 guide is retained for source-GUI translation.
The normative replacement specification is [UI Visual Design](ui-visual-design.md).
Its vector art, expandable layout and continuous animation requirements
supersede the historical restrictions below for the `idtech5-ui` project.

The rules every openQ4 menu, panel and in-game GUI follows, so that anything we
add looks like it shipped with Quake 4 rather than like a mod bolted on top.

The reference is Quake 4's own main menu and settings screens. Read this before
authoring a new `.gui`, and check a change against `guis/mainmenu.gui` and the
`p_quickjoin` panel in `guis/mpmain.gui`, which are the worked examples.

---

## 1. The look, in one paragraph

Quake 4's interface is a dark olive-green military display. Panels are black
plates with a thin olive rail drawn along their inside edge and **45-degree
chamfers cut off their corners**. Buttons are the same idea in miniature: a
plate with a 45-degree cut at the bottom left, a bright edge running down the
left and along the bottom, fading away to nothing at the right. Nothing is a
plain rectangle, nothing has a rounded corner, and nothing is fully opaque. The
accent colour is a warm orange, used only for the thing the player is pointing
at or the one line of text that matters right now.

## 2. Use the shipped art

Do not author new interface images. Everything below already ships in the
retail PK4s and is what the stock menus are built from. Adding art also breaks
the project rule about running on stock assets, so treat "there is no image for
this" as a design constraint, not a reason to make one.

### Frames

| Material | Size | Use |
| --- | --- | --- |
| `gfx/guis/mainmenu/tooltip_edge` | 256x16 | Top cap of a panel frame: olive rail along the top and both sides, 45-degree chamfers at both corners. Mirror it with `matscalex -1` + `matscaley -1` for the bottom cap. |
| `gfx/guis/mainmenu/tooltip_mid` | 256x2 | The side rails, stretched to the height between the two caps. |
| `gfx/guis/mainmenu/popup_top` / `popup_mid` / `popup_btm` / `popup_bg` | — | The larger modal frame the ban-list popup uses. Same top/mid/bottom stacking. |
| `gfx/guis/mainmenu/topbar` / `btmbar` | 1024x128 | Full-width screen bordering with the 45-degree step indentations and notch detailing. Screen furniture, not panel furniture. |
| `gfx/guis/mainmenu/screen` | 512x512 | The dark screen texture that fills a panel body. |
| `gfx/guis/mainmenu/bg_darkgrad` | — | A soft vertical gradient laid over the body to lift its top edge. |
| `gfx/guis/mainmenu/horiz_line2`, `vert_line`, `vert_line2` | — | Rules and separators. Always low alpha. |
| `gfx/guis/mainmenu/corner` | 32x32 | The small 45-degree triangle marker. It is the single most recognisable piece of Quake 4 UI vocabulary - put one to the left of a button label and one before a panel title. |

### Button plates

All of them carry the 45-degree cut at the bottom left, a highlight down the
left and along the bottom, and a fade to nothing at the right.

| Material | Size | Use |
| --- | --- | --- |
| `b6_light` | 256x32 | Standard button. The panel workhorse. |
| `b4_light` | 128x32 | Short button - icon buttons, small controls. |
| `b3_light` | 512x32 | Wide light plate. Good as a header wash behind a title. |
| `b5_dark` / `b1_dark` / `b2_dark` | — | Dark plates. Nearly invisible on a dark body except for their bottom rail, which is exactly the main menu's look; do not mix them with light plates in the same column. |

### Symbols

Use a real Quake 4 image before reaching for a word.

| Material | Meaning |
| --- | --- |
| `gfx/guis/common/gear1_still` | Settings. (`gear1` is the same art with a slow rotation - use the still one unless the motion earns its place.) |
| `gfx/guis/mainmenu/icon_arrow` | Leave / exit / go. |
| `gfx/guis/mainmenu/icon_locked`, `icon_favorite`, `icon_dedserver`, `icon_pb`, `icon_repeater` | Server-list state. |
| `gfx/guis/mainmenu/box`, `box_check` | Checkboxes. |

## 3. Colour

Declared once per GUI as `definevec4` on the desktop and referenced as
`$desktop::name` **inside scripts**. Window properties do not resolve those
names, so a `matcolor` or `forecolor` written as a property must be literal
numbers. This is a silent failure: the parser reports
`expected ',' but found 'desktop'` and drops the rest of that windowDef.

| Name | Value | Use |
| --- | --- | --- |
| olive | `0.545,0.588,0.294` | Panel and button plate tint. Plates are tinted olive, never white. |
| `corner` | `0.564,0.603,0.286,0.4` | Corner markers and rails at rest. |
| `orange` | `0.890,0.537,0,1` | Hover, focus, and the one line of status text that matters. |
| `orange_4` | `0.890,0.537,0,0.4` | The corner marker of a primary button at rest. |
| `white_8` | `1,1,1,0.8` | Button and body text. |
| `white_4` | `1,1,1,0.4` | Disabled text. |
| body fill | `0.09,0.11,0.07,0.88` | `screen` under a panel. |
| secondary text | `0.72,0.76,0.63,1` | Supporting lines. |
| tertiary text | `0.55,0.58,0.48,1` | Least important line, e.g. a server name. |

Rest alpha for a plate is `0.28`-`0.62` depending on whether it is a secondary
or primary action; hover takes it to `1`. Frame caps and rails sit at `0.95`.

## 4. Typography

| Font | Use |
| --- | --- |
| `fonts/marine` | Titles and button labels. Renders lower case as small caps, which is why mixed-case source text still reads as a heading. |
| `fonts/lowpixel` | Body text, status lines, lists. |

- Button labels `0.24`-`0.26`; panel titles `0.27`-`0.30`; body `0.17`; status
  `0.24`.
- Always set `textspacing -1`. The stock menus do, and text set without it sits
  noticeably looser than everything around it.
- Text is drawn with its first baseline at `rect.y + lineHeight`, **not**
  centred in the rect. A label inside a 32-high plate therefore needs its own
  windowDef at about `plate.y + 7`, which is why the stock menus keep the plate,
  the label and the corner marker as three separate windows.
- Localise everything. A visible string is a `#str_` lookup, never a literal.

## 5. Anatomy

### A button

Three windows. The plate is the interactive one - it owns the rect the player
clicks, and it drives the other two.

```
windowDef qj_b_join      rect 336,190,172,32   background b6_light   matcolor <olive>,0.62
windowDef qj_t_join      rect 352,197,140,18   text, fonts/marine, 0.26, forecolor 1,1,1,0.8
windowDef qj_c_join      rect 344,203,9,9      background corner     matcolor <orange_4>
```

Label inset is 16 from the plate's left edge; the corner marker sits at plate
`+8` horizontally and roughly centres on the label.

### Hover

Instant in, eased out. That asymmetry is the Quake 4 feel and is worth copying
exactly:

```
onMouseEnter {
    if ( "desktop::active" == 0 ) {
        stoptransitions "qj_b_join" ;
        set        "qj_b_join::matcolor_w" "1" ;
        transition "qj_t_join::forecolor" "$desktop::white_8" "$desktop::orange" "0" ;
        transition "qj_c_join::matcolor"  "$desktop::orange_4" "$desktop::orange" "0" ;
        set        "qj_t_join::textscale" "0.27" ;
        set        "cmd" "play main_menu_mouseover" ;
    }
}
onMouseExit {
    if ( "desktop::active" == 0 ) {
        transition "qj_b_join::matcolor_w" "1" "0.62" "300" ;
        transition "qj_t_join::forecolor" "$desktop::orange" "$desktop::white_8" "300" ;
        transition "qj_c_join::matcolor"  "$desktop::orange" "$desktop::orange_4" "300" ;
        transition "qj_t_join::textscale" "0.27" "0.26" "300" ;
    }
}
```

- The label grows by exactly `0.01` on hover. It is a very small move and it is
  what makes the buttons feel physical.
- `play main_menu_mouseover` on enter, `play main_menu_selection` on action.
  A button that does not make a sound reads as broken.
- Guard both handlers with `"desktop::active" == 0` so a running animation or a
  modal cannot be clicked through.
- `stoptransitions` names the window that *started* the transitions, which is
  the interactive one, not the window being animated.

### A panel

```
qj_frame_top   tooltip_edge   x,y,w,16                       matcolor 1,1,1,0.95
qj_frame_mid   tooltip_mid    x,y+16,w,h-32                  matcolor 1,1,1,0.95
qj_frame_btm   tooltip_edge   x,y+h-16,w,16  matscale -1,-1  matcolor 1,1,1,0.95
qj_screen      screen         x+4,y+4,w-8,h-8                matcolor 0.09,0.11,0.07,0.88
qj_grad        bg_darkgrad    x+4,y+4,w-8,h-8                matcolor <olive>,0.14
qj_head        b3_light       x+4,y+4,w-8,26                 matcolor 1,1,1,0.16
qj_head_rule   horiz_line2    x+4,y+28,w-8,6                 matcolor 1,1,1,0.3
```

Body inset from the frame is 4. Content inset from the frame is 16. A header
band is 26 high with the title baseline inside it and a rule under it.

## 6. Layout

- **Author inside the plain 640x480 canvas.** Aspect correction is on by
  default (`ui_aspectCorrection`), so a panel authored there keeps its 4:3 shape
  and stays centred on a wide display. Only full-bleed backgrounds should read
  `gui::virtual_screen_x_expand` / `_y_expand` to cover the extra width.
- **Size the panel to its content.** If a panel's content varies - a gametype
  with four join options versus one - give the frame two heights and switch
  them with `visible ( expr )` rather than leaving a third of the panel empty.
- **A partial-screen panel does not dim the screen.** Quake 4 does not, and
  neither should we; if the panel needs the background to recede, soften it
  (see below) rather than laying a black sheet over the whole display.
- Two columns work well: information on the left, actions on the right.
- Never let a control cross the frame rail.

## 7. Softening the scene behind an in-game panel

A panel that covers part of the screen over live gameplay uses the Raven
special-effect blur, not a dimming quad:

```cpp
renderSystem->SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 0..3, 0.0f ); // no tint
renderSystem->SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 4, 0.2f );    // range
renderSystem->SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 5, 0.004f );  // focus
renderSystem->SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 6, 0.85f );   // strength
renderSystem->SetSpecialEffectParm( SPECIAL_EFFECT_BLUR, 7, 512.0f );  // distance scale
renderSystem->SetSpecialEffect( SPECIAL_EFFECT_BLUR, true );
```

**Parm 6 is the effect strength and must not be zero.** Leaving it at zero asks
for no effect and Vulkan correctly draws nothing. Parm 4 is the distance over
which the image reaches full blur and parm 5 the focus point, both measured
against parm 7; focusing just past the near plane softens the whole scene rather
than picking out a subject. `r_forceSpecialEffects 1` turns the pass on without
any game state, which is the way to tell "the controller is off" apart from "the
pass is broken".

Turn the effect off on every path that can leave the panel, including map
shutdown, and never cancel it while another owner (the arena presentation) has
it enabled.

## 8. Checklist

- [ ] Every visible string is a `#str_` lookup, present in all six `.lang` files.
- [ ] Every `matcolor` / `forecolor` written as a property is literal numbers;
      `$desktop::` names appear only inside scripts.
- [ ] Buttons are plate + label + corner marker, with the instant-in /
      300ms-out hover and both menu sounds.
- [ ] The panel is framed with real Quake 4 art and has 45-degree corners.
- [ ] Nothing reads `virtual_screen_x_expand` unless it is a full-bleed
      background.
- [ ] Launched and looked at, on both `r_renderApi gl` and `vulkan`, at a
      widescreen resolution.
- [ ] `python tools/tests/match_control_ui_contract.py` and
      `python tools/tests/lang_table_encoding.py` pass.

## 9. Traps

- A GUI parse error is a **warning**, not a failure. The engine logs
  `mpmain.gui, line N: expected ...` and silently drops the rest of that
  windowDef, so the menu comes up looking almost right. Grep the log for
  `.gui, line` after any GUI edit.
- `testGUI guis/<name>.gui` renders a GUI standalone with no game state. It is
  the fastest way to separate "this does not draw" from "this is never made
  visible".
- A loose `guis/*.gui` under `.install/baseoq4/` overrides the packed copy -
  that directory is first in the search path. It is the fast iteration loop;
  delete the file when you are done or it will shadow your next build.
- Window names are resolved after the whole GUI is parsed, so a script may
  reference a window declared later in the file.

Related: [`stock-asset-baseline.md`](stock-asset-baseline.md),
[`renderer-validation-matrix.md`](renderer-validation-matrix.md).
