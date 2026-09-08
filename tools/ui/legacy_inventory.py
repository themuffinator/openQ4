#!/usr/bin/env python3
"""Inventory effective Quake 4 GUI sources without extracting retail assets.

Mounts are explicitly ordered from low to high priority. Within a mount, loose
files beat PK4s; PK4 order follows FileSystem.cpp's FS_ComparePk4LoadOrder.
This is a lexical/dependency inventory, not an engine parser or parity verifier.
No macro expansion, conditional evaluation or gameplay reachability is claimed.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
from functools import cmp_to_key
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
from zipfile import BadZipFile, ZipFile


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    start: int
    end: int
    line: int
    column: int


class LexError(ValueError):
    pass


def lex(source: str) -> list[Token]:
    """Retain spans/locations; ignore comments and keep strings indivisible."""
    tokens: list[Token] = []
    i, line, column = 0, 1, 1
    punctuation = set('{}()[],;#=+*?!<>|&%:-')
    while i < len(source):
        start, start_line, start_column = i, line, column
        char = source[i]
        kind = 'word'
        if char.isspace():
            i += 1
            kind = 'skip'
        elif source.startswith('//', i):
            end = source.find('\n', i)
            i = len(source) if end < 0 else end
            kind = 'skip'
        elif source.startswith('/*', i):
            end = source.find('*/', i + 2)
            if end < 0:
                raise LexError(f'{line}:{column}: unterminated block comment')
            i = end + 2
            kind = 'skip'
        elif char in '\"\'':
            quote = char
            i += 1
            while i < len(source) and source[i] != quote:
                i += 2 if source[i] == '\\' else 1
            if i >= len(source):
                raise LexError(f'{line}:{column}: unterminated quoted token')
            i += 1
            kind = 'string'
        elif char in punctuation:
            i += 1
            kind = 'punctuation'
        else:
            while i < len(source):
                if (source[i].isspace() or source[i] in punctuation or
                        source[i] in '\"\'' or source.startswith('//', i) or
                        source.startswith('/*', i)):
                    break
                i += 1
        raw = source[start:i]
        if kind != 'skip':
            # Keep engine escape spelling: this inventory must not corrupt
            # paths/color escapes by applying Python's string escape rules.
            value = raw[1:-1] if kind == 'string' else raw
            tokens.append(Token(kind, value, start, i, start_line, start_column))
        lines = raw.count('\n')
        line += lines
        column = len(raw.rsplit('\n', 1)[-1]) + 1 if lines else column + len(raw)
    return tokens


def qpath(value: str) -> str:
    value = value.replace('\\', '/')
    if not value or value.startswith('/') or ':' in value:
        raise ValueError(f'not a game-relative path: {value!r}')
    parts = value.split('/')
    if '..' in parts:
        raise ValueError(f'parent traversal in game-relative path: {value!r}')
    return '/'.join(p for p in parts if p not in ('', '.')).lower()


def compare_paks(left: str, right: str) -> int:
    """Independent implementation of the engine's documented package order."""
    a, b = left.lower(), right.lower()
    ma, mb = re.fullmatch(r'pak(\d+)\.pk4', a), re.fullmatch(r'pak(\d+)\.pk4', b)
    if ma and mb:
        ka, kb = (-len(ma[1]), int(ma[1])), (-len(mb[1]), int(mb[1]))
        if ka != kb:
            return (ka > kb) - (ka < kb)
    if bool(ma) != bool(mb):
        ka, kb = ('pak' if ma else a), ('pak' if mb else b)
        if ka != kb:
            return (ka > kb) - (ka < kb)
        return -1 if ma else 1
    return (a > b) - (a < b)


@dataclass(frozen=True)
class Resource:
    path: str
    owner: str
    location: Path
    member: str | None = None

    def read(self) -> bytes:
        if self.member is None:
            return self.location.read_bytes()
        with ZipFile(self.location) as archive:
            return archive.read(self.member)


