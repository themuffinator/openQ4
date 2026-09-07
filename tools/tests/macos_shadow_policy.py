#!/usr/bin/env python3
"""Regression checks for the Apple-path shadow-mapping policy.

The macOS Apple legacy GL2.1 corridor cannot run the shadow-map pipeline:
the caster and receiver programs require GLSL and framebuffer objects that
the corridor does not provide. The supported behavior is capability-gated,
per-light stencil fallback - never silent shadow loss and never a fatal
error. These pins keep that contract from regressing silently.
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_capability_gating() -> None:
    backend = read("src/renderer/draw_arb2.cpp")

    # the shadow-map pipeline must stay gated on GLSL program support so the
    # Apple GL2.1 corridor can never select it
    require(backend, "glConfig.GLSLProgramAvailable", "shadow-map capability gate")

    # per-light fail-closed contract: capability or resource failure routes
    # the light to the retail stencil path instead of dropping its shadows
    for token in (
        "SHADOWMAP_SUPPORT_OK",
        "RB_ShadowMapStencilFallback",
        "RB_ShadowMapMarkStencilFallbackSticky",
        "SetAllowIncomplete",
        "IsKnownIncomplete",
    ):
        require(backend, token, "shadow-map per-light stencil fallback contract")


def validate_docs() -> None:
    shadow_doc = read("docs/user/shadow-mapping.md")

    for token in (
        "Platform Support Matrix",
        "Apple legacy GL2.1 tier",
        "Stencil only",
        "Lights fall back to the retail stencil path automatically",
        "rejects incomplete maps and uses complete stencil coverage when available",
    ):
        require(shadow_doc, token, "shadow-mapping user doc platform matrix")


def main() -> None:
    validate_capability_gating()
    validate_docs()
    print("macOS shadow policy checks passed")


if __name__ == "__main__":
    main()
