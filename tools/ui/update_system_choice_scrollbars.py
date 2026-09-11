#!/usr/bin/env python3
"""Author editable local scrollbars for SYSTEM's choice popups.

The four choices retain their application actions and accepted-value bindings.
Only local popup artwork and its existing feedback timelines are extended.
"""
import argparse
import copy
import json
from pathlib import Path
from update_system_presets import ROOT, SOURCE, keyword, length, load, nodes, number
from update_system_scrollbar import ink, edge


def compose(document):
    result=copy.deepcopy(document)
    for choice in list(nodes(result['root']).values()):
        control=choice.get('control',{})
        if control.get('role')!='choice':
            continue
        index=nodes(choice);popup=index[control['parts']['popup']]
        ident=choice['id']+'-popup-scroll'
        olive=[0.5451,0.5882,0.2941,1]
        shape=[[14,0],[26,0],[26,edge(-4)],[22,edge()],[10,edge()],[10,4]]
        thumb={'id':ident+'-thumb','type':'group','properties':{
            'position':keyword('absolute'),'display':keyword('block'),'box-sizing':keyword('border-box'),
            'left':length(0),'top':length(0),'width':length(36),'height':length(36),'opacity':number(1)},
            'children':[ink(ident+'-base',shape,[0.32,0.36,0.16,0.95],olive),
                        ink(ident+'-active',shape,[0.8902,0.5373,0,1],olive,0)]}
        track={'id':ident+'-track','type':'group','properties':{
            'position':keyword('absolute'),'display':keyword('block'),'box-sizing':keyword('border-box'),
            'left':length(0),'top':length(0),'width':length(36),'height':length(36),'opacity':number(1)},
            'children':[ink(ident+'-trough',[[16,0],[22,0],[22,edge()],[14,edge()],[14,2]],
                            [0.0353,0.0471,0.0314,0.95],[0.5451,0.5882,0.2941,0.4]),thumb]}
        popup['children']=[child for child in popup['children'] if child['id']!=track['id']]+[track]
        control['scrollbar']={'track':track['id'],'thumb':thumb['id'],'lineStep':36,'minimumThumb':36}
        for state,timeline_id in control['states'].items():
            timeline=next(t for t in result['timelines'] if t['id']==timeline_id)
            timeline['tracks']=[t for t in timeline['tracks'] if t['node']!=ident+'-active']
            alpha={'default':0,'hover':0.55,'focus':0.85,'pressed':1,'disabled':0}[state]
            timeline['tracks'].append({'node':ident+'-active','property':'opacity','keys':[
                {'atMs':0,'value':number(alpha)},{'atMs':timeline['durationMs'],'value':number(alpha)}]})
    nodes(result['root'])
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=ROOT/SOURCE)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args();prefix,document=load(args.source);result=compose(document)
    if args.check:
        if result!=document:raise SystemExit('SYSTEM choice scrollbar artwork differs from generator')
    else:
        args.source.write_text(prefix+json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')


if __name__=='__main__':main()
