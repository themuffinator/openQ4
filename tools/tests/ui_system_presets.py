#!/usr/bin/env python3
"""Strict source contracts for SYSTEM presets. Runtime/layout is tested separately."""
import copy
import itertools
import json
import re
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ui'))
import build_retained_screens as screens
import retained_system as system
SOURCE=screens.OUTPUTS['system']
PROFILES=('minimum','lowpower','low','performance','balanced','enhanced','quality','ultra','maximum')
GRAPHICS=0  # page.tab of the GRAPHICS tab, which holds the preset and the renderer rows

def load(path):
    return json.loads('\n'.join(line for line in path.read_text(encoding='utf-8').splitlines() if not line.lstrip().startswith('//')))

def nodes(root):
    result={}
    def walk(node):
        if node['id'] in result: raise ValueError('Duplicate canonical node: '+node['id'])
        result[node['id']]=node
        for child in node.get('children',[]): walk(child)
    walk(root)
    return result

def renamed(value,old,new):
    if isinstance(value,str): return new+value[len(old):] if value.startswith(old) else value
    if isinstance(value,list): return [renamed(item,old,new) for item in value]
    if isinstance(value,dict): return {key:renamed(item,old,new) for key,item in value.items()}
    return value

def evaluate(value,state):
    if not isinstance(value,dict): return value
    if 'state' in value:return state[value['state']]
    op,args=value['op'],value['args']
    if op=='!':return not evaluate(args[0],state)
    if op=='&&':return evaluate(args[0],state) and evaluate(args[1],state)
    if op=='==':return evaluate(args[0],state)==evaluate(args[1],state)
    raise AssertionError('Unexpected preset guard operator '+op)

def contract(page):
    index=nodes(page['root']); choice=index['settings_preset']['control']
    assert choice['role']=='choice' and choice['action']=='preset' and 'event' not in choice
    assert choice['value']=={'state':'settings.draft.com_performancePreset'}
    assert tuple(o['value'] for o in choice['options'])==PROFILES
    for i,o in enumerate(choice['options']):
        assert o['id']==o['node']=='settings_preset-option-'+str(i)
        assert o['label']=='#str_229977' and o['labelIndex']==i
    assert page['actions']['preset']=={'input':'string','operation':'settings.system.preset','arguments':{'name':{'input':'value'}}}
    assert page['actions']['autoDetect']=={'operation':'settings.system.autodetect','arguments':{}}
    assert index['settings_autodetect']['control']['event']=='autoDetect'
    event=page['events']['autoDetect'];assert len(event)==1 and event[0]['op']=='if'
    assert event[0]['then']==[{'op':'action','action':'autoDetect'}] and not event[0].get('else')
    conditions=[event[0]['condition']]
    for id in ('settings_preset','settings_autodetect'):
        matches=[b for b in page['bindings'] if b['node']==id and b['property']=='enabled'];assert len(matches)==1
        conditions.append(matches[0]['value'])
    # The event answers wherever it is called from; the rows also only while
    # their GRAPHICS tab is current.
    keys=('settings.open','settings.busy','settings.confirmationVisible','page.discardVisible','ui.numberDraftsPending','settings.render.autoDetect')
    for phase,tab in itertools.product(range(6),range(3)):
        for bits in itertools.product((False,True),repeat=6):
            opened,busy,confirm,modal,local,detected=bits
            state=dict(zip(keys,bits));state['settings.phase']=phase;state['page.tab']=tab
            expected=opened and phase==1 and not(busy or confirm or modal or local)
            assert evaluate(conditions[0],state)==(expected and detected)
            assert evaluate(conditions[1],state)==(expected and tab==GRAPHICS)
            assert evaluate(conditions[2],state)==(expected and detected and tab==GRAPHICS)
    for prefix in ('draft','baseline'):
        alias=prefix+'Preset';assert page['aliases'][alias]=={'variable':alias}
        assert page['presentationVariables'][alias]['value']=={'state':'settings.'+prefix+'.com_performancePreset'}

