#!/usr/bin/env python3
"""Build the layered initialization screen from visual specification 14.17."""
import argparse
import json
import math
import re
from pathlib import Path
import build_retained_screens as ui
import build_retained_scopes as optics

U = ui.U
GLYPHS = json.loads(Path(__file__).with_name('initialization_paths.json').read_text())['glyphs']
TONE = {26:'#171A09',27:'#181B09',28:'#181C09',29:'#191D0A',30:'#1A1E0A',31:'#1C1F0A',
        32:'#1C200B',33:'#1D210B',35:'#1E230C',36:'#1F240C',37:'#1F250C',38:'#20260C',39:'#21270C'}

# A key just after a timeline's start: the runtime begins each track from the
# value it has when played, so this states where the motion itself starts.
SNAP = .001
EASE = [.33, 0, .2, 1]

def pose(tx=0, ty=0, s=1, rot=0):
    return {'type':'transform','unit':'dp','value':[round(tx,3),round(ty,3),s,s,rot]}

def sector(ident, inner, outer, start, end, color):
    # Sub-degree chords remain below a pixel at 4K. Every ring is editable.
    steps=max(2, math.ceil(abs(end-start)))
    def point(r,a):
        a=math.radians(a)
        return U*(320+r*math.sin(a)), U*(240-r*math.cos(a))
    points=[point(outer,start+(end-start)*i/steps) for i in range(steps+1)]
    points += [point(inner,end-(end-start)*i/steps) for i in range(steps+1)]
    return ui.path(ident,points,fill=ui.solid(color))

def wordmark():
    paths=[]
    for name,data in GLYPHS.items():
        tokens=re.findall(r'[MZ]|-?\d+(?:\.\d+)?',data)
        commands=[];i=0;move=True
        while i<len(tokens):
            if tokens[i]=='M': move=True;i+=1;continue
            if tokens[i]=='Z': commands.append({'id':f'p{len(commands)}','op':'close'});i+=1;continue
            x=129.42+.3667*float(tokens[i]);y=148.76+.3667*float(tokens[i+1])
            commands.append({'id':f'p{len(commands)}','op':'move' if move else 'line','points':[[U*x,U*y]]})
            i+=2;move=False
        paths.append(optics.compound('letter-'+name,[commands],ui.rgb('#F2F0EC')))
    return paths

