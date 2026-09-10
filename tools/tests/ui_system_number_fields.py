#!/usr/bin/env python3
"""Source contracts for production SYSTEM's shared slider/Number components.

Checks authoring, ownership/bindings, source preservation and responsive
constraints. This is not Rml layout, input, transaction or visual qualification.
"""
from pathlib import Path
import copy
import importlib.util
import itertools
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('system_numbers',ROOT/'tools/ui/update_system_number_fields.py')
component=importlib.util.module_from_spec(spec);spec.loader.exec_module(component)

def evaluate(expression,state):
    if not isinstance(expression,dict):return expression
    if 'state' in expression:return state[expression['state']]
    operation,args=expression['op'],expression['args']
    if operation=='!':return not evaluate(args[0],state)
    if operation=='&&':return evaluate(args[0],state) and evaluate(args[1],state)
    if operation=='||':return evaluate(args[0],state) or evaluate(args[1],state)
    if operation=='==':return evaluate(args[0],state)==evaluate(args[1],state)
    if operation=='select':return evaluate(args[1] if evaluate(args[0],state) else args[2],state)
    raise AssertionError('Unexpected guarded expression '+operation)

def execute(steps,state,events,actions):
    for step in steps:
        if step['op']=='if':execute(step['then'] if evaluate(step['condition'],state) else step.get('else',[]),state,events,actions)
        elif step['op']=='setState':state.update({key:evaluate(value,state) for key,value in step['values'].items()})
        elif step['op']=='action':actions.append(step['action'])
        elif step['op']=='call':execute(events[step['event']],state,events,actions)
        else:raise AssertionError('Unexpected guarded operation '+step['op'])

