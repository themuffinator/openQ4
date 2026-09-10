#!/usr/bin/env python3
"""Actual manager/deferred/retained native-owner methods with counted host I/O.

The pure Interaction/native text models provide editor identity, transactions,
history and prepared publication. SDL, OS input, COM activation and Session
integration are not exercised. Test artifacts stay in this repository's .tmp.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,tempfile,re,configparser
import ui_manager_lifecycle as manager
from filesystem_case_segments import function_body
ROOT=Path(__file__).resolve().parents[2]

def manager_source(full=False):
    manager.ROOT=ROOT
    source='#include <thread>\n#include "src/ui/UserInterfaceNativeText.h"\n'+manager.production_source()
    header=(ROOT/'src/ui/UserInterfaceLocal.h').read_text()
    declarations=header[header.index('bool NativeTextAttach('):header.index('\n\tvoid\t',header.index('bool NativeTextEnter('))]
    declarations=declarations.replace('private:','public:')
    declarations+='\n'+re.search(r'const std::thread::id nativePresenceThread[^;]+;',header).group(0)+'\n'
    declarations+='\n bool textBoundaryActive=false,textBoundaryFailed=false;\n'
    if full:
        declarations+=header[header.index('\topenq4::ui::TextBrokerContext QueryTextContext('):header.index('\nbool NativeTextAttach(')]
    source=source.replace('class idUserInterfaceManagerLocal {\npublic:','class idUserInterfaceManagerLocal {\npublic:'+declarations)
    production=(ROOT/'src/ui/UserInterface.cpp').read_text()
    source+=function_body(production,'std::uint64_t UI_NextTextLifetime(')
    source+=production[production.index('// Private native collection owner boundary.'):production.index('bool idUserInterfaceManagerLocal::DispatchApplicationActions(')]
    if full:
        source+=production[production.index('openq4::ui::TextBrokerContext UI_QueryTextContext('):production.index('// Private native collection owner boundary.')]
        source=source.replace('private:','public:') # Test observability only, no method edits.
        retained=(ROOT/'src/ui/UserInterfaceRetained.cpp').read_text()
        rt=(ROOT/'src/ui/retained/Runtime.cpp').read_text()
        rtheader=(ROOT/'src/ui/retained/Runtime.h').read_text()
        declarations=[];bodies=[]
        for name in ['AttachNumberNative','RefreshNumberNative','BeginNumberNativeCollection','IsNumberNativeCurrent','ApplyNumberNative','CompleteNumberNativeCollection','SettleNumberNative','PrepareNumberNativeSettlement','PublishNumberNativeSettlement','RetireNumberNative','RetireNumberNativeExact','QueryNumberNativePresence']:
            start=rt.rfind('\n',0,rt.index('Runtime::'+name+'('))+1
            body=function_body(rt,rt[start:rt.index('(',start)+1])
            declarations.append(body[:body.index('{')].replace('Runtime::','').strip()+';')
            bodies.append(body)
        runtime=RUNTIME_SUPPORT.replace('/*DECLARATIONS*/','\n'.join(declarations))
        runtime=runtime.replace('/*CONTEXT*/',function_body(rtheader,'struct NumberEditorContext {')+';')
        runtime+='\nnamespace openq4::ui {\n'+'\n'.join(bodies)+'\n}\n'
        native=(ROOT/'src/ui/UserInterfaceRetained.h').read_text()
        native=native[native.index('\tbool PrepareNativeText('):native.index('\nprivate:')]
        impl=IMPL_SUPPORT.replace('/*MATCH*/',function_body(retained,'bool NativeOwnerMatches(')).replace('/*ACCEPT*/',function_body(retained,'bool AcceptInput()'))
        source=source.replace('class idUserInterfaceRetained : public idUserInterfaceLocal {',runtime+'\nclass idUserInterfaceRetained : public idUserInterfaceLocal {')
        anchor='    std::vector<std::string> pendingActions;'
        source=source.replace(anchor,impl+'\n'+native+'\n'+anchor)
        source+=retained[retained.index('bool idUserInterfaceRetained::PrepareNativeText('):retained.index('bool UI_RetainedDiagnostic(')]
    return source

RUNTIME_SUPPORT=r'''
using namespace openq4::ui;
static bool windowFocused=true;
bool Sys_SDL_IsGameWindowFocused(){return windowFocused;}
struct Console {bool open=false;bool Active(){return open;}} consoleObject;
Console* console=&consoleObject;
double RetainedUI_PresentationTime(){return 1;}
namespace openq4::ui {
/*CONTEXT*/
// Counted Host/layout seam; all following Runtime native wrapper bodies are
// production extraction. This cannot establish rendering or callback safety of
// a Host which destroys its own currently executing Runtime.
class Runtime {
public:
 struct Impl {
    DocumentModel model;Interaction interaction;bool canonical=true,document=true,available=true;
    int prepares=0;std::function<void()> callback;
    Impl(){
        model.id="numbers";model.root.id="root";Node n;n.id="number-with-a-long-control-id";
        Control c;c.role=ControlRole::Number;c.action="edit";Expression e;e.type=0;c.value=e;
        NumberSpec spec;spec.minimum=.5;spec.maximum=2;spec.maxBytes=2048;c.widget=spec;
        for(auto state:{ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})c.states[state]="feedback";
        n.control=c;model.root.children.push_back(n);interaction.Reset(model);std::string error;
        assert(interaction.SetReadbacks({{n.id,{1.0,false,{}}}},error));interaction.SetBounds({{n.id,{0,0,100,40,true}}});
        assert(interaction.Focus(n.id)&&interaction.BeginNumberEdit(n.id,error));
    }
    bool PrepareNumberEdit(double,std::string&){++prepares;auto call=callback;if(call)call();return available;}
 };
 std::shared_ptr<Impl> impl=std::make_shared<Impl>();
 std::optional<NumberEditorContext> QueryNumberEditor(std::string& error,double seconds){
    if(!impl->PrepareNumberEdit(seconds,error))return {};
    const auto widget=impl->interaction.Widget("number-with-a-long-control-id");
    if(!widget||!widget->number||widget->number->nativePresentation||widget->number->nativeUnsettled)return {};
    return NumberEditorContext{"number-with-a-long-control-id",*widget->number,impl->interaction.ModalToken()};
 }
 /*DECLARATIONS*/
};
}
'''
IMPL_SUPPORT=r'''
    struct Impl {
        std::uint64_t textBackend=UI_NextTextLifetime(),textDocument=UI_NextTextLifetime();
        bool initialized=true,active=true,interactive=true,suspended=false,unavailable=false,close=false;
        int prepares=0,quarantines=0;std::function<void()> callback;
        std::shared_ptr<Runtime> runtime=std::make_shared<Runtime>();
        Runtime* RuntimeView() const noexcept {return runtime.get();}
        bool Prepare(){++prepares;auto call=callback;const bool result=!unavailable;if(call)call();return result;}
        void Quarantine(bool){++quarantines;}
        /*MATCH*/
        /*ACCEPT*/
    };
    std::unique_ptr<Impl> impl=std::make_unique<Impl>();
    std::function<void()> onTextQuery;
    TextBrokerContext QueryTextContext(std::uint64_t allocation,std::uint64_t window,std::uint64_t session) override {
        auto call=onTextQuery;if(call)call();
        return {TextBrokerRoute::Retained,window,session,TextEditorIdentity{allocation,1,2,3,window,4,5,"counted-text"}};
    }
'''

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++');parser.add_argument('--sdl-source',type=Path)
    parser.add_argument('--msvc',action='store_true');parser.add_argument('--debug-crt',action='store_true')
    parser.add_argument('--sanitize',action='store_true');parser.add_argument('--no-mutations',action='store_true')
    args=parser.parse_args()
    out=Path(tempfile.mkdtemp(prefix='managed-native-',dir=ROOT/'.tmp'))
    env={**os.environ,'TEMP':str(out),'TMP':str(out),'TMPDIR':str(out)}
    default=manager_source()+r'''
static uiNativeTextRoute_t Probe(void* value) noexcept {return *static_cast<uiNativeTextRoute_t*>(value);}
int main() {
    auto* legacy=new idUserInterfaceLocal;
    uiNativeTextRoute_t route{legacy,5,true};
    openq4::ui::NativeTextEditorBarrier expected;expected.editor={1,2,3,4,5,6,7,"number"};expected.native={8,9};
    std::string error;openq4::ui::NativeTextEditorView out;out.presentation.text="untouched";
    assert(!UI_NativeTextRefresh(Probe,&route,expected,out,error));assert(out.presentation.text=="untouched");
    assert(!UI_NativeTextCurrent(Probe,&route,expected));
    assert(!UI_NativeTextRetireExact(expected.native,expected.editor));
    assert(UI_NativeTextPresence(expected.native,expected.editor)==openq4::ui::NativeTextPresence::BusyOrUnknown);delete legacy;
#ifdef ID_DEDICATED
    assert(UI_NativeTextPresence(expected.native,expected.editor)==openq4::ui::NativeTextPresence::BusyOrUnknown);
#else
    assert(UI_NativeTextPresence(expected.native,expected.editor)==openq4::ui::NativeTextPresence::AbsentOriginal);
#endif
    std::puts("Managed native legacy/default gates passed.");
}
'''
    source=manager_source(True)+'\n'+(ROOT/'tools/tests/native/UiManagedNativeOwnerTest.cpp').read_text()
    core=ROOT/'src/ui/retained'
    paths=[ROOT/n for n in ['src/ui/UserInterfaceNativeText.h','src/ui/UserInterface.h','src/ui/UserInterfaceLocal.h','src/ui/UserInterface.cpp','src/ui/UserInterfaceManaged.h','src/ui/UserInterfaceDeferred.h','src/ui/UserInterfaceDeferred.cpp','src/ui/UserInterfaceRetained.h','src/ui/UserInterfaceRetained.cpp','src/ui/UserInterfaceText.h','src/ui/UserInterfaceClipboard.h','src/ui/application/ManagedNativeTextOwner.h','src/ui/application/ManagedNativeTextOwner.cpp','src/ui/application/NativeTextCollectionCoordinator.h','src/sys/sdl3/NativeQueueBatch.h','tools/tests/ui_manager_lifecycle.py','tools/tests/ui_managed_native_owner.py','tools/tests/filesystem_case_segments.py','tools/tests/native/UiManagedNativeOwnerTest.cpp','tools/tests/sdl3_clipboard_status.py','subprojects/sdl3.wrap','subprojects/packagefiles/sdl3/include/SDL3/SDL_openq4_native_fence.h']]
    paths += [core/n for n in ['Runtime.h','Runtime.cpp','Interaction.h','Interaction.cpp','Document.h','Document.cpp','Vector.h','TextInput.h','TextInput.cpp','TextEdit.h','TextEdit.cpp','TextEditCommand.h','NativeTextDocument.h','NativeTextDocument.cpp','NativeTextEditor.h','NativeTextEditor.cpp','TextInputBroker.h','Input.h','Input.cpp']]
    import sdl3_clipboard_status as helper
    helper.ROOT=ROOT
    helper.FILES=['include/SDL3/SDL_'+n+'.h' for n in ['atomic','audio','begin_code','blendmode','camera','close_code','endian','error','events','gamepad','guid','init','iostream','joystick','keyboard','keycode','mouse','mutex','pen','pixels','platform_defines','power','properties','rect','scancode','sensor','stdinc','surface','thread','touch','video']]
    wrap=configparser.ConfigParser();wrap.read(ROOT/'subprojects/sdl3.wrap',encoding='utf-8')
    assert wrap['wrap-file']['directory']=='SDL3-3.4.10','Review SDL include closure for a new version'
    sdl,provision,_=helper.provision_source(args.sdl_source,dict(wrap['wrap-file']),out)
    sdl_paths=[sdl/n for n in helper.FILES]+[sdl/'LICENSE.txt']
    sdl_before={str(p.relative_to(sdl)):sha(p) for p in sdl_paths}
    before={str(p.relative_to(ROOT)):sha(p) for p in paths}
    valid=out/'valid.cpp';doc=(core/'Document.cpp').read_text()
    valid.write_text('#include "Interaction.h"\n#include <cmath>\nnamespace openq4::ui {\n'+function_body(doc,'bool Utf8(')+function_body(doc,'bool ValidStateValue(')+'}\n',newline='\n')
    includes=[ROOT,core,ROOT/'subprojects/packagefiles/sdl3/include',sdl/'include']
    if args.msvc:flags=[args.compiler,'/nologo','/std:c++20','/EHsc','/W3','/MTd' if args.debug_crt else '/MT']+['/I'+str(p) for p in includes]
    else:flags=[args.compiler,'-std=c++20','-I',str(ROOT),'-I',str(core),'-I',str(includes[2]),'-I',str(includes[3])]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-g0','-no-pie']
    units=[valid]+[core/(n+'.cpp') for n in ['Interaction','TextInput','TextEdit','NativeTextDocument','NativeTextEditor','Input']]+[ROOT/'src/ui/application/ManagedNativeTextOwner.cpp']
    report={'passed':False,'scope':__doc__,'sources':before,'sdl_source':provision,'sdl_headers':sdl_before,'cases':[],'objects':[],'validation_sha256':sha(valid)}
    def run(command,name,timeout=180):
        r=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=timeout)
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr,encoding='utf-8');return r,log
    def case(name,body,objects,dedicated=False,mutant=False):
        cpp=out/(name+'.cpp');cpp.write_text(body,encoding='utf-8',newline='\n');binary=out/(name+'.exe')
        macro='/D' if args.msvc else '-D'
        command=flags+[macro+'ID_DEDICATED' if dedicated else macro+'USE_SDL3',str(cpp)]+[str(p) for p in objects]
        command+=['/Fe:'+str(binary)] if args.msvc else ['-o',str(binary)]
        r,log=run(command,name+'-compile');entry={'name':name,'command':command,'source_sha256':sha(cpp),'compile_exit':r.returncode,'compile_log_sha256':sha(log)};report['cases'].append(entry)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        r,log=run([str(binary)],name+'-run',60);entry.update(exit=r.returncode,run_log_sha256=sha(log),binary_sha256=sha(binary))
        if mutant:
            if not r.returncode or not any(s in r.stdout+r.stderr for s in ['FAIL','Assertion','Assertion failed','AddressSanitizer','runtime error:']):raise RuntimeError('Mutation survived/did not reach checked failure: '+name+'\n'+r.stdout+r.stderr)
            entry['rejected']=True;print('Rejected '+name,flush=True)
        elif r.returncode:raise RuntimeError(r.stdout+r.stderr)
        else:entry['output']=r.stdout.strip();print(r.stdout.strip(),flush=True)
    mutations={
        'ignore-allocation':('allocations[i]==route.current && allocations[i]->allocationId==owner.allocation','allocations[i]==route.current'),
        'ignore-current-route':('allocations[i]==route.current && allocations[i]->allocationId==owner.allocation','allocations[i]->allocationId==owner.allocation'),
        'ignore-input-policy':('!route.current || !route.inputAllowed || route.window!=owner.window','!route.current || route.window!=owner.window'),
        'ignore-window':('!route.current || !route.inputAllowed || route.window!=owner.window','!route.current || !route.inputAllowed'),
        'ignore-backend-generation':('owner.backend == textBackend && owner.document == textDocument','owner.document == textDocument'),
        'ignore-document-generation':('owner.backend == textBackend && owner.document == textDocument','owner.backend == textBackend'),
        'ignore-suspension':('!suspended && !unavailable && !close','!unavailable && !close'),
        'ignore-closed-owner':('!suspended && !unavailable && !close','!suspended && !unavailable'),
        'ignore-refresh-post-observation':('!accepted || nativeBoundaryFailed || !NativeTextCheck(probe,context,candidate.barrier)','!accepted || nativeBoundaryFailed'),
        'dont-poison-native-reentry':('if(nativeBoundaryActive || textBoundaryActive || clipboardBoundaryActive) {\n        nativeBoundaryFailed=true;','if(nativeBoundaryActive || textBoundaryActive || clipboardBoundaryActive) {\n        nativeBoundaryFailed=false;'),
        'native-text-reentry-allowed':('if (textBoundaryActive || nativeBoundaryActive)', 'if (textBoundaryActive)'),
        'native-clipboard-reentry-allowed':('if (clipboardBoundaryActive || nativeBoundaryActive)', 'if (clipboardBoundaryActive)'),
        'dont-poison-outer-text':('if(textBoundaryActive)textBoundaryFailed=true;','if(textBoundaryActive)textBoundaryFailed=false;'),
        'dont-poison-outer-clipboard':('if(clipboardBoundaryActive)clipboardBoundaryFailed=true;','if(clipboardBoundaryActive)clipboardBoundaryFailed=false;'),
        'attach-unfrozen-owner':('const auto frozen=owner;const auto nativeCopy=native;','const auto& frozen=owner;const auto nativeCopy=native;'),
        'allocate-at-publication':('const auto& expected=prepared.Receipt().before;','const auto expected=prepared.Receipt().before;'),
        'prepare-inside-current':('return owner && owner->CurrentNativeText(expected);','return owner && owner->PrepareNativeText(expected.editor) && owner->CurrentNativeText(expected);'),
        'omit-current-native-barrier':('return runtime && runtime->IsNumberNativeCurrent(expected);','return runtime != nullptr;'),
        'presence-global-thread-identity':('const std::thread::id nativePresenceThread = std::this_thread::get_id();','inline static const std::thread::id nativePresenceThread = std::this_thread::get_id();'),
        'presence-on-wrong-thread':('std::this_thread::get_id()!=nativePresenceThread || nativeBoundaryActive','nativeBoundaryActive'),
        'presence-in-native-callback':('nativePresenceThread || nativeBoundaryActive || textBoundaryActive','nativePresenceThread || textBoundaryActive'),
        'presence-in-text-callback':('nativeBoundaryActive || textBoundaryActive ||\n        clipboardBoundaryActive','nativeBoundaryActive ||\n        clipboardBoundaryActive'),
        'presence-in-clipboard-callback':('clipboardBoundaryActive || applicationPumpDepth || !native.document','applicationPumpDepth || !native.document'),
        'presence-in-application-pump':('clipboardBoundaryActive || applicationPumpDepth || !native.document','clipboardBoundaryActive || !native.document'),
        'presence-unissued-allocation':('owner.allocation>nextAllocationId || !owner.backend','!owner.backend'),
        'presence-missing-runtime-absent':('runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown','runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::AbsentOriginal'),
        'presence-generation-means-absence':('return runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;','if(!impl->NativeOwnerMatches(owner,false))return NativeTextPresence::AbsentOriginal; return runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;'),
        'presence-prepares-host':('return runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;','impl->Prepare(); return runtime?runtime->QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;'),
        'presence-runtime-document-gate':('return impl?impl->interaction.QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;','return impl && impl->document?impl->interaction.QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;'),
        'presence-missing-allocation-present':('return Presence::AbsentOriginal;','return Presence::PresentExact;'),
        'deferred-drops-retirement':('return backend != NULL ? backend->RetireNativeTextExact(native,owner) : false;','return false;'),
    }
    try:
        objects=[]
        for unit in units:
            obj=out/(unit.stem+('.obj' if args.msvc else '.o'))
            command=flags+['/c',str(unit),'/Fo:'+str(obj)] if args.msvc else flags+['-c',str(unit),'-o',str(obj)]
            r,log=run(command,unit.stem+'-compile')
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
            objects.append(obj);report['objects'].append({'name':obj.name,'sha256':sha(obj),'source':str(unit),'source_sha256':sha(unit),'command':command,'compile_log_sha256':sha(log)})
        case('client-default',default,objects[:-1]);case('dedicated',default,[],True);case('managed-owner',source,objects)
        if not args.no_mutations:
            for name,(old,new) in mutations.items():
                expected=2 if name in ['native-text-reentry-allowed','dont-poison-outer-text','dont-poison-outer-clipboard'] else 1
                if source.count(old)!=expected:raise RuntimeError('Mutation anchor count '+str(source.count(old))+': '+name)
                case(name,source.replace(old,new),objects,mutant=True)
            interaction=(core/'Interaction.cpp').read_text()
            original=function_body(interaction,'NativeTextPresence Interaction::QueryNumberNativePresence(')
            native_mutations={
                'presence-candidate-absent':('candidate || !authority ||','!authority ||'),
                'presence-copies-barrier':('const auto& before=view.barrier;','const auto before=view.barrier;'),
                'presence-revision-equality':('current.session==owner.session && current.control==owner.control','current.session==owner.session && current.revision==owner.revision && current.control==owner.control'),
                'presence-forgets-native-document':('before.native==native &&','before.native.editorLease==native.editorLease &&'),
                'presence-forgets-native-lease':('before.native==native &&','before.native.document==native.document &&'),
                'presence-forgets-allocation':('current.allocation==owner.allocation &&',''),
                'presence-forgets-backend':('current.backend==owner.backend &&',''),
                'presence-forgets-document':('current.document==owner.document &&',''),
                'presence-forgets-modal':('current.modal==owner.modal &&',''),
                'presence-forgets-window':('current.window==owner.window &&',''),
                'presence-forgets-session':('current.session==owner.session &&',''),
                'presence-forgets-control':(' && current.control==owner.control;',';'),
            }
            for name,(old,new) in native_mutations.items():
                if original.count(old)!=1:raise RuntimeError('Native presence mutation anchor: '+name)
                path=out/(name+'-Interaction.cpp');path.write_text(interaction.replace(original,original.replace(old,new)),newline='\n')
                obj=out/(name+('.obj' if args.msvc else '.o'))
                command=flags+['/c',str(path),'/Fo:'+str(obj)] if args.msvc else flags+['-c',str(path),'-o',str(obj)]
                r,log=run(command,name+'-model-compile')
                if r.returncode:raise RuntimeError(r.stdout+r.stderr)
                report['objects'].append({'name':obj.name,'sha256':sha(obj),'source':str(path),'source_sha256':sha(path),'command':command,'compile_log_sha256':sha(log)})
                mutant_objects=[obj if value.stem=='Interaction' else value for value in objects]
                case(name,source,mutant_objects,mutant=True)
        report['passed']=True
    except Exception as error:report['failure']=str(error);print(error,flush=True)
    report['sources_unchanged']=before=={str(p.relative_to(ROOT)):sha(p) for p in paths}
    report['sdl_headers_unchanged']=sdl_before=={str(p.relative_to(sdl)):sha(p) for p in sdl_paths}
    report['passed'] &= report['sources_unchanged'] and report['sdl_headers_unchanged']
    path=out/'result.json';path.write_text(json.dumps(report,indent=2)+'\n');print(path,flush=True)
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
