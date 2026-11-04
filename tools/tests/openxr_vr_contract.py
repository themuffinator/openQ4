#!/usr/bin/env python3
"""Source contract for OpenXR virtual reality (docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md).

Pins what keeps VR safe to ship without a headset in CI:
- the Khronos loader is pinned, built statically and only into the SDL3 client
  on Windows and Linux; dedicated servers and the test runtime never install it;
- the renderer and game ABIs grew append-only (render API 21, game API 50)
  and renderView_t's off-axis fields default to a symmetric view;
- engines without OpenXR, and dedicated servers, run the null VR system;
- controller input enters through the gamepad key path, never synthesised OS
  input, and every action name the runtime shows is localised;
- both game modules draw the aim marker into each eye between its 3D pass and
  its fade;
- the user guide, developer plan, licence notice and runtime smoke exist.
"""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
LANGUAGES = (
    "english", "spanish", "french", "italian", "polish", "russian", "german",
    "brazilian", "czech", "hungarian", "turkish", "ukrainian",
)
ACTION_STRINGS = [f"#str_{n}" for n in range(230100, 230111)]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise AssertionError(f"{label}: missing {token!r}")


def check_build() -> None:
    wrap = read("subprojects/openxr.wrap")
    require(wrap, "directory = OpenXR-SDK-release-1.1.65", "openxr.wrap pins the SDK release")
    if not re.search(r"source_hash = [0-9a-f]{64}", wrap):
        raise AssertionError("openxr.wrap must pin the tarball hash")
    loader = read("subprojects/packagefiles/openxr/meson.build")
    require(loader, "static_library('openxr_loader'", "the loader builds statically")
    require(loader, "dependency('jsoncpp')", "the loader shares the project's jsoncpp")
    require(loader, "install: false", "the loader is never installed beside the executable")

    options = read("meson_options.txt")
    require(options, "'openxr',\n  type: 'feature',", "meson_options.txt declares the openxr feature")

    meson = read("meson.build")
    require(meson, "build_openxr = build_engine and use_sdl3_backend and not openxr_option.disabled() \\\n"
                   "  and (host_system == 'windows' or host_system == 'linux')",
            "OpenXR is limited to the SDL3 client on Windows and Linux")
    snapshot = meson.index("openq4_dedicated_sources = openq4_engine_sources")
    client_sources = meson.index("openq4_engine_sources += files('src/sys/openxr/OpenXRSystem.cpp')")
    if client_sources < snapshot:
        raise AssertionError("OpenXR sources must join the client after the dedicated-server snapshot")
    require(meson, "client_engine_cpp_args += ['-DOPENQ4_OPENXR=1']", "only the client defines OPENQ4_OPENXR")
    require(meson, "client_deps += openxr_loader_dep", "only the client links the loader")
    runtime = meson[meson.index("shared_module(\n    'openq4-xr-test-runtime'"):]
    require(runtime[:600], "install: false", "the OpenXR test runtime is a test fixture")
    require(meson, "test('openq4-vr-math-core', openq4_vr_math_core_test", "the VR math core has a native test")
    if "src/sys/openxr" in read("tools/build/meson_sources.py"):
        raise AssertionError("src/sys/openxr must stay outside the engine source globs")


