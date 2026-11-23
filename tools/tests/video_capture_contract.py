#!/usr/bin/env python3
"""Structural contracts for video capture (recordVideo, aviGame, aviDemo).

The renderer reads presented frames back through an asynchronous ring queued
at the swap, beside the save preview, on OpenGL and Vulkan; the session asks
for a frame before each in-sequence screen update and collects finished reads
after it; a fixed-rate recording sets the game's tics a frame and pins the
presentation fraction; sound comes from the OpenAL loopback mixer the demo
export uses. These checks pin that wiring and the documented defaults.
"""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="replace")


def require(text: str, needle: str, context: str) -> None:
    if needle not in text:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_order(text: str, needles: tuple[str, ...], context: str) -> None:
    position = -1
    for needle in needles:
        found = text.find(needle, position + 1)
        if found < 0:
            raise AssertionError(f"Missing {needle!r} (in order) in {context}")
        position = found


def between(text: str, start: str, end: str, context: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise AssertionError(f"Missing {start!r} in {context}")
    finish = text.find(end, begin + len(start))
    if finish < 0:
        raise AssertionError(f"Missing {end!r} after {start!r} in {context}")
    return text[begin:finish]


def validate_renderer() -> None:
    api = read("src/renderer/RenderSystem.h")
    require(api, "virtual bool RequestVideoFrame( long long tag ) { (void)tag; return false; }", "the appended readback slots")
    require_order(api, ("virtual bool UploadImageRegion(", "virtual bool RequestVideoFrame(", "virtual int PollVideoFrame(",
                        "virtual void ReleaseVideoFrame()", "virtual void EndVideoCapture()"), "render API 37 slot order")
    require(read("src/renderer/RenderModuleAPI.h"), "#define RENDER_API_VERSION\t\t\t38", "render API version")

    backend = read("src/renderer/tr_backend.cpp")
    require_order(backend, ("RB_QueueSavePreview();", "RB_QueueVideoFrame();", "R_SwapBuffersWithInputTiming();"),
                  "OpenGL reads the frame before the swap")
    vulkan = read("src/renderer/Vulkan/vk_GuiExecutor.cpp")
    submit = between(vulkan, "static bool VK_GuiExecutor_SubmitFrame( bool present ) {", "VK_Device_FlushUploadBatch();",
                     "Vulkan submit")
    require_order(submit, ("RB_QueueSavePreview();", "RB_QueueVideoFrame();"), "Vulkan records the copy before submitting")
    require_order(vulkan, ("VK_ConsumeSavePreview( slot );", "VK_ConsumeVideoFrames( slot );"),
                  "a copy becomes readable when its frame slot retires")
    require(vulkan, "RB_ShutdownVideoCapture();", "Vulkan frees the ring")
    require(read("src/renderer/RenderSystem_init.cpp"), "\tCancelSavePreview();\n\tEndVideoCapture();",
            "renderer shutdown frees the ring")
    gl = read("src/renderer/SavePreviewCapture.cpp")
    require(gl, "static const int RB_VIDEO_RING = 4;", "the OpenGL ring")
    require(gl, "glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, NULL );", "the OpenGL read goes to a pack buffer")


def validate_session() -> None:
    session = read("src/framework/Session.cpp")
    update = between(session, "void idSessionLocal::UpdateScreen( bool outOfSequence ) {", "idSessionLocal::Frame", "UpdateScreen")
    require_order(update, ("VideoCapture_BeforePresent();", "renderSystem->BeginFrame(", "renderSystem->EndFrame(",
                           "insideUpdateScreen = false;", "VideoCapture_AfterPresent();"),
                  "a frame is asked for before it is drawn and collected after")
    if update.count("if ( !outOfSequence ) {") < 2:
        raise AssertionError("loading-screen updates must not be recorded")
    frame = between(session, "void idSessionLocal::Frame() {", "idSessionLocal::RunGameTic", "Session Frame")
    require(frame, "const int captureTics = aviCaptureMode ? com_aviDemoTics.GetInteger() : VideoCapture_FixedTicsPerFrame();",
            "fixed-rate tics")
    require(frame, "lastGameTic = latchedTicNumber - captureTics;", "exactly the capture's tics run a frame")
    if "roqParam" in session or "TakeScreenshot( com_aviDemoWidth" in session:
        raise AssertionError("the old TGA/RoQ aviDemo path must stay removed")
    begin = between(session, "void idSessionLocal::BeginAVICapture(", "void idSessionLocal::EndAVICapture", "BeginAVICapture")
    require(begin, "VideoCapture_Start( aviDemoShortName.c_str(), com_aviDemoTics.GetInteger() )", "aviDemo records video")
    require(session, "VideoCapture_Init();", "commands registered")
    require(session, "VideoCapture_Shutdown();", "a recording finishes at shutdown")
    require(session, "!VideoCapture_Active() && !Sound_CaptureActive() && Session_ShouldSilenceAudioWhenUnfocused()",
            "a recording keeps its sound without focus")

    game = read("src/game/Game_local.cpp")
    fraction = between(game, "float idGameLocal::GetPresentationTicFraction( void ) const {", "idGameLocal::GetPresentationTimeMsec",
                       "presentation fraction")
    require(fraction, 'cvarSystem->GetCVarBool( "com_videoFixedStep" )', "fixed-rate frames draw their tic's pose")


def validate_capture() -> None:
    capture = read("src/framework/VideoCapture.cpp")
    defaults = {
        "com_videoFormat": '"mp4"', "com_videoCodec": '"auto"', "com_videoEncoder": '"auto"', "com_videoFPS": '"60"',
        "com_videoQuality": '"80"', "com_videoBitrate": '"0"', "com_videoHeight": '"0"', "com_videoAudio": '"1"',
        "com_videoAudioBitrate": '"192"', "com_videoTiming": '"auto"', "com_videoHardware": '"1"',
        "com_videoPreset": '"auto"', "com_videoFFmpeg": '""', "com_videoFolder": '"videos"',
    }
    guide = read("docs/user/video-capture.md")
    for name, value in defaults.items():
        require(capture, f'idCVar {name}( "{name}", {value},', f"{name} default")
        require(guide, f"`{name}`", f"{name} documented")
    for command in ("recordVideo", "stopVideo", "toggleVideo", "videoEncoders"):
        require(capture, f'cmdSystem->AddCommand( "{command}"', f"{command} command")
        require(guide, command, f"{command} documented")
    require(capture, "idAsyncNetwork::IsActive()", "online recordings follow the clock")
    require(capture, "Sound_SetVideoLoopback( true, VIDEO_AUDIO_RATE )", "sound from the loopback mixer")
    loopback = read("src/sound/SoundLoopback.cpp")
    require(loopback, "Sound_CaptureActive()", "never takes a demo export's samples")
    require(loopback, "hardware.RenderLoopback(", "mixes through the backend's loopback")

    mf = read("src/framework/VideoEncoder_MediaFoundation.cpp")
    require(mf, 'LoadLibraryW( L"mfplat.dll" )', "Media Foundation loads at run time (Windows N editions)")
    if re.search(r'pragma comment\(\s*lib,\s*"mf(plat|readwrite)', mf):
        raise AssertionError("mfplat/mfreadwrite must not be linked: the game must start without Media Foundation")
    avi = read("src/framework/VideoEncoder_AVI.cpp")
    require(avi, "cinfo.input_components = JPEG_RGB_SAMPLES;", "jpeg-6 is Doom 3's RGBA build")
    require(avi, '"AVIX"', "OpenDML segments past 1 GiB")
    ffmpeg = read("src/framework/VideoEncoder_FFmpeg.cpp")
    require(ffmpeg, "FFmpeg_EncoderWorks(", "GPU encoders only after a test encode")
    require(ffmpeg, "SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN", "no console window on Windows")


def main() -> None:
    validate_renderer()
    validate_session()
    validate_capture()
    print("video_capture_contract: ok")


if __name__ == "__main__":
    main()
