#!/usr/bin/env python3
"""Strict source contracts for SYSTEM presets. Runtime/layout is tested separately."""
import copy
import importlib.util
import itertools
import re
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('presets',ROOT/'tools/ui/update_system_presets.py')
presets=importlib.util.module_from_spec(spec);spec.loader.exec_module(presets)
spec=importlib.util.spec_from_file_location('numbers',ROOT/'tools/ui/update_system_number_fields.py')
numbers=importlib.util.module_from_spec(spec);spec.loader.exec_module(numbers)

def evaluate(value,state):
    if not isinstance(value,dict): return value
    if 'state' in value:return state[value['state']]
    op,args=value['op'],value['args']
    if op=='!':return not evaluate(args[0],state)
    if op=='&&':return evaluate(args[0],state) and evaluate(args[1],state)
    if op=='==':return evaluate(args[0],state)==evaluate(args[1],state)
    raise AssertionError('Unexpected preset guard operator '+op)

def contract(page):
    nodes=presets.nodes(page['root']); choice=nodes['settings_preset']['control']
    assert choice['role']=='choice' and choice['action']=='preset' and 'event' not in choice
    assert choice['value']=={'state':'settings.draft.com_performancePreset'}
    assert tuple(o['value'] for o in choice['options'])==('minimum','lowpower','performance','balanced','quality','ultra')
    for i,o in enumerate(choice['options']):
        assert o['id']==o['node']=='settings_preset-option-'+str(i)
        assert o['label']=='#str_229977' and o['labelIndex']==i
    assert page['actions']['preset']=={'input':'string','operation':'settings.system.preset','arguments':{'name':{'input':'value'}}}
    assert page['actions']['autoDetect']=={'operation':'settings.system.autodetect','arguments':{}}
    assert nodes['settings_autodetect']['control']['event']=='autoDetect'
    event=page['events']['autoDetect'];assert len(event)==1 and event[0]['op']=='if'
    assert event[0]['then']==[{'op':'action','action':'autoDetect'}] and not event[0].get('else')
    conditions=[event[0]['condition']]
    for id in ('settings_preset','settings_autodetect'):
        matches=[b for b in page['bindings'] if b['node']==id and b['property']=='enabled'];assert len(matches)==1
        conditions.append(matches[0]['value'])
    for phase in range(6):
        for bits in itertools.product((False,True),repeat=5):
            opened,busy,confirm,modal,local=bits
            state=dict(zip(('settings.open','settings.busy','settings.confirmationVisible','page.discardVisible','ui.numberDraftsPending'),bits));state['settings.phase']=phase
            expected=opened and phase==1 and not(busy or confirm or modal or local)
            assert all(evaluate(condition,state)==expected for condition in conditions)
    for prefix in ('draft','baseline'):
        alias=prefix+'Preset';assert page['aliases'][alias]=={'variable':alias}
        assert page['presentationVariables'][alias]['value']=={'state':'settings.'+prefix+'.com_performancePreset'}

class Presets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.prefix,cls.page=presets.load(ROOT/presets.SOURCE);cls.nodes=presets.nodes(cls.page['root'])
    def test_contract(self):contract(self.page)
    def test_generators_independently_idempotent(self):
        self.assertEqual(presets.compose(self.page),self.page)
        self.assertEqual(numbers.compose(self.page),self.page)
        self.assertEqual(numbers.compose(presets.compose(self.page)),presets.compose(numbers.compose(self.page)))
    def test_placement_and_existing_order(self):
        self.assertEqual([n['id'] for n in self.nodes['settings-body']['children']],['performance-band','image-column','render-column'])
        self.assertEqual([n['id'] for n in self.nodes['performance-band']['children']],['settings_preset','settings_autodetect'])
        self.assertEqual(self.nodes['performance-band']['properties']['flex-wrap'],presets.keyword('wrap'))
        for id in ('settings_preset','settings_autodetect'):
            self.assertGreater(self.nodes[id]['properties']['flex-shrink']['value'],0)
        self.assertNotIn('height',self.nodes['settings_preset-value']['properties'])
        self.assertEqual(self.nodes['settings_preset-value']['properties']['white-space'],presets.keyword('normal'))
    def test_existing_art_is_reused(self):
        for new,old in [('settings_preset','settings_postaa'),('settings_autodetect','settings_back')]:
            for part in ('plate','focus'):
                expected=presets.renamed(self.nodes[old+'-'+part],old,new)
                self.assertEqual(self.nodes[new+'-'+part],expected)
        for id,node in self.nodes.items():
            if id.startswith(('settings_preset','settings_autodetect')):self.assertNotEqual(node['type'],'image')
    def test_all_locales_have_exact_six_labels(self):
        files=sorted((ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'))
        self.assertEqual({f.stem for f in files},{n+'_openq4' for n in ('english','french','italian','spanish','polish','russian')})
        for file in files:
            for id in ('229976','229977','229978','229985'):
                values=re.findall(r'"#str_'+id+r'"\s+"([^"\r\n]+)"',file.read_text(encoding='utf-8'))
                self.assertEqual(len(values),1,file.name+id)
                if id=='229977':self.assertEqual(len(values[0].split(';')),6)
    def test_no_duplicate_profile_values_in_page(self):
        self.assertEqual(self.page['actions']['preset']['arguments'],{'name':{'input':'value'}})
        self.assertEqual(self.page['events']['autoDetect'][0]['then'],[{'op':'action','action':'autoDetect'}])
        self.assertEqual(self.nodes['settings_apply']['control']['event'],'apply')
        self.assertIn({'state':'ui.numberDraftsPending'},self.page['events']['apply'][0]['condition']['args'][1]['args'])
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
        reject(lambda p:presets.nodes(p['root'])['settings_preset']['control']['options'][2].update(value='ultra'))
        for id in ('settings_preset','settings_autodetect'):
            reject(lambda p,id=id:next(b for b in p['bindings'] if b['id']==id+'.enabled').update(value=True))
        # Missing each individual gate still fails exhaustive truth-table checks.
        def replace(value,key):
            if value=={'state':key}:return True if key=='settings.open' else False
            if isinstance(value,list):return [replace(v,key) for v in value]
            if isinstance(value,dict):return {k:replace(v,key) for k,v in value.items()}
            return value
        for key in ('settings.open','settings.busy','settings.confirmationVisible','page.discardVisible','ui.numberDraftsPending'):
            reject(lambda p,key=key:p['events']['autoDetect'][0].update(condition=replace(p['events']['autoDetect'][0]['condition'],key)))

if __name__=='__main__':unittest.main()