def check_abi() -> None:
    api = read("src/renderer/RenderModuleAPI.h")
    require(api, "#define RENDER_API_VERSION\t\t\t31", "renderer module ABI v31")
    require(api, "void\t\t\t( *RendererDeviceEvent )( int event );\t// renderDeviceEvent_t",
            "the device lifetime service retains its signature")
    if api.index("( *RendererDeviceEvent )") > api.index("( *TakeFrameInputClocks )"):
        raise AssertionError("frame timing services must append after the device lifetime service")

    render_system = read("src/renderer/RenderSystem.h")
    anchor = "virtual void ResetRetainedFontCache() = 0;"
    tail = render_system[render_system.index(anchor) + len(anchor):]
    slots = re.findall(r"virtual \w+\s+(\w+)\(", tail)
    # the VR slots follow the retained font slots in their original order;
    # later ABI versions only append after them
    if slots[:4] != ["GetVRGraphicsBinding", "SetVRFrame", "SetVRRenderTarget", "GetVRFrameResult"] \
            or slots[4:] != ["PresentScaledScene", "BeginGuiRoot", "BeginSavePreview", "PollSavePreview", "CancelSavePreview"]:
        raise AssertionError(f"the VR slots must retain their order before scaled presentation, GUI roots and checkpoint previews, found {slots}")

    # game modules call through idVRSystem too: its slots only ever grow at the end
    vr_system = read("src/framework/VRSystem.h")
    interface = vr_system[vr_system.index("class idVRSystem {"):vr_system.index("extern idVRSystem *")]
    vr_slots = re.findall(r"virtual \w+\s+(\w+)\(", interface)
    if vr_slots != ["Init", "Shutdown", "RendererStarted", "RendererStopping", "BeginFrame", "EndFrame",
                    "IsPacing", "IsActive", "GetFrameState", "GetUsercmdInput", "DrawMenuPointer", "Vibrate", "ShiftTrackingOrigin"]:
        raise AssertionError(f"idVRSystem's slots changed order: {vr_slots}")
    # game modules read the frame state by layout: new members go at the end
    frame = vr_system[vr_system.index("typedef struct vrFrameState_s {"):vr_system.index("} vrFrameState_t;")]
    if not frame.rstrip().endswith("bool\t\t\t\t\troomScale;\t\t// the body walks after the head (vr_roomScale)"):
        raise AssertionError("vrFrameState_t grows at the end; roomScale is its newest member")

    render_world = read("src/renderer/RenderWorld.h")
    for field in ("bool					asymmetricFov = false;", "float					fovTanLeft = 0.0f;",
                  "float					fovTanRight = 0.0f;", "float					fovTanUp = 0.0f;",
                  "float					fovTanDown = 0.0f;"):
        require(render_world, field, "renderView_t keeps existing views symmetric")

    game = read("src/game/Game.h")
    require(game, "const int GAME_API_VERSION\t\t= 58;", "game module ABI v58")
    import_block = game[game.index("struct gameImport_t {"):game.index("struct gameExport_t {")]
    # Append-only ABI: vrSystem (v50) keeps its slot; later members follow it.
    if not import_block.rstrip().rstrip("};").rstrip().endswith(
            "idVRSystem *				vrSystem;				// tracked head/controller poses (never NULL)\n"
            "	idUsercmdGen *				usercmdGen;				// late-look input sampling (draw pass only)"):
        raise AssertionError("gameImport_t must keep vrSystem in place, followed only by usercmdGen (v54)")
    for module in ("src/game/Game_local.cpp", "src/mpgame/Game_local.cpp"):
        require(read(module), "vrSystem\t\t\t\t\t= import->vrSystem;", f"{module} binds the VR system")
    require(read("src/framework/Common.cpp"), "gameImport.vrSystem\t\t\t\t\t= ::vrSystem;", "the engine exports its VR system")


