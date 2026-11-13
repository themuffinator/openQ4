# Level editor (experimental)

> This guide matches openQ4 0.14.1. Anything added since is marked **Not in 0.14.1**. See [release status](release-status.md) for what each download includes.

openQ4 has a built-in level editor for making and changing Quake 4 maps. You
edit brushes, patches and entities in 2D and 3D views, then build and play the
map in one click. It works the same on OpenGL and Vulkan, on Windows, Linux and
macOS.

The editor is experimental. It edits Quake 4 `.map` files without losing
anything it does not understand, and it compiles and plays them, but it is not
a restored copy of Quake 4's original editor (Radiant).

## Opening the editor

Enter `editorExperimental` in the console. To start openQ4 straight in the
editor, add `+editorExperimental` to the command line; openQ4 then opens in a
window rather than full screen.

The editor's interface ships in openQ4's own `baseoq4` data, so install the
complete openQ4 package, not just the executable. **Return to menu** goes back
to the main menu.

## Your first build

1. Type a map path in **Map**, relative to `maps/` (`.map` is optional), and
   press **Open**, or press **New** for an empty map.
2. Edit it (see [Editing](#editing)).
3. Type the path to save to in **Map** and press **Save**. Saves go to openQ4's
   save folder (`fs_savepath`), never into the installed game files.
4. Press **Build** to compile the saved map, then **Play** to try it.

## The workspace

The window has three toolbars, a view area and docked panels.

- **File row:** **New**, **Open**, **Save**, **Save copy**, **Undo**, **Redo**,
  **Build**, **Play**, **Stop** (cancels a build) and **Return to menu**.
  **Map** holds the path that **New**, **Open** and **Save** use (press Enter
  to open it); **Close map** closes the open map. **Save copy** writes to a new
  path and keeps you on the current map. A large map is usable at once while
  its 3D preview fills in over a few seconds.
- **View row:** **3D**, **Top**, **Front** and **Side** pick the view, and
  **Quad** shows all four. **Grid -** and **Grid +** halve and double the grid
  (0.125 to 1024 units). **Validate** checks the map, **Frame** centres the
  views on the selection, and **Panels** restores the default panel layout.
- **Edit row:** **Lock** turns texture lock on or off. **Brush** and **Patch**
  create a box brush or a flat 3x3 patch at the view centre, using the material
  set in **Tools**. **Duplicate**, **Delete**, **Group**, **Ungroup**,
  **Turn left** and **Turn right** (90 degrees about the axis the active view
  looks along) and **Snap** (to the grid) act on the selection.

### Views

The **3D** view shows the map's geometry and entity models with a light on the
camera. Point entities show their models as soon as they load; animated
characters show a coloured marker instead. It does not show the map's compiled
lighting, game simulation or every material effect.

**Top**, **Front** and **Side** are wireframe views on the grid. World brushes
are grey-green, entity geometry blue, patches yellow and the selection orange.
Very distant or tiny pieces draw as boxes so whole maps stay fast.

| Action | 3D view | Top, Front, Side |
| --- | --- | --- |
| Click | select (Shift adds or removes) | select (Shift adds or removes) |
| Drag | | move the selection in grid steps (one undo step) |
| Ctrl + drag | | draw a new brush, one grid step deep |
| Right drag | orbit (Shift: pan) | pan |
| Wheel | zoom | zoom towards the cursor |
| Arrow keys | | nudge by one grid step |
| F | frame the selection | frame the selection |
| Delete | delete the selection | delete the selection |

Clicking world or `func_group` geometry selects that one brush or patch.
Clicking any other entity's geometry selects the whole entity. Hidden and locked
layers cannot be picked.

### Panels

**Browser**, **Inspector**, **Tools** and **Problems** are docked panels. The
buttons on each panel's tab move it to the left or right column (`<`, `>`),
under the views (`v`, Problems only), or hide it (`x`). Panels in the same place
become tabs. Drag the gaps between panels and views to resize them. **Panels**
restores the default layout.

- **Browser** lists the map's **Entities**, plus **Maps**, entity **Classes**,
  **Materials**, **Models** and **Sounds**. Asset lists fill in gradually in the
  background, so the editor never stalls. Type a filter and press Enter.
  Choosing a class fills **Tools → Class**, a material fills **Tools →
  Material**, and a model or sound fills the Inspector's **Value**.
- **Inspector** shows the selected entity's properties, each with its type and
  description from the entity definition, plus the class's usage notes. Select
  a property to fill **Key** and **Value**. **Apply** refuses a value of the
  wrong type, such as `noshadows "yes"`. **Remove** deletes the property.
  **Rename** renames the entity and updates every `target`, `bind` and other
  reference to it in one undo step; the line below shows how many references it
  has.
- **Tools** creates point entities (**Class**, **Name**, then **Create** at the
  view centre), applies a **Material** to selected brushes and patches, assigns
  a **Layer**, hides, shows, locks and unlocks layers, and saves or inserts
  **Prefabs**.
- **Problems** lists validation and compiler findings, and shows build
  progress. Select one to select and frame the thing it refers to.

## Editing

Every edit can be undone. A whole drag, turn or tool action is one undo step,
and a cancelled drag leaves the map untouched. Brushes and patches keep their
exact original text until you change them, and then only the changed numbers
are rewritten.

- **Texture lock** keeps textures attached to brush faces as they move, turn or
  scale. With it off, textures stay fixed in the world. Patches always carry
  their own texture coordinates.
- Moving an entity that has an `origin` moves its geometry with it. Turning a
  point entity updates its `angle` (or `rotation` for other axes) and turns a
  light's projection; scaling a light scales its radius.
- **Group** moves selected world brushes and patches into a `func_group`, which
  Quake 4 merges back into the world when the map compiles. **Ungroup** returns
  them to the world.
- **Layers** are an `editor_layer` key on entities and groups; assigning a layer
  to world brushes groups them first. Hidden layers disappear from the views and
  cannot be picked. Locked layers stay visible but cannot be selected or moved.
- **Prefabs** are ordinary maps saved under `fs_savepath/baseoq4/prefabs/`.
  Saving a prefab stores the selection centred on its base. Inserting one places
  it at the view centre, renames entities whose names are already taken, and
  keeps the links between the inserted entities.

Each map remembers its camera, view mode, grid, texture lock, layer visibility
and selection in `fs_savepath/baseoq4/editor_experimental/views/`. They are
saved when you save, leave the editor or start a play test, and restored when
you open the map again.

## Build and play

**Build** compiles the saved file, so save first: unsaved edits are not built.
The build runs dmap (the map compiler), including collision and bot/AI
navigation, in a separate dedicated-server process with its own private folder,
so the editor keeps working while it runs. **Problems** shows progress and every
compiler warning or error, linked to the entity or brush it names.

Your map's existing compiled files (`.proc`, `.cm` and `.aas*` beside the map)
change only when a build succeeds; then they are all replaced together and
outputs the new build no longer makes are removed. A leak, a compiler error, a
crash or **Stop** leaves them untouched. A build that runs longer than an hour
is stopped (`editorExperimental_buildTimeout`).

**Play** starts a separate game window with its own private profile, so the
test's settings, saves and logs never touch yours, and loads the last successful
build (`devmap`, or `spawnServer` for maps under `mp/`). The editor stays open
with your map, selection and cameras unchanged; close the test window to carry
on editing.

## Checking your map

**Validate** (or `mapEdit validate`) lists these problems, each linked to where
it is:

- duplicate entity names, missing `target` or `bind` entities, and unknown
  classes;
- property values of the wrong type, malformed origins, and models, materials
  and sounds that do not exist (once the asset lists have finished loading);
- unreadable or broken brushes and patches, and old-format pieces that are kept
  but cannot be edited;
- a single-player map with no `info_player_*` start.

Compiler findings from the last build are added to the list.

## Saving, backups and recovery

The editor works on its own copy of the map; changes do not affect the running
game. It opens maps from the normal game files, including installed PK4s
(Quake 4's packages), and saves to `fs_savepath/baseoq4/maps/`, never changing
installed packages.

- When you save over a changed map, its previous version is kept beside it as a
  `.map.bak`.
- Saving to a different path that already holds a map is refused; open that map
  first or choose a new path.
- Every few seconds the editor checks whether another program changed the open
  map, and tells you. It then refuses to save over that file: use **Save copy**,
  or `mapEdit reload` to load the other program's version.
- **New**, **Open** and **Close map** ask before discarding unsaved work, and an
  open that fails keeps your current map.
- Undo keeps up to 256 steps, within 16 MiB.

While a map has unsaved changes, the editor writes a recovery copy every two
minutes and when openQ4 quits. Recovery copies are kept under
`fs_savepath/baseoq4/editor_experimental/recovery/`. Run
`mapEdit recover <map>` to open the latest one as an unnamed map, then save it
under a new name.

## Settings

| Setting | Default | What it does |
| --- | --- | --- |
| `editorExperimental_autoSaveSeconds` | 120 | Seconds between recovery copies; `0` turns the timer off. |
| `editorExperimental_externalCheckSeconds` | 3 | Seconds between checks for changes made by other programs; `0` turns them off. |
| `editorExperimental_buildTimeout` | 3600 | Seconds before a build is stopped. |
| `editorExperimental_playArgs` | "" | Extra command-line arguments for the play-test window. |
| `editorExperimental_layout` | "" | The saved panel layout. **Panels** resets it. |

## Console commands

`mapEdit` and `editorExperimental` are the same command, and
`editorExperimental help` lists everything it can do. Entity handles last for
the session: `world` names worldspawn, `selected` the first selected entity, and
`<handle>:<n>` an entity's nth brush or patch.

```text
mapEdit open game/airdefense1
mapEdit entities light
mapEdit select world:12 world:13
mapEdit move 64 0 0
mapEdit rotate 2 90
mapEdit brush -64 -64 0 64 64 128 textures/common/caulk
mapEdit group
mapEdit layer detail
mapEdit rename 25 door_main
mapEdit validate
mapEdit save tools/my_level
mapEdit build
mapEdit buildstatus
mapEdit play
```

`mapEdit outputs` lists the compiled files beside the saved map.
`mapEdit index` and `mapEdit find material floor` search the asset lists.
`mapEdit latency` reports how quickly the editor has been responding.
