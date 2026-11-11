#!/usr/bin/env python3
"""Generate the live Marine/Strogg status gauges (visual specification 14.1–14.4).

The editable paths are first-party reconstructions from the project's atlas.
No retail textures are embedded. Values and visibility belong to the player.
"""
import argparse
import json
import math
from pathlib import Path
import xml.etree.ElementTree as ET
import build_retained_screens as ui
import build_retained_weapon_wheel as wheel

ROOT = Path(__file__).resolve().parents[2]
ICONS = json.loads(Path(__file__).with_name('status_bar_icons.json').read_text())['icons']
U = 1.5
S = lambda key: {'state': key}
op = lambda name, *args: {'op': name, 'args': list(args)}
choose = lambda condition, yes, no: op('select', condition, yes, no)
BACK = 'M1 10.4Q1 9.2 2.2 9.2H122.8Q124 9.2 124 10.4V54.7Q124 56.2 122.5 56.2H91.7L84.9 49.3H2.2Q1 49.3 1 48.1Z'
FILL = 'M5.4 14.8Q5.4 13.8 6.4 13.8H118.6Q119.6 13.8 119.6 14.8V34.6H105.4A16.6 16.6 0 0 0 93.6 44.7H6.4Q5.4 44.7 5.4 43.7Z'
ST_BACK = 'M1 22.5L26.8 7.3H69L72 11H128V53.4H1Z'
ST_FILL = 'M46.7 15.7H124V40.3H69.3L56.3 48.7H5.1V39.8L15.8 33L24.2 32.5L33.4 26.8L33.9 24.1Z'


def shape(ident, data, tint, stroke=None, blend=None):
    commands = wheel.svg_commands(data)
    for command in commands:
        if 'points' in command:
            command['points'] = [[U*x, U*y] for x, y in command['points']]
    result = {'id': ident, 'commands': commands}
    if stroke:
        result['stroke'] = ui.stroke(ui.solid(tint), U*stroke, minimum=0, join='round', cap='round')
    else:
        result['fill'] = ui.solid(tint)
    if blend:
        result['blend'] = blend
    return result


def art(ident, paths, **props):
    return ui.vector(ident, {**ui.FULL, 'display':ui.keyword('block'), **props}, paths)


def shear(ident, width, height, k, children):
    """Exact shear from two centered rotations and an anisotropic scale.

    This uses the canonical transform model, including its transformed clips,
    instead of shearing just the paths and leaving the digits/clip unsheared.
    """
    theta = .5*math.atan2(2*k, k*k)
    scale = math.sqrt(1+k*k/4)+abs(k)/2
    phi = math.atan2(-math.sin(theta)*scale, math.cos(theta)/scale)
    box = ui.absolute(left=0, top=0, width=width, height=height)
    outer = ui.transform(); outer['value'][4] = math.degrees(theta)
    inner = ui.transform(); inner['value'][4] = math.degrees(phi)
    # Canonical CSS applies scale before rotation in the same value. Keep the
    # singular-value scale in its own node so this composes as R * S * R.
    return ui.group(ident, {**box, 'transform': outer}, [ui.group(ident+'-scale',
        {**box,'transform':ui.transform(sx=scale,sy=1/scale)},
        [ui.group(ident+'-inner', {**box, 'transform': inner}, children)])])


def symbol(ident, name, x, y, height, tint):
    if name in ('health', 'armor'):
        data = ('M12 1.5A10.5 10.5 0 1 1 12 22.5A10.5 10.5 0 1 1 12 1.5Z'
                'M9.8 6H14.2V9.8H18V14.2H14.2V18H9.8V14.2H6V9.8H9.8Z') if name=='health' else 'M4 3H20V11C20 17 16 20 12 21.5C8 20 4 17 4 11Z'
        w=h=24
        paths=[{'id':'symbol','commands':wheel.svg_commands(data),'fillRule':'evenodd','fill':ui.solid(tint)}]
    elif name in ICONS:
        data = ICONS[name]; _, _, w, h = map(float, data['vb'].split())
        paths = []
        for element in ET.fromstring('<svg>'+data['svg']+'</svg>').iter('path'):
            color = tint[:3]+[tint[3]*float(element.get('fill-opacity', '1'))]
            paths.append({'id': 'p'+str(len(paths)), 'commands': wheel.svg_commands(element.attrib['d']),
                          'fillRule': element.get('fill-rule', 'nonzero'), 'fill': ui.solid(color)})
    else:
        paths, w, h = wheel.icon(name, tint)
    for path in paths:
        for command in path['commands']:
            if 'points' in command:
                command['points'] = [[a*height/h, b*height/h] for a, b in command['points']]
    return ui.vector(ident, {**ui.absolute(left=x, top=y, width=w*height/h, height=height), 'opacity': ui.number(1), 'display':ui.keyword('block')}, paths)