class Catalog:
    def __init__(self) -> None:
        self.resources: dict[str, Resource] = {}
        self.versions: dict[str, list[dict]] = defaultdict(list)
        self.diagnostics: list[dict] = []
        self.mounts: list[dict] = []

    def add(self, resource: Resource, layer_paths: dict[str, str]) -> None:
        try:
            key = qpath(resource.path)
        except ValueError as error:
            self.diagnostics.append({'severity': 'error', 'code': 'invalid_qpath',
                                     'source': resource.owner, 'message': str(error)})
            return
        if key in layer_paths:
            self.diagnostics.append({
                'severity': 'error', 'code': 'ambiguous_case_or_duplicate',
                'source': resource.owner, 'path': resource.path,
                'other_path': layer_paths[key],
                'message': 'Same-layer duplicate; effective runtime entry needs verification.',
            })
        layer_paths[key] = resource.path
        self.versions[key].append({'source': resource.owner, 'path': resource.path})
        self.resources[key] = resource

    def mount(self, label: str, root: Path) -> None:
        if not root.is_dir():
            raise ValueError(f'mount directory does not exist: {root}')
        if any(item['name'] == label for item in self.mounts):
            raise ValueError(f'duplicate mount label: {label}')
        packages = sorted((p for p in root.iterdir() if p.suffix.lower() == '.pk4'),
                          key=cmp_to_key(lambda a, b: compare_paks(a.name, b.name)))
        mount = {'name': label, 'archives_low_to_high': [], 'ignored_archives': []}
        self.mounts.append(mount)
        for package in packages:
            if root.name.lower() == 'q4base' and re.fullmatch(
                    r'game(?:000|100|200|300|x.*)\.pk4', package.name, re.IGNORECASE):
                mount['ignored_archives'].append(package.name)
                continue
            mount['archives_low_to_high'].append(package.name)
            layer: dict[str, str] = {}
            with ZipFile(package) as archive:
                for item in archive.infolist():
                    if not item.is_dir():
                        self.add(Resource(item.filename, f'{label}/{package.name}',
                                          package, item.filename), layer)
        layer = {}
        for file in sorted(root.rglob('*'), key=lambda p: p.as_posix()):
            if file.is_file() and file.suffix.lower() != '.pk4':
                self.add(Resource(file.relative_to(root).as_posix(), f'{label}/loose', file), layer)

    def resolve_include(self, owner: str, include: str) -> str | None:
        try:
            requested = qpath(include)
            base = str(PurePosixPath(owner).parent)
            relative = requested if requested.startswith(base + '/') else base + '/' + requested
            for candidate in (relative, requested):
                if candidate in self.resources:
                    return candidate
        except ValueError:
            pass
        return None


def decode(data: bytes) -> tuple[str, str]:
    try:
        return data.decode('utf-8-sig'), 'utf-8'
    except UnicodeDecodeError:
        # Lossless single-byte view for lexical metadata. This is deliberately
        # not an assertion about the localized language's runtime code page.
        return data.decode('latin-1'), 'byte-preserving-latin-1'


def location(token: Token) -> dict:
    return {'line': token.line, 'column': token.column}


