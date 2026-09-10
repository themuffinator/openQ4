#!/usr/bin/env python3
"""Author paired precise Number fields using the existing SYSTEM slider artwork.

The output remains editable canonical JSON. This tool owns the two paired-row
components and their Number bindings/timelines, not the page's transaction or
native-input implementation. It never reads assets, launches a game or touches
staged output. --check verifies reproducible generation without writing.
"""
from __future__ import annotations
import argparse
import copy
import json
from functools import reduce
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path('content/baseoq4/pak0/guis/menu/settings/system.q4ui')
PAIRS = {
    'settings_brightness': ('r_brightness', '#str_230004'),
    'settings_ambient': ('r_forceAmbient', '#str_230005'),
}

def value(kind, data, unit=None):
    result = {'type': kind, 'value': data}
    if unit: result['unit'] = unit
    return result

def length(data, unit='dp'): return value('length', data, unit)
def keyword(data): return value('keyword', data)
def base(position='relative'):
    return {'position': keyword(position), 'display': keyword('block'),
            'box-sizing': keyword('border-box'), 'opacity': value('number', 1)}
def node(identifier, kind='group', properties=None, children=None):
    result = {'id': identifier, 'type': kind, 'properties': base() | (properties or {})}
    if children is not None: result['children'] = children
    return result

def index_nodes(root):
    result = {}
    def visit(current):
        if current['id'] in result: raise ValueError('Duplicate authored node: ' + current['id'])
        result[current['id']] = current
        for child in current.get('children', []): visit(child)
    visit(root)
    return result

def rectangle_path(identifier, color):
    return {'id': identifier, 'commands': [
        {'id': 'p0', 'op': 'move', 'points': [[0, 0]]},
        {'id': 'p1', 'op': 'line', 'points': [[{'fraction': 1, 'dp': 0}, 0]]},
        {'id': 'p2', 'op': 'line', 'points': [[{'fraction': 1, 'dp': 0}, {'fraction': 1, 'dp': 0}]]},
        {'id': 'p3', 'op': 'line', 'points': [[0, {'fraction': 1, 'dp': 0}]]},
        {'id': 'close', 'op': 'close'}],
        'fill': {'type': 'solid', 'color': value('color', color)}}

def paint(identifier, width, height, color):
    result = node(identifier, 'vector', base('absolute') | {
        'left': length(0), 'top': length(0), 'width': length(width), 'height': length(height),
        'pointer-events': keyword('none')})
    result['paths'] = [rectangle_path('ink', color)]
    return result

