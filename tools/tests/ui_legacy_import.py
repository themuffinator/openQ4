#!/usr/bin/env python3
"""Test native import provenance and legacy expression/script preservation."""
from pathlib import Path
import hashlib
import json
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/ui'))
import legacy_import
from legacy_syntax import Parser, grammar


class Quoted(str):
    pass


def data(*lines):
    tokens = []
    punctuation = set('{}(),;[]+-*/%') | {'&&','||','>=','<=','==','!=','>','<','?',':'}
    for line,values in enumerate(lines,1):
        for index,value in enumerate(values):
            value = str(value) if not isinstance(value,str) else value
            kind = 1 if isinstance(value,Quoted) else 3 if re.fullmatch(r'\d+(?:\.\d+)?',value) else 5 if value in punctuation else 4
            tokens.append([kind,0,line,int(index == 0),'guis/test.gui',value])
    return {'source':'guis/test.gui','tokens':tokens}


def number(node):
    if node['kind'] == 'number':
        return float(node['spelling'])
    if node['kind'] == 'group':
        return number(node['value'])
    a,b = number(node['left']),number(node['right'])
    return {'+':lambda:a+b,'-':lambda:a-b,'*':lambda:a*b,'/':lambda:a/b}[node['operator']]()


class SyntaxTests(unittest.TestCase):
    def setUp(self):
        self.rules = grammar()

    def parse(self,*lines):
        result = Parser(data(*lines),self.rules).parse()
        self.assertTrue(result['complete_syntax'],result['diagnostics'])
        self.assertEqual(result['tokens_consumed'],result['token_count'])
        self.assertFalse(result['replacement_acceptance'])
        return result

    def test_right_association_and_arithmetic_priority(self):
        for tokens,expected in [(('20','/','4','/','2'),10),(('10','-','3','-','1'),8),
                                (('2','+','3','*','4'),14),(('(','2','+','3',')','*','4'),20)]:
            parser = Parser(data(tokens),self.rules)
            self.assertEqual(number(parser.expression()),expected)
            self.assertEqual(parser.i,len(tokens))

    def test_logical_operators_share_native_priority(self):
        parser = Parser(data(('a','&&','b','||','c')),self.rules)
        node = parser.expression()
        self.assertEqual(node['operator'],'&&')
        self.assertEqual(node['right']['operator'],'||')

    def test_typed_rect_and_quoted_state_binding(self):
        result = self.parse(('windowDef','Desktop','{'),('rect','0',',','0',',','640',',','480'),
                            ('visible','(',Quoted('gui::open'),'==','1',')'),('}',))
        props = result['statements'][0]['children']
        self.assertEqual(props[0]['type'],'RECTANGLE')
        self.assertEqual([number(x) for x in props[0]['values']],[0,0,640,480])
        self.assertEqual(props[1]['values'][0]['value']['left']['name'],'gui::open')

    def test_if_else_chain_and_command_arguments(self):
        result = self.parse(('windowDef','Desktop','{','onAction','{'),
                            ('if','(','gui::a','==','1',')','{','set',Quoted('cmd'),Quoted('play sound/x; close'),';','}'),
                            ('else','if','(','gui::b','==','2',')','{','resetTime','0',';','}'),
                            ('else','{','setFocus',Quoted('button'),'}','}','}'))
        branch = result['statements'][0]['children'][0]['statements'][0]
        self.assertEqual(branch['yes'][0]['arguments'][1]['value'],'play sound/x; close')
        self.assertEqual(branch['no'][0]['kind'],'if')
        self.assertEqual(branch['no'][0]['no'][0]['name'],'setFocus')
        self.assertFalse(branch['no'][0]['no'][0]['semicolon'])

    def test_relative_timeline_is_local_to_window(self):
        result = self.parse(('windowDef','Desktop','{','onTime','100','{','}','onTime','+','50','{','}'),
                            ('windowDef','Child','{','onTime','+','25','{','}','}','onTime','+','10','{','}','}'))
        nodes = result['statements'][0]['children']
        self.assertEqual([n['milliseconds'] for n in nodes if n['kind']=='event'],[100,150,160])
        self.assertEqual(nodes[2]['children'][0]['milliseconds'],25)

    def test_names_and_strings_cannot_create_fake_declarations(self):
        result = self.parse(('windowDef','onTime','{','text',Quoted('windowDef Fake { onAction { } }')),
                            ('onNamedEvent','background','{','set',Quoted('text'),Quoted('#str_1'),';','}','}'))
        self.assertEqual(result['counts']['windows'],1)
        self.assertEqual(result['counts']['events'],1)
        self.assertEqual(result['statements'][0]['name'],'onTime')

    def test_ordered_duplicate_windows_are_retained(self):
        result = self.parse(('windowDef','Desktop','{','windowDef','same','{','}','windowDef','same','{','}','}'))
        children = result['statements'][0]['children']
        self.assertEqual([x['name'] for x in children],['same','same'])
        self.assertLess(children[0]['span'][0],children[1]['span'][0])

    def test_definitions_icon_and_native_single_value(self):
        result = self.parse(('windowDef','Desktop','{','notime','1','visible','1'),
                            ('definefloat','value','10','-','3','-','1'),
                            ('definevec4','tint','1',',','0',',','0',',','1'),
                            ('defineicon',Quoted('mark'),Quoted('gfx/icon'),',','0',',','0',',','16',',','16'),('}',))
        nodes = result['statements'][0]['children']
        self.assertEqual(len(nodes),5)
        self.assertEqual(number(nodes[2]['values'][0]),8)
        self.assertEqual(nodes[4]['material'],'gfx/icon')

    def test_custom_line_preserves_all_operands(self):
        result = self.parse(('windowDef','Desktop','{'),('custom','one','two','three'),('}',))
        node = result['statements'][0]['children'][0]
        self.assertEqual(node['type'],'legacy_defined_value')
        self.assertEqual(node['tokens'],['one','two','three'])
        self.assertEqual(node['resolution'],'pending')

    def test_dependency_manifest_retains_action_fragments_without_execution(self):
        result = self.parse(('windowDef','D','{','font',Quoted('fonts/marine'),'background',Quoted('gui::image')),
                            ('text',Quoted('#str_1'),'cvar',Quoted('s_volume')),
                            ('onAction','{','set',Quoted('cmd'),Quoted('applySettings'),Quoted('$gui::value'),';',
                             'consolecmd',Quoted('exec example.cfg'),';','}','}'))
        refs = result['dependencies']
        self.assertEqual(refs['gui_state_references'],['image','value'])
        self.assertEqual(refs['cvar_references'],['s_volume'])
        self.assertEqual(refs['localization_keys'],['#str_1'])
        self.assertEqual(refs['application_requests'][0]['fragments'],['applySettings','$gui::value'])
        self.assertEqual(refs['application_requests'][1]['kind'],'consolecmd')
        self.assertEqual(refs['asset_references'][1]['classification'],'runtime-binding')
        self.assertEqual(refs['binding_resolution'],'pending')

    def test_unqualified_script_and_broken_geometry_remain_diagnostics(self):
        for lines,code in [([('windowDef','D','{','onAction','{','unknown','x',';','}','}')],'unknown_script_command'),
                           ([('windowDef','D','{','onAction','{','setFocus',';','}','}')],'script_arity'),
                           ([('windowDef','D','{','rect','0',',','0','}')],'syntax'),
                           ([('windowDef','D','{','onTime','2147483648','{','}','}')],'syntax')]:
            result = Parser(data(*lines),self.rules).parse()
            self.assertFalse(result['complete_syntax'])
            self.assertEqual(result['diagnostics'][0]['code'],code)

    def test_malformed_legacy_color_keeps_structure_but_requires_review(self):
        source = data(('windowDef','D','{','matcolor','1',',','1',',','1',',','\\'),('}',))
        source['tokens'][-2][0] = 5
        result = Parser(source,self.rules).parse()
        self.assertFalse(result['complete_syntax'])
        self.assertTrue(result['complete_token_coverage'])
        self.assertEqual(result['statements'][0]['children'][0]['values'][3]['kind'],'unresolved_reference')
        self.assertEqual(result['diagnostics'][0]['code'],'unresolved_legacy_term')
        term = result['statements'][0]['children'][0]['values'][3]
        self.assertEqual(term['name'],'\\')
        self.assertEqual(term['span'],[10,11])
        self.assertEqual(term['native_semantics'],{'lookup':'table-then-window-variable',
            'fixup':'once-after-window-parse','unbound_after_fixup':0.0,'constant_lowering':False})
        self.assertIn('tokens alone do not authorize constant lowering',result['diagnostics'][0]['message'])
        self.assertEqual(result['diagnostics'][0]['token'],10)
        self.assertEqual(result['diagnostics'][0]['parser_source'],'guis/test.gui')

    def test_backslash_following_literal_n_is_not_repaired_as_an_escape(self):
        for split in (False,True):
            tail = [('n','matscalex'),('-','1')] if split else [('n','matscalex','-','1')]
            source = data(('windowDef','D','{','matcolor','1',',','1',',','1',',','\\'),*tail,('}',))
            source['tokens'][10][0] = 5
            result = Parser(source,self.rules).parse()
            self.assertTrue(result['complete_token_coverage'])
            self.assertFalse(result['complete_syntax'])
            self.assertFalse(result['replacement_acceptance'])
            props=result['statements'][0]['children']
            self.assertEqual(props[0]['values'][3]['native_semantics']['unbound_after_fixup'],0)
            self.assertEqual(props[1]['name'],'n')
            self.assertEqual(props[1]['tokens'],['matscalex'] if split else ['matscalex','-','1'])
            if split:
                self.assertEqual(props[2]['name'],'-')
                self.assertEqual(props[2]['tokens'],['1'])

    def test_possible_backslash_definition_does_not_become_a_constant(self):
        source=data(('windowDef','D','{','matcolor','1',',','1',',','1',',','\\'),
                    ('float',Quoted('\\'),'0.375'),('}',))
        source['tokens'][10][0]=5
        result=Parser(source,self.rules).parse()
        self.assertTrue(result['complete_token_coverage'])
        term=result['statements'][0]['children'][0]['values'][3]
        self.assertEqual(term['kind'],'unresolved_reference')
        self.assertFalse(term['native_semantics']['constant_lowering'])
        self.assertEqual(result['statements'][0]['children'][1]['name'],'\\')

    def test_consuming_last_token_does_not_hide_incomplete_structure(self):
        result = Parser(data(('windowDef','D','{','rect','0',',','0',',','1',',','1')),self.rules).parse()
        self.assertEqual(result['tokens_consumed'],result['token_count'])
        self.assertFalse(result['complete_syntax'])
        self.assertFalse(result['complete_token_coverage'])


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'.tmp').mkdir(exist_ok=True)
        temp = tempfile.TemporaryDirectory(dir=ROOT/'.tmp',prefix='ui-import-test-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root/'ui-import').mkdir()
        self.source = 'windowDef D { text "\xe9" }'
        self.row = {'path':'guis/test.gui','sha256':hashlib.sha256(self.source.encode('latin-1')).hexdigest()}
        self.document = dict(data(('windowDef','D','{','}')),format=1,encoding='byte-preserving-latin-1',
                             source_bytes=self.source,parser_flags=6308)
        self.document['tokens'] = []
        self.write()
        self.log = 'UI_GUI_EXPORT_BEGIN guis/test.gui\nUI_GUI_EXPORT_END exported 0 ui-import/00000.json\n'

    def write(self):
        (self.root/'ui-import/00000.json').write_text(json.dumps(self.document),encoding='ascii')

    def test_native_bytes_match_sha_with_non_utf8_source(self):
        result = legacy_import.collect(self.root,self.log,[self.row])
        self.assertTrue(result['passed'])
        self.assertEqual(result['resources'][0]['runtime_source_sha256'],self.row['sha256'])
        self.assertTrue(legacy_import.collect(self.root,self.log.replace('\n','\r\n'),[self.row])['passed'])

    def test_errors_cannot_be_hidden_by_a_written_token_file(self):
        for log in (self.log.replace('UI_GUI_EXPORT_END','WARNING: missing include\nUI_GUI_EXPORT_END'),
                    self.log.replace('exported 0','failed'),self.log*2,self.log.replace('BEGIN guis/test.gui','BEGIN guis/other.gui')):
            self.assertFalse(legacy_import.collect(self.root,log,[self.row])['passed'])

    def test_wrong_source_hash_token_count_and_identity_are_rejected(self):
        for key,value in [('source_bytes','edited'),('source','guis/other.gui'),('format',2),('tokens',[[4,0,1,0,'test','x']])]:
            previous = self.document[key]
            self.document[key] = value
            self.write()
            self.assertFalse(legacy_import.collect(self.root,self.log,[self.row])['passed'])
            self.document[key] = previous

    def test_request_paths_are_not_console_commands(self):
        path = self.root/'requests.json'
        for name in ('guis/../../other','guis/a";quit','guis/a\nquit','guis/a\\b','C:/a'):
            path.write_text(json.dumps([dict(self.row,path=name)]))
            with self.assertRaises(ValueError):
                legacy_import.requests(path)
        path.write_text(json.dumps([self.row,self.row]))
        with self.assertRaises(ValueError):
            legacy_import.requests(path)
        path.write_text(json.dumps([self.row]))
        self.assertEqual(legacy_import.requests(path),[self.row])


if __name__ == '__main__':
    unittest.main()
