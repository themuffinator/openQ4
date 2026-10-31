"""The retained AUDIO settings page (docs/dev/ui-visual-design.md sections 7,
9 and 13.7; UI-01): the stock page's rows (guis/menu/settings/audio.gui),
LEVELS, DEVICES and EFFECTS, in the settings sub-page
(retained_settings_rows). They fit one screen, so the page has no tabs:
LEVELS and DEVICES in the first column, EFFECTS in the second, under Create
Server's header band.

The page applies each change at once, as the stock page does, through the
session (session.menuValue verbs, idSessionLocal::HandleRetainedAudioRequest):
the three volumes and the subtitle size and background as whole percents,
the sound system, device, EAX, speakers and HRTF with the stock page's own
commands (sound drivar, sound eax, sound speakers), which restart the sound
system and may ask with the stock message box. The session publishes the
values it reads back (audio.*) and the device list (audio.device.<i>.label,
audio.device.count). BACK, or the back input, returns to the settings
screen that opened it."""
from __future__ import annotations

from build_retained_screens import Document
from retained_settings_rows import heading, setting_row, settings_subpage, value_column

DEVICES = 16                               # the device list's rows (BuildMainMenuAudioDeviceChoices)

# Each section: its heading and its rows (kind, id, label, verb, state, and
# the slider's range and step or the choice's options).
SECTIONS = (
    ("levels", "#str_41096", (
        ("slider", "audio_volume", "#str_201029", "audioVolume", "audio.volume", (0, 100, 10)),
        ("slider", "audio_music", "#str_201006", "audioMusic", "audio.music", (0, 100, 5)),
        ("slider", "audio_dialogue", "#str_231300", "audioDialogue", "audio.dialogue", (0, 100, 5)),
    )),
    ("devices", "#str_229962", (
        ("choice", "audio_backend", "#str_200971", "audioBackend", "audio.backend", (("#str_200970", 0, 0), ("#str_200970", 1, 1))),
        ("device", "audio_device", "#str_201005", "audioDevice", "audio.device", None),
    )),
    ("effects", "#str_229963", (
        ("toggle", "audio_eax", "#str_200972", "audioEax", "audio.eax", None),
        ("choice", "audio_speakers", "#str_200152", "audioSpeakers", "audio.speakers",
         (("#str_229980", 0, 0), ("#str_229980", 1, 1), ("#str_229980", 2, 2))),
        ("choice", "audio_hrtf", "#str_229981", "audioHrtf", "audio.hrtf",
         (("#str_229980", 0, 0), ("#str_229980", 1, 1), ("#str_229980", 2, 2))),
        ("toggle", "audio_subtitles", "#str_231308", "audioSubtitles", "audio.subtitles", None),
        ("slider", "audio_subtitle_size", "#str_231309", "audioSubtitleSize", "audio.subtitleSize", (100, 200, 10)),
        ("slider", "audio_subtitle_background", "#str_231310", "audioSubtitleBackground", "audio.subtitleBackground", (0, 100, 10)),
        ("toggle", "audio_subtitle_speakers", "#str_231311", "audioSubtitleSpeakers", "audio.subtitleSpeakers", None),
    )),
)
VERBS = tuple(row[3] for _section, _heading, rows in SECTIONS for row in rows)


# The columns: the sections each holds, by their place in SECTIONS.
COLUMNS = ((0, 1), (2,))


def audio_document() -> dict:
    doc = Document("openq4.audio")
    doc.state.update({
        "audio.volume": {"type": "number", "initial": 0}, "audio.music": {"type": "number", "initial": 0},
        "audio.dialogue": {"type": "number", "initial": 0}, "audio.backend": {"type": "number", "initial": 1},
        "audio.device": {"type": "number", "initial": 0}, "audio.device.count": {"type": "number", "initial": 0},
        "audio.eax": {"type": "boolean", "initial": False}, "audio.speakers": {"type": "number", "initial": 0},
        "audio.hrtf": {"type": "number", "initial": 0}, "audio.subtitles": {"type": "boolean", "initial": False},
        "audio.subtitleSize": {"type": "number", "initial": 100}, "audio.subtitleBackground": {"type": "number", "initial": 0},
        "audio.subtitleSpeakers": {"type": "boolean", "initial": False},
    })
    for row in range(DEVICES):
        doc.state[f"audio.device.{row}.label"] = {"type": "string", "initial": ""}
    for _section, _heading, rows in SECTIONS:
        for kind, _ident, _label_key, verb, _state, _extra in rows:
            boolean = kind == "toggle"
            doc.actions[verb] = {"operation": "session.menuValue", "input": "boolean" if boolean else "number",
                                 "arguments": {"command": verb, "value": {"input": "value"}}}
    count = {"state": "audio.device.count"}
    columns = []
    for sections in COLUMNS:
        # Each column's values start where its own labels allow, which leaves
        # the device names their room.
        value_x = value_column([row[2] for place in sections for row in SECTIONS[place][2]])
        items = []
        for place in sections:
            _section, heading_key, rows = SECTIONS[place]
            items.append(heading(heading_key))
            for kind, ident, label_key, verb, state, extra in rows:
                value = {"state": state}
                if kind == "slider":
                    items.append(setting_row(doc, ident, label_key, "slider", value_x, value=value, action=verb, slider=(*extra, 0)))
                elif kind == "toggle":
                    items.append(setting_row(doc, ident, label_key, "toggle", value_x, value=value, action=verb))
                elif kind == "choice":
                    items.append(setting_row(doc, ident, label_key, "choice", value_x, value=value, action=verb,
                                             entries=[((key, index), option) for key, option, index in extra]))
                else:
                    # The sound system's devices, as many as it lists.
                    entries = [({"state": f"audio.device.{row}.label"}, row, {"op": "<", "args": [row, count]}) for row in range(DEVICES)]
                    items.append(setting_row(doc, ident, label_key, "choice", value_x, value=value, action=verb, entries=entries,
                                             count=count))
        columns.append(items)
    return settings_subpage(doc, "audio", "#str_229901", [("audio", "#str_229901", columns)])
