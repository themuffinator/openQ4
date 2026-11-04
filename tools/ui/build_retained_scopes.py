#!/usr/bin/env python3
"""Build the three native vector scopes from visual specification 14.13.

The authored paths are shared with the openQ4 design atlas, not retail bitmap
assets. The 4:3 optical center fits either dimension; edge haze fills the view.
"""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path

import build_retained_screens as ui

U = ui.U
PATHS = json.loads(Path(__file__).with_name('scope_paths.json').read_text(encoding='utf-8'))['paths']


def circle(radius, cx=320, cy=240):
    """Four cubics, flattened by the renderer at the actual output density."""
    k = radius * 0.552284749831
    return [
        {'id': 'start', 'op': 'move', 'points': [[U * (cx + radius), U * cy]]},
        *[{'id': f'arc-{i}', 'op': 'cubic', 'points': [[U*x, U*y] for x, y in points]}
          for i, points in enumerate([
              [(cx+radius, cy+k), (cx+k, cy+radius), (cx, cy+radius)],
              [(cx-k, cy+radius), (cx-radius, cy+k), (cx-radius, cy)],
              [(cx-radius, cy-k), (cx-k, cy-radius), (cx, cy-radius)],
              [(cx+k, cy-radius), (cx+radius, cy-k), (cx+radius, cy)]])],
        {'id': 'close', 'op': 'close'},
    ]


def compound(ident, contours, tint, blend=None):
    commands = []
    for contour in contours:
        for cmd in contour:
            commands.append({**cmd, 'id': f'p{len(commands)}'})
    result = {'id': ident, 'commands': commands, 'fillRule': 'evenodd', 'fill': ui.solid(tint)}
    if blend:
        result['blend'] = blend
    return result


def measured(ident, tint, blend=None):
    # The atlas uses source-unit SVG moves, lines and Bezier curves. Preserve
    # curves as curves: pixel-staircase outlines do not become smooth merely
    # because the renderer antialiases each of their tiny straight segments.
    tokens = re.findall(r'[MLCQZ]|-?\d+(?:\.\d+)?', PATHS[ident])
    commands, index, operation = [], 0, None
    operations = {'M': ('move', 1), 'L': ('line', 1), 'C': ('cubic', 3), 'Q': ('quadratic', 2)}
    while index < len(tokens):
        token = tokens[index]
        if token in operations:
            operation = token
            index += 1
            continue
        if token == 'Z':
            commands.append({'id': f'p{len(commands)}', 'op': 'close'})
            index += 1
            continue
        op, count = operations[operation]
        points = [[U*float(tokens[index+2*i]), U*float(tokens[index+2*i+1])] for i in range(count)]
        commands.append({'id': f'p{len(commands)}', 'op': op, 'points': points})
        if operation == 'M':
            operation = 'L'
        index += 2*count
    return compound(ident, [commands], tint, blend)


def diamond(radius):
    return ui.path('d', [(U*320, U*(240-radius)), (U*(320+radius), U*240),
                         (U*320, U*(240+radius)), (U*(320-radius), U*240)])['commands']


def ring(ident, radius, width, tint, blend=None, cx=320, cy=240):
    return compound(ident, [circle(radius+width/2, cx, cy), circle(radius-width/2, cx, cy)], tint, blend)


def radial(ident, stops, color, blend=None, shape=circle):
    """One editable gradient, with continuous coverage across its colour stops."""
    first, last = stops[0][0], stops[-1][0]
    path = compound(ident, [shape(last), shape(first)] if first else [shape(last)], ui.rgb(color), blend)
    colors = [(r/last, ui.rgb(color, alpha)) for r, alpha in stops]
    if first:
        colors.insert(0, (0, colors[0][1]))
    path['fill'] = ui.linear((U*320,U*240),(U*(320+last),U*240),colors)
    path['fill']['type'] = 'diamond' if shape == diamond else 'radial'
    return [path]


def center(ident, children, **props):
    return ui.group(ident, {**ui.absolute(width=960, height=720), 'left': ui.length(50, '%'),
                           'top': ui.length(50, '%'), 'margin-left': ui.length(-480), 'margin-top': ui.length(-360), **props}, children)


def art(ident, paths, **props):
    return ui.vector(ident, {**ui.FULL, **props}, paths)


def moving(paths):
    """Faint moving furniture uses retained local triangles, not pixel cells.

    Exact output-pixel coverage recompiles at every yaw/scale/subpixel change.
    These low-contrast fills use the runtime's transformable mesh path; keep
    the stationary reticle, aperture and focus masks analytically antialiased.
    """
    return [{**path, 'antialias': False} for path in paths]


