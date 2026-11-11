#!/usr/bin/env python3
"""Author the native weapon wheels (visual design 14.6 and 14.16).

Symbols are the atlas's first-party reconstructions, never retail textures.
All inventory, selection, placement and ammo values come from the player.
"""
from __future__ import annotations
import argparse
import json
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path
import build_retained_screens as ui

ROOT = Path(__file__).resolve().parents[2]
ICONS = json.loads(Path(__file__).with_name('weapon_wheel_icons.json').read_text(encoding='utf-8'))['icons']
ROSTER = ['blaster','machinegun','shotgun','hyperblaster','grenadelauncher','nailgun','rocketlauncher','railgun','lightninggun','darkmattergun','napalmgun']
COLORS = ['#DDEBC3','#FFFF00','#FF8C00','#0073FF','#338F12','#99CCCC','#FF4000','#00FF00','#FFFF99','#C433FF','#FFBF40']
S = lambda key: {'state': key}
op = lambda name, *args: {'op': name, 'args': list(args)}
choose = lambda condition, yes, no: op('select', condition, yes, no)


def at(r, a):
    return (280+r*math.sin(a), 280-r*math.cos(a))


def arc(r, a, b):
    return [at(r, a+(b-a)*i/16) for i in range(17)]


def sector(i, n, inner=104, outer=232, gap=6, strogg=False):
    a=i*math.tau/n; span=math.tau/n; l=a-span/2; r=a+span/2
    d0=math.asin(gap/2/inner); d1=math.asin(gap/2/outer)
    if strogg:
        t=-.07
        return [at(inner,l+d0),at(outer,l+d1+t),at(outer+10,l+d1+t+17/outer),
                at(outer+10,l+.42*span+t),at(outer,l+.42*span+t+17/outer),
                at(outer,r-d1+t-14/outer),at(outer-14,r-d1+t),at(inner,r-d0),
                at(inner,a+.045),at(inner+8,a),at(inner,a-.045)]
    return arc(outer,l+d1,r-d1)+arc(inner,r-d0,l+d0)


def shape(ident, pts, tint, width=None, blend=None, closed=True):
    return ui.path(ident,pts,fill=ui.solid(tint) if width is None else None,
                   stroke=ui.stroke(ui.solid(tint),width,join='round') if width else None,
                   blend=blend,closed=closed)


def circle(ident, radius, tint, width=None):
    result=shape(ident,[],tint,width)
    k=radius*.552284749831
    result['commands']=[{'id':'start','op':'move','points':[[280+radius,280]]}]
    for i,pts in enumerate([[(280+radius,280+k),(280+k,280+radius),(280,280+radius)],
        [(280-k,280+radius),(280-radius,280+k),(280-radius,280)],
        [(280-radius,280-k),(280-k,280-radius),(280,280-radius)],
        [(280+k,280-radius),(280+radius,280-k),(280+radius,280)]]):
        result['commands'].append({'id':'arc'+str(i),'op':'cubic','points':pts})
    result['commands'].append({'id':'close','op':'close'})
    return result


def art(ident, paths, **props):
    return ui.vector(ident, {**ui.FULL,'opacity':ui.number(1),'display':ui.keyword('block'),**props}, paths)


def svg_commands(data):
    """Convert the authored absolute SVG subset, retaining cubic curves.

    Elliptical arcs become <=90 degree cubics, so the native vector renderer
    still controls flattening at the final display density.
    """
    tokens=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?',data)
    result=[]; pos=0; cmd=''; x=y=sx=sy=0
    def emit(kind,*pts): result.append({'id':'p'+str(len(result)),'op':kind,**({'points':[[round(a,5),round(b,5)] for a,b in pts]} if pts else {})})
    while pos<len(tokens):
        if tokens[pos].isalpha(): cmd=tokens[pos];pos+=1
        if cmd=='Z': emit('close');x,y=sx,sy;cmd='';continue
        count={'M':2,'L':2,'H':1,'V':1,'C':6,'Q':4,'A':7}[cmd]
        v=list(map(float,tokens[pos:pos+count]));pos+=count
        if cmd in ('M','L'):
            x,y=v;emit('move' if cmd=='M' else 'line',(x,y))
            if cmd=='M': sx,sy=x,y;cmd='L'
        elif cmd=='H': x=v[0];emit('line',(x,y))
        elif cmd=='V': y=v[0];emit('line',(x,y))
        elif cmd in ('C','Q'):
            emit('cubic' if cmd=='C' else 'quadratic',*zip(v[::2],v[1::2]));x,y=v[-2:]
        elif cmd=='A':
            rx,ry,rot,large,sweep,ex,ey=v;rx,ry=abs(rx),abs(ry)
            if rx==0 or ry==0: emit('line',(ex,ey));x,y=ex,ey;continue
            phi=math.radians(rot);c,s=math.cos(phi),math.sin(phi)
            dx,dy=(x-ex)/2,(y-ey)/2;xp,yp=c*dx+s*dy,-s*dx+c*dy
            scale=max(1,math.sqrt(xp*xp/(rx*rx)+yp*yp/(ry*ry)));rx*=scale;ry*=scale
            den=rx*rx*yp*yp+ry*ry*xp*xp
            f=math.sqrt(max(0,(rx*rx*ry*ry-den)/den)) if den else 0
            if bool(large)==bool(sweep): f=-f
            cxp,cyp=f*rx*yp/ry,-f*ry*xp/rx
            cx,cy=c*cxp-s*cyp+(x+ex)/2,s*cxp+c*cyp+(y+ey)/2
            start=math.atan2((yp-cyp)/ry,(xp-cxp)/rx);end=math.atan2((-yp-cyp)/ry,(-xp-cxp)/rx)
            delta=(end-start)%math.tau
            if not sweep: delta-=math.tau
            steps=max(1,math.ceil(abs(delta)/(math.pi/2)))
            def pt(a): return (cx+c*rx*math.cos(a)-s*ry*math.sin(a),cy+s*rx*math.cos(a)+c*ry*math.sin(a))
            def tangent(a): return (-c*rx*math.sin(a)-s*ry*math.cos(a),-s*rx*math.sin(a)+c*ry*math.cos(a))
            for k in range(steps):
                a=start+delta*k/steps;b=start+delta*(k+1)/steps;f=4/3*math.tan((b-a)/4)
                p0,p1=pt(a),pt(b);t0,t1=tangent(a),tangent(b)
                emit('cubic',(p0[0]+f*t0[0],p0[1]+f*t0[1]),(p1[0]-f*t1[0],p1[1]-f*t1[1]),p1)
            x,y=ex,ey
    return result


