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
    if 'input' in value:return state['input']
    op,args=value['op'],value['args']
    if op=='!':return not evaluate(args[0],state)
    if op=='&&':return evaluate(args[0],state) and evaluate(args[1],state)
    if op=='==':return evaluate(args[0],state)==evaluate(args[1],state)
    if op=='!=':return evaluate(args[0],state)!=evaluate(args[1],state)
    if op=='||':return evaluate(args[0],state) or evaluate(args[1],state)
    if op=='select':return evaluate(args[1 if evaluate(args[0],state) else 2],state)
    raise AssertionError('Unexpected preset guard operator '+op)

# What a preset writes to the settings the page's rows and marks read
# (ExpandPerformancePreset), from the tables the generator read.
def expansion(name):
    preset=system.PRESETS[name];level=system.SHADOW_LEVELS[preset['shadowQuality']]
    typed=lambda key,value:bool(value) if system.CATALOG[key][0]=='boolean' else value
    values={key:typed(key,level[field]) for key,field in system.SHADOW_SETTINGS}
    values.update({key:typed(key,preset[field]) for key,field in (('r_multiSamples','multiSamples'),('r_postAA','postAA'),
        ('image_anisotropy','anisotropy'),('r_bloom','bloom'),('r_ssao','ssao'),('r_hdrToneMap','hdrToneMap'),('r_hdrSceneTarget','hdrToneMap'),
        ('r_motionBlur','motionBlur'),('r_depthOfField','depthOfField'),('r_crt','crt'),('r_useLightGrid','useLightGrid'),
        ('r_shadowMapMaxUpdatesPerView','shadowMapMaxUpdates'),('com_maxfps','maxFps'))})
    values.update({'r_skipSpecular':False,'r_skipBump':False,'r_skipSky':False,'r_shadowMapCascadeCount':4,'r_shadowMapCascadeStabilize':True,
        'com_performancePreset':name})
    return {'settings.draft.'+key:value for key,value in values.items()}

# The rows a preset fills, each with a value its preset never writes.
MARKED={'settings_msaa':('r_multiSamples',16),'settings_postaa':('r_postAA',4),'settings_anisotropy':('image_anisotropy',8),
    'settings_bloom':('r_bloom',None),'settings_ssao':('r_ssao',None),'settings_tonemap':('r_hdrToneMap',None),
    'settings_motionblur':('r_motionBlur',None),'settings_depthoffield':('r_depthOfField',None),'settings_crt':('r_crt',None),
    'settings_irradiance':('r_useLightGrid',None),'settings_specular':('r_skipSpecular',None),'settings_bump':('r_skipBump',None),
    'settings_sky':('r_skipSky',None),'settings_shadows':('r_shadows',None)}

