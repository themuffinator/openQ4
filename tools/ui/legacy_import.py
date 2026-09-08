#!/usr/bin/env python3
"""Bind native-preprocessed GUI tokens to source hashes and engine diagnostics."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re

PATH = re.compile(r'guis/[A-Za-z0-9_./-]{1,240}')


def requests(path: Path) -> list[dict]:
    values = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(values, list) or not 1 <= len(values) <= 512:
        raise ValueError('legacy import requires 1..512 source/hash records')
    seen = set()
    for value in values:
        if not isinstance(value, dict) or set(value) != {'path', 'sha256'}:
            raise ValueError('legacy import record requires path and sha256')
        source, digest = value['path'], value['sha256']
        if (not isinstance(source, str) or not PATH.fullmatch(source) or '..' in source or
                source.lower() in seen or not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest)):
            raise ValueError('invalid or duplicate legacy import source/hash')
        seen.add(source.lower())
    if len(commands(values)) > 32768:
        raise ValueError('legacy import command batch exceeds 32 KiB')
    return values


def commands(values: list[dict]) -> str:
    return ''.join(f'ui_exportLegacy "{row["path"]}" "ui-import/{i:05d}.json"\n' for i,row in enumerate(values))


def collect(game: Path, log: str, values: list[dict]) -> dict:
    records = []
    blocks = re.findall(r'^UI_GUI_EXPORT_BEGIN ([^\r\n]+)\r?\n(.*?)^UI_GUI_EXPORT_END ([^\r\n]+)\r?$', log, re.M | re.S)
    if len(blocks) != len(values):
        return {'passed':False, 'diagnostics':['missing or extra native export intervals'], 'resources':[]}
    for index,(requested,block) in enumerate(zip(values,blocks)):
        source, messages, end = block
        output = f'ui-import/{index:05d}.json'
        result = dict(requested, output=output, diagnostics=[])
        errors = result['diagnostics']
        if source != requested['path']:
            errors.append('native export order/source mismatch')
        errors.extend(line for line in messages.splitlines() if 'WARNING:' in line or 'ERROR:' in line or line.startswith('usage:'))
        match = re.fullmatch(r'exported (\d+) ' + re.escape(output), end)
        if not match:
            errors.append('native export did not finish')
        path = game / output
        if path.is_file() and match:
            try:
                data = json.loads(path.read_text(encoding='ascii'))
                if data['format'] != 1 or data['encoding'] != 'byte-preserving-latin-1' or data['source'] != source:
                    raise ValueError('native token file identity mismatch')
                digest = hashlib.sha256(data['source_bytes'].encode('latin-1')).hexdigest()
                result['runtime_source_sha256'] = digest
                if digest != requested['sha256']:
                    errors.append('effective engine source differs from inventory')
                tokens = data['tokens']
                if not isinstance(tokens, list) or len(tokens) != int(match[1]) or len(tokens) > 1000000:
                    raise ValueError('native token count mismatch')
                for token in tokens:
                    if (not isinstance(token,list) or len(token) != 6 or
                            any(type(v) is not int for v in token[:4]) or
                            any(not isinstance(v,str) for v in token[4:])):
                        raise ValueError('invalid native token record')
                result['tokens'] = len(tokens)
                result['parser_flags'] = data['parser_flags']
                result['output_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
            except (KeyError, TypeError, ValueError, UnicodeError) as error:
                errors.append(str(error))
        elif not path.is_file():
            errors.append('missing native token file')
        result['passed'] = not errors
        records.append(result)
    return {'passed':all(row['passed'] for row in records), 'resources':records}