class Presets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.page=load(SOURCE);cls.nodes=nodes(cls.page['root'])
    def test_contract(self):contract(self.page)
    def test_generator_reproduces_preset_contract(self):
        # The page is generated. Rebuilding all of it outlasts this test's 30 s
        # meson budget, so ui_system_number_fields.py (and
        # build_retained_screens.py --check) compares the whole document; this
        # rebuilds the generator's preset programs and gates.
        doc=screens.Document('openq4.system');system.page_actions(doc);system.page_events(doc)
        for name in ('preset','autoDetect'):self.assertEqual(self.page['actions'][name],doc.actions[name])
        self.assertEqual(self.page['events']['autoDetect'],doc.events['autoDetect'])
        current={'op':'==','args':[{'state':'page.tab'},GRAPHICS]}
        for id,enabled in (('settings_preset',system.QUIET),('settings_autodetect',system.both(system.QUIET,system.st('settings.render.autoDetect')))):
            self.assertEqual(next(b['value'] for b in self.page['bindings'] if b['id']==id+'.enabled'),{'op':'&&','args':[current,enabled]})
        self.assertEqual(tuple(system.PROFILES),PROFILES)
        self.assertEqual(self.page['extensions']['openq4']['presets'],system.EXTENSIONS['presets'])
    def test_graphics_tab_placement_and_order(self):
        self.assertEqual([n['id'] for n in self.nodes['settings-body']['children']],['settings-page-graphics','settings-page-screen','settings-page-image'])
        # The column's heading leads it; the preset is the tab's first row.
        self.assertEqual([n['id'] for n in self.nodes['settings-graphics-column-0']['children']],
                         ['settings-graphics-head-0','settings_preset','settings_autodetect','settings_backend','settings_gpu','settings_renderer',
                          'settings_renderer-status','settings_restart_renderer','rendering-active','rendering-notice'])
        # The tab's rows scroll in one area: settings-page-graphics >
        # settings-graphics-rows > settings-graphics-columns > its two columns.
        self.assertEqual([n['id'] for n in self.nodes['settings-page-graphics']['children']],['settings-graphics-rows'])
        self.assertEqual(self.nodes['settings-graphics-rows']['properties']['overflow'],{'type':'keyword','value':'auto'})
        self.assertEqual([n['id'] for n in self.nodes['settings-graphics-rows']['children']],['settings-graphics-columns'])
        self.assertEqual([n['id'] for n in self.nodes['settings-graphics-columns']['children']],['settings-graphics-column-0','settings-graphics-column-1'])
        self.assertEqual(self.page['state']['page.tab'],{'type':'number','initial':GRAPHICS})
        current={'op':'==','args':[{'state':'page.tab'},GRAPHICS]}
        for id in ('settings_preset','settings_autodetect','settings_backend','settings_gpu'):
            enabled=next(b['value'] for b in self.page['bindings'] if b['id']==id+'.enabled')
            self.assertEqual(enabled['op'],'&&');self.assertEqual(enabled['args'][0],current)
        for id in ('settings_preset','settings_backend','settings_gpu'):
            self.assertEqual(self.nodes[id]['control']['placementBounds'],'settings-body')
        # The preset is the tab's first row: the strip reaches it, and both the
        # arrival on GRAPHICS and the switch to it focus it.
        self.assertEqual(self.nodes['settings_preset']['control']['navigation']['up'],'tab-graphics')
        self.assertEqual(self.nodes['settings_preset']['control']['navigation']['down'],'settings_autodetect')
        self.assertEqual(self.nodes['settings_autodetect']['control']['navigation']['up'],'settings_preset')
        arrival=self.page['events']['onActivate'][-1]
        self.assertEqual((arrival['condition'],arrival['then']),(current,[{'op':'focus','control':'settings_preset'}]))
        switch=self.page['events']['tab_graphics'][0]['then'][0]['then']
        self.assertEqual(switch[-1],{'op':'focus','control':'settings_preset'})
    def test_existing_art_is_reused(self):
        # Both rows draw the stock setting row's plate, wash and focus rail.
        for new,old in [('settings_preset','settings_postaa'),('settings_autodetect','settings_postaa')]:
            for part in ('plate','wash','focus'):
                expected=renamed(self.nodes[old+'-'+part],old,new)
                self.assertEqual(self.nodes[new+'-'+part],expected)
        for id,node in self.nodes.items():
            if id.startswith(('settings_preset','settings_autodetect')):self.assertNotEqual(node['type'],'image')
    def test_all_locales_have_every_profile_label(self):
        files=sorted((ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'))
        self.assertEqual({f.stem for f in files},{n+'_openq4' for n in ('english','french','german','italian','spanish','polish','russian',
            'brazilian','czech','hungarian','turkish','ukrainian')})
        for file in files:
            for id in ('229976','229977','229978','230010'):
                values=re.findall(r'"#str_'+id+r'"\s+"([^"\r\n]+)"',file.read_text(encoding='utf-8'))
                self.assertEqual(len(values),1,file.name+id)
                if id=='229977':self.assertEqual(len(values[0].split(';')),len(PROFILES))
    def test_no_duplicate_profile_values_in_page(self):
        self.assertEqual(self.page['actions']['preset']['arguments'],{'name':{'input':'value'}})
        self.assertEqual(self.page['events']['autoDetect'][0]['then'],[{'op':'action','action':'autoDetect'}])
        self.assertEqual(self.nodes['settings_apply']['control']['event'],'apply')
        self.assertIn({'state':'ui.numberDraftsPending'},self.page['events']['apply'][0]['condition']['args'][1]['args'])
        self.assertEqual(self.page['extensions']['openq4']['presets']['source'],'src/framework/PerformancePreset.cpp')
    def test_mutations_refused(self):
        def reject(change):
            p=copy.deepcopy(self.page);change(p)
            with self.assertRaises((AssertionError,KeyError)):contract(p)
        reject(lambda p:p['actions']['preset'].update(operation='settings.system.edit'))
        reject(lambda p:p['actions']['preset']['arguments'].update(name='ultra'))
        reject(lambda p:p['events']['autoDetect'][0].update(condition=True))
        reject(lambda p:p['events']['autoDetect'][0]['then'].append({'op':'setState','values':{'settings.draft.com_performancePreset':'quality'}}))
        reject(lambda p:p['events']['autoDetect'][0].update(**{'else':[{'op':'action','action':'autoDetect'}]}))
        reject(lambda p:p['presentationVariables']['baselinePreset'].update(value={'state':'settings.draft.com_performancePreset'}))
        reject(lambda p:nodes(p['root'])['settings_preset']['control']['options'][2].update(value='ultra'))
        for id in ('settings_preset','settings_autodetect'):
            reject(lambda p,id=id:next(b for b in p['bindings'] if b['id']==id+'.enabled').update(value=True))
            # A row that answers while another tab is current.
            reject(lambda p,id=id:(lambda b:b.update(value=b['value']['args'][1]))(next(b for b in p['bindings'] if b['id']==id+'.enabled')))
        # Missing each individual gate still fails exhaustive truth-table checks.
        def replace(value,key):
            if value=={'state':key}:return True if key=='settings.open' else False
            if isinstance(value,list):return [replace(v,key) for v in value]
            if isinstance(value,dict):return {k:replace(v,key) for k,v in value.items()}
            return value
        for key in ('settings.open','settings.busy','settings.confirmationVisible','page.discardVisible','ui.numberDraftsPending'):
            reject(lambda p,key=key:p['events']['autoDetect'][0].update(condition=replace(p['events']['autoDetect'][0]['condition'],key)))

if __name__=='__main__':unittest.main()