def document():
    doc=ui.Document('openq4.initialization')
    doc.state['message']={'type':'string','initial':'#str_104343'}
    doc.state['step']={'type':'number','initial':0}
    doc.state['motion_reduced']={'type':'boolean','initial':False,'cvar':'ui_retainedReducedMotion'}
    def ring(ident,a,b,tone): return optics.ring(ident,(a+b)/2,b-a,ui.rgb(TONE[tone]))
    field=ui.vector('field',dict(ui.FULL),[ui.path('fill',[(0,0),({'fraction':1},0),({'fraction':1},{'fraction':1}),(0,{'fraction':1})],fill=ui.solid(ui.rgb(TONE[26])))])
    # The machined bands are solid annuli in steps of one to three levels: no
    # visible edge needs coverage, so they compile once as a local mesh that
    # can turn and scale intact. (A radial paint with hard stops subdivides
    # into tens of thousands of triangles.) The field shows through the center.
    bands=[(169,36),(175.5,37),(204.5,38),(210.5,30),(264.8,29),(271.8,28),(273,27),(283.5,28),(285.5,29),(295.5,28),(297.5,27),(300.5,26)]
    rings=[optics.compound(f'band-{index}',[optics.circle(outer),optics.circle(inner)],ui.rgb(TONE[tone]))
           for index,((inner,tone),(outer,_)) in enumerate(zip(bands,bands[1:]))]
    band=[];teeth=[]
    for c in (0,180):
        for a,b,s,e in ((175.5,183.5,c-45,c+45),(183.5,204.5,c-45,c-43),(183.5,204.5,c+43,c+45)):
            band.append(sector(f'plate-{len(band)}',a,b,s,e,ui.rgb(TONE[39])))
    tooth_bands=[(204.5,39),(210.5,38),(213.5,36),(216.5,35),(233.5,36),(237,33),(244,32),(247.5,32)]
    tooth_stops=[(0,ui.rgb(TONE[39]))]
    for index,(r,tone) in enumerate(tooth_bands[1:]):
        tooth_stops.extend([((r-.08)/248,ui.rgb(TONE[tooth_bands[index][1]])),(r/248,ui.rgb(TONE[tone]))])
    tooth_stops.append((1,ui.rgb(TONE[32])))
    teeth_path=optics.compound('teeth',[sector('tooth',204.5,247.5,k*45-11.25,k*45+11.25,[1,1,1,1])['commands'] for k in range(8)],[1,1,1,1])
    teeth_path['fill']=ui.linear((U*320,U*240),(U*568,U*240),tooth_stops);teeth_path['fill']['type']='radial'
    teeth=[teeth_path]
    sweep=[]
    for a,b,strength in ((161,250,.26),(250,300,.13)):
        stops=[(0,ui.rgb('#5E6E24',0)),(1-67.5/360,ui.rgb('#5E6E24',0))]
        for angle in (60,50,40,30,20,10,7.5,5,2.5):
            amplitude=(1-(angle-7.5)/60)**1.6 if angle>=7.5 else angle/7.5
            stops.append((1-angle/360,ui.rgb('#5E6E24',strength*amplitude)))
        stops.append((1,ui.rgb('#5E6E24',0)))
        p=sector(f'sweep-{len(sweep)}',a,b,-67.5,0,[1,1,1,1]);p['blend']='additive'
        p['fill']=ui.linear((U*320,U*240),(U*320,0),stops);p['fill']['type']='conic';sweep.append(p)
    halo=optics.radial('halo',[(0,.1),(75,.07),(134,.02),(168,0)],'#FF8340','additive')
    # Each turning part sits in a still spin group, so the entrance can wind it
    # in without taking the slow turn's transform from its timeline.
    ring_layers=[optics.art('rings',rings),
                 ui.group('band-spin',{**ui.FULL,'transform':pose()},[optics.art('plate-band',band,transform=ui.rotation(0))]),
                 ui.group('teeth-spin',{**ui.FULL,'transform':pose()},[optics.art('teeth',teeth,transform=ui.rotation(0))]),
                 optics.art('sweep',sweep,transform=ui.rotation(-38),opacity=ui.number(1)),optics.art('halo',halo,opacity=ui.number(.8))]
    # These dim olive edges and the soft light sweep need no per-pixel coverage.
    # Their local triangles can rotate intact; lettering and all bright detail
    # keep analytic antialiasing. The disk and the halo fade into the field and
    # the clamps are shade on shade, so their edges need none either.
    for ident,duration,angle in (('plate-band',180000,-360),('teeth',120000,360),('sweep',10000,322)):
        begin=-38 if ident=='sweep' else 0
        doc.timelines.add(ident+'-turn',duration,[ui.track(ident,'transform',[(0,ui.rotation(begin)),(duration,ui.rotation(angle))])],iterations=0)
        doc.events.setdefault('onInit',[]).append({'op':'playTimeline','timeline':ident+'-turn'})
    doc.timelines.add('halo-breathe',6000,[ui.track('halo','opacity',[(0,ui.number(.4)),(3000,ui.number(1)),(6000,ui.number(.4))])],iterations=0)
    # The source rings sit 25.5 u above the optical center of the 4:3 canvas.
    # Fixed clamps stay above the rotating plates, as in the stock assembly.
    clamps=[]
    for n in range(4):
        clamps.append(sector(f'clamp-{n}',174,211,n*90+41,n*90+49,ui.rgb(TONE[26])))
        clamps.append(sector(f'clamp-edge-{n}',174,211,n*90+41,n*90+42,ui.rgb(TONE[32])))
    for path in band + teeth + sweep + rings + halo + clamps:
        path['antialias'] = False
    ring_layers.append(ui.group('clamp-lock',{**ui.FULL,'transform':pose()},[optics.art('clamps',clamps)]))
    for n in range(14):
        ident=f'ember-{n}';x=130+(n*71)%370;y=310+(n*37)%90;r=.6+(n%4)*.2
        ember=ui.vector(ident,{**ui.absolute(left=U*x,top=U*y,width=U*r*2,height=U*r*2),
            'opacity':ui.number(0),'transform':ui.transform(),'display':ui.keyword('block')},[ui.path('spark',[(U*r,0),(U*r*2,U*r),(U*r,U*r*2),(0,U*r)],fill=ui.solid(ui.rgb('#FFB070')))])
        duration=6000+(n*379)%5000
        doc.timelines.add(ident+'-rise',duration,[ui.track(ident,'transform',[(0,ui.transform()),(duration,ui.transform(ty=-U*(110+n*7)))]),
            ui.track(ident,'opacity',[(0,ui.number(0)),(duration//4,ui.number(.3)),(duration*3//4,ui.number(.5)),(duration,ui.number(0))])],iterations=0)
        doc.events['onInit'].append({'op':'playTimeline','timeline':ident+'-rise'})
        doc.bind(ident+'.display',ident,'display',{'op':'select','args':[{'state':'motion_reduced'},'none','block']})
        ring_layers.append(ember)
    ring_group=ui.group('ring-assembly',{**ui.FULL,'transform':ui.transform(tx=-U,ty=-25.5*U)},ring_layers)
    emblem=ui.path('emblem',[(U*(190+x),U*(125+y)) for x,y in ui.EMBLEM],fill=ui.solid(ui.rgb('#0E1000')))
    # The mechanism scales about the rings' center, which lies 1 u left of and
    # 25.5 u above the canvas center the group scales about.
    mechanism=ui.group('mechanism',{**ui.FULL,'transform':pose()},[ring_group,optics.art('emblem-art',[emblem])])
    letters=wordmark()
    glow=[]
    for n,width,alpha in ((0,18,.016),(1,13,.027),(2,8,.042)):
        for letter in letters:
            p=json.loads(json.dumps(letter));p['id']=f"glow-{n}-{p['id']}";p['fill']=ui.solid(ui.rgb('#FF8340',alpha))
            p['stroke']=ui.stroke(ui.solid(ui.rgb('#FF8340',alpha)),width,join='round');p['blend']='additive';glow.append(p)
    # A moving narrow light is clipped to the fixed vector letter silhouettes.
    beam=ui.vector('glint-beam',{**ui.FULL,'transform':ui.transform(tx=-U*640)},[ui.path('beam',[(0,0),(U*640,0),(U*640,U*480),(0,U*480)],
        fill=ui.linear((0,0),(U*640,0),[(0,[1,1,1,0]),(.47,[1,1,1,0]),(.50,[1,1,1,.5]),(.53,[1,1,1,0]),(1,[1,1,1,0])]))])
    glint=ui.group('wordmark-glint',{**ui.FULL,'display':ui.keyword('block')},[beam]);glint['mask']={'paths':json.loads(json.dumps(letters))}
    for p in glint['mask']['paths']:p['fill']=ui.solid([1,1,1,1])
    doc.bind('glint.display','wordmark-glint','display',{'op':'select','args':[{'state':'motion_reduced'},'none','block']})
    # The first pass crosses the lettering as the clamps lock; then one every 9 s.
    doc.timelines.add('glint-pass',9000,[ui.track('glint-beam','transform',[(0,ui.transform(tx=-U*640)),(940,ui.transform(tx=-U*640)),(1840,ui.transform(tx=U*640)),(9000,ui.transform(tx=U*640))])],iterations=0)
    # The lettering holds still through the entrance. Moving its antialiased
    # glow would compile again at every sub-pixel phase, a group fade's
    # isolated layer compiles it again on entry and exit, and a fade of its own
    # recolors some 50k vertices every frame.
    title=ui.group('wordmark-block',{**ui.FULL},[optics.art('wordmark-halo',glow),optics.art('wordmark',letters),glint])
    status=ui.label('status','#str_104343',{**ui.absolute(left=U*90,top=U*404,width=U*460,height=U*27),**ui.typeface('marine',16,24,ui.rgb('#F09E0D')),
        'text-align':ui.keyword('center'),'opacity':ui.number(1),'transform':ui.transform()})
    doc.bind('status.text','status','text',{'state':'message'})
    # Each step's line rises softly into place instead of replacing the last.
    doc.timelines.add('status-arrive',320,[ui.track('status','opacity',[(0,ui.number(1)),(SNAP,ui.number(0),EASE),(280,ui.number(1))]),
        ui.track('status','transform',[(0,ui.transform()),(SNAP,ui.transform(ty=U*5),ui.EASE_OUT),(320,ui.transform())])])
    # The rule fills left to right behind a marker that glides to the current
    # step. A segment jump (several steps between two repaints) cascades.
    doc.state['shown']={'type':'number','initial':-1}
    segment=ui.path('segment',[(0,0),(U*21,0),(U*21,U),(0,U)],fill=ui.solid(ui.rgb('#F09E0D')))
    segments=[]
    for i in range(7):
        box=ui.absolute(left=U*(236+i*24),top=U*437,width=U*21,height=U)
        segments.append(ui.vector(f'progress-{i}',{**box,'opacity':ui.number(.15)},[segment]))
        done=f'progress-{i}-done'
        segments.append(ui.vector(done,{**box,'opacity':ui.number(.55),'display':ui.keyword('none')},[segment]))
        doc.bind(done+'.display',done,'display',{'op':'select','args':[{'op':'<=','args':[i,{'state':'step'}]},'block','none']})
    marker='progress-marker'
    segments.append(ui.vector(marker,{**ui.absolute(left=U*236,top=U*437,width=U*21,height=U),'opacity':ui.number(.62),
        'transform':ui.transform(),'display':ui.keyword('block')},[segment]))
    doc.bind(marker+'.display',marker,'display',{'op':'select','args':[{'state':'motion_reduced'},'none','block']})
    doc.timelines.add('marker-pulse',800,[ui.track(marker,'opacity',[(0,ui.number(.62)),(400,ui.number(.95)),(800,ui.number(.62))])],iterations=0)
    doc.events['onInit'].append({'op':'playTimeline','timeline':'marker-pulse'})
    arrivals=[]
    for step in range(7):
        doc.timelines.add(f'marker-{step}',440,[ui.track(marker,'transform',[(0,ui.transform(),EASE),(440,ui.transform(tx=U*24*step))])])
        arrivals.append({'op':'if','condition':{'op':'==','args':[{'state':'step'},step]},'then':[{'op':'playTimeline','timeline':f'marker-{step}'}]})
        for shown in range(-1,step):
            tracks=[]
            for i in range(shown+1,step+1):
                delay=70*(i-shown-1)
                keys=[(0,ui.number(.55)),(SNAP,ui.number(0))]+([(delay,ui.number(0))] if delay>SNAP else [])
                tracks.append(ui.track(f'progress-{i}-done','opacity',keys[:-1]+[(keys[-1][0],keys[-1][1],EASE),(delay+300,ui.number(.55))]))
            doc.timelines.add(f'fill-{shown+1}-{step}',70*(step-shown-1)+300,tracks)
            arrivals.append({'op':'if','condition':{'op':'&&','args':[{'op':'==','args':[{'state':'shown'},shown]},{'op':'==','args':[{'state':'step'},step]}]},
                'then':[{'op':'playTimeline','timeline':f'fill-{shown+1}-{step}'}]})
    doc.events['stepChanged']=[{'op':'if','condition':{'op':'!','args':[{'state':'motion_reduced'}]},
        'then':[{'op':'playTimeline','timeline':'status-arrive'},*arrivals]},{'op':'setState','values':{'shown':{'state':'step'}}}]
    # Explicit reduced-motion rest poses: no shortened infinite animations.
    doc.timelines.add('motion-still',1,[ui.track('plate-band','transform',[(0,ui.rotation(0)),(1,ui.rotation(0))]),
        ui.track('teeth','transform',[(0,ui.rotation(0)),(1,ui.rotation(0))]),ui.track('sweep','transform',[(0,ui.rotation(-38)),(1,ui.rotation(-38))]),
        ui.track('halo','opacity',[(0,ui.number(.8)),(1,ui.number(.8))])])
    # Entrance: the mechanism drops from 1.8x to its seat, its gears winding in
    # against each other, settles through a slight undershoot, and the clamps
    # close on it. The halo flares as they lock; the light sweep and status line
    # follow, and the glint crosses the still lettering. onInit starts it for
    # any host; startup plays it again from onEnter once the first, veiled frame
    # (which pays for document and geometry creation) is on screen, so that
    # cost cannot skip its opening.
    veil=ui.vector('veil',{**ui.FULL,'opacity':ui.number(1),'display':ui.keyword('block')},
        [ui.path('black',[(0,0),({'fraction':1},0),({'fraction':1},{'fraction':1}),(0,{'fraction':1})],fill=ui.solid([0,0,0,1]))])
    doc.bind('veil.display','veil','display',{'op':'select','args':[{'state':'motion_reduced'},'none','block']})
    def seat(scale):
        # Keep the rings' center still while the group scales about the canvas center.
        return pose(tx=-U*(1-scale),ty=-25.5*U*(1-scale),s=scale)
    doc.timelines.add('enter',1300,[
        ui.track('veil','opacity',[(0,ui.number(1)),(SNAP,ui.number(1),[.25,.1,.25,1]),(240,ui.number(0))]),
        ui.track('mechanism','transform',[(0,pose()),(SNAP,seat(1.8),[.22,1,.36,1]),(820,seat(.986),[.45,0,.55,1]),(1080,seat(1))]),
        ui.track('teeth-spin','transform',[(0,pose()),(SNAP,pose(rot=-150),[.22,1,.36,1]),(960,pose())]),
        ui.track('band-spin','transform',[(0,pose()),(SNAP,pose(rot=90),[.22,1,.36,1]),(960,pose())]),
        ui.track('clamp-lock','transform',[(0,pose()),(SNAP,pose(s=1.16,rot=-20)),(470,pose(s=1.16,rot=-20),[.3,0,.2,1]),(740,pose(s=.985),EASE),(900,pose())]),
        ui.track('halo','opacity',[(0,ui.number(.8)),(SNAP,ui.number(0)),(560,ui.number(0),EASE),(760,ui.number(1),EASE),(1300,ui.number(.4))]),
        ui.track('sweep','opacity',[(0,ui.number(1)),(SNAP,ui.number(0)),(600,ui.number(0),EASE),(1050,ui.number(1))]),
        ui.track('status-block','opacity',[(0,ui.number(1)),(SNAP,ui.number(0)),(640,ui.number(0),EASE),(960,ui.number(1))]),
        ui.track('status-block','transform',[(0,ui.transform()),(SNAP,ui.transform(ty=U*6)),(640,ui.transform(ty=U*6),ui.EASE_OUT),(1000,ui.transform())])],
        complete='entered')
    entrance=[{'op':'playTimeline','timeline':'enter'},{'op':'playTimeline','timeline':'glint-pass'}]
    doc.events['onInit']=[{'op':'if','condition':{'state':'motion_reduced'},'then':[{'op':'playTimeline','timeline':'motion-still'}],'else':doc.events['onInit']+entrance}]
    doc.events['onEnter']=[{'op':'if','condition':{'op':'!','args':[{'state':'motion_reduced'}]},'then':entrance}]
    doc.events['entered']=[{'op':'if','condition':{'op':'!','args':[{'state':'motion_reduced'}]},'then':[{'op':'playTimeline','timeline':'halo-breathe'}]}]
    status_block=ui.group('status-block',{**ui.FULL,'opacity':ui.number(1),'transform':ui.transform()},[status,*segments])
    center=optics.center('canvas',[mechanism,title,status_block])
    result=doc.build(ui.group('screen',{**ui.FULL,'pointer-events':ui.keyword('none')},[field,center,veil]));result['canvas']['minWidth']=960
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');args=p.parse_args()
    target=ui.PAK0/'guis/loading/initialization.q4ui';text=ui.render(document()).replace('build_retained_screens.py','build_retained_initialization.py')
    if args.check:
        if not target.exists() or target.read_text(encoding='utf-8')!=text.replace('\r\n','\n'): raise SystemExit('stale initialization document')
    else:
        target.write_bytes(text.encode('utf-8'));print(target.relative_to(ui.ROOT))

if __name__=='__main__': main()
