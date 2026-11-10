// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual SYSTEM/Runtime/RmlUi and shared profile/transaction source. Counted host;
// not production adapter dispatch, CVar writes, real font shaping or native input.
#include "src/ui/retained/Runtime.h"
#include "src/ui/application/SettingsTransaction.h"
#include "src/framework/PerformancePreset.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <json/json.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
using namespace openq4;using namespace openq4::ui;
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static std::string Read(const std::string& p){std::ifstream f(p,std::ios::binary);Check(bool(f),"bound source readable");return {std::istreambuf_iterator<char>(f),{}};}
struct TestHost final:Host,SettingsHost {
 StateValues live;std::map<std::string,std::string> strings;unsigned writes=0,observations=0,errors=0,draws=0;std::uint64_t frame=0;float expansion=1;bool writable=false;
 bool ReadFile(const std::string&,std::string&)override{return false;}bool softFocus=false;std::string language="english";
 bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{if(name=="sys_lang"&&type==2){value=language;return true;}if(name!="ui_retainedSoftFocus"||type!=1)return false;value=softFocus;return true;}
 std::string Translate(const std::string& id)override{return strings.contains(id)?strings.at(id):id.starts_with("#str_")?"Localized label":id;}
 void Log(bool bad,const std::string& s)override{if(bad){++errors;std::fprintf(stderr,"Runtime: %s\n",s.c_str());}}
 std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=256;return 1;}
 void Draw(const std::vector<Vertex>& v,const std::vector<int>& i,std::uintptr_t)override{++draws;bool ok=i.size()%3==0;for(auto n:i)ok=ok&&n>=0&&size_t(n)<v.size();for(auto p:v)ok=ok&&std::isfinite(p.x)&&std::isfinite(p.y);Check(ok,"finite indexed actual draw");}
 std::uint64_t RenderFrame()const override{return frame;}bool BeginLayer(std::uint32_t,int,int)override{return true;}void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}void EndLayer(std::uint32_t)override{}
 FontMetrics GetFontMetrics(const std::string&,int s)override{return {s*.78f,s*.22f,s*1.4f,s*.5f};}Glyph GetGlyph(const std::string&,int s,std::uint32_t cp)override{float a=s*(cp==' '?.28f:.51f)*expansion+.125f;return {a,0,-s*.78f,a*.9f,float(s),0,0,1,1,"test-font"};}
 bool Read(StateValues& v,std::string&)override{v=live;return true;}bool Defaults(StateValues& v,std::string&)override{v=live;return true;}
 bool Validate(const StateValues& b,const StateValues& c,std::string&)override{if(b.size()!=c.size())return false;for(auto& [k,v]:c)if(!b.contains(k)||b.at(k).index()!=v.index()||!ValidStateValue(v))return false;return true;}
 bool Write(const StateValues& patch,std::string&)override{++writes;if(!writable)return false;for(const auto& [key,value]:patch)live.at(key)=value;return true;}bool NeedsConfirmation(const StateValues&,const StateValues&)const override{return !writable;}
 static bool Quoted(std::istream& s,std::string& out){s>>std::ws;if(s.get()!='"')return false;out.clear();for(int c=s.get();c!=EOF;c=s.get()){if(c=='"')return true;if(c=='\\'){const int n=s.get();if(n==EOF)return false;out+=n=='n'?'\n':n=='t'?'\t':char(n);continue;}out+=char(c);}return false;}
 void Locale(const std::string& path){std::istringstream f(::Read(path));std::string line;while(std::getline(f,line)){std::istringstream s(line);std::string k,v;if(Quoted(s,k)&&Quoted(s,v)&&k.starts_with("#str_"))strings[k]=v;}for(auto* k:{"#str_229976","#str_229977","#str_229978","#str_230010"})Check(strings.contains(k),"all actual locale preset/status strings exist");}
};
struct View {
 TestHost host;SettingsTransaction tx{host};Runtime runtime{host};Viewport viewport;std::string source,error,message="#str_230007";double time=1;bool pending=false,canApply=false;static constexpr std::uint64_t Owner=29;
 View(const std::string& s,const std::string& locale,float density=1,int width=1280,int height=720,float expansion=1,const std::string& guiLocale={}):source(s){
  host.Locale(locale);if(!guiLocale.empty())host.Locale(guiLocale);
  // The prompt bar's key caps name their keys from the stock code strings.
  if(const auto at=locale.rfind("_openq4.lang");at!=std::string::npos){const auto code=locale.substr(0,at)+"_code.lang";if(std::ifstream(code))host.Locale(code);}host.expansion=expansion;Json::Value doc;Json::CharReaderBuilder b;auto reader=std::unique_ptr<Json::CharReader>(b.newCharReader());Check(reader->parse(s.data()+s.find('{'),s.data()+s.size(),&doc,&error),error.c_str());
  for(auto& k:doc["state"].getMemberNames())if(k.starts_with("settings.draft.")){auto d=doc["state"][k];StateValue v;auto type=d["type"].asString();if(type=="number")v=d["initial"].asDouble();else if(type=="boolean")v=d["initial"].asBool();else v=d["initial"].asString();host.live.emplace(k.substr(15),v);}
  Check(host.live.size()==76,"full authored 76-key typed catalog");host.live["com_performancePreset"]=std::string("balanced");host.live["r_brightness"]=1.375;host.live["r_forceAmbient"]=.1375;host.live["r_renderer"]=std::string("best");Check(tx.Begin(Owner).code==SettingsCode::Ok,"begin real transaction");
  std::vector<Diagnostic> ds;bool loaded=runtime.LoadDocument(s,"guis/menu/settings/system.q4ui",ds);for(auto& d:ds)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"actual SYSTEM compiles and loads");viewport.width=width;viewport.height=height;viewport.displayScale=density;
  // As the engine adapter lays the page out (UserInterfaceRetained): its 720 dp
  // canvas spans the view's height, at least 960 dp wide, whatever the density.
  Check(doc["canvas"]["height"].asDouble()==720&&doc["canvas"]["minWidth"].asDouble()==960,"the page lays out on the 4:3 canvas");
  viewport.canvasHeight=doc["canvas"]["height"].asFloat();viewport.canvasMinWidth=doc["canvas"]["minWidth"].asFloat();runtime.SetReducedMotion(true,time);Sync();Frame();
 }
 void Sync(){StateValues v{{"settings.render.autoDetect",true},{"settings.render.gl",true},{"settings.render.vulkan",true},{"settings.render.canChoose",true},{"settings.render.backend",std::string("best")},{"settings.render.active",std::string("Vulkan — Test GPU")},{"settings.render.notice",std::string("#str_230145")},{"settings.msaaAvailable",true},{"settings.renderer.available",true},{"settings.open",true},{"settings.phase",1.0},{"settings.busy",false},{"settings.confirmationVisible",false},{"settings.dirty",tx.Dirty()},{"settings.canApply",canApply},{"settings.message",message},{"ui.numberDraftsPending",pending},{"ui.numberDraftMessage",std::string("#str_230006")}};for(auto& [k,x]:tx.Draft())v["settings.draft."+k]=x;for(auto& [k,x]:tx.Baseline())v["settings.baseline."+k]=x;Check(runtime.SetState(v,error,time),error.c_str());}
 void Frame(){time+=.02;++host.frame;runtime.Frame(viewport,time);Check(!host.errors,"no runtime diagnostics");}void Key(MenuInput k){runtime.MenuAction(k,true,time);runtime.MenuAction(k,false,time);}
 WidgetViewState Widget(){auto w=runtime.GetWidgetState("settings_preset");Check(w.has_value(),"preset widget exists");return *w;}std::string Alias(const char* id){std::string s;Check(runtime.GetPresentationAlias(id,s),id);return s;}
 Rml::Element* Element(const char* id){auto* c=Rml::GetContext(0);Check(c!=nullptr,"actual Rml context");auto* e=c->GetDocument(0)->GetElementById(id);Check(e!=nullptr,id);return e;}Bounds Box(const char* id){Bounds b;Check(runtime.GetBounds(id,b),id);return b;}
 // The page's tabs: a control acts only while its page (settings-page-<tab>)
 // is current, so a check first switches to it as its tab_<tab> event does.
 double Tab(){return std::get<double>(runtime.GetState().at("page.tab"));}
 std::string TabOf(const char* id){for(auto* e=Element(id);e;e=e->GetParentNode())if(e->GetId().starts_with("settings-page-"))return e->GetId().substr(14);Check(false,"the control lies on one of the page's tabs");return {};}
 // Reduced motion still cross-fades the pages, over 80 ms: Settle outlasts it.
 void Settle(){for(int i=0;i<6;++i)Frame();}
 void Show(const char* id){Event(("tab_"+TabOf(id)).c_str());Settle();Check(runtime.GetState().at("page.tab")==StateValue(double(TabIndex(TabOf(id)))),"the tab event makes the control's tab current");}
 static int TabIndex(const std::string& tab){return tab=="graphics"?0:tab=="screen"?1:tab=="image"?2:-1;}
 bool Focus(const char* id){Show(id);return runtime.FocusControl(id,time);}
 // Two displays, as the settings service publishes them, show the device and
 // span rows; the size and rate rows read their own lists.
 void Displays(){Check(runtime.SetState({{"settings.display.available",true},{"settings.display.spanAvailable",true},{"settings.display.count",2.0},
  {"settings.display.optionCount",3.0},{"settings.display.0.label",std::string("1: Primary monitor")},{"settings.display.1.label",std::string("2: Secondary monitor")},
  {"settings.display.mode.optionCount",4.},{"settings.display.mode.selected",0.},{"settings.display.mode.0.label",std::string("Desktop Native (1920 \xC3\x97 1080)")},{"settings.display.mode.1.label",std::string("1280 \xC3\x97 720 (16:9)")},{"settings.display.mode.2.label",std::string("2560 \xC3\x97 1080 (21:9)")},{"settings.display.mode.3.label",std::string("Custom (1600 \xC3\x97 900)")},
  {"settings.display.refresh.optionCount",3.},{"settings.display.refresh.selected",0.},{"settings.display.refresh.0.label",std::string("Auto")},{"settings.display.refresh.1.label",std::string("60 Hz")},{"settings.display.refresh.2.label",std::string("144 Hz")}},error,time),error.c_str());Frame();}
 void Open(){Check(Focus("settings_preset"),"preset can focus");Key(MenuInput::Accept);Frame();Check(Widget().popupOpen,"choice opens");Check(runtime.TakeActions().empty(),"open emits no action");}
 ControlAction Select(unsigned i){Open();Key(MenuInput::Home);for(unsigned n=0;n<i;++n)Key(MenuInput::Down);Frame();Check(Widget().highlight=="settings_preset-option-"+std::to_string(i),"exact highlight");Check(runtime.TakeActions().empty(),"highlight cannot mutate settings");Key(MenuInput::Accept);auto a=runtime.TakeActions();Check(a.size()==1&&a[0].proposal&&a[0].proposalToken,"one immutable typed proposal");return a.front();}
 StateValues Patch(const PerformancePreset& p){PerformancePresetAssignments a;Check(ExpandPerformancePreset(p,a,error),"shared production expansion");StateValues out;for(auto& x:a){StateValue v;if(auto s=std::get_if<std::string>(&x.value))v=*s;else if(std::holds_alternative<bool>(host.live.at(x.key)))v=std::get<int>(x.value)!=0;else v=double(std::get<int>(x.value));out.emplace(x.key,v);}Check(out.size()==55,"exact 55-field footprint");return out;}
 SettingsResult Dispatch(const ActionInvocation& a,bool observe=true){NumberDraftSummary d;Check(runtime.QueryNumberDrafts(d,error,time),"actual Number barrier queried");if(!d.blocking.empty())return {SettingsCode::Busy,"local draft"};return tx.EditGenerated(Owner,[&](StateValues& out,std::string& e){PerformancePreset resolved;const PerformancePreset* p=nullptr;if(a.operation=="settings.system.preset"){Check(a.arguments.size()==1&&std::holds_alternative<std::string>(a.arguments.at("name")),"exact preset operation schema");p=FindPerformancePreset(std::get<std::string>(a.arguments.at("name")));}else{Check(a.operation=="settings.system.autodetect"&&a.arguments.empty(),"exact Auto schema");++host.observations;if(!observe){e="observation failed";return false;}PerformancePresetSignals signals;signals.steamDeck=true;if(!ResolveDetectedPerformancePreset(DetectPerformancePreset(signals),resolved))return false;p=&resolved;}if(!p)return false;out=Patch(*p);return true;});}
 void Accept(const ControlAction& p){Check(runtime.CanDispatchControlAction(p,time),"proposal still eligible");ActionInvocation a;Check(runtime.ResolveAction(p.action,a,error,&*p.proposal),"actual typed argument resolved");auto prior=Widget().accepted;Check(Dispatch(a).code==SettingsCode::Ok,"transaction generated edit succeeds");Check(SettingsValueEqual(prior,Widget().accepted),"operation alone cannot change authoritative widget");Sync();Check(runtime.AcknowledgeControlProposal(p.node,p.proposalToken,true),"ACK after real transaction readback");Frame();Check(SettingsValueEqual(Widget().accepted,*p.proposal),"choice paints accepted typed readback");}
 Runtime::EventEffects Event(const char* name){Runtime::EventEffects e;Check(runtime.RunEvent(name,time,e,error),error.c_str());return e;}
};
// The page's three tabs and each one's keyboard order: document order, down
// the first column and on down the second. Restart Renderer joins GRAPHICS
// only while a renderer change waits for it (Tabs checks it there).
struct PageTab { const char* id; std::vector<const char*> order; };
static const PageTab PageTabs[]={
 {"graphics",{"settings_preset","settings_autodetect","settings_backend","settings_gpu","settings_renderer","settings_ui_scale","settings_text_scale","settings_ui_aspect","settings_reset_sizes"}},
 {"screen",{"settings_display_device","settings_multiscreen","settings_fullscreen","settings_borderless","settings_fullscreen_policy","settings_msaa","settings_vsync",
  "settings_window_width","settings_window_height","settings_display_mode","settings_custom_width","settings_custom_height","settings_display_refresh"}},
 {"image",{"settings_brightness","settings_brightness_number","settings_shadows","settings_specular","settings_bump","settings_sky","settings_bloom","settings_ssao",
  "settings_tonemap","settings_postaa","settings_resolution_scale","settings_ambient_enabled","settings_ambient","settings_ambient_number","settings_crt","settings_irradiance",
  "settings_lightgrid_preload"}}};
