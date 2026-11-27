# Interface variants

27 November 2026. The retained interface (`ui_retained 1`) resolves one
**interface variant** at run time: `desktop`, `touch` or `vr`. A document keeps
one layout per screen and adapts it with variant conditions, or, where a
screen's layout genuinely differs (the touch settings pages and message box),
has a variant document beside it. The stock interface is
unaffected and stays the fallback. Design context: [visual specification](../ui-visual-design.md)
sections 13.2 (viewing profiles), 13.4 (modality) and 13.6 (touch).

## Resolution

`src/ui/retained/InterfaceVariant.h` is engine-free and resolves two values
from a set of signals.

**Device class** (`ui_interfaceDeviceClass`, read-only): `headset` while the VR
session presents; `couch` for the couch platform profile, a television or
Android TV; `handheld` for the `steamdeck` profile or its host signal; on an
Android or iOS/iPadOS build, `tablet` when SDL's `SDL_IsTablet` says so or the
window's shorter side is at least 600 dp, otherwise `phone`; anything else is a
`desk`, touchscreen or not.

**Variant** (`ui_interfaceVariantActive`, read-only), under
`ui_interfaceVariant auto`:

1. `vr` while the VR session presents (`idVRSystem::IsActive`). Nothing else
   resolves `vr`; headset detection and the VR layouts belong to the VR work.
2. Otherwise the pointer the player used last, as section 13.4's modality: a
   finger takes `touch`, a mouse or trackpad takes `desktop`.
3. Before either has been used, the device's own: `touch` on a phone, tablet
   or handheld, `desktop` elsewhere.

`ui_interfaceVariant` (archived; `auto`, `desktop`, `touch` or `vr`) forces a
variant. Forcing `vr` lays the documents out for VR without starting a headset,
which is meant for testing.

## Signals

All signals come through SDL3 (`Sys_SDL_InterfaceSignals` in
`src/sys/sdl3/sdl3_backend.cpp`); there is no new platform dependency.

| Signal | Source |
| --- | --- |
| Mobile build | `SDL_PLATFORM_ANDROID` or `SDL_PLATFORM_IOS` |
| Tablet, television | `SDL_IsTablet`, `SDL_IsTV` |
| Touchscreen | `SDL_GetTouchDevices` with `SDL_TOUCH_DEVICE_DIRECT`, or a finger |
| Keyboard | `SDL_HasKeyboard`, or a key outside the system's (Back, volume, media) while no text field holds the on-screen keyboard |
| Mouse | `SDL_HasMouse`, or a mouse that moved; touch and pen emulation never count |
| Pointer used last | a finger down; a mouse that moved 6 px past a finger's lead, pressed or scrolled; a mouse attached |
| Window size | the window's shorter side in dp (pixels over the display scale) |
| Platform profile | `com_platformProfile` (`steamdeck`, `couch`) |
| VR session | `vrSystem->IsActive()` |

SDL on Android reports no keyboards or mice, so there a keyboard or mouse
counts once it is used. Attaching a mouse (`SDL_EVENT_MOUSE_ADDED`) takes the
desktop presentation at once; the last mouse leaving gives it back to the
device. A keyboard's arrival changes `keyboard`, not the variant.

## Live changes

Every `RetainedUI_DefaultViewport` and every query re-resolves, so a headset
starting or stopping, a device attaching and a change of pointer apply in the
next frame. A change between `desktop` and `touch` waits while a finger or
mouse button is down, so a layout never moves under a press: a desktop
touchscreen's first tap completes on the layout it landed on, and the touch
layout follows. VR starting or stopping and a forced change apply at once.
The console prints each change:

```
Interface variant: touch (phone device, touch pointer, keyboard no, mouse no, request auto)
```

## Engine query

`src/ui/RetainedUI.h`:

- `RetainedUI_InterfaceVariant()` and `RetainedUI_InterfaceDeviceClass()`;
- `RetainedUI_InterfaceKeyboard()` and `RetainedUI_InterfaceMouse()`.

A dedicated server answers `desktop`, `desk` and true.

## Document sources

The runtime's `Viewport` carries `variant`, `device`, `keyboard` and `mouse`,
and documents read them through view sources, declared like `view.aspect`:

