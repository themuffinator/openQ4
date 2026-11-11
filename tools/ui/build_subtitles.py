#!/usr/bin/env python3
"""Generate the two-line accessible subtitle overlay (visual design 14.8)."""
import argparse
import build_retained_screens as ui


def document():
    doc = ui.Document('openq4.subtitles')
    for name, initial in {'x': 120, 'width': 1040, 'size': 17, 'pitch': 24,
                          'lines': 1, 'background': .6, 'indent0': 0, 'indent1': 0, 'bottom': 570,
                          'aboveLines': 0, 'aboveBottom': 0, 'aboveIndent': 0}.items():
        doc.state[name] = {'type': 'number', 'initial': initial}
    for name in ('line0', 'line1', 'speaker0', 'speaker1', 'aboveLine', 'aboveSpeaker'):
        doc.state[name] = {'type': 'string', 'initial': ''}
    def state(name): return {'state': name}
    def op(name, *args): return {'op': name, 'args': list(args)}

    def plate(name):
        node = ui.vector(name, {**ui.FULL, 'opacity': ui.number(1)}, [ui.path('plate', [
            [0, 0], [{'fraction': 1, 'dp': 0}, 0],
            [{'fraction': 1, 'dp': 0}, {'fraction': 1, 'dp': 0}],
            [6, {'fraction': 1, 'dp': 0}], [0, {'fraction': 1, 'dp': -6}]
        ], fill=ui.solid([0, 0, 0, 1]))])
        doc.bind(name+'.opacity', name, 'opacity', state('background'))
        return node

    def label(name, role, row, text, indent):
        tint = '#B0C891' if role == 'speaker' else '#FFFFFF'
        node = ui.label(name, '#str_230007', {
            **ui.absolute(left=12, top=6, width=1016, height=24),
            **ui.typeface('lowpixel', 17, 24, ui.rgb(tint)),
            'white-space': ui.keyword('nowrap'), 'overflow': ui.keyword('hidden'),
            'display': ui.keyword('block')})
        doc.bind(name+'.text', name, 'text', state(text))
        doc.bind(name+'.size', name, 'font-size', state('size'))
        doc.bind(name+'.line-height', name, 'line-height', op('*', state('size'), 1.4))
        doc.bind(name+'.top', name, 'top', op('+', 6, op('*', row, state('pitch'))))
        doc.bind(name+'.height', name, 'height', state('pitch'))
        doc.bind(name+'.left', name, 'left', op('+', 12, state(indent)) if role == 'line' else 12)
        doc.bind(name+'.width', name, 'width', op('-', op('-', state('width'), 24), state(indent)) if role == 'line' else state(indent))
        return node

    height = op('+', 12, op('*', state('pitch'), state('lines')))
    doc.bind('panel.x', 'panel', 'left', state('x'))
    doc.bind('panel.y', 'panel', 'top', op('-', state('bottom'), height))
    doc.bind('panel.width', 'panel', 'width', state('width'))
    doc.bind('panel.height', 'panel', 'height', height)
    children = [plate('back')]
    for row in range(2):
        for role in ('speaker', 'line'):
            name = f'{role}{row}'
            node = label(name, role, row, name, f'indent{row}')
            if row:
                doc.bind(name+'.display', name, 'display', op('select', op('>', state('lines'), 1), 'block', 'none'))
            children.append(node)
    panel = ui.group('panel', ui.absolute(left=120, top=534, width=1040, height=36), children)
    # A second speaker's one-line caption above a portrait cinematic's artwork, used when
    # only one line fits below it; hidden otherwise.
    above_height = op('+', 12, state('pitch'))
    doc.bind('abovePanel.x', 'abovePanel', 'left', state('x'))
    doc.bind('abovePanel.y', 'abovePanel', 'top', op('-', state('aboveBottom'), above_height))
    doc.bind('abovePanel.width', 'abovePanel', 'width', state('width'))
    doc.bind('abovePanel.height', 'abovePanel', 'height', above_height)
    doc.bind('abovePanel.display', 'abovePanel', 'display', op('select', op('>', state('aboveLines'), 0), 'block', 'none'))
    above = ui.group('abovePanel', {**ui.absolute(left=120, top=0, width=1040, height=36), 'display': ui.keyword('none')},
                     [plate('aboveBack'), label('aboveSpeaker', 'speaker', 0, 'aboveSpeaker', 'aboveIndent'),
                      label('aboveLine', 'line', 0, 'aboveLine', 'aboveIndent')])
    result = doc.build(ui.group('screen', {**ui.FULL, 'pointer-events': ui.keyword('none')}, [above, panel]))
    result['canvas'] = {'height': 720}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    target = ui.PAK0 / 'guis/hud/subtitles.rui'
    text = ui.render(document()).replace('build_retained_screens.py', 'build_subtitles.py')
    if args.check:
        if not target.exists() or target.read_text(encoding='utf-8') != text.replace('\r\n', '\n'):
            raise SystemExit('stale subtitle document')
    else:
        target.write_bytes(text.encode('utf-8'))
        print(target.relative_to(ui.ROOT))


if __name__ == '__main__':
    main()
