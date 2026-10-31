// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Production document, Runtime and SettingsTransaction; counted font/CVar host.
#define main PresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main

static constexpr const char* Row="settings_lightgrid_preload";
static constexpr const char* StatusLine="settings_lightgrid_preload-status";
static constexpr const char* RendererRow="settings_renderer";
static constexpr const char* Notice="settings_renderer-status";
static void Inside(const Bounds& a,const Bounds& b,const char* why) {
 const bool okay=a.x>=b.x-1&&a.y>=b.y-1&&a.x+a.width<=b.x+b.width+1&&a.y+a.height<=b.y+b.height+1;
 if(!okay)std::fprintf(stderr,"%s: %g,%g %gx%g in %g,%g %gx%g\n",why,a.x,a.y,a.width,a.height,b.x,b.y,b.width,b.height);
 Check(okay,why);
}
// A note follows its row down the column: directly under it, inside the
// column and the pages' bounds.
static void Under(View& v,const char* note,const char* row,const char* why) {
 const auto a=v.Box(note),b=v.Box(row);
 if(std::abs(a.y-b.y-b.height)>1||a.x<b.x-1||a.x+a.width>b.x+b.width+1)std::fprintf(stderr,"%s: %s %g,%g %gx%g under %s %g,%g %gx%g\n",why,note,a.x,a.y,a.width,a.height,row,b.x,b.y,b.width,b.height);
 Check(std::abs(a.y-b.y-b.height)<=1&&a.x>=b.x-1&&a.x+a.width<=b.x+b.width+1,why);
 Inside(a,v.Box("settings-body"),why);
}
// What the service reports about the loaded map, and the line it shows.
struct LightGrid { bool committed,consumed,effective,pending; const char* text; };
static const LightGrid Loaded[]={
 {true,true,false,true,"#str_230078"},   // a saved preload waits for the next map
 {true,true,true,false,"#str_230079"},   // the map preloads its light grids
 {false,true,false,false,"#str_230080"}, // the map streams them
 {false,false,false,false,"#str_230081"},// the map has none
};
static void Status(View& v,const LightGrid& c,bool mapLoaded=true) {
 const StateValues s{{"settings.lightGrid.committed",c.committed},{"settings.lightGrid.mapLoaded",mapLoaded},
  {"settings.lightGrid.consumed",c.consumed},{"settings.lightGrid.effective",c.effective},{"settings.lightGrid.pending",c.pending}};
 Check(v.runtime.SetState(s,v.error,v.time),v.error.c_str());v.Frame();
}
static std::string Presented(View& v,const char* node,const char* property) {
 const auto value=v.runtime.PresentedValue(node,property);Check(value.has_value(),node);return value->text;
}
// One option label from a semicolon-separated localized list, as painted.
static std::string Segment(View& v,const char* key,unsigned index) {
 std::istringstream labels(v.host.strings.at(key));std::string label;
 for(unsigned i=0;i<=index;++i)std::getline(labels,label,';');
 return Rml::StringUtilities::EncodeRml(label);
}
static std::string NoticeText(View& v) { return Rml::StringUtilities::EncodeRml(v.host.strings.at("#str_230082")); }
// The Renderer Fallback choice: the ordinary edit, availability and the notice.
static void Renderer(View& v) {
 Check(v.Alias("draftRenderer")=="best"&&v.Alias("baselineRenderer")=="best","the aliases read the applied renderer");
 Check(v.Element("settings_renderer-value")->GetInnerRML()==Segment(v,"#str_41104",0),"the closed choice names the automatic pick");
 Check(Presented(v,Notice,"display")=="none","no reported fallback shows no notice");
 // A reported fallback shows the notice under the applied renderer.
 Check(v.runtime.SetState({{"settings.renderer.fallback",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(Presented(v,Notice,"display")=="block"&&v.Element(Notice)->GetInnerRML()==NoticeText(v),"a reported fallback shows the notice");
 Check(v.runtime.SetState({{"settings.renderer.fallback",false}},v.error,v.time),v.error.c_str());v.Frame();
 Check(v.Focus(RendererRow),"the renderer choice focuses");v.Frame();
 v.Key(MenuInput::Accept);v.Frame();Check(v.runtime.GetWidgetState(RendererRow)->popupOpen,"the renderer choice opens");
 v.Key(MenuInput::End);v.Frame();v.Key(MenuInput::Accept);auto actions=v.runtime.TakeActions();
 Check(actions.size()==1&&actions[0].proposal&&SettingsValueEqual(*actions[0].proposal,std::string("arb2")),"the choice proposes ARB2");
 ActionInvocation invocation;Check(v.runtime.ResolveAction(actions[0].action,invocation,v.error,&*actions[0].proposal),"the renderer action resolves");
 Check(invocation.operation=="settings.system.edit"&&invocation.arguments==StateValues({{"r_renderer",std::string("arb2")}}),"the choice edits only the renderer");
 Check(v.tx.Edit(View::Owner,invocation.arguments).code==SettingsCode::Ok,"the renderer draft is accepted");v.Sync();
 Check(v.runtime.AcknowledgeControlProposal(RendererRow,actions[0].proposalToken,true),"readback precedes acknowledgment");v.Frame();
 Check(v.host.writes==0,"a draft writes no setting");
 Check(v.Alias("draftRenderer")=="arb2"&&v.Alias("baselineRenderer")=="best","aliases tell the draft from the applied renderer");
 Check(v.Element("settings_renderer-value")->GetInnerRML()==Segment(v,"#str_41104",1),"the closed choice names ARB2");
 // The notice describes the applied renderer: a different pick hides it.
 Check(v.runtime.SetState({{"settings.renderer.fallback",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(Presented(v,Notice,"display")=="none","a drafted pick hides the notice about the applied renderer");
 Check(v.tx.Edit(View::Owner,{{"r_renderer",std::string("best")}}).code==SettingsCode::Ok,"return to the applied renderer");v.Sync();v.Frame();
 Check(Presented(v,Notice,"display")=="block","returning to the applied renderer shows the notice again");
 Check(v.tx.Edit(View::Owner,{{"r_renderer",std::string("arb2")}}).code==SettingsCode::Ok,"draft ARB2 again");v.Sync();
 Check(v.runtime.SetState({{"settings.renderer.fallback",false}},v.error,v.time),v.error.c_str());v.Frame();
 // Where the renderer cannot report its selection the row is disabled.
 Check(v.runtime.SetState({{"settings.renderer.available",false}},v.error,v.time),v.error.c_str());v.Frame();
 Check(!v.runtime.CanActivateControl(RendererRow,v.time),"a renderer without a selection report disables the row");
 auto opacity=v.runtime.PresentedValue(RendererRow,"opacity");
 Check(opacity&&std::abs(opacity->data[0]-.45)<1e-6,"the unavailable row has visible disabled feedback");
 v.Sync();v.Frame();Check(v.runtime.CanActivateControl(RendererRow,v.time),"a reporting renderer enables the row");
 opacity=v.runtime.PresentedValue(RendererRow,"opacity");Check(opacity&&opacity->data[0]==1,"the available row has full opacity");
 // While the page is busy it dims with its column.
 Check(v.runtime.SetState({{"settings.busy",true}},v.error,v.time),v.error.c_str());v.Frame();
 opacity=v.runtime.PresentedValue(RendererRow,"opacity");
 Check(!v.runtime.CanActivateControl(RendererRow,v.time)&&opacity&&std::abs(opacity->data[0]-.4)<1e-6,"a busy page dims the row like its column");
 v.Sync();v.Frame();
 // With the applied renderer drafted again, the notice sits inside the card.
 Check(v.tx.Edit(View::Owner,{{"r_renderer",std::string("best")}}).code==SettingsCode::Ok,"draft the applied renderer");v.Sync();
 Check(v.runtime.SetState({{"settings.renderer.fallback",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(Presented(v,Notice,"display")=="block"&&v.Element(Notice)->GetInnerRML()==NoticeText(v),"the notice reads its own text");
 Under(v,Notice,RendererRow,"the notice follows the renderer row");
}
// A request the page does not offer (a retired back-end name) shows as typed,
// selects no option and gives way to Auto.
static void Unoffered(View& v) {
 Check(v.tx.Cancel(View::Owner).code==SettingsCode::Ok,"end the transaction");
 v.host.live["r_renderer"]=std::string("nv20");Check(v.tx.Begin(View::Owner).code==SettingsCode::Ok,"begin on a retired request");
 v.Sync();Check(v.runtime.SetState({{"settings.renderer.fallback",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(v.Alias("draftRenderer")=="nv20"&&v.Element("settings_renderer-value")->GetInnerRML()=="nv20","an unoffered request shows as typed");
 Check(Presented(v,Notice,"display")=="block","the notice explains the retired request");
 Check(v.Focus(RendererRow),"the renderer choice focuses");v.Frame();
 v.Key(MenuInput::Accept);v.Frame();Check(v.runtime.GetWidgetState(RendererRow)->popupOpen,"the choice opens");
 for(const auto* part:{"settings_renderer-option-0-selected","settings_renderer-option-1-selected"})
  Check(!v.Element(part)->IsVisible(true),"no option claims the unoffered request");
 v.Key(MenuInput::Home);v.Frame();v.Key(MenuInput::Accept);auto actions=v.runtime.TakeActions();
 Check(actions.size()==1&&actions[0].proposal&&SettingsValueEqual(*actions[0].proposal,std::string("best")),"Auto replaces the retired request");
 Check(v.runtime.AcknowledgeControlProposal(RendererRow,actions[0].proposalToken,false),"the proposal is declined");v.Frame();
 Check(v.tx.Cancel(View::Owner).code==SettingsCode::Ok,"end the transaction");
 v.host.live["r_renderer"]=std::string("best");Check(v.tx.Begin(View::Owner).code==SettingsCode::Ok,"begin again");
 Check(v.runtime.SetState({{"settings.renderer.fallback",false}},v.error,v.time),v.error.c_str());v.Sync();v.Frame();
}
static void Run(const std::string& source,const std::string& folder,const char* locale,float expansion) {
 View v(source,folder+"/"+locale+"_openq4.lang",1,1280,720,expansion,folder+"/"+locale+"_guis.lang");
 Check(Presented(v,StatusLine,"display")=="none","no loaded map shows no light-grid status");
 // The toggle proposes the preload through the ordinary edit and writes nothing.
 Check(v.TabOf(Row)=="image"&&v.TabOf(RendererRow)=="graphics","Preload Light Grids is on IMAGE QUALITY, Renderer Fallback on GRAPHICS");
 Check(v.Focus(Row),"the preload toggle focuses");v.Frame();
 v.Key(MenuInput::Accept);auto actions=v.runtime.TakeActions();
 Check(actions.size()==1&&actions[0].proposal&&SettingsValueEqual(*actions[0].proposal,true),"the toggle proposes preloading");
 ActionInvocation invocation;Check(v.runtime.ResolveAction(actions[0].action,invocation,v.error,&*actions[0].proposal),"the preload action resolves");
 Check(invocation.operation=="settings.system.edit"&&invocation.arguments==StateValues({{"r_lightGridPreload",true}}),"the preload edits only its own setting");
 Check(v.tx.Edit(View::Owner,invocation.arguments).code==SettingsCode::Ok,"the preload draft is accepted");v.Sync();
 Check(v.runtime.AcknowledgeControlProposal(Row,actions[0].proposalToken,true),"readback precedes acknowledgment");v.Frame();
 Check(v.host.writes==0,"a draft writes no setting");
 Check(v.Alias("draftLightGridPreload")=="1"&&v.Alias("baselineLightGridPreload")=="0","aliases tell the draft from the applied choice");
 // The status line names what the loaded map does with its light grids.
 for(const auto& c:Loaded) {
  Status(v,c);
  Check(Presented(v,StatusLine,"display")=="block"&&Presented(v,StatusLine,"text")==c.text,"the status names the loaded map's light grids");
 }
 Status(v,Loaded[0],false);Check(Presented(v,StatusLine,"display")=="none","an unloaded map hides the status");
 Renderer(v);Unoffered(v);
 // The help, status and deferred messages fit at every size and density.
 struct Case{int width,height;float density;};
 for(const auto c:{Case{1280,720,1},Case{1280,720,2},Case{640,480,1},Case{640,480,2}}) {
  std::fprintf(stderr,"Render options locale=%s expansion=%g viewport=%dx%d density=%g\n",locale,expansion,c.width,c.height,c.density);
  v.viewport.width=c.width;v.viewport.height=c.height;v.viewport.displayScale=c.density;v.viewport.FitToMinimum(640,480);v.Frame();
  for(const auto& s:Loaded) {
   // The help and the status follow the row down its column, each in its own slots.
   Status(v,s);Check(v.Focus("settings_irradiance"),"focus leaves the preload row");v.Frame();
   Check(v.runtime.FocusControl(Row,v.time),"the preload row receives focus");v.Frame();
   Inside(v.Box(Row),v.Box("settings-body"),"the focused preload row stays in the pages' bounds");
   Inside(v.Box(Row),{0,0,float(c.width),float(c.height)},"the preload row stays inside the physical viewport");
   Under(v,"settings_lightgrid_preload-help",Row,"the help follows the preload row");
   Under(v,StatusLine,"settings_lightgrid_preload-help","the status follows the help");
   Inside(v.Box(StatusLine),{0,0,float(c.width),float(c.height)},"the status stays inside the physical viewport");
   RowTextFits(v,"settings_lightgrid_preload-label");RowTextFits(v,"settings_lightgrid_preload-help");RowTextFits(v,StatusLine);
  }
  for(const auto* m:{"#str_230073","#str_230074","#str_230075","#str_230076"}){v.message=m;v.Sync();v.Frame();TextFits(v,"settings-message");}
  v.message="#str_230007";v.Sync();v.Frame();
  // The renderer row and its notice fit, the notice under the row.
  Check(v.runtime.SetState({{"settings.renderer.fallback",true}},v.error,v.time),v.error.c_str());
  Check(v.Focus("settings_gpu"),"focus leaves the renderer row");v.Frame();
  Check(v.runtime.FocusControl(RendererRow,v.time),"the renderer row receives focus");v.Frame();
  Inside(v.Box(RendererRow),v.Box("settings-body"),"the focused renderer row stays in the pages' bounds");
  Inside(v.Box(RendererRow),{0,0,float(c.width),float(c.height)},"the renderer row stays inside the physical viewport");
  Under(v,Notice,RendererRow,"the notice stays under the renderer row");
  Inside(v.Box(Notice),{0,0,float(c.width),float(c.height)},"the notice stays inside the physical viewport");
  RowTextFits(v,"settings_renderer-label");RowTextFits(v,"settings_renderer-value");RowTextFits(v,Notice);
 }
 // Recovery titles the confirmation panel for any attempt.
 Check(v.runtime.SetState({{"settings.phase",3.0},{"settings.confirmationVisible",true}},v.error,v.time),v.error.c_str());v.Frame();
 Check(Presented(v,"confirmation-panel-title","text")=="#str_230077","recovery reads the neutral title");TextFits(v,"confirmation-panel-title");
 Check(v.runtime.SetState({{"settings.phase",1.0},{"settings.confirmationVisible",false}},v.error,v.time),v.error.c_str());v.Frame();
 Check(Presented(v,"confirmation-panel-title","text")=="#str_229993","otherwise the panel asks the Keep question");
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
 std::printf("SYSTEM render options: %u checks passed\n",checks);
}
