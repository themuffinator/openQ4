# SYSTEM render options

The retained SYSTEM page's render options that apply through their own
executors rather than a Keep/Revert question: **Preload Light Grids**
(`r_lightGridPreload`), which the next map load uses, and **Renderer
Fallback** (`r_renderer`), which restarts the renderer.

## Preload Light Grids

The row sits under Irradiance Volumes, cloned from that toggle. Its card holds
the label (`#str_42820`), the stock help text (`#str_42821`: smoother area
changes, longer loads, more video memory, applies next map load) and a status
line. Help and status sit inside the card, so focus reveals the whole
explanation, and the plate and focus frame grow around it.

- **Draft.** The toggle proposes `r_lightGridPreload` through the existing
  `edit.r_lightGridPreload` action. Nothing is written until Apply, and the
  `draftLightGridPreload`/`baselineLightGridPreload` aliases tell the draft
  from the applied choice.
- **Apply.** The change completes through the
  [deferred executor](settings-effect-execution.md#deferred-light-grid-executor)
  without a Keep/Revert question. The page reports Applying, Restoring or
  Saving settings while it runs. A draft that also changes display or
  renderer settings applies through the
  [combined executor](settings-effect-execution.md#combined-executor); at
  this increment it was refused with a notice to apply them separately.
- **Status.** The line is hidden until a map is loaded. It then reads:

  | State | Text |
  | --- | --- |
  | A saved change waits for the next load | `#str_230078` This map keeps the previous light-grid setting until the next map loads. |
  | The loaded map preloaded | `#str_230079` This map preloads its light grids. |
  | The loaded map streams | `#str_230080` This map streams light grids as areas come into view. |
  | The map has no baked light grids | `#str_230081` This map has no baked light grids. |

## Renderer Fallback

The row sits last in the render column, after V-Sync, cloned from that choice:
the label `#str_41103` and two options from `#str_41104`, Auto (`best`) and
ARB2 (`arb2`), the values the settings catalog accepts from the page.

- **Draft.** The choice proposes `r_renderer` through the existing
  `edit.r_renderer` action, and the `draftRenderer`/`baselineRenderer` aliases
  tell the draft from the applied choice. A request the page does not offer,
  such as a retired back-end name from an old configuration, shows as typed
  and selects no option until the player picks one.
- **Apply.** The change completes through the
  [renderer executor](settings-effect-execution.md#renderer-fallback-executor)
  without a Keep/Revert question: the renderer restarts on the same display,
  and the attempt completes once the renderer's own report agrees. A draft
  that also changes display or preload settings applies through the
  [combined executor](settings-effect-execution.md#combined-executor), which
  asks for the same report; at this increment it was refused.
- **Availability.** Only a presenting OpenGL renderer that has reported its
  selection can prove the change. Elsewhere (on Vulkan, which has one back
  end, or while the renderer is not ready) the row is disabled and dims to
  0.45, like MSAA. Focus passes over it, as over every disabled control.
  While the page is busy an available row dims to 0.4 with the rest of its
  column. Like V-Sync and the other restart-backed rows, a window the
  recovery record cannot describe is still refused at Apply, before anything
  is written: a borderless window on Wayland has no position to restore.
- **Notice.** While the running renderer reports a fallback to its automatic
  pick, because the request is unavailable here or names a retired back end,
  a notice under the value reads `#str_230082`: "The selected renderer is
  unavailable here; openQ4 is using a supported one." It describes the
  applied renderer, so it hides while a different pick is drafted. It sits
  inside the card, so focus reveals it, and the popup opens below it. On
  Vulkan, where the row is disabled, a retired request still shows it.

## Service status

The settings service publishes five owner-only Booleans, declared by the page:
`settings.lightGrid.committed`, `.mapLoaded`, `.consumed`, `.effective` and
`.pending`. They come from the committed preload (an open attempt's baseline
until its choice is saved, as for level loads) and the renderer's latest
light-grid receipt from [render API 20](settings-effect-execution.md#renderer-settings-reports).

A receipt describes the map on screen only when it carries the latest policy
token and that token was issued after the last unload. A receipt from an
earlier load, from a load the service could not answer (token 0, as on a
dedicated server or when the setting cannot be read), or from an earlier
renderer module never counts, and a load that ran without a renderer shows no
status. Editor worlds start empty and publish no receipt. Render-demo playback
is an ordinary load: it takes a fresh token after the unload that starts it,
and its receipt describes the demo's map until the demo stops. `consumed` means the map has light grids
the load set up; `effective` means it preloaded them, including a preload that
left some areas out; `pending` means the committed choice differs from what
the loaded map used.

Two more owner-only Booleans describe the renderer row.
`settings.renderer.available` is the renderer executor's own admission test:
a ready, presenting OpenGL renderer with a selection report for its current
module. `settings.renderer.fallback` is set while the renderer's latest report
says it fell back from an unavailable or retired request to its automatic
pick. Without a report, neither is set.

`tools/ui/retained_system.py` owns both rows, their help, status
and notice lines, the seven state declarations, the aliases, the bindings and
the feedback timelines, together with the confirmation title's recovery
binding. Both rows follow the page's shared editing rule (open, editing, not
busy, no confirmation, discard dialog or unconfirmed numeric draft) and dim to
0.4 otherwise, like Irradiance Volumes and V-Sync. The renderer row also needs
the renderer to be available. With both rows in place
`RETAINED_SYSTEM_MISSING_SETTINGS` no longer lists `r_lightGridPreload` or
`r_renderer`.

## Qualification

- `tools/tests/ui_settings_service.py` drives the status through a session:
  no map, a load in progress, a streamed map, an attempt not yet saved, a saved
  preload waiting for the next map, an unload, the next load, a stale token, a
  map without baked grids, a load without a renderer, an incomplete preload and
  another owner.
- `tools/tests/native/UiSystemRenderOptionsTest.cpp` loads the production page
  with every SYSTEM test locale at normal and 40% wider glyphs. It checks the
  exact proposal with no write, the aliases, each status text and its
  visibility, the recovery title, and that the label, help, status lines and
  the deferred status messages fit at 1280x720 and 640x480, at 100% and 200%
  density, with the focused card revealed inside the body.
- SP map loads of `game/airdefense2` on OpenGL and Vulkan opened the page
  in game, toggled the row, applied, and reloaded the map with console verbs
  only. Both logged the edit and apply, the schema-2 journal going Pending then
  Confirmed, the deferred observation and the controller returning to idle,
  then `preload 1, token 2, preloaded, 29/29 areas resident` on the reload.
  The journal was removed and the configuration archived
  `seta r_lightGridPreload "1"`. Engine screenshots show the status reading
  streams, then waits for the next map, then preloads.

Evidence: `.tmp/ui/system-render-options/validation-evidence.json`, SHA-256
`092fe0eb98ffc56f6dc38fc2141b6bbe302f9620c37c1798ed9395e753f6b237`.

This evidence covers the Preload Light Grids row only.

### Renderer Fallback

- `tools/tests/ui_settings_service.py` drives the renderer status through a
  session: no report, a reporting renderer, each reported fallback kind, a
  fallback while the row cannot apply, the report going away and another
  owner.
- `tools/tests/native/UiSystemRenderOptionsTest.cpp` checks the exact `arb2`
  proposal with no write, the aliases, the closed choice naming each option,
  the row disabled and dimmed without a selection report, dimmed with its
  column while the page is busy, and the notice: its own text inside the card
  for a reported fallback, hidden while a different pick is drafted. A
  retired request (`nv20`) shows as typed, selects no option and gives way
  to Auto. At every size and density it reveals the whole card, the
  notice included, and fits the label, value and notice. The other SYSTEM
  choice tests (scrolling popups, popup placement and large text) now include
  the row, with the fixtures publishing the capability as they do MSAA's.
- `tools/tests/ui_settings_display_service.py` checks that the startup trace
  names the executor that recovered. It used to read the executor after
  finishing had cleared it, so a renderer recovery was traced as deferred.
- In SP map loads of `game/airdefense2` with console verbs only, OpenGL
  started on the retired `nv20` request. The notice showed under the
  name. Applying Auto restarted the renderer (`request=best`), the journal
  went Pending then Confirmed and the notice disappeared. Applying ARB2 then
  restarted it again (`request=arb2`), and the configuration archived
  `seta r_renderer "arb2"` with the journal removed. With the configuration
  read-only, an ARB2 apply could not save, so the Confirmed record stayed. The
  next launch replayed it and committed on the first full frame
  (`UI_SETTINGS_STARTUP approved=1 renderer=1`). On Vulkan the row was
  disabled at 0.45 and Accept left the draft unchanged.

Evidence: `.tmp/ui/system-renderer-fallback/validation-evidence.json`, SHA-256
`abf8cfcf300430ff6f2026b8eef18dff78a8869644bee56c1c6044ab8cf796a7`.

The remaining SYSTEM rows, the editor round trip and the full platform,
display and input matrix stay open.