def check_engine() -> None:
    system = read("src/framework/VRSystem.cpp")
    require(system, "#if defined( OPENQ4_OPENXR )\nidVRSystem *vrSystem = VR_GetOpenXRSystem();",
            "OpenXR builds run the OpenXR system")
    require(system, "static idVRSystemNull vrSystemNull;\nidVRSystem *vrSystem = &vrSystemNull;",
            "other builds run the null system")
    require(system, 'idCVar vr_enable( "vr_enable", "0",', "VR is off by default")
    require(system, 'idCVar vr_aimLaser( "vr_aimLaser", "1",', "the aim dot is on by default")
    require(system, 'idCVar vr_comfortVignette( "vr_comfortVignette", "0.5",', "the comfort vignette is on by default")
    require(system, 'idCVar vr_vehicleStereo( "vr_vehicleStereo", "1",', "vehicles render in stereo by default")
    # single player vehicles: the eye sits on the turret's axis and the turret
    # follows the head; multiplayer has no vehicles and keeps them flat
    player = read("src/game/Player.cpp")
    require(player, '( IsInVehicle() && !cvarSystem->GetCVarBool( "vr_vehicleStereo" ) )',
            "single player vehicles render in stereo unless vr_vehicleStereo is off")
    require(player, "seat->GetVRSeat( origin );", "a vehicle seat puts the eye on its turret's axis")
    require(read("src/game/vehicle/VehicleParts.cpp"), "vrDriver->GetVRVehicleAim( vrSeatAim, vrWorldAim )",
            "a turret driven in VR follows the head without dropping rotation")
    require(read("src/mpgame/Player.cpp"), "|| pm_thirdPerson.GetBool() || IsInVehicle() ) {",
            "multiplayer, which has no vehicles, keeps them on the floating screen")
    # a seat's turret follows the head, so the HUD hangs on the line of sight
    require(read("src/sys/openxr/OpenXRSystem.cpp"),
            "( vr_aimMode.GetInteger() == VR_AIM_HAND && !vr_hudOnSight.GetBool() ) ? vr_hudHeightOffset.GetFloat() : 0.0f;",
            "the HUD hangs on the line of sight while the head aims a vehicle")
    require(read("src/game/PlayerView.cpp"), 'cvarSystem->SetCVarBool( "vr_hudOnSight", player->IsInVehicle() );',
            "a vehicle seat in stereo asks for the HUD on the line of sight")
    for module in ("src/game/PlayerView.cpp", "src/mpgame/PlayerView.cpp"):
        require(read(module), "\t\tSingleView( hud, &eyeView, RF_NO_GUI | RF_PRIMARY_VIEW );\n"
                              "\t\tVR_DrawComfortVignette( eyeView, vignette, vignetteMaterial );\n\t\tif ( drawAimMarker ) {\n"
                              "\t\t\tVR_DrawAimMarker( eyeView, aimMarker, aimLaser, aimMaterial );\n\t\t}\n\t\tScreenFade();",
                f"{module} vignettes and marks the aim in each eye over its 3D pass, under its fade")
        # the vignette follows artificial motion only, read from the engine's cvar
        require(read(module), 'cvarSystem->GetCVarFloat( "vr_comfortVignette" )',
                f"{module} reads the vignette setting without touching the frame state")
        # a zoom magnifies each eye by the weapon's own ratio; the dot alone marks it
        require(read(module), "VR_ZoomTangentScale( player->CalcFov( true ), player->CalcFov( false ) )",
                f"{module} magnifies the eyes for a weapon's zoom")

    openxr = read("src/sys/openxr/OpenXRSystem.cpp")
    for banned in ("SendInput", "keybd_event", "mouse_event", "XTest"):
        if banned in openxr:
            raise AssertionError(f"OpenXR input must not synthesise OS input ({banned})")
    require(openxr, "Sys_PostVRControllerKey( key, down );", "controller buttons take the gamepad key path")
    require(openxr, "createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;", "the instance asks for OpenXR 1.0")
    require(openxr, "XR_KHR_OPENGL_ENABLE_EXTENSION_NAME", "the session binds OpenGL")
    for token in vrActionTokens():
        require(openxr, token, "OpenXR action names are localised")

    session = read("src/framework/Session.cpp")
    begin = session.index("\tvrSystem->BeginFrame();\n\trenderSystem->BeginFrame(")
    end = session.index("\tvrSystem->EndFrame();\n\n\tinsideUpdateScreen = false;")
    if not begin < end:
        raise AssertionError("an XR frame must bracket one UpdateScreen")
    require(read("src/renderer/RenderSystem_init.cpp"), "R_RendererModule_RendererDeviceEvent( RENDER_DEVICE_STOPPING );",
            "the session closes before the renderer destroys its context")
    require(session, "\tDraw();\n\tvrSystem->DrawMenuPointer();\n",
            "the menu pointer draws over every other 2D element of the frame")
    backend = read("src/sys/sdl3/sdl3_backend.cpp")
    require(backend, "#ifndef ID_DEDICATED\nvoid Sys_PostVRControllerKey(int key, bool down) {",
            "only the client posts VR controller keys")
    require(backend, "SDL3_QueueMouseButtonEvent(key, down, eventTime, false);",
            "pointer clicks reach menus like mouse buttons and never the usercmd poll")
    require(backend, "bool Sys_PostVRPointer(bool onScreen, float u, float v) {",
            "the menu pointer takes the mouse's route into menus")


def vrActionTokens() -> list[str]:
    return [f'"{key}"' for key in ACTION_STRINGS]


def check_strings() -> None:
    for language in LANGUAGES:
        table = read(f"content/baseoq4/pak0/strings/{language}_openq4.lang")
        for key in ACTION_STRINGS:
            if not re.search(rf'"{key}"\t"[^"]+"', table):
                raise AssertionError(f"{language}_openq4.lang lacks {key}")


def check_documents() -> None:
    for relative in ("docs/user/vr.md", "docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md", "LICENSES/openxr-sdk.txt",
                     "tools/tests/openxr_vr_smoke.py", "tools/tests/openxr_vr_menu_smoke.py",
                     "tools/tests/openxr_vr_mp_smoke.py",
                     "tools/tests/openxr/OpenXRTestRuntime.cpp",
                     "tools/tests/native/VRMathCoreTest.cpp"):
        if not (ROOT / relative).is_file():
            raise AssertionError(f"missing {relative}")
    guide = read("docs/user/vr.md")
    for cvar in ("vr_enable", "vr_aimMode", "vr_aimLaser", "vr_hapticStrength", "vr_turnMode", "vr_recenter",
                 "vr_restart", "vr_twoHanded", "vr_comfortVignette", "vr_physicalCrouch", "vr_vehicleStereo"):
        require(guide, cvar, "the VR guide documents the settings players use")


def main() -> int:
    check_build()
    check_abi()
    check_engine()
    check_strings()
    check_documents()
    print("openxr_vr_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
