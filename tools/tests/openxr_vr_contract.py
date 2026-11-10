#!/usr/bin/env python3
"""Source contract for OpenXR virtual reality (docs/dev/archive/2026-10-18/plans/2026-10-02-openxr-vr.md).

Pins what keeps VR safe to ship without a headset in CI:
- the Khronos loader is pinned, built statically and only into the SDL3 client
  on Windows and Linux; dedicated servers and the test runtime never install it;
- the renderer and game ABIs grew append-only (render API 21, game API 50)
  and renderView_t's off-axis fields default to a symmetric view;
- on Vulkan (render API 32) the runtime creates the renderer's instance and
  device (XR_KHR_vulkan_enable2), the session binds that device, the renderer
  copies its own targets into the swapchain images and waits for its GPU work
  before the engine destroys a swapchain, and the test runtime speaks it too;
- an OpenGL renderer on EGL (native Wayland, render API 32) binds through
  XR_MNDX_egl_enable, which the test runtime speaks on Linux;
- engines without OpenXR, and dedicated servers, run the null VR system;
- controller input enters through the gamepad key path, never synthesised OS
  input, and every action name the runtime shows is localised;
- both game modules draw the aim marker into each eye between its 3D pass and
  its fade;
- a stereo frame shares view-independent work between the eyes and keeps
  eye-dependent work per eye (VR-04): the second eye emits the first eye's
  world-GUI quads, a remote camera renders once per frame, and mirror,
  reflection, refraction and x-ray render maps render once per eye;
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
    require(api, "#define RENDER_API_VERSION			33", "renderer module ABI v33")
    require(api, "void			( *RendererDeviceEvent )( int event );	// renderDeviceEvent_t",
            "the device lifetime service retains its signature")
    services = api[api.index("typedef struct renderModuleServices_s {"):api.index("} renderModuleServices_t;")]
    slots = re.findall(r"\(\s*\*(\w+)\s*\)", services)
    # the device lifetime service precedes the frame timing services, and the
    # Vulkan VR services close the table in the order they were appended
    if slots.index("RendererDeviceEvent") > slots.index("TakeFrameInputClocks"):
        raise AssertionError("frame timing services must append after the device lifetime service")
    if slots[-3:] != ["CreateVulkanInstanceForVR", "GetVulkanPhysicalDeviceForVR", "CreateVulkanDeviceForVR"]:
        raise AssertionError(f"the Vulkan VR services must close renderModuleServices_t in order, found {slots[-3:]}")

    render_system = read("src/renderer/RenderSystem.h")
    anchor = "virtual void ResetRetainedFontCache() = 0;"
    tail = render_system[render_system.index(anchor) + len(anchor):]
    slots = re.findall(r"virtual \w+\s+(\w+)\(", tail)
    # the VR slots follow the retained font slots in their original order;
    # later ABI versions only append after them
    if slots[:4] != ["GetVRGraphicsBinding", "SetVRFrame", "SetVRRenderTarget", "GetVRFrameResult"] \
            or slots[4:] != ["PresentScaledScene", "BeginGuiRoot", "BeginSavePreview", "PollSavePreview", "CancelSavePreview",
                             "ReloadGeneratedCacheImages"]:
        raise AssertionError(f"the VR slots must retain their order before scaled presentation, GUI roots, checkpoint previews and the pure image-cache reload, found {slots}")

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


def check_vulkan() -> None:
    openxr = read("src/sys/openxr/OpenXRSystem.cpp")
    require(openxr, '#define VR_VULKAN_ENABLE2_EXTENSION_NAME\t"XR_KHR_vulkan_enable2"',
            "the engine asks the runtime for XR_KHR_vulkan_enable2")
    require(openxr, "vulkanBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;", "a Vulkan session binds the renderer's device")
    require(openxr, "if ( binding.vkDevice == NULL || binding.vkDevice != vulkanDevice || vulkanCreator != instance ) {",
            "only a device the runtime created is bound")
    require(openxr, '\tcmdSystem->BufferCommandText( CMD_EXEC_INSERT, "vid_restart\\n" );',
            "a device made before VR asked restarts the renderer once")
    # OpenXR destroys no swapchain while submitted commands still use its images
    require(openxr, "\tReleaseRendererImages();\n\tDestroySwapchain( screenSwapchain );",
            "a resized virtual screen waits for the renderer before its old swapchain goes")
    require(openxr, "\tReleaseRendererImages();\n\tDestroyActions();", "a closing session waits for the renderer")
    require(read("src/renderer/RenderSystem_vr.cpp"), "\t\tR_VR_BackendReleaseImages();",
            "the renderer honours the release request")

    device = read("src/renderer/Vulkan/VulkanDevice.cpp")
    require(device, "R_RendererModule_CreateVulkanInstanceForVR( (void *)vkGetInstanceProcAddr, &ici, &created, &result )",
            "the Vulkan instance is offered to the VR runtime")
    require(device, "R_RendererModule_CreateVulkanDeviceForVR( (void *)vkGetInstanceProcAddr, (void *)vkCtx.physicalDevice,",
            "the Vulkan device is offered to the VR runtime")
    require(device, "R_RendererModule_GetVulkanPhysicalDeviceForVR( (void *)vkCtx.instance, &physical )",
            "the renderer runs on the GPU the runtime names")

    present = read("src/renderer/Vulkan/vk_VRPresentation.cpp")
    for token in ("binding.kind = RENDER_VR_BINDING_VULKAN;", "vkCmdCopyImage( cmd, target.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,",
                  "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,",
                  "output.premultipliedAlpha = index == 0;", "vkQueueWaitIdle( vkCtx.graphicsQueue );"):
        require(present, token, "the Vulkan backend presents to OpenXR")
    executor = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(executor, "bool VK_Exec_SetDefaultOutput( const vkExecDefaultOutput_t *output ) {",
            "an eye or the virtual screen stands in for the Vulkan window")
    require(executor, "if ( target.premultipliedAlpha && !( blendBits & GLS_ALPHA_COVERAGE ) ) {",
            "the virtual screen blends like GL's premultiplied default target")

    runtime = read("tools/tests/openxr/OpenXRTestRuntime.cpp")
    for token in ("XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,", "TR_EXT_ENTRY( xrCreateVulkanDeviceKHR, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ),",
                  "#define VK_NO_PROTOTYPES"):
        require(runtime, token, "the test runtime speaks XR_KHR_vulkan_enable2 through the application's loader")
    require(read("tools/tests/openxr_vr_smoke.py"), 'parser.add_argument("--render-api", choices=("gl", "vulkan"), default="gl")',
            "the VR smoke runs on either renderer")


def check_egl() -> None:
    render_system = read("src/renderer/RenderSystem.h")
    kinds = render_system[render_system.index("RENDER_VR_BINDING_NONE = 0,"):render_system.index("} renderVRBindingKind_t;")]
    if not kinds.rstrip().split("\n")[-1].lstrip().startswith("RENDER_VR_BINDING_EGL"):
        raise AssertionError("renderVRBindingKind_t grows at the end; EGL is its newest kind")
    require(render_system, "void *\t\t\t\t\teglGetProcAddress;", "the binding carries EGL's loader")
    gl = read("src/renderer/OpenGL/gl_VRPresentation.cpp")
    require(gl, "binding.kind = RENDER_VR_BINDING_EGL;", "an OpenGL context on EGL reports an EGL binding")
    require(gl, "return R_VR_QueryEGLBinding( binding );", "a context with no GLX binding falls back to EGL")
    openxr = read("src/sys/openxr/OpenXRSystem.cpp")
    require(openxr, '#define VR_EGL_ENABLE_EXTENSION_NAME\t\t"XR_MNDX_egl_enable"',
            "the engine asks the runtime for XR_MNDX_egl_enable")
    require(openxr, "eglBinding.type = XR_TYPE_GRAPHICS_BINDING_EGL_MNDX;", "an EGL context binds through XR_MNDX_egl_enable")
    require(openxr, "if ( binding.kind == RENDER_VR_BINDING_EGL && !haveEGL ) {",
            "a runtime without XR_MNDX_egl_enable gets a clear warning, not a failed session")
    runtime = read("tools/tests/openxr/OpenXRTestRuntime.cpp")
    require(runtime, "\tTR_MNDX_EGL_ENABLE_EXTENSION_NAME,", "the test runtime offers XR_MNDX_egl_enable on Linux")
    require(runtime, 'bindingName = "opengl_egl";', "the test runtime reports an EGL session")


def check_play_space() -> None:
    openxr = read("src/sys/openxr/OpenXRSystem.cpp")
    require(openxr, "const vrPlaySpaceChoice_t choice = VR_ChoosePlaySpace( vr_playSpace.GetInteger(), haveStage, haveFloor );",
            "the base space follows vr_playSpace and what the runtime offers")
    require(openxr, '#define VR_LOCAL_FLOOR_EXTENSION_NAME\t\t"XR_EXT_local_floor"', "standing play can use LOCAL_FLOOR")
    require(openxr, "recenterPose.position.y = VR_StandingReference( standingPlay, head.pose.position.y, vr_standingHeight.GetFloat(), learned );",
            "a recentre in standing play keeps the floor")
    require(openxr, "if ( vr_playSpace.IsModified() && !CreateBaseSpace() ) {", "a play-space change takes effect between frames")
    require(openxr, '"vr_calibrateHeight"', "the standing height can be measured again")
    require(read("src/framework/VRMathCore.h"), "inline vrPlaySpaceChoice_t VR_ChoosePlaySpace( int mode, bool haveStage, bool haveLocalFloor ) {",
            "the play-space choice is pure, natively tested math")
    require(read("tools/tests/openxr/OpenXRTestRuntime.cpp"), "OPENQ4_XR_TEST_SPACES", "the test runtime can withhold floor-level spaces")
    # the headset's own recentre: announced ahead of the frame it applies to
    require(openxr, "if ( spaceChangePending && time >= spaceChangeTime ) {",
            "the VR origin follows a runtime recentre from the frame the space moves, not from its announcement")
    require(openxr, "spaceChangeTime = spaceChangePending ? Max( spaceChangeTime, pending->changeTime ) : pending->changeTime;",
            "several announced moves recentre once, after the last")
    require(read("tools/tests/openxr/OpenXRTestRuntime.cpp"), 'command == "recenter"',
            "the test runtime can recentre its LOCAL spaces ahead of time")
    require(read("tools/tests/openxr_vr_playspace_smoke.py"), "the head while the runtime's recentre is still pending",
            "the play-space smoke holds the origin until the runtime's recentre applies")


def check_temporal() -> None:
    # VR-05: each eye keeps its own temporal history
    temporal = read("src/renderer/TemporalPresentation.cpp")
    require(temporal, "identity = TemporalHistoryCore_MixIdentity( identity, 0x56520000u | static_cast<unsigned int>( vrEye ) );",
            "an eye's view mixes its eye into its temporal identity")
    require(temporal, "R_TemporalPresentation_VREye() >= 0 && R_TemporalPresentation_TemporalAAWanted() );",
            "an eye requests temporal AA as asked; the virtual screen never does")
    require(read("src/renderer/RenderSystem_vr.cpp"),
            "cmd->temporalAA = R_TemporalPresentation_GetFrameState().temporalAARequested;",
            "a VR target command carries its target's temporal request to the backend")
    for backend in ("src/renderer/OpenGL/gl_VRPresentation.cpp", "src/renderer/Vulkan/vk_VRPresentation.cpp"):
        require(read(backend), "R_TemporalPresentation_SetVRTargetState( cmd->width, cmd->height, cmd->temporalAA );",
                f"{backend} restores the target's temporal state when the frame reaches it")
    require(read("src/renderer/draw_common.cpp"),
            "RB_SelectTemporalHistorySlot( command.viewDef != NULL ? command.viewDef->temporalHistorySlot : 0 );",
            "OpenGL resolves each eye from its own history images")
    vulkan = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    require(vulkan, "VK_TemporalPresentation_SelectDirectHistorySlot( viewDef->temporalHistorySlot );",
            "Vulkan's own temporal scene keeps a history per eye")
    require(vulkan, "const viewDef_t *rootView = VK_PostProcess_PortalSkyMainView( viewDef );",
            "a portal sky belongs to its own eye's main view")
    require(read("src/renderer/Vulkan/vk_PostProcess.cpp"),
            "VK_PostProcess_SelectTemporalMotionSlot( viewDef != NULL ? viewDef->temporalHistorySlot : 0 );",
            "Vulkan keeps motion-vector history per eye")
    game = read("src/game/PlayerView.cpp")
    require(game, "\t\tgameLocal.SelectTemporalHistorySlot( eye );\n\t\tSingleView( hud, &eyeView, RF_NO_GUI | RF_PRIMARY_VIEW );",
            "the single-player scene chain swaps in each eye's history")
    require(read("src/game/Game_render.cpp"), "gameRender.temporalHistorySlot * 2 + i,",
            "the game allocates history images per eye")
    require(read("tools/tests/openxr_vr_temporal_smoke.py"), "each eye should resolve from its own history",
            "the simulated-runtime smoke qualifies per-eye history")


def check_stereo_sharing() -> None:
    # VR-04: share what does not depend on the eye, keep what does
    gui = read("src/renderer/tr_guisurf.cpp")
    require(gui, "if ( R_VR_FrontEndEye() != 1 || !R_StereoGuiShareEligible() ) {",
            "only the second eye emits shared GUI quads")
    require(gui, "if ( R_VR_FrontEndEye() != 0 || !R_StereoGuiShareEligible() ) {",
            "only the first eye keeps GUI quads for sharing")
    require(gui, "tr.guiRecursionLevel == 1 && tr.viewDef != NULL && !tr.viewDef->isSubview",
            "subviews and nested GUIs never share")
    require(gui, "slot.frameCount == tr.frameCount && slot.time == tr.viewDef->renderView.time",
            "shared quads belong to this frame and GUI time only")
    require(gui, "if ( tr.viewCount == viewCountBeforeRedraw ) {",
            "a GUI that rendered a view of its own draws again for the second eye")
    require(read("src/renderer/RenderSystem_init.cpp"), "R_ShutdownStereoGuiShare();",
            "the shared GUI models are freed with the renderer")
    subview = read("src/renderer/tr_subview.cpp")
    if subview.count("stage->dynamicFrameCount == R_VR_EyeSubviewFrameKey()") != 4             or subview.count("stage->dynamicFrameCount = R_VR_EyeSubviewFrameKey();") != 4:
        raise AssertionError("mirror, reflection, refraction and x-ray render maps must render per eye")
    remote = subview.partition("static void R_RemoteRender(")[2].partition("void R_MirrorRender(")[0]
    require(remote, "if ( stage->dynamicFrameCount == tr.frameCount ) {",
            "a remote camera renders once per frame for both eyes")
    vr = read("src/renderer/RenderSystem_vr.cpp")
    require(vr, "return (int)( (unsigned int)tr.frameCount * 4u + eye );",
            "the eye subview key separates the frame's targets")
    require(read("src/renderer/TemporalPresentation.cpp"), "return R_VR_FrontEndEye();",
            "temporal presentation and stereo sharing agree on the eye")
    share = read("tools/tests/openxr_vr_stereo_share_smoke.py")
    require(share, 'assert 2 * stereo["shared"] == stereo["guis"]', "the share smoke requires every second eye's GUI shared")
    require(share, 'assert stereo["eye_subviews"] == 2 * flat["eye_subviews"]', "the share smoke requires a mirror per eye")


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
    require(system, 'idCVar vr_pauseOnFocusLoss( "vr_pauseOnFocusLoss", "1",', "single player pauses when the headset loses focus")
    require(read("src/framework/Session.cpp"), "if ( !VR_ConsumeFocusLoss() || !vr_pauseOnFocusLoss.GetBool() ) {",
            "the session consumes every focus loss and pauses only when asked to")
    require(read("tools/tests/openxr_vr_smoke.py"), "losing input focus in gameplay did not pause single player",
            "the single-player smoke requires the pause")
    # single player vehicles: the eye sits on the turret's axis and the turret
    # follows the head; multiplayer has no vehicles and keeps them flat
    player = read("src/game/Player.cpp")
    require(player, '( IsInVehicle() && !cvarSystem->GetCVarBool( "vr_vehicleStereo" ) )',
            "single player vehicles render in stereo unless vr_vehicleStereo is off")
    require(player, "seat->GetVRSeat( origin );", "a vehicle seat puts the eye on its turret's axis")
    # an eye's off-axis culling planes are unit planes, so the radius test keeps boxes reaching into the eye
    require(read("src/renderer/tr_main.cpp"), "\t\tfor ( i = 0; i < 4; i++ ) {\n\t\t\ttr.viewDef->frustum[i].Normal().Normalize();",
            "the off-axis frustum's side planes are normalized")
    # a vehicle's turns carry the seat from boarding: scripts turn vehicles as they seat the player
    require(player, "// the seat follows the vehicle's turns from here, not from the first",
            "the seat's turn carry starts when the player boards")
    # world GUIs, characters and usable things answer the weapon hand where it points
    require(player, "start = GetEyePosition();\n\t// openQ4 VR: the weapon hand points at world GUIs",
            "the focus trace starts where the weapon hand aims")
    require(player, "\tGetVRAimOrigin( start );\n\tend = start + viewAngles.ToForward() * 768.0f;",
            "the focus trace runs from the weapon hand along its aim")
    require(read("src/game/vehicle/VehicleParts.cpp"), "vrDriver->GetVRVehicleAim( vrSeatAim, vrWorldAim )",
            "a turret driven in VR follows the head without dropping rotation")
    require(read("src/mpgame/Player.cpp"), "|| pm_thirdPerson.GetBool() || IsInVehicle() || vrPresentationCamera ) {",
            "multiplayer, which has no vehicles, keeps them and the Arena's authored shots on the floating screen")
    # the aim's pitch is the tracked hand's own on both ends of the wire
    require(read("src/framework/UsercmdGen.cpp"), "cmd.buttons |= BUTTON_VRAIM;", "a tracked aim marks its usercmds")
    for module in ("src/game/Player.cpp", "src/mpgame/Player.cpp"):
        player = read(module)
        require(player, "if ( ( usercmd.buttons & BUTTON_VRAIM ) != 0 ) {\n\t\tdelta.pitch = 0.0f;",
                f"{module} keeps no pitch offset on a tracked aim")
        require(player, "if ( !vrAim && !centerView.IsDone( gameLocal.time ) ) {",
                f"{module} does not level a tracked aim when the view is centred")
    require(read("src/game/gamesys/WeaponWheel.inl"),
            '( trackedAim || !cvarSystem->GetCVarBool( "ui_retained" ) ) ) {',
            "turning the weapon hand steers the wheel under either interface")
    require(read("src/framework/UsercmdGen.cpp"), "\tClearAngles();\n\t// openQ4 VR: the tracked aim starts again from the zeroed angles\n\tVR_UsercmdAnglesCleared();",
            "a new map takes the body's yaw again, so its start is not turned by the level before")
    # a seat's turret follows the head, so the HUD hangs on the line of sight
    require(read("src/sys/openxr/OpenXRSystem.cpp"),
            "( vr_aimMode.GetInteger() == VR_AIM_HAND && !vr_hudOnSight.GetBool() ) ? vr_hudHeightOffset.GetFloat() : 0.0f;",
            "the HUD hangs on the line of sight while the head aims a vehicle")
    require(read("src/game/PlayerView.cpp"), 'cvarSystem->SetCVarBool( "vr_hudOnSight", player->IsInVehicle() );',
            "a vehicle seat in stereo asks for the HUD on the line of sight")
    for module in ("src/game/PlayerView.cpp", "src/mpgame/PlayerView.cpp"):
        # (single player's scene chain keeps each eye's temporal history apart, VR-05)
        slot_restore = "\t\tgameLocal.SelectTemporalHistorySlot( 0 );\n" if module.startswith("src/game/") else ""
        require(read(module), "\t\tSingleView( hud, &eyeView, RF_NO_GUI | RF_PRIMARY_VIEW );\n" + slot_restore +
                              "\t\tVR_DrawComfortVignette( eyeView, vignette, vignetteMaterial );\n\t\tif ( drawAimMarker ) {\n"
                              "\t\t\tVR_DrawAimMarker( eyeView, aimMarker, aimLaser, aimMaterial );\n\t\t}\n\t\tScreenFade();",
                f"{module} vignettes and marks the aim in each eye over its 3D pass, under its fade")
        # the vignette follows artificial motion only, read from the engine's cvar
        require(read(module), 'cvarSystem->GetCVarFloat( "vr_comfortVignette" )',
                f"{module} reads the vignette setting without touching the frame state")
        # a zoom magnifies each eye by the weapon's own ratio; the dot alone marks it
        require(read(module), "const float zoomScale = player->VRZoomScale();",
                f"{module} magnifies the eyes for a weapon's zoom")

    openxr = read("src/sys/openxr/OpenXRSystem.cpp")
    for banned in ("SendInput", "keybd_event", "mouse_event", "XTest"):
        if banned in openxr:
            raise AssertionError(f"OpenXR input must not synthesise OS input ({banned})")
    require(openxr, "Sys_PostVRControllerKey( key, down );", "controller buttons take the gamepad key path")
    require(openxr, "createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;", "the instance asks for OpenXR 1.0")
    require(openxr, "XR_KHR_OPENGL_ENABLE_EXTENSION_NAME", "the session binds OpenGL or Vulkan")
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
    check_vulkan()
    check_egl()
    check_play_space()
    check_temporal()
    check_stereo_sharing()
    check_engine()
    check_strings()
    check_documents()
    print("openxr_vr_contract: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
