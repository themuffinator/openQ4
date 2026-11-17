#!/usr/bin/env python3
"""Assemble the openQ4 interface atlas from its tracked parts.

docs/dev/ui/atlas/atlas.json lists the page in order: shell fragments, one
markup file per sheet, the drawing scripts and their styles, and the literal
text between them. The atlas draws vector reconstructions only; it reads no
game assets, and none of its parts are extracted retail art.

    python tools/ui/update_design_atlas.py                 # .tmp/atlas/openq4-atlas.html
    python tools/ui/update_design_atlas.py --check         # every part present, every file used
    python tools/ui/update_design_atlas.py --compare FILE  # report where the build differs from FILE
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ATLAS = ROOT / 'docs/dev/ui/atlas'
MANIFEST = ATLAS / 'atlas.json'


def load_manifest(path=MANIFEST):
    return json.loads(path.read_text(encoding='utf-8'))


def assemble(manifest, root=ATLAS):
    pieces = []
    for entry in manifest['parts']:
        if 'file' in entry:
            # Checkouts may carry CRLF; the atlas is assembled with LF.
            pieces.append((root / entry['file']).read_bytes().decode('utf-8').replace('\r\n', '\n'))
        else:
            pieces.append(entry['text'])
    return ''.join(pieces)


def check(manifest, root=ATLAS):
    """Every listed part exists once, and every part file on disk is listed."""
    problems = []
    listed = [entry['file'] for entry in manifest['parts'] if 'file' in entry]
    for name in listed:
        if not (root / name).is_file():
            problems.append(f'missing part {name}')
    for name in sorted({n for n in listed if listed.count(n) > 1}):
        problems.append(f'part listed twice: {name}')
    on_disk = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    on_disk -= {'atlas.json', 'README.md'}
    for name in sorted(on_disk - set(listed)):
        problems.append(f'unlisted part {name}')
    return problems


def first_difference(a, b):
    for index, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return index
    return min(len(a), len(b)) if len(a) != len(b) else -1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=ROOT / '.tmp/atlas/openq4-atlas.html')
    parser.add_argument('--check', action='store_true', help='validate the manifest and exit')
    parser.add_argument('--compare', type=Path, help='compare the build with an existing atlas file')
    args = parser.parse_args()
    manifest = load_manifest()
    problems = check(manifest)
    if problems:
        print('\n'.join(problems), file=sys.stderr)
        return 1
    if args.check:
        print(f'{len(manifest["parts"])} parts, all present')
        return 0
    page = assemble(manifest)
    if args.compare:
        other = args.compare.read_text(encoding='utf-8')
        index = first_difference(page, other)
        if index < 0:
            print(f'identical to {args.compare}')
            return 0
        print(f'differs from {args.compare} at character {index}: {page[index:index + 80]!r} vs {other[index:index + 80]!r}')
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(page.encode('utf-8'))
    print(f'{args.output} ({len(page):,} characters)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
