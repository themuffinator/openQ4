#!/usr/bin/env python3
"""Merge the tracked sheets into an existing complete visual atlas.

The atlas contains independently authored vector references. Keep extracted
retail reference images outside Git; this updater does not read game assets.
"""
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
START = '<!-- openq4-medlabs-implant-start -->'
END = '<!-- openq4-medlabs-implant-end -->'
AWAKENING_START = '<!-- openq4-awakening-menu-start -->'
AWAKENING_END = '<!-- openq4-awakening-menu-end -->'

def update_audio(text):
    """Replace the illustrative voice driver with local playback sources."""
    start, end = '// openq4-playback-graphs-start', '// openq4-playback-graphs-end'
    text = re.sub(r'\n*' + re.escape(start) + r'.*?' + re.escape(end) + r'\n*', '\n\n', text, flags=re.S)
    text = re.sub(r"/\* A voice line's level.*?(?=/\* The trace in a rect)",
                  '/* Playback controls below provide the Remastered drive. */\nA.voiceLevel = () => 0;\n', text, flags=re.S)
    script = (ROOT / 'docs/dev/ui/atlas/audio-waveforms.js').read_text(encoding='utf-8')
    anchor = '/* The Rhino squad emblem'
    if anchor not in text:
        raise ValueError('The atlas is missing its transmission module')
    text = text.replace(anchor, start + '\n' + script + '\n' + end + '\n\n' + anchor, 1)
    text = text.replace('mode: "voice", offset: i * 2750', 'mode: "voice", source: t.self ? "voice:self" : "voice:" + t.name')
    text = text.replace('mode: "voice", offset: 900', 'mode: "voice", source: "voice:self"')
    text = text.replace('A.voiceLevel((now + 900))', 'A.playbackPeaks("voice:self", 1)[0]')
    text = text.replace('Remastered follows the playing line\'s level, so the trace falls flat between words.',
        'Remastered draws the actual decoded audio peaks at the playback cursor. Choose a local audio file above and press Play; silence is flat, pausing holds the graph, and seeking moves it with the audio.')
    text = text.replace("each with the transmission waveform following the talker's level", "each with a separate history of that talker's actual audio peaks")
    text = text.replace('lab(12, 160, "scroll 24.6 u/s toward the leading edge");',
        'lab(12, 160, mode === "table" ? "scroll 24.6 u/s toward the leading edge" : "actual sample peaks · 60 Hz · 650 ms");')
    text = text.replace('lab(12, 174, "core 4 texels; speech spikes to 32%");',
        'lab(12, 174, mode === "table" ? "core 4 texels; speech spikes to 32%" : "silence flat · pause and seek follow playback");')
    return text

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    text = args.base.read_text(encoding='utf-8')
    text = re.sub(r'visual specification 1\.\d+', 'visual specification 1.19', text)
    text = update_audio(text)
    start, end = '<!-- openq4-wheel-placement-start -->', '<!-- openq4-wheel-placement-end -->'
    text = re.sub(re.escape(start) + r'.*?' + re.escape(end), '', text, flags=re.S)
    placement = (ROOT / 'docs/dev/ui/atlas/weapon-wheel-placement.js').read_text(encoding='utf-8')
    text += '\n' + start + '\n<script>\n' + placement + '\n</script>\n' + end + '\n'
    text = re.sub(r'\n*' + re.escape(START) + r'.*?' + re.escape(END) + r'\n*', '\n', text, flags=re.S)
    sheet = (ROOT / 'docs/dev/ui/atlas/medlabs-implant.html').read_text(encoding='utf-8')
    text = text.rstrip() + '\n' + START + '\n' + sheet + '\n' + END + '\n'
    if 'href="#medlabs-implant"' not in text:
        text = text.replace('href="#sphud"', 'href="#medlabs-implant">Implant</a><a href="#sphud"', 1)
    # The Awakening's in-game menu family (visual specification 1.40).
    text = re.sub(r'\n*' + re.escape(AWAKENING_START) + r'.*?' + re.escape(AWAKENING_END) + r'\n*', '\n', text, flags=re.S)
    sheet = (ROOT / 'docs/dev/ui/atlas/awakening-menu.html').read_text(encoding='utf-8')
    text = text.rstrip() + '\n' + AWAKENING_START + '\n' + sheet + '\n' + AWAKENING_END + '\n'
    if 'href="#awakening-menu"' not in text:
        text = text.replace('href="#change"', 'href="#awakening-menu">Awakening menus</a><a href="#change"', 1)
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding='utf-8')
    print(args.output.resolve())

if __name__ == '__main__':
    main()
