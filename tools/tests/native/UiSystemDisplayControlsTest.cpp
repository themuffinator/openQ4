// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Real production document, Runtime/RmlUi and transaction; counted font/host.
// Never opens a game window. Engine Apply/Keep/Revert is qualified separately.
#define main PresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main

struct Field { const char* id; const char* key; StateValue target; const char* label; };
static const Field fields[] = {
 {"settings_fullscreen","r_fullscreen",false,"#str_200147"},
 {"settings_borderless","r_borderless",true,"#str_229909"},
 {"settings_fullscreen_policy","r_fullscreenDesktop",false,"#str_229910"},
 {"settings_msaa","r_multiSamples",16.0,"#str_41093"}
};
static void Inside(const Bounds& a,const Bounds& b,const char* why) {
 Check(a.x>=b.x-1&&a.y>=b.y-1&&a.x+a.width<=b.x+b.width+1&&a.y+a.height<=b.y+b.height+1,why);
}
static void Propose(View& v,const Field& field) {
 const auto before=v.tx.Draft();
 Check(v.Focus(field.id),"display control focuses");v.Frame();
 Inside(v.Box(field.id),v.Box("settings-body"),"complete focused display control visible");
 RowTextFits(v,(std::string(field.id)+"-label").c_str());
 v.Key(MenuInput::Accept);v.Frame();
 if(v.runtime.GetWidgetState(field.id)->popupOpen) {
  v.Key(MenuInput::End);v.Frame();
  auto state=v.runtime.GetWidgetState(field.id);
  Check(state&&state->popupOpen,"display popup remains open during highlight");
  Inside(v.Box((std::string(field.id)+"-popup").c_str()),v.Box("settings-body"),"display popup respects scroll body");
  Inside(v.Box(state->highlight.c_str()),v.Box((std::string(field.id)+"-viewport").c_str()),"last display option wholly visible");
  TextFits(v,(state->highlight+"-label").c_str());
  Check(v.runtime.TakeActions().empty()&&SettingsValuesEqual(v.tx.Draft(),before),"popup highlight changes no draft");
  v.Key(MenuInput::Accept);
 }
 auto actions=v.runtime.TakeActions();
 Check(actions.size()==1&&actions[0].proposal&&SettingsValueEqual(*actions[0].proposal,field.target),"exact typed display proposal");
 Check(v.runtime.CanDispatchControlAction(actions[0],v.time),"live display proposal eligible");
 ActionInvocation action;Check(v.runtime.ResolveAction(actions[0].action,action,v.error,&*actions[0].proposal),"display action resolves");
 Check(action.operation=="settings.system.edit"&&action.arguments==StateValues({{field.key,field.target}}),"one existing catalog key and no direct restart");
 Check(v.tx.Edit(View::Owner,action.arguments).code==SettingsCode::Ok,"display edit stages in transaction");
 v.Sync();Check(v.runtime.AcknowledgeControlProposal(field.id,actions[0].proposalToken,true),"authoritative readback before ACK");v.Frame();
 for(const auto& [key,value]:before)Check(SettingsValueEqual(v.tx.Draft().at(key),key==field.key?field.target:value),"display edit preserves unrelated draft fields");
 Check(v.host.writes==0,"selection never writes CVars or starts display work");
}
static void Run(const std::string& source,const std::string& directory,const char* locale,float expansion) {
 View v(source,directory+"/"+locale+"_openq4.lang",1,1280,720,expansion,directory+"/"+locale+"_guis.lang");
 for(const auto& field:fields)Check(v.host.strings.contains(field.label),"display label uses existing localized text");
 for(const auto* key:{"#str_230020","#str_229911","#str_229900"})Check(v.host.strings.contains(key),"all choice and section translations exist");
 const auto baseline=v.tx.Baseline();
 struct Case{int width,height;float density,text;};
 for(const auto c:{Case{1280,720,1.25,1},Case{640,480,1,2},Case{3440,1440,2,2}}) {
  std::fprintf(stderr,"Display controls locale=%s expansion=%g viewport=%dx%d density=%g text=%g\n",locale,expansion,c.width,c.height,c.density,c.text);
  v.viewport.width=c.width;v.viewport.height=c.height;v.viewport.displayScale=c.density;v.viewport.textScale=c.text;
  Check(v.tx.Edit(View::Owner,{{"r_fullscreen",true},{"r_borderless",false},{"r_fullscreenDesktop",true},{"r_multiSamples",0.0}}).code==SettingsCode::Ok,"seed opposite display drafts");v.Sync();v.Frame();
  for(const auto& field:fields)Propose(v,field);
  Check(SettingsValuesEqual(v.host.live,baseline),"all display choices leave original host snapshot intact");
  // Existing popup cancellation must also preserve a nonlisted custom value.
  Check(v.tx.Edit(View::Owner,{{"r_multiSamples",3.0}}).code==SettingsCode::Ok,"counted host permits observed custom baseline shape");v.Sync();v.Frame();
  Check(v.runtime.FocusControl("settings_msaa",v.time),"focus custom MSAA");v.Key(MenuInput::Accept);v.Frame();v.Key(MenuInput::Home);v.Key(MenuInput::Back);v.Frame();
  Check(v.runtime.TakeActions().empty()&&SettingsValueEqual(v.tx.Draft().at("r_multiSamples"),3.0),"cancel preserves unlisted accepted value");
  Check(v.tx.Edit(View::Owner,{{"r_multiSamples",16.0}}).code==SettingsCode::Ok,"restore authored value");v.Sync();v.Frame();
 }
 std::string snapshot;Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"display source snapshot saves");
 v.runtime.CloseDocument();std::vector<Diagnostic> diagnostics;
 Check(v.runtime.LoadDocument(source,"guis/menu/settings/system.q4ui",diagnostics)&&v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"display controls survive source-bound recreation");v.Sync();v.Frame();
 for(const auto& field:fields)Check(SettingsValueEqual(v.runtime.GetWidgetState(field.id)->accepted,field.target),"restored widget follows current service draft");
 Check(v.runtime.TakeActions().empty(),"resource recreation replays no display operation");
 for(const auto& blocked:StateValues{{"settings.busy",true},{"settings.confirmationVisible",true},{"page.discardVisible",true},{"ui.numberDraftsPending",true},{"settings.open",false},{"settings.phase",2.0}}) {
  v.Sync();Check(v.runtime.SetState({{blocked.first,blocked.second}},v.error,v.time),"publish display guard");v.Frame();
  for(const auto& field:fields)Check(!v.runtime.CanActivateControl(field.id,v.time),"display operations gated during competing workflows");
  Check(v.runtime.SetState({{"page.discardVisible",false}},v.error,v.time),"clear local discard modal");
 }
 v.Sync();Check(v.Focus("settings_msaa"),"focus available MSAA before capability loss");
 v.Key(MenuInput::Accept);v.Frame();Check(v.runtime.GetWidgetState("settings_msaa")->popupOpen,"MSAA popup starts open");
 Check(v.runtime.SetState({{"settings.msaaAvailable",false}},v.error,v.time),"unavailable renderer capability");v.Frame();
 Check(!v.runtime.CanActivateControl("settings_msaa",v.time),"MSAA disabled for unavailable backend");
 auto opacity=v.runtime.PresentedValue("settings_msaa","opacity");
 Check(opacity&&std::abs(opacity->data[0]-.45)<1e-6,"unavailable MSAA has visible disabled feedback");
 Check(!v.runtime.GetWidgetState("settings_msaa")->popupOpen && v.runtime.TakeActions().empty(),"capability loss closes popup without a proposal");
 v.Sync();v.Frame();Check(v.runtime.FocusControl("settings_msaa",v.time),"focus restored MSAA capability");
 opacity=v.runtime.PresentedValue("settings_msaa","opacity");
 Check(opacity&&opacity->data[0]==1,"available MSAA restores full opacity");
 v.Key(MenuInput::Accept);v.Frame();Check(v.runtime.GetWidgetState("settings_msaa")->popupOpen,"restored MSAA popup opens");
 v.Key(MenuInput::Home);v.Frame();v.Key(MenuInput::Accept);
 auto pending=v.runtime.TakeActions();Check(pending.size()==1&&pending[0].proposal,"queue MSAA proposal before capability change");
 Check(v.runtime.SetState({{"settings.msaaAvailable",false}},v.error,v.time),"revoke queued MSAA capability");v.Frame();
 Check(!v.runtime.CanDispatchControlAction(pending[0],v.time),"capability loss rejects stale queued MSAA proposal");
 Check(v.tx.Cancel(View::Owner).code==SettingsCode::Ok&&v.host.writes==0&&SettingsValuesEqual(v.host.live,baseline),"Cancel discards all display drafts without touching host settings");
}
int main(int argc,char** argv) {
 Check(argc==3||argc==4,"page, language folder and optional locale");auto source=Read(argv[1]);
 unsigned ran=0;
 for(const auto* locale:{"english","spanish","polish","russian","french","italian","german"}) {
  if(argc==4&&std::string(argv[3])!=locale)continue;
  for(float expansion:{1.f,1.4f})Run(source,argv[2],locale,expansion);
  ++ran;
 }
 Check(ran>0,"the named locale is one this test covers");
 std::printf("SYSTEM display controls: %u checks passed\n",checks);
}
