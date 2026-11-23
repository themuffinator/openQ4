# Record Video

openQ4 records what you see and hear to a video file: the game, the HUD,
menus and the console, with the game's own sound. The default is an MP4
(H.264 video, AAC sound) at your window's resolution and 60 frames a second,
good enough to upload as it is.

## Start and stop

The quickest way is a key: bind **Record Video** in **Settings > Controls >
Other**, then press it once to start recording and again to stop.

Or open the console and type:

```
recordVideo
```

and later:

```
stopVideo
```

`recordVideo name` picks the file name; without one the file is named after
the map and the time, such as `airdefense1_20261123-184016.mp4`. To start and
stop with one key, bind `toggleVideo`:

```
bind F10 toggleVideo
```

The console says where the file went when the recording starts and when it
is saved. Videos are written to `baseoq4/videos` in your save folder:
`%LOCALAPPDATA%\openQ4` on Windows, `~/.local/share/openq4` on Linux and
Steam Deck, and `~/Library/Application Support/openQ4` on macOS.

The game keeps playing its sound to you while it records. Starting and
stopping take a moment while the sound device switches over.

## How recording keeps the video smooth

In single-player and when playing back demos, every video frame is one exact
step of the game. The video plays smoothly whatever your computer manages
while recording: if encoding cannot keep up, the game slows down while you
record instead of dropping frames. Online, the game cannot slow the server
down, so recordings follow the clock: when the game draws slower than the
video's frame rate, a frame is shown twice.

Frame rates that divide 60 (60, 30, 20...) record frame-exact; others, such
as 24 or 120, follow the clock. `com_videoTiming` chooses: `auto` (the
default), `fixed` or `realtime`.

## Formats

| `com_videoFormat` | Video | Sound | Needs |
| --- | --- | --- | --- |
| `mp4` (default) | H.264, or HEVC or AV1 with `com_videoCodec` | AAC | Nothing on Windows; FFmpeg elsewhere |
| `mkv` | H.264, HEVC, AV1, VP9 or ProRes | AAC | FFmpeg |
| `webm` | VP9 or AV1 | Opus | FFmpeg |
| `mov` | H.264, HEVC or ProRes 422 HQ (`com_videoCodec prores`) | AAC | FFmpeg |
| `avi` | Motion JPEG | Uncompressed PCM | Nothing |
| `gif` | GIF, at most 30 frames a second and 480 pixels high | None | FFmpeg |

On Windows, MP4 needs nothing else: it is written by Windows' own encoder
(Media Foundation), on your graphics card's video encoder when the driver
offers one. Windows N editions need Microsoft's Media Feature Pack for it.

The other formats use **FFmpeg**, which openQ4 does not include. Install it
and either put `ffmpeg` on your `PATH`, copy it next to the game, or point
`com_videoFFmpeg` at it. FFmpeg uses your graphics card's encoder (NVIDIA
NVENC, AMD AMF, Intel Quick Sync, Apple VideoToolbox) when a test encode
shows it works, and its software encoders otherwise.

When a format needs FFmpeg and FFmpeg is not found, the recording is made as
a Motion JPEG AVI instead, which needs nothing but makes large files (about
5 MB a second at 1280x720). `videoEncoders` lists what your system has.

## Settings

The everyday settings are in **Settings > Game Options**, under **Video
Capture** (on the General tab; in the classic menus, the page's last
section):

| Menu row | Setting | Choices |
| --- | --- | --- |
| Video Format | `com_videoFormat` | MP4, MKV, WebM, MOV, AVI, GIF |
| Video Quality | `com_videoQuality` | 1 to 100 |
| Video Frame Rate | `com_videoFPS` | 24, 30, 60, 120 |
| Video Resolution | `com_videoHeight` | Window Size, 480p, 720p, 1080p, 1440p, 2160p |
| Record Sound | `com_videoAudio` | On or off (GIFs have no sound) |
| Video Timing | `com_videoTiming` | Automatic, Frame-Exact, Real Time |

Changes apply to the next recording. Everything else is a console setting:

| Setting | Default | What it does |
| --- | --- | --- |
| `com_videoFormat` | `mp4` | File format; see above |
| `com_videoCodec` | `auto` | `h264`, `hevc`, `av1`, `vp9`, `prores` or `mjpeg`; `auto` picks the format's usual one |
| `com_videoFPS` | `60` | Frames a second |
| `com_videoQuality` | `80` | 1 (smallest file) to 100 (best picture); 80 is close to the screen |
| `com_videoBitrate` | `0` | A fixed bitrate in kbit/s instead of `com_videoQuality` |
| `com_videoHeight` | `0` | Scale down to this height, keeping the shape; 0 records at the window's size |
| `com_videoAudio` | `1` | Record the game's sound |
| `com_videoAudioBitrate` | `192` | Sound bitrate in kbit/s |
| `com_videoTiming` | `auto` | `fixed`, `realtime` or `auto` (fixed offline, real time online) |
| `com_videoHardware` | `1` | Allow the graphics card's video encoder |
| `com_videoEncoder` | `auto` | Force `mediafoundation`, `ffmpeg` or `builtin` |
| `com_videoPreset` | `auto` | Speed of FFmpeg's software H.264/HEVC encoders (x264 names such as `veryfast` or `slow`) |
| `com_videoFFmpeg` | empty | The `ffmpeg` program or its folder |
| `com_videoFolder` | `videos` | Folder under your save folder |

For example, a 1080p, 30-frame recording as small as it will go while still
looking good:

```
set com_videoHeight 1080
set com_videoFPS 30
set com_videoQuality 70
recordVideo
```

## Demos

The classic `aviGame` and `aviDemo name` commands record video too, with the
same settings, at `com_aviDemoTics` game steps a frame: 30 frames a second
at its default of 2, 60 at 1. `aviDemo` plays a render demo back exactly, one
step a frame, so the video is smooth even on a slow computer.

## Good to know

- The recording is exactly what the window shows: open the console or a menu
  and it is in the video.
- Recording works with OpenGL and Vulkan. A renderer that cannot read its
  frames back (OpenGL with `r_frontBuffer 1`) refuses after a few seconds
  with a console message.
- The window can be resized while recording; frames are scaled to the size
  the recording started at.
- A recording continues across level changes; the loading screens are left
  out.
