#!/usr/bin/env python3
"""Author game-driven scene focus, recovery and death, without script edits."""
import argparse
import build_retained_screens as ui

def product(*args): return {'op':'*','args':list(args)}
def state(name): return {'state':name}

def document():
    doc=ui.Document('openq4.view-effects')
    for name in ('recovery_amount','death_amount','death_shock','death_focus','death_tunnel',
                 'crash_focus','crash_lids','wheel_amount'):
        doc.state[name]={'type':'number','initial':0}
    def filter(ident,source,blur,saturation,boost=None):
        node=ui.group(ident,{**ui.FULL,'display':ui.keyword('none'),
                      'backdrop-blur':ui.length(0),'backdrop-saturate':ui.number(1)})
        doc.bind(ident+'.display',ident,'display',{'op':'select','args':[{'op':'>','args':[state(source),.001]},'block','none']})
        sigma=product(state(source),blur)
        if boost:
            sigma={'op':'+','args':[sigma,product(state(boost),4.5)]}
        doc.bind(ident+'.blur',ident,'backdrop-blur',sigma)
        doc.bind(ident+'.saturation',ident,'backdrop-saturate',{'op':'-','args':[1,product(state(source),saturation)]})
        return node
    def veil(ident,color,source,stops=None,vertical=False):
        # Independent view-space edges form a smooth vignette at any aspect;
        # corners naturally darken more without squeezing camera geometry.
        paths=[]
        edges=[((0,0),(0,{'fraction':1}))] if vertical else [((0,0),({'fraction':1},0)),((0,0),(0,{'fraction':1}))]
        stops=stops or [(0,.66),(.1,.34),(.28,.05),(.5,0),(.72,.05),(.9,.34),(1,.66)]
        for n,(start,end) in enumerate(edges):
            paths.append(ui.path('wash-'+str(n),[(0,0),({'fraction':1},0),({'fraction':1},{'fraction':1}),(0,{'fraction':1})],
              fill=ui.linear(start,end,[(at,ui.rgb(color,alpha)) for at,alpha in stops])))
        node=ui.vector(ident,{**ui.FULL,'opacity':ui.number(0)},paths)
        doc.bind(ident+'.opacity',ident,'opacity',state(source))
        return node
    # Isotropic dp sigma, two-pass Gaussian filtering on the live scene. The
    # wheel pass is submitted before the HUD; wheel artwork stays unfiltered.
    nodes=[filter('wheel-focus','wheel_amount',8,.06),
           filter('recovery-focus','recovery_amount',20,.30),
           veil('recovery-periphery','#33271E','recovery_amount'),
           # Warm, heavy eyes distinguish the crash from the colder death
           # grade. Soft vertical shading keeps a wide, unobstructed center.
           filter('crash-focus','crash_focus',16,.38),
           veil('crash-periphery','#241B15','crash_focus'),
           veil('crash-eyelids','#050403','crash_lids',
                [(0,.84),(.10,.68),(.22,.30),(.36,.04),(.46,0),(.56,0),(.70,.04),(.84,.26),(1,.72)],vertical=True),
           filter('death-focus','death_amount',3.4,.94,boost='death_focus'),
           veil('death-vignette','#08090B','death_amount'),
           veil('death-tunnel','#08090B','death_tunnel',
                [(0,.28),(.1,.24),(.25,.13),(.4,.015),(.5,0),(.6,.015),(.75,.13),(.9,.24),(1,.28)]),
           veil('death-afterimage','#782B24','death_shock')]
    result=doc.build(ui.group('screen',{**ui.FULL,'pointer-events':ui.keyword('none')},nodes))
    result['canvas']['minWidth']=960
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    target=ui.PAK0/'guis/hud/view_effects.rui';text=ui.render(document()).replace('build_retained_screens.py','build_retained_effects.py')
    if args.check:
        if not target.exists() or target.read_text(encoding='utf-8')!=text.replace('\r\n','\n'):raise SystemExit('stale view effects document')
    else:
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(text.encode('utf-8'));print(target.relative_to(ui.ROOT))

if __name__=='__main__':main()
