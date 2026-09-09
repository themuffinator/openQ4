// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/State.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
using namespace openq4::ui;
static void Check(bool good, const char* message) {
	if (!good) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b)<.0001; }
static void CheckCombinedSources() {
	const std::string source = R"({"format":"openq4-ui","version":1,"id":"combined-state",
 "root":{"id":"root","type":"group","properties":{"left":{"type":"length","value":1,"unit":"dp"}}},
 "state":{"application":{"type":"number","initial":2},"host":{"type":"number","initial":1,"cvar":"host_value"}},
 "presentationVariables":{"pending":{"type":"number","initial":0,"value":{"state":"application"}}},
 "bindings":[{"id":"dependent","node":"root","property":"left","value":
  {"op":"/","args":[1,{"op":"-","args":[{"state":"application"},{"state":"host"}]}]}}]})";
	Document document; std::vector<Diagnostic> diagnostics; std::string error;
	Check(document.Load(source,diagnostics),"compile cross-owner state dependency");
	State state; Check(state.Reset(document.Model(),error),"initialize cross-owner state");
	Check(!state.Set({{"application",1.0}},error) && !state.Set({{"host",2.0}},error,true),
		"either sequential update order would expose an invalid intermediate binding");
	PresentationValue transient; transient.data[0]=99;
	Check(state.WritePresentationVariable("pending",transient,false,error),"stage a transient expression value before event entry");
	const auto variables=state.Variables(); const auto revision=state.Revision();
	Check(!state.SetCombined({{"application",1.0}},{{"host",true}},error),"combined update rejects a late host type error");
	Check(state.Variables()==variables && state.Revision()==revision && state.Presentation().variables.at("pending").pending &&
		Near(state.Presentation().variables.at("pending").value.data[0],99),"failed combined validation preserves pending expression ownership and both source groups");
	Check(state.SetCombined({{"application",1.0}},{{"host",2.0}},error),"combined update evaluates only the final valid source pair");
	Check(Near(state.Properties().at({"root","left"}).data[0],-1) && state.Revision()==revision+1 &&
		!state.Presentation().variables.at("pending").pending && Near(state.Presentation().variables.at("pending").value.data[0],1),
		"combined source commit replaces transient values in one revision");
	transient.data[0]=88;
	Check(state.WritePresentationVariable("pending",transient,true,error) &&
		state.SetCombined({{"application",2.0}},{{"host",3.0}},error),"combined refresh retains explicit expression suppression");
	Check(state.Presentation().variables.at("pending").expressionDisabled && Near(state.Presentation().variables.at("pending").value.data[0],88),
		"explicit presentation owner survives combined source changes");
	const auto committed=state.Variables(); const auto committedRevision=state.Revision();
	Check(state.SetCombined({{"application",2.0}},{{"host",3.0}},error) && state.Revision()==committedRevision,
		"unchanged clean combined sources do not advance revision");
	const StateValues sameBatch{{"host",4.0}};
	Check(!state.SetCombined(sameBatch,sameBatch,error),"aliased batch arguments cannot bypass source ownership");
	Check(!state.SetCombined({{"host",4.0}},{},error) && !state.SetCombined({},{{"application",4.0}},error),"both combined source ownership directions are enforced");
	Check(!state.SetCombined({{"application",4.0}},{{"host",4.0}},error),"combined binding failure rolls back the entire refresh");
	Check(state.Variables()==committed && state.Revision()==committedRevision && Near(state.Properties().at({"root","left"}).data[0],-1) &&
		Near(state.Presentation().variables.at("pending").value.data[0],88),"combined failures preserve variables, properties, revision and explicit owner");
}
int main(int argc, char** argv) {
	CheckCombinedSources();
	std::ifstream file(argc>1 ? argv[1] : "tools/ui/fixtures/binding-smoke.q4ui",std::ios::binary);
	Check(file.good(),"binding fixture exists");
	const std::string source((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
	Document document; std::vector<Diagnostic> diagnostics; std::string error;
	if (!document.Load(source,diagnostics)) for (const auto& d:diagnostics) std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());
	Check(diagnostics.empty(),"compile typed binding fixture");
	Check(document.Source()==source,"binding source round trip");
	State state; Check(state.Reset(document.Model(),error),"initial state evaluates");
	auto width=[&]() { return state.Properties().at({"progress-fill","width"}).data[0]; };
	Check(Near(width(),25),"initial proportional gauge");
	Check(state.Set({{"progress",75.0},{"limit",150.0},{"available",false}},error),"atomic state batch");
	Check(Near(width(),50) && !state.Enabled().at("reference-controls"),"geometry and semantic availability share state");
	Check(state.Properties().at({"reading","text"}).text=="75","numeric text");
	const auto revision=state.Revision(); const auto variables=state.Variables();
	Check(state.Set({{"progress",75.0}},error) && state.Revision()==revision,"unchanged batches keep revision");
	for (const auto& change:std::vector<StateValues>{{{"progress",88.0},{"limit",0.0}},{{"progress",true}},{{"unknown",1.0}},
		{{"scale",2.0}},{{"progress",std::numeric_limits<double>::infinity()}},{{"heading",std::string("\xc0\x80",2)}},
		{{"heading",std::string("a\0b",3)}},{{"heading",std::string(65537,'a')}}}) {
		Check(!state.Set(change,error) && !error.empty(),"invalid batch diagnosed");
		Check(state.Variables()==variables && state.Revision()==revision && Near(width(),50),"invalid batches cannot partially update presentation");
	}
	Check(state.Set({{"heading",std::string("<img src='hostile'> & player")}},error),"dynamic player text remains data");
	Check(state.Set({{"progress",900.0}},error) && Near(width(),100),"explicit clamp bounds game values");
	Check(state.Set({{"scale",1.25}},error,true),"host owns readonly source");
	Check(state.Properties().at({"scale-reading","text"}).text=="1.25","fixed decimal output");
	Check(!state.Set({{"progress",1.0}},error,true),"host cannot overwrite application state");
	auto reject=[&](const std::string& pointer,const std::string& value,const char* message) {
		Check(!document.ReplaceValue(pointer,value,diagnostics),message);
		Check(!diagnostics.empty() && document.Source()==source,"failed expression edit preserves source");
	};
	reject("/bindings/0/value","{\"state\":\"missing\"}","unknown state reference");
	reject("/bindings/0/value","{\"state\":\"shown\"}","binding type mismatch");
	reject("/bindings/0/value","{\"op\":\"+\",\"args\":[1]}","operator arity");
	reject("/bindings/0/value","{\"op\":\"exec\",\"args\":[]}","no executable expression escape");
	reject("/bindings/0/value","{\"op\":\"&&\",\"args\":[1,2]}","no implicit boolean coercion");
	reject("/bindings/0/value","{\"op\":\"/\",\"args\":[1,0]}","invalid initial expression");
	reject("/bindings/0/value","-1","invalid property range");
	reject("/bindings/1/value","\"Hardcoded label\"","localized literal display text");
	reject("/bindings/1/value","{\"op\":\"numberText\",\"args\":[1],\"decimals\":9}","bounded number formatting");
	reject("/bindings/3/node","\"reference-title\"","availability requires control");
	reject("/bindings/4/value","\"javascript:bad\"","keyword validation");
	reject("/bindings/0/node","\"missing\"","binding target exists");
	reject("/bindings/0/id","\"progress-number\"","binding IDs unique");
	reject("/bindings/2/node","\"reading\"","one binding owner per property");
	reject("/bindings/0","{\"id\":\"x\",\"node\":\"reference-controls\",\"property\":\"opacity\",\"value\":1}","timeline and binding ownership collision");
	Check(document.ReplaceValue("/bindings/0/value","{\"op\":\"select\",\"args\":[true,50,{\"op\":\"/\",\"args\":[1,0]}]}",diagnostics),"unselected branch cannot execute");
	Check(state.Reset(document.Model(),error) && Near(width(),50),"lazy selection");
	Check(document.Load(source,diagnostics),"restore source");
	Check(document.ReplaceValue("/bindings/3/value","{\"op\":\"||\",\"args\":[true,{\"op\":\">\",\"args\":[{\"op\":\"/\",\"args\":[1,0]},0]}]}",diagnostics),"boolean short circuit");
	Check(state.Reset(document.Model(),error) && state.Enabled().at("reference-controls"),"short circuit boolean result");
	Check(document.Load(source,diagnostics),"restore for edit roundtrip");
	Check(document.ReplaceValue("/bindings/0/value/args/0/args/1/args/0/state","\"limit\"",diagnostics),"edit actual expression source");
	Check(state.Reset(document.Model(),error) && Near(width(),100),"edited expression drives shared evaluator");
	Check(document.Load(source,diagnostics),"restore for component binding");
	Check(document.ReplaceValue("/bindings/0","{\"id\":\"tint\",\"node\":\"screen\",\"property\":\"background-color\",\"value\":[{\"op\":\"/\",\"args\":[{\"state\":\"progress\"},100]},0,0,1]}",diagnostics),"compile four-component color binding");
	Check(state.Reset(document.Model(),error) && state.Set({{"progress",50.0}},error),"update color components");
	Check(Near(state.Properties().at({"screen","background-color"}).data[0],.5),"component expression result");
	Check(!state.Set({{"progress",101.0}},error),"dynamic color stays within supported range");
	Check(!document.ReplaceValue("/bindings/0/value","[1,0,0]",diagnostics),"component arity checked");
	Check(document.Load(source,diagnostics),"restore for transform binding");
	Check(document.ReplaceValue("/bindings/0","{\"id\":\"move\",\"node\":\"reference-panel\",\"property\":\"transform\",\"value\":[{\"state\":\"progress\"},0,1,1,0]}",diagnostics),"compile five-component transform");
	Check(state.Reset(document.Model(),error) && Near(state.Properties().at({"reference-panel","transform"}).data[0],25),"transform retains authored units and binding components");
	StateValues data{{"keep",1.0}};
	for (const auto& bad : {"[]","{\"a\":1,\"a\":2}","{\"a\":{}}","{\"a\":\"\\u0000\"}","{\"a\":1e999}"}) {
		Check(!ParseStateValues(bad,data,diagnostics) && data.size()==1 && data.contains("keep"),"invalid serialized batch preserves destination");
	}
	Check(ParseStateValues("{\"progress\":75,\"shown\":true,\"heading\":\"Player\"}",data,diagnostics) && data.size()==3 && data.at("shown").index()==1,"serialized state retains types");
	std::puts("UI state: atomic batches, ownership, source edits, lazy expressions and diagnostics passed");
}