def analyze(path: str, resource: Resource, catalog: Catalog) -> dict:
    data = resource.read()
    source, encoding = decode(data)
    result: dict = {
        'path': path, 'source': resource.owner, 'sha256': hashlib.sha256(data).hexdigest(),
        'bytes': len(data), 'encoding_view': encoding, 'windows': [], 'events': [],
        'includes': [], 'directives': [], 'asset_references': [], 'localization_keys': [],
        'state_references': [], 'script_commands': {}, 'diagnostics': [],
        'analysis': 'lexical; macros and expressions are not evaluated',
    }
    try:
        tokens = lex(source)
    except LexError as error:
        result['diagnostics'].append({'severity': 'error', 'code': 'lex_error', 'message': str(error)})
        return result
    keys: set[str] = set()
    state_refs: set[str] = set()
    commands: Counter = Counter()
    script_depths: list[int] = []
    event_openings: set[int] = set()
    header_tokens: set[int] = set()
    depth, minimum_depth = 0, 0
    script_start = False
    for i, token in enumerate(tokens):
        value = token.value
        low = value.lower()
        following = tokens[i + 1: i + 5]
        if token.kind == 'string':
            keys.update(re.findall(r'#str_[A-Za-z0-9_]+', value))
            state_refs.update(re.findall(r'\$?(?:gui|desktop)::[A-Za-z0-9_]+', value, re.IGNORECASE))
        if token.kind == 'punctuation' and value == '#' and following:
            directive = following[0].value.lower()
            result['directives'].append({'name': directive, **location(token)})
            if directive == 'include':
                if len(following) >= 2 and following[1].kind == 'string':
                    requested = following[1].value
                    resolved = catalog.resolve_include(path, requested)
                    result['includes'].append({'requested': requested, 'resolved': resolved, **location(token)})
                    if resolved is None:
                        result['diagnostics'].append({'severity': 'error', 'code': 'missing_include',
                                                      'path': requested, **location(token)})
                else:
                    result['diagnostics'].append({'severity': 'error', 'code': 'include_form_requires_review',
                                                  **location(token)})
        if token.kind == 'word' and low.endswith('def') and len(following) >= 2:
            if following[1].kind == 'punctuation' and following[1].value == '{':
                result['windows'].append({'type': value, 'name': following[0].value, **location(token)})
                header_tokens.add(i + 1)
        is_header_token = i in header_tokens
        if token.kind == 'word' and low.startswith('on') and not script_depths and not is_header_token:
            opening = next((j for j, t in enumerate(following) if t.kind == 'punctuation' and t.value == '{'), None)
            if opening is not None:
                result['events'].append({'type': value,
                                         'argument': ' '.join(t.value for t in following[:opening]),
                                         **location(token)})
                event_openings.add(i + 1 + opening)
                header_tokens.update(range(i + 1, i + 1 + opening))
        if token.kind == 'word' and not script_depths and not is_header_token:
            if low in ('background', 'font', 'model') and following:
                target = following[0]
                result['asset_references'].append({'kind': low, 'value': target.value,
                                                   'literal': target.kind == 'string', **location(token)})
            elif low == 'defineicon' and len(following) >= 2:
                result['asset_references'].append({'kind': 'icon', 'value': following[1].value,
                                                   'literal': following[1].kind == 'string', **location(token)})
        if script_depths and script_start and token.kind == 'word':
            commands[low] += 1
            script_start = False
            if low == 'set' and len(following) >= 2 and following[0].kind == 'string':
                destination = following[0].value.lower()
                if destination == 'background' or destination.endswith('::background'):
                    target = following[1]
                    result['asset_references'].append({'kind': 'script_background', 'value': target.value,
                                                       'literal': target.kind == 'string', **location(token)})
        if token.kind == 'punctuation':
            if value == '{':
                depth += 1
                if i in event_openings:
                    script_depths.append(depth)
                if script_depths:
                    script_start = True
            elif value == '}':
                if script_depths and depth == script_depths[-1]:
                    script_depths.pop()
                depth -= 1
                minimum_depth = min(minimum_depth, depth)
                script_start = bool(script_depths)
            elif value == ';' and script_depths:
                script_start = True
    if depth or minimum_depth < 0:
        result['diagnostics'].append({'severity': 'review', 'code': 'unbalanced_source_fragment',
                                      'final_depth': depth, 'minimum_depth': minimum_depth,
                                      'message': 'Requires include-expanded engine parsing; not proof of a broken GUI.'})
    result['localization_keys'] = sorted(keys)
    result['state_references'] = sorted(state_refs)
    result['script_commands'] = dict(sorted(commands.items()))
    return result


def family_hint(path: str) -> str:
    """Routing aid only; an explicit human/source review still owns acceptance."""
    low = path.lower()
    if '/menu/' in low or PurePosixPath(low).name in ('mainmenu.gui', 'mpmain.gui'):
        return 'menus'
    if '/vehicles/' in low:
        return 'vehicles'
    if '/weapons/' in low or 'scope' in low:
        return 'weapons-scopes'
    if '/loading/' in low:
        return 'loading'
    if any(part in low for part in ('/monitors/', '/maps/', '/movers/', '/models/')):
        return 'world-surfaces'
    if '/common/' in low:
        return 'shared-components'
    if 'hud' in low or 'wristcomm' in low:
        return 'hud-communications'
    if any(word in low for word in ('scoreboard', 'matchcontrol', 'buymenu', 'arena_', 'mpmsg')):
        return 'multiplayer'
    return 'requires-family-review'


