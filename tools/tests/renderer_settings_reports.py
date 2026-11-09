#!/usr/bin/env python3
"""Render API 20 settings reports.

The SYSTEM page's renderer and light-grid settings need evidence from the
renderer itself: how it resolved r_renderer each time it selected a back end,
and what each level load did with its light grids, using the preload policy
the settings service committed rather than a draft it could still undo. This
pins that plumbing across the module boundary: the ABI version and services
table, the module forwarders, the engine stores and their epochs, the
selection report beside the existing fallback warnings, one policy request per
level load with a receipt at every exit, no CVar read in the preload itself,
the settings provider's rule and dedicated stubs, and the unload hook.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="surrogateescape")


def body(text: str, signature: str) -> str:
    start = text.index(signature)
    depth = 0
    for index in range(text.index("{", start), len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated {signature}")


def require(text: str, token: str, context: str) -> None:
    if token not in text:
        raise AssertionError(f"{context}: missing {token!r}")


def ordered(text: str, tokens: list[str], context: str) -> None:
    at = -1
    for token in tokens:
        found = text.find(token, at + 1)
        if found < 0:
            raise AssertionError(f"{context}: {token!r} missing or out of order")
        at = found


def main() -> int:
    api = read("src/renderer/RenderModuleAPI.h")
    require(api, "#define RENDER_API_VERSION\t\t\t33", "render API version")
    require(api, '#include "RendererSettingsReports.h"', "the API carries the report types")
    services = body(api, "typedef struct renderModuleServices_s")
    ordered(services, ["( *ResetRenderApiAfterDeviceFailure )( void );",
                       "( *PublishRendererSelection )( const renderRendererSelection_t *selection );",
                       "( *GetLightGridLoadPolicy )( bool *preload, uint64_t *token );",
                       "( *PublishLightGridLoadReceipt )( const renderLightGridLoadReceipt_t *receipt );"],
            "version-20 services append after version 12")

    reports = read("src/renderer/RendererSettingsReports.h")
    for token in ("RENDER_SELECTION_REQUESTED\t\t= 0", "RENDER_SELECTION_UNAVAILABLE\t= 1", "RENDER_SELECTION_LEGACY\t\t\t= 2",
                  "char\t\t\trequested[ 32 ];", "bool\t\t\tpromotionActive;", "bool\t\t\tuploadObserved;",
                  "RENDER_LIGHTGRID_NO_ASSETS\t\t\t\t= 1", "RENDER_LIGHTGRID_PRELOAD_INCOMPLETE\t\t= 5", "uint64_t\t\ttoken;"):
        require(reports, token, "the report types")

    loader = read("src/renderer/RendererModule.cpp")
    table = body(loader, "static const renderModuleServices_t rm_services")
    ordered(table, ["RM_Services_ResetRenderApiAfterDeviceFailure,", "RM_Services_PublishRendererSelection,",
                    "RM_Services_GetLightGridLoadPolicy,", "RM_Services_PublishLightGridLoadReceipt,"], "the services table")
    publish = body(loader, "void R_RendererModule_PublishRendererSelection(")
    for token in ("rm_displayModuleEpoch == 0", "rm_rendererSelectionSerial == UINT64_MAX",
                  "rm_rendererSelection.requested[ sizeof( rm_rendererSelection.requested ) - 1 ] = '\\0';",
                  "rm_rendererSelectionSerial++;", "rm_rendererSelectionEpoch = rm_displayModuleEpoch;"):
        require(publish, token, "the engine stamps each selection report")
    receipt = body(loader, "void R_RendererModule_PublishLightGridLoadReceipt(")
    for token in ("rm_lightGridReceiptSerial++;", "rm_lightGridReceiptEpoch = rm_displayModuleEpoch;"):
        require(receipt, token, "the engine stamps each light-grid receipt")
    for query in ("bool R_RendererModule_QueryRendererSelection(", "bool R_RendererModule_QueryLightGridLoad("):
        require(body(loader, query), "!= rm_displayModuleEpoch", f"{query} refuses a report from another module")
    require(body(loader, "bool R_RendererModule_GetLightGridLoadPolicy("), "return UI_SettingsLevelLoadPolicy( *preload, *token );",
            "the policy comes from the settings service")

    module = read("src/renderer/RendererGLModule.cpp")
    for name, member in (("void R_RendererModule_PublishRendererSelection(", "PublishRendererSelection"),
                         ("bool R_RendererModule_GetLightGridLoadPolicy(", "GetLightGridLoadPolicy"),
                         ("void R_RendererModule_PublishLightGridLoadReceipt(", "PublishLightGridLoadReceipt")):
        require(body(module, name), f"rgm_services != NULL && rgm_services->{member} != NULL", f"{name} forwards only to a present service")
    require(body(module, "bool R_RendererModule_GetLightGridLoadPolicy("), "return false;", "a missing policy service leaves the CVar")

    render = read("src/renderer/RenderSystem.cpp")
    select = body(render, "void idRenderSystemLocal::SetBackEndRenderer()")
    ordered(select, ["r_actualRenderer.SetString( R_GetBackEndRendererName( backEndRenderer ) );",
                     "selection.selected = backEndRenderer;", "selection.automatic = R_PickBestBackEndRenderer();",
                     "selection.promotionActive = RendererBootstrap_ShouldAutoPromoteModernVisible();",
                     "R_RendererUpload_QueryStorage( uploadStorage )",
                     "selection.fallback = RENDER_SELECTION_LEGACY;", "selection.fallback = RENDER_SELECTION_UNAVAILABLE;",
                     "R_RendererModule_PublishRendererSelection( &selection );", "r_renderer.ClearModified();"],
            "the selection report follows the resolution and its warnings")
    if select.count("R_RendererModule_PublishRendererSelection(") != 1:
        raise AssertionError("one selection report per resolution")
    placeholder = select[:select.index("bool oldVPstate")]
    if "PublishRendererSelection" in placeholder:
        raise AssertionError("the pre-capability placeholder selects nothing to report")

    grid = read("src/renderer/RenderWorld_lightgrid.cpp")
    setup = body(grid, "void idRenderWorldLocal::SetupLightGrid()")
    if setup.count("R_RendererModule_GetLightGridLoadPolicy(") != 1:
        raise AssertionError("a level load asks for its policy exactly once")
    if setup.count("PublishLightGridLoadReceipt( receipt );") != 3:
        raise AssertionError("every SetupLightGrid exit publishes its receipt")
    for token in ("receipt.preload = r_lightGridPreload.GetBool();", "PreloadLightGridImages( receipt );",
                  "receipt.outcome = RENDER_LIGHTGRID_NO_ASSETS;"):
        require(setup, token, "SetupLightGrid")
    preload = body(grid, "void idRenderWorldLocal::PreloadLightGridImages(")
    if "r_lightGridPreload" in preload:
        raise AssertionError("the preload uses the load's policy, never the CVar a draft may have written")
    for token in ("receipt.outcome = RENDER_LIGHTGRID_RENDERER_STOPPED;", "if ( !receipt.preload ) {",
                  "receipt.outcome = RENDER_LIGHTGRID_STREAMED;", "receipt.areasResident = areaCount - failedCount;",
                  "RENDER_LIGHTGRID_PRELOAD_INCOMPLETE : RENDER_LIGHTGRID_PRELOADED;"):
        require(preload, token, "the preload fills its receipt")
    require(read("src/renderer/RenderWorld_local.h"), "void\t\t\t\t\tPreloadLightGridImages( renderLightGridLoadReceipt_t &receipt );",
            "the preload takes the load's receipt")

    service = read("src/ui/SettingsService.cpp")
    dedicated = service[:service.index("#else")]
    for token in ("bool UI_SettingsLevelLoadPolicy(bool&, std::uint64_t&) { return false; }", "void UI_SettingsLevelUnloaded() {}"):
        require(dedicated, token, "dedicated stubs")
    committed = body(service, "bool CommittedPreload(Service& service, bool& preload)")
    ordered(committed, ["service.display.Active() && !service.display.Persisted()", "service.transaction.Baseline().find(\"r_lightGridPreload\")",
                        "service.host.ReadValue(\"r_lightGridPreload\",value,error)"],
            "the committed preload is the baseline until an attempt's choice is saved")
    provider = body(service, "bool UI_SettingsLevelLoadPolicy(bool& preload, std::uint64_t& token)")
    ordered(provider, ["CommittedPreload(Settings(),committed)", "token = ++levelLoadToken;"],
            "each level load takes the committed preload and a fresh token")
    if "cvarSystem" in provider or "cvarSystem" in committed:
        raise AssertionError("the provider reads the catalog, not raw CVars")
    host = read("src/ui/application/SystemSettingsHost.cpp")
    require(body(host, "bool SystemSettingsHost::ReadValue("), "Parse(item, variable->GetString(), candidate, error)",
            "one catalog value parses as Read parses it")

    session = read("src/framework/Session.cpp")
    unload = body(session, "void idSessionLocal::UnloadMap()")
    ordered(unload, ["CloseSystemSettings();", "UI_SettingsLevelUnloaded();", "fileSystem->CancelLevelLoadCache();"],
            "the unload closes SYSTEM, then marks the level gone")
    print("renderer settings reports: API 20, services, forwarders, epochs, selection, light-grid policy and receipts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