| View source | State type | Value |
| --- | --- | --- |
| `variant` | string (initial `""`) | `desktop`, `touch` or `vr` |
| `device` | string (initial `""`) | `desk`, `couch`, `handheld`, `phone`, `tablet` or `headset` |
| `keyboard` | boolean | a keyboard is attached or was used |
| `mouse` | boolean | a mouse is attached or was used |
| `physical` | number | the physical dp (the platform's density-independent pixel) one document dp spans |
| `target` | number | the document dp a 48 physical dp touch target spans in the touch variant; 0 in the others |

The compiler refuses a source of the wrong type, and the application can
neither read back nor write a view source. `tools/ui/build_retained_screens.py`
has the authoring helpers: `variant_source`, `variant_is(doc, "touch", "vr")`,
`touch_variant`, `device_is`, `keyboard_present`, `mouse_present`,
`physical_density` and `touch_target` (the `target` source).

## Touch reach

Section 13.6 asks for hit regions of at least 48 dp. For a finger, and for any
pointer in the touch or VR variant, a press that lands on no control reaches
the nearest control whose box, grown to 48 physical dp about its centre
(`48 × Viewport::displayScale` pixels), holds the point. Between close
neighbours the nearer box wins, a control already that large reaches no
further, an open list keeps the finger, and controls a modal blocks are never
reached. A mouse on the desktop keeps exact hits. The VR laser pointer gets the
same reach.

## The touch variant

**Why the menus need it.** Every desktop menu lays out on a 720 dp canvas
that spans the view's safe height (960 dp wide below 4:3), so a document dp is
not the platform's dp. On a phone in landscape (2340x1080 at density 2.625) a
document dp spans only 0.57 physical dp, and in portrait 0.43: a 36 dp row is
15 physical dp tall and 17 dp text reads at about 7 dp. Growing those
compositions cannot fix a portrait phone, so the touch variant follows one
simpler rule instead: **one column in the device's own dp**, the same in
landscape and portrait, on a phone or a tablet.

- **Buttons** are the column's width (at most 440 dp, 16 dp from the view's
  safe edges), at least 56 dp tall, 10 dp apart, with a 16 dp Marine label
  that wraps (`retained_touch.button`). Hover, focus and a press light the
  plate and turn the label orange; a button that picks one of several (a
  difficulty) is outlined in orange while chosen.
- **Text** is 16 dp for labels and values, 17 dp for a dialog's message and
  20 dp for titles, times the player's text size.
- **Nothing else**: no prompt bar, crumbs, wordmark or decorative frame. A
  finger needs none of them; Back is a button.

What each screen becomes:

| Screen | Touch layout |
| --- | --- |
| Title, the pauses (Marine, Strogg, Awakening), Single Player, Campaigns, Multiplayer, SETTINGS, New Game's difficulty, the restart screen after a death, Restart Level, the Arena pause | The column, added to the screen's own document (`touch_menu`): each button a twin of the composition's control (the same label and action, shown and enabled under the same conditions), the composition set aside. Its sizes bind to `view.physical`, so they are device dp on the canvas. |
| Every Yes/No or notice dialog of those screens and the preset check | A card the column's width in the view's middle (`touch_dialog`): the dialog's title, message and buttons, the dialog's art set aside. The card's twin takes the first focus. |
| GAME OPTIONS, CONTROLS, AUDIO, DISPLAY | Their own touch documents, `guis/menu/touch/{game,controls,audio,system}.rui`, built by the same modules at 368 by 56 dp rows (`touch_geometry`). A 64 dp header holds Back and the title; under it the page's categories as big buttons, and a category's rows once one is chosen (Back returns to the categories). Rows stand as many abreast as the view holds, in a list that scrolls. DISPLAY keeps its draft: APPLY CHANGES is a big button in a footer under the list, with the service's message beside it or over it in a narrow view; a row's return-to-preset mark is a 44 dp strip under the row, "Reset to Preset". |
| The message box | Its own touch document, `guis/menu/touch/msgbox.rui`: the session's title, message, typed field and answers in a card, under the same state names and verbs. |

The adapter (`UserInterfaceRetained.cpp`) loads `guis/menu/touch/<name>.rui`
in place of `guis/menu/[<folder>/]<name>[_awakening|_strogg].rui` while the
variant is touch and that file exists, and swaps back and forth live, keeping
the session's state and the page's draft; the screen keeps its logical name.
A touch document has no canvas: its dp is the device's, and its text sizes
divide out the device class's starting text size
(`RetainedUI_DeviceTextDefault`), so a phone's 150 % reads as 100 % there and
a larger choice still grows it.

