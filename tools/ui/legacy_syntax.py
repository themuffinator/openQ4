#!/usr/bin/env python3
"""Structured translation input from the engine's preprocessed GUI token stream.

Preserves ordered declarations, typed expressions and script branches. This is
an import model, not a runtime replacement or a claim of resolved game bindings.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = {'windowDef','animationDef','editDef','choiceDef','sliderDef','markerDef',
           'bindDef','listDef','fieldDef','renderDef','gameSSDDef','gameBearShootDef','gameBustOutDef'}
COUNTS = {'FLOAT':1,'BOOL':1,'INT':1,'VEC2':2,'VEC3':3,'VEC4':4,'RECTANGLE':4,'STRING':0}
SCALARS = set('showtime showcoords forceaspectwidth forceaspectheight matscalex matscaley bordersize '
              'nowrap textspacing shadow textalign textalignx textaligny textstyle wantenter naturalmatscale '
              'matcover matfit matcanvasfill nativescreenoverlay noclip nocursor menugui modal alwaysthink '
              'chatwindow invertrect stepsize step low high vertical verticalflip scrollbar choicetype '
              'currentchoice maxchars numeric wrap readonly forcescroll password cvarmax horizontal multiplesel'.split())
STRINGS = set('name play comment font screenalignx screenalignh screenaligny screenalignv '
              'thumbshader source listname tabstops tabaligns tabvaligns tabtypes tabtextscales '
              'tabiconsizes tabiconvoffset needupdate'.split())
SINGLE_VALUES = set('notime cvarmin value liveupdate cvargroup updategroup needsrender '
                    'outlinecolor outlinewidth rimlightcolor brightskincolor'.split())


def grammar() -> dict:
    window = (ROOT/'src/ui/Window.cpp').read_text(encoding='utf-8')
    scripts = (ROOT/'src/ui/GuiScript.cpp').read_text(encoding='utf-8')
    register_block = re.search(r'idWindow::RegisterVars\[\]\s*=\s*\{(.*?)\n\};',window,re.S).group(1)
    script_block = re.search(r'idWindow::ScriptNames\[\]\s*=\s*\{(.*?)\n\};',window,re.S).group(1)
    command_block = re.search(r'commandList\[\]\s*=\s*\{(.*?)\n\};',scripts,re.S).group(1)
    return {
        'registers':{name.lower():kind for name,kind in re.findall(r'\{\s*"([^"]+)"\s*,\s*idRegister::(\w+)',register_block)},
        'events':set(re.findall(r'"([^"]+)"',script_block)),
        'commands':{name.lower():(int(low),int(high)) for name,low,high in re.findall(r'\{\s*"([^"]+)"\s*,\s*\w+\s*,\s*(\d+)\s*,\s*(\d+)',command_block)},
        'source_sha256':{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
                         ('src/ui/Window.cpp','src/ui/GuiScript.cpp','src/ui/RegExp.cpp')},
    }


class SyntaxError(ValueError):
    pass


def dependencies(nodes: list[dict]) -> dict:
    states, cvars, strings = set(),set(),set()
    assets, requests, symbols = [],[],[]
    asset_kinds = {'font':'font','background':'material','backgroundhover':'material',
                   'backgroundfocus':'material','backgroundline':'material','backgroundgreyed':'material',
                   'varbackground':'material','thumbshader':'material','customshader':'material',
                   'model':'model','model1':'model','model2':'model','skin':'skin','skin1':'skin',
                   'anim':'animation','anim1':'animation','anim2':'animation','gui':'gui'}

    def value(text):
        if not isinstance(text,str):
            return
        if re.fullmatch(r'\$?gui::[A-Za-z0-9_.-]+',text,re.I):
            states.add(text.lstrip('$')[5:])
        strings.update(re.findall(r'#str_[A-Za-z0-9_]+',text))

    def walk(node, pointer, owner):
        kind = node.get('kind')
        if kind == 'window':
            owner = pointer
            symbols.append({'name':node['name'],'type':node['type'],'pointer':pointer,'span':node['span']})
        if kind == 'property':
            name = node['name'].lower()
            content = node.get('value')
            if name == 'cvar' and content:
                cvars.add(content)
            asset_kind = asset_kinds.get(name,'material' if name.startswith('mtr_') else None)
            if asset_kind and content is not None:
                assets.append({'kind':asset_kind,'value':content,'pointer':pointer,'owner':owner,
                               'classification':'runtime-binding' if content.lower().startswith('gui::') else 'pending'})
        elif kind == 'icon':
            assets.append({'kind':'material','value':node['material'],'pointer':pointer,'owner':owner,'classification':'pending'})
        elif kind == 'command':
            args = [arg['value'] for arg in node['arguments']]
            name = node['name'].lower()
            if name == 'set' and args:
                if args[0].lower() == 'cmd':
                    requests.append({'kind':'gui-command','fragments':args[1:],'owner':owner,'pointer':pointer})
                if args[0].lower().split('::')[-1] in asset_kinds and len(args) > 1:
                    assets.append({'kind':asset_kinds[args[0].lower().split('::')[-1]],'value':args[1],
                                   'pointer':pointer,'owner':owner,'classification':'pending'})
            elif name in ('consolecmd','runscript','endgame'):
                requests.append({'kind':name,'fragments':args,'owner':owner,'pointer':pointer})
        for key,item in node.items():
            if key in ('span','grammar_source_sha256'):
                continue
            if isinstance(item,dict):
                walk(item,pointer+'/'+key,owner)
            elif isinstance(item,list):
                for i,child in enumerate(item):
                    if isinstance(child,dict):
                        walk(child,pointer+'/'+key+'/'+str(i),owner)
                    else:
                        value(child)
            else:
                value(item)
    for index,node in enumerate(nodes):
        walk(node,f'/statements/{index}','')
    names = Counter(symbol['name'].lower() for symbol in symbols)
    return {'windows':symbols,'duplicate_window_names':{name:count for name,count in names.items() if count > 1},
            'gui_state_references':sorted(states),'cvar_references':sorted(cvars),'localization_keys':sorted(strings),
            'asset_references':assets,'application_requests':requests,'binding_resolution':'pending'}


class Parser:
    def __init__(self, data: dict, rules: dict | None = None):
        self.data = data
        self.tokens = data['tokens']
        self.i = 0
        self.rules = rules or grammar()
        self.events = {name.lower() for name in self.rules['events']}
        self.diagnostics = []
        self.counts = Counter()

    def peek(self, value: str | None = None) -> bool:
        return self.i < len(self.tokens) and (value is None or self.tokens[self.i][5] == value)

    def value(self) -> str:
        return self.tokens[self.i][5] if self.peek() else ''

    def take(self, expected: str | None = None) -> str:
        if not self.peek() or (expected is not None and not self.peek(expected)):
            raise SyntaxError(f'expected {expected or "token"}, found {self.value()!r}')
        result = self.value()
        self.i += 1
        return result

    def where(self, index: int) -> dict:
        if index >= len(self.tokens):
            return {'token':index}
        token = self.tokens[index]
        return {'token':index,'parser_source':token[4],'line':token[2]}

    def diagnostic(self, code: str, message: str, index: int | None = None):
        self.diagnostics.append({'code':code,'message':message,**self.where(self.i if index is None else index)})

    def expression(self, priority=4, depth=0) -> dict:
        if depth > 64:
            raise SyntaxError('expression nesting exceeds 64')
        start = self.i
        if priority == 0:
            token = self.tokens[self.i] if self.peek() else None
            value = self.take()
            if value == '(':
                node = {'kind':'group','value':self.expression(depth=depth+1)}
                self.take(')')
            elif value == '-':
                if not self.peek() or (self.tokens[self.i][0] != 3 and self.value() != '.'):
                    raise SyntaxError('legacy negative terms require a number')
                node = {'kind':'number','spelling':'-'+self.take()}
            elif token[0] == 3 or value == '.':
                node = {'kind':'number','spelling':value}
            elif token[0] in (1,2,4):
                node = {'kind':'time' if value.lower() == 'time' else 'reference','name':value,'token_type':token[0]}
                if self.peek('['):
                    self.take('[')
                    node = {'kind':'index','target':node,'index':self.expression(depth=depth+1),
                            'resolution':'table-or-vector-pending'}
                    self.take(']')
            elif value == '\\':
                # Two shipped monitor sources contain a bare backslash where
                # a color alpha belongs. Native ParseTerm defers this as a
                # variable name. Retain its structure and require review.
                node = {'kind':'unresolved_reference','name':value,'token_type':token[0]}
                self.diagnostic('unresolved_legacy_term','bare backslash is treated as a variable by the legacy parser',start)
            else:
                raise SyntaxError(f'invalid expression term {value!r}')
        else:
            node = self.expression(priority-1,depth)
            operators = {1:{'*','/','%'},2:{'+','-'},3:{'>','>=','<','<=','==','!='},4:{'&&','||','?'}}
            if self.value() in operators[priority]:
                operator = self.take()
                # idWindow parses the right operand at the same priority:
                # subtraction/division are right-associative, unlike C/C++.
                right = self.expression(priority,depth+1)
                if operator == '?':
                    self.take(':')
                    node = {'kind':'conditional','condition':node,'yes':right,
                            'no':self.expression(priority-1,depth+1)}
                else:
                    node = {'kind':'binary','operator':operator,'left':node,'right':right}
        node['span'] = [start,self.i]
        return node

    def expressions(self, count: int) -> list[dict]:
        result = []
        for i in range(count):
            if i:
                self.take(',')
            result.append(self.expression())
        return result

    def script(self, depth=0) -> list[dict]:
        if depth > 64:
            raise SyntaxError('script nesting exceeds 64')
        self.take('{')
        nodes = []
        while not self.peek('}'):
            nodes.append(self.script_statement(depth))
        self.take('}')
        return nodes

    def script_statement(self, depth: int) -> dict:
        start = self.i
        command = self.take()
        self.counts['script_statements'] += 1
        if command.lower() == 'if':
            node = {'kind':'if','condition':self.expression(),'yes':self.script(depth+1),'no':[]}
            if self.peek('else'):
                self.take()
                if self.value().lower() == 'if':
                    if depth >= 64:
                        raise SyntaxError('else-if nesting exceeds 64')
                    node['no'] = [self.script_statement(depth+1)]
                else:
                    node['no'] = self.script(depth+1)
        else:
            args = []
            while self.peek() and self.value() not in (';','}'):
                if self.peek('{'):
                    raise SyntaxError('unexpected script brace')
                args.append({'value':self.take(),'token_type':self.tokens[self.i-1][0]})
            semicolon = self.peek(';')
            if semicolon:
                self.take()
            node = {'kind':'command','name':command,'arguments':args,'semicolon':semicolon}
            arity = self.rules['commands'].get(command.lower())
            if not arity:
                self.diagnostic('unknown_script_command',command,start)
            elif not arity[0] <= len(args) <= arity[1]:
                self.diagnostic('script_arity',f'{command}: expected {arity}, found {len(args)}',start)
        node['span'] = [start,self.i]
        return node

    def statements(self, depth=0, braced=False) -> list[dict]:
        if depth > 64:
            raise SyntaxError('window nesting exceeds 64')
        nodes = []
        previous_time = 0
        while self.peek() and not (braced and self.peek('}')):
            start = self.i
            key = self.take()
            low = key.lower()
            if key in WINDOWS:
                name = self.take()
                self.take('{')
                node = {'kind':'window','type':key,'name':name,'children':self.statements(depth+1,True)}
                self.take('}')
                self.counts['windows'] += 1
            elif low in self.events or key in ('onTime','onNamedEvent'):
                node = {'kind':'event','name':key}
                if key == 'onNamedEvent':
                    node['argument'] = self.take()
                elif key == 'onTime':
                    relative = self.peek('+')
                    if relative:
                        self.take()
                    spelling = self.take()
                    if not re.fullmatch(r'[+-]?\d+',spelling):
                        raise SyntaxError(f'invalid timeline time {spelling!r}')
                    time = int(spelling) + (previous_time if relative else 0)
                    if not -2147483648 <= time <= 2147483647:
                        raise SyntaxError('timeline time overflow')
                    node.update(milliseconds=time,relative=relative)
                    previous_time = time
                node['statements'] = self.script()
                self.counts['events'] += 1
            elif key in ('definefloat','float','definevec4'):
                node = {'kind':'definition','type':key,'name':self.take(),
                        'values':self.expressions(4 if key == 'definevec4' else 1)}
                self.counts['definitions'] += 1
            elif key == 'defineicon':
                node = {'kind':'icon','name':self.take(),'material':self.take()}
                if self.peek(','):
                    self.take()
                    node['rect'] = self.expressions(4)
                self.counts['icons'] += 1
            elif key in ('}', '{', ';'):
                raise SyntaxError(f'unexpected declaration token {key!r}')
            else:
                kind = self.rules['registers'].get(low)
                node = {'kind':'property','name':key}
                if low in STRINGS or low.startswith('mtr_') or kind == 'STRING':
                    node.update(type='string',value=self.take())
                elif low in SCALARS:
                    sign = self.take() if self.value() in ('-','+') else ''
                    node.update(type='scalar',value=sign+self.take())
                elif low == 'shear':
                    node.update(type='VEC2',values=self.expressions(2))
                elif kind:
                    node.update(type=kind,values=self.expressions(COUNTS[kind]))
                elif low in SINGLE_VALUES:
                    node.update(type='native_value',value=self.take())
                else:
                    # The native fallback consumes a value and the rest of its
                    # line as a defined variable. Keep all operands for review.
                    value = [self.take()]
                    while self.peek() and self.tokens[self.i][3] == 0:
                        value.append(self.take())
                    node.update(type='legacy_defined_value',tokens=value,resolution='pending')
                    self.counts['custom_properties'] += 1
                self.counts['properties'] += 1
            node['span'] = [start,self.i]
            nodes.append(node)
        return nodes

    def parse(self) -> dict:
        nodes = []
        completed = False
        try:
            nodes = self.statements()
            completed = True
        except (SyntaxError,RecursionError) as error:
            self.diagnostic('syntax',str(error))
        return {'format':1,'source':self.data['source'],'replacement_acceptance':False,
                'complete_syntax':not self.diagnostics and self.i == len(self.tokens),
                'complete_token_coverage':completed and self.i == len(self.tokens),
                'tokens_consumed':self.i,'token_count':len(self.tokens),'statements':nodes,
                'counts':dict(self.counts),'diagnostics':self.diagnostics,
                'dependencies':dependencies(nodes),
                'grammar_source_sha256':self.rules['source_sha256']}


def analyze_capture(folder: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError('use a fresh syntax output directory')
    capture = json.loads((folder/'capture.json').read_text())
    imports = capture['legacy_import']
    if not imports['passed']:
        raise ValueError('native export/source/diagnostic qualification failed')
    output.mkdir(parents=True)
    rules = grammar()
    reports = []
    total = Counter()
    state_refs, cvar_refs, string_refs = set(),set(),set()
    asset_refs, application_requests = Counter(),Counter()
    for record in imports['resources']:
        source = folder/'save/baseoq4'/record['output']
        if hashlib.sha256(source.read_bytes()).hexdigest() != record['output_sha256']:
            raise ValueError(f'token file changed: {source}')
        data = json.loads(source.read_text(encoding='ascii'))
        result = Parser(data,rules).parse()
        result['source_sha256'] = record['sha256']
        result['native_tokens_sha256'] = record['output_sha256']
        destination = output/Path(record['output']).name
        destination.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        total.update(result['counts'])
        deps = result['dependencies']
        state_refs.update(deps['gui_state_references'])
        cvar_refs.update(deps['cvar_references'])
        string_refs.update(deps['localization_keys'])
        asset_refs.update(row['kind'] for row in deps['asset_references'])
        application_requests.update(row['kind'] for row in deps['application_requests'])
        reports.append({key:result[key] for key in ('source','source_sha256','complete_syntax','complete_token_coverage','tokens_consumed','token_count','counts','diagnostics')})
    summary = {'resources':reports,'counts':dict(total),'complete_syntax':sum(row['complete_syntax'] for row in reports),
               'complete_token_coverage':sum(row['complete_token_coverage'] for row in reports),
               'dependencies':{'unique_gui_state_references':len(state_refs),'unique_cvar_references':len(cvar_refs),
                               'unique_localization_keys':len(string_refs),'asset_references':dict(asset_refs),
                               'application_requests':dict(application_requests),'binding_resolution':'pending'},
               'replacement_acceptance':False,'grammar_source_sha256':rules['source_sha256']}
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    return summary


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('capture',type=Path)
    cli.add_argument('--output',type=Path,required=True)
    args = cli.parse_args()
    result = analyze_capture(args.capture,args.output)
    print(json.dumps({key:value for key,value in result.items() if key != 'resources'},indent=2))
