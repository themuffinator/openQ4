#!/usr/bin/env python3
"""Source contracts for production SYSTEM's shared slider/Number components.

Checks authoring, ownership/bindings, source preservation and the paired row's
constraints. This is not Rml layout, input, transaction or visual qualification.
"""
from pathlib import Path
import copy
import itertools
import json
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ui'))
import build_retained_screens as screens
import retained_system as system
SOURCE=screens.OUTPUTS['system']
PAIRS={'settings_brightness':('r_brightness',.5,2),'settings_ambient':('r_forceAmbient',0,1)}
# The typed window and custom sizes: each row is itself the Number control.
SIZES={'settings_window_width':('r_windowWidth','edit.r_windowWidth',320),'settings_window_height':('r_windowHeight','edit.r_windowHeight',240),
       'settings_custom_width':('r_customWidth','size.r_customWidth',320),'settings_custom_height':('r_customHeight','size.r_customHeight',240)}
PARTS=('viewport','text','selection','caret','composition','validation')
IMAGE=2  # page.tab of the IMAGE QUALITY tab, which holds both paired rows

def load(path):
    return json.loads('\n'.join(line for line in path.read_text(encoding='utf-8').splitlines() if not line.lstrip().startswith('//')))

def index_nodes(root):
    result={}
    def visit(current):
        if current['id'] in result: raise ValueError('Duplicate authored node: '+current['id'])
        result[current['id']]=current
        for child in current.get('children',[]): visit(child)
    visit(root)
    return result

def controls(node):
    """The controls a row holds in reading order, as the sub-page reads them."""
    if 'control' in node: return [node['id']]
    return [found for child in node.get('children',[]) for found in controls(child)]

def keyword(data): return {'type':'keyword','value':data}

def contract(page):
    """The paired rows' inventory, shared proposal, reading order and parts."""
    nodes=index_nodes(page['root'])
    roles=lambda role:{id for id,n in nodes.items() if n.get('control',{}).get('role')==role}
    assert roles('slider')==set(PAIRS),'Review the numeric field contract when the slider inventory changes'
    assert roles('number')=={id+'_number' for id in PAIRS}|set(SIZES)
    assert page['extensions']['openq4']['numberFields']['pairs']=={id:id+'_number' for id in PAIRS}
    bindings={(b['node'],b['property']):b['value'] for b in page['bindings']}
    timelines={t['id']:t for t in page['timelines']}
    for slider_id,(key,minimum,maximum) in PAIRS.items():
        number_id=slider_id+'_number';slider=nodes[slider_id]['control'];number=nodes[number_id]['control']
        for spec in (slider,number):
            assert spec['value']=={'state':'settings.draft.'+key} and spec['action']=='edit.'+key
            assert (spec['minimum'],spec['maximum'])==(minimum,maximum)
        assert page['actions']['edit.'+key]=={'input':'number','operation':'settings.system.edit','arguments':{key:{'input':'value'}}}
        assert 'step' not in number and 'decimals' not in number
        assert number['maxBytes']==128 and number['exponent'] is True
        # One row: the slider, then its Number, neither row group a control.
        row,holder=nodes[slider_id+'_row'],nodes[slider_id+'_row-controls']
        assert 'control' not in row and 'control' not in holder
        assert controls(row)==[slider_id,number_id]
        assert slider['navigation']['down']==number_id and number['navigation']['up']==slider_id
        assert nodes[slider_id+'-value']['properties']['display']==keyword('none')
        assert bindings[(number_id,'enabled')]==bindings[(slider_id,'enabled')]
        assert bindings[(slider_id,'enabled')]['args'][0]=={'op':'==','args':[{'state':'page.tab'},IMAGE]}
        assert number['parts']=={part:number_id+'-'+part for part in PARTS}
        for role in ('default','hover','focus','pressed','disabled'):
            assert [(t['node'],t['property']) for t in timelines[number_id+'.'+role]['tracks']]==[(number_id+'-field-focus','opacity')]
    for size_id,(key,action,minimum) in SIZES.items():
        spec=nodes[size_id]['control']
        assert (spec['action'],spec['value'])==(action,{'state':'settings.draft.'+key})
        assert (spec['minimum'],spec['maximum'],spec.get('integer'),spec['exponent'],spec['maxBytes'])==(minimum,16384,True,False,32)
        assert spec['parts']=={part:size_id+'-'+part for part in PARTS} and size_id+'-field' in nodes
        assert page['actions'][action]['arguments'][key]=={'input':'value'}
    sized(page,nodes,bindings)
    return nodes

