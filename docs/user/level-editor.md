# Level editor (experimental)

> **Development documentation:** This page follows the current source tree and may describe changes newer than the public download. See [release status and released-version guides](release-status.md) before using it with a release package.

Run `editorExperimental` in the client console to open the level editor
workspace. It is built on the engine's own GUI and renderer interfaces, so it
works the same on OpenGL and Vulkan, on Windows, Linux and macOS. Install the
matching bundled data with the engine: the workspace interface and its
translations ship in `baseoq4`.

The editor is still marked experimental: it edits Quake 4 `.map` sources
losslessly and compiles and plays them, but it is not a restored MFC Radiant.
The legacy Radiant source and its `editor` command in tool-enabled builds are
kept unchanged and are not built. The editor's settings, recovery files, view
state and build sandbox live in their own `editor_experimental` namespace.

## The workspace

The window has three toolbars, a view area and docked panels.

- **File row:** **New**, **Open**, **Save**, **Save copy**, **Undo**, **Redo**,
  **Build**, **Play**, **Stop** (cancel a build) and **Return to menu**. Type a
  map path (relative to `maps/`, `.map` optional) in **Map** and press Enter or
  **Open**; **Close map** closes it. Opening a large map is interactive at
  once: the 3D preview fills in over the next few seconds.
- **View row:** **3D**, **Top**, **Front** and **Side** pick the view; **Quad**
  shows all four at once. **Grid -** and **Grid +** halve and double the grid
  (0.125 to 1024 units). **Validate** checks the map, **Frame** centres the
  views on the selection, and **Panels** restores the default panel layout.
- **Edit row:** **Lock** toggles texture lock. **Brush** and **Patch** create
  a box brush or a flat 3x3 patch at the view centre, using the material in
  **Tools**. **Duplicate**, **Delete**, **Group**, **Ungroup**, **Turn left**
  and **Turn right** (90 degrees about the axis the active view looks along)
  and **Snap** (to the grid) act on the selection.

### Views

The 3D view renders the source geometry and entity models with a camera
light. Point entities show their static models (from their `model` key or
their entityDef) as soon as these load; skinned characters keep a coloured
marker. It does not show compiled lighting, game simulation or every material
effect.

The **Top**, **Front** and **Side** views are wireframes on the grid. World
brushes are grey-green, entity geometry blue, patches yellow and the selection
orange. Very distant or tiny primitives draw as boxes so whole maps stay fast.

| Action | 3D view | Top, Front, Side |
| --- | --- | --- |
| Click | select (Shift adds or removes) | select (Shift adds or removes) |
| Drag | | move the selection by grid steps; one undo step |
| Ctrl + drag | | draw a new brush (one grid-depth step deep) |
| Right drag | orbit (Shift: pan) | pan |
| Wheel | zoom | zoom about the cursor |
| Arrow keys | | nudge by one grid step |
| F | frame the selection | frame the selection |
| Delete | delete the selection | delete the selection |

Clicking world or `func_group` geometry selects that one brush or patch;
clicking any other entity's geometry selects the whole entity. Hidden and
locked layers cannot be picked.

### Panels and docking

**Browser**, **Inspector**, **Tools** and **Problems** are docked panels. Each
panel's tab has buttons to move it to the left or right column (`<`, `>`),
under the views (`v`, Problems only) or to hide it (`x`). Panels in the same
place become tabs; click a tab to bring it forward. Drag the gaps between
panels and views to resize them. The layout is saved in
`editorExperimental_layout`; **Panels** restores the default.

- **Browser** lists the map's **Entities**, **Maps**, entity **Classes**,
  **Materials**, **Models** and **Sounds**. Asset names are indexed in the
  background a little each frame, so the editor never stalls while they load.
  Type a filter and press Enter. Selecting a class fills **Tools > Class**, a
  material fills **Tools > Material**, and a model or sound fills the inspector
  **Value**.
- **Inspector** shows the selection's properties, each with its type from the
  class's entityDef (`editor_var`, `editor_bool`, `editor_snd`, ...) or a
  well-known key, and the class's usage text. Select a property to fill **Key**
  and **Value**; the line below shows its type and description. **Apply**
  refuses a value of the wrong type, such as `noshadows "yes"`. **Remove**
  deletes the property. **Rename** renames the entity and updates every
  `target`, `bind` and other reference to it in one undo step; the line below
  counts its references.
- **Tools** creates point entities (**Class**, **Name**, **Create** at the view
  centre), applies a **Material** to selected brushes and patches, assigns a
  **Layer**, hides, shows, locks and unlocks layers, and saves or inserts
  **Prefabs**.