def readout(doc, ident, key, x, y, width, size, face, color, digits=4):
    """Trailing aligned tabular positions using the original family glyphs."""
    nodes = []
    pitch = size*.58
    for digit in range(digits):
        node = ui.label(f'{ident}-{digit}', '#str_230007', {
            **ui.absolute(left=width-(digits-digit)*pitch, top=0, width=pitch, height=size*1.3),
            **ui.typeface(face, size, size*1.1, color), 'text-align': ui.keyword('center')})
        nodes.append(node)
        doc.state[f'{key}.{digit}'] = {'type': 'string', 'initial': ''}
        doc.bind(f'{ident}-{digit}.text', node['id'], 'text', S(f'{key}.{digit}'))
        doc.bind(f'{ident}-{digit}.color', node['id'], 'color', [op('+', c, op('*', 1-c, S(key+'.gain'))) for c in color[:3]]+[1])
    group = ui.group(ident, {**ui.absolute(left=x, top=y, width=width, height=size*1.3), 'transform': ui.transform(), 'display':ui.keyword('block')}, nodes)
    grow = op('+', 1, op('*', .2, S(key+'.gain')))
    doc.bind(ident+'.gain', ident, 'transform', [op('*', op('-', 1, grow), width/2), 0, grow, grow, 0])
    return group


