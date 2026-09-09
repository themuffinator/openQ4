// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Document.h"
#include <bit>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace openq4::ui;

static void Check(bool condition, const char* message) {
	if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Same(const PresentationValue& a, const PresentationValue& b) {
	if (a.type != b.type || a.text != b.text) return false;
	for (size_t i = 0; i < a.data.size(); ++i)
		if (std::bit_cast<std::uint64_t>(a.data[i]) != std::bit_cast<std::uint64_t>(b.data[i])) return false;
	return true;
}
static PresentationValue Parse(PresentationType type, const std::string& text) {
	PresentationValue value; std::string error = "previous failure";
	Check(ParsePresentationValue(type,text,value,error),"valid presentation text parses");
	Check(error.empty() && ValidPresentationValue(value),"successful parse clears stale errors and creates a canonical value");
	return value;
}
static void Reject(PresentationType type, const std::string& text) {
	PresentationValue value; value.type = PresentationType::String; value.text = "keep previous metadata";
	const auto previous = value; std::string error;
	Check(!ParsePresentationValue(type,text,value,error) && !error.empty(),"invalid input returns a diagnostic");
	Check(Same(value,previous),"failed parsing preserves the complete previous value");
}

static void CheckAliases() {
	Check(PresentationAliasKey("Desktop::TurboMode") == "desktop::turbomode","qualified aliases fold ASCII case");
	Check(PresentationAliasKey("dpadGUI") == "dpadgui","root metadata folds ASCII case");
	Check(PresentationAliasKey("P_WEAPSWITCH::VISIBLE") == "p_weapswitch::visible","legacy presentation aliases fold both components");
	Check(PresentationAliasKey("9.root-id::foreColor_r") == "9.root-id::forecolor_r","canonical ID punctuation and leading digits are accepted");
	Check(PresentationAliasKey("gui") == "gui" && PresentationAliasKey("guiData::visible") == "guidata::visible","dictionary prefix rejection is exact");
	const std::string maximum(128,'A');
	Check(PresentationAliasKey(maximum) == std::string(128,'a'),"128-byte root aliases are accepted");
	Check(PresentationAliasKey(maximum+"::"+maximum) == std::string(128,'a')+"::"+std::string(128,'a'),"both qualified parts accept their independent limit");
	for (const auto& invalid : std::vector<std::string>{"","::","::visible","root::","root:::visible","root:visible",
		"root::child::visible","gui::skill","GUI::Skill","root::gui::skill"," root","root ","root /child", "root::foreColor[0]",
		"root/child::text","$root::text","root::text;quit","root::text\n",std::string("root\0::text",11),
		std::string("root::te\0xt",11),"caf\xc3\xa9::text",std::string(129,'a'),maximum+"::"+std::string(129,'a')}) {
		Check(PresentationAliasKey(invalid).empty(),"invalid, ambiguous and dictionary aliases are rejected");
	}
}

static void CheckConversions() {
	for (const auto& token : {"1","+1","01","1.",".1e1","1e+0"," 1\t\r\n"})
		Check(Parse(PresentationType::Number,token).data[0] == 1,"signed, decimal and exponential forms have identical values");
	Check(Parse(PresentationType::Number,"-.5").data[0] == -.5,"negative leading decimal parses");
	Check(Parse(PresentationType::Number,"+1.25e-2").data[0] == .0125,"positive sign and signed exponent parse");
	Check(Parse(PresentationType::Number,"-1e12").data[0] == -1e12,"negative magnitude boundary is inclusive");
	Check(Parse(PresentationType::Number,"1e12").data[0] == 1e12,"positive magnitude boundary is inclusive");
	for (const auto& token : {"1","1.0","+1e0","true"," \ttrue\r\n"}) {
		const auto value = Parse(PresentationType::Boolean,token);
		Check(value.data[0] == 1 && FormatPresentationValue(value) == "1","true values have numeric boolean readback");
	}
	for (const auto& token : {"0","-0","0e0","false"}) {
		const auto value = Parse(PresentationType::Boolean,token);
		Check(value.data[0] == 0 && FormatPresentationValue(value) == "0","false values have numeric boolean readback");
	}
	const auto rect = Parse(PresentationType::Vector4," -24, -88,640,958 ");
	Check(rect.data == std::array<double,4>{-24,-88,640,958},"actual settings-scroll rectangle parses in legacy logical coordinates");
	Check(FormatPresentationValue(rect) == "-24 -88 640 958","rectangles emit unitless whitespace-separated tuples");
	Check(Same(rect,Parse(PresentationType::Vector4,"-24\t-88\r\n640\v958")),"ASCII whitespace tuple form matches comma tuple form");
	const auto colour = Parse(PresentationType::Vector4,"0.125 0.5 1 0.75");
	Check(FormatPresentationValue(colour) == "0.125 0.5 1 0.75","colors emit straight numeric components without CSS encoding");
	Check(Parse(PresentationType::Vector2,"1,2").data == std::array<double,4>{1,2,0,0},"two-component values zero unused fields");
	Check(Parse(PresentationType::Vector3,"1 2 3").data == std::array<double,4>{1,2,3,0},"three-component values zero unused fields");
	for (const auto& text : {std::string{},std::string("#str_104001"),std::string(" \tmetadata; <tag> \"quoted\" $variable\n"),
		std::string("caf\xc3\xa9 \xf0\x9f\x8e\xae"),std::string(65536,'x')}) {
		const auto value = Parse(PresentationType::String,text);
		Check(value.text == text && FormatPresentationValue(value) == text,"bounded UTF-8 metadata remains exact literal data");
	}
}

static void CheckRejections() {
	for (const auto& text : std::vector<std::string>{""," ","+","-",".","+.","--1","+-1","++1","-+1",
		"nan","NaN","nan(1)","inf","infinity","-INF","1e9999","1e-9999","1000000000001","-1000000000001",
		"1e","1e+","1e-","0x1","0x1p0","1f","1px","1.2.3","1 2","1,2",",1","1,",std::string("1\0",2),"1\xc2\xa0"})
		Reject(PresentationType::Number,text);
	for (const auto& text : {"2","-1","0.5","1.0001","TRUE","False","true false","true,0"})
		Reject(PresentationType::Boolean,text);
	for (const auto& text : {"","1","1 2 3","1 2 3 4 5","1,2,3,4,5",",1,2,3,4","1,2,3,4,",
		"1,,2,3,4","1,2 3,4","1 2,3,4","1;2;3;4","1 2 3 bad","1 2 3 inf","1 2 3 1000000000001"})
		Reject(PresentationType::Vector4,text);
	Reject(PresentationType::Vector2,"1 2 3");
	Reject(PresentationType::Vector3,"1 2");
	for (const auto& text : {std::string(65537,'x'),std::string("a\0b",3),std::string("\xff"),std::string("\xc0\xaf"),
		std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe2\x82")})
		Reject(PresentationType::String,text);
	Reject(static_cast<PresentationType>(999),"1");

	PresentationValue value = Parse(PresentationType::Number,"1");
	value.text = "hidden data";
	Check(!ValidPresentationValue(value) && FormatPresentationValue(value).empty(),"numeric values reject hidden text");
	value.text.clear(); value.data[3] = 1;
	Check(!ValidPresentationValue(value),"numeric values reject nonzero unused components");
	value = Parse(PresentationType::String,"text"); value.data[0] = 1;
	Check(!ValidPresentationValue(value),"string values reject hidden numeric components");
	value = Parse(PresentationType::Boolean,"1"); value.data[0] = 2;
	Check(!ValidPresentationValue(value),"canonical booleans reject values beyond zero and one");
	value = Parse(PresentationType::Vector4,"1 2 3 4");
	for (const double number : {std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
		std::numeric_limits<double>::quiet_NaN(),1000000000001.0}) {
		value.data[3] = number;
		Check(!ValidPresentationValue(value) && FormatPresentationValue(value).empty(),"nonfinite and excessive canonical components cannot be formatted");
	}
	value = {}; value.type = static_cast<PresentationType>(999);
	Check(!ValidPresentationValue(value) && FormatPresentationValue(value).empty(),"unknown canonical value types cannot be formatted");
}

static void CheckRoundTrips() {
	std::vector<double> samples{0.0,-0.0,0.1,-0.1,1.0/3.0,1e12,-1e12,std::nextafter(1e12,0.0),
		std::numeric_limits<double>::min(),std::numeric_limits<double>::denorm_min(),
		std::nextafter(1.0,0.0),std::nextafter(1.0,2.0)};
	std::uint64_t seed = 0x754abc285e90137dULL;
	for (size_t i = 0; i < 4096; ++i) {
		seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
		const double candidate = std::bit_cast<double>(seed);
		if (std::isfinite(candidate) && std::abs(candidate) <= 1e12) samples.push_back(candidate);
	}
	for (double number : samples) {
		PresentationValue value; value.data[0] = number;
		const auto formatted = FormatPresentationValue(value);
		Check(!formatted.empty() && Same(value,Parse(PresentationType::Number,formatted)),"finite numeric formatting round-trips every bit including signed zero and subnormals");
		value.type = PresentationType::Vector4; value.data = {number,-number,1.0/7.0,-0.0};
		Check(Same(value,Parse(PresentationType::Vector4,FormatPresentationValue(value))),"vector formatting preserves every component without precision loss");
	}
}

static void CheckLocaleIndependence() {
	const char* current = std::setlocale(LC_NUMERIC,nullptr);
	const std::string saved = current ? current : "C";
	// Available names differ by OS. Every installed locale must preserve the
	// dot decimal and comma tuple separators used by documents and game callers.
	for (const char* locale : {"C","de-DE","de_DE.UTF-8","German_Germany.1252","fr_FR.UTF-8"}) {
		if (!std::setlocale(LC_NUMERIC,locale)) continue;
		Check(Parse(PresentationType::Number,"1.5").data[0] == 1.5,"numeric parsing is independent of the C locale");
		Check(FormatPresentationValue(Parse(PresentationType::Number,"1.5")) == "1.5","numeric formatting always uses a dot decimal");
		Check(Parse(PresentationType::Vector2,"1.5,2.5").data[1] == 2.5,"comma remains a tuple delimiter in comma-decimal locales");
		Reject(PresentationType::Number,"1,5");
	}
	Check(std::setlocale(LC_NUMERIC,saved.c_str()) != nullptr,"restore the test process numeric locale");
}

int main() {
	CheckAliases(); CheckConversions(); CheckRejections(); CheckRoundTrips(); CheckLocaleIndependence();
	std::puts("PASS: UI presentation values, alias keys, atomic parsing and precise round trips");
	return 0;
}
