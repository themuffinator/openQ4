# SDL3 Migration Plan (Linux/macOS)

This document defines the implementation plan for migrating non-Windows platform backends toward SDL3 while preserving current runtime stability.

## Goals

- Reduce backend divergence between Windows and non-Windows hosts.
- Keep Linux/macOS builds and release packaging stable during migration.
- Avoid regressions in stock-asset compatibility while platform code is modernized.

## Current State

- Windows can build/run with `platform_backend=sdl3`.
- Linux/macOS runtime paths are currently native (`src/sys/linux`, `src/sys/osx`) and rely on X11/GLX (Linux) and Cocoa/OpenGL glue (macOS).
- Non-Windows `platform_backend=sdl3` is currently a staging alias that maps to native platform sources.

## Migration Phases

1. Backend Vocabulary + CI Alignment (completed)
- Use one backend selector (`platform_backend`) across all hosts.
- Allow non-Windows `platform_backend=sdl3` as a staging mode in CI and local builds.
- Keep effective non-Windows source selection native until SDL3 paths are implemented.

2. Shared SDL3 Shell Introduction
- Add shared non-Windows SDL3 window/input lifecycle skeleton under `src/sys/`.
- Keep renderer context setup delegated to existing native code initially.
- Gate behind opt-in build/runtime cvars to allow side-by-side validation.

3. Linux SDL3 Runtime Bring-Up
- Replace Linux native window/event pump path with SDL3 equivalents.
- Validate fullscreen/windowed transitions, input capture, and multi-monitor behavior.
- Validate XWayland and native Wayland behavior explicitly in logs and docs.

4. macOS SDL3 Runtime Bring-Up
- Replace macOS native window/event pump path with SDL3 equivalents.
- Validate Cocoa integration assumptions, cursor modes, and focus transitions.
- Ensure app-bundle execution path behaves correctly.

5. Promotion To First-Class
- Promote Linux/macOS SDL3 path to default once compile/link/runtime checks pass consistently.
- Keep native backends available for rollback until at least one release cycle is stable.

## Validation Requirements Per Phase

- Configure/build/install succeeds in CI for Windows/Linux/macOS.
- SP and MP startup smoke tests run without platform-specific content hacks.
- Log diagnostics for display/input failures remain actionable and explicit.
