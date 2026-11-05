#!/usr/bin/env python3
"""Canonical content manifest shared by Android APK verification and fixtures."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

CONTENT_PACKS_TABLE = Path(__file__).resolve().with_name('content_packs.json')


def content_pack_names() -> tuple[str, ...]:
    """Every runtime pack in tools/build/content_packs.json, in load order."""
    document = json.loads(CONTENT_PACKS_TABLE.read_text(encoding='utf-8'))
    names = tuple(pack['name'] for pack in document['packs'])
    if not names or any(not re.fullmatch(r'pak[0-9]\.pk4', name) for name in names):
        raise ValueError('Invalid content pack table')
    return names


def required_package_files() -> set[str]:
    return {'mod.json', *content_pack_names()}


def package_manifest(directory: Path) -> bytes:
    entries = {}
    for path in directory.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Package assets may not be links: {path}')
        if path.is_file():
            name = path.relative_to(directory).as_posix()
            if (len(name) > 240 or not re.fullmatch(r'[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*', name)
                    or len(name.split('/')) > 16 or any(part in ('.', '..') for part in name.split('/'))):
                raise ValueError(f'Invalid package path: {name}')
            entries[name] = path
    if len(entries) > 4096 or not required_package_files() <= entries.keys():
        raise ValueError('Invalid package manifest file count or missing required file')
    lines = ['openQ4-package-v1\n']
    total = 0
    for name, path in sorted(entries.items()):
        size = path.stat().st_size
        total += size
        if size > 2 * 1024**3 or total > 4 * 1024**3:
            raise ValueError('Package size exceeds limit')
        digest = hashlib.sha256()
        count = 0
        with path.open('rb') as stream:
            while chunk := stream.read(65536):
                count += len(chunk)
                if count > size:
                    raise ValueError(f'Package file changed during staging: {name}')
                digest.update(chunk)
        if count != size:
            raise ValueError(f'Package file changed during staging: {name}')
        lines.append(f'{digest.hexdigest()}\t{size}\t{name}\n')
    result = ''.join(lines).encode('ascii')
    if len(result) > 1024 * 1024:
        raise ValueError('Package manifest exceeds limit')
    return result
