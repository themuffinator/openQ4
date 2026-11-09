#!/usr/bin/env python3
"""Build the editable vector catalog used by every screen-space gameplay HUD.

Symbols are first-party drawings from the openQ4 atlas. Layout, localized text,
input and script state stay with the game's GUI. No retail pixels are embedded.
"""
import argparse
import copy
import json
import math
import re
from pathlib import Path
import xml.etree.ElementTree as ET
import build_retained_screens as ui
from build_retained_weapon_wheel import svg_commands
from build_retained_status_bar import BACK, FILL, ST_BACK, ST_FILL
from classic_hud_art import gauge_art, gauge_rim

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'content/baseoq4/pak0/guis/hud'
SYMBOLS = json.loads(Path(__file__).with_name('hud_symbols.json').read_text())
WHITE = [1,1,1,1]

def path(d, box=(0,0,128,128), fill=WHITE, stroke=None, width=1, blend=None):
    x,y,w,h = box
    commands = svg_commands(d)
    for command in commands:
        if 'points' in command:
            command['points'] = [[{'fraction':(a-x)/w},{'fraction':(b-y)/h}] for a,b in command['points']]
    value = {'id':'p','commands':commands}
    if fill is not None: value['fill'] = ui.solid(fill)
    if stroke is not None: value['stroke'] = ui.stroke(ui.solid(stroke),width*128/math.sqrt(w*h),minimum=0,join='round',cap='round')
    if blend: value['blend']=blend
    return value

def colour(value, opacity=1):
    if value in ('currentColor','white'): return [1,1,1,opacity]
    if value in ('black',): return [0,0,0,opacity]
    if len(value)==4 and value.startswith('#'): value='#'+''.join(c*2 for c in value[1:])
    return ui.rgb(value,opacity)

def svg_shape(el):
    a=el.attrib; n=lambda k,default=0:float(a.get(k,default))
    if el.tag=='path': return a['d']
    if el.tag=='line': return f'M{n("x1")} {n("y1")}L{n("x2")} {n("y2")}'
    if el.tag in ('polygon','polyline'): return 'M'+a['points']+('Z' if el.tag=='polygon' else '')
    if el.tag in ('circle','ellipse'):
        x,y=n('cx'),n('cy');rx,ry=(n('r'),n('r')) if el.tag=='circle' else (n('rx'),n('ry'))
        return f'M{x-rx} {y}A{rx} {ry} 0 1 0 {x+rx} {y}A{rx} {ry} 0 1 0 {x-rx} {y}Z'
    if el.tag=='rect':
        x,y,w,h=n('x'),n('y'),n('width'),n('height');r=min(n('rx'),w/2,h/2)
        return f'M{x+r} {y}H{x+w-r}Q{x+w} {y} {x+w} {y+r}V{y+h-r}Q{x+w} {y+h} {x+w-r} {y+h}H{x+r}Q{x} {y+h} {x} {y+h-r}V{y+r}Q{x} {y} {x+r} {y}Z'
    return None

def multiply(a,b):
    return (a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5])

def svg_paths(item):
    box=tuple(map(float,item.get('vb','0 0 64 64').split()))
    root=ET.fromstring('<svg>'+item['svg']+'</svg>'); paths=[]
    definitions={e.get('id'):e for e in root.iter() if e.get('id')}
    def paint(value,opacity):
        if not value.startswith('url('): return ui.solid(colour(value,opacity))
        g=definitions[value[5:-1]]
        stops=[]
        for stop in g:
            n=stop.get('offset','0');n=float(n[:-1])/100 if n.endswith('%') else float(n)
            stops.append([n,colour(stop.get('stop-color','currentColor'),opacity*float(stop.get('stop-opacity','1')))])
        if g.tag=='linearGradient':
            return ui.linear([{'fraction':float(g.get('x1','0'))/box[2]},{'fraction':float(g.get('y1','0'))/box[3]}], [{'fraction':float(g.get('x2','1'))/box[2]},{'fraction':float(g.get('y2','0'))/box[3]}], stops)
        cx,cy,r=[float(g.get(k,'0')) for k in ('cx','cy','r')]
        return {'type':'radial','from':[{'fraction':cx/box[2]},{'fraction':cy/box[3]}], 'to':[{'fraction':(cx+r)/box[2]},{'fraction':cy/box[3]}], 'stops':[{'at':v,'color':ui.colour(c)} for v,c in stops]}
    def visit(el,styles,transform=(1,0,0,1,0,0),opacity=1):
        if el.tag=='defs': return
        styles={**styles,**el.attrib};opacity*=float(el.get('opacity','1'))
        for kind,args in re.findall(r'(\w+)\(([^)]+)\)',el.get('transform','')):
            v=[float(x) for x in re.split('[ ,]+',args.strip())]
            if kind=='translate': t=(1,0,0,1,v[0],v[1] if len(v)>1 else 0)
            elif kind=='scale': t=(v[0],0,0,v[-1],0,0)
            elif kind=='rotate':
                angle=math.radians(v[0]);t=(math.cos(angle),math.sin(angle),-math.sin(angle),math.cos(angle),0,0)
                if len(v)==3:t=multiply(multiply((1,0,0,1,v[1],v[2]),t),(1,0,0,1,-v[1],-v[2]))
            else: raise ValueError('Unsupported symbol transform '+kind)
            transform=multiply(transform,t)
        d=svg_shape(el)
        if d:
            commands=svg_commands(d)
            for cmd in commands:
                if 'points' in cmd:
                    a,b,c,d,e,f=transform
                    cmd['points']=[[{'fraction':(a*x+c*y+e-box[0])/box[2]},{'fraction':(b*x+d*y+f-box[1])/box[3]}] for x,y in cmd['points']]
            p={'id':'p'+str(len(paths)),'commands':commands,'fillRule':styles.get('fill-rule','nonzero')}
            if styles.get('fill','currentColor')!='none': p['fill']=paint(styles.get('fill','currentColor'),opacity*float(styles.get('fill-opacity','1')))
            if styles.get('stroke','none')!='none':
                p['stroke']=ui.stroke(paint(styles['stroke'],opacity*float(styles.get('stroke-opacity','1'))),float(styles.get('stroke-width','1'))*128/math.sqrt(box[2]*box[3]),minimum=0,join=styles.get('stroke-linejoin','round'),cap=styles.get('stroke-linecap','round'))
            paths.append(p)
        for child in el:visit(child,styles,transform,opacity)
    visit(root,{})
    return paths