# The view sources the rows follow: the text size (1 to 2) and the view's shape.
VIEWS=[{'view.text':text,'view.aspect':aspect} for text in (1,1.5,2) for aspect in (4/3,9/16)]
TALL=880.5/430.5   # retained_settings_rows.TALL_SCALE: a column's rows across the content's width
ROW_PITCH=36.0
NUMBER_FONT=15.0

def sized(page,nodes,bindings):
    """The typed fields grow with their rows: at every text size, and in a tall
    view, a row is the 100% row grown (by the text size, or TALL), the
    slider's Number and each field's viewport stay inside it, and the field's
    text (whose size the runtime multiplies by the text size) grows with it."""
    assert page['state']['view.text']=={'type':'number','initial':1,'view':'text'}
    assert page['state']['view.aspect']['view']=='aspect'
    for state in VIEWS:
        def length(id,prop):
            value=bindings.get((id,prop),nodes[id]['properties'][prop]['value'] if prop in nodes[id]['properties'] else 0)
            return evaluate(value,state)
        tall=state['view.aspect']<.76
        def grown(row):
            factor=length(row,'height')/ROW_PITCH
            assert abs(factor-(TALL if tall else state['view.text']))<1e-3,(row,state)
            return factor
        def field(row,viewport,text,factor):
            assert length(viewport,'top')+length(viewport,'height')<=length(row,'height')+1e-3,(viewport,state)
            # The runtime multiplies a font length by the text size itself.
            assert abs(length(text,'font-size')*state['view.text']-NUMBER_FONT*factor)<1e-3,(text,state)
        for slider_id in PAIRS:
            row,holder,number=slider_id+'_row',slider_id+'_row-controls',slider_id+'_number'
            factor=grown(row)
            assert length(holder,'left')>=length(slider_id,'width')-1e-3,(slider_id,state)
            assert length(holder,'left')+length(holder,'width')<=length(row,'width')+1e-3,(slider_id,state)
            assert abs(length(holder,'height')-length(row,'height'))<1e-3 and abs(length(slider_id,'height')-length(row,'height'))<1e-3,(slider_id,state)
            field(holder,number+'-viewport',number+'-text',factor)
        for size_id in SIZES:
            factor=grown(size_id)
            assert length(size_id+'-viewport','left')+length(size_id+'-viewport','width')<=length(size_id,'width')+1e-3,(size_id,state)
            field(size_id,size_id+'-viewport',size_id+'-text',factor)