def icon(name, tint):
    data=ICONS[name]; _,_,w,h=map(float,data['vb'].split());paths=[]
    for e in ET.fromstring('<svg>'+data['svg']+'</svg>').iter('path'):
        path={'id':'symbol-'+str(len(paths)),'commands':svg_commands(e.attrib['d']),
              'fillRule':e.get('fill-rule','nonzero')}
        if e.get('fill') != 'none':
            path['fill']=ui.solid(tint[:3]+[tint[3]*float(e.get('fill-opacity','1'))])
        if e.get('stroke', 'none') != 'none':
            path['stroke']=ui.stroke(ui.solid(tint[:3]+[tint[3]*float(e.get('stroke-opacity','1'))]),
                                     float(e.get('stroke-width','1')), minimum=0,
                                     join=e.get('stroke-linejoin','miter'), cap=e.get('stroke-linecap','butt'))
        paths.append(path)
    return paths,w,h


def dashed(ident, points, tint, pattern=(5, 4), width=1, closed=True):
    """Explicit dash geometry: the native stroke format has no dash array."""
    paths=[]; phase=0; remaining=pattern[0]; drawing=True
    route=points+[points[0]] if closed else points
    for start,end in zip(route,route[1:]):
        dx,dy=end[0]-start[0],end[1]-start[1]; length=math.hypot(dx,dy); used=0
        while used < length-1e-6:
            take=min(remaining,length-used)
            if drawing:
                paths.append(shape(ident+str(len(paths)),[(start[0]+dx*used/length,start[1]+dy*used/length),
                    (start[0]+dx*(used+take)/length,start[1]+dy*(used+take)/length)],tint,width,closed=False))
            used+=take; remaining-=take
            if remaining < 1e-6:
                phase=(phase+1)%len(pattern); remaining=pattern[phase]; drawing=phase%2==0
    return paths


def wheel_palette(doc):
    """The Marine family remains Marine in MP; only the team accents change."""
    team=choose(S('mp'),S('team'),-1)
    return [('olive',op('<',team,0),'#B3D06E','#B2CC80','#616F26',op('!',S('mp'))),
            ('team0',op('==',team,0),'#AAE355','#AAE355','#616F26',False),
            ('team1',op('==',team,1),'#FF8E00','#FF8E00','#6B3A08',False)]