Not yet in the touch layout, and still the desktop composition in the touch
variant: Load Game and Save Game (whose tall layout already gives portrait
rows a readable size, but whose filters and actions stay small), Chapters,
the multiplayer pages (Join, Create, Player Setup, the in-match menus, buy
menu and summary), Demos, Credits, Mods, Updates and the on-screen keyboard.
They are AND-04.3's remaining work.

**Also in the variant:**

- **Reach** (above) gives every small control a 48 dp hit region where its
  neighbours leave room; the desktop compositions still in use rely on it.
- **CONTROLS fits the devices** (`retained_controls.device_fit`). The MOUSE
  tab's mouse-only rows (Invert Mouse, MOUSE INPUT, ACCELERATION) step aside
  while `view.mouse` is false; its AIM rows stay. Where the session reports
  the built-in touch controls (`controls.touchLayout`, Android), ON-SCREEN
  CONTROLS follows: Show in Play (`in_touchControls`), Layout Preset
  (`in_touchLayoutPreset`), Control Size (`in_touchControlsScale`), Look
  Sensitivity (`in_touchLookSensitivity`) and Touch Layout (the setup's
  editor, moved here from the Other tab). With those controls and no mouse
  the category reads TOUCH (Turkish keeps its MOUSE label) and opens on the
  touch rows. The four CVars exist on every platform so the page can read
  them; only Android's controls act on them.
- **Text size by device class.** A fresh Android install starts
  `ui_retainedTextScale` at 1.5 on a phone and 1.25 on a tablet
  (`SDL_IsTablet`), set before the saved configuration runs, as a platform
  profile's defaults are, so a saved choice and Android's interface setup
  keep the player's size. The desktop compositions need that size; the touch
  layouts divide it back out. Android setup's Automatic text resolves to the
  same base times the system font scale (phone 150 %, tablet 125 %, rounded to
  5 %, at most 200 %). The Steam Deck profile starts text at 125 %.
- **Settings rows grow toward the target** in the desktop settings pages
  (`retained_settings_rows.py`), by `min(2, max(1, target / 36))`, for a touch
  variant showing them (a document without its touch file). Desktop documents
  are unchanged: every growth binding gives its authored value outside the
  touch variant (`check_text_bindings`).
- **Text entry** keeps the existing path: the platform's input method where
  the session provides one (Android), otherwise the PAD-05 on-screen
  keyboard, which a touch or controller press opens.
- **Not yet:** DISPLAY's Window Mode and resolution rows still show on a
  phone, where the platform owns the window, and CONTROLS' binding categories
  on a device with neither keyboard nor controller (AND-04.4).

The document schema allows 16,384 bindings (was 8,192): CONTROLS, the
largest page, holds about 8,200.

**Testing on a desktop.** `ui_interfaceVariant touch` forces the layout,
`ui_retainedDensity` stands in for a phone's display scale (2.625 for a 2340x1080
phone; 1.3125 for a half-size capture), `ui_retainedSafeArea "l t r b"` for its
cutout and gesture bar, `ui_interfaceTestDevices none phone` (or `keyboard`,
`mouse`, `tablet`) for the keyboard, mouse and device class the documents see.
A harness sets `controls.touchLayout` with `openq4_retainedGui state`.

## Qualification

`openq4-ui-scrollbar-runtime` covers the resolver (device classes, the
pointer's lead, VR, requests, parsing), the tracker's latch, every view source
and its typing, a live change and the reach (a finger beside, above and
between controls, a large control, the mouse on the desktop and in the touch
and VR variants) at 1 and 2.625 display density.
`ui_retained_resource_lifecycle.py` checks the viewport carries what a refresh
applied. `openq4-ui-retained-screens --controls` covers the touch rows, their
hiding without the built-in controls, the mouse rows without a mouse, the
TOUCH label and the tab's first focus; `--game`, `--audio` and `--settings`
still pass with the growth bindings. A hidden-window OpenGL client reported each
forced and automatic variant in the log, and captured Game Options, Controls
(with the touch rows) and Audio at 1280x720 (desk), 1170x540 and 540x1170 (a
phone at half size, density 1.3125, with cutout insets) and 1280x800 and
800x1280 (a tablet). The Android JVM/JNI setup tests cover the new Automatic
text. Physical devices, a Bluetooth keyboard or mouse on Android, a desktop
touchscreen, a Steam Deck and a headset have not run any of it.