def surround(ident, aperture, tint):
    """Resolve the center shape in the full viewport, including portrait bands."""
    aperture = json.loads(json.dumps(aperture))
    for cmd in aperture:
        for point in cmd.get('points', []):
            point[0] = {'fraction': .5, 'dp': round(point[0]-480, 4)}
            point[1] = {'fraction': .5, 'dp': round(point[1]-360, 4)}
    full = ui.path('full', [(0, 0), ({'fraction': 1}, 0), ({'fraction': 1}, {'fraction': 1}), (0, {'fraction': 1})])['commands']
    return art(ident, [compound(ident, [full, aperture], tint)])


def rotate_binding(doc, ident, source):
    doc.bind(f'{ident}.transform', ident, 'transform', [0, 0, 1, 1, {'state': source}])


def lens_focus(radius, shape=circle):
    """Two-pass Gaussian scene focus, feathered outside the unaltered sight.

    Work in isotropic optical units, then resolve the mask against the full
    viewport so portrait and ultrawide bands receive the same focus treatment.
    """
    edge = radial('focus-feather', [(radius, 0), (radius+8, .08),
                  (radius+19, .5), (radius+30, .92), (radius+38, 1)], '#FFFFFF', shape=shape)[0]
    def point(value):
        return [{'fraction': .5, 'dp': round(value[0]-480, 4)},
                {'fraction': .5, 'dp': round(value[1]-360, 4)}]
    # The paint itself supplies the aperture and clamps to opaque outside.
    # A full-view rectangle avoids tessellating two redundant circular edges
    # at the transparent and opaque ends of the feather, especially at 4K.
    edge['commands'] = ui.path('full', [(0, 0), ({'fraction': 1}, 0),
        ({'fraction': 1}, {'fraction': 1}), (0, {'fraction': 1})])['commands']
    for name in ('from', 'to'):
        edge['fill'][name] = point(edge['fill'][name])
    return ui.group('lens-softfocus', {**ui.FULL, 'backdrop-blur': ui.length(U*9),
                    'backdrop-saturate': ui.number(.86)}, mask={'paths':[edge]})


def machinegun(doc):
    marks = radial('lens', [(0, 0), (60, 0), (96, .058), (128, .11), (152, .165), (168, .208), (176.3, .32)], '#000000')
    marks += [ring('inner-ring', 177.7, 2.9, [0, 0, 0, 1]), ring('outer-ring', 183.1, 2.2, [0, 0, 0, 1]), measured('mg_marks', [0, 0, 0, 1])]
    glow = radial('glow', [(84, 0), (96, .05), (120, .2), (144, .49), (160, .75), (174, 1), (178, 1)], '#1B270E', 'additive')
    dots = []
    for i in range(16):
        a = i*math.tau/16
        points = [(U*(320+x*math.cos(a)-y*math.sin(a)), U*(240+x*math.sin(a)+y*math.cos(a)))
                  for x, y in ((169.3, -4.6), (178.5, -4.6), (178.5, 4.6), (169.3, 4.6))]
        dots.append(ui.path(f'dot-{i}', points, fill=ui.solid([0, 0, 0, .34])))
    rotate_binding(doc, 'compass', 'playerYaw')
    return [lens_focus(184),
            surround('veil', circle(182), ui.rgb('#363636', .4)),
            center('optical-center', [art('glow', glow), art('compass', moving(dots), transform=ui.rotation(0)), art('marks', marks)])]