def contract(page):
    index=nodes(page['root']); choice=index['settings_preset']['control']
    assert choice['role']=='choice' and choice['action']=='preset' and 'event' not in choice
    # The draft's preset, or Custom while a row has left it: an entry that
    # shows and cannot be chosen, after the nine presets.
    assert choice['value']['op']=='select' and choice['value']['args'][1:]==['custom',{'state':'settings.draft.com_performancePreset'}]
    assert tuple(o['value'] for o in choice['options'])==PROFILES+('custom',)
    for i,o in enumerate(choice['options'][:-1]):
        assert o['id']==o['node']=='settings_preset-option-'+str(i)
        assert o['label']=='#str_229977' and o['labelIndex']==i and 'enabled' not in o
    custom=choice['options'][-1]
    assert custom['label']=='#str_233303' and 'labelIndex' not in custom and custom['enabled'] is False
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
        self.assertEqual([n['id'] for n in self.nodes['settings-body']['children']],['settings-page-graphics','settings-page-screen','settings-page-image',
                                                                                    'settings-page-effects'])
        # The column's heading leads it; the preset is the tab's first row.
        self.assertEqual([n['id'] for n in self.nodes['settings-graphics-column-0']['children']],
                         ['settings-graphics-head-0','settings_preset_marked','settings_autodetect','settings_backend','settings_gpu','settings_renderer',
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
        # The preset's reset mark follows it in the reading order (a mark not
        # shown is passed over) and returns to it with Right.
        self.assertEqual([n['id'] for n in self.nodes['settings_preset_marked']['children']],['settings_preset','settings_preset_reset'])
        self.assertEqual(self.nodes['settings_preset']['control']['navigation']['down'],'settings_preset_reset')
        self.assertEqual(self.nodes['settings_preset_reset']['control']['navigation'],
                         {'left':'settings_preset_reset','right':'settings_preset','up':'settings_preset','down':'settings_autodetect'})
        self.assertEqual(self.nodes['settings_autodetect']['control']['navigation']['up'],'settings_preset_reset')
        arrival=self.page['events']['onActivate'][-1]
        self.assertEqual((arrival['condition'],arrival['then']),(current,[{'op':'focus','control':'settings_preset'}]))
        switch=self.page['events']['tab_graphics'][0]['then'][0]['then']
        self.assertEqual(switch[-1],{'op':'focus','control':'settings_preset'})
    def test_shadows_is_one_choice_over_the_presets_ladder(self):
        levels=system.SHADOW_LEVELS
        self.assertEqual([level['name'] for level in levels],['off','original','low','medium','high','ultra'])
        choice=self.nodes['settings_shadows']['control']
        self.assertEqual((choice['role'],choice['action'],choice['label']),('choice','edit.shadow-quality','#str_233300'))
        self.assertNotIn('settings_shadowmaps',self.nodes)
        self.assertEqual([(o['label'],o['labelIndex'],o['value'],o.get('enabled',True)) for o in choice['options']],
                         [('#str_233301',i,i,i<6) for i in range(7)])
        action=self.page['actions']['edit.shadow-quality']
        self.assertEqual((action['input'],action['operation']),('number','settings.system.edit'))
        writes=lambda level:{'settings.draft.'+key:evaluate(value,{'input':level}) for key,value in action['arguments'].items()}
        for index,level in enumerate(levels):
            # The patch is the level's whole recipe, and reads back as that level.
            chosen=writes(index)
            for key,field in system.SHADOW_SETTINGS:
                self.assertEqual(chosen['settings.draft.'+key],bool(level[field]) if system.CATALOG[key][0]=='boolean' else level[field],(index,key))
            self.assertEqual((chosen['settings.draft.r_shadowMapMaxUpdatesPerView'],chosen['settings.draft.r_shadowMapCascadeCount'],
                              chosen['settings.draft.r_shadowMapCascadeStabilize']),(0,4,True))
            self.assertEqual(evaluate(choice['value'],chosen),index)
            # Off and Original are their switches alone; a changed shadow-map value is Custom.
            for key,field in system.SHADOW_SETTINGS[2:]:
                mixed=dict(chosen);value=mixed['settings.draft.'+key]
                mixed['settings.draft.'+key]=(not value) if isinstance(value,bool) else value+1
                self.assertEqual(evaluate(choice['value'],mixed),index if index<2 else 6,(index,key))
        # Character Self-Shadows answers only under shadow maps.
        gate=next(b['value'] for b in self.page['bindings'] if b['id']=='settings_character_self_shadow.enabled')
        for index in range(6):
            state={**writes(index),'page.tab':3,'settings.open':True,'settings.phase':1,'settings.busy':False}
            self.assertEqual(bool(evaluate(gate,state)),index>=2,index)
    def test_a_changed_row_is_marked_and_resets_to_its_preset(self):
        choice=self.nodes['settings_preset']['control']
        editable={'page.tab':0,'settings.open':True,'settings.phase':1,'settings.busy':False}
        shown=lambda ident,state:evaluate(next(b['value'] for b in self.page['bindings'] if b['id']==ident+'_reset.display'),state)
        self.assertEqual({ident[:-6] for ident in self.nodes if ident.endswith('_reset')},set(MARKED)|{'settings_preset'})
        for ident in (*MARKED,'settings_preset'):
            mark=self.nodes[ident+'_reset']['control']
            # The mark runs its reset while it may and hands the focus back to its row.
            self.assertEqual((mark['role'],mark['label'],mark['event']),('button','#str_233302','reset.'+ident));self.assertNotIn('action',mark)
            event=self.page['events']['reset.'+ident];self.assertEqual(len(event),1)
            self.assertEqual(event[0]['then'],[{'op':'action','action':'reset.'+ident},{'op':'focus','control':ident}])
            self.assertEqual(event[0]['condition'],next(b['value'] for b in self.page['bindings'] if b['id']==ident+'_reset.enabled')['args'][1])
            self.assertNotIn('else',event[0])
            self.assertEqual(self.page['actions']['reset.'+ident]['operation'],'settings.system.edit')
            self.assertNotIn('input',self.page['actions']['reset.'+ident])
            self.assertEqual([n['id'] for n in self.nodes[ident+'_marked']['children']],[ident,ident+'_reset'])
        for name in PROFILES:
            written=expansion(name)
            # A preset as written marks nothing and reads as itself.
            self.assertEqual(evaluate(choice['value'],written),name)
            self.assertEqual(evaluate(self.nodes['settings_shadows']['control']['value'],written),system.PRESETS[name]['shadowQuality'])
            for ident in (*MARKED,'settings_preset'):self.assertEqual(shown(ident,written),'none',(name,ident))
            # The frame cap is the display's: Auto-Detect's refresh rate is no change.
            capped={**written,'settings.draft.com_maxfps':written['settings.draft.com_maxfps']+21}
            self.assertEqual(evaluate(choice['value'],capped),name)
            for ident,(key,other) in MARKED.items():
                changed=dict(written);key='settings.draft.'+key
                changed[key]=(not changed[key]) if other is None else other
                # That row alone is marked, the preset reads Custom and both marks show.
                self.assertEqual(evaluate(choice['value'],changed),'custom',(name,ident))
                for each in MARKED:self.assertEqual(shown(each,changed),'block' if each==ident else 'none',(name,ident,each))
                self.assertEqual(shown('settings_preset',changed),'block')
                enabled=next(b['value'] for b in self.page['bindings'] if b['id']==ident+'_reset.enabled')
                tab=next(b['value'] for b in self.page['bindings'] if b['id']==ident+'.enabled')['args'][0]['args'][1]
                self.assertTrue(evaluate(enabled,{**changed,**editable,'page.tab':tab}))
                self.assertFalse(evaluate(enabled,{**changed,**editable,'page.tab':tab,'settings.busy':True}))
                # Its mark, and the preset's, return exactly what the preset wrote.
                for reset in (ident,'settings_preset'):
                    patch=self.page['actions']['reset.'+reset]['arguments']
                    restored={**changed,**{'settings.draft.'+k:evaluate(v,changed) for k,v in patch.items()}}
                    self.assertEqual(restored,written,(name,ident,reset))
            # Every row changed at once comes back through the preset's mark alone.
            changed=dict(written)
            for key,other in MARKED.values():changed['settings.draft.'+key]=(not changed['settings.draft.'+key]) if other is None else other
            patch=self.page['actions']['reset.settings_preset']['arguments']
            self.assertEqual({**changed,**{'settings.draft.'+k:evaluate(v,changed) for k,v in patch.items()}},written,name)
            self.assertNotIn('com_maxfps',patch);self.assertNotIn('com_performancePreset',patch)
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
            for id in ('229976','229977','229978','230010','233300','233301','233302','233303'):
                values=re.findall(r'"#str_'+id+r'"\s+"([^"\r\n]+)"',file.read_text(encoding='utf-8'))
                self.assertEqual(len(values),1,file.name+id)
                if id=='229977':self.assertEqual(len(values[0].split(';')),len(PROFILES))
                # Off, Original, four shadow-map levels and Custom.
                if id=='233301':self.assertEqual(len(values[0].split(';')),7)
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
        reject(lambda p:nodes(p['root'])['settings_preset']['control']['options'][9].pop('enabled'))
        reject(lambda p:nodes(p['root'])['settings_preset']['control'].update(value={'state':'settings.draft.com_performancePreset'}))
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
