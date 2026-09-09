// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Runtime.h"
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool value, const char* message) { ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
static Json::Value Typed(const char* type, Json::Value value, const char* unit = nullptr) {
	Json::Value result; result["type"]=type; result["value"]=std::move(value); if(unit) result["unit"]=unit; return result;
}
static Json::Value Ref(const char* name) { Json::Value value; value["state"]=name; return value; }
static Json::Value Select(Json::Value condition, Json::Value yes, Json::Value no) {
	Json::Value value; value["op"]="select"; value["args"].append(condition); value["args"].append(yes); value["args"].append(no); return value;
}
static std::string Text(const Json::Value& value) { Json::StreamWriterBuilder writer; writer["indentation"]=""; writer["precision"]=17; return Json::writeString(writer,value); }
static Json::Value Parse(const std::string& source) {
	Json::CharReaderBuilder builder; std::unique_ptr<Json::CharReader> reader(builder.newCharReader()); Json::Value value; std::string error;
	Check(reader->parse(source.data(),source.data()+source.size(),&value,&error),"snapshot is valid JSON"); return value;
}
static Json::Value Box(const char* id, double x, double y, double width, double height) {
	Json::Value node; node["id"]=id; node["type"]="group"; node["children"]=Json::Value(Json::arrayValue); auto& p=node["properties"];
	p["display"]=Typed("keyword","block"); p["position"]=Typed("keyword","absolute"); p["opacity"]=Typed("number",1);
	p["left"]=Typed("length",x,"dp"); p["top"]=Typed("length",y,"dp"); p["width"]=Typed("length",width,"dp"); p["height"]=Typed("length",height,"dp");
	return node;
}
static void Bind(Json::Value& source,const char* node,const char* property,Json::Value expression) {
	Json::Value value; value["id"]=std::string(node)+"-"+property; value["node"]=node; value["property"]=property; value["value"]=expression; source["bindings"].append(value);
}
static Json::Value Button(Json::Value& source,const char* id,double y) {
	auto node=Box(id,5,y,120,30); auto& control=node["control"]; control["role"]="button"; control["label"]="#str_label"; control["action"]="hit";
	Json::Value colour(Json::arrayValue);for(double c:{.2,.3,.4,1.0})colour.append(c);node["properties"]["background-color"]=Typed("color",colour);
	for(const auto* name:{"default","hover","focus","pressed","disabled"}) control["states"][name]=std::string(id)+"-"+name;
	for(const auto* name:{"default","hover","focus","pressed","disabled"}) {
		Json::Value timeline,track; timeline["id"]=std::string(id)+"-"+name; timeline["durationMs"]=1;
		track["node"]=id; track["property"]="opacity";
		for(unsigned at:{0u,1u}) { Json::Value key;key["atMs"]=at;key["value"]=Typed("number",std::string(name)=="focus"?1:.7);track["keys"].append(key); }
		timeline["tracks"].append(track);source["timelines"].append(timeline);
	}
	return node;
}
static Json::Value WriteState(const char* name,bool value) { Json::Value step;step["op"]="setState";step["values"][name]=value;return step; }
static Json::Value Source() {
	Json::Value source;source["format"]="openq4-ui";source["version"]=1;source["id"]="modal-runtime";
	for(const char* id:{"open","nested","other","safeEnabled","alternativeEnabled","hostOpen"}) {
		source["state"][id]["type"]="boolean";source["state"][id]["initial"]=std::string(id)=="safeEnabled";
	}
	source["state"]["hostOpen"]["cvar"]="modal_host";
	source["actions"]["hit"]["operation"]="test.hit";source["actions"]["hit"]["arguments"]=Json::Value(Json::objectValue);
	source["events"]["close"].append(WriteState("open",false));source["events"]["closeNested"].append(WriteState("nested",false));
	source["events"]["conflict"].append(WriteState("other",true));
	auto root=Box("root",0,0,640,400),body=Box("body",10,20,200,100),content=Box("body-content",0,0,180,360);
	body["properties"]["overflow"]=Typed("keyword","auto");
	content["children"].append(Button(source,"opener",5));content["children"].append(Button(source,"background-lower",280));body["children"].append(content);root["children"].append(body);
	auto modal=Box("modal",250,20,220,300);modal["modal"]["initialFocus"]="safe";modal["modal"]["back"]="close";
	auto scroll=Box("modal-scroll",0,0,200,100),modalContent=Box("modal-content",0,0,180,360);scroll["properties"]["overflow"]=Typed("keyword","auto");
	modalContent["children"].append(Button(source,"safe",5));modalContent["children"].append(Button(source,"alternative",50));modalContent["children"].append(Button(source,"modal-lower",280));scroll["children"].append(modalContent);modal["children"].append(scroll);
	auto nested=Box("nested-modal",0,130,160,80);nested["modal"]["initialFocus"]="nested-safe";nested["modal"]["back"]="closeNested";
	nested["children"].append(Button(source,"nested-safe",5));modal["children"].append(nested);root["children"].append(modal);
	auto other=Box("other-modal",480,20,150,100);other["modal"]["initialFocus"]="other-safe";other["children"].append(Button(source,"other-safe",5));root["children"].append(other);
	source["root"]=root;
	Bind(source,"modal","display",Select(Ref("open"),"block",Select(Ref("hostOpen"),"block","none")));
	Bind(source,"nested-modal","display",Select(Ref("nested"),"block","none"));Bind(source,"other-modal","display",Select(Ref("other"),"block","none"));
	Bind(source,"safe","enabled",Ref("safeEnabled"));Bind(source,"alternative","enabled",Ref("alternativeEnabled"));
	Bind(source,"modal-lower","enabled",Ref("alternativeEnabled"));
	// The opener is disabled by the same state transaction that opens the modal.
	Bind(source,"opener","enabled",Select(Ref("open"),false,true));
	source["aliases"]["other::visible"]["node"]="other-modal";source["aliases"]["other::visible"]["property"]="visible";source["aliases"]["other::visible"]["shown"]="block";
	return source;
}
struct TestHost final:Host {
	bool hostOpen=false; unsigned errors=0,draws=0;std::uint64_t frame=1;Runtime* observing=nullptr;std::string firstDrawFocus;
	bool ReadFile(const std::string&,std::string&)override{return false;}
	std::string Translate(const std::string& s)override{return s;}
	bool ReadCVar(const std::string& name,size_t type,StateValue& value)override{if(name!="modal_host"||type!=1)return false;value=hostOpen;return true;}
	void Log(bool error,const std::string& message)override{if(error){++errors;std::fprintf(stderr,"Runtime: %s\n",message.c_str());}}
	std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=1;return 1;}
	void Draw(const std::vector<Vertex>&,const std::vector<int>&,std::uintptr_t)override{if(draws++==0 && observing)firstDrawFocus=observing->FocusedControl();}
	std::uint64_t RenderFrame()const override{return frame;}
	bool BeginLayer(std::uint32_t,int,int)override{return true;}
	void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}
	void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{}
	void EndLayer(std::uint32_t)override{}
	FontMetrics GetFontMetrics(const std::string&,int s)override{return {s*.8f,s*.2f,s*1.2f,s*.5f};}
	Glyph GetGlyph(const std::string&,int s,std::uint32_t)override{return {s*.5f,0,-s*.8f,s*.5f,float(s),0,0,1,1,"font"};}
};
struct View {
	TestHost& host;Runtime runtime;Viewport viewport;double now=1;
	explicit View(TestHost& h,const Json::Value& source=Source()):host(h),runtime(h) {
		viewport.width=640;viewport.height=400;std::vector<Diagnostic> errors;const bool loaded=runtime.LoadDocument(Text(source),"modal.q4ui",errors);
		for(const auto& error:errors)std::fprintf(stderr,"%s: %s\n",error.pointer.c_str(),error.message.c_str());Check(loaded,"canonical modal fixture loads");Frame();
	}
	void Frame(){host.draws=0;host.firstDrawFocus.clear();host.observing=&runtime;++host.frame;now+=.02;runtime.Frame(viewport,now);host.observing=nullptr;}
	void Set(const StateValues& values){std::string error;Check(runtime.SetState(values,error,now)&&error.empty(),"modal state update succeeds");}
	void Key(MenuInput key){runtime.MenuAction(key,true,now);runtime.MenuAction(key,false,now);}
	std::string Save(){std::string value,error;Check(runtime.SaveSnapshot(value,error,now)&&error.empty(),"modal snapshot saves");return value;}
	void Restore(const std::string& snapshot){std::string error;Check(runtime.RestoreSnapshot(snapshot,error,now)&&error.empty(),"modal snapshot restores");}
	Bounds BoundsOf(const char* id){Bounds result;Check(runtime.GetBounds(id,result),"node bounds available");return result;}
};
static void Schema() {
	Document document;std::vector<Diagnostic> diagnostics;const auto source=Source();const bool valid=document.Load(Text(source),diagnostics);
	for(const auto& error:diagnostics)std::fprintf(stderr,"%s: %s\n",error.pointer.c_str(),error.message.c_str());Check(valid,"modal metadata compiles");
	Check(document.Model().FindNode("modal")->modal->backEvent=="close","modal event is canonical");
	auto rejects=[&](const std::function<void(Json::Value&)>& mutate){auto bad=source;mutate(bad);Check(!document.Load(Text(bad),diagnostics)&&!diagnostics.empty(),"malformed modal rejected transactionally");Check(document.Model().id=="modal-runtime","failed compile keeps live model");};
	rejects([](auto& s){s["root"]["children"][1]["modal"]["initialFocus"]="opener";});
	rejects([](auto& s){s["root"]["children"][1]["modal"]["initialFocus"]="nested-safe";});
	rejects([](auto& s){s["root"]["children"][1]["modal"]["initialFocus"]="missing";});
	rejects([](auto& s){s["root"]["children"][1]["modal"]["back"]="missing";});
	rejects([](auto& s){s["root"]["children"][1]["modal"]["unknown"]=true;});
	rejects([](auto& s){s["root"]["children"][1]["modal"].removeMember("initialFocus");});
	rejects([](auto& s){s["root"]["children"][0]["children"][0]["children"][0]["modal"]["initialFocus"]="opener";});
	rejects([](auto& s){s["state"]["open"]["initial"]=true;s["state"]["other"]["initial"]=true;});
	auto initial=source;initial["state"]["open"]["initial"]=true;initial["state"]["nested"]["initial"]=true;
	Check(document.Load(Text(initial),diagnostics),"initial nested modal chain accepted");
}
static void FocusAndActions(TestHost& host) {
	View view(host);Check(view.runtime.FocusControl("opener",view.now),"opener focused");
	view.runtime.MenuAction(MenuInput::Accept,true,view.now);view.Set({{"open",true}});
	Check(view.runtime.FocusedControl().empty(),"opening awaits fresh layout rather than old hidden bounds");view.Frame();
	Check(view.runtime.FocusedControl()=="safe","first visible frame selects authored safe control");
	Check(host.draws>0&&host.firstDrawFocus=="safe","safe focus is committed before the first visible draw callback");
	view.runtime.MenuAction(MenuInput::Accept,false,view.now);Check(view.runtime.TakeActions().empty(),"held opener Accept cannot activate first modal control");
	Check(!view.runtime.FocusControl("opener",view.now),"background focus cannot escape modal");
	view.Key(MenuInput::Accept);auto actions=view.runtime.TakeActions();Check(actions.size()==1&&actions[0].node=="safe","fresh Accept uses safe default");
	const auto old=actions[0];Check(view.runtime.CanDispatchControlAction(old,view.now),"current Activate has valid scope provenance");
	view.Set({{"open",false}});view.Frame();Check(view.runtime.FocusedControl()=="opener","close restores opener captured before enabled invalidation");
	view.Set({{"open",true}});view.Frame();Check(!view.runtime.CanDispatchControlAction(old,view.now),"same-root reopen rejects already taken Activate");
	view.Key(MenuInput::Back);actions=view.runtime.TakeActions();Check(actions.size()==1&&actions[0].event=="close"&&view.runtime.CanDispatchModalBack(actions[0],view.now),"authored Back emits exactly one valid event record");
	Check(!view.runtime.PopModal(view.now),"manual Pop cannot hide active authored scope");
	Runtime::EventEffects effects;std::string error;Check(view.runtime.RunEvent(actions[0].event,view.now,effects,error),"authored Back executes ordinary transactional event");view.Frame();
	Check(view.runtime.FocusedControl()=="opener"&&!view.runtime.CanDispatchModalBack(actions[0],view.now),"Back event closes and retires its provenance");
	view.Set({{"open",true},{"safeEnabled",false}});view.Frame();Check(view.runtime.FocusedControl().empty(),"all-disabled modal retains empty safe focus");
	view.Key(MenuInput::Accept);Check(view.runtime.TakeActions().empty(),"empty modal cannot accept a background action");
	view.Set({{"safeEnabled",true}});view.Frame();Check(view.runtime.FocusedControl()=="safe","newly available default gets focus");
	view.Set({{"alternativeEnabled",true}});view.Frame();Check(view.runtime.FocusedControl()=="safe","later alternate availability does not steal focus");
	view.viewport.width=0;view.Frame();Check(view.runtime.FocusedControl().empty(),"invalid viewport has no active stale focus");
	view.viewport.width=640;view.Frame();Check(view.runtime.FocusedControl()=="safe","viewport recovery preserves Revert-like safe selection rather than earlier alternate");
	Check(view.runtime.FocusControl("alternative",view.now),"choose alternative before nesting");view.Set({{"nested",true}});view.Frame();
	Check(view.runtime.FocusedControl()=="nested-safe","nested modal default focused");view.Set({{"nested",false}});view.Frame();
	Check(view.runtime.FocusedControl()=="alternative","nested close restores exact previous modal selection");
}
static void TransactionsAndWheel(TestHost& host) {
	View view(host);Check(view.runtime.FocusControl("opener",view.now),"transaction opener focused");view.Set({{"open",true}});view.Frame();
	const auto before=view.Save();const auto revision=view.runtime.StateRevision();std::string error;
	Check(!view.runtime.SetState({{"other",true}},error,view.now+100)&&!error.empty(),"disjoint state rejected");Check(view.runtime.StateRevision()==revision&&view.Save()==before,"rejected disjoint state leaves clock/state/focus untouched");
	Runtime::EventEffects effects;effects.stateChanges["sentinel"]=true;
	Check(!view.runtime.RunEvent("conflict",view.now+100,effects,error)&&effects.stateChanges.contains("sentinel"),"disjoint event output and state atomic");
	Check(!view.runtime.SetPresentationAlias("other::visible","1",true,error),"disjoint explicit presentation override rejected");Check(view.Save()==before,"failed presentation/event preserve snapshot");
	const auto background=view.BoundsOf("background-lower").y;view.runtime.PointerMove(50,50,view.now);view.runtime.PointerWheel(1,view.now);view.Frame();
	Check(view.BoundsOf("background-lower").y==background,"wheel outside small modal cannot scroll background");
	const auto lower=view.BoundsOf("modal-lower").y;view.runtime.PointerMove(280,50,view.now);view.runtime.PointerWheel(1,view.now);view.Frame();
	Check(view.BoundsOf("modal-lower").y<lower,"wheel inside modal scrolls its authored body");
	view.Set({{"open",false}});view.Frame();view.runtime.PointerMove(50,50,view.now);view.runtime.PointerWheel(1,view.now);view.Frame();
	Check(view.BoundsOf("background-lower").y<background,"background wheel resumes after modal closes");
}
static void InitialLayout(TestHost& host) {
	auto source=Source();source["state"]["open"]["initial"]=true;
	source["root"]["children"][1]["children"][0]["children"][0]["children"][0]["properties"]["top"]=Typed("length",280,"dp");
	{
		View view(host,source);const auto safe=view.BoundsOf("safe"),scroll=view.BoundsOf("modal-scroll");
		Check(view.runtime.FocusedControl()=="safe"&&host.firstDrawFocus=="safe","initial visible modal focuses before any draw");
		Check(safe.y>=scroll.y&&safe.y+safe.height<=scroll.y+scroll.height,"initial offscreen default is revealed before first visible frame");
	}
	source=Source();source["state"]["open"]["initial"]=true;source["state"]["safeEnabled"]["initial"]=false;source["state"]["alternativeEnabled"]["initial"]=true;
	View view(host,source);Check(view.runtime.FocusedControl()=="alternative","initial disabled default falls back within current modal");
	view.Set({{"safeEnabled",true}});view.Frame();Check(view.runtime.FocusedControl()=="alternative","newly enabled default does not steal an established fallback selection");
}
static void Snapshots(TestHost& host) {
	View view(host);Check(view.runtime.FocusControl("opener",view.now),"snapshot opener focused");view.Set({{"open",true}});
	const auto pending=view.Save();Check(Parse(pending)["version"]==4&&Parse(pending)["interaction"]["focusPending"].asBool(),"v4 records pending first-layout focus");
	view.Restore(pending);Check(view.runtime.FocusedControl().empty(),"restore does not select using stale layout");view.Frame();Check(view.runtime.FocusedControl()=="safe","first restored frame resolves pending safe focus");
	view.Set({{"alternativeEnabled",true}});view.Frame();Check(view.runtime.FocusControl("alternative",view.now),"non-default snapshot selection");
	const auto selected=view.Save();view.Restore(selected);view.Frame();Check(view.runtime.FocusedControl()=="alternative","resource-style restore preserves exact selection");
	view.Key(MenuInput::Accept);const auto activation=view.runtime.TakeActions();view.Key(MenuInput::Back);const auto back=view.runtime.TakeActions();
	Check(activation.size()==1&&back.size()==1&&view.runtime.CanDispatchControlAction(activation[0],view.now)&&view.runtime.CanDispatchModalBack(back[0],view.now),"taken records valid before failed restore");
	const auto stable=view.Save();auto corrupt=Parse(stable);corrupt["interaction"]["modals"]=Json::Value(Json::arrayValue);std::string error;
	Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now+100),"missing saved modal chain rejected");Check(view.Save()==stable,"corrupt scope rollback preserves complete instance");
	Check(view.runtime.CanDispatchControlAction(activation[0],view.now)&&view.runtime.CanDispatchModalBack(back[0],view.now),"failed restore leaves valid in-flight action ownership untouched");
	corrupt=Parse(stable);corrupt["interaction"]["modals"][0]["authored"]=false;Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now),"forged manual modal ownership rejected");
	corrupt=Parse(stable);corrupt["hostSources"]["unknown"]=false;Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now),"unknown historical source rejected");
	corrupt=Parse(stable);corrupt["version"]=3;Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now),"modal snapshot cannot downgrade to legacy semantics");
	corrupt=Parse(stable);corrupt["document"]["path"]="other.q4ui";Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now),"modal snapshot exact path identity is required");
	corrupt=Parse(stable);corrupt["document"]["source"]="{}";Check(!view.runtime.RestoreSnapshot(Text(corrupt),error,view.now),"modal snapshot exact source identity is required");
	view.Restore(stable);view.Frame();Check(!view.runtime.CanDispatchControlAction(activation[0],view.now)&&!view.runtime.CanDispatchModalBack(back[0],view.now),"successful restore rejects both taken Activate and Back from prior lifetime");
	view.Set({{"open",false}});const auto closing=view.Save();view.Restore(closing);view.Frame();Check(view.runtime.FocusedControl()=="opener","snapshot closing before layout preserves return focus");
	// Current host visibility wins, while the historical table still validates.
	host.hostOpen=true;view.Frame();Check(view.runtime.FocusedControl()=="safe","host-driven modal opens safely");const auto historical=view.Save();
	host.hostOpen=false;view.Restore(historical);view.Frame();Check(view.runtime.FocusedControl()=="opener","fresh host hides saved modal and restores saved opener");
	view.Restore(closing);view.Frame();host.hostOpen=true;view.Restore(closing);view.Frame();Check(view.runtime.FocusedControl()=="safe","fresh host can introduce modal during restore");host.hostOpen=false;
	View peer(host);Check(peer.runtime.FocusedControl().empty(),"independent instance has no borrowed modal focus");
	peer.Restore(peer.Save());peer.Frame();Check(peer.runtime.FocusedControl().empty(),"snapshot preserves intentionally empty background focus");
	view.runtime.Shutdown();std::vector<Diagnostic> diagnostics;Check(view.runtime.LoadDocument(Text(Source()),"modal.q4ui",diagnostics),"resources recreated");view.Restore(selected);view.Frame();Check(view.runtime.FocusedControl()=="alternative","shutdown/reload snapshot preserves focus with fresh bounds");
}
int main(){Schema();TestHost host;FocusAndActions(host);TransactionsAndWheel(host);InitialLayout(host);Snapshots(host);Check(host.errors==0,"valid modal runtime paths log no errors");std::printf("Modal Runtime: %u checks passed\n",checks);return 0;}