- **Problems** lists validation and compiler findings. Select one to select and
  frame the entity or primitive it refers to. It also shows build progress.

### Selections and editing

Every edit is undoable; a whole drag, transform or tool action is one undo
step, and a cancelled gesture leaves the map untouched. Brushes and patches
keep their exact source text until you change them, and then only the numbers
that changed are rewritten.

- **Texture lock** keeps textures attached to brush faces when they move,
  turn or scale. Without it, textures stay fixed in the world. Patches always
  carry their own texture coordinates.
- Moving an entity that has an `origin` moves its geometry with it. Turning a
  point entity updates its `angle` (or `rotation` for other axes) and turns a
  light's projection vectors; scaling a light scales its radius.
- **Group** moves selected world brushes and patches into a `func_group`,
  which Quake 4 merges back into the world when it compiles the map.
  **Ungroup** returns them to the world.
- **Layers** are an `editor_layer` key on entities and groups. Assigning a
  layer to world brushes groups them first. Hidden layers disappear from the
  views and from picking; locked layers stay visible but cannot be selected or
  moved. Layer visibility is part of the map's view state.
- **Prefabs** are ordinary maps under `fs_savepath/baseoq4/prefabs/`. Saving a
  prefab stores the selection centred on its base; inserting one places it at
  the view centre, renames entities whose names are taken and keeps the links
  between the inserted entities.

### View state

Each map remembers its camera, view mode, grid, texture lock, layer
visibility and selection in
`fs_savepath/baseoq4/editor_experimental/views/<map>.map.view`. It is saved
when you save, leave the workspace or start a play test, and restored when the
map is opened again.

## Build and play

**Build** compiles the saved map: save first, because builds compile the
file, not unsaved edits. The editor starts the dedicated server in a separate
process with a private sandbox save path
(`editor_experimental/build`) that holds only the map, and runs `dmap` there,
including collision and AI navigation. The Problems panel shows progress and
every compiler warning or error, linked to the entity or brush it names. A
leak, a compiler error, a crash or **Stop** ends the build without touching the
`.proc`, `.cm` and `.aas*` files beside your map. Only a complete, successful
build replaces them, all together, and removes outputs the new build no
longer produces. The editor keeps working while a build runs.

**Play** starts the client in its own window with the build sandbox as its
whole profile, so the test's configuration, saves and logs never touch yours,
and loads the last successful build (`devmap`, or `spawnServer` for maps under
`mp/`). The editor stays open, with its document, selection and cameras
unchanged; close the test window to continue editing. Extra arguments for the
test client can be set in `editorExperimental_playArgs`.

## Validation

**Validate** (or `mapEdit validate`) reports, each linked to its source:

- duplicate entity names, missing `target`/`bind` entities and unknown classes;
- property values of the wrong type, malformed origins, and models, materials
  and sounds that do not exist (once the asset index is complete);
- unreadable or degenerate brushes and patches, and legacy primitives that are
  kept but cannot be edited;
- a single-player map with no `info_player_*` start.

Compiler findings from the last build are added to the list.

## Files, saving and recovery

The workspace edits a separate map document; changes do not affect the
running game. Reads use the normal asset search, including installed PK4s.
Saves go to `fs_savepath/baseoq4/maps/` and never change installed PK4s. A save
to the working path refuses to overwrite a file another program changed, and a
changed existing map keeps its previous bytes as a sibling `.map.bak`. The
editor checks the open map every few seconds
(`editorExperimental_externalCheckSeconds`) and tells you when another program
changed it; save a copy or `mapEdit reload`.

New, Open and Close ask before discarding unsaved work, and a failed open keeps
the current document. Undo/redo keeps up to 256 steps within 16 MiB.

Recovery saves run every 120 seconds while the map is unsaved
(`editorExperimental_autoSaveSeconds`, 0 disables the timer) and when the
client quits. They live under `editor_experimental/recovery/`. Run
`mapEdit recover <map>` to open the latest one as an unnamed document, then
save it under a new name.

## Console commands

`editorExperimental help` lists every command; `mapEdit` is the same command.
Entity handles are session-local; `world` names worldspawn and `selected` the
first selected entity. `<handle>:<n>` names an entity's nth primitive.

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

`mapEdit outputs` lists the compiled files beside the saved map,
`mapEdit index` and `mapEdit find material floor` use the asset index, and
`mapEdit latency` reports the editor's measured response times.