def material_index(catalog: Catalog) -> dict[str, list[dict]]:
    """Index direct braced material declarations; never guess guide expansion.

    Multiple definitions remain candidates: declaration-manager first-use and
    file registration order require separate runtime verification.
    """
    definitions: dict[str, list[dict]] = defaultdict(list)
    for path in sorted(p for p in catalog.resources if p.endswith('.mtr')):
        source, _ = decode(catalog.resources[path].read())
        try:
            tokens = lex(source)
        except LexError as error:
            catalog.diagnostics.append({'severity': 'review', 'code': 'material_lex_error',
                                         'resource': path, 'message': str(error)})
            continue
        depth = 0
        for i, token in enumerate(tokens):
            if token.kind != 'punctuation':
                continue
            if token.value == '{':
                if depth == 0 and i > 0:
                    start = i - 1
                    while start > 0 and tokens[start - 1].end == tokens[start].start:
                        start -= 1
                    header = tokens[start:i]
                    name = ''.join(t.value for t in header)
                    # Parenthesized guide invocations are not direct decl names.
                    if header and all(t.value not in '{}(),;' for t in header):
                        definitions[name.lower()].append({'file': path,
                                                         'source': catalog.resources[path].owner,
                                                         **location(tokens[start])})
                depth += 1
            elif token.value == '}':
                depth -= 1
    return definitions


def asset_dependencies(resources: list[dict], catalog: Catalog) -> list[dict]:
    definitions = material_index(catalog)
    references: dict[str, dict] = {}
    for resource in resources:
        for ref in resource['asset_references']:
            value = ref['value']
            item = references.setdefault(value, {'value': value, 'kinds': set(), 'referenced_by': set()})
            item['kinds'].add(ref['kind'])
            item['referenced_by'].add(resource['path'])
    dependencies = []
    for value, item in sorted(references.items()):
        images: list[str] = []
        declarations: list[dict] = []
        if not value:
            resolution = 'empty'
        elif value.startswith('$') or '::' in value:
            resolution = 'runtime_binding'
        elif value.startswith('_'):
            resolution = 'engine_builtin_requires_review'
        else:
            try:
                normalized = qpath(value)
                declarations = definitions.get(normalized, [])
                candidates = [normalized]
                if not PurePosixPath(normalized).suffix:
                    candidates.extend(normalized + extension for extension in ('.tga', '.dds', '.png', '.jpg', '.ttf'))
                images = sorted(p for p in candidates if p in catalog.resources)
                if 'font' in item['kinds']:
                    images.extend(sorted(p for p in catalog.resources
                                         if p.startswith(normalized + '/') and
                                         p.endswith(('.fontdat', '.dat', '.ttf', '.otf'))))
                resolution = ('multiple_declaration_candidates' if len(declarations) > 1 else
                              'declared_material' if declarations else
                              'file_candidates' if images else 'unresolved_requires_review')
            except ValueError:
                resolution = 'expression_or_invalid_path_requires_review'
        dependencies.append({
            'value': value, 'kinds': sorted(item['kinds']), 'referenced_by': sorted(item['referenced_by']),
            'resolution': resolution, 'declaration_candidates': declarations, 'file_candidates': images,
            'art_classification': 'pending', 'bitmap_exception': None,
        })
    return dependencies