def document(strogg=False, awakening=False, chat=False):
    from remastered_hud_base import HudDocument, shear_group
    family='strogg' if strogg else 'marine'; variant=family+('_awakening' if awakening else '')
    doc=HudDocument('openq4.quick-chat') if chat else ui.Document('openq4.weapon-wheel.'+variant)
    roster=ROSTER[:10]+['corecannon','napalmgun','freezegun'] if awakening else ROSTER
    colors=COLORS[:10]+['#FF5AC8','#FFBF40','#9EEBFF'] if awakening else COLORS
    if chat: roster=roster[:len(PHRASES)]
    n=len(roster); span=math.tau/n; opening=[]; closing=[]; selection=[]; loops=[]
    for key,kind,initial in [('blend','number',0),('offset','number',0),('fit','number',1),('selected','number',0),
        ('previous','number',0),('mp','boolean',False),('team','number',-1),('weaponName','string',''),('ammo','string','')]:
        doc.state[key]={'type':kind,'initial':initial}
    doc.state['reduced']={'type':'boolean','initial':False,'cvar':'ui_retainedReducedMotion'}
    doc.state['flashing']={'type':'boolean','initial':False,'cvar':'ui_reducedFlashing'}
    if chat:
        doc.state['selected']['initial']=-1;doc.state['mp']['initial']=True
        doc.state['channel']={'type':'number','initial':0};doc.state['zoomKey']={'type':'string','initial':''}
    palette=wheel_palette(doc)

    def show(ident,condition): doc.bind(ident+'.show',ident,'display',choose(condition,'block','none'))
    def full(ident,children,**props): return ui.group(ident,{**ui.FULL,'display':ui.keyword('block'),**props},children)
    def masked(ident,children,paths,**props):
        node=full(ident,children,**props);node['mask']={'paths':paths};return node
    def loop(ident,duration,tracks):
        doc.timelines.add(ident,duration,tracks,iterations=0);loops.append({'op':'playTimeline','timeline':ident})
    def themes(ident,make):
        nodes=[]
        for suffix,condition,line,edge,light,sp in palette:
            node=make(ident+'-'+suffix,line,edge,light,sp);nodes.append(node);show(node['id'],condition)
        return full(ident,nodes)
    def plain_text(ident,value,x,y,size,tint,face='chain',width=76,height=None):
        node=ui.label(ident,'#str_230007',{**ui.absolute(left=x-width/2,top=y-size*.6,width=width,height=height or size*1.5),
            **ui.typeface(face,size,size*1.3,ui.rgb(tint)),'text-align':ui.keyword('center'),'white-space':ui.keyword('normal'),
            'display':ui.keyword('block'),'opacity':ui.number(1),'transform':ui.transform()})
        doc.bind(ident+'.text',ident,'text',value);return node
    def strogg_label(ident,value,x,y,size,tint,translation,delay,duration,width=76,height=None,white=False):
        height=height or size*1.5
        parts=ui.strogg_text(ident,'#str_230007',ui.absolute(left=0,top=0,width=width,height=height),size,size*1.3,ui.rgb(tint),
            **{'text-align':ui.keyword('center'),'white-space':ui.keyword('normal')})
        for node in parts: doc.bind(node['id']+'.text',node['id'],'text',value)
        translation.add(ident,delay,duration,rest=ui.rgb(tint),white=white)
        return shear_group(ident+'-slant',x-width/2,y-size*.6,width,height,math.tan(math.radians(11.4)),parts,opacity=ui.number(1))
    def round_plate(ident,i,outer):
        # Filled fillets keep one translucent surface. Fill plus an
        # overlapping round stroke would make its corners too opaque.
        points=sector(i,n,outer=outer);corners=[]
        for j,p in enumerate(points):
            before,after=points[j-1],points[(j+1)%len(points)]
            va=(before[0]-p[0],before[1]-p[1]);vb=(after[0]-p[0],after[1]-p[1])
            la,lb=math.hypot(*va),math.hypot(*vb);ua=(va[0]/la,va[1]/la);ub=(vb[0]/lb,vb[1]/lb)
            angle=math.acos(max(-1,min(1,ua[0]*ub[0]+ua[1]*ub[1])))
            reach=min(la*.5,lb*.5,2/max(.0001,math.tan(angle*.5)))
            corners.append(((p[0]+ua[0]*reach,p[1]+ua[1]*reach),p,(p[0]+ub[0]*reach,p[1]+ub[1]*reach)))
        path=shape(ident,[],[0,0,0,1]);commands=[]
        for j,(start,corner,end) in enumerate(corners):
            commands.append({'id':f'edge{j}','op':'move' if j==0 else 'line','points':[ui.point(*start)]})
            commands.append({'id':f'corner{j}','op':'quadratic','points':[ui.point(*corner),ui.point(*end)]})
        commands.append({'id':'close','op':'close'});path['commands']=commands;return path

    wash=circle('wash',280,[0,0,0,1]);wash['fill']=ui.linear((280,280),(560,280),
        [(0,[0,0,0,.35]),(.82,[0,0,0,.35]),(1,[0,0,0,0])]);wash['fill']['type']='radial'
    wheel=[art('wash',[wash])]
    if not strogg:
        def bezel(ident,line,edge,light,sp):
            ticks=[shape('tick'+str(k),[at(247,k*math.pi/60),at(252,k*math.pi/60)],ui.rgb(line,.18),.9,closed=False) for k in range(120)]
            ticks += [shape('boundary'+str(i),[at(242,(i+.5)*span),at(253,(i+.5)*span)],ui.rgb(line,.5),1.4,closed=False) for i in range(n)]
            return art(ident,ticks)
        wheel.append(themes('bezel',bezel))

    translate=ui.Translation(); translate_select=ui.Translation()
    order=[i for i in [5,0,8,3,10,1,6,9,2,7,4,11,12] if i<n]
    flash_nodes=[]; circuits=[]; circuit_tracks=[]
    for i,name in enumerate(roster):
        key=f'slot{i}'; a=i*span; owned=S(key+'.owned'); sel=op('==',S('selected'),i)
        empty=op('==',S(key+'.count'),0); finite=op('>=',S(key+'.count'),0)
        for suffix,kind,initial in [('owned','boolean',False),('count','number',0),('fill','number',0),('reserve','string',''),('index','string','')]:
            doc.state[key+'.'+suffix]={'type':kind,'initial':initial}
        if chat:
            doc.state[key+'.owned']['initial']=True;doc.state[key+'.count']['initial']=-1
        outline=dashed('outline',sector(i,n,strogg=True),ui.rgb('#F59512',.16)) if strogg else [shape('outline',sector(i,n),[1,1,1,.15],1)]
        nodes=[art(key+'-outline',outline)];show(key+'-outline',op('!',owned))
        plates=[]
        for enlarged,outer in [(False,232),(True,240)]:
            ident=key+('-plate-selected' if enlarged else '-plate-rest')
            p=shape('plate',sector(i,n,outer=outer,strogg=True),[0,0,0,1]) if strogg else round_plate('plate',i,outer)
            plates.append(art(ident,[p],opacity=ui.number((.72 if enlarged else .55) if strogg else (.7 if enlarged else .5))))
            show(ident,sel if enlarged else op('!',sel))
        nodes.append(full(key+'-plate',plates));show(key+'-plate',owned)
        def highlight(ident,line,edge,light,sp):
            paths=[]
            if not strogg: paths.append(shape('light',sector(i,n,inner=106,outer=238,gap=10),ui.rgb(light,.35),blend='additive'))
            paths.append(shape('edge',sector(i,n,outer=240,strogg=strogg),ui.rgb(edge),2.4))
            return art(ident,paths)
        selected=highlight(key+'-selected','','#F2AD0B','',0) if strogg else themes(key+'-selected',highlight)
        nodes.append(selected);show(selected['id'],op('&&',owned,sel))

        if strogg:
            # Every plate owns a masked grain field; its source is authored
            # deterministic vector noise, independent of retail textures.
            silhouette=sector(i,n,outer=240,strogg=True);xs,ys=zip(*silhouette)
            dots=[]
            for k in range(135):
                gx=min(xs)-30+((k*73+i*19)%997)/997*(max(xs)-min(xs)+60)
                gy=min(ys)-120+((k*127+i*43)%991)/991*(max(ys)-min(ys)+240)
                dots.append(shape('g'+str(k),[(gx,gy),(gx+.8,gy),(gx+.8,gy+.8),(gx,gy+.8)],ui.rgb('#FFC066',.3+(k%4)*.12),blend='additive'))
            grain=art(key+'-grain',dots,transform=ui.transform())
            grain_mask=masked(key+'-grain-mask',[grain],[shape('mask',silhouette,[1,1,1,1])],opacity=ui.number(.2))
            nodes.append(grain_mask);show(grain_mask['id'],owned)
            loop(key+'-grain-run',2000,[ui.track(grain['id'],'transform',[(0,ui.transform()),(2000,ui.transform(tx=-30,ty=120))])])
            opening.append(held(grain_mask['id'],'opacity',[(0,ui.number(.6)),(250,ui.number(.2))]))
            fl=art(key+'-boot-flash',[shape('flash',silhouette,ui.rgb('#FFD06F'),blend='additive')],opacity=ui.number(0))
            wrap=full(key+'-boot-flash-safe',[fl]);show(wrap['id'],op('&&',owned,op('!',S('flashing'))));nodes.append(wrap)
            delay=order.index(i)*12; close_delay=list(reversed(order)).index(i)*8
            opening.append(held(fl['id'],'opacity',[(0,ui.number(0))]+([(delay,ui.number(0))] if delay else [])+[(delay+1,ui.number(.9)),(delay+150,ui.number(0))]))
            closing.append(held(fl['id'],'opacity',[(0,ui.number(0))]+([(close_delay,ui.number(0))] if close_delay else [])+[(close_delay+1,ui.number(.8)),(close_delay+60,ui.number(0))]))
            # Circuits reveal from the core outward, each leg independently.
            points=[at(88,a),at(95,a),at(99,a+.1),at(99,a+.1)]
            leaves=[]
            for k,(p,q) in enumerate(zip(points,points[1:])):
                ident=f'{key}-circuit{k}'
                leaves.append(art(ident,[shape('idle',[p,q],ui.rgb('#F59512',.32),1,closed=False)],opacity=ui.number(1)))
                bright=art(ident+'-lit',[shape('lit',[p,q],ui.rgb('#FFCC00',.95),1.6,closed=False)]);show(bright['id'],sel)
                leaves[-1]=full(ident+'-part',[leaves[-1],bright],opacity=ui.number(1))
                circuit_tracks.append(held(ident+'-part','opacity',[(0,ui.number(0)),(40+k*50,ui.number(0)),(90+k*50,ui.number(1))]))
            x,y=points[-1];leaves.append(art(key+'-circuit-end',[shape('node',[(x-2.2,y-2.2),(x+2.2,y-2.2),(x+2.2,y+2.2),(x-2.2,y+2.2)],ui.rgb('#F59512',.4),1)]))
            circuit=full(key+'-circuit',leaves);show(circuit['id'],owned);circuits.append(circuit)
        else:
            def ring_flash(ident,line,edge,light,sp):
                # Team games flash in the team's own text colour.
                col='#FFFFFF' if ident.endswith('olive') else edge
                return art(ident,[shape('flash',sector(i,n,outer=240),ui.rgb(col),2.4)])
            flash=themes(key+'-selection-flash',ring_flash);show(flash['id'],op('&&',owned,sel));flash_nodes.append(flash)

        content=[]; label_angle=a-.035 if strogg else a
        if strogg:
            x,y=at(121,a+.14)
            node=strogg_label(key+'-index',S(key+'.index'),x,y,11,'#FCFFC8',translate,140+i*18,90)
            doc.bind(key+'.index.alpha',node['id'],'opacity',choose(owned,.5,.2));nodes.append(node)
        else:
            x,y=at(118,a);node=plain_text(key+'-index',S(key+'.index'),x,y,12,'#FFFFFF','lowpixel');nodes.append(node)
            team_color=[choose(op('==',S('team'),0),ui.rgb('#AAE355')[c],ui.rgb('#FF8E00')[c]) for c in range(4)]
            team_selected=op('&&',owned,op('&&',sel,op('&&',S('mp'),op('>=',S('team'),0))))
            white=[1,1,1,choose(owned,.55,.15)]
            doc.bind(key+'.index.color',node['id'],'color',[choose(team_selected,team_color[c],white[c]) for c in range(4)])
        ix,iy=at(182 if strogg else 158,label_angle); active=op('&&',sel,op('!',empty))
        for suffix,col in [('idle',ui.rgb('#F59512' if strogg else '#A8A360')),('lit',ui.rgb('#FFCC00' if strogg else '#FF8000')),('mp',ui.rgb('#00D9FF' if i==0 else colors[i]))]:
            paths,w,h=icon('gauntlet' if suffix=='mp' and i==0 else name,col);sz=26 if strogg else 24
            for path in paths:
                for command in path['commands']:
                    if 'points' in command: command['points']=[[x*sz/h,y*sz/h] for x,y in command['points']]
                if 'stroke' in path: path['stroke']['widthDp']*=sz/h
            ident=key+'-icon-'+suffix;content.append(ui.vector(ident,{**ui.absolute(left=ix-w*sz/h/2,top=iy-sz/2,width=w*sz/h,height=sz),'opacity':ui.number(1)},paths))
            normal=choose(active,1 if suffix=='lit' else 0,.6 if suffix=='idle' else 0)
            doc.bind(ident,ident,'opacity',choose(S('mp'),choose(active,1,.6) if suffix=='mp' else 0,normal))
        x,y=at(142 if strogg else 193,label_angle)
        if strogg:
            ammo=strogg_label(key+'-reserve',S(key+'.reserve'),x,y,17,'#FCFFC8',translate,140+i*18,90,white=True)
            # The transient colour track belongs to the label; an empty
            # readout has its own red copy after the translation settles.
            red=plain_text(key+'-reserve-empty',S(key+'.reserve'),x,y,17,'#C72C1B','r_strogg')
            show(red['id'],empty);content.append(red);show(ammo['id'],op('!',empty))
        else:
            ammo=plain_text(key+'-reserve',S(key+'.reserve'),x,y,17,'#DDEBC3')
            doc.bind(key+'.reserve.color',ammo['id'],'color',[choose(empty,ui.rgb('#C72C1B')[c],ui.rgb('#DDEBC3')[c]) for c in range(4)])
        content.append(ammo)
        band=[]
        if strogg:
            lo=a-span/2-.07+.05; hi=a+span/2-.07-.12; cw=(hi-lo)/8
            for cell in range(8):
                b0=lo+cell*cw+cw*.12;b1=lo+(cell+1)*cw-cw*.12;sk=cw*.35
                pts=[at(206,b0),at(206,b1),at(217,b1+sk),at(217,b0+sk)]
                band.append(art(f'{key}-track{cell}',[shape('track',pts,ui.rgb('#FF9000',.14))]))
                ident=f'{key}-cell{cell}';band.append(art(ident,[shape('fill',pts,ui.rgb('#FF9000',.8))]))
                doc.bind(ident,ident,'opacity',choose(op('>=',op('*',S(key+'.fill'),8),cell+.5),1,0))
        else:
            lo=a-span/2+math.asin(8/207);hi=a+span/2-math.asin(8/207)
            band_pts=arc(221,lo,hi)+arc(207,hi,lo)
            band.append(art(key+'-track',[shape('track',band_pts,ui.rgb(colors[i],.2))]))
            fill=art(key+'-band-fill',[shape('fill',band_pts,ui.rgb(colors[i],.35)),
                shape('bevel',arc(219.8,lo+.006,hi-.006)+arc(208.2,hi-.006,lo+.006),ui.rgb(colors[i],.45),2.4)],transform=ui.transform())
            doc.bind(key+'.band.fill',fill['id'],'transform',[0,0,1,1,op('*',op('-',S(key+'.fill'),1),math.degrees(hi-lo))])
            show(fill['id'],op('>',S(key+'.fill'),0))
            band.append(masked(key+'-band-mask',[fill],[shape('mask',band_pts,[1,1,1,1])]))
            dividers=[]
            for cell in range(1,8):
                q=lo+(hi-lo)*cell/8
                dividers.append(shape('divider'+str(cell),arc(221,q-.003,q+.003)+arc(207,q+.003,q-.003),[0,0,0,.32]))
            band.append(art(key+'-dividers',dividers))
        gauge=full(key+'-band',band,transform=ui.transform());content.append(gauge);show(gauge['id'],finite)
        doc.bind(key+'.band.extension',gauge['id'],'transform',[choose(sel,8*math.sin(a),0),choose(sel,-8*math.cos(a),0),1,1,0])
        slash=art(key+'-empty',[shape('circle',[(ix+17*math.sin(j*math.tau/64),iy-17*math.cos(j*math.tau/64)) for j in range(64)],ui.rgb('#A50000'),4),
            shape('slash',[(ix-12,iy+12),(ix+12,iy-12)],ui.rgb('#A50000'),4,closed=False)])
        content.append(slash);doc.bind(key+'.empty',slash['id'],'opacity',choose(empty,1,0))
        contents=full(key+'-contents',content,opacity=ui.number(1));nodes.append(contents);doc.bind(key+'.contents',contents['id'],'opacity',0 if chat else choose(owned,1,0))
        if chat:
            show(key+'-index',False)
            x,y=at(168,a);ident=f'phrase{i}-text'
            phrase=ui.label(ident,PHRASES[i],{'position':ui.keyword('relative'),'display':ui.keyword('block'),'width':ui.length(106),
                **ui.typeface('lowpixel',16,19,ui.rgb('#DDEBC3')),'text-align':ui.keyword('center'),'white-space':ui.keyword('normal'),'opacity':ui.number(1)})
            doc.bind(ident+'.opacity',ident,'opacity',choose(sel,1,.6))
            nodes.append(ui.group(f'phrase{i}-label',{**ui.absolute(left=x-56,top=y-24,width=112,height=48),'display':ui.keyword('flex'),
                'flex-direction':ui.keyword('column'),'justify-content':ui.keyword('center'),'align-items':ui.keyword('center')},[phrase]))
        notch=art(key+'-previous',[shape('notch',[at(101,a),at(91,a-.05),at(91,a+.05)],ui.rgb('#FCFFC8' if strogg else '#DDEBC3',.7))]);nodes.append(notch)
        show(notch['id'],False if chat else op('&&',owned,op('==',S('previous'),i)))
        entry=full(key+'-entry',nodes,opacity=ui.number(1),transform=ui.transform());wheel.append(entry)
        delay=order.index(i)*12 if strogg else i*8
        opening.append(held(entry['id'],'opacity',[(0,ui.number(0))]+([(delay,ui.number(0))] if delay else [])+[(delay+(1 if strogg else 90),ui.number(1))]))
        close_delay=list(reversed(order)).index(i)*8 if strogg else 0
        closing.append(held(entry['id'],'opacity',[(0,ui.number(1))]+([(close_delay,ui.number(1))] if close_delay else [])+[(close_delay+60,ui.number(0))]))
        if not strogg:
            opening.append(held(entry['id'],'transform',[(0,ui.transform(sx=.9,sy=.9))]+([(delay,ui.transform(sx=.9,sy=.9))] if delay else [])+[(delay+90,ui.transform())]))

    if strogg:
        opening+=circuit_tracks
        wheel.append(full('circuits',circuits))
        rings=[]
        for radius,duration,tint,alpha,pattern,turn in [(93,5000,'#F59512',.38,(14,6,3,6),360),(99,2000,'#FFCC00',.24,(2,10),-360)]:
            ident='orbit'+str(radius)
            rings.append(art(ident,dashed('dash',[at(radius,j*math.tau/360) for j in range(360)],ui.rgb(tint,alpha),pattern,1.3),transform=ui.transform()))
            loop(ident+'-run',duration,[ui.track(ident,'transform',[(0,ui.rotation(0)),(duration,ui.rotation(turn))])])
        wheel.append(full('orbits',rings,transform=ui.transform(),opacity=ui.number(1)))
        opening += [held('orbits','transform',[(0,ui.rotation(-120)),(300,ui.rotation(0),ui.EASE_OUT)]),held('orbits','opacity',[(0,ui.number(0)),(120,ui.number(1))])]
        closing.append(held('orbits','opacity',[(0,ui.number(1)),(40,ui.number(1)),(160,ui.number(0))]))
        octagon=[at(84,j*math.tau/8+math.pi/8) for j in range(8)]
        paths=[]
        for ident,pts in [('core',octagon),('tab',[(260,358),(300,358),(293,372),(267,372)])]:
            path=shape(ident,pts,[0,0,0,.68]);path['stroke']=ui.stroke(ui.solid(ui.rgb('#F59512',.5)),1.2);paths.append(path)
        hub=full('hub',[art('hub-art',paths)],transform=ui.transform(),opacity=ui.number(1));wheel.append(hub)
        # The bar follows the available name slot; its bright head rests
        # twelve dp beyond the text box without leaking outside the core.
        bar=ui.scan_bar('hub-scan',26,ui.absolute(left=0,top=0,height=26))
        bar_slot=ui.group('hub-scan-slot',ui.absolute(left=196,top=251,width=165,height=26),[bar])
        hub['children'].append(masked('hub-scan-mask',[bar_slot],[shape('mask',octagon,[1,1,1,1])]))
        translate.bar('hub-scan',160,90);translate_select.bar('hub-scan',40,90)
        for ident,value,y,size,col,delay,duration in [('name',S('weaponName'),258,12.5,'#FFCC00',160,300),('ammo',S('ammo'),300,21,'#FCFFC8',175,150)]:
            node=strogg_label('hub-'+ident,value,280,y,size,col,translate,delay,duration,width=138,height=34,white=ident=='ammo');hub['children'].append(node)
            translate_select.add('hub-'+ident,40 if ident=='name' else 55,duration,rest=ui.rgb(col),white=ident=='ammo')
        opening.append(held('hub','opacity',[(0,ui.number(0)),(120,ui.number(1))]))
        closing.append(held('hub','opacity',[(0,ui.number(1)),(40,ui.number(1)),(160,ui.number(0))]))
        t,end=translate.tracks(0);opening+=t
        t,select_end=translate_select.tracks(0);selection+=t
        pointer=art('pointer',[shape('arrow',[at(100,0),at(90,-.07),at(93,0),at(90,.07)],ui.rgb('#FFCC00'))],transform=ui.transform());wheel.append(pointer)
        doc.bind('pointer.rotation',pointer['id'],'transform',[0,0,1,1,op('*',S('selected'),360/n)])
        open_end=max(end,300);close_end=max(160,(n-1)*8+60)
    else:
        hub=full('hub',[art('hub-art',[circle('disc',86,[0,0,0,.55])])],transform=ui.transform());wheel.append(hub)
        bands=[]
        for k in range(28):
            y=60+k*18;shift=(k*37)%59-30
            bands.append(shape('band'+str(k),[(180+shift,y),(365,y+2),(375-shift,y+5),(198,y+4)],[1,1,1,.45+(k%3)*.2]))
        torn=art('hub-torn-art',bands,transform=ui.transform())
        torn_wrap=full('hub-torn',[torn],opacity=ui.number(.06))
        lines=art('hub-lines',[shape('line'+str(k),[(180,180+k*2),(380,180+k*2)],ui.rgb('#FFFFFF',.11),.65,blend='additive',closed=False) for k in range(102)],transform=ui.transform())
        hub['children'].append(masked('hub-screen',[torn_wrap,lines],[circle('mask',79,[1,1,1,1])]))
        loop('hub-torn-run',1000,[ui.track(torn['id'],'transform',[(0,ui.transform()),(1000,ui.transform(ty=180))])])
        loop('hub-lines-run',3600,[ui.track(lines['id'],'transform',[(0,ui.transform()),(3600,ui.transform(ty=2))])])
        def rings(ident,line,edge,light,sp): return art(ident,[circle('ring'+str(r),r,ui.rgb(line,.35),1.1) for r in (80,86)])
        hub['children'].append(themes('hub-rings',rings))
        def compass(ident,line,edge,light,sp):
            paths=[]
            for k in range(16):
                x,y=at(93,k*math.tau/16);r=2.6 if k==0 else 1.6
                col=('#FF8000' if k==0 else line) if ident.endswith('olive') else (edge if k==0 else line)
                paths.append(shape('dot'+str(k),[(x+r*math.sin(j*math.tau/16),y-r*math.cos(j*math.tau/16)) for j in range(16)],ui.rgb(col,1 if k==0 else .35)))
            return art(ident,paths)
        compass_node=themes('compass',compass);compass_node['properties']['transform']=ui.transform();hub['children'].append(compass_node)
        # Olive is the neutral multiplayer selection; SP alone uses orange.
        for suffix,condition,line,edge,light,sp in palette:
            if suffix!='olive': continue
            for node in compass_node['children']:
                if node['id']=='compass-olive': node['paths'][0]['fill']=ui.solid(ui.rgb(edge))
        x,y=at(93,0)
        sp_dot=art('compass-sp-dot',[shape('dot',[(x+2.6*math.sin(j*math.tau/16),y-2.6*math.cos(j*math.tau/16)) for j in range(16)],ui.rgb('#FF8000'))])
        show(sp_dot['id'],op('!',S('mp')));compass_node['children'].append(sp_dot)
        doc.bind('compass.rotation','compass','transform',[0,0,1,1,op('*',S('selected'),360/n)])
        def pointer_art(ident,line,edge,light,sp):
            nodes=[]
            for suffix,color,condition in [('mp',edge,op('!',sp)),('sp','#FF8000',sp)]:
                child=art(ident+'-'+suffix,[shape('arrow',[at(98,0),at(84,-.1),at(84,.1)],ui.rgb(color))]);show(child['id'],condition);nodes.append(child)
            return full(ident,nodes)
        pointer=themes('pointer',pointer_art);pointer['properties']['transform']=ui.transform();hub['children'].append(pointer)
        if chat: show('pointer',op('>=',S('selected'),0))
        doc.bind('pointer.rotation','pointer','transform',[0,0,1,1,op('*',S('selected'),360/n)])
        name=plain_text('hub-name',S('weaponName'),280,257,12,'#B0C891','marine',138,34)
        if chat:
            from remastered_hud_mp import glyph_chip
            doc.bindings=[binding for binding in doc.bindings if binding['id']!='hub-name.text']
            words=[]
            for suffix,key,col,condition in [('all',ALL_KEY,'#FFFF8D',op('==',S('channel'),0)),
                ('team0',TEAM_KEY,'#AAE355',op('&&',op('==',S('channel'),1),op('==',S('team'),0))),
                ('team1',TEAM_KEY,'#FF8E00',op('&&',op('==',S('channel'),1),op('==',S('team'),1)))]:
                word=ui.label('hub-'+suffix,key,{**ui.absolute(left=211,top=258,width=138,height=24),**ui.typeface('marine',16,20,ui.rgb(col)),
                    'display':ui.keyword('block'),'text-align':ui.keyword('center')});show(word['id'],condition);words.append(word)
            name=full('hub-name',words,opacity=ui.number(1),color=ui.colour(ui.rgb('#B0C891')))
            chip=glyph_chip(doc,'hub',S('zoomKey'),h=16/1.5,size=6.4,cut=8/3,pad=(8/3,8/3),gap=10/3,min_width=16/1.5)
            verb=ui.label('hub-switch',CHANNEL_KEY,{'position':ui.keyword('relative'),'display':ui.keyword('block'),
                **ui.typeface('lowpixel',12,16,ui.rgb('#B0C891')),'white-space':ui.keyword('nowrap')})
            hint=ui.group('hub-hint',{**ui.absolute(left=200,top=288,width=160,height=16),'display':ui.keyword('flex'),
                'flex-direction':ui.keyword('row'),'justify-content':ui.keyword('center'),'align-items':ui.keyword('center')},[chip,verb])
            doc.bind('hub.hint','hub-hint','display',choose(op('>=',S('team'),0),'flex','none'));hub['children'].append(hint)
        name_clip=ui.group('hub-name-clip',{**ui.absolute(left=0,top=247,width=560,height=36),'overflow':ui.keyword('hidden')},[
            ui.group('hub-name-unclip',ui.absolute(left=0,top=-247,width=560,height=560),[name])])
        hub['children'] += [name_clip,plain_text('hub-ammo',S('ammo'),280,300,21,'#DDEBC3',width=138,height=34)]
        flashes=full('selection-flash',flash_nodes,opacity=ui.number(0));safe_flash=full('selection-flash-safe',[flashes]);show(safe_flash['id'],op('!',S('flashing')));wheel.append(safe_flash)
        ring_flash=themes('hub-ring-flash',lambda ident,line,edge,light,sp: art(ident,[circle('ring'+str(r),r,ui.rgb('#FFFFFF' if ident.endswith('olive') else edge,.9),1.1) for r in (80,86)]))
        ring_flash['properties']['opacity']=ui.number(0);hub['children'].append(ring_flash);show(ring_flash['id'],op('!',S('flashing')))
        def tune(delay,duration):
            def keys(start,end): return [(0,ui.length(start))]+([(delay,ui.length(start))] if delay else [])+[(delay+duration,ui.length(end))]
            return [held('hub-name-clip','top',keys(264,247)),held('hub-name-clip','height',keys(2,36)),held('hub-name-unclip','top',keys(-264,-247)),
                held('hub-name','opacity',[(0,ui.number(0))]+([(delay,ui.number(0))] if delay else [])+[(delay+duration,ui.number(1))]),
                held('hub-name','color',[(0,ui.colour([1,1,1,1])),(delay+duration+100,ui.colour([1,1,1,1])),(delay+duration+200,ui.colour(ui.rgb('#B0C891')))])]
        opening += [held('hub','transform',[(0,ui.transform(sx=.55,sy=.06)),(150,ui.transform()),(250,ui.transform()),(300,ui.transform(sx=.9,sy=.9)),(350,ui.transform())]),
            held('hub-ring-flash','opacity',[(0,ui.number(0)),(149,ui.number(0)),(150,ui.number(.9)),(200,ui.number(0))]),
            held('hub-torn','opacity',[(0,ui.number(.06)),(100,ui.number(.06)),(150,ui.number(.4)),(650,ui.number(.06))])]+tune(200,200)
        closing += [held('hub','transform',[(0,ui.transform()),(150,ui.transform(sx=.55,sy=.06))]),
            held('hub-torn','opacity',[(0,ui.number(.8)),(150,ui.number(.06))])]
        selection += [held('hub-torn','opacity',[(0,ui.number(.06)),(50,ui.number(.4)),(350,ui.number(.06))]),
            held('selection-flash','opacity',[(0,ui.number(1)),(150,ui.number(0))])]+tune(0,120)
        open_end=650;close_end=150;select_end=350

    # Infinite-ammunition symbols are geometry because Chain has no infinity
    # glyph. Keep the same state/readout contract for both HUD families.
    infinity=shape('infinity',[],ui.rgb('#FCFFC8' if strogg else '#DDEBC3'),1.8)
    infinity['commands']=[{'id':'start','op':'move','points':[[280,300]]},
        {'id':'left','op':'cubic','points':[[258,278],[258,322],[280,300]]},
        {'id':'right','op':'cubic','points':[[302,278],[302,322],[280,300]]}]
    hub['children'].append(art('hub-infinity',[infinity]));show('hub-infinity',op('==',S('ammo'),'∞'))
    show('hub-ammo-slant' if strogg else 'hub-ammo',op('!=',S('ammo'),'∞'))
    doc.timelines.add('open',open_end,opening);doc.timelines.add('close',close_end,closing);doc.timelines.add('selection',select_end,selection)
    for event in ('open','close','selection'): doc.events[event]=[{'op':'playTimeline','timeline':event}]
    if chat: doc.events['channel']=[{'op':'playTimeline','timeline':'selection'}]
    doc.events['onInit']=loops
    props={**ui.absolute(width=560,height=560),'left':ui.length(50,'%'),'top':ui.length(50,'%'),'margin-left':ui.length(-280),'margin-top':ui.length(-280),'transform':ui.transform()}
    root=full('wheel-root',[ui.group('wheel-center',props,wheel)],opacity=ui.number(0))
    doc.bind('root.blend','wheel-root','opacity',S('blend'));doc.bind('wheel.placement','wheel-center','transform',[S('offset'),0,S('fit'),S('fit'),0])
    return doc.build(root,{'extensions':{'openq4':{'reference':'ui-visual-design.md 14.6 / 14.16','family':family,'slots':n,
        'input':'Player-owned radial selection; no pointer hit testing','placement':'ui_weaponWheelOffset percent of viewport width from center, default +27; fitted at narrow edges'}}})


