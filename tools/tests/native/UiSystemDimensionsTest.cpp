// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Production document and real Runtime/RmlUi/transaction, with counted font and
// settings host. Actual device validation is covered by the host/service tests.
#define main PresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main

struct Dimension { const char* id;const char* key;const char* label;double minimum,target;bool custom; };
static const Dimension dimensions[] = {
 {"settings_window_width","r_windowWidth","#str_229946",320,1600,false},
 {"settings_window_height","r_windowHeight","#str_229947",240,900,false},
 {"settings_custom_width","r_customWidth","#str_229948",320,2560,true},
 {"settings_custom_height","r_customHeight","#str_229949",240,1440,true}
};
static void Inside(const Bounds& a,const Bounds& b,const char* why) {
 if(a.x<b.x-1||a.y<b.y-1||a.x+a.width>b.x+b.width+1||a.y+a.height>b.y+b.height+1)
  std::fprintf(stderr,"%s: %g,%g %gx%g inside %g,%g %gx%g\n",why,a.x,a.y,a.width,a.height,b.x,b.y,b.width,b.height);
 Check(a.x>=b.x-1&&a.y>=b.y-1&&a.x+a.width<=b.x+b.width+1&&a.y+a.height<=b.y+b.height+1,why);
}
static NumberEditIdentity Identity(View& v,const char* id) {
 auto widget=v.runtime.GetWidgetState(id);Check(widget&&widget->number,"dimension editor owns identity");return widget->number->identity;
}
static void Replace(View& v,const char* id,const std::string& text) {
 Check(v.runtime.SetNumberSelection(id,Identity(v,id),0,v.runtime.GetWidgetState(id)->number->state.text.size(),v.error,v.time),"select complete dimension buffer");
 Check(v.runtime.ReplaceNumberSelection(id,Identity(v,id),text,v.error,v.time),"replace local dimension text");
}
static void Pending(View& v) {
 NumberDraftSummary draft;Check(v.runtime.QueryNumberDrafts(draft,v.error,v.time),"query real pending numeric drafts");
 v.pending=!draft.blocking.empty();v.Sync();v.Frame();
}
static void Propose(View& v,const Dimension& field) {
 const auto before=v.tx.Draft();
 Check(v.Focus(field.id),"dimension field can focus");v.Frame();
 RowTextFits(v,(std::string(field.id)+"-label").c_str());
 Inside(v.Box((std::string(field.id)+"-viewport").c_str()),v.Box("settings-body"),field.id);
 Check(v.runtime.BeginNumberEdit(field.id,v.error,v.time),"dimension begins precise local edit");
 for(const auto& bad:{std::string("-"),std::to_string(int(field.minimum)-1),std::string("16385"),std::string("1280.00000000000000001"),std::string("1e3")}) {
  Replace(v,field.id,bad);Pending(v);
  Inside(v.Box((std::string(field.id)+"-viewport").c_str()),v.Box("settings-body"),"invalid dimension remains fully editable after status reflow");
  Check(v.runtime.CanActivateControl(field.id,v.time)&&!v.runtime.CanActivateControl("settings_apply",v.time),"pending number stays editable while Apply is blocked");
  Check(!v.runtime.CommitNumberEdit(field.id,Identity(v,field.id),v.error,v.time),"invalid or fractional pixel dimension cannot propose");v.Frame();
  Inside(v.Box((std::string(field.id)+"-viewport").c_str()),v.Box("settings-body"),"validation leaves the full editing viewport visible");
  Check(v.runtime.TakeActions().empty()&&SettingsValuesEqual(v.tx.Draft(),before),"invalid dimension changes no typed draft");
  Check(v.runtime.GetWidgetState(field.id)->number->state.text==bad,"validation preserves correction text");
  RowTextFits(v,(std::string(field.id)+"-validation").c_str());
  Inside(v.Box((std::string(field.id)+"-validation").c_str()),v.Box("settings-body"),"the validation under the row stays inside the pages' bounds");
 }
 Replace(v,field.id,std::to_string(int(field.target)));Pending(v);
 Check(v.runtime.CommitNumberEdit(field.id,Identity(v,field.id),v.error,v.time),"whole pixel dimension commits a proposal");
 auto actions=v.runtime.TakeActions();Check(actions.size()==1&&actions[0].proposal&&*actions[0].proposal==StateValue(field.target),"one exact typed dimension proposal");
 Check(v.runtime.CanDispatchControlAction(actions[0],v.time),"current numeric proposal still eligible while local draft blocks other actions");
 ActionInvocation action;Check(v.runtime.ResolveAction(actions[0].action,action,v.error,&*actions[0].proposal),"authored dimension action resolves");
 StateValues expected{{field.key,field.target}};if(field.custom)expected["r_mode"]=-1.0;
 Check(action.operation=="settings.system.edit"&&SettingsValuesEqual(action.arguments,expected),"custom mode and dimension are one atomic draft patch");
 Check(v.tx.Edit(View::Owner,action.arguments).code==SettingsCode::Ok,"dimension patch stages in owned transaction");v.Sync();
 Check(v.runtime.AcknowledgeControlProposal(field.id,actions[0].proposalToken,true),"authoritative dimension readback precedes acknowledgment");Pending(v);
 Check(!v.pending&&v.host.writes==0,"accepted field clears local barrier without live writes");
 for(const auto& [key,value]:before)Check(SettingsValueEqual(v.tx.Draft().at(key),expected.contains(key)?expected.at(key):value),"dimension commit preserves unrelated draft fields");
}
static void Run(const std::string& source,const std::string& directory,const char* locale,float expansion) {
 View v(source,directory+"/"+locale+"_openq4.lang",1,1280,720,expansion,directory+"/"+locale+"_guis.lang");v.canApply=true;
 for(const auto& field:dimensions)Check(v.host.strings.contains(field.label),"dimension label is localized");
 for(const char* key:{"#str_230021","#str_230022","#str_230023","#str_230024","#str_229943"})Check(v.host.strings.contains(key),"dimension help and validation are localized");
 const auto baseline=v.host.live;
 struct Case{int width,height;float density,text;};
 for(const auto c:{Case{1280,720,1.25,1},Case{640,480,1,2},Case{3440,1440,2,2}}) {
  std::fprintf(stderr,"Dimensions locale=%s expansion=%g viewport=%dx%d density=%g text=%g\n",locale,expansion,c.width,c.height,c.density,c.text);
  v.viewport.width=c.width;v.viewport.height=c.height;v.viewport.displayScale=c.density;v.viewport.textScale=c.text;
  Check(v.tx.Edit(View::Owner,{{"r_windowWidth",1280.0},{"r_windowHeight",720.0},{"r_customWidth",1920.0},{"r_customHeight",1080.0},{"r_mode",3.0},{"r_fullscreen",false},{"r_fullscreenDesktop",false}}).code==SettingsCode::Ok,"seed complete dimension draft");v.Sync();v.Frame();
  for(const auto& field:dimensions)Propose(v,field);
  Check(v.runtime.FocusControl("settings_custom_height",v.time),"custom sizing help reachable");v.Frame();RowTextFits(v,"dimensions-hint");
  Check(v.tx.Edit(View::Owner,{{"r_fullscreen",true},{"r_fullscreenDesktop",true}}).code==SettingsCode::Ok,"draft dependency changes");v.Sync();v.Frame();
  for(const auto& field:dimensions) {
   Check(!v.runtime.CanActivateControl(field.id,v.time),"inactive dimension family rejects input");
   auto opacity=v.runtime.PresentedValue(field.id,"opacity");Check(opacity&&std::abs(opacity->data[0]-.45)<1e-6,"inactive dimension family visibly dims");
  }
 }
 Check(v.tx.Edit(View::Owner,{{"r_fullscreen",false},{"r_fullscreenDesktop",false}}).code==SettingsCode::Ok,"restore editable sizing families");v.Sync();v.Frame();
 for(const auto& guard:StateValues{{"settings.busy",true},{"settings.confirmationVisible",true},{"page.discardVisible",true},{"settings.open",false},{"settings.phase",2.0}}) {
  v.Sync();Check(v.runtime.SetState({{guard.first,guard.second}},v.error,v.time),"publish competing workflow");v.Frame();
  for(const auto& field:dimensions)Check(!v.runtime.CanActivateControl(field.id,v.time),"dimension edits honor modal and service ownership");
  Check(v.runtime.SetState({{"page.discardVisible",false}},v.error,v.time),"clear local modal guard");
 }
 v.Sync();v.Frame();Check(v.Focus("settings_window_width")&&v.runtime.BeginNumberEdit("settings_window_width",v.error,v.time),"begin unfinished size before recreation");
 Replace(v,"settings_window_width","-");Pending(v);std::string snapshot;
 Check(v.runtime.SaveSnapshot(snapshot,v.error,v.time),"dimension document snapshot saves pending correction");
 v.runtime.CloseDocument();std::vector<Diagnostic> diagnostics;
 Check(v.runtime.LoadDocument(source,"guis/menu/settings/system.q4ui",diagnostics)&&v.runtime.RestoreSnapshot(snapshot,v.error,v.time),"dimension fields recreate from exact source");v.Sync();v.Frame();
 Check(v.runtime.GetWidgetState("settings_window_width")->number->state.text=="-"&&v.runtime.TakeActions().empty(),"recreation preserves invalid text without replaying a proposal");
 NumberDraftSummary draft;Check(v.runtime.QueryNumberDrafts(draft,v.error,v.time)&&v.runtime.DiscardNumberDrafts(draft.barrier,v.error,v.time),"explicit discard clears restored local size");v.pending=false;v.Sync();v.Frame();
 Check(v.Focus("settings_window_width")&&v.runtime.BeginNumberEdit("settings_window_width",v.error,v.time),"begin proposal before dependency loss");Replace(v,"settings_window_width","1920");
 Check(v.runtime.CommitNumberEdit("settings_window_width",Identity(v,"settings_window_width"),v.error,v.time),"queue dimension edit");auto queued=v.runtime.TakeActions();Check(queued.size()==1,"one queued dimension edit");
 Check(v.runtime.SetState({{"settings.draft.r_fullscreen",true}},v.error,v.time),"window size becomes inactive before dispatch");v.Frame();
 Check(!v.runtime.CanDispatchControlAction(queued[0],v.time),"dependency change retires stale dimension proposal");
 Check(v.tx.Cancel(View::Owner).code==SettingsCode::Ok&&v.host.writes==0&&SettingsValuesEqual(v.host.live,baseline),"all sizing drafts cancel without touching the live host");
}
int main(int argc,char** argv) {
 Check(argc==3||argc==4,"page, language folder and optional locale");const auto source=Read(argv[1]);
 unsigned ran=0;
 for(const char* locale:{"english","spanish","polish","russian","french","italian","german"}) {
  if(argc==4&&std::string(argv[3])!=locale)continue;
  for(float expansion:{1.f,1.4f})Run(source,argv[2],locale,expansion);
  ++ran;
 }
 Check(ran>0,"the named locale is one this test covers");
 std::printf("SYSTEM dimensions: %u checks passed\n",checks);
}
