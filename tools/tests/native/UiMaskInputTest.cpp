// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Reuse the native host and authored widget fixtures; expectations below are
// independently chosen points/regions, never a copy of the hit-test algorithm.
#define main ValueRuntimeFixtureMain
#include "UiValueRuntimeTest.cpp"
#undef main

static void Contour(Json::Value& path, std::initializer_list<std::pair<double,double>> points) {
	unsigned index = path["commands"].size(); bool first = true;
	for (const auto& [x,y] : points) {
		Json::Value command; command["id"]="point-"+std::to_string(index++);
		command["op"]=first?"move":"line"; first=false;
		command["points"].append(Array({x,y})); path["commands"].append(command);
	}
	Json::Value close; close["id"]="point-"+std::to_string(index); close["op"]="close"; path["commands"].append(close);
}
static Json::Value Mask(double width, double height, bool hole=false, double alpha=1) {
	Json::Value result,path; path["id"]="coverage"; path["fillRule"]="evenodd";
	path["fill"]["type"]="solid"; path["fill"]["color"]=Typed("color",Array({.8,.1,.3,alpha}));
	Contour(path,{{0,0},{width,0},{width,height},{0,height}});
	if(hole) Contour(path,{{40,30},{120,30},{120,90},{40,90}});
	result["paths"].append(path); return result;
}
static Json::Value Button(Json::Value& source, const char* id, double x, double y, double width, double height) {
	auto node=Box(id,x,y,width,height); auto& control=node["control"];
	control["role"]="button";control["label"]="#str_label";control["action"]="hit";
	source["actions"]["hit"]["operation"]="test.hit";
	source["actions"]["hit"]["arguments"]=Json::Value(Json::objectValue);
	for(const char* state:{"default","hover","focus","pressed","disabled"})control["states"][state]=std::string(id)+"-feedback";
	PropertyTimeline(source,(std::string(id)+"-feedback").c_str(),id,"opacity",Typed("number",1),Typed("number",1),1);
	Colour(node,{.2,.3,.4,1});return node;
}
static Json::Value MaskSource() {
	Json::Value source;source["format"]="retained-ui";source["version"]=1;source["id"]="mask-input";
	auto root=Box("root",0,0,640,400);
	root["children"].append(Button(source,"behind",20,20,160,120));
	auto masked=Box("mask",20,20,160,120);masked["mask"]=Mask(160,120,true);
	auto top=Button(source,"front",0,0,160,120);
	top["children"].append(Box("label",45,35,100,30,"text"));masked["children"].append(top);
	root["children"].append(masked);source["root"]=root;return source;
}
static void ExpectHit(View& view, float x, float y, const std::string& expected) {
	view.Click(x,y);auto actions=view.runtime.TakeActions();
	if(expected.empty()) Check(actions.empty(),"masked point dispatches no action");
	else {
		if(actions.size()!=1 || actions[0].node!=expected)
			std::fprintf(stderr,"Point %.2f,%.2f expected %s, got %s\n",x,y,expected.c_str(),actions.empty()?"none":actions[0].node.c_str());
		Check(actions.size()==1 && actions[0].node==expected,"point selects exactly the visible eligible control");
	}
}
static void HolesAndDensity(TestHost& host) {
	for(float density:{1.f,1.25f,1.5f,2.f}) {
		View view(host,Text(MaskSource()));view.viewport.width=1280;view.viewport.height=800;
		view.viewport.displayScale=density;view.viewport.originX=37;view.viewport.originY=19;
		view.viewport.pixelDensityX=1.5f;view.viewport.pixelDensityY=1.25f;view.Frame();
		ExpectHit(view,30*density,35*density,"front");
		ExpectHit(view,75*density,65*density,"behind"); // Includes actual text child ink.
		ExpectHit(view,170*density,130*density,"front");
		Check(view.runtime.PushModal("mask",view.time),"masked scope can own modal input");
		ExpectHit(view,75*density,65*density,""); // The hole cannot escape modal ownership.
		Check(view.runtime.PopModal(view.time),"modal closes");
	}
}
static void EmptyAndTransparent(TestHost& host) {
	for(int kind=0;kind<3;++kind) {
		auto source=MaskSource();auto& mask=source["root"]["children"][1]["mask"];
		mask=Mask(160,120,false,0);
		if(kind==0)mask["paths"]=Json::Value(Json::arrayValue);
		if(kind==2) {
			mask=Mask(160,120);
			source["root"]["children"][1]["properties"]["transform"]=Typed("transform",Array({0,0,0,0,0}),"dp");
		}
		View view(host,Text(source));ExpectHit(view,30,35,"behind");
		Check(!view.runtime.FocusControl("front",view.time),"entirely masked subtree cannot receive semantic focus");
		view.Key(MenuInput::Next);Check(view.runtime.FocusedControl()=="behind","navigation skips empty mask");
	}
	auto source=MaskSource();auto& mask=source["root"]["children"][1]["mask"];
	mask=Mask(160,120,false,.1);View view(host,Text(source));ExpectHit(view,75,65,"front");
	Check(view.runtime.FocusControl("front",view.time),"partly transparent mask remains usable");
}
static void NestedAndTransformed(TestHost& host) {
	auto source=MaskSource();auto& parent=source["root"]["children"][1];
	parent["properties"]["transform"]=Typed("transform",Array({200,80,1,1,90}),"dp");
	auto nested=Box("nested",0,0,160,120);nested["mask"]=Mask(160,55);
	nested["children"]=parent["children"];parent["children"]=Json::Value(Json::arrayValue);parent["children"].append(nested);
	View view(host,Text(source));
	// The 160x120 parent rotates about (100,80), then translates by (200,80).
	ExpectHit(view,345,90,"front"); // Local (10,15), admitted by both masks.
	ExpectHit(view,270,90,""); // Local (10,90), rejected by nested mask.
	ExpectHit(view,320,155,""); // Local (75,40), rejected by parent hole.
}
static void MovingAndResizing(TestHost& host) {
	auto source=MaskSource();auto& parent=source["root"]["children"][1];
	parent["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
	TransformTimeline(source,"move","mask",Array({0,0,1,1,0}),Array({200,0,1,1,0}),1);
	View view(host,Text(source));ExpectHit(view,30,35,"front");view.Frame();
	Check(view.runtime.Statistics().vectorHitPathsCompiled==0,"unchanged masks reuse geometric hit mesh");
	ExpectHit(view,30,35,"front");ExpectHit(view,75,65,"behind");
	Check(view.runtime.Statistics().vectorHitPathsCompiled==0 && view.runtime.Statistics().vectorHitCacheHits>0,
		"pointer motion does not tessellate cached mask paths");
	Check(view.runtime.PlayTimeline("move",view.time),"mask motion starts");view.Frame();
	ExpectHit(view,30,35,"behind");ExpectHit(view,230,35,"front");ExpectHit(view,275,65,"");
	view.viewport.displayScale=1.5f;view.viewport.width=960;view.viewport.height=600;view.Frame();
	ExpectHit(view,345,52.5f,"front");ExpectHit(view,412.5f,97.5f,"");
}
static void PaintCoverage(TestHost& host) {
	auto source=MaskSource();auto& mask=source["root"]["children"][1]["mask"];
	mask=Mask(160,120);auto& fill=mask["paths"][0]["fill"];
	fill.removeMember("color");fill["type"]="linear";fill["from"]=Array({0,0});fill["to"]=Array({160,0});
	for(const auto& [at,alpha]:std::initializer_list<std::pair<double,double>>{{0,0},{.5,0},{1,.25}}) {
		Json::Value stop;stop["at"]=at;stop["color"]=Typed("color",Array({1,0,1,alpha}));fill["stops"].append(stop);
	}
	View gradient(host,Text(source));ExpectHit(gradient,40,40,"behind");ExpectHit(gradient,150,40,"front");
	// Separate stroke geometry admits the ring, but never its unpainted center.
	mask=Mask(160,120);auto& path=mask["paths"][0];path.removeMember("fill");
	path["stroke"]["paint"]["type"]="solid";path["stroke"]["paint"]["color"]=Typed("color",Array({0,0,0,.5}));
	path["stroke"]["widthDp"]=8;
	View stroke(host,Text(source));ExpectHit(stroke,22,40,"front");ExpectHit(stroke,75,65,"behind");
}
static void CompoundControls(TestHost& host) {
	auto source=Parse(Source());source["root"]["children"][1]["mask"]=Mask(90,50);
	View view(host,Text(source));ExpectHit(view,200,100,"");
	view.Point(75,100);view.runtime.PointerButton(true,view.time);
	Check(view.Widget("slider").preview.has_value(),"visible slider part starts capture");
	view.Point(235,100);view.runtime.PointerButton(false,view.time);
	auto actions=view.runtime.TakeActions();Check(actions.size()==1 && actions[0].node=="slider","matched captured drag may finish outside mask");

	auto scroll=Parse(ScrollSource(false));scroll["root"]["children"][0]["mask"]=Mask(80,100);
	View scroller(host,Text(scroll));const auto before=scroller.BoxOf("lower").y;
	const auto body=scroller.BoxOf("body");
	scroller.Point(body.x+140,body.y+20);scroller.runtime.PointerWheel(1,scroller.time);scroller.Frame();
	Check(Near(before,scroller.BoxOf("lower").y),"wheel cannot scroll through masked-out body");
	scroller.Point(body.x+30,body.y+20);scroller.runtime.PointerWheel(1,scroller.time);scroller.Frame();
	Check(scroller.BoxOf("lower").y<before,"visible body still scrolls");

	auto choices=Parse(Source());auto& parent=choices["root"]["children"][3];
	parent["properties"]["left"]=Typed("length",100,"dp");parent["properties"]["top"]=Typed("length",100,"dp");
	parent["properties"]["overflow"]=Typed("keyword","visible");
	auto& popup=parent["children"][0]["children"][0];popup["mask"]=Mask(80,80);
	View choice(host,Text(choices));Check(choice.runtime.FocusControl("choice",choice.time),"choice focused");choice.Key(MenuInput::Accept);choice.Frame();
	const auto popupBox=choice.BoxOf("popup");
	ExpectHit(choice,popupBox.x+140,popupBox.y+55,"");
	Check(!choice.Widget("choice").popupOpen,"masked-out popup region keeps ordinary outside-click dismissal without selecting");
	Check(choice.runtime.FocusControl("choice",choice.time),"choice refocused");choice.Key(MenuInput::Accept);choice.Frame();
	choice.Click(popupBox.x+30,popupBox.y+55);
	const auto proposed=choice.runtime.TakeActions();Check(proposed.size()==1 && proposed[0].node=="choice","visible popup option remains selectable");
}
int main() {
	TestHost host;HolesAndDensity(host);EmptyAndTransparent(host);NestedAndTransformed(host);MovingAndResizing(host);PaintCoverage(host);CompoundControls(host);
	Check(host.errors==0,"mask input fixture has no runtime errors");
	std::printf("Masked input: %u checks passed\n",checks);return 0;
}