def build():
    art={};materials={};images=[];image_aliases={}
    def add(name,paths,aliases=()):
        paths=copy.deepcopy(paths)
        for i,p in enumerate(paths):p['id']='p'+str(i)
        art[name]=ui.vector(name,{**ui.absolute(left=0,top=0,width=128,height=128),'display':ui.keyword('none')},paths)
        for alias in aliases:materials[alias.lower()]=name
    def aliases(name,*names):
        for n in names:materials[n.lower()]=name
    # Mask-based atlas glyphs are expressed as explicit positive stencils here,
    # keeping the catalog portable and the marks readable at 16 physical pixels.
    replacements={
        'sb_doubler':'M7 7C7 1 19 1 19 7C19 10 9 13 7 20H20',
        'item_doubler':'M5 4L19 20M19 4L5 20',
        'item_guard':'M5 3H19V11C19 17 15 20 12 22C9 20 5 17 5 11Z',
        'item_scout':'M15 2L5 13H11L8 23L21 9H14Z',
        'item_ammoregen':'M4 4H9V20H4ZM11 4H16V20H11ZM18 4H22V20H18Z',
        'chat':'M2 3H22V17H13L7 22V17H2ZM6 8H18M6 12H15',
    }
    for kind,items in SYMBOLS.items():
        if kind not in ('icons','crosshairs'):continue
        for name,item in items.items():
            if name in replacements:
                paths=[path(replacements[name],(0,0,24,24),None,WHITE,1.8)]
            elif name in ('item_deadzone','simple_item_deadzone'):
                paths=[path('M12 1L16 5L22 6L21 12L23 18L17 20L12 23L7 20L1 18L3 12L2 6L8 5Z M8 8H16V14L14 17H10L8 14Z',(0,0,24,24),None,WHITE,1.3)]
            else:paths=svg_paths(item)
            add(name,paths)
            prefix='gfx/guis/crosshairs/' if kind=='crosshairs' else 'gfx/guis/hud/icons/'
            aliases(name,prefix+name,prefix+name+'.tga')
            if kind=='icons':
                aliases(name,prefix+name+'_colored',prefix+name+'_colored.tga')
    for name in ('veh_mgun','veh_rocket','veh_shell'):aliases(name,'gfx/guis/vehicles/'+name)
    # Awakening cockpit reticles: four rounded brackets, optional range rails,
    # acquisition arrows, and the friendly-fire veto. Tint remains weapon-owned.
    corners = 'M0 42V34Q0 22 12 22H32V26H14Q8 26 8 34V42Z '
    corners += 'M96 22H116Q128 22 128 34V42H120V34Q120 26 114 26H96Z '
    corners += 'M0 86H8V94Q8 102 14 102H32V106H12Q0 106 0 94Z '
    corners += 'M120 86H128V94Q128 106 116 106H96V102H114Q120 102 120 94Z'
    brackets = [path(corners, fill=[1,1,1,.55], stroke=WHITE, width=.7),
                path('M62.5 62.5L65.5 65.5M65.5 62.5L62.5 65.5', fill=None, stroke=WHITE, width=1)]
    rails = [path('M5 46H8V82H5Z M120 46H123V82H120Z', fill=[1,1,1,.35], stroke=WHITE, width=.5)]
    add('aa-space-default',brackets+rails,['gfx/guis/q4x_crosshairs/aa_space_default'])
    add('aa-space-oor',brackets,['gfx/guis/q4x_crosshairs/aa_space_oor'])
    add('aa-space-acquire',brackets+rails+[
        path('M12 48L28 64L12 80Z M116 48L100 64L116 80Z',fill=[1,1,1,.55],stroke=WHITE,width=1)],
        ['gfx/guis/q4x_crosshairs/aa_space_acquire','gfx/guis/q4x_crosshairs/aa_space_lock'])
    add('aa-space-friendly',[
        path('M16 64A48 34 0 1 0 112 64A48 34 0 1 0 16 64Z',fill=None,stroke=[1,1,1,.12],width=2),
        path('M20 35L108 93M108 35L20 93',fill=None,stroke=WHITE,width=7),
        path('M20 35L108 93M108 35L20 93',fill=None,stroke=[.5,.5,.5,1],width=5)],
        ['gfx/guis/q4x_crosshairs/aa_space_friendly'])
    for name in ('marinelogo','strogglogo','quakelogo'):aliases(name,'gfx/guis/mainmenu/'+name)
    for name in ('ctf_flag','onectf_arc','noammo'):aliases(name,'gfx/guis/hud/'+name)
    aliases('frowny','gfx/mp/frowny')
    aliases('item_health','gfx/guis/hud/icons/teammate_health')
    aliases('item_armor','gfx/guis/hud/icons/teammate_armor')
    aliases('kill_self',*[f'gfx/guis/hud/icons/kill_{v}_openq4' for v in ('drown','slime','lava')])
    rect='M0 0H128V128H0Z'
    chamfer='M0 0H112L128 16V128H16L0 112Z'
    rounded='M4 0H124Q128 0 128 4V124Q128 128 124 128H4Q0 128 0 124V4Q0 0 4 0Z'
    add('solid',[path(rect)],['_white','white','gfx/guis/white','gfx/guis/common/128_sqsolid','gfx/guis/common/bar1','gfx/guis/common/line1'])
    add('clear',[path(rect,fill=[1,1,1,0])],['gfx/effects/particles_shapes/blank'])
    add('rounded',[path(rounded)])
    add('plate',[path(chamfer)])
    add('frame',[path(chamfer,fill=None,stroke=WHITE,width=1.2)])
    add('rounded-frame',[path(rounded,fill=None,stroke=WHITE,width=1.2)])
    add('bracket',[path('M36 1H1V127H36',fill=None,stroke=WHITE,width=2)])
    add('arrow',[path('M8 4L120 64L8 124L40 64Z')])
    add('line',[path('M0 60H128V68H0Z')])
    add('hazard',[path('M64 4L124 116H4Z M58 36H70V78H58Z M58 91H70V103H58Z')])
    art['hazard']['paths'][0]['fillRule']='evenodd'
    add('scanlines',[path(''.join(f'M0 {y}H128V{y+1}H0Z' for y in range(0,128,6)),fill=[1,1,1,.25])])
    add('noise',[path(''.join(f'M{x} {y}H{x+3+(x+y)%9}V{y+1}H{x}Z' for y in range(0,128,7) for x in range((y*11)%13,128,17)),fill=[1,1,1,.22])])
    add('waveform',[path('M0 64H12L16 38L20 80L24 60L28 69L35 18L39 101L44 58L49 72L57 42L60 80L65 62L70 64L73 20L79 98L83 59L89 73L95 44L99 89L104 62H128',fill=None,stroke=WHITE,width=1.5,blend='additive')])
    marine_fill, marine_armor, marine_ekg = gauge_art(path)
    strogg_fill, strogg_armor, strogg_ekg = gauge_art(path, True)
    add('ekg', marine_ekg)
    add('strogg-ekg', strogg_ekg)
    gradient=path(rect);gradient['fill']=ui.linear([0,0],[{'fraction':1},0],[[0,[1,1,1,0]],[1,WHITE]])
    add('gradient',[gradient])
    add('marine-back',[path(BACK,(0,0,125,59))])
    add('marine-fill',marine_fill)
    add('marine-armor',marine_armor)
    add('strogg-back',[path(ST_BACK,(0,0,129,67))])
    add('strogg-fill',strogg_fill)
    add('strogg-armor',strogg_armor)
    add('weapon-strip',[path('M2 2H123L127 6V126H5L1 122V3Q1 2 2 2Z')])
    add('pickup',[path('M0 0H126Q128 0 128 2V112L112 128H0Z')])
    # Preserve the transparent margins within the authored 200x50 / 100x50
    # quads. The shield material is one C-shaped half; the GUI mirrors it.
    add('boss-back',[path('M227.1 55.9V71L241.3 85.3H297.1L302.1 89.2H337.9L342.9 85.3H398.7L412.9 71V55.9L398.7 41.6H241.3Z',(220,40,200,50))])
    add('boss-health',[path('M246 63.45L255.2 54.1H382.8L392 63.45L382.8 72.8H255.2Z',(219,40,200,50),fill=[.67,.67,.67,1]),
        path('M246 63.45L255.2 54.1H382.8L392 63.45L382.8 72.8H255.2Z',(219,40,200,50),fill=None,stroke=WHITE,width=1)])
    add('boss-shield',[path('M231 58.2L231.3 69.2L245.2 82.9H303.9L310.5 76.5L252.3 76L239.9 63.5L252.3 50.9H316.1L317.6 49.2V45.6L316.1 44H245.2Z',(220,40,100,50))])
    add('vehicle-panel',[path('M0 0H98L128 30V128H0Z')])
    add('flatbed',[path('M6 78V43L31 36L42 16H85L95 36L122 43V78L113 84V107H94V85H34V107H15V84Z M35 38H93V73H35Z',fill=None,stroke=WHITE,width=3)])
    add('tram',[path('M19 14H109L119 28V99L106 111V120H92V112H36V120H22V111L9 99V28Z M21 35H107V64H21Z M24 82H35V93H24Z M93 82H104V93H93Z',fill=None,stroke=WHITE,width=3)])
    add('electric',[path('M57 5L31 70H58L47 123L99 50H71L91 5Z M24 12Q-4 64 24 116 M112 12Q140 64 112 116',fill=None,stroke=WHITE,width=4)])
    add('hex',[path('M1 43L22 7H64L85 43L64 79H22Z',(0,0,86,86),None,[1,1,1,.08],10,'additive'),
        path('M1 43L22 7H64L85 43L64 79H22Z',(0,0,86,86),None,[1,1,1,.2],4,'additive'),
        path('M1 43L22 7H64L85 43L64 79H22Z',(0,0,86,86),None,WHITE,1.2,'additive')])
    add('med-edge',[path('M0 234H18L154 0',(0,0,256,256),None,[1,1,1,.12],12,'additive'),
        path('M0 234H18L154 0',(0,0,256,256),None,WHITE,1.4,'additive')])
    add('edge-glow',[path('M1 1H127V127H1Z',fill=None,stroke=[1,1,1,.1],width=7,blend='additive'),
        path('M1 1H127V127H1Z',fill=None,stroke=[1,1,1,.3],width=1,blend='additive')])
    add('damage',[path('M0 0H128V128H116V20H0Z',fill=[1,1,1,.6],blend='additive')])
    # The legacy GUI rotates a full-canvas texture. Preserve its transparent
    # margins: a soft dome centred 151 u above the aim point, not a triangle
    # stretched across the whole viewport. Linear-painted sectors provide an
    # elliptical gradient in the catalog, whose bridge renders paths directly
    # without document-node transforms. Adjacent sectors agree on each radius.
    dome=[]
    for i in range(64):
        a,b=2*math.pi*i/64,2*math.pi*(i+1)/64
        x0,y0=320+150*math.cos(a),89+52*math.sin(a)
        x1,y1=320+150*math.cos(b),89+52*math.sin(b)
        p=path(f'M320 89L{x0} {y0}L{x1} {y1}Z',(0,0,640,480),blend='additive')
        # Work in the catalog's square coordinates, including the ellipse's
        # anisotropic radii, so every point on the outer chord is transparent.
        dx,dy=(x1-x0)/640,(y1-y0)/480
        nx,ny=dy,-dx
        mx,my=((x0+x1)/2-320)/640,((y0+y1)/2-89)/480
        reach=(mx*nx+my*ny)/(nx*nx+ny*ny)
        p['fill']=ui.linear([{'fraction':.5},{'fraction':89/480}],
                            [{'fraction':.5+nx*reach},{'fraction':89/480+ny*reach}],
                            [[0,WHITE],[1,[1,1,1,0]]])
        dome.append(p)
    add('direction',dome)
    add('direction-chevron',[path('M275 152L320 126L365 152L320 140Z',(0,0,640,480),blend='additive')])
    add('ring',[path('M64 4A60 60 0 1 0 64 124A60 60 0 1 0 64 4Z',fill=None,stroke=WHITE,width=1.5)])
    add('scope-quadrant',[path('M128 3C59 3 3 59 3 128 M128 12C65 12 12 65 12 128 M48 8V20 M8 48H20 M94 0V12 M0 94H12',fill=None,stroke=WHITE,width=1.5)])
    add('scope-glow',[path('M128 3C59 3 3 59 3 128',fill=None,stroke=[1,1,1,.25],width=9,blend='additive')])
    add('pointer',[path('M4 2L93 86H53L73 121L54 127L34 91L4 116Z',fill=[0,0,0,.8]),path('M8 12L79 81H45L64 118L57 121L35 80L8 103Z')],['gfx/guis/guicursor_arrow','gfx/guis/guicursor_hand'])
    # Closed 1 u boxes with elbow links, not filled plates (spec 14.14).
    for i,(boxw,boxh,link) in enumerate(((128,32,30),(128,32,45),(128,32,59)),1):
        add('tourney-box'+str(i),[path(f'M1 1H96V31H1Z M97 16H{97+link}V32',(0,0,128,32),None,WHITE,1)])
    add('tourney-link',[path('M0 0H64V128H128',fill=None,stroke=WHITE,width=1)])
    add('score-header',[path('M0 8H122L128 14V104H0Z')])
    # Stock menu furniture the bridged multiplayer GUIs draw (the match
    # summary, the Classic scoreboard, HUD and objectives): the measured
    # constructions of ui-visual-design.md's stock furniture vocabulary,
    # authored in each texture's own texels so they stretch and flip exactly
    # as the stock art does. Rails are filled one-texel geometry, never
    # strokes, which would thicken unevenly in a stretched tile. The art
    # carries its own colours and alpha; the GUI's tint multiplies them.
    def texel(d,w,h,fill=WHITE): return path(d,(0,0,w,h),fill=fill)
    def graded(d,w,h,rgb,stops,vertical=False):
        p=path(d,(0,0,w,h)); end=[0,{'fraction':1}] if vertical else [{'fraction':1},0]
        stops=[(0,stops[0][1])]*(stops[0][0]>0)+list(stops)+[(1,stops[-1][1])]*(stops[-1][0]<1)
        p['fill']=ui.linear([0,0],end,[[at,[*rgb,a]] for at,a in stops]); return p
    def poly(points): return 'M'+'L'.join(f'{x} {y}' for x,y in points)+'Z'
    def rect(x0,y0,x1,y1): return poly([(x0,y0),(x1,y0),(x1,y1),(x0,y1)])
    hexrgb=lambda v:tuple(int(v[i:i+2],16)/255 for i in (1,3,5))
    # Soft backing (scoregrad2): clear outside a 424x442 texel panel whose
    # sides feather over 27 and 28 texels.
    add('soft-backing',[graded(rect(44,34.5,468,476.5),512,512,(1,1,1),[(44/512,0),(71/512,1),(440/512,1),(468/512,0)])])
    # Tab strips (ctrls_tab1, summ_tab2, summ_tab3): a baseline rail broken
    # under the active tab, which rises 25 texels with a 4-texel leading flare
    # and a 13-texel 45-degree trailing shoulder; #5A652A from 1.00 at its top
    # to 0.60 at the baseline, then a wash across the strip to clear at 54.
    tab_green=hexrgb('#5A652A')
    for n,(left,right) in enumerate(((22,145),(183,322),(336,481)),1):
        shoulder=right-13
        add('tab-strip-'+str(n),[
            graded(rect(17,26.5,490,54),512,64,tab_green,[(26.5/64,.6),(54/64,0)],vertical=True),
            graded(poly([(left-4,26.5),(left,22.5),(left,1.5),(shoulder,1.5),(right,14.5),(right,26.5)]),512,64,tab_green,
                   [(1.5/64,1),(6/64,1),(26.5/64,.6)],vertical=True),
            texel(rect(17,26,left-2,27),512,64),texel(poly([(left,22),(left+1,22),(left-2,26),(left-3,26)]),512,64),
            texel(rect(left,1,left+1,22),512,64),texel(rect(left,1,shoulder,2),512,64),
            texel(poly([(shoulder,1),(shoulder+1.4,1),(right+1,13.6),(right+1,15)]),512,64),
            texel(rect(right,14,right+1,26),512,64),texel(rect(right,26,490,27),512,64)])
    # List bands (bg_line2, bg_hover2, bg_grey2): rows 3-60 of 64, ends fading
    # over the first 28% and the last 26% of the width.
    band=[(0,0),(.28,1),(.74,1),(1,0)]
    for name,rgb,alpha in (('list-line','#9A9C6E',20/255),('list-hover','#9A9C6E',75/255),('list-grey','#787E87',105/255)):
        add(name,[graded(rect(0,3,512,61),512,64,hexrgb(rgb),[(at,a*alpha) for at,a in band])])
    # Separators (horiz_line2, vert_line): a two-texel line centered in its
    # strip; the vertical one fades over its last 6% at each end.
    add('separator-h',[texel(rect(1,3,255,5),256,8)])
    add('separator-v',[graded(rect(3,0,5,256),8,256,(1,1,1),[(1/256,0),(17/256,1),(239/256,1),(255/256,0)],vertical=True)])
    # Header band (scoreheader): an 8-texel upper-leading cut, top and leading
    # rails, fill 49% to 45% of the width, then fading out at the trailing end.
    add('header-band',[
        graded(poly([(2,10),(10,2),(512,2),(512,29),(2,29)]),512,32,(1,1,1),
               [(0,.486),(.45,.486),(.61,.38),(.735,.25),(.86,.125),(1,0)]),
        texel(rect(2,10,3,29),512,32),texel(poly([(2,10),(3,10),(10,3),(10,2)]),512,32),
        graded(rect(10,2,512,3),512,32,(1,1,1),[(0,1),(.45,1),(.61,.76),(.735,.48),(.86,.22),(1,0)])])
    # Light plate (b3_light): a 24-texel band from texel 23, a lower-leading
    # 45-degree cut through 56% of it, rails on the leading edge, the cut and
    # the bottom, fill 49%; fill and bottom rail full to 45% of the width.
    add('light-plate',[
        graded(poly([(23,3),(512,3),(512,27),(37,27),(23,13)]),512,32,(1,1,1),
               [(0,.486),(.45,.486),(.61,.40),(.735,.27),(.86,.087),(1,0)]),
        texel(rect(23,3,24,13),512,32),texel(poly([(23,12.5),(24,12.5),(38.5,27),(37.5,27)]),512,32),
        graded(rect(38,27,512,28),512,32,(1,1,1),[(0,1),(.45,1),(.61,.82),(.73,.54),(.86,.22),(1,0)])])
    # Chat box (chatbox_edge, chatbox_mid): black 0.49 behind #CCCC51 rails on
    # the leading edge and the top, with a small chamfer between them; the top
    # rail dissolves toward the open trailing side.
    rail=hexrgb('#CCCC51')
    add('chatbox-edge',[
        texel(poly([(2,16),(2,2),(3,1),(512,1),(512,16)]),512,16,fill=[0,0,0,126/255]),
        texel(rect(1,3,2,16),512,16,fill=[*rail,1]),texel(poly([(1,3),(1,4.2),(4.2,1),(3,1)]),512,16,fill=[*rail,1]),
        graded(rect(3,1,512,2),512,16,rail,[(0,1),(.31,1),(.5,.76),(.656,.55),(.78,.38),(.906,.18),(1,0)])])
    add('chatbox-mid',[texel(rect(2,0,512,8),512,8,fill=[0,0,0,126/255]),texel(rect(1,0,2,8),512,8,fill=[*rail,1])])
    # Card frame (tooltip_edge, tooltip_mid and their 128-texel variants): a
    # black body inside #A0A040 rails, a 2-texel top-leading cut and an
    # 8-texel top-trailing cut; the GUI flips the cap for the bottom.
    card=hexrgb('#A0A040')
    for suffix,w in (('',256),('-small',128)):
        add('card-edge'+suffix,[
            texel(poly([(3,16),(3,4.5),(4.5,3),(w-10.6,3),(w-3,10.6),(w-3,16)]),w,16,fill=[0,0,0,1]),
            texel(rect(2,4,3,16),w,16,fill=[*card,1]),texel(poly([(2,4),(2,5.4),(5.4,2),(4,2)]),w,16,fill=[*card,1]),
            texel(rect(4,2,w-10.5,3),w,16,fill=[*card,1]),
            texel(poly([(w-11,2),(w-9.6,2),(w-2,9.6),(w-3.4,9.6)]),w,16,fill=[*card,1]),
            texel(rect(w-3,9.6,w-2,16),w,16,fill=[*card,1])])
        add('card-mid'+suffix,[texel(rect(3,0,w-3,2),w,2,fill=[0,0,0,1]),texel(rect(2,0,3,2),w,2,fill=[*card,1]),
                               texel(rect(w-3,0,w-2,2),w,2,fill=[*card,1])])
    # Backdrop gradient (bg_darkgrad2): clear across the leading 18%, full
    # from 34% to 91%, clear again by 98%.
    add('backdrop-grad',[graded(rect(0,0,512,512),512,512,(1,1,1),[(.18,0),(.343,1),(.914,1),(.977,0)])])
    # Marker (corner): the solid upper-trailing half of a 16-texel square (◥)
    # in its 32-texel tile, with a halo about 7 texels deep: five widening
    # copies at 0.11 each, each edge pushed out along its normal.
    halo=[]
    for d in (7.5,6,4.5,3,1.5):
        s=d*math.sqrt(2)
        halo.append(texel(poly([(8-d-s,8-d),(24+d,8-d),(24+d,24+d+s)]),32,32,fill=[1,1,1,.11]))
    add('marker-corner',halo+[texel(poly([(8,8),(24,8),(24,24)]),32,32)])
    common={
        'add_box2':'edge-glow','add_lines1':'scanlines','arrow2':'arrow','arrow5':'arrow','bracket1':'bracket','colorstatic':'noise',
        'gradient':'gradient','haz_symb1':'hazard','img_static3':'noise','marine/decal2':'scanlines','marine/scrolltext1faster':'scanlines',
        'monitor_darkedge2':'frame','monitor_line6':'scanlines','screen_dirt2':'noise','simplebox3':'frame','smalltext1':'scanlines','smalltext3':'scanlines',
        'static1':'noise','strogg/data':'scanlines','strogg/doorgui_inner_r':'ring','strogg/doorgui_outer_r':'ring','strogg/genericseal_dot':'hex',
        'strogg/scrolltext1fast':'scanlines','strogg/scrolltext2':'scanlines','warnbar1':'scanlines'}
    hud={
        'arbar':'marine-armor','aud_wavbg':'rounded','backbar':'marine-back','backbar_add':'marine-back',
        'ctf_bgbar':'plate','ctf_bgbar2':'plate','directional_hit':'direction','directional_hit2':'direction-chevron','ekg':'ekg',
        'ffa_bgbar':'rounded','ffa_bgbar2':'rounded','flatbedbar':'flatbed','flatbededge':'flatbed',
        'medchange_edge':'med-edge','medchange_hex':'hex','mp_waveform':'waveform','obj_btn1':'frame','obj_btn2':'frame','obj_btn3':'frame','obj_btn4':'frame',
        'pickupbar':'pickup','powerup_bgbar':'rounded','s_arbar':'strogg-armor','s_backbar':'strogg-back','s_backbar_add':'strogg-back',
        's_boss_healthbar':'boss-health','s_boss_shieldbar':'boss-shield','s_bossbar':'boss-back','s_ekg':'strogg-ekg','s_hudstatic':'noise','s_valbar':'strogg-fill','s_waveform':'waveform',
        'trambar':'tram','tramedge':'tram','valbar':'marine-fill','veh_arbar':'marine-fill','veh_backbar':'marine-back','veh_backbar2':'vehicle-panel','veh_backbar2a':'vehicle-panel',
        'veh_valbar':'marine-fill','veh_xdir1':'direction','veh_xdir2':'direction','waveform':'waveform','wsbar':'weapon-strip','wsbarglow':'rounded-frame',
        'reticles/airdef_loadbar':'scanlines','reticles/airdef_loadbar2':'scanlines','reticles/nailgun_scope':'scope-quadrant',
        'reticles/nailgun_scope_glow':'scope-glow','reticles/nailgun_scope_ring1':'ring','reticles/nailgun_scope_ring2':'ring'}
    for n in ('tourn_bar1','tourn_bar2','tourn_bar3','tourn_sep','tourn_sep2','tourney_box1','tourney_box2','tourney_box3',
              'tourney_box1fill','tourney_box2fill','tourney_box3fill','tourntop_msg','tourntop_over','tourntop_over2','tourntop_over3'):
        hud[n]='plate'
    for n in ('tourney_box1sel','tourney_box2sel','tourney_box3sel','tourntop_sel','tourntop_sel2'):hud[n]='frame'
    for i in range(1,4):hud['tourney_box'+str(i)]='tourney-box'+str(i)
    for n in ('tourn_bar1','tourn_bar2','tourn_bar3','tourn_sep','tourn_sep2'):hud[n]='tourney-link'
    menu={
        'b1_dark':'plate','b3_light':'light-plate','bg_darkgrad2':'backdrop-grad','bg_grey2':'list-grey','bg_hover2':'list-hover',
        'bg_line2':'list-line','chatbox_edge':'chatbox-edge','chatbox_mid':'chatbox-mid','corner':'marker-corner','ctrls_tab1':'tab-strip-1',
        'horiz_line2':'separator-h','vert_line':'separator-v','scoregrad2':'soft-backing','scoreheader':'header-band',
        'summ_tab2':'tab-strip-2','summ_tab3':'tab-strip-3','screen':'solid',
        'tooltip_edge':'card-edge','tooltip_edge2':'card-edge-small','tooltip_mid':'card-mid','tooltip_mid2':'card-mid-small'}
    for prefix,table in [('common',common),('hud',hud),('mainmenu',menu)]:
        for material,artwork in table.items():aliases(artwork,'gfx/guis/'+prefix+'/'+material)
    # Open objective plates have a leading rail and a separate chamfered foot,
    # with a fading interior. They are not closed rectangles (Appendix B.5).
    for n,w,h,foot in ((1,279,127,False),(2,279,17,True),(3,600,112,False),(4,600,20,True)):
        cut=h*.9 if foot else 0
        outline=f'M0 0H{w}V{h}H{cut}L0 {h-cut}Z'
        shape=path(outline,(0,0,w,h))
        shape['fill']=ui.linear([0,0],[{'fraction':1},0],[[0,[1,1,1,.49]],[.35,[1,1,1,.49]],[.65,[1,1,1,0]],[1,[1,1,1,0]]])
        rail=path(f'M.5 0V{h-cut}L{cut} {h-.5}H{w}' if foot else f'M.5 0V{h}',(0,0,w,h),None,WHITE,1)
        rail['stroke']['paint']=ui.linear([0,0],[{'fraction':1},0],[[0,WHITE],[.4,WHITE],[.75,[1,1,1,0]],[1,[1,1,1,0]]])
        add('objective-'+str(n),[shape,rail],['gfx/guis/hud/obj_btn'+str(n)])
    for kind in ('ctf','ffa'):
        shape=path('M0 0H110L128 128H0Z')
        shape['fill']=ui.linear([0,0],[{'fraction':1},0],[[0,[1,1,1,.87]],[.34,[1,1,1,.5]],[.81,[1,1,1,.15]],[1,[1,1,1,.1]]])
        add(kind+'-slab',[shape],['gfx/guis/hud/'+kind+'_bgbar','gfx/guis/hud/'+kind+'_bgbar2'])
    for material,base in (('backbar_add','marine-back'),('s_backbar_add','strogg-back')):
        add(base+'-glow',gauge_rim(path, base=='strogg-back'),['gfx/guis/hud/'+material])
    aliases('rounded','gfx/guis/hud_rev3/radiobar','gfx/guis/soundmeter/audiobg')
    aliases('rounded-frame','gfx/guis/soundmeter/audioverlay','gfx/guis/vehicles/mdisp_edge','gfx/guis/vehicles/mdisp_btns')
    aliases('solid','gfx/guis/vehicles/bar2')
    aliases('electric','gfx/guis/vehicles/veh_elecwarn')
    aliases('scope-quadrant','gfx/guis/hud/reticles/nailgun_scope_oq4')
    aliases('scope-glow','gfx/guis/hud/reticles/nailgun_scope_oq4_left','gfx/guis/hud/reticles/nailgun_scope_oq4_top')
    aliases('chat','gfx/guis/hud/chat')
    # These material guides supply an intrinsic colour in addition to the GUI
    # tint. Preserve it in the editable vector; a white GUI tint must not turn
    # every weapon/pickup into the same white silhouette.
    weapon_colours={'machinegun':(1,1,0),'nailgun':(.6,.8,.8),'railgun':(0,1,0),
        'shotgun':(1,.5,0),'hyperblaster':(0,.45,1),'rocket':(1,.2,0),
        'grenade':(.2,.56,.07),'lightning':(1,1,.73),'darkmatter':(.77,0,1),'gauntlet':(0,.85,1)}
    tinted={prefix+name:(prefix+name,rgb) for name,rgb in weapon_colours.items()
            for prefix in ('item_','ammo_') if prefix+name in art}
    tinted.update({
        'item_healthshard':('item_health',(.5,1,.5)), 'item_healthsmall':('item_health',(1,1,.2)),
        'item_healthlarge':('item_health',(1,.5,0)), 'item_healthmega':('item_health',(0,.5,1)),
        'item_armorshard':('item_armorshard',(0,.5,1)), 'item_armorsmall':('item_armor',(1,1,0)),
        'item_armorlarge':('item_armor',(1,0,0))})
    tinted.update({'item_flamegun':('item_fire',(1,1,0)),
        'item_corecannon':('item_corecannon',(1,1,0)), 'item_freezegun':('item_freeze',(1,1,0))})
    def tint(value,rgb):
        if isinstance(value,dict):
            if value.get('type')=='color':
                for channel,scale in enumerate(rgb):value['value'][channel]*=scale
            else:
                for child in value.values():tint(child,rgb)
        elif isinstance(value,list):
            for child in value:tint(child,rgb)
    for name,(base,rgb) in tinted.items():
        paths=copy.deepcopy(art[base]['paths']);tint(paths,rgb)
        add(name+'_colored',paths,['gfx/guis/hud/icons/'+name+'_colored','gfx/guis/hud/icons/'+name+'_colored.tga'])
    aliases('item_generic','gfx/guis/hud/icons/item_moderator.tga')
    aliases('item_adrenaline','gfx/guis/hud/icons/item_adrenal.tga','gfx/guis/hud/icons/item_adrenal')
    aliases('gun_napalmgun','gfx/guis/hud/icons/gun_flamegun')
    # Optional movement HUD uses the same vector presentation as the GUI HUD.
    for name,d in {
        'center':'M45 45H83V83H45Z', 'forward':'M64 8L118 102H10Z',
        'back':'M64 120L10 26H118Z', 'moveleft':'M8 64L102 10V118Z',
        'moveright':'M120 64L26 118V10Z', 'moveup':'M64 8L112 56H83V118H45V56H16Z',
        'movedown':'M64 120L16 72H45V10H83V72H112Z'}.items():
        add('input-'+name,[path(d)],['gfx/guis/showinput/hud_showinput_'+name])
    for name,target in [('ammo_napalmgun','simple_ammo_napalmgun'),('item_napalmgun','simple_item_napalmgun'),('deadzone','simple_item_deadzone')]:aliases(target,'gfx/guis/hud/simpleicons/'+name)
    aliases('arrow','gfx/guis/monitors/marine/mcc1_triangle')
    aliases('quakelogo','gfx/guis/mainmenu/intro_logo','gfx/guis/mainmenu/endlogo')
    # Detailed body diagrams, campaign maps and the Quake title treatment are
    # complex imagery; they remain installed-content pictures, never copied.
    images += ['gfx/guis/vehicles/mdisp_main','gfx/guis/vehicles/mdisp_mech','gfx/guis/monitors/marine/mcc13_bigmap','gfx/guis/monitors/marine/mcc13_key','gfx/guis/mainmenu/q4text']
    images += ['gfx/guis/mainmenu/e3_'+n for n in ('0','0_add','1','2')]
    # Complex earned medals are explicit bitmap exceptions (design section 6).
    # Register every stock award during level precache, including Holy Shit.
    images += ['gfx/mp/awards/'+n for n in ('assist','capture','combo_kill','defense',
               'excellent','holy_shit','humiliation','impressive','rampage')]
    # The Remastered Strogg gauge's complex grain is the installed stock
    # texture under an editable vector mask, not a bundled extracted asset.
    images += ['q4-add/gfx/guis/hud/s_static']
    # The ammo backdrop uses the original side-view weapon bitmap, including
    # its transparent margins. The user explicitly requested this exception.
    weapon_images = {name: symbol for name, symbol in materials.items() if symbol.startswith('gun_')}
    for name, symbol in weapon_images.items():
        del materials[name]
        canonical = name.removesuffix('.tga').removesuffix('_colored')
        for alias in (name, name if name.endswith('.tga') else name + '.tga'):
            images.append(alias)
            if alias != canonical:
                image_aliases[alias] = canonical
    # Rhino is maintained with the other first-party source symbols.
    if 'radio_rhino' in SYMBOLS.get('icons',{}):aliases('radio_rhino','gfx/guis/hud/radio_rhino')
    else:raise ValueError('radio_rhino is required in hud_symbols.json')
    # Expansion overlays share the same presentation bridge and editable art.
    for name,item in SYMBOLS['panels'].items():add(name,svg_paths(item))
    add('ship-disc',[path('M64 5.25A58.75 58.75 0 1 0 64 122.75A58.75 58.75 0 1 0 64 5.25Z',fill=ui.rgb('#1F2A13',.85),stroke=ui.rgb('#64883D'),width=2.3)])
    add('ship-shade',[path('M64 8A56 56 0 1 0 64 120A56 56 0 1 0 64 8Z',fill=[0,0,0,.25])])
    add('ship-cone',[path('M64 64L49.23 33.7A33.7 33.7 0 0 1 78.77 33.7Z',fill=ui.rgb('#585E29',.5),blend='additive')])
    add('race-plate',[path('M0 124H117L128 25H18L15 5H5Z',fill=[0,0,0,.45])])
    add('race-pip',[path('M28 0H128L100 128H0Z')])
    add('gev-hull',[path('M37 16Q64 2 91 16L100 103Q64 123 28 103Z M19 32V95 M109 32V95',fill=None,stroke=WHITE,width=6)])
    add('gev-turret',[path('M58 2H70V49H58Z M64 45A20 20 0 1 0 64 85A20 20 0 1 0 64 45Z')])
    for i in range(1,4):
        r=60-(i-1)*8
        add('gev-shield'+str(i),[path(f'M{64-r*.57} {64-r*.82}A{r} {r} 0 0 0 {64-r*.57} {64+r*.82}',fill=None,stroke=WHITE,width=5)])
    for n,target in {'hud_base_01':'ship-disc','hud_base_02':'ship-shade','hud_base_03':'ship-cone','hud_ship_01':'ship','hud_target_01':'ring'}.items():aliases(target,'gfx/guis/vehicle/airassault/'+n)
    for i in range(5):aliases('ship-damage'+str(i),'gfx/guis/vehicle/airassault/hud_ship_0'+str(i+2))
    for i,direction in enumerate(('N','NE','E','SE','S','SW','W','NW')):
        p=path('M38 30L64 104L90 30L74 48H54Z',fill=ui.rgb('#D50000'),blend='additive')
        angle=i*math.pi/4
        for cmd in p['commands']:
            for point in cmd.get('points',[]):
                x,y=point[0]['fraction']-.5,point[1]['fraction']-.5
                point[0]['fraction']=.5+x*math.cos(angle)-y*math.sin(angle);point[1]['fraction']=.5+x*math.sin(angle)+y*math.cos(angle)
        add('ship-warning-'+direction.lower(),[p],['gfx/guis/vehicle/airassault/hud_warning_'+direction])
    for material,target in {
        'common/directional_indicator':'direction','common/marine/squares1':'scanlines','common/monitor_line8':'scanlines','common/q4x_compass01':'ring',
        'hud/new/blackbar2':'rounded','hud/radiobar':'rounded','hud/tempboss_shield':'boss-shield','wav':'waveform',
        'vehicle/speeder/boosticon':'hazard','vehicle/speeder/progressbar_01':'race-plate','vehicle/speeder/progressbar_03':'race-pip',
        'vehicles/bar':'rounded','vehicles/bar_b':'rounded','vehicles/infinity':'icon_infinity','vehicles/veh_base':'gev-hull',
        'vehicles/veh_healthtemp':'rounded','vehicles/veh_turret':'gev-turret',
        'vehicles/veh_shields1':'gev-shield1','vehicles/veh_shields2':'gev-shield2','vehicles/veh_shields3':'gev-shield3'}.items():aliases(target,'gfx/guis/'+material)
    for material,name in list(materials.items()):
        if not material.endswith('.tga'):materials.setdefault(material+'.tga',name)
    document=ui.Document('openq4.hud-artwork').build(ui.group('catalog',ui.FULL,list(art.values())))
    roots=['hud','hud_strogg','cursor','mphud','wristcomm','wristcomm_strogg','scoreboard','summary','mpmsgmode','cinematic','e3cinematic',
           'vehicles/hud','vehicles/airdef_cannon','vehicles/medchange','vehicles/mdisp','vehicles/strogg_noise','vehicles/blank']
    expansion=['q4xhud','q4xhud_strogg','q4x_cannon_hud','dropshiphealth','mcchealth','introcinematic',
               'vehicles/speederplayerhud','vehicles/gev_hud','vehicles/mcc/cannonplayerhud','vehicles/airassault/turretplayerhud']
    # HUDs openQ4 itself ships (content/baseoq4), audited from the repository's own files.
    own=['vehicles/turret_hud']
    registry={'version':1,'roots':['guis/'+n+'.gui' for n in roots],'materials':dict(sorted(materials.items())),'images':sorted(set(images)),
              'expansionRoots':['guis/'+n+'.gui' for n in expansion],
              'openq4Roots':['guis/'+n+'.gui' for n in own],
              'imageAliases':{'gfx/guis/mainmenu/e3_0_add':'q4-add/gfx/guis/mainmenu/e3_0', **dict(sorted(image_aliases.items()))}}
    return {'artwork.q4ui':document,'coverage.json':registry}

def canonical_numbers(value):
    """Discard platform libm roundoff far below the renderer's float precision."""
    if isinstance(value, float):
        result = round(value, 12)
        return 0.0 if result == 0 else result
    if isinstance(value, dict):
        return {key: canonical_numbers(item) for key, item in value.items()}
    if isinstance(value, list):
        return [canonical_numbers(item) for item in value]
    return value


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for name,data in build().items():
        text=json.dumps(canonical_numbers(data),indent=2,ensure_ascii=False)+'\n';target=OUT/name
        if args.check:
            assert target.read_text(encoding='utf8')==text,str(target)+' is stale'
        else:target.write_text(text,encoding='utf8')
    print('Retained HUD catalog is current' if args.check else 'Built retained HUD catalog')

if __name__=='__main__':main()