def draft_guards(result):
    """Authored presentation guards; the adapter independently enforces ownership."""
    state = lambda key: {'state': key}
    op = lambda name, *args: {'op': name, 'args': list(args)}
    conjunction = lambda *args: reduce(lambda a,b: op('&&',a,b),args)
    pending = state('ui.numberDraftsPending')
    result['state']['ui.numberDraftsPending'] = {'type': 'boolean', 'initial': False}
    result['state']['ui.numberDraftMessage'] = {'type': 'string', 'initial': '#str_229982'}
    result['actions']['focusNumberDraft'] = {'operation': 'ui.numberDrafts.focus', 'arguments': {}}
    editing = conjunction(state('settings.open'),op('==',state('settings.phase'),1))
    closed = conjunction(op('!',state('settings.open')),op('==',state('settings.phase'),0))
    idle = conjunction(op('!',state('settings.busy')),op('!',state('settings.confirmationVisible')))
    local_message = conjunction(pending,idle,op('||',op('==',state('settings.phase'),0),op('==',state('settings.phase'),1)))
    dialog = conjunction(state('page.discardVisible'),idle,op('||',editing,conjunction(closed,pending)))
    continuing = conjunction(state('page.discardVisible'),idle,editing)
    apply = conjunction(state('settings.canApply'),op('!',pending))
    apply_exit = conjunction(continuing,state('settings.dirty'),apply)
    result['events']['continueEditing'] = [
        {'op': 'setState', 'values': {'page.discardVisible': False}},
        {'op': 'action', 'action': 'focusNumberDraft'}]
    result['events']['onBack'][0]['then'] = [{'op': 'call', 'event': 'continueEditing'}]
    result['events']['onBack'][0]['else'][0]['then'][0]['condition'] = op('||',state('settings.dirty'),pending)
    result['events']['discard'][0]['condition'] = dialog
    result['events']['applyExit'][0]['condition'] = apply_exit
    result['events']['apply'] = [{'op': 'if', 'condition': apply, 'then': [{'op': 'action', 'action': 'apply'}]}]
    nodes = index_nodes(result['root'])
    nodes['settings_apply']['control'].pop('action',None)
    nodes['settings_apply']['control']['event'] = 'apply'
    for binding in result['bindings']:
        owner, prop = binding['node'], binding['property']
        condition = {'settings_apply': apply, 'discard_apply_changes': apply_exit,
                     'discard_changes': dialog, 'discard_keep_editing': continuing}.get(owner)
        if condition is not None:
            if prop == 'enabled': binding['value'] = copy.deepcopy(condition)
            elif prop == 'opacity': binding['value'] = op('select',copy.deepcopy(condition),1,.4)
        elif owner == 'discard-panel' and prop == 'display':
            binding['value'] = op('select',dialog,'flex','none')
        elif owner == 'settings-message' and prop == 'text':
            binding['value'] = op('select',local_message,state('ui.numberDraftMessage'),state('settings.message'))