def railgun(doc):
    doc.state['player_ammo'] = {'type': 'number', 'initial': 3}
    marks = [compound('aperture-band', [diamond(352), measured('rail_ap', [0, 0, 0, 1])['commands']], ui.rgb('#101010', .26)),
             compound('edge', [diamond(366), diamond(362)], [0, 0, 0, .87]), measured('rail_lines', [0, 0, 0, .87])]
    # The graduated diamond light follows Manhattan distance, preserving 45 degrees.
    glow = radial('glow', [(200, 0), (240, .13), (270, .34), (300, .68), (330, .97), (360, 1)], '#082828', 'additive', diamond)
    haze = radial('haze', [(370, .25), (407, .31), (444, .41), (482, .47), (519, .5), (556, .51)], '#838383', shape=diamond)
    bars = []
    for x in (55, 514):
        for y, flip in ((229, False), (244, True)):
            for offset, height, alpha in ((.44 if flip else 1.31, 5.25, .19), (5.69 if flip else .44, .87, .5)):
                bars.append(ui.path(f'bar-{len(bars)}', [(U*x, U*(y+offset)), (U*(x+72), U*(y+offset)),
                                                       (U*(x+72), U*(y+offset+height)), (U*x, U*(y+offset+height))], fill=ui.solid(ui.rgb('#00FFFF', alpha))))
    cells = []
    for i, (x, y) in enumerate(((398, 14), (420, 36), (442, 59))):
        def cell_rect(left, width, alpha):
            pts = [(U*(x+left+dy-9), U*(y+dy)) for left, dy in ((left, 2.25), (left+width, 2.25), (left+width, 15.75), (left, 15.75))]
            return ui.path(f'cell-{left}', pts, fill=ui.solid(ui.rgb('#99FFFF', alpha)))
        ident = f'clip-{i}'
        cells.append(art(ident, [cell_rect(4.75, 30.9, .42), cell_rect(2.38, 2.37, 1)], display=ui.keyword('block')))
        doc.bind(ident+'.display', ident, 'display', {'op': 'select', 'args': [{'op': '>', 'args': [{'state': 'player_ammo'}, 2-i]}, 'block', 'none']})
    ringpaths = []
    for index, (start, finish, alpha) in enumerate(((185, 355, .22), (5, 175, .11))):
        points = [(U*(14.4+radius*math.cos(math.radians(a))), U*(14.4+radius*math.sin(math.radians(a))))
                  for radius, angles in ((14.4, range(start, finish+1, 5)), (4.4, range(finish, start-1, -5))) for a in angles]
        ringpaths.append(ui.path(f'half-{index}', points, fill=ui.solid([0, 0, 0, alpha])))
    turning = ui.vector('turning-ring', {**ui.absolute(left=U*(85-14.4), top=U*(315-14.4), width=U*28.8, height=U*28.8), 'transform': ui.rotation(0)}, moving(ringpaths))
    doc.timelines.add('ring-turn', 9000, [ui.track('turning-ring', 'transform', [(0, ui.rotation(0)), (9000, ui.rotation(360))])], iterations=0)
    # The sweep stays centered; an essential timeline preserves stock optical motion.
    sweep = art('sweep', moving([compound('sweep', [diamond(222), diamond(218)], ui.rgb('#00FFFF', .08), 'additive')]), transform=ui.transform(), opacity=ui.number(0))
    doc.timelines.add('diamond-sweep', 2000, [ui.track('sweep', 'transform', [(0, ui.transform()), (1000, ui.transform(sx=680/440, sy=680/440)), (2000, ui.transform(sx=680/440, sy=680/440))]),
                                            ui.track('sweep', 'opacity', [(0, ui.number(0)), (500, ui.number(1)), (1000, ui.number(0)), (2000, ui.number(0))])], iterations=0)
    doc.events['onInit'] = [{'op': 'playTimeline', 'timeline': name} for name in ('ring-turn', 'diamond-sweep')]
    return [surround('outer-haze', diamond(556), ui.rgb('#838383', .51)), center('optical-center', [art('glow', glow), art('haze', haze), art('marks', marks+bars), turning, *cells, sweep])]