def evaluate(expression,state):
    if not isinstance(expression,dict):return expression
    if 'state' in expression:return state[expression['state']]
    operation,args=expression['op'],expression['args']
    if operation=='!':return not evaluate(args[0],state)
    if operation=='&&':return evaluate(args[0],state) and evaluate(args[1],state)
    if operation=='||':return evaluate(args[0],state) or evaluate(args[1],state)
    if operation=='==':return evaluate(args[0],state)==evaluate(args[1],state)
    if operation=='select':return evaluate(args[1] if evaluate(args[0],state) else args[2],state)
    # The view-source geometry: comparisons and the lengths' scaling.
    if operation in ('<','>','*','/'):
        left,right=evaluate(args[0],state),evaluate(args[1],state)
        return {'<':lambda:left<right,'>':lambda:left>right,'*':lambda:left*right,'/':lambda:left/right}[operation]()
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
        cls.page=load(SOURCE)
        cls.nodes=index_nodes(cls.page['root'])

    def test_generated_page_reproduces_source(self):
        # The generator's last pass frames every document in the safe area.
        rendered=screens.render(screens.safe_frame(system.system_document()))
        self.assertEqual(SOURCE.read_bytes().decode('utf-8').replace('\r\n','\n'),rendered.replace('\r\n','\n'))

    def test_complete_authored_slider_inventory(self):
        contract(self.page)
        self.assertEqual({id for id,n in self.nodes.items() if n.get('control',{}).get('role')=='slider'},set(PAIRS))
        self.assertEqual({id for id,n in self.nodes.items() if n.get('control',{}).get('role')=='number'},
                         {id+'_number' for id in PAIRS} |
                         {'settings_window_width','settings_window_height','settings_custom_width','settings_custom_height'})

    def test_shared_authoritative_proposal_and_finite_range(self):
        for slider_id,(key,_,_) in PAIRS.items():
            slider=self.nodes[slider_id]['control'];number=self.nodes[slider_id+'_number']['control']
            self.assertEqual(number['action'],slider['action']);self.assertEqual(number['value'],{'state':'settings.draft.'+key})
            self.assertEqual((number['minimum'],number['maximum']),(slider['minimum'],slider['maximum']))
            self.assertNotIn('step',number);self.assertNotIn('decimals',number)
            self.assertEqual(number['maxBytes'],128);self.assertIs(number['exponent'],True)
            self.assertEqual(self.page['actions'][number['action']],{'input':'number','operation':'settings.system.edit','arguments':{key:{'input':'value'}}})
            self.assertNotIn('cvar',self.page['state']['settings.draft.'+key])
            self.assertIn('settings.baseline.'+key,self.page['state'])

    def test_sibling_roles_focus_order_and_shared_row(self):
        for slider_id in PAIRS:
            number_id=slider_id+'_number'
            row=self.nodes[slider_id+'_row'];controls_group=self.nodes[slider_id+'_row-controls'];number=self.nodes[number_id]
            self.assertNotIn('control',row);self.assertNotIn('control',controls_group)
            self.assertEqual([n['id'] for n in row['children']],[slider_id,slider_id+'_row-controls'])
            self.assertEqual([n['id'] for n in controls_group['children']],[number_id])
            self.assertEqual(controls(row),[slider_id,number_id])
            self.assertEqual(self.nodes[slider_id]['control']['navigation']['down'],number_id)
            self.assertEqual(number['control']['navigation']['up'],slider_id)
            # The Number stands after the slider in the same row and shows the
            # value; the slider's own readout stays hidden.
            width=lambda node:node['properties']['width']['value']
            left=controls_group['properties']['left']['value']
            self.assertGreaterEqual(left,width(self.nodes[slider_id]))
            self.assertLessEqual(left+width(controls_group),width(row))
            self.assertEqual(controls_group['properties']['height'],row['properties']['height'])
            self.assertEqual(self.nodes[slider_id+'-value']['properties']['display'],keyword('none'))
            self.assertEqual(self.nodes[number['control']['parts']['validation']]['properties']['display'],keyword('none'))
            for owner in (self.nodes[slider_id],number):
                def scan(node):
                    for child in node.get('children',[]):
                        self.assertNotIn('control',child);scan(child)
                scan(owner)

    def test_shared_editable_vector_art_and_parts(self):
        for slider_id in PAIRS:
            number_id=slider_id+'_number';control=self.nodes[number_id]['control'];parts=control['parts']
            self.assertEqual(set(parts),set(PARTS))
            viewport=self.nodes[parts['viewport']]
            self.assertEqual(viewport['properties']['overflow'],keyword('hidden'))
            direct={n['id'] for n in viewport['children']}
            self.assertNotIn(parts['validation'],direct)
            for part in ('text','selection','caret','composition'):
                self.assertIn(parts[part],direct);self.assertNotIn('transform',self.nodes[parts[part]]['properties'])
            # One Number field component: the pair's plate is the size rows'.
            self.assertEqual(self.nodes[number_id+'-field']['paths'],self.nodes['settings_window_width-field']['paths'])
            self.assertEqual(self.nodes[number_id+'-field-focus']['paths'],self.nodes['settings_ambient_number-field-focus']['paths'])
            for role in ('default','hover','focus','pressed','disabled'):
                timeline=next(t for t in self.page['timelines'] if t['id']==number_id+'.'+role)
                self.assertEqual([(t['node'],t['property']) for t in timeline['tracks']],[(number_id+'-field-focus','opacity')])

    def test_typed_sizes_are_whole_number_fields(self):
        for size_id,(key,action,minimum) in SIZES.items():
            spec=self.nodes[size_id]['control']
            self.assertEqual((spec['minimum'],spec['maximum'],spec['integer'],spec['exponent']),(minimum,16384,True,False))
            self.assertEqual(spec['parts'],{part:size_id+'-'+part for part in PARTS})
            self.assertIn(size_id+'-field',[n['id'] for n in self.nodes[size_id]['children']])
        # A custom size selects the custom resolution, as the stock entry did.
        for key in ('r_customWidth','r_customHeight'):
            self.assertEqual(self.page['actions']['size.'+key],{'input':'number','operation':'settings.system.edit',
                                                                'arguments':{key:{'input':'value'},'r_mode':-1}})

    def test_bad_source_bindings_rejected(self):
        contract(self.page)
        def binding(page,name):return next(item for item in page['bindings'] if item['id']==name)
        for kind in ('action','binding','inventory','duplicate','order','readout','availability','motion','size','textsource','fixedfont','ungrown','tallwidth'):
            page=copy.deepcopy(self.page);nodes=index_nodes(page['root'])
            if kind=='action':page['actions']['edit.r_brightness']['operation']='settings.brightness.set'
            elif kind=='binding':nodes['settings_brightness']['control']['value']={'state':'settings.baseline.r_brightness'}
            elif kind=='inventory':nodes['settings_ambient']['control']['role']='button'
            elif kind=='duplicate':page['root']['children'].append(copy.deepcopy(nodes['settings_brightness']))
            elif kind=='order':nodes['settings_brightness_row']['children'].reverse()
            elif kind=='readout':nodes['settings_ambient-value']['properties']['display']=keyword('block')
            elif kind=='availability':binding(page,'settings_ambient_number.enabled').update(value=True)
            elif kind=='motion':next(t for t in page['timelines'] if t['id']=='settings_brightness_number.focus')['tracks'][0]['node']='settings-title'
            elif kind=='size':nodes['settings_custom_width']['control']['integer']=False
            # The rows no longer read the text size; a field's text kept at its
            # authored size (as fixed chrome) while its row grows; a field's
            # holder that stays 36 dp tall; a Number past its row in a tall view.
            elif kind=='textsource':page['state'].pop('view.text')
            elif kind=='fixedfont':binding(page,'tall.settings_ambient_number-text.font-size').update(value={'op':'/','args':[15,{'state':'view.text'}]})
            elif kind=='ungrown':binding(page,'tall.settings_brightness_row-controls.height').update(value=36.0)
            else:binding(page,'tall.settings_window_width-viewport.width')['value']['args'][1]=900.0
            with self.subTest(kind=kind),self.assertRaises((AssertionError,KeyError,ValueError)):contract(page)

    def test_engine_owned_local_draft_declarations_and_localization(self):
        self.assertEqual(self.page['state']['ui.numberDraftsPending'],{'type':'boolean','initial':False})
        self.assertEqual(self.page['state']['ui.numberDraftMessage'],{'type':'string','initial':'#str_230007'})
        self.assertEqual(self.page['actions']['focusNumberDraft'],{'operation':'ui.numberDrafts.focus','arguments':{}})
        self.assertEqual(self.page['presentationVariables']['numberDraftsPending'],
                         {'type':'boolean','initial':False,'value':{'state':'ui.numberDraftsPending'}})
        self.assertEqual(self.page['aliases']['numberDraftsPending'],{'variable':'numberDraftsPending'})
        for path in (ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'):
            text=path.read_text(encoding='utf-8')
            for key in ('#str_230004','#str_230005','#str_230006'):
                self.assertEqual(len(re.findall('"'+key+'"',text)),1,(path,key))

    def test_service_status_keys_resolve_in_each_locale(self):
        service=(ROOT/'src/ui/SettingsService.cpp').read_text(encoding='utf-8')
        keys=set(re.findall(r'"(#str_[0-9]+)"',service))
        for path in (ROOT/'content/baseoq4/pak0/strings').glob('*_openq4.lang'):
            entries=re.findall(r'"(#str_[0-9]+)"\s+"([^"\n]*)"',path.read_text(encoding='utf-8'))
            for key in keys:
                values=[text for candidate,text in entries if candidate==key]
                self.assertEqual(len(values),1,(path.name,key))
                self.assertTrue(values[0].strip(),(path.name,key))
        english=dict(re.findall(r'"(#str_[0-9]+)"\s+"([^"\n]*)"',
            (ROOT/'content/baseoq4/pak0/strings/english_openq4.lang').read_text(encoding='utf-8')))
        ready=self.page['state']['ui.numberDraftMessage']['initial']
        self.assertEqual(english[ready],'Ready')
        self.assertIn(ready,keys)

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
