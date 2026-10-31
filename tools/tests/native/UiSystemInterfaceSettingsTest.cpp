// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Production document, Runtime and SettingsTransaction; counted font/CVar host.
#define main PresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main

static void Inside(const Bounds& a,const Bounds& b,const char* why) {
 const bool okay=a.x>=b.x-1&&a.y>=b.y-1&&a.x+a.width<=b.x+b.width+1&&a.y+a.height<=b.y+b.height+1;
 if(!okay)std::fprintf(stderr,"%s: %g,%g %gx%g in %g,%g %gx%g\n",why,a.x,a.y,a.width,a.height,b.x,b.y,b.width,b.height);
 Check(okay,why);
}
static void Refresh(View& v) {
 v.viewport.userScale=float(std::get<double>(v.host.live.at("ui_retainedScale")));
 v.viewport.textScale=float(std::get<double>(v.host.live.at("ui_retainedTextScale")));
 v.viewport.FitToMinimum(640,480);
 v.canApply=v.tx.Dirty();v.Sync();v.Frame();
}
static void Choose(View& v,const char* control,double expected,bool last) {
 Check(v.Focus(control),"interface choice focuses");v.Frame();
 v.Key(MenuInput::Accept);v.Frame();auto opened=v.runtime.GetWidgetState(control);
 Check(opened&&opened->popupOpen,"interface choice opens inside actual body");
 v.Key(last?MenuInput::End:MenuInput::Home);v.Frame();
 Check(v.runtime.TakeActions().empty(),"highlighting does not change settings");
 v.Key(MenuInput::Accept);auto actions=v.runtime.TakeActions();
 Check(actions.size()==1&&actions[0].proposal&&SettingsValueEqual(*actions[0].proposal,expected),"exact size proposal");
 ActionInvocation invocation;Check(v.runtime.ResolveAction(actions[0].action,invocation,v.error,&*actions[0].proposal),"authored size action resolves");
 Check(invocation.operation=="settings.system.edit"&&invocation.arguments.size()==1,"size edit uses ordinary transaction");
 Check(v.tx.Edit(View::Owner,invocation.arguments).code==SettingsCode::Ok,"size draft accepted");
 v.Sync();Check(v.runtime.AcknowledgeControlProposal(control,actions[0].proposalToken,true),"size readback precedes acknowledgment");
 Refresh(v);
}
static void Apply(View& v) {
 Check(v.runtime.FocusControl("settings_apply",v.time),"Apply can focus after size reset");v.Frame();
 const Bounds screen{0,0,float(v.viewport.width),float(v.viewport.height)};
 Inside(v.Box("settings_apply"),screen,"Apply remains reachable at current size");
 const auto action=v.Event("apply");Check(action.actions.size()==1&&action.actions[0].operation=="settings.system.apply","normal guarded Apply event");
 Check(v.tx.Apply(View::Owner,v.time).code==SettingsCode::Ok,"ordinary Apply commits sizes");Refresh(v);
}
static void Run(const std::string& source,const std::string& folder,const char* locale,float expansion) {
 View v(source,folder+"/"+locale+"_openq4.lang",1,1280,720,expansion,folder+"/"+locale+"_guis.lang");v.host.writable=true;
 const auto original=v.host.live;Choose(v,"settings_ui_scale",2,true);Choose(v,"settings_text_scale",2,true);
 Check(v.host.writes==0&&SettingsValuesEqual(v.host.live,original),"size drafts do not preview or archive before Apply");
 Check(v.Alias("draftUIScale")=="2"&&v.Alias("baselineUIScale")=="1"&&v.Alias("draftTextScale")=="2","both size readbacks identify draft versus applied");
 Apply(v);Check(v.host.writes==1&&!v.tx.Dirty(),"sizes commit together without a device confirmation");
 for(const auto& [key,value]:original)if(key!="ui_retainedScale"&&key!="ui_retainedTextScale")Check(SettingsValueEqual(v.host.live.at(key),value),"size Apply leaves unrelated settings alone");
 struct Case{int width,height;float density;};
 for(const auto c:{Case{1280,720,1},Case{1280,720,2},Case{640,480,1},Case{640,480,2}}) {
  std::fprintf(stderr,"Interface sizes locale=%s expansion=%g viewport=%dx%d density=%g UI=200 text=200\n",locale,expansion,c.width,c.height,c.density);
  v.viewport.width=c.width;v.viewport.height=c.height;v.viewport.displayScale=c.density;Refresh(v);
  Check(v.Focus("settings_reset_sizes"),"reset can receive ordinary focus");v.Frame();
  Inside(v.Box("settings_reset_sizes"),v.Box("settings-body"),"complete reset control is revealed");
  Inside(v.Box("settings_reset_sizes"),{0,0,float(c.width),float(c.height)},"reset stays inside physical viewport");
  RowTextFits(v,"settings_reset_sizes-label");
  Check(v.tx.Edit(View::Owner,{{"r_brightness",1.2}}).code==SettingsCode::Ok,"unrelated draft before reset");Refresh(v);
  const auto reset=v.Event("resetSizes");Check(reset.actions.size()==1,"guarded reset produces one transaction edit");
  Check(reset.actions[0].operation=="settings.system.edit"&&reset.actions[0].arguments==StateValues({{"ui_retainedScale",1.0},{"ui_retainedTextScale",1.0}}),"reset changes only two size fields");
  const auto writes=v.host.writes;Check(v.tx.Edit(View::Owner,reset.actions[0].arguments).code==SettingsCode::Ok,"reset edits draft");Refresh(v);
  Check(v.host.writes==writes&&SettingsValueEqual(v.tx.Draft().at("r_brightness"),1.2)&&SettingsValueEqual(v.host.live.at("ui_retainedScale"),2.0),"reset preserves unrelated draft and waits for Apply");
  Apply(v);Check(SettingsValueEqual(v.host.live.at("ui_retainedScale"),1.0)&&SettingsValueEqual(v.host.live.at("ui_retainedTextScale"),1.0),"Apply restores normal interface size");
  Check(v.tx.Edit(View::Owner,{{"ui_retainedScale",2.0},{"ui_retainedTextScale",2.0}}).code==SettingsCode::Ok,"prepare next large size case");Refresh(v);Apply(v);
 }
 Choose(v,"settings_ui_scale",.75,false);Choose(v,"settings_text_scale",1,false);Apply(v);
 Check(SettingsValueEqual(v.host.live.at("ui_retainedScale"),.75)&&SettingsValueEqual(v.host.live.at("ui_retainedTextScale"),1.0),"smallest UI preference and normal text apply from largest small-window layout");
 v.pending=true;v.Sync();v.Frame();Check(!v.runtime.CanActivateControl("settings_ui_scale",v.time)&&!v.runtime.CanActivateControl("settings_text_scale",v.time)&&!v.runtime.CanActivateControl("settings_reset_sizes",v.time),"unconfirmed numeric draft blocks size controls");
 Check(v.Event("resetSizes").actions.empty(),"named reset cannot bypass local draft guard");
}
int main(int argc,char** argv) {
 Check(argc==3||argc==4,"source, locale folder and optional locale");const auto source=Read(argv[1]);
 unsigned ran=0;
 for(const auto* locale:{"english","spanish","polish","russian","french","italian","german"}) {
  if(argc==4&&std::string(argv[3])!=locale)continue;
  for(float expansion:{1.f,1.4f})Run(source,argv[2],locale,expansion);
  ++ran;
 }
 Check(ran>0,"the named locale is one this test covers");
 std::printf("SYSTEM interface settings: %u checks passed\n",checks);
}
