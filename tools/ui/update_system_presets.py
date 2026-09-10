#!/usr/bin/env python3
"""Compose SYSTEM performance controls from its existing editable vector parts.

This generator owns only the performance band, its feedback and draft bindings.
The shared framework profile catalog supplies values at dispatch; this document
never duplicates those values or applies a profile on popup highlight.
"""
import argparse
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('content/baseoq4/pak0/guis/menu/settings/system.q4ui')
PROFILES = ('minimum', 'lowpower', 'performance', 'balanced', 'quality', 'ultra')


def nodes(root):
    result = {}
    def walk(node):
        if node['id'] in result: raise ValueError('Duplicate canonical node: ' + node['id'])
        result[node['id']] = node
        for child in node.get('children', []): walk(child)
    walk(root)
    return result


def typed(kind, value, unit=None):
    result = {'type': kind, 'value': value}
    if unit is not None: result['unit'] = unit
    return result


def length(value, unit='dp'): return typed('length', value, unit)
def keyword(value): return typed('keyword', value)
def number(value): return typed('number', value)


def renamed(value, old, new):
    if isinstance(value, str): return new + value[len(old):] if value.startswith(old) else value
    if isinstance(value, list): return [renamed(item, old, new) for item in value]
    if isinstance(value, dict): return {key: renamed(item, old, new) for key, item in value.items()}
    return value


def allowed():
    terms = [{'state': 'settings.open'}, {'op': '==', 'args': [{'state': 'settings.phase'}, 1]}]
    terms += [{'op': '!', 'args': [{'state': key}]} for key in
              ('settings.busy', 'settings.confirmationVisible', 'page.discardVisible', 'ui.numberDraftsPending')]
    result = terms[0]
    for term in terms[1:]: result = {'op': '&&', 'args': [result, term]}
    return result


def compose(document):
    result = copy.deepcopy(document)
    index = nodes(result['root'])
    choice = renamed(index['settings_postaa'], 'settings_postaa', 'settings_preset')
    auto = renamed(index['settings_back'], 'settings_back', 'settings_autodetect')
    choice['properties'].update({'width': length(320), 'min-width': length(240),
                                 'display': keyword('flex'), 'flex-wrap': keyword('wrap'),
                                 'align-items': keyword('center'), 'column-gap': length(8), 'row-gap': length(2),
                                 'min-height': length(44), 'padding': length(10),
                                 'flex-grow': number(2), 'flex-shrink': number(1), 'margin-bottom': length(0)})
    c = choice['control']
    c.update(label='#str_229976', action='preset', value={'state': 'settings.draft.com_performancePreset'})
    template = copy.deepcopy(c['options'][0])
    ci = nodes(choice)
    row = copy.deepcopy(ci['settings_preset-option-0'])
    c['options'] = []
    ci['settings_preset-content']['children'] = []
    for i, name in enumerate(PROFILES):
        ident = 'settings_preset-option-' + str(i)
        option = renamed(template, 'settings_preset-option-0', ident)
        option.update(label='#str_229977', labelIndex=i, value=name)
        c['options'].append(option)
        item = renamed(row, 'settings_preset-option-0', ident)
        nodes(item)[ident + '-label']['properties']['text'] = typed('text', '#str_229977')
        ci['settings_preset-content']['children'].append(item)
    ci['settings_preset-label']['properties']['text'] = typed('text', '#str_229976')
    # The title and selected option share a row when space permits. Both remain
    # in normal flow and wrap at narrow widths or with expanded translations.
    ci['settings_preset-label']['properties'].update({'flex-grow': number(1), 'flex-shrink': number(1)})
    ci['settings_preset-value']['properties'].pop('height', None)
    ci['settings_preset-value']['properties'].update({'min-height': length(23),
        'margin-top': length(0), 'flex-shrink': number(1)})
    ci['settings_preset-chevron']['properties'].pop('top', None)
    ci['settings_preset-chevron']['properties']['bottom'] = length(18)
    auto['properties'].pop('margin-right', None)
    auto['properties'].update({'width': length(200), 'min-width': length(176),
                              'flex-grow': number(1), 'flex-shrink': number(1)})
    auto['control'].update(label='#str_229978', event='autoDetect')
    nodes(auto)['settings_autodetect-label']['properties']['text'] = typed('text', '#str_229978')
    band = {'id': 'performance-band', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('flex'), 'box-sizing': keyword('border-box'),
        'opacity': number(1), 'width': length(100, '%'), 'flex-wrap': keyword('wrap'),
        'column-gap': length(16), 'row-gap': length(12), 'align-items': keyword('center')},
        'children': [choice, auto]}
    body = index['settings-body']
    body['children'] = [band] + [child for child in body['children'] if child['id'] != 'performance-band']
    result['actions']['preset'] = {'input': 'string', 'operation': 'settings.system.preset',
                                   'arguments': {'name': {'input': 'value'}}}
    result['actions']['autoDetect'] = {'operation': 'settings.system.autodetect', 'arguments': {}}
    result['events']['autoDetect'] = [{'op': 'if', 'condition': allowed(),
                                      'then': [{'op': 'action', 'action': 'autoDetect'}]}]
    for target, original in [('settings_preset', 'settings_postaa'), ('settings_autodetect', 'settings_back')]:
        result['bindings'] = [b for b in result['bindings'] if b['id'] != target + '.enabled']
        result['bindings'].append({'id': target + '.enabled', 'node': target, 'property': 'enabled', 'value': allowed()})
        result['timelines'] = [t for t in result['timelines'] if not t['id'].startswith(target + '.')]
        result['timelines'] += [renamed(t, original, target) for t in list(result['timelines']) if t['id'].startswith(original + '.')]
    # Numeric-pair composition owns the final feedback/binding entries. Keep
    # that order stable so both independent generators remain idempotent.
    for key in ('bindings', 'timelines'):
        tail = [v for v in result[key] if v['id'].startswith(('settings_brightness_number.', 'settings_ambient_number.'))]
        result[key] = [v for v in result[key] if v not in tail] + tail
    for prefix in ('draft', 'baseline'):
        alias = prefix + 'Preset'
        result['presentationVariables'][alias] = {'type': 'string', 'initial': '',
            'value': {'state': 'settings.' + prefix + '.com_performancePreset'}}
        result['aliases'][alias] = {'variable': alias}
    result['extensions']['openq4']['scope'] = ('Production SYSTEM foundation with paired local numeric fields, immediate image controls, preset draft selection and display-confirmation structure. Full setting inventory, native text entry, display catalogs, preset effect execution, audio, accessibility and editor acceptance remain open.')
    result['extensions']['openq4']['presets'] = {
        'source': 'src/framework/PerformancePreset.cpp',
        'scope': 'Source-backed preset and Auto-Detect draft editing. Device effects, persistent application and full SYSTEM inventory remain separately gated.',
        'localization': ['#str_229976', '#str_229977', '#str_229978'],
        'proposalPolicy': 'Popup highlight is transient. Selected value follows authoritative draft readback before acknowledgement.'}
    nodes(result['root'])
    return result


def load(path):
    raw = path.read_text(encoding='utf-8'); first = raw.index('{')
    return raw[:first], json.loads(raw[first:])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    prefix, document = load(args.source); result = compose(document)
    if args.check:
        if result != document: raise SystemExit('SYSTEM performance controls differ from shared artwork composition')
    else: args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