class PairedNumbers(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.prefix,cls.page=component.load(ROOT/component.SOURCE)
        cls.nodes=component.index_nodes(cls.page['root'])

    def test_generated_page_is_idempotent(self):
        self.assertEqual(component.compose(self.page),self.page)

    def test_complete_authored_slider_inventory(self):
        self.assertEqual({id for id,n in self.nodes.items() if n.get('control',{}).get('role')=='slider'},set(component.PAIRS))
        self.assertEqual({id for id,n in self.nodes.items() if n.get('control',{}).get('role')=='number'},
                         {id+'_number' for id in component.PAIRS})

    def test_shared_authoritative_proposal_and_finite_range(self):
        for slider_id,(key,_) in component.PAIRS.items():
            slider=self.nodes[slider_id]['control'];number=self.nodes[slider_id+'_number']['control']
            self.assertEqual(number['action'],slider['action']);self.assertEqual(number['value'],{'state':'settings.draft.'+key})
            self.assertEqual((number['minimum'],number['maximum']),(slider['minimum'],slider['maximum']))
            self.assertNotIn('step',number);self.assertNotIn('decimals',number)
            self.assertEqual(number['maxBytes'],128);self.assertIs(number['exponent'],True)
            self.assertEqual(self.page['actions'][number['action']],{'input':'number','operation':'settings.system.edit','arguments':{key:{'input':'value'}}})
            self.assertNotIn('cvar',self.page['state']['settings.draft.'+key])
            self.assertIn('settings.baseline.'+key,self.page['state'])

    def test_sibling_roles_focus_order_and_wrapping(self):
        for slider_id in component.PAIRS:
            row=self.nodes[slider_id+'_row'];controls=self.nodes[slider_id+'_row-controls'];number=self.nodes[slider_id+'_number']
            self.assertNotIn('control',row);self.assertNotIn('control',controls)
            self.assertEqual([n['id'] for n in controls['children']],[slider_id,slider_id+'_number'])
            self.assertEqual(controls['properties']['flex-wrap'],component.keyword('wrap'))
            self.assertEqual(controls['properties']['column-gap'],component.length(8))
            self.assertEqual(self.nodes[slider_id]['properties']['min-width'],component.length(128))
            self.assertEqual(number['properties']['min-width'],component.length(96))
            self.assertEqual(self.nodes[number['control']['parts']['viewport']]['properties']['height'],component.length(36))
            self.assertEqual(self.nodes[slider_id+'-label']['properties']['width'],component.length(100,'%'))
            for owner in controls['children']:
                def scan(node):
                    for child in node.get('children',[]):
                        self.assertNotIn('control',child);scan(child)
                scan(owner)

    def test_shared_editable_vector_art_and_parts(self):
        for slider_id in component.PAIRS:
            number_id=slider_id+'_number';control=self.nodes[number_id]['control'];parts=control['parts']
            self.assertEqual(set(parts),{'viewport','text','selection','caret','composition','validation'})
            viewport=self.nodes[parts['viewport']]
            self.assertEqual(viewport['properties']['overflow'],component.keyword('hidden'))
            direct={n['id'] for n in viewport['children']}
            self.assertNotIn(parts['validation'],direct)
            for part in ('text','selection','caret','composition'):
                self.assertIn(parts[part],direct);self.assertNotIn('transform',self.nodes[parts[part]]['properties'])
            self.assertEqual(self.nodes[number_id+'-plate']['paths'],self.nodes[slider_id+'-plate']['paths'])
            self.assertEqual(self.nodes[number_id+'-focus']['paths'],self.nodes[slider_id+'-focus']['paths'])
            for role in ('default','hover','focus','pressed','disabled'):
                timeline=next(t for t in self.page['timelines'] if t['id']==number_id+'.'+role)
                self.assertEqual([(t['node'],t['property']) for t in timeline['tracks']],[(number_id+'-focus','opacity')])

    def test_unrelated_page_state_actions_events_and_aliases_preserved(self):
        changed=component.compose(self.page)
        for key in ('state','actions','events','aliases','presentationVariables','tokens'):
            self.assertEqual(changed[key],self.page[key])
        for id,node in self.nodes.items():
            if id.startswith(('settings_brightness','settings_ambient')):continue
            if node.get('control'):
                self.assertEqual(component.index_nodes(changed['root'])[id],node)

    def test_bad_source_bindings_rejected_without_mutation(self):
        for kind in ('action','binding','inventory','duplicate'):
            page=copy.deepcopy(self.page);nodes=component.index_nodes(page['root'])
            if kind=='action':page['actions']['edit.r_brightness']['operation']='settings.brightness.set'
            elif kind=='binding':nodes['settings_brightness']['control']['value']={'state':'settings.baseline.r_brightness'}
            elif kind=='inventory':nodes['settings_ambient']['control']['role']='button'
            else:page['root']['children'].append(copy.deepcopy(nodes['settings_brightness']))
            before=copy.deepcopy(page)
            with self.assertRaises(ValueError):component.compose(page)
            self.assertEqual(page,before)

    def test_drifted_shared_motion_requires_review(self):
        page=copy.deepcopy(self.page)
        timeline=next(t for t in page['timelines'] if t['id']=='settings_brightness.focus')
        timeline['tracks'][0]['node']='settings-title'
        with self.assertRaises(ValueError):component.compose(page)

    def test_engine_owned_local_draft_declarations_and_localization(self):
        self.assertEqual(self.page['state']['ui.numberDraftsPending'],{'type':'boolean','initial':False})
        self.assertEqual(self.page['state']['ui.numberDraftMessage'],{'type':'string','initial':'#str_229982'})
        self.assertEqual(self.page['actions']['focusNumberDraft'],{'operation':'ui.numberDrafts.focus','arguments':{}})
        for path in (ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'):
            text=path.read_text(encoding='utf-8')
            for key in ('#str_230004','#str_230005','#str_230006'):
                self.assertEqual(len(re.findall('"'+key+'"',text)),1,(path,key))

    def test_apply_and_apply_exit_guards_and_visual_state(self):
        bindings={binding['id']:binding['value'] for binding in self.page['bindings']}
        self.assertEqual(self.nodes['settings_apply']['control'].get('event'),'apply')
        self.assertNotIn('action',self.nodes['settings_apply']['control'])
        for pending,can_apply,busy,visible,confirmation,dirty,opened in itertools.product((False,True),repeat=7):
            state={'ui.numberDraftsPending':pending,'settings.canApply':can_apply,'settings.busy':busy,
                   'page.discardVisible':visible,'settings.confirmationVisible':confirmation,
                   'settings.dirty':dirty,'settings.open':opened,'settings.phase':1}
            can=can_apply and not pending
            self.assertEqual(evaluate(bindings['settings_apply.enabled'],state),can)
            self.assertEqual(evaluate(bindings['settings_apply.opacity'],state),1 if can else .4)
            actions=[];execute(self.page['events']['apply'],dict(state),self.page['events'],actions)
            self.assertEqual(actions,['apply'] if can else [])
            can_exit=can and visible and opened and not busy and not confirmation and dirty
            self.assertEqual(evaluate(bindings['discard_apply_changes.enabled'],state),can_exit)
            self.assertEqual(evaluate(bindings['discard_apply_changes.opacity'],state),1 if can_exit else .4)
            actions=[];after=dict(state);execute(self.page['events']['applyExit'],after,self.page['events'],actions)
            self.assertEqual(actions,['applyExit'] if can_exit else [])
            if can_exit:self.assertFalse(after['page.discardVisible'])

    def test_back_local_draft_continues_exact_field_without_dismiss(self):
        for dirty,pending in itertools.product((False,True),repeat=2):
            state={'ui.numberDraftsPending':pending,'settings.dirty':dirty,'settings.busy':False,
                   'page.discardVisible':False,'settings.phase':1}
            actions=[];execute(self.page['events']['onBack'],state,self.page['events'],actions)
            self.assertEqual(actions,[] if dirty or pending else ['dismiss'])
            self.assertEqual(state['page.discardVisible'],dirty or pending)
            if dirty or pending:
                actions=[];execute(self.page['events']['onBack'],state,self.page['events'],actions)
                self.assertFalse(state['page.discardVisible']);self.assertEqual(actions,['focusNumberDraft'])
        self.assertEqual(self.page['events']['continueEditing'],[
            {'op':'setState','values':{'page.discardVisible':False}},
            {'op':'action','action':'focusNumberDraft'}])

    def test_closed_service_local_only_discard_remains_recoverable(self):
        bindings={binding['id']:binding['value'] for binding in self.page['bindings']}
        for phase,opened,pending,busy,confirmation in itertools.product(range(8),(False,True),(False,True),(False,True),(False,True)):
            state={'settings.phase':phase,'settings.open':opened,'ui.numberDraftsPending':pending,'settings.busy':busy,
                   'settings.confirmationVisible':confirmation,'page.discardVisible':True}
            allowed=not busy and not confirmation and ((phase==1 and opened) or (phase==0 and not opened and pending))
            self.assertEqual(evaluate(bindings['discard_changes.enabled'],state),allowed)
            self.assertEqual(evaluate(bindings['discard-panel.display'],state),'flex' if allowed else 'none')
            actions=[];execute(self.page['events']['discard'],dict(state),self.page['events'],actions)
            self.assertEqual(actions,['cancel','dismiss'] if allowed else [])
            if phase==0 and not opened:self.assertFalse(evaluate(bindings['discard_keep_editing.enabled'],state))

    def test_local_message_has_priority_only_while_blocking(self):
        expression=next(b['value'] for b in self.page['bindings'] if b['id']=='settings-message.text')
        for pending,busy,confirmation,phase in itertools.product((False,True),(False,True),(False,True),range(8)):
            local=pending and not busy and not confirmation and phase in (0,1)
            self.assertEqual(evaluate(expression,{'ui.numberDraftsPending':pending,'ui.numberDraftMessage':'#str_230006',
                             'settings.message':'service result','settings.busy':busy,'settings.confirmationVisible':confirmation,
                             'settings.phase':phase}),'#str_230006' if local else 'service result')

if __name__=='__main__':unittest.main()
