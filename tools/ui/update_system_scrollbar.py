#!/usr/bin/env python3
"""Author SYSTEM's editable Marine scrollbar and reserve its own body gutter.

The control consumes measured viewport geometry at runtime. No content height,
scroll offset, application action or raster furniture is embedded in this file.
"""
import argparse
import copy
import json
from pathlib import Path
from update_system_presets import ROOT, SOURCE, keyword, length, load, nodes, number, typed


def edge(dp=0):
    return {'fraction': 1, 'dp': dp}


def polygon(ident, points, colour, stroke=None):
    commands = [{'id': 'point-' + str(i), 'op': 'move' if i == 0 else 'line', 'points': [point]}
                for i, point in enumerate(points)] + [{'id': 'close', 'op': 'close'}]
    result = {'id': ident, 'commands': commands,
              'fill': {'type': 'solid', 'color': typed('color', colour)}}
    if stroke:
        result['stroke'] = {'paint': {'type': 'solid', 'color': typed('color', stroke)},
                            'widthDp': 1, 'minimumPixels': 1, 'join': 'miter'}
    return result


def ink(ident, points, colour, stroke=None, opacity=1):
    return {'id': ident, 'type': 'vector', 'properties': {
        'position': keyword('absolute'), 'display': keyword('block'), 'box-sizing': keyword('border-box'),
        'left': length(0), 'top': length(0), 'width': length(100, '%'), 'height': length(100, '%'),
        'pointer-events': keyword('none'), 'opacity': number(opacity)},
        'paths': [polygon('outline', points, colour, stroke)]}


def compose(document):
    result = copy.deepcopy(document)
    index = nodes(result['root'])
    body = index['settings-body']
    panel = index['settings-panel']
    # The wrapper retains the original body's vertical allocation. Its fixed
    # gutter cannot cover content, become part of scroll overflow or move rails.
    body['properties'].update({'width': length(0), 'height': length(100, '%'),
        'min-width': length(0), 'margin-top': length(0), 'margin-bottom': length(0)})
    ident = 'settings_body_scrollbar'
    olive = [0.5451, 0.5882, 0.2941, 1]
    orange = [0.8902, 0.5373, 0, 1]
    thumb_shape = [[14, 0], [26, 0], [26, edge(-4)], [22, edge()], [10, edge()], [10, 4]]
    thumb = {'id': ident + '-thumb', 'type': 'group', 'properties': {
        'position': keyword('absolute'), 'display': keyword('block'), 'box-sizing': keyword('border-box'),
        'left': length(0), 'top': length(0), 'width': length(36), 'height': length(36), 'opacity': number(1)},
        'children': [ink(ident + '-thumb-base', thumb_shape, [0.32, 0.36, 0.16, 0.95], olive),
                     ink(ident + '-thumb-active', thumb_shape, orange, orange, 0)]}
    track = {'id': ident + '-track', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('block'), 'box-sizing': keyword('border-box'),
        'width': length(36), 'height': length(100, '%'), 'opacity': number(1)},
        'children': [ink(ident + '-trough', [[16, 0], [22, 0], [22, edge()], [14, edge()], [14, 2]],
                         [0.0353, 0.0471, 0.0314, 0.95], [0.5451, 0.5882, 0.2941, 0.4]), thumb]}
    bar = {'id': ident, 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('block'), 'box-sizing': keyword('border-box'),
        'width': length(36), 'min-width': length(36), 'height': length(100, '%'),
        'flex-grow': number(0), 'flex-shrink': number(0), 'opacity': number(1)},
        'control': {'role': 'scrollbar', 'label': '#str_200085', 'viewport': 'settings-body',
            'orientation': 'vertical', 'lineStep': 36, 'minimumThumb': 36,
            'parts': {'track': ident + '-track', 'thumb': ident + '-thumb'},
            'states': {state: ident + '.' + state for state in ('default', 'hover', 'focus', 'pressed', 'disabled')}},
        'children': [track]}
    wrapper = {'id': 'settings-scroll-region', 'type': 'group', 'properties': {
        'position': keyword('relative'), 'display': keyword('flex'), 'box-sizing': keyword('border-box'),
        'width': length(100, '%'), 'flex-grow': number(1), 'flex-shrink': number(1),
        'min-height': length(0), 'column-gap': length(8), 'margin-top': length(8), 'margin-bottom': length(8),
        'align-items': keyword('stretch'), 'opacity': number(1)}, 'children': [body, bar]}
    for i, child in enumerate(panel['children']):
        if child['id'] in ('settings-body', 'settings-scroll-region'):
            panel['children'][i] = wrapper
            break
    else:
        raise ValueError('SYSTEM body must remain inside its panel')
    tail_prefixes = ('settings_preset.', 'settings_autodetect.',
                     'settings_brightness_number.', 'settings_ambient_number.')
    tail = [t for t in result['timelines'] if t['id'].startswith(tail_prefixes)]
    result['timelines'] = [t for t in result['timelines']
                           if not t['id'].startswith((ident + '.',) + tail_prefixes)]
    for state, duration, opacity in [('default', 300, 0), ('hover', 60, 0.55), ('focus', 80, 0.85),
                                     ('pressed', 60, 1), ('disabled', 80, 0)]:
        result['timelines'].append({'id': ident + '.' + state, 'durationMs': duration, 'iterations': 1,
            'tracks': [{'node': ident + part, 'property': 'opacity', 'keys': [
                {'atMs': 0, 'value': number(alpha)}, {'atMs': duration, 'value': number(alpha)}]}
                for part, alpha in [('-thumb-active', opacity), ('-thumb-base', 0.4 if state == 'disabled' else 1)]]})
    result['timelines'] += tail
    nodes(result['root'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    prefix, document = load(args.source)
    result = compose(document)
    if args.check:
        if result != document:
            raise SystemExit('SYSTEM scrollbar differs from authored vector composition')
    else:
        args.source.write_text(prefix + json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