def document(strogg=False):
    family = 'strogg' if strogg else 'marine'
    doc = ui.Document('openq4.status-bar.'+family)
    for key, kind, initial in [('scale','number',1), ('inset','number',0), ('mp','boolean',False),
            ('weapon','number',0), ('clip','boolean',False), ('gun','boolean',True),
            ('low','boolean',False), ('pulse','number',1), ('phase','number',0)]:
        doc.state[key] = {'type':kind, 'initial':initial}
    doc.state['backing'] = {'type':'boolean', 'initial':False, 'cvar':'ui_retainedOpaqueBacking'}
    width, height = ((129, 67) if strogg else (125, 59))
    positions = (0,180,320) if strogg else (0,177,313)
    group_width = positions[-1]+width
    gauges = []
    face = 'r_strogg' if strogg else 'chain'
    ink = ui.rgb('#FCFFC8' if strogg else '#DDEBC3')
    accent = ui.rgb('#FFCC00' if strogg else '#A8A360', .6)
    for kind, x in zip(('ammo','health','armor'), positions):
        for suffix, dtype, initial in [('fill','number',1), ('visible','boolean',True), ('gain','number',0)]:
            doc.state[kind+'.'+suffix] = {'type':dtype, 'initial':initial}
        paths = (ST_BACK, ST_FILL) if strogg else (BACK, FILL)
        base = '#FF9000' if strogg else '#B3D06E'
        backing = art(kind+'-back', [shape('back',paths[0],[0,0,0,1])], opacity=ui.number(.5))
        doc.bind(kind+'.backing',backing['id'],'opacity',choose(S('backing'),.85,.5))
        children = [backing]
        for layer, alpha in [('track',.2), ('fill',.35)]:
            variants=[('normal',base,op('!',S('mp')) if kind=='ammo' else op('!',S('low')) if kind=='health' else True)]
            if kind=='health': variants.append(('low','#FF4C00',S('low')))
            if kind=='ammo':
                colors=wheel.COLORS[:10]+['#FF5AC8','#FFBF40','#9EEBFF','#FFBF40','#00D9FF']
                variants += [('mp'+str(i),color,op('&&',S('mp'),op('==',S('weapon'),i))) for i,color in enumerate(colors)]
            layer_nodes=[]
            for suffix,color,shown in variants:
                paint=[shape('plate',paths[1],ui.rgb(color))]
                if layer=='fill': paint += [shape('bevel',paths[1],ui.rgb(color,.45),stroke=1.5)]
                node=art(f'{kind}-{layer}-{suffix}',paint,opacity=ui.number(alpha))
                doc.bind(node['id']+'.show',node['id'],'display',choose(shown,'block','none'))
                doc.bind(node['id']+'.alpha',node['id'],'opacity',choose(S('mp'),.5,.35) if layer=='fill' else .2)
                layer_nodes.append(node)
            if layer=='fill':
                left = op('-', U*(124 if strogg else 120), op('*', U*(119 if strogg else 115),S(kind+'.fill')))
                moving = ui.group(kind+'-fill-content', {**ui.absolute(left=0,top=0,width=U*width,height=U*height)}, layer_nodes)
                doc.bind(kind+'.fill-offset',moving['id'],'left',op('-',0,left))
                fill = ui.group(kind+'-fill', {**ui.absolute(left=0,top=0,width=U*width,height=U*height),'overflow':ui.keyword('hidden')},[moving])
                doc.bind(kind+'.fill-left',fill['id'],'left',left)
                doc.bind(kind+'.fill-width',fill['id'],'width',op('*',U*(119 if strogg else 115),S(kind+'.fill')))
                children.append(fill)
            else: children += layer_nodes
        if kind=='armor':
            dividers = [ui.path('cell'+str(i),[(U*v,U*13.8),(U*v,U*48)],stroke=ui.stroke(ui.solid([0,0,0,.28]),U*1.5,minimum=0),closed=False) for i,v in enumerate((15.6,27.3,39.1,50.8,62.5,74.2,85.9,97.7,108.9))]
            children.append(ui.group('armor-cells',dict(ui.FULL),[art('armor-dividers',dividers)],mask={'paths':[shape('mask',paths[1],[1,1,1,1])]}))
        if kind=='health':
            beat=[(0,34.57),(11.2,34.57),(14.9,31.11),(18.6,34.57),(22.7,34.57),(23.7,37.07),(25.8,17.07),(27.7,40.77),(29.2,34.57),(37.4,34.57),(43.5,28.35),(44.7,28.35),(49.6,34.57),(51.8,34.57),(55.2,31.57),(58.6,34.57),(62.5,34.57)]
            traces=[]
            for low,color in [(False,'#161B0E'),(True,'#4C0D00')]:
                ident='ekg-low' if low else 'ekg-normal'
                lines=[ui.path('beat'+str(i),[(U*(xx+i*62.5),U*yy) for xx,yy in beat],stroke=ui.stroke(ui.solid(ui.rgb(color)),U*.95,minimum=0,join='round'),closed=False,blend='additive') for i in (-1,0,1,2)]
                node=art(ident,lines,transform=ui.transform(),opacity=ui.number(1))
                doc.bind(ident+'.motion',ident,'transform',[op('*',S('phase'),U*62.5),0,1,1,0])
                doc.bind(ident+'.low',ident,'opacity',choose(S('low'),1 if low else 0,0 if low else 1))
                traces.append(node)
            children.insert(-1,ui.group('ekg-mask',dict(ui.FULL),traces,mask={'paths':[shape('mask',paths[1],[1,1,1,1])]}))
            rim=art('health-rim',[shape('rim',paths[0],[1,0,0,.7],stroke=3,blend='additive')],opacity=ui.number(0))
            doc.bind('health.rim','health-rim','opacity',choose(S('low'),S('pulse'),0))
            children.append(rim)
        if strogg:
            grain=[]
            for i in range(64):
                gx=(i*37%119)+5;gy=(i*19%37)+12
                grain.append(ui.path('grain'+str(i),[(U*gx,U*gy),(U*(gx+.65),U*gy),(U*(gx+.65),U*(gy+.65)),(U*gx,U*(gy+.65))],fill=ui.solid([.9,.6,.2,.13]),blend='additive'))
            children.append(ui.group(kind+'-grain-mask',dict(ui.FULL),[art(kind+'-grain',grain)],mask={'paths':[shape('mask',paths[1],[1,1,1,1])]}))
        numbers = readout(doc,kind+'-number',kind,U*(31 if kind=='ammo' else 60),U*8,U*58,36,face,ink)
        readouts=[numbers]
        if kind=='ammo':
            doc.bind('ammo.number-position',numbers['id'],'left',choose(S('clip'),U*27,U*60))
            doc.state['reserve.gain']={'type':'number','initial':0}
            reserve=readout(doc,'reserve-number','reserve',U*81,U*13,U*37,23,face,ink)
            doc.bind('reserve.visible',reserve['id'],'display',choose(S('clip'),'block','none'))
            readouts.append(reserve)
        sheared=shear(kind+'-shear',U*width,U*height,.22 if strogg else -.22,children)
        contents=[sheared,ui.group(kind+'-digits',dict(ui.FULL),readouts) if strogg else
                  shear(kind+'-digits',U*width,U*height,-.22,readouts)]
        if kind!='ammo':
            icon=symbol(kind+'-icon',kind,U*(12 if strogg else 98),U*(14 if strogg else 38),U*16,accent)
            contents.append(icon)
            if not strogg:
                for channel,col in [('mp', '#FA2B05' if kind=='armor' else '#F7C004')]:
                    lit=symbol(kind+'-icon-mp',kind,U*98,U*38,U*16,ui.rgb(col,.8));contents.append(lit)
                    doc.bind(lit['id'],lit['id'],'opacity',choose(S('mp'),1,0))
                doc.bind(icon['id'],icon['id'],'opacity',choose(S('mp'),0,1))
            flash=symbol(kind+'-icon-gain',kind,U*(12 if strogg else 98),U*(14 if strogg else 38),U*16,[1,1,1,1]);contents.append(flash)
            doc.bind(flash['id'],flash['id'],'opacity',S(kind+'.gain'))
        else:
            roster=wheel.ROSTER[:10]+['corecannon','napalmgun','freezegun']
            for i,name in enumerate(roster+['napalmgun','gauntlet']):
                selected=op('==',S('weapon'),i)
                icon=symbol('ammo-icon'+str(i),name,U*(12 if strogg else 100),U*(14 if strogg else 38),U*16,accent)
                contents.append(icon);doc.bind(icon['id'],icon['id'],'display',choose(selected,'block','none'))
                flash=symbol('ammo-icon-gain'+str(i),name,U*(12 if strogg else 100),U*(14 if strogg else 38),U*16,[1,1,1,1])
                contents.append(flash)
                doc.bind(flash['id']+'.show',flash['id'],'display',choose(selected,'block','none'))
                doc.bind(flash['id']+'.gain',flash['id'],'opacity',S('ammo.gain'))
                gun='gun_darkmatter' if name=='darkmattergun' else 'gun_'+name
                if gun in ICONS:
                    gw=float(ICONS[gun]['vb'].split()[2]);gh=float(ICONS[gun]['vb'].split()[3])
                    pic=symbol('weapon-art'+str(i),gun,U*(110-26*gw/gh),U*15,U*26,ui.rgb('#C7BD78',.4))
                    contents.insert(1,pic)
                    doc.bind(pic['id'],pic['id'],'display',choose(op('&&',selected,op('&&',S('gun'),op('!',S('mp')))),'block','none'))
        gauge=ui.group(kind+'-gauge',{**ui.absolute(left=U*x,top=0,width=U*width,height=U*height),'display':ui.keyword('block')},contents)
        doc.bind(kind+'.visible',gauge['id'],'display',choose(S(kind+'.visible'),'block','none'))
        gauges.append(gauge)
    root=ui.group('status-root',dict(ui.FULL),[ui.group('status-bar',{
        **ui.absolute(width=U*group_width,height=U*height,bottom=U), 'left':ui.length(50,'%'),
        'margin-left':ui.length(-U*group_width/2),'transform':ui.transform()},gauges)])
    doc.bind('bar.scale','status-bar','transform',[0,op('-',op('*',op('-',1,S('scale')),U*height/2),S('inset')),S('scale'),S('scale'),0])
    return doc.build(root,{'canvas':{'height':720,'minWidth':960},'extensions':{'openq4':{'reference':'ui-visual-design.md 14.1–14.4','family':family,'widthUnits':group_width}}})


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for strogg in (False,True):
        path=ROOT/'content/baseoq4/pak0/guis/hud'/('status_bar_'+('strogg' if strogg else 'marine')+'.rui')
        data='// Generated by tools/ui/build_retained_status_bar.py; editable vector art.\n'+json.dumps(document(strogg),indent=2)+'\n'
        if args.check: assert path.read_text(encoding='utf-8')==data,path
        else: path.write_text(data,encoding='utf-8',newline='\n')
    print('Native status bar documents are current')


if __name__=='__main__': main()