def nailgun(doc):
    doc.state.update({'lockStatus': {'type': 'number', 'initial': 0}, 'lockYaw': {'type': 'number', 'initial': 0},
                      'lockRange': {'type': 'string', 'initial': ''}, 'lockTime': {'type': 'string', 'initial': ''},
                      'locked': {'type': 'boolean', 'initial': False}})
    stops = [(144, 0), (168, .07), (192, .255), (216, .57), (240, .97), (252, 1), (264, .69), (276, 0)]
    # One graded light for the complete acquisition ramp. The resting tint
    # differs from the measured #241700 by at most one 8-bit channel value;
    # the locked endpoint is exact. Avoid drawing two identical radial meshes.
    glow = art('glow', radial('glow', stops, '#533200', 'additive'), opacity=ui.number(.44))
    doc.bind('glow.opacity', 'glow', 'opacity', {'op': '+', 'args': [.44,
        {'op': '*', 'args': [.56, {'op': 'clamp', 'args': [{'state': 'lockStatus'}, 0, 1]}]}]})
    marks = [measured('nail_hook', [0, 0, 0, .5]), ring('band1', 263, 15, ui.rgb('#020202', .5)),
             ring('edge', 272, 2.4, [0, 0, 0, 1]), ring('band2', 282.5, 18, ui.rgb('#020202', .71)),
             ring('hairline', 218.1, 2.2, ui.rgb('#3C2500'), 'additive')]
    haze = radial('haze', [(291.5, .08), (310, .24), (332, .4), (356, .5)], '#828282')
    compass = art('compass', moving([measured('nail_ring2', [0, 0, 0, .34])]), transform=ui.rotation(0))
    rotate_binding(doc, 'compass', 'playerYaw')
    arrow = art('lock-arrow', moving([ui.path('arrow', [(U*x, U*y) for x,y in ((532.6,188),(555.3,240),(532.6,292),(539.6,240))], fill=ui.solid(ui.rgb('#623A00')), blend='additive')]), transform=ui.rotation(0), opacity=ui.number(0))
    rotate_binding(doc, 'lock-arrow', 'lockYaw')
    doc.bind('lock-arrow.opacity', 'lock-arrow', 'opacity', {'op':'clamp', 'args':[{'state':'lockStatus'},0,1]})
    lines = []
    tracks_in, tracks_out = [], []
    for i, (x, y, end) in enumerate(((51,237,224),(51,241,254),(486,237,224),(486,241,254))):
        ident = f'lock-line-{i}'
        paint = ui.linear((0,0),(U*103,0),[(0,ui.rgb('#5E3800', .1 if x>320 else 1)),(1,ui.rgb('#5E3800', 1 if x>320 else .1))])
        lines.append(ui.vector(ident, {**ui.absolute(left=U*x,top=U*(y+.5),width=U*103,height=U), 'transform':ui.transform()},
                               moving([ui.path('line',[(0,0),(U*103,0),(U*103,U),(0,U)],fill=paint,blend='additive')])))
        tracks_in.append(ui.track(ident,'transform',[(0,ui.transform()),(50,ui.transform(ty=U*(end-y)))]))
        tracks_out.append(ui.track(ident,'transform',[(0,ui.transform(ty=U*(end-y))),(300,ui.transform())]))
    readouts = ui.group('readouts', {**ui.FULL, 'opacity':ui.number(0)}, [
        ui.label('locked-label','#str_200980',{**ui.absolute(left=U*52,top=U*226,width=U*150,height=U*30),**ui.typeface('r_strogg',U*19.2,U*24,ui.rgb('#EE8E00'))}),
        ui.label('range','#str_230030',{**ui.absolute(left=U*400,top=U*229,width=U*134,height=U*25),**ui.typeface('r_strogg',U*11.5,U*20,ui.rgb('#EE8E00')),'text-align':ui.keyword('right')}),
        ui.label('countdown','#str_230030',{**ui.absolute(left=U*541,top=U*226,width=U*55,height=U*30),**ui.typeface('r_strogg',U*19.2,U*24,ui.rgb('#EE8E00'))}),
    ])
    doc.bind('range.text','range','text',{'state':'lockRange'})
    doc.bind('countdown.text','countdown','text',{'state':'lockTime'})
    tracks_in.append(ui.track('readouts','opacity',[(0,ui.number(0)),(50,ui.number(0)),(100,ui.number(1))]))
    tracks_out.append(ui.track('readouts','opacity',[(0,ui.number(0)),(300,ui.number(0))]))
    # All tracks must end within their timeline; line tracks hold after 50 ms.
    doc.timelines.add('lock',100,tracks_in,essential=True)
    doc.timelines.add('unlock',300,tracks_out,essential=True)
    doc.events['lockAquired'] = [{'op':'setState','values':{'locked':True}},{'op':'playTimeline','timeline':'lock'}]
    doc.events['lockStop'] = [{'op':'setState','values':{'locked':False,'lockStatus':0}},{'op':'playTimeline','timeline':'unlock'}]
    doc.events['lockStart'] = []
    doc.events['zoomIn'] = [{'op':'call','event':'lockStop'}]
    return [surround('outer-haze',circle(356),ui.rgb('#828282',.5)), center('optical-center',[glow,compass,art('haze',haze),art('marks',marks),arrow,*lines,readouts])]


def document(kind):
    doc = ui.Document('openq4.scope.'+kind)
    doc.state['playerYaw'] = {'type':'number','initial':0}
    doc.events['zoomIn'] = []
    layers = globals()[kind](doc)
    if kind != 'machinegun':
        layers.insert(0, lens_focus(362, diamond) if kind == 'railgun' else lens_focus(255))
    root = ui.group('screen',{**ui.FULL,'pointer-events':ui.keyword('none')}, layers)
    result = doc.build(root)
    result['canvas']['minWidth'] = 960
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    for kind in ('machinegun','railgun','nailgun'):
        path = ui.PAK0/'guis/weapons'/f'{kind}_scope.q4ui'
        contents = ui.render(document(kind)).replace('build_retained_screens.py','build_retained_scopes.py')
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8') != contents.replace('\r\n','\n'):
                raise SystemExit(f'stale generated scope: {path}')
        else:
            path.write_bytes(contents.encode('utf-8'))
            print(path.relative_to(ui.ROOT))


if __name__ == '__main__':
    main()
