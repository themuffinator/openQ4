// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Document.h"
#include "src/ui/retained/Motion.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Near(double a, double b) { return std::abs(a-b) < .0001; }
static const char* Source = R"json(
// Retain this authored comment and spacing exactly.
{
 "format": "openq4-ui", "version": 1, "id": "test-document",
 "tokens": {"size": {"type":"length", "value":100, "unit":"dp"}},
 "root": {"id":"root", "type":"group", "children":[
  {"id":"panel", "type":"group", "properties":{
   "left":{"type":"length", "value":10, "unit":"dp"},
   "width":{"type":"token", "value":"size"},
   "opacity":{"type":"number", "value":0},
   "transform":{"type":"transform", "unit":"dp", "value":[0,0,1,1,0]}
  }},
  {"id":"label", "type":"text", "properties":{"text":{"type":"text", "value":"#str_test"}}}
 ]},
 "timelines":[
  {"id":"enter", "durationMs":1000, "iterations":1, "tracks":[
   {"node":"panel", "property":"left", "keys":[
    {"atMs":0, "value":{"type":"length", "value":10, "unit":"dp"}},
    {"atMs":1000, "value":{"type":"length", "value":110, "unit":"dp"}}]},
   {"node":"panel", "property":"opacity", "keys":[
    {"atMs":0, "value":{"type":"number", "value":0}},
    {"atMs":1000, "value":{"type":"number", "value":1}}]},
   {"node":"panel", "property":"transform", "keys":[
    {"atMs":0, "value":{"type":"transform", "unit":"dp", "value":[0,0,1,1,0]}},
    {"atMs":1000, "value":{"type":"transform", "unit":"dp", "value":[100,0,1,1,0]}}]}
  ]},
  {"id":"fade", "durationMs":1000, "tracks":[
   {"node":"panel", "property":"opacity", "keys":[
    {"atMs":0, "value":{"type":"number", "value":1}},
    {"atMs":1000, "value":{"type":"number", "value":0}}]}
  ]}
 ],
 "editor":{"zoom":1.25},
 "extensions": {"vendor/example~1": {"raw":"<script>never markup</script>", "unicode":"\u00e9\ud83d\ude80", "empty":[ ]}}
}
// Keep this trailer too.
)json";
int main() {
	Document document;
	std::vector<Diagnostic> errors;
	Check(document.Load(Source,errors),"parse typed commented document");
	Check(errors.empty() && document.Source() == Source,"unchanged source round trip is byte exact");
	Check(document.Model().FindNode("panel") != nullptr,"stable node lookup");
	Check(Near(document.Model().FindNode("panel")->properties.at("width").data[0],100),"resolve design token");
	Check(document.BuildMarkup().find("<script>") == std::string::npos,"extensions cannot inject markup");
	Check(document.BuildMarkup().find("#str_test") == std::string::npos,"localization goes through escaped text API");
	Check(document.ReplaceValue("/tokens/size/value","160 // supplied trailing comment",errors),"edit scalar by exact source offset");
	std::string expected = Source;
	expected.replace(expected.find("\"value\":100"),11,"\"value\":160");
	Check(document.Source() == expected,"scalar edit preserves all surrounding source bytes");
	Check(Near(document.Model().FindNode("panel")->properties.at("width").data[0],160),"token edits update resolved model");
	Check(document.ReplaceValue("/extensions/vendor~1example~01/empty","[1, /* inside replacement */ 2]",errors),"JSON pointer escaping and empty-container source spans");
	Check(document.Source().find("[1, /* inside replacement */ 2]") != std::string::npos,"replacement interior comments retained");
	Check(document.ReplaceValue("/extensions/vendor~1example~01/raw","\"changed \\\"value\\\"\"",errors),"string source span includes quotes");
	const auto beforeInvalid = document.Source();
	auto reject = [&](const std::string& pointer, const std::string& value, const char* message) {
		Check(!document.ReplaceValue(pointer,value,errors),message);
		Check(!errors.empty() && document.Source() == beforeInvalid,"failed edit preserves prior source/model and diagnoses");
	};
	reject("/version","2","reject unknown schema version");
	reject("/root/children/1/id","\"panel\"","reject duplicate stable IDs");
	reject("/root/children/0/properties/width/value","\"missing\"","reject dangling token");
	reject("/root/children/0/properties/opacity/value","2","reject invalid opacity");
	reject("/root/children/1/properties/text/value","\"Hardcoded\"","reject unlocalized display string");
	reject("/timelines/0/tracks/0/node","\"absent\"","reject dangling animation target");
	reject("/timelines/0/tracks/0/keys/1/atMs","0","reject duplicate key times");
	reject("/timelines/0/tracks/0/keys/1/value/unit","\"px\"","reject ambiguous mixed-unit interpolation");
	reject("/editor/zoom","1e999","reject infinite numbers in opaque metadata");
	reject("/editor/zoom","NaN","reject non-JSON numeric spellings");
	reject("/editor/zoom","01","reject leading-zero numbers");
	reject("/editor/zoom","1.","reject incomplete decimals");
	reject("/editor/zoom","1e+","reject incomplete exponents");
	reject("/editor","{\"zoom\":1,\"zoom\":2}","reject duplicate object keys");
	reject("/editor/zoom","1,\"escape\":2","fragment cannot escape its source span");
	reject("/root/children/01/id","\"other\"","array pointers reject leading zeros");
	reject("/extensions/vendor~2example","null","reject malformed pointer escape");
	reject("/editor","{\"bad\":\"\\ud800\\u0041\"}","reject unpaired high surrogate");
	reject("/editor","{\"bad\":\"\\udc00\"}","reject lone low surrogate");
	reject("/editor","{\"bad\":\"\\u0000\"}","reject escaped embedded NUL");
	reject("/editor","{\"bad\":\"raw\nnewline\"}","reject unescaped string control characters");
	reject("/editor","{\"bad\":\""+std::string("\xc0\x80",2)+"\"}","reject overlong UTF-8");
	Check(!document.Load("{\n  \"format\": \n}",errors),"syntax diagnostics");
	Check(errors[0].line == 3 && errors[0].byte > 0,"syntax diagnostic source position");
	Check(document.Source() == beforeInvalid,"failed whole-document load is transactional");
	Check(document.Load(Source,errors),"reload authored model");
	Check(!document.ReplaceValue("/root/type","\"image\"",errors),"unsupported features fail instead of silently disappearing");
	Check(errors[0].pointer == "/root/type" && errors[0].line > 1 && errors[0].column > 1,"semantic diagnostic pointer and source location");
	std::string windowsSource = "\xef\xbb\xbf";
	for (char c : std::string(Source)) windowsSource += c == '\n' ? "\r\n" : std::string(1,c);
	Check(document.Load(windowsSource,errors),"load UTF-8 BOM and Windows line endings");
	Check(document.ReplaceValue("/editor/zoom","2.5",errors),"BOM does not shift source edit offsets");
	windowsSource.replace(windowsSource.find("1.25"),4,"2.5");
	Check(document.Source() == windowsSource,"BOM and CRLF are byte exact after a value edit");
	Check(document.Load(Source,errors),"restore model after source encoding checks");
	const Easing ease{.16,1,.3,1};
	Check(ease.Evaluate(0) == 0 && ease.Evaluate(1) == 1 && ease.Evaluate(.5) > .9,"specified page easing and exact endpoints");
	Check(Near(Easing{0,0,0,1}.Evaluate(.125),.5),"Bezier solves x, including flat derivative");
	Value red, transparentBlue;
	red.type = transparentBlue.type = ValueType::Colour;
	red.data = {1,0,0,1}; transparentBlue.data = {0,0,1,0};
	const auto mixed = red.Interpolate(transparentBlue,.5);
	Check(Near(mixed.data[0],1) && Near(mixed.data[2],0) && Near(mixed.data[3],.5),"colour interpolation uses premultiplied alpha to avoid dark/coloured fringes");
	Motion motion;
	auto opacity = [&]() { return motion.Values().at({"panel","opacity"}).data[0]; };
	auto left = [&]() { return motion.Values().at({"panel","left"}).data[0]; };
	motion.Reset(document.Model());
	Check(motion.Play("enter",10),"play by stable timeline ID");
	motion.Advance(10.25);
	Check(Near(opacity(),.25) && Near(left(),35),"250 ms frame stall samples absolute elapsed time");
	motion.Advance(10.25+1.0/240);
	Check(Near(opacity(),.25+1.0/240),"240 Hz presentation independent of game ticks");
	motion.Advance(9); motion.Advance(std::numeric_limits<double>::quiet_NaN());
	Check(Near(opacity(),.25+1.0/240),"backwards and invalid clock samples do not reverse motion");
	motion.Advance(11);
	Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"long gap reaches exact final values");
	motion.Reset(document.Model()); motion.Play("enter",0); motion.Play("fade",.25);
	Check(Near(opacity(),.25),"interruption starts at sampled current value even without intervening frame");
	motion.Advance(.5);
	Check(Near(opacity(),.1875) && Near(left(),60),"new owner reverses opacity while other properties continue");
	motion.Cancel("enter",CancelPolicy::RestoreBase,.5);
	Check(Near(left(),10) && Near(opacity(),.1875),"cancelling older owner cannot overwrite newer owner");
	motion.Cancel("fade",CancelPolicy::Hold,.75); const double held = opacity();
	motion.Advance(100);
	Check(Near(opacity(),held),"hold cancellation stops motion at sampled value");
	motion.Reset(document.Model()); motion.Play("enter",0); motion.Pause("enter",.25);
	motion.Advance(5); Check(Near(opacity(),.25),"pause freezes presentation value");
	motion.Resume("enter",5); motion.Advance(5.25);
	Check(Near(opacity(),.5),"resume excludes paused time");
	const auto scrub = motion.Scrub("enter",750);
	Check(Near(scrub.at({"panel","opacity"}).data[0],.75) && Near(opacity(),.5),"editor scrub is a pure authored-time sample");
	motion.Reset(document.Model()); motion.SetReducedMotion(true,0); motion.Play("enter",0);
	Check(Near(left(),110),"reduced motion applies spatial endpoint immediately");
	motion.Advance(.04); Check(Near(opacity(),.5),"reduced opacity uses bounded 80 ms interpolation");
	motion.Advance(.081); Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"reduced transition finishes without loops");
	motion.SetReducedMotion(false,.1); motion.Reset(document.Model()); motion.Play("enter",0);
	motion.SetReducedMotion(true,.25); motion.Advance(.29);
	Check(Near(opacity(),.625) && Near(left(),110),"live reduced-motion change retargets without an opacity jump");
	motion.Advance(.34); Check(Near(opacity(),1),"live preference completes opacity within 80 ms");
	Check(document.ReplaceValue("/timelines/0/iterations","0",errors),"author repeating timeline");
	motion.SetReducedMotion(false,1); motion.Reset(document.Model()); motion.Play("enter",0); motion.Advance(7.25);
	Check(Near(opacity(),.25) && motion.IsPlaying("enter"),"repeat phase derives from elapsed time across multiple skipped cycles");
	motion.SetReducedMotion(true,7.25); motion.Advance(8);
	Check(Near(opacity(),1) && !motion.IsPlaying("enter"),"reduced motion terminates an existing decorative loop");
	Check(!motion.Play("unknown",9),"unknown animation leaves state intact");
	std::string shorthandSource = Source;
	shorthandSource.insert(shorthandSource.find("\"left\":{\"type\""),
		"\"padding\":{\"type\":\"length\",\"value\":10,\"unit\":\"dp\"},\"padding-left\":{\"type\":\"length\",\"value\":20,\"unit\":\"dp\"},");
	Check(document.Load(shorthandSource,errors),"load overlapping shorthand and side properties");
	const auto& padded = document.Model().FindNode("panel")->properties;
	Check(!padded.contains("padding") && Near(padded.at("padding-left").data[0],20) && Near(padded.at("padding-top").data[0],10),"explicit sides override shorthand independent of source/cascade order");
	Check(document.ReplaceValue("/timelines/0/tracks/0/property","\"padding\"",errors),"expand animated shorthand into effective property tracks");
	Check(document.ReplaceValue("/timelines/1/tracks/0",R"json({"node":"panel","property":"padding-left","keys":[
	 {"atMs":0,"value":{"type":"length","value":20,"unit":"dp"}},
	 {"atMs":1000,"value":{"type":"length","value":0,"unit":"dp"}}
	]})json",errors),"author a competing side transition");
	motion.SetReducedMotion(false,9); motion.Reset(document.Model()); motion.Play("enter",0); motion.Play("fade",.25); motion.Advance(.5);
	Check(Near(motion.Values().at({"panel","padding-left"}).data[0],31.875) && Near(motion.Values().at({"panel","padding-top"}).data[0],60),"new side owner interrupts only its effective part of a shorthand track");
	motion.Cancel("enter",CancelPolicy::RestoreBase,.5);
	Check(Near(motion.Values().at({"panel","padding-left"}).data[0],31.875),"shorthand cancellation cannot overwrite a newer side owner");
	std::puts("UI document: typed JSONC, lossless value edits, diagnostics, tokens, timeline ownership, stalls, pause, scrub and reduced motion passed");
}