# The quick-chat wheel's phrases clockwise from the top (#str_231261-231268),
# the channel words and the zoom control's verb.
PHRASES = ['#str_2312' + str(61 + i) for i in range(8)]
ALL_KEY, TEAM_KEY, CHANNEL_KEY = '#str_231254', '#str_231001', '#str_231256'
# Your team's text color in multiplayer (14.6) and its selection light.
TEAM_TEXT = [('#AAE355', '#616F26'), ('#FF8E00', '#6B3A08')]


def held(node, prop, keys):
    """A track that starts where it is authored: a play retargets the first
    key to the property's current value, so a copy of that key 1 ms later
    makes the authored start take effect at once."""
    if len(keys) >= 2 and keys[1][0] > 1:
        keys = [keys[0], (1, keys[0][1])] + list(keys[1:])
    return ui.track(node, prop, keys)


def chat_document():
    """Quick chat shares the Marine wheel's plates, signal hub and choreography."""
    return document(chat=True)


def write(path, document, check):
    text = '// Generated by tools/ui/build_retained_weapon_wheel.py; editable vector art.\n' + json.dumps(document, ensure_ascii=False, indent=2) + '\n'
    text = re.sub(r'\[\n\s+(-?\d+(?:\.\d+)?),\n\s+(-?\d+(?:\.\d+)?)\n\s+\]', r'[\1, \2]', text)
    if check:
        assert path.read_text(encoding='utf-8') == text, path
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8', newline='\n')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for strogg in (False,True):
        for awakening in (False,True):
            name=('strogg' if strogg else 'marine')+('_awakening' if awakening else '')
            path=ROOT/'content/baseoq4/pak0/guis/hud'/('weapon_wheel_'+name+'.rui')
            write(path,document(strogg,awakening),args.check)
    write(ROOT/'content/baseoq4/pak0/guis/hud/quick_chat.rui',chat_document(),args.check)
    print('Native weapon wheel documents are current')


if __name__=='__main__': main()