def inventory(catalog: Catalog) -> dict:
    paths = sorted(path for path in catalog.resources if path.endswith('.gui'))
    records: dict[str, dict] = {}
    pending = list(paths)
    while pending:
        path = pending.pop(0)
        if path in records:
            continue
        record = analyze(path, catalog.resources[path], catalog)
        records[path] = record
        pending.extend(item['resolved'] for item in record['includes'] if item['resolved'] is not None)
    # Include order is a dependency order, not a runtime root/load order.
    order: list[str] = []
    active: list[str] = []
    visited: set[str] = set()
    graph_diagnostics: list[dict] = []

    def visit(path: str) -> None:
        if path in active:
            cycle = active[active.index(path):] + [path]
            graph_diagnostics.append({'severity': 'error', 'code': 'include_cycle', 'paths': cycle})
            return
        if path in visited:
            return
        active.append(path)
        for include in records[path]['includes']:
            if include['resolved'] is not None:
                visit(include['resolved'])
        active.pop()
        visited.add(path)
        order.append(path)

    for path in sorted(records):
        visit(path)
    resources = [records[path] for path in sorted(records)]
    dependencies = asset_dependencies(resources, catalog)
    windows = Counter(w['type'] for r in resources for w in r['windows'])
    events = Counter(e['type'] for r in resources for e in r['events'])
    commands: Counter = Counter()
    for record in resources:
        commands.update(record['script_commands'])
    diagnostics = catalog.diagnostics + graph_diagnostics + [
        dict(item, resource=r['path']) for r in resources for item in r['diagnostics']]
    return {
        'schema_version': 1,
        'scope': 'All GUI resources and includes in the supplied effective VFS; reachability is unproven.',
        'mounts_low_to_high': catalog.mounts,
        'summary': {'gui_files': len(paths), 'resources_with_includes': len(resources),
                    'window_declarations': sum(windows.values()), 'window_types': dict(sorted(windows.items())),
                    'event_declarations': sum(events.values()), 'event_types': dict(sorted(events.items())),
                    'script_command_tokens': dict(sorted(commands.items())),
                    'unique_localization_keys': len({k for r in resources for k in r['localization_keys']}),
                    'unique_asset_references': len(dependencies),
                    'asset_resolution_counts': dict(sorted(Counter(d['resolution'] for d in dependencies).items())),
                    'diagnostic_counts': dict(sorted(Counter(d['code'] for d in diagnostics).items()))},
        'include_dependency_order': order,
        'source_versions_low_to_high': {p: catalog.versions[p] for p in sorted(records)},
        'resources': resources,
        'asset_dependencies': dependencies,
        'diagnostics': diagnostics,
    }


def migration_seed(report: dict) -> dict:
    return {
        'schema_version': 1,
        'status': 'Inventory seed; no resource is translated or visually/behaviorally qualified.',
        'mounts_low_to_high': report['mounts_low_to_high'],
        'resources': [{
            'path': r['path'], 'source': r['source'], 'source_sha256': r['sha256'],
            'family_hint': family_hint(r['path']), 'family_reviewed': False,
            'includes': [i['resolved'] for i in r['includes']],
            'replacement': None, 'bitmap_exceptions': None,
            'translation_status': 'pending', 'behavior_evidence': [], 'visual_evidence': [],
            'source_diagnostics': r['diagnostics'],
        } for r in report['resources']],
    }


def write_json(path: Path, value: dict | list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + '\n', encoding='utf-8')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mount', action='append', required=True, metavar='NAME=DIRECTORY',
                        help='Game directory, repeat in low-to-high priority order; no automatic savepath mounts.')
    parser.add_argument('--output', type=Path, required=True, help='Full generated lexical inventory JSON.')
    parser.add_argument('--seed-manifest', type=Path, help='Create a new migration seed; refuses to overwrite existing work.')
    parser.add_argument('--export-requests', type=Path, help='Write source/hash requests for native preprocessing.')
    args = parser.parse_args(argv)
    if args.seed_manifest and args.seed_manifest.exists():
        parser.error('migration manifest already exists; refusing to overwrite evidence')
    destinations = [path.resolve() for path in (args.output,args.seed_manifest,args.export_requests) if path]
    if len(set(destinations)) != len(destinations):
        parser.error('inventory, migration manifest and export requests must be different files')
    catalog = Catalog()
    try:
        for mount in args.mount:
            label, sep, directory = mount.partition('=')
            if not sep or not re.fullmatch(r'[A-Za-z0-9_-]+', label):
                raise ValueError('mount must be NAME=DIRECTORY with a simple unique name')
            catalog.mount(label, Path(directory))
        report = inventory(catalog)
        write_json(args.output, report)
        if args.seed_manifest:
            write_json(args.seed_manifest, migration_seed(report))
        if args.export_requests:
            write_json(args.export_requests, [{'path':r['path'],'sha256':r['sha256']} for r in report['resources']])
    except (OSError, ValueError, BadZipFile, RecursionError) as error:
        print(f'GUI inventory failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps(report['summary'], indent=2))
    print(f'Inventory: {args.output}')
    return 2 if any(d['severity'] == 'error' for d in report['diagnostics']) else 0


if __name__ == '__main__':
    raise SystemExit(main())