static std::string PageDisplay(View& v,const PageTab& tab){auto shown=v.runtime.PresentedValue(("settings-page-"+std::string(tab.id)).c_str(),"display");Check(shown.has_value(),tab.id);return shown->text;}
// The tabs: each tab_<id> event makes its tab current at once and focuses its
// first row; Next and Down keep within the tab; a hidden tab's rows cannot act;
// onTabNext and onTabPrevious wrap around the strip; the pages cross-fade.
static void Tabs(View& v){
 Check(v.Tab()==0&&PageDisplay(v,PageTabs[0])=="block"&&PageDisplay(v,PageTabs[1])=="none"&&PageDisplay(v,PageTabs[2])=="none","the page opens on GRAPHICS alone");
 for(const auto* tab:{"tab-graphics","tab-screen","tab-image"})Check(v.runtime.CanActivateControl(tab,v.time),"every tab of the strip answers");
 v.Displays();
 for(const unsigned index:{1u,2u,0u}){const auto& tab=PageTabs[index];
  // GRAPHICS, the current tab, is reached by wrapping from IMAGE.
  if(index)v.Event(("tab_"+std::string(tab.id)).c_str());else v.Event("onTabNext");v.Settle();
  Check(v.Tab()==index,"the tab event makes its tab current");
  for(const auto& other:PageTabs)if(PageDisplay(v,other)!=(&other==&tab?"block":"none"))std::fprintf(stderr,"tab %s: page %s display=%s leaving=%g\n",tab.id,other.id,PageDisplay(v,other).c_str(),std::get<double>(v.runtime.GetState().at("page.leaving")));
  for(const auto& other:PageTabs)Check(PageDisplay(v,other)==(&other==&tab?"block":"none"),"only the current tab's page shows");
  if(v.runtime.FocusedControl()!=tab.order.front())std::fprintf(stderr,"tab %s focused %s\n",tab.id,v.runtime.FocusedControl().c_str());
  Check(v.runtime.FocusedControl()==tab.order.front(),"the tab event focuses the tab's first row");
  for(size_t i=1;i<tab.order.size();++i){v.Key(MenuInput::Next);v.Frame();
   if(v.runtime.FocusedControl()!=tab.order[i])std::fprintf(stderr,"tab %s Next after %s reached %s, expected %s\n",tab.id,tab.order[i-1],v.runtime.FocusedControl().c_str(),tab.order[i]);
   Check(v.runtime.FocusedControl()==tab.order[i],"Next keeps the tab's document order");}
  v.Key(MenuInput::Next);v.Frame();
  if(v.runtime.FocusedControl()!="settings_back")std::fprintf(stderr,"tab %s Next after its last row reached %s\n",tab.id,v.runtime.FocusedControl().c_str());
  Check(v.runtime.FocusedControl()=="settings_back","past the tab's last row Next reaches Back, never a hidden tab's row");
  Check(v.runtime.FocusControl(tab.order.back(),v.time),"the tab's last row focuses");v.Key(MenuInput::Down);v.Frame();
  Check(v.runtime.FocusedControl()==tab.order.front(),"Down from the tab's last row wraps to its first");
  v.Key(MenuInput::Up);v.Frame();Check(v.runtime.FocusedControl()=="tab-"+std::string(tab.id),"Up from the tab's first row reaches its tab");
  for(const auto& other:PageTabs)if(&other!=&tab)for(const auto* id:other.order)
   Check(!v.runtime.CanActivateControl(id,v.time)&&!v.runtime.FocusControl(id,v.time),"a hidden tab's rows can neither act nor take focus");
  Check(v.runtime.TakeActions().empty(),"switching and walking a tab edits nothing");
 }
 Check(v.Event("onTabPrevious").actions.empty()&&v.Tab()==2,"onTabPrevious wraps from GRAPHICS to IMAGE");v.Settle();
 Check(v.Event("onTabNext").actions.empty()&&v.Tab()==0,"onTabNext wraps from IMAGE to GRAPHICS");v.Settle();
 Check(v.Event("tab_graphics").actions.empty()&&v.Tab()==0,"the current tab's event changes nothing");
 // Restart Renderer joins GRAPHICS while a renderer change waits for it.
 Check(v.runtime.SetState({{"settings.render.pending",true},{"settings.render.canRestart",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(v.runtime.FocusControl("settings_renderer",v.time),"the renderer row focuses");
 for(const auto* id:{"settings_restart_renderer","settings_ui_scale"}){v.Key(MenuInput::Next);v.Frame();Check(v.runtime.FocusedControl()==id,"a pending renderer change puts Restart Renderer after the renderer row");}
 Check(v.runtime.SetState({{"settings.render.pending",false},{"settings.render.canRestart",false}},v.error,v.time),v.error.c_str());v.Frame();
 Check(!v.runtime.FocusControl("settings_restart_renderer",v.time),"without a pending change Restart Renderer hides");
 // The pages cross-fade over 150 ms where motion is allowed.
 v.runtime.SetReducedMotion(false,v.time);v.Event("tab_image");v.Frame();
 Check(PageDisplay(v,PageTabs[0])=="block"&&PageDisplay(v,PageTabs[2])=="block","both pages show while they cross-fade");
 const auto leaving=v.runtime.PresentedValue("settings-page-graphics","opacity"),arriving=v.runtime.PresentedValue("settings-page-image","opacity");
 Check(leaving&&arriving&&leaving->data[0]<1&&arriving->data[0]<1&&!v.runtime.CanActivateControl("settings_preset",v.time),"mid-fade the leaving page fades out and no longer acts");
 for(int i=0;i<10;++i)v.Frame();
 Check(PageDisplay(v,PageTabs[0])=="none"&&PageDisplay(v,PageTabs[2])=="block"&&v.runtime.PresentedValue("settings-page-image","opacity")->data[0]==1,"after 150 ms only the new page shows");
 v.runtime.SetReducedMotion(true,v.time);v.Event("tab_graphics");v.Frame();
 Check(v.Tab()==0&&!v.runtime.CanActivateControl("settings_brightness",v.time)&&v.runtime.CanActivateControl("settings_preset",v.time),"the tab switches at once");
 for(int i=0;i<5;++i)v.Frame();Check(PageDisplay(v,PageTabs[2])=="none","reduced motion bounds the fade to 80 ms");
}
static void Semantics(const std::string& source,const std::string& locale){View v(source,locale);auto baseline=v.host.live;Check(v.Alias("draftPreset")=="balanced"&&v.Alias("baselinePreset")=="balanced","exact initial aliases");Tabs(v);v.Open();v.Key(MenuInput::End);v.Frame();{const auto last=v.Box("settings_preset-option-8"),list=v.Box("settings_preset-viewport");Check(v.Widget().highlight=="settings_preset-option-8"&&last.y>=list.y-.5f&&last.y+last.height<=list.y+list.height+.5f,"End reaches the ninth profile, wholly visible");}v.Key(MenuInput::Back);v.Frame();Check(!v.Widget().popupOpen&&v.runtime.TakeActions().empty()&&SettingsValuesEqual(v.tx.Draft(),baseline),"Back cancels highlight without writing");
 std::set<std::string> footprint(PerformancePresetTargets().begin(),PerformancePresetTargets().end());unsigned i=0;for(auto& p:PerformancePresets()){auto proposal=v.Select(i++);Check(std::get<std::string>(*proposal.proposal)==p.name,"all nine exact canonical names");auto before=v.tx.Draft();v.Accept(proposal);for(auto& [k,x]:v.Patch(p))Check(SettingsValueEqual(v.tx.Draft().at(k),x),"generated target accepted");for(auto& [k,x]:before)if(!footprint.contains(k))Check(SettingsValueEqual(v.tx.Draft().at(k),x),"all23 unrelated custom values preserved");Check(v.Alias("draftPreset")==p.name&&v.Alias("baselinePreset")=="balanced","draft alias changes, baseline stays exact");}
 Check(v.host.writes==0&&SettingsValuesEqual(v.host.live,baseline),"nine selections zero live writes");auto refused=v.Select(0);Check(v.runtime.AcknowledgeControlProposal(refused.node,refused.proposalToken,false),"refusal ACK");v.Frame();Check(std::get<std::string>(v.Widget().accepted)=="maximum","refused proposal preserves old readback");auto effects=v.Event("autoDetect");Check(effects.stateChanges.empty()&&effects.actions.size()==1,"Auto event one action zero writes");auto before=v.tx.Draft();Check(v.Dispatch(effects.actions[0],false).code==SettingsCode::Invalid&&SettingsValuesEqual(v.tx.Draft(),before),"failed observation preserves whole draft");v.message="#str_230010";v.Sync();v.Frame();auto message=v.runtime.PresentedValue("settings-message","text");Check(message&&message->text=="#str_230010","failed observation uses existing localized status");Check(v.Dispatch(effects.actions[0]).code==SettingsCode::Ok,"shared detected expansion succeeds");v.Sync();v.Frame();PerformancePresetSignals signals;signals.steamDeck=true;Check(v.Alias("draftPreset")==DetectPerformancePreset(signals).preset->name&&v.host.writes==0,"detected readback with zero writes");Check(v.Event("apply").actions.empty(),"unsupported Apply still blocked");
 Check(v.Focus("settings_brightness_number")&&v.runtime.BeginNumberEdit("settings_brightness_number",v.error,v.time),"begin real paired Number");auto n=v.runtime.GetWidgetState("settings_brightness_number")->number;Check(v.runtime.SetNumberSelection("settings_brightness_number",n->identity,0,n->state.text.size(),v.error,v.time),"select Number");n=v.runtime.GetWidgetState("settings_brightness_number")->number;Check(v.runtime.ReplaceNumberSelection("settings_brightness_number",n->identity,"-",v.error,v.time),"unconfirmed invalid local text");NumberDraftSummary d;Check(v.runtime.QueryNumberDrafts(d,v.error,v.time)&&d.blocking.size()==1,"actual local barrier blocks");v.pending=true;v.Sync();v.Frame();
 Check(!v.runtime.CanActivateControl("settings_lightgrid_preload",v.time),"the typed number blocks the tab's operations");Check(v.Event("autoDetect").actions.empty()&&v.Event("apply").actions.empty(),"named events cannot bypass local drafts");Check(v.Dispatch(effects.actions[0]).code==SettingsCode::Busy,"new local buffer invalidates queued operation");Check(v.runtime.DiscardNumberDrafts(d.barrier,v.error,v.time),"explicit exact local discard");
 // A published pending number blocks GRAPHICS' operations as well, and holds the strip.
 v.pending=false;v.Sync();v.Show("settings_preset");v.pending=true;v.Sync();v.Frame();
 Check(!v.runtime.CanActivateControl("settings_preset",v.time)&&!v.runtime.CanActivateControl("settings_autodetect",v.time),"both new controls blocked");
 for(const auto* event:{"tab_image","onTabNext","onTabPrevious"})Check(v.Event(event).actions.empty()&&v.Tab()==0,"a pending typed number holds the tab still");
 Check(!v.runtime.CanActivateControl("tab-image",v.time),"a pending typed number holds the strip");
 v.pending=false;v.Sync();v.Frame();Check(v.runtime.CanActivateControl("settings_preset",v.time)&&v.runtime.CanActivateControl("settings_autodetect",v.time),"discard enables operations again");std::string snapshot;auto draft=v.Alias("draftPreset");Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"source-bound save");v.runtime.CloseDocument();std::vector<Diagnostic> ds;Check(v.runtime.LoadDocument(source,"guis/menu/settings/system.q4ui",ds)&&v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"resource-style exact source recreation");v.Sync();v.Frame();Check(v.Alias("draftPreset")==draft&&v.Alias("baselinePreset")=="balanced"&&v.runtime.TakeActions().empty(),"aliases survive without action replay");}
static void TextFits(View& v,const char* id){auto* e=v.Element(id);auto size=e->GetBox().GetSize(Rml::BoxArea::Content);bool found=false;for(int i=0;i<e->GetNumChildren();++i)if(auto* t=dynamic_cast<Rml::ElementText*>(e->GetChild(i)))for(auto& line:t->GetLines()){found=true;if(line.position.x+line.width>size.x+1.1f||line.position.y+t->GetLineHeight()>size.y+1.1f)std::fprintf(stderr,"fit %s text=%s box=%g,%g line=%g,%g,%d lineheight=%g\n",id,line.text.c_str(),size.x,size.y,line.position.x,line.position.y,line.width,t->GetLineHeight());Check(line.position.x+line.width<=size.x+1.1f,"localized line fits content width");Check(line.position.y+t->GetLineHeight()<=size.y+1.1f,"localized line fits flowing height");}if(!found)std::fprintf(stderr,"no text lines in %s: rml=%s\n",id,e->GetInnerRML().c_str());Check(found,"essential text generated actual lines");}
// A row's label and value hold a fixed column, which the generator guarantees
// with every language's real glyph widths (check_settings_text and
// check_system_text), as on CONTROLS, GAME OPTIONS and AUDIO: they are checked
// at plain widths, and the +40% glyph stress is kept for text that flows
// (lists and their options, the footer message, the dialogs, the prompt bar).
// The rows grow with the text size (retained_settings_rows.grown), so their
// text is measured at every text size.
static void RowTextFits(View& v,const char* id){if(v.host.expansion==1)TextFits(v,id);}
static void Opening(View& v,float density,int width,int height,bool wholeFirstSetting){
 v.viewport.width=width;v.viewport.height=height;v.viewport.displayScale=density;
 Check(v.tx.Edit(View::Owner,{{"com_performancePreset",std::string("balanced")}}).code==SettingsCode::Ok,"opening default draft");v.Sync();v.Frame();
 v.Show("settings_backend");
 const auto inside=[](const Bounds& a,const Bounds& b){return a.x>=b.x-.5f&&a.y>=b.y-.5f&&a.x+a.width<=b.x+b.width+.5f&&a.y+a.height<=b.y+b.height+.5f;};
 const Bounds screen{0,0,float(width),float(height)};
 auto body=v.Box("settings-body"),first=v.Box("settings_backend"),footer=v.Box("settings-footer");
 if(!inside(body,screen))std::fprintf(stderr,"body=%g,%g %gx%g viewport=%dx%d density=%g\n",body.x,body.y,body.width,body.height,width,height,density);
 Check(inside(body,screen),"the pages' bounds stay inside the viewport");
 // APPLY CHANGES and the message stand under the tab's rows, which scroll in
 // one area (settings-<tab>-rows > -columns > -column-N); at 100% text in a
 // wide view the tab's two columns stand side by side in it, wholly shown.
 const auto rows=v.Box("settings-graphics-rows");
 if(!(inside(rows,body)&&footer.y>=rows.y+rows.height-.5f))std::fprintf(stderr,"rows=%g,%g %gx%g footer=%g,%g viewport=%dx%d density=%g\n",rows.x,rows.y,rows.width,rows.height,footer.y,footer.height,width,height,density);
 Check(inside(rows,body)&&footer.y>=rows.y+rows.height-.5f,"the footer stands under the tab's rows");
 {const auto left=v.Box("settings-graphics-column-0"),right=v.Box("settings-graphics-column-1");
  for(const auto& column:{left,right})if(!inside(column,rows))std::fprintf(stderr,"column=%g,%g %gx%g rows=%g,%g %gx%g viewport=%dx%d density=%g\n",column.x,column.y,column.width,column.height,rows.x,rows.y,rows.width,rows.height,width,height,density);
  Check(inside(left,rows)&&inside(right,rows)&&right.x>=left.x+left.width-.5f&&std::abs(right.y-left.y)<.5f,"the tab's two columns stand side by side in its rows");}
 Check(inside(footer,screen),"the footer stays inside the viewport");
 // The canvas sets the dp ratio; the display density no longer does.
 const float ratio=v.viewport.DpRatio();Check(std::abs(ratio-std::min(height/720.f,width/960.f))<1e-4f,"one canvas height spans the view");
 for(auto* id:{"settings_back","settings_apply"}){auto b=v.Box(id);if(!(b.height>=44*ratio-.5f&&inside(b,screen)))std::fprintf(stderr,"%s=%g,%g %gx%g viewport=%dx%d ratio=%g\n",id,b.x,b.y,b.width,b.height,width,height,ratio);Check(b.height>=44*ratio-.5f&&inside(b,screen),"Back and Apply keep full size targets inside the viewport");}
 for(auto* id:{"title","settings-message","settings_back-label","settings_apply-label"})TextFits(v,id);
 if(wholeFirstSetting){
  if(!inside(first,body)){std::fprintf(stderr,"opening first=%g,%g body=%g,%g density=%g\n",first.y,first.height,body.y,body.height,density);for(auto* id:{"settings-footer","settings-message","settings-actions","settings_back","settings_apply"}){auto b=v.Box(id);std::fprintf(stderr,"%s %g,%g %gx%g\n",id,b.x,b.y,b.width,b.height);}}
  Check(inside(first,body)&&inside(first,screen),"opening shows complete backend selector");
 }
}
// The GPU row with a long published device name: the closed value takes it
// in two lines, the list's row (across the whole settings row) in one.
static void Hardware(View& v,const std::string& device="NVIDIA GeForce RTX 4090 Laptop GPU") {
 for(const char* id:{"settings_backend","settings_gpu"}) {
  Check(v.Focus(id),"hardware selector reachable");v.Frame();
  RowTextFits(v,(std::string(id)+"-label").c_str());RowTextFits(v,(std::string(id)+"-value").c_str());
 }
 StateValues gpu{{"settings.render.count",1.0},{"settings.render.selected",0.0},{"settings.render.0.usable",true},
  {"settings.render.0.label",device}};
 Check(v.runtime.SetState(gpu,v.error,v.time),"publish actual GPU label");v.Frame();RowTextFits(v,"settings_gpu-value");
 v.Key(MenuInput::Accept);v.Frame();auto bounds=v.Box("settings_gpu-popup");
 TextFits(v,"settings_gpu-option-1-label");
 {const auto row=v.Box("settings_gpu");if(!(bounds.x>=row.x-.5f&&bounds.x<=row.x+row.width*.1f&&bounds.width>=row.width*.9f))std::fprintf(stderr,"gpu popup=%g,%g %gx%g row=%g,%g %gx%g\n",bounds.x,bounds.y,bounds.width,bounds.height,row.x,row.y,row.width,row.height);Check(bounds.x>=row.x-.5f&&bounds.x<=row.x+row.width*.1f&&bounds.width>=row.width*.9f,"the device list unfolds from the row's start, across the whole row");}
 Check(bounds.x>=-.1f&&bounds.y>=-.1f&&bounds.x+bounds.width<=v.viewport.width+.5f&&bounds.y+bounds.height<=v.viewport.height+.5f,"GPU popup clamped");
 {const auto body=v.Box("settings-body");Check(bounds.x>=body.x-.5f&&bounds.y>=body.y-.5f&&bounds.x+bounds.width<=body.x+body.width+.5f&&bounds.y+bounds.height<=body.y+body.height+.5f,"GPU popup unfolds within the pages' bounds");}
 v.Key(MenuInput::Back);v.Frame();
 Check(v.runtime.SetState({{"settings.render.selected",32.0}},v.error,v.time),"missing saved GPU state");v.Frame();
 Check(v.Element("settings_gpu-value")->GetInnerRML().find(v.host.Translate("#str_230154"))!=std::string::npos,"missing GPU has explicit localized value");
 Check(v.runtime.SetState({{"settings.render.autoDetect",false},{"settings.render.pending",true},{"settings.render.canRestart",false}},v.error,v.time),"pending hardware state");v.Frame();
 Check(!v.runtime.CanActivateControl("settings_autodetect",v.time)&&!v.runtime.CanActivateControl("settings_restart_renderer",v.time),"hardware pending blocks wrong-GPU detection and premature restart");
 Check(v.runtime.SetState({{"settings.render.selected",-1.0},{"settings.render.pending",false}},v.error,v.time),"restore hardware selection");v.Sync();v.Frame();
}
static void Layout(View& v,float density,int width,int height){std::fprintf(stderr,"layout %dx%d density=%g expansion=%g\n",width,height,density,v.host.expansion);v.viewport.width=width;v.viewport.height=height;v.viewport.displayScale=density;Hardware(v);std::istringstream labels(v.host.strings.at("#str_229977"));std::string label;size_t longest=0;unsigned candidate=0,index=0;while(std::getline(labels,label,';')){if(label.size()>longest){longest=label.size();candidate=index;}++index;}Check(v.tx.Edit(View::Owner,{{"com_performancePreset",std::string(PerformancePresets()[candidate].name)}}).code==SettingsCode::Ok,"select longest localized readback for fit");v.Sync();Check(v.Focus("settings_preset"),"preset reachable");v.Frame();auto box=v.Box("settings_preset");Check(box.x>=0&&box.x+box.width<=width+1,"selector within horizontal viewport");RowTextFits(v,"settings_preset-label");RowTextFits(v,"settings_preset-value");v.Open();v.Key(MenuInput::End);v.Frame();auto popup=v.Box("settings_preset-popup");Check(popup.x>=-.1f&&popup.y>=-.1f&&popup.x+popup.width<=width+.2f&&popup.y+popup.height<=height+.2f,"popup fully clamped to viewport");{const auto body=v.Box("settings-body");Check(popup.x>=body.x-.5f&&popup.y>=body.y-.5f&&popup.x+popup.width<=body.x+body.width+.5f&&popup.y+popup.height<=body.y+body.height+.5f,"popup unfolds within the pages' bounds");}for(unsigned option=0;option<PerformancePresetCount;++option)TextFits(v,("settings_preset-option-"+std::to_string(option)+"-label").c_str());v.Key(MenuInput::Back);v.Frame();Check(v.runtime.FocusControl("settings_autodetect",v.time),"Auto remains reachable at narrow viewport");v.Frame();auto button=v.Box("settings_autodetect"),body=v.Box("settings-body");Check(button.x>=0&&button.x+button.width<=width+1,"Auto horizontally contained");if(button.y<body.y-.5f||button.y+button.height>body.y+body.height+.5f)std::fprintf(stderr,"auto density=%g expansion=%g button=%g,%g,%g,%g body=%g,%g,%g,%g\n",density,v.host.expansion,button.x,button.y,button.width,button.height,body.x,body.y,body.width,body.height);Check(button.y>=body.y-.5f&&button.y+button.height<=body.y+body.height+.5f,"focused Auto wholly inside the pages' bounds");RowTextFits(v,"settings_autodetect-label");v.Key(MenuInput::Accept);auto actions=v.runtime.TakeActions();Check(actions.size()==1&&actions[0].event=="autodetect","Auto button queues guarded authored event");v.Frame();auto old=v.Box("settings_preset");v.Frame();box=v.Box("settings_preset");Check(old.x==box.x&&old.y==box.y&&old.width==box.width&&old.height==box.height,"unchanged layout settles");Check(v.host.draws>0&&v.host.writes==0,"actual render and zero native writes");}
// The Menu button (section 13.5): Apply with pending changes, Back in the
// dialogs and on a clean page, and the focus to a typed number Apply waits for.
static void MenuButton(const std::string& source,const std::string& locale){View v(source,locale);
 auto Ops=[&](){std::vector<std::string> o;for(auto& a:v.Event("onMenuButton").actions)o.push_back(a.operation);return o;};
 Check(v.runtime.HasEvent("onMenuButton"),"SYSTEM declares the Menu button");Check((Ops()==std::vector<std::string>{"ui.dismiss"}),"clean page: Menu closes as Back");
 // The prompt bar's Back is the page's own Back.
 Check(v.runtime.FocusControl("settings_back",v.time),"the prompt bar's Back focuses");v.Key(MenuInput::Accept);
 {const auto back=v.runtime.TakeActions();Check(back.size()==1&&back[0].event=="onback"&&back[0].action.empty(),"the prompt bar's Back runs the page's Back");}
 v.Show("settings_preset");
 v.Accept(v.Select(0));Check(v.tx.Dirty(),"a preset leaves pending changes");Check(Ops().empty(),"Apply unavailable: Menu does nothing");
 v.canApply=true;v.Sync();v.Frame();Check((Ops()==std::vector<std::string>{"settings.system.apply"}),"pending changes: Menu applies");
 v.pending=true;v.Sync();v.Frame();Check((Ops()==std::vector<std::string>{"ui.numberDrafts.focus"}),"a typed number first takes the focus");v.pending=false;v.Sync();v.Frame();
 Check(v.Event("onBack").actions.empty()&&v.runtime.GetState().at("page.discardVisible")==StateValue(true),"Back asks to discard");
 Check(v.runtime.ModalOpen(),"the discard dialog holds the input at once: the adapter keeps Menu as Back");v.Frame();Check(v.runtime.ModalOpen(),"and still after a frame");
 for(const auto* event:{"tab_screen","onTabNext","onTabPrevious"})Check(v.Event(event).actions.empty()&&v.Tab()==0,"the discard dialog holds the tab still");
 Check(!v.runtime.CanActivateControl("tab-screen",v.time)&&!v.runtime.CanActivateControl("settings_back",v.time),"the dialog holds the strip and the prompt bar");
 auto e=v.Event("onMenuButton");Check(e.actions.size()==1&&e.actions[0].operation=="ui.numberDrafts.focus"&&v.runtime.GetState().at("page.discardVisible")==StateValue(false),"in the discard dialog Menu continues editing, as Back");
 v.Frame();Check(!v.runtime.ModalOpen(),"the dialog closed releases the input");
 Check(v.runtime.SetState({{"settings.confirmationVisible",true},{"settings.canRevert",true}},v.error,v.time),v.error.c_str());v.Frame();
 for(const auto* event:{"tab_image","onTabNext","onTabPrevious"})Check(v.Event(event).actions.empty()&&v.Tab()==0,"the display confirmation holds the tab still");
 Check((Ops()==std::vector<std::string>{"settings.system.revert"}),"in the display confirmation Menu reverts, as Back");Check(v.host.writes==0,"the Menu button never writes settings itself");}
static void ExternalRefresh(const std::string& source,const std::string& locale) {
 View v(source,locale);
 const auto queued=v.Select(0);
 Check(v.runtime.CanDispatchControlAction(queued,v.time),"preset proposal starts eligible");
 for(const auto& [key,value]:v.Patch(*FindPerformancePreset("ultra")))v.host.live.at(key)=value;
 Check(v.tx.Refresh(View::Owner).code==SettingsCode::Ok,"external preset refreshes clean transaction");
 v.Sync();v.Frame();
 Check(v.Alias("draftPreset")=="ultra" && v.Alias("baselinePreset")=="ultra" &&
       std::get<std::string>(v.Widget().accepted)=="ultra","actual SYSTEM aliases and painted selector follow external preset");
 Check(!v.runtime.CanDispatchControlAction(queued,v.time),"old preset proposal cannot overwrite external readback");
 Check(v.Focus("settings_brightness_number") &&
       v.runtime.BeginNumberEdit("settings_brightness_number",v.error,v.time),"open local number editor before external change");
 auto number=v.runtime.GetWidgetState("settings_brightness_number")->number;
 Check(v.runtime.SetNumberSelection("settings_brightness_number",number->identity,0,number->state.text.size(),v.error,v.time),"select local number");
 number=v.runtime.GetWidgetState("settings_brightness_number")->number;
 Check(v.runtime.ReplaceNumberSelection("settings_brightness_number",number->identity,"-",v.error,v.time),"retain unfinished number text");
 v.host.live["r_brightness"]=1.625;
 Check(v.tx.Refresh(View::Owner).code==SettingsCode::Ok,"external numeric value refreshes service without committing local text");
 v.Sync();v.Frame();
 number=v.runtime.GetWidgetState("settings_brightness_number")->number;
 Check(number && number->state.text=="-" && number->conflict,"external refresh preserves unfinished text with a conflict");
 NumberDraftSummary drafts;
 Check(v.runtime.QueryNumberDrafts(drafts,v.error,v.time) && drafts.blocking.size()==1,"conflicting local text still blocks Apply and dismissal");
 Check(v.runtime.DiscardNumberDrafts(drafts.barrier,v.error,v.time),"explicit local discard accepts refreshed readback");
 v.Sync();v.Frame();
 Check(v.runtime.QueryNumberDrafts(drafts,v.error,v.time) && drafts.blocking.empty() &&
       v.runtime.GetWidgetState("settings_brightness_number")->accepted==StateValue(1.625),"discard reveals actual numeric value");
 Check(!v.tx.Dirty() && v.host.writes==0,"refresh and local conflict handling never write host settings");
}
// A typed number left pending on its tab while another tab is chosen: either
// the strip holds still until the number is committed or discarded, or the
// focus the Menu button and Keep editing ask for (ui.numberDrafts.focus) still
// reaches the number, so Apply's barrier can always be cleared from the page.
static void PendingNumberAcrossTabs(const std::string& source,const std::string& locale){View v(source,locale);
 Check(v.Focus("settings_brightness_number")&&v.runtime.BeginNumberEdit("settings_brightness_number",v.error,v.time),"begin the paired number");
 auto n=v.runtime.GetWidgetState("settings_brightness_number")->number;Check(v.runtime.SetNumberSelection("settings_brightness_number",n->identity,0,n->state.text.size(),v.error,v.time),"select the number");
 n=v.runtime.GetWidgetState("settings_brightness_number")->number;Check(v.runtime.ReplaceNumberSelection("settings_brightness_number",n->identity,"-",v.error,v.time),"invalid local text");
 NumberDraftSummary d;Check(v.runtime.QueryNumberDrafts(d,v.error,v.time)&&d.blocking.size()==1,"the typed number blocks");v.pending=true;v.Sync();v.Frame();
 for(const auto* event:{"tab_graphics","onTabNext","onTabPrevious"}){
  v.Event(event);v.Settle();
  Check(v.runtime.QueryNumberDrafts(d,v.error,v.time)&&d.blocking.size()==1&&d.blocking.front().control=="settings_brightness_number","the typed number stays a draft");
  if(v.Tab()==2)continue;
  const bool reached=v.runtime.FocusNumberDraft(d.barrier,d.blocking.front().control,v.error,v.time);v.Frame();
  if(!reached||v.runtime.FocusedControl()!="settings_brightness_number")
   std::fprintf(stderr,"%s left the typed number on a hidden tab: focus reached=%d focused=%s tab=%g error=%s\n",event,reached,v.runtime.FocusedControl().c_str(),v.Tab(),v.error.c_str());
  Check(reached&&v.runtime.FocusedControl()=="settings_brightness_number","a pending typed number on a hidden tab can still be focused from the page");
  v.Show("settings_brightness_number");
 }
 Check(v.runtime.DiscardNumberDrafts(d.barrier,v.error,v.time),"discard the typed number");Check(v.host.writes==0,"no setting written");}
int main(int argc,char** argv){
 Check(argc==3,"page and locale directory args");auto source=Read(argv[1]);std::string dir=argv[2];Semantics(source,dir+"/english_openq4.lang");
 ExternalRefresh(source,dir+"/english_openq4.lang");
 for(auto* locale:{"english","spanish","polish","russian","french","italian","german","brazilian","czech","hungarian","turkish","ukrainian"})for(float expansion:{1.f,1.4f}){
  std::fprintf(stderr,"locale %s expansion=%g\n",locale,expansion);
  View view(source,dir+"/"+locale+"_openq4.lang",1,1280,720,expansion);
  // Reuse the loaded instance across the viewport matrix: this also proves
  // reflow after actual resize/density changes, with stable accepted aliases.
  for(auto dims:{std::array<int,3>{1280,720,100},std::array<int,3>{1280,720,125},std::array<int,3>{1280,720,200},std::array<int,3>{800,600,100}})
   {Opening(view,dims[2]/100.f,dims[0],dims[1],std::string(locale)=="english"&&expansion==1.f);Layout(view,dims[2]/100.f,dims[0],dims[1]);}
 }
 // Last, as they qualify the dialogs' and the tabs' hold on input.
 MenuButton(source,dir+"/english_openq4.lang");
 PendingNumberAcrossTabs(source,dir+"/english_openq4.lang");
 std::printf("PASS %u checks; actual SYSTEM/RmlUi, twelve locales +40%% width stress, counted transaction host.\n",checks);
 return 0;
}