def compose(document):
    result = copy.deepcopy(document)
    nodes = index_nodes(result['root'])
    sliders = {key for key, item in nodes.items() if item.get('control', {}).get('role') == 'slider'}
    if sliders != set(PAIRS): raise ValueError('Review the numeric field contract when the authored slider inventory changes')
    for slider_id, (cvar, validation_key) in PAIRS.items():
        slider = copy.deepcopy(nodes[slider_id]); control = slider['control']
        if control['value'] != {'state': 'settings.draft.' + cvar}: raise ValueError('Slider lost its authoritative draft binding')
        action = result['actions'][control['action']]
        if action != {'input': 'number', 'operation': 'settings.system.edit', 'arguments': {cvar: {'input': 'value'}}}:
            raise ValueError('Slider action no longer proposes one typed draft value')
        number_id = slider_id + '_number'; row_id = slider_id + '_row'
        label = copy.deepcopy(nodes[slider_id + '-label'])
        label['properties']['width'] = length(100, '%')
        label['properties']['margin-bottom'] = length(8)
        plate = copy.deepcopy(nodes[slider_id + '-plate'])
        slider['children'] = [child for child in slider['children'] if child['id'] not in (label['id'], plate['id'])]
        slider['properties'].update({'width': length(160), 'min-width': length(128), 'height': length(36),
            'min-height': length(36), 'padding': length(7), 'margin-bottom': length(0),
            'flex-grow': value('number', 2), 'flex-shrink': value('number', 1)})
        for child in slider['children']:
            if child['id'] == control['parts']['value']: child['properties']['display'] = keyword('none')
            if child['id'] == control['parts']['track']: child['properties']['margin-top'] = length(0)
        # These IDs remain stable for edit/selection/proposal ownership. The
        # Number is a sibling, never an illegally nested semantic control.
        parts = {part: number_id + '-' + part for part in ('viewport', 'text', 'selection', 'caret', 'composition', 'validation')}
        field_plate = copy.deepcopy(plate); field_plate['id'] = number_id + '-plate'
        field_focus = copy.deepcopy(nodes[slider_id + '-focus']); field_focus['id'] = number_id + '-focus'
        text = node(parts['text'], 'text', base('absolute') | {
            'left': length(10), 'top': length(6), 'width': length(4096), 'height': length(23),
            'text': value('text', '#str_229982'), 'font-family': value('font', 'marine'),
            'font-size': length(16), 'line-height': length(23),
            'color': value('color', [1, 1, 1, .88]), 'white-space': keyword('pre'), 'text-align': keyword('left')})
        viewport = node(parts['viewport'], properties={'width': length(100, '%'), 'height': length(36),
            'overflow': keyword('hidden')}, children=[field_plate, field_focus,
            paint(parts['selection'], 0, 23, [.8902, .5373, 0, .4]), text,
            paint(parts['caret'], 1, 23, [1, 1, 1, 1]), paint(parts['composition'], 0, 1, [.8902, .5373, 0, 1])])
        validation = node(parts['validation'], 'text', {'width': length(100, '%'), 'margin-top': length(8),
            'text': value('text', validation_key), 'font-family': value('font', 'marine'),
            'font-size': length(14), 'line-height': length(20), 'white-space': keyword('normal'),
            'color': value('color', [.8941, .4275, .3373, 1])})
        number = node(number_id, properties={'width': length(112), 'min-width': length(96),
            'min-height': length(36), 'flex-grow': value('number', 1), 'flex-shrink': value('number', 1)}, children=[viewport, validation])
        number['control'] = {'role': 'number', 'label': control['label'], 'action': control['action'],
            'value': copy.deepcopy(control['value']), 'minimum': control['minimum'], 'maximum': control['maximum'],
            'exponent': True, 'maxBytes': 128, 'parts': parts,
            'states': {state: number_id + '.' + state for state in control['states']}}
        controls = node(row_id + '-controls', properties={'display': keyword('flex'), 'width': length(100, '%'),
            'flex-wrap': keyword('wrap'), 'column-gap': length(8), 'row-gap': length(8),
            'align-items': keyword('flex-start')}, children=[slider, number])
        row = node(row_id, properties={'width': length(100, '%'), 'padding': length(12),
            'margin-bottom': length(8)}, children=[plate, label, controls])
        def replace(current):
            for index, child in enumerate(current.get('children', [])):
                if child['id'] in (row_id, slider_id): current['children'][index] = row; return True
                if replace(child): return True
            return False
        if not replace(result['root']): raise ValueError('Slider row has no parent')
        result['bindings'] = [binding for binding in result['bindings'] if binding['id'] != number_id + '.enabled']
        enabled = copy.deepcopy(next(binding for binding in result['bindings'] if binding['id'] == slider_id + '.enabled'))
        enabled.update(id=number_id + '.enabled', node=number_id); result['bindings'].append(enabled)
        result['timelines'] = [timeline for timeline in result['timelines'] if not timeline['id'].startswith(number_id + '.')]
        for state, timeline_id in control['states'].items():
            timeline = copy.deepcopy(next(timeline for timeline in result['timelines'] if timeline['id'] == timeline_id))
            timeline['id'] = number_id + '.' + state
            for track in timeline['tracks']:
                if track['node'] != slider_id + '-focus' or track['property'] != 'opacity':
                    raise ValueError('Review shared slider focus artwork before regenerating Number fields')
                track['node'] = number_id + '-focus'
            result['timelines'].append(timeline)
    index_nodes(result['root'])
    draft_guards(result)
    result['extensions']['openq4']['numberFields'] = {
        'pairs': {slider: slider + '_number' for slider in PAIRS},
        'proposalPolicy': 'Explicit typed commit to the same settings draft; no slider-step quantization.',
        'scope': 'Authored production fields and local-draft presentation guards. Native text ownership, adapter dispatch and rendered locale fit require separate engine qualification.'}
    return result

def load(path):
    source = path.read_text(encoding='utf-8'); first = source.index('{')
    return source[:first], json.loads(source[first:])
def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument('--source', type=Path, default=ROOT / SOURCE)
    parser.add_argument('--check', action='store_true'); args = parser.parse_args()
    prefix, document = load(args.source); result = compose(document)
    if args.check:
        if result != document: raise SystemExit('SYSTEM numeric fields differ from their shared authored component definition')
    else: args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
if __name__ == '__main__': main()
