# Renderer Validation Matrix

This matrix is the validation source of truth for the staged GL renderer work. It separates safe automated startup/self-test coverage from gameplay smoke coverage that must be run manually with the mode-specific SP/MP launch tasks.

## Build And Stage

Use the project wrapper:

```powershell
tools\build\meson_setup.ps1 setup --wipe builddir . --backend ninja --buildtype=debug --wrap-mode=forcefallback
tools\build\meson_setup.ps1 compile -C builddir
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

For incremental validation after an existing setup:

```powershell
tools\build\meson_setup.ps1 compile -C builddir -- -j1
tools\build\meson_setup.ps1 install -C builddir --no-rebuild --skip-subprojects
```

## Automated Safe Matrix

The safe matrix starts the staged client, runs renderer self-tests or startup probes, prints `gfxInfo`, then quits. It does not launch maps.

```powershell
python tools\tests\renderer_validation_matrix.py
```

The runner writes a timestamped report under `.tmp/renderer-validation/` with per-case logs and a JSON copy for CI or release triage.

Automated coverage:

| Case | Coverage |
|---|---|
| `renderer-foundation-selftests` | context ladder, tier selector, upload manager, GPU timer, scene packet, render graph, shader library, draw plan, submit plan, and modern executor self-tests |
| `tier-auto` | default compatibility-preserving startup and `gfxInfo` |
| `tier-legacy` | forced legacy compatibility startup and `gfxInfo` |
| `tier-gl33` | forced GL 3.3 startup and `gfxInfo` |
| `tier-gl41` | forced GL 4.1 startup and `gfxInfo` |
| `tier-gl43` | forced GL 4.3 GPU-driven tier startup and `gfxInfo` |
| `tier-gl45` | forced GL 4.5 low-overhead tier startup and `gfxInfo` |
| `tier-gl46` | forced GL 4.6 top tier startup and `gfxInfo` |
| `tier-gl33-debug-context` | debug-context request with non-debug fallback available |
| `present-vsync0-fps0` | unlocked presentation startup probe |
| `present-vsync1-fps240` | high-refresh capped presentation startup probe |
| `present-vsync1-fps30` | low-fps capped presentation startup probe |

The forced tier cases pass when startup succeeds and the selected tier is reported. If a machine cannot support the forced tier, the log must show the selected fallback tier.

## Manual Gameplay Matrix

Gameplay validation remains mandatory before renderer release sign-off, but it is not run by the safe matrix because local map startup is currently freeze-prone. Use the SP launch task for single-player maps and the MP launch task or `tools\debug\start_listen_server_client.ps1` for multiplayer.

| Case | Mode | Map | Purpose |
|---|---|---|---|
| `sp-airdefense1` | SP | `game/airdefense1` | stock SP baseline, outdoor lighting, BSE smoke |
| `sp-airdefense2` | SP | `game/airdefense2` | flashlight, projected shadows, animated characters |
| `sp-storage2` | SP | `game/storage2` | indoor materials and post-process coverage |
| `sp-bse-heavy` | SP | `game/medlabs` | stress BSE effects without replacement content |
| `sp-cinematic-subview` | SP | `game/mcc_landing` | subviews, remote cameras, cinematic and GUI interaction |
| `mp-q4dm1-listen` | MP | `mp/q4dm1` | listen-server and local-client MP parity |

For each gameplay case, validate the matrix variants that the hardware supports:

| Dimension | Values |
|---|---|
| `r_glTier` | `auto`, `legacy`, `gl33`, `gl41`, `gl43`, `gl45`, `gl46` |
| `r_swapInterval` | `0`, `1` |
| `com_maxfps` | `30`, `240`, `0` |
| display mode | windowed, fullscreen |
| renderer diagnostics | `r_rendererMetrics 1`, `r_rendererMetrics 2` on at least one representative run |

After each gameplay smoke, inspect the configured log file under `fs_savepath\<gameDir>\logs\openq4.log` or the case-specific log emitted by the launch tool. Fix errors and warnings, then repeat the loop until the case is clean.

## Acceptance

- Automated safe matrix passes after build and install.
- Manual gameplay matrix reaches in-game/map gameplay for every required SP/MP case on supported hardware.
- Logs are inspected after every run.
- No stock-asset compatibility overrides are added as a validation shortcut.
- RenderDoc validation remains limited to forced modern/core bring-up paths until the visible renderer no longer depends on ARB2 compatibility features.
