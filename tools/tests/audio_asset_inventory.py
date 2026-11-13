#!/usr/bin/env python3
"""Inventory installed audio and subtitle references without copying transcripts.

Later PK4 names override earlier ones, including case-insensitive virtual names.
The report retains asset identifiers, localization keys and coverage counts only.
It does not establish audibility or promise missing subtitle text exists.
"""
import argparse
from collections import Counter
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import re
import zipfile

TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]|[^\s{}"]+', re.S)


def declarations(text):
    tokens = [m[0] for m in TOKEN.finditer(text) if not m[0].startswith(('//', '/*'))]
    i = 0
    while i < len(tokens):
        start = i
        while i < len(tokens) and tokens[i] != '{':
            i += 1
        if i == len(tokens):
            break
        name = tokens[i - 1].strip('"') if i > start else ''
        i += 1
        begin, depth = i, 1
        while i < len(tokens) and depth:
            depth += (tokens[i] == '{') - (tokens[i] == '}')
            i += 1
        if depth:
            raise ValueError('Unterminated declaration: ' + name)
        yield name.lower(), tokens[begin:i - 1]


def inventory(assets, measure_sizes=False):
    entries, packages = {}, []
    for pak in sorted(assets.glob('*.pk4'), key=lambda p: p.name.lower()):
        with zipfile.ZipFile(pak) as archive:
            packages.append({'name': pak.name, 'size': pak.stat().st_size})
            for info in archive.infolist():
                name = info.filename.replace('\\', '/').lower()
                if name.endswith(('.sndshd', '.lipsync', '.ogg', '.wav', '.roq', '.lang')):
                    entries[name] = (pak, info.filename)
    # Loose user content overrides archives, as the development filesystem does.
    # Read it in place; .home may contain hard-linked reference assets.
    loose = 0
    for path in assets.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.sndshd', '.lipsync', '.ogg', '.wav', '.roq', '.lang'):
            entries[path.relative_to(assets).as_posix().lower()] = (None, path)
            loose += 1
    shaders, lips, texts, hashes = {}, {}, set(), {}
    locale_keys = {}  # language -> keys its installed tables define
    for name, (pak, original) in entries.items():
        if not name.endswith(('.sndshd', '.lipsync', '.lang')):
            continue
        if pak is None:
            data = original.read_bytes()
        else:
            with zipfile.ZipFile(pak) as archive:
                data = archive.read(original)
        hashes[name] = hashlib.sha256(data).hexdigest()
        text = data.decode('utf-8', errors='replace')
        if name.endswith('.lang'):
            keys = set(re.findall(r'"(#str_\d+)"', text))
            texts.update(keys)
            # strings/<language>_<part>.lang, or strings/<language>.lang
            language = Path(name).stem.split('_')[0]
            locale_keys.setdefault(language, set()).update(keys)
            continue
        for decl, body in declarations(text):
            if name.endswith('.sndshd'):
                samples = [s.strip('"').replace('\\', '/') for s in body if s.strip('"').lower().startswith('sound/')]
                description = next((body[i + 1].strip('"') for i, token in enumerate(body[:-1]) if token.lower() == 'description'), '')
                shaders[decl] = {'samples': samples, 'source': name, 'has_description': bool(description),
                                 'vo_for_player': 'voforplayer' in [t.lower() for t in body]}
            else:
                key = next((body[i + 1].strip('"') for i, token in enumerate(body[:-1]) if token.lower() == 'text'), '')
                # Never write raw transcript text. An authored literal is counted.
                lips[decl] = {'source': name, 'text_key': key if re.fullmatch(r'#str_\d+', key) else '',
                              'literal_text': bool(key and not key.startswith('#str_'))}
    voice = {name: shader for name, shader in shaders.items() if shader['vo_for_player'] or
             any(re.match(r'(?i)^sound/vo(?:/|_[^/]+/)', sample) for sample in shader['samples'])}
    mapped = {name for name in voice if name in lips and (lips[name]['literal_text'] or lips[name]['text_key'] in texts)}
    result = {'assets': str(assets), 'packages': packages, 'loose_files': loose, 'decl_sha256': hashes,
            'counts': {'samples': sum(n.endswith(('.ogg', '.wav')) for n in entries),
                       'sound_shaders': len(shaders), 'speech_shaders': len(voice), 'lip_sync_decls': len(lips),
                       'speech_with_text': len(mapped), 'speech_without_text': len(voice) - len(mapped),
                       'unmapped_with_description': sum(voice[n]['has_description'] for n in set(voice) - mapped)},
            'sample_directories': dict(Counter('/'.join(n.split('/')[:2]) for n in entries if n.endswith(('.ogg', '.wav')))),
            # Which installed language tables carry each mapped line's key. A locale
            # without its own entry shows the English text (the normal dictionary
            # fallback); one with no installed table at all is not counted as covered.
            # Literal declaration text is the same in every language.
            'locale_coverage': {language: {'keys': len(keys), 'speech_with_text': sum(
                1 for n in mapped if lips[n]['literal_text'] or lips[n]['text_key'] in keys)}
                for language, keys in sorted(locale_keys.items())},
            'literal_text_lines': sum(1 for n in mapped if lips[n]['literal_text']),
            'speech_without_text': sorted(set(voice) - mapped), 'speech': voice, 'lip_sync': lips,
            # Authored caption-pack declarations (subtitle/<shader or sample path>) present.
            'authored_subtitle_decls': sum(1 for n in lips if n.startswith('subtitle/')),
            '_texts': sorted(texts)}
    if measure_sizes:
        sizes, unknown = [], []
        with ExitStack() as stack:
            archives = {p: stack.enter_context(zipfile.ZipFile(p)) for p in {p for p, _ in entries.values() if p is not None}}
            for name, (pak, original) in entries.items():
                if not name.endswith('.ogg'):
                    continue
                size = archives[pak].getinfo(original).file_size if pak else original.stat().st_size
                with archives[pak].open(original) if pak else original.open('rb') as stream:
                    header = stream.read(300)
                    stream.seek(max(0, size - 65563))
                    tail = stream.read()
                packet = 27 + header[26] if len(header) >= 27 else 0
                channels = header[packet + 11] if len(header) >= packet + 16 else 0
                frames = 0
                if header.startswith(b'OggS') and header[packet:packet + 7] == b'\x01vorbis' and channels in (1, 2):
                    for i in range(len(tail) - 27, -1, -1):
                        if tail[i:i + 4] != b'OggS' or tail[i + 4] != 0 or not tail[i + 5] & 4:
                            continue
                        segments = tail[i + 26]
                        if i + 27 + segments > len(tail):
                            continue
                        if i + 27 + segments + sum(tail[i + 27:i + 27 + segments]) == len(tail):
                            frames = int.from_bytes(tail[i + 6:i + 14], 'little')
                            break
                if not frames or frames >= 2**32:
                    unknown.append(name)
                    continue
                sizes.append({'sample': name, 'input_bytes': size, 'pcm_bytes': frames * channels * 2,
                              'decode_reservation_bytes': size + frames * channels * 2})
        result['ogg_header_estimates'] = {
            'files': len(sizes), 'unknown': unknown,
            'total_input_bytes': sum(s['input_bytes'] for s in sizes),
            'total_pcm_bytes': sum(s['pcm_bytes'] for s in sizes),
            'largest_reservations': sorted(sizes, key=lambda s: s['decode_reservation_bytes'], reverse=True)[:20],
            'over_32_mib': sum(s['decode_reservation_bytes'] > 32 * 1024**2 for s in sizes),
            'over_64_mib': sum(s['decode_reservation_bytes'] > 64 * 1024**2 for s in sizes),
        }
    return result


def has_text(lips, texts, name):
    lip = lips.get(name.lower())
    return bool(lip) and (lip['literal_text'] or lip['text_key'] in texts)


def pack_template(report, texts):
    """Empty authored subtitle/ declarations for every spoken line the runtime finds
    no transcript for (src/sound/snd_emitter.cpp SoundTranscriptText): by shader for
    a single-sample shader, by sample path otherwise. Identifiers only, no text."""
    lips = report['lip_sync']
    lines = ['// openQ4 caption pack template generated by tools/tests/audio_asset_inventory.py.',
             "// Fill in text with your own words, or a #str_ key from your pack's own .lang files.",
             '// Only text you have the right to publish belongs here; an entry left empty is ignored.',
             '// Ship the file as lipsync/<name>.lipsync in a PK4 or mod folder; see docs/user/gameplay-settings.md.', '']
    entries = 0
    for name, shader in sorted(report['speech'].items()):
        samples = list(dict.fromkeys(re.sub(r'\.[^./]+$', '', s.lower()) for s in shader['samples']))
        if len(shader['samples']) == 1 and (has_text(lips, texts, name) or has_text(lips, texts, samples[0])):
            continue
        keys = [name] if len(shader['samples']) == 1 else [s for s in samples if not has_text(lips, texts, s)]
        for key in keys:
            lines.append(f'lipSync subtitle/{key} {{ text "" }}  // {shader["source"]}')
            entries += 1
    return '\n'.join(lines) + '\n', entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', required=True, type=Path, help='One game directory containing PK4s')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--sizes', action='store_true', help='Measure Ogg header allocation estimates; does not decode audio')
    parser.add_argument('--pack-template', type=Path, help='Also write an empty caption-pack .lipsync for the lines without a transcript')
    args = parser.parse_args()
    report = inventory(args.assets.resolve(), args.sizes)
    if not report['packages'] and not report['loose_files']:
        parser.error('No PK4s or loose assets found in the requested game directory')
    if args.pack_template:
        template, entries = pack_template(report, set(report.pop('_texts')))
        args.pack_template.parent.mkdir(parents=True, exist_ok=True)
        args.pack_template.write_text(template, encoding='utf-8')
        report['counts']['pack_template_entries'] = entries
    report.pop('_texts', None)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'output': str(output), 'counts': report['counts']}))


if __name__ == '__main__':
    main()
