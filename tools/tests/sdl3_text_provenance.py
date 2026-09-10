#!/usr/bin/env python3
"""Compile private Windows observer + actual SDL admission bodies with doubles.

No OS input/IME/window/clipboard operation. Pinned source is read or safely
provisioned into .tmp; the supplied/extracted SDL tree is never modified.
"""
from __future__ import annotations
import argparse
import configparser
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / 'subprojects/packagefiles/sdl3'
FILES = ['src/events/SDL_events.c', 'src/events/SDL_keyboard.c', 'src/video/windows/SDL_windowskeyboard.c', 'src/video/windows/SDL_windowsevents.c', 'src/video/windows/SDL_windowswindow.c', 'src/video/windows/SDL_windowsvideo.c']
def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdl-source',type=Path)
    parser.add_argument('--compiler',default='clang++')
    parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    scratch=Path(tempfile.mkdtemp(prefix='sdl-text-provenance-',dir=ROOT/'.tmp'))
    spec=importlib.util.spec_from_file_location('clipboard_source',ROOT/'tools/tests/sdl3_clipboard_status.py')
    assert spec and spec.loader
    helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
    helper.ROOT=ROOT;helper.FILES=FILES
    wrap=configparser.ConfigParser();wrap.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8')
    source,provision,_=helper.provision_source(args.sdl_source,dict(wrap['wrap-file']),scratch)
    projection=scratch/'sdl'
    for name in FILES:
        target=projection/name;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes((source/name).read_bytes().replace(b'\r\n',b'\n'))
    shutil.copyfile(source/'LICENSE.txt',projection/'LICENSE.txt')
    patch=PACKAGE/'text-provenance.patch'
    env=os.environ.copy();env['TEMP']=env['TMP']=str(scratch)
    def run(command,log):
        result=subprocess.run(command,cwd=scratch,env=env,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (scratch/log).write_text(result.stdout,encoding='utf-8');return result
    # A supplied tree may already contain precisely the patch.
    initialized=run(['git','-C',str(projection),'init','--quiet'],'git-init.log')
    if initialized.returncode: raise RuntimeError(initialized.stdout)
    fence=PACKAGE/'dispatch-fence.patch'
    queue=PACKAGE/'queue-consumer.patch'
    generation=PACKAGE/'queue-generation.patch'
    generated='OQ4_WindowsNativeFenceQueueGeneration' in (projection/FILES[0]).read_text(encoding='utf-8')
    if generated:
        reverse=run(['git','-C',str(projection),'apply','--reverse','--check',str(generation)],'generation-reverse-check.log')
        if reverse.returncode: raise RuntimeError('SDL source does not match the reviewed queue generation query')
        reverse=run(['git','-C',str(projection),'apply','--reverse',str(generation)],'generation-reverse.log')
        if reverse.returncode: raise RuntimeError(reverse.stdout)
    queued='OQ4_QueueAdmissionBegin' in (projection/FILES[0]).read_text(encoding='utf-8')
    if queued:
        reverse=run(['git','-C',str(projection),'apply','--reverse','--check',str(queue)],'queue-reverse-check.log')
        if reverse.returncode: raise RuntimeError('SDL source does not match the reviewed checked queue')
        reverse=run(['git','-C',str(projection),'apply','--reverse',str(queue)],'queue-reverse.log')
        if reverse.returncode: raise RuntimeError(reverse.stdout)
    fenced='OQ4_WIN_BeginFenceAdmission' in (projection/FILES[0]).read_text(encoding='utf-8')
    if fenced:
        # Validate the underlying observer patch independently, then restore
        # the exact combined admission body for the disabled-fence tests.
        reverse=run(['git','-C',str(projection),'apply','--reverse','--check',str(fence)],'fence-reverse-check.log')
        if reverse.returncode: raise RuntimeError('SDL source does not match the reviewed native fence')
        reverse=run(['git','-C',str(projection),'apply','--reverse',str(fence)],'fence-reverse.log')
        if reverse.returncode: raise RuntimeError(reverse.stdout)
    check=run(['git','-C',str(projection),'apply','--check',str(patch)],'patch-check.log')
    if check.returncode==0:
        applied=run(['git','-C',str(projection),'apply',str(patch)],'patch.log')
        if applied.returncode: raise RuntimeError(applied.stdout)
    else:
        reverse=run(['git','-C',str(projection),'apply','--reverse','--check',str(patch)],'patch-reverse-check.log')
        if reverse.returncode: raise RuntimeError('SDL source matches neither original nor patched private observer sources')
    if fenced:
        applied=run(['git','-C',str(projection),'apply',str(fence)],'fence-restore.log')
        if applied.returncode: raise RuntimeError(applied.stdout)
    if queued:
        applied=run(['git','-C',str(projection),'apply',str(queue)],'queue-restore.log')
        if applied.returncode: raise RuntimeError(applied.stdout)
    if generated:
        applied=run(['git','-C',str(projection),'apply',str(generation)],'generation-restore.log')
        if applied.returncode: raise RuntimeError(applied.stdout)
    decoded={name:(projection/name).read_bytes().decode('utf-8').replace('\r\n','\n') for name in FILES}
    public=PACKAGE/'include/SDL3/SDL_openq4_text_provenance.h'
    internal=PACKAGE/'src/video/windows/SDL_openq4_text_provenance.h'
    implementation=PACKAGE/'src/video/windows/SDL_openq4_text_provenance.c'
    api=public.read_text(encoding='utf-8');api=api[api.index('typedef enum OQ4_TextKind'):api.rindex('#ifdef __cplusplus')]
    internal_text=internal.read_text(encoding='utf-8');internal_text=internal_text[internal_text.index('typedef struct OQ4_TextScope'):internal_text.rindex('#endif')]
    code=implementation.read_text(encoding='utf-8');code=code[code.index('#define OQ4_TEXT_SLOTS'):code.rindex('#endif')]
    def write(name,text): (scratch/name).write_text(text,encoding='utf-8')
    write('provenance-api.inc',api);write('provenance-internal.inc',internal_text);write('provenance.inc',code)
    admission=helper.body(decoded[FILES[0]],'bool SDL_PushEvent(SDL_Event *event)')+'\n'+helper.body(decoded[FILES[1]],'void SDL_SendKeyboardText(const char *text)')
    write('admission.inc',admission)
    write('window-scope.inc',helper.body(decoded[FILES[3]],'LRESULT CALLBACK WIN_WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)'))
    write('session.inc','\n'.join(helper.body(decoded[FILES[2]],signature) for signature in ['bool WIN_StartTextInput(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)','bool WIN_StopTextInput(SDL_VideoDevice *_this, SDL_Window *window)','bool WIN_ClearComposition(SDL_VideoDevice *_this, SDL_Window *window)']))
    # Guard the unchanged full legacy IME body, not a reconstructed expectation.
    original=(source/FILES[2]).read_bytes().decode('utf-8').replace('\r\n','\n')
    ime_signature='bool WIN_HandleIMEMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM *lParam, SDL_VideoData *videodata)'
    # The source has disabled and enabled implementations; compare both regions.
    assert original.count(ime_signature)==decoded[FILES[2]].count(ime_signature)==2
    assert original[original.rindex(ime_signature):]==decoded[FILES[2]][decoded[FILES[2]].rindex(ime_signature):]
    assert decoded[FILES[3]].index('OQ4_WIN_ObserveTextMessage(data->window') < decoded[FILES[3]].index('if (WIN_HandleIMEMessage(hwnd')
    support=ROOT/'tools/tests/native/SdlTextProvenanceTest.cpp'
    command=[args.compiler,'-std=c++20','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-I',str(scratch),str(support),'-o',str(scratch/'test.exe')]
    compiled=run(command,'compile.log')
    if compiled.returncode: raise RuntimeError(compiled.stdout)
    tested=run([str(scratch/'test.exe')],'run.log')
    if tested.returncode: raise RuntimeError(tested.stdout)
    mutations={
        'result-clears-origin':('provenance.inc','if (lp & GCS_RESULTSTR) {','if (lp & GCS_RESULTSTR) { state->composition = 0;'),
        'late-character-adopts-current':('provenance.inc','&& !state->seen_ime && !state->tainted','&& !state->tainted'),
        'context-change-trusted':('provenance.inc','if (!state->composition || state->context != context)','if (!state->composition)'),
        'byte-budget-removed':('provenance.inc','length + 1 > OQ4_TEXT_BYTES - oq4_bytes','false'),
        'no-atomic-copy-bound':('provenance.inc','capacity <= slot->record.text_bytes','capacity == 0'),
        'no-context-release':('provenance.inc','SDL_free(text); SDL_free(wide); ImmReleaseContext(state->window->internal->hwnd, context);','SDL_free(text); SDL_free(wide);'),
        'no-public-failure-rollback':('admission.inc','OQ4_WIN_DropTextAssociation(event);','(void)event;'),
        'no-token-limit':('provenance.inc','oq4_token >= 0x7fffffff','false'),
        'public-push-inherits-native-scope':('provenance.inc','record.native_message = admission.message;','admission = oq4_scope; record.native_message = admission.message;'),
        'nested-keyboard-inherits-native-scope':('provenance.inc','if (!previous_push) oq4_keyboard_scope = oq4_scope;','oq4_keyboard_scope = oq4_scope;'),
        'native-admission-reused':('provenance.inc','oq4_keyboard_event = NULL;','(void)oq4_keyboard_event;'),
        'marker-integrity-unchecked':('provenance.inc','return oq4_healthy && oq4_marker_expected && event->type == oq4_marker_expected->type &&','return true || (oq4_healthy && oq4_marker_expected && event->type == oq4_marker_expected->type &&'),
        'marker-check-after-admission-only':('admission.inc','!OQ4_WIN_ValidateTextMarker(event)','false'),
        'disabled-destruction-leaks-registry':('provenance.inc','if (msg == WM_NCDESTROY && (state = OQ4_Window(window, false))) SDL_zero(*state);','(void)window;'),
    }
    mutation_results={}
    if not args.no_mutations:
        for name,(file,old,new) in mutations.items():
            path=scratch/file;before=path.read_text(encoding='utf-8')
            if old not in before: raise RuntimeError(f'missing mutation anchor {name}')
            mutated=before.replace(old,new)
            if name=='marker-integrity-unchecked':
                mutated=mutated.replace('event->user.data2 == oq4_marker_expected->user.data2;', 'event->user.data2 == oq4_marker_expected->user.data2);')
            path.write_text(mutated,encoding='utf-8')
            try:
                c=run(command,f'{name}-compile.log')
                if c.returncode: raise RuntimeError(f'mutation did not compile: {name}\n{c.stdout}')
                r=run([str(scratch/'test.exe')],f'{name}-run.log')
                if not r.returncode: raise RuntimeError(f'accepted invalid mutation: {name}')
                mutation_results[name]={'compiled':True,'rejected':True,'exit_code':r.returncode}
            finally: path.write_text(before,encoding='utf-8')
    result={'status':'passed','checks':int(re.search(r'PASS (\d+) checks',tested.stdout)[1]),'mutations':mutation_results,'provision':provision,
            'sources':{str(path.relative_to(ROOT)):sha(path) for path in [public,internal,implementation,patch,fence,queue,generation,support,Path(__file__)]},
            'native_projection':{name:sha(projection/name) for name in FILES},'command':command,
            'scope':'Production observer and SDL admission/scope/session methods against counted native/queue doubles; any fence calls use explicitly disabled-provider doubles. Full Windows message dispatch, enabled combined providers and OS IME/TSF/UI behavior unqualified.'}
    (scratch/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(tested.stdout.strip());print(scratch/'result.json');return 0
if __name__=='__main__': raise SystemExit(main())
