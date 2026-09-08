// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Document.h"
#include <json/json.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>
#include <string_view>

namespace openq4::ui {
namespace {
constexpr size_t MaxSourceBytes = 16 * 1024 * 1024;
std::string Number(double value) {
	char buffer[64];
	const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 10);
	return std::string(buffer, result.ptr);
}
bool Identifier(const std::string& text) {
	if (text.empty() || text.size() > 128) return false;
	for (unsigned char c : text)
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
	return true;
}
std::string PointerPart(const std::string& text) {
	std::string result;
	for (char c : text) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1,c);
	return result;
}
void Diagnose(std::vector<Diagnostic>& output, const std::string& source,
	const std::string& pointer, const std::string& message, size_t offset) {
	Diagnostic d{pointer, message, std::min(offset, source.size()), 1, 1};
	for (size_t i = 0; i < d.byte; ++i) {
		if (source[i] == '\n') { ++d.line; d.column = 1; } else ++d.column;
	}
	output.push_back(std::move(d));
}
bool Utf8(const std::string& text, size_t& bad) {
	for (size_t i = 0; i < text.size();) {
		bad = i;
		const unsigned char c = text[i++];
		if (c == 0) return false;
		if (c < 0x80) continue;
		unsigned length = c >= 0xf0 && c <= 0xf4 ? 3 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xc2 && c <= 0xdf ? 1 : 0;
		if (!length || i + length > text.size()) return false;
		unsigned cp = c & (0x7f >> (length + 1));
		for (unsigned n = 0; n < length; ++n) {
			const unsigned char next = text[i++];
			if ((next & 0xc0) != 0x80) return false;
			cp = (cp << 6) | (next & 0x3f);
		}
		if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
			(length == 2 && cp < 0x800) || (length == 3 && cp < 0x10000)) return false;
	}
	return true;
}
// JsonCpp 1.9.6 is permissive about numeric spellings, raw string controls
// and surrogate pairs. Enforce JSON's lexical forms before its DOM parser.
bool LexicalForms(const std::string& source, size_t& bad) {
	bool inString = false;
	auto hex4 = [&](size_t at) -> int {
		if (at + 4 > source.size()) return -1;
		int value = 0;
		for (size_t j = at; j < at + 4; ++j) {
			const char c = source[j];
			int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
			if (digit < 0) return -1;
			value = value * 16 + digit;
		}
		return value;
	};
	for (size_t i = 0; i < source.size(); ++i) {
		if (!inString && source[i] == '/' && i + 1 < source.size()) {
			if (source[i+1] == '/') { while (i < source.size() && source[i] != '\n') ++i; continue; }
			if (source[i+1] == '*') {
				i += 2;
				while (i + 1 < source.size() && !(source[i] == '*' && source[i+1] == '/')) ++i;
				++i; continue;
			}
		}
		bad = i;
		if (!inString && (source[i] == '-' || (source[i] >= '0' && source[i] <= '9'))) {
			size_t p = i;
			auto digit = [&]() { return p < source.size() && source[p] >= '0' && source[p] <= '9'; };
			if (source[p] == '-') ++p;
			if (!digit()) return false;
			if (source[p] == '0') ++p; else while (digit()) ++p;
			if (p < source.size() && source[p] == '.') { ++p; if (!digit()) return false; while (digit()) ++p; }
			if (p < source.size() && (source[p] == 'e' || source[p] == 'E')) {
				++p;
				if (p < source.size() && (source[p] == '+' || source[p] == '-')) ++p;
				if (!digit()) return false;
				while (digit()) ++p;
			}
			if (p < source.size() && std::string_view(",]} \t\r\n/").find(source[p]) == std::string_view::npos) return false;
			i = p-1; continue;
		}
		if (inString && static_cast<unsigned char>(source[i]) < 0x20) return false;
		if (source[i] == '"') { inString = !inString; continue; }
		if (!inString || source[i] != '\\' || i + 1 >= source.size()) continue;
		bad = i;
		if (source[++i] != 'u') continue;
		const int cp = hex4(i + 1);
		if (cp < 0) continue; // The JSON parser reports malformed hex escapes.
		if (cp >= 0xdc00 && cp <= 0xdfff) return false;
		if (cp >= 0xd800 && cp <= 0xdbff) {
			if (i + 6 >= source.size() || source[i+5] != '\\' || source[i+6] != 'u') return false;
			const int low = hex4(i + 7);
			if (low < 0xdc00 || low > 0xdfff) return false;
			i += 6;
		}
		i += 4;
	}
	return true;
}
bool Parse(const std::string& source, Json::Value& root, std::vector<Diagnostic>& diagnostics) {
	if (source.size() > MaxSourceBytes) {
		Diagnose(diagnostics,source,"","Document exceeds the 16 MiB source limit",0); return false;
	}
	size_t bad = 0;
	if (!Utf8(source,bad) || !LexicalForms(source,bad)) {
		Diagnose(diagnostics,source,"","Invalid UTF-8, JSON number, string control character or Unicode escape; NUL is unsupported",bad); return false;
	}
	Json::CharReaderBuilder builder;
	builder["collectComments"] = true;
	builder["allowComments"] = true;
	builder["allowTrailingCommas"] = false;
	builder["strictRoot"] = false;
	builder["failIfExtra"] = true;
	builder["rejectDupKeys"] = true;
	builder["allowSpecialFloats"] = false;
	builder["stackLimit"] = 128;
	builder["skipBom"] = false;
	// Upstream skipBom moves its offset origin forward by three bytes. Parse
	// equivalent whitespace instead so every span still addresses original bytes.
	std::string paddedBom;
	const char* input = source.data();
	if (source.starts_with("\xef\xbb\xbf")) { paddedBom = source; paddedBom.replace(0,3,"   "); input = paddedBom.data(); }
	try {
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		if (reader->parse(input,input+source.size(),&root,nullptr)) return true;
		for (const auto& error : reader->getStructuredErrors())
			Diagnose(diagnostics,source,"",error.message,static_cast<size_t>(std::max<ptrdiff_t>(0,error.offset_start)));
	} catch (const std::exception& e) { Diagnose(diagnostics,source,"",e.what(),0); }
	return false;
}
struct ValidationError {};
std::vector<std::string> ExpandedProperties(const std::string& name) {
	static const std::map<std::string,std::vector<std::string>> shorthands = {
		{"margin",{"margin-top","margin-right","margin-bottom","margin-left"}},
		{"padding",{"padding-top","padding-right","padding-bottom","padding-left"}},
		{"border-width",{"border-top-width","border-right-width","border-bottom-width","border-left-width"}},
		{"border-color",{"border-top-color","border-right-color","border-bottom-color","border-left-color"}}};
	const auto found = shorthands.find(name);
	return found == shorthands.end() ? std::vector<std::string>{name} : found->second;
}
class Validator {
public:
	Validator(const std::string& text, std::vector<Diagnostic>& errors) : source(text), diagnostics(errors) {}
	DocumentModel Read(const Json::Value& root) {
		FiniteTree(root,"");
		Fields(root,"",{"format","version","id","tokens","root","timelines","editor","extensions"});
		Require(root["format"] == "openq4-ui",root,"/format","Expected format 'openq4-ui'");
		Require(root["version"].isUInt() && root["version"].asUInt() == 1,root["version"],"/version","Unsupported document version; expected 1");
		model.id = Id(root["id"],"/id");
		if (root.isMember("editor")) Require(root["editor"].isObject(),root["editor"],"/editor","Editor metadata must be an object");
		if (root.isMember("tokens")) {
			Require(root["tokens"].isObject(),root["tokens"],"/tokens","Expected a token object");
			for (const auto& name : root["tokens"].getMemberNames()) {
				Require(Identifier(name),root["tokens"][name],"/tokens","Invalid token ID");
				model.tokens[name] = Typed(root["tokens"][name],"/tokens/"+PointerPart(name),false);
			}
		}
		model.root = ReadNode(root["root"],"/root",0);
		if (root.isMember("timelines")) {
			Require(root["timelines"].isArray(),root["timelines"],"/timelines","Expected a timeline array");
			std::set<std::string> ids;
			for (Json::ArrayIndex i = 0; i < root["timelines"].size(); ++i) {
				auto timeline = ReadTimeline(root["timelines"][i],"/timelines/"+std::to_string(i));
				Require(ids.insert(timeline.id).second,root["timelines"][i],"/timelines/"+std::to_string(i),"Duplicate timeline ID");
				model.timelines.push_back(std::move(timeline));
			}
		}
		return std::move(model);
	}
private:
	void Require(bool condition, const Json::Value& at, const std::string& path, const std::string& message) {
		if (condition) return;
		Diagnose(diagnostics,source,path,message,static_cast<size_t>(std::max<ptrdiff_t>(0,at.getOffsetStart())));
		throw ValidationError{};
	}
	void Fields(const Json::Value& value, const std::string& path, std::initializer_list<std::string_view> allowed) {
		Require(value.isObject(),value,path,"Expected an object");
		for (const auto& name : value.getMemberNames())
			Require(std::find(allowed.begin(),allowed.end(),name) != allowed.end(),value[name],path+"/"+PointerPart(name),"Unknown field; extension data belongs in 'extensions'");
		if (value.isMember("extensions")) Require(value["extensions"].isObject(),value["extensions"],path+"/extensions","Extensions must be an object");
	}
	void FiniteTree(const Json::Value& v, const std::string& path) {
		if (v.isNumeric()) Require(std::isfinite(v.asDouble()),v,path,"Non-finite numbers are not supported");
		size_t bad = 0;
		if (v.isString()) Require(Utf8(v.asString(),bad),v,path,"Invalid Unicode or NUL in string");
		if (v.isObject()) for (const auto& name : v.getMemberNames()) {
			Require(Utf8(name,bad),v,path,"Invalid Unicode or NUL in object key");
			FiniteTree(v[name],path+"/"+PointerPart(name));
		}
		if (v.isArray()) for (Json::ArrayIndex i = 0; i < v.size(); ++i) FiniteTree(v[i],path+"/"+std::to_string(i));
	}
	std::string Id(const Json::Value& value, const std::string& path) {
		Require(value.isString() && Identifier(value.asString()),value,path,"Expected a stable ID (1..128 ASCII letters, digits, '.', '_' or '-')");
		return value.asString();
	}
	double Numeric(const Json::Value& value, const std::string& path, double low, double high) {
		Require(value.isNumeric(),value,path,"Expected a number");
		const double result = value.asDouble();
		Require(std::isfinite(result) && result >= low && result <= high,value,path,"Number outside supported range ["+Number(low)+", "+Number(high)+"]");
		return result;
	}
	Value Typed(const Json::Value& value, const std::string& path, bool allowToken = true) {
		Require(value.isObject() && value["type"].isString(),value,path,"Expected a typed value object");
		const auto type = value["type"].asString();
		Value result;
		if (type == "token") {
			Fields(value,path,{"type","value","extensions"});
			const auto id = Id(value["value"],path+"/value");
			Require(allowToken,value,path,"Tokens must contain a concrete value, not another token reference");
			auto token = model.tokens.find(id);
			Require(token != model.tokens.end(),value,path,"Unknown design token '"+id+"'");
			return token->second;
		}
		if (type == "length" || type == "transform") {
			Fields(value,path,{"type","value","unit","extensions"});
			Require(value["unit"].isString(),value,path+"/unit","Length/transform requires an explicit unit");
			result.unit = value["unit"].asString();
			Require(result.unit == "dp" || result.unit == "px" || (type == "length" && result.unit == "%"),value["unit"],path+"/unit","Expected dp, px or a layout percentage; transforms require dp or px");
		} else Fields(value,path,{"type","value","extensions"});
		if (type == "number" || type == "length") {
			result.type = type == "number" ? ValueType::Number : ValueType::Length;
			result.data[0] = Numeric(value["value"],path+"/value",-1000000,1000000);
		} else if (type == "color" || type == "transform") {
			result.type = type == "color" ? ValueType::Colour : ValueType::Transform;
			const unsigned count = type == "color" ? 4 : 5;
			Require(value["value"].isArray() && value["value"].size() == count,value["value"],path+"/value","Incorrect component count");
			for (unsigned i = 0; i < count; ++i) result.data[i] = Numeric(value["value"][i],path+"/value/"+std::to_string(i),type == "color" ? 0 : -1000000,type == "color" ? 1 : 1000000);
		} else {
			Require(type == "keyword" || type == "font" || type == "text",value,path+"/type","Unsupported value type '"+type+"'");
			Require(value["value"].isString(),value["value"],path+"/value","Expected a string");
			result.text = value["value"].asString();
			result.type = type == "keyword" ? ValueType::Keyword : type == "font" ? ValueType::Font : ValueType::Text;
			if (type == "text") Require(result.text.starts_with("#str_") && Identifier(result.text.substr(1)),value["value"],path+"/value","Display text must reference a #str_ localization key");
			else Require(Identifier(result.text),value["value"],path+"/value","Expected a single keyword or font identifier");
		}
		return result;
	}
	VectorCoordinate Coordinate(const Json::Value& value, const std::string& path) {
		if (value.isNumeric()) return {0,Numeric(value,path,-1000000,1000000)};
		Fields(value,path,{"fraction","dp","extensions"});
		Require(value.isMember("fraction") || value.isMember("dp"),value,path,"Coordinate requires fraction or dp");
		return {value.isMember("fraction") ? Numeric(value["fraction"],path+"/fraction",-64,64) : 0,
			value.isMember("dp") ? Numeric(value["dp"],path+"/dp",-1000000,1000000) : 0};
	}
	PathPoint Point(const Json::Value& value, const std::string& path) {
		Require(value.isArray() && value.size() == 2,value,path,"Point requires x and y coordinates");
		return {Coordinate(value[0],path+"/0"),Coordinate(value[1],path+"/1")};
	}
	VectorColour Colour(const Json::Value& value, const std::string& path) {
		const auto colour = Typed(value,path);
		Require(colour.type == ValueType::Colour,value,path,"Paint requires a typed color or color token");
		return {colour.data[0],colour.data[1],colour.data[2],colour.data[3]};
	}
	VectorPaint Paint(const Json::Value& value, const std::string& path) {
		Require(value.isObject() && value["type"].isString(),value,path,"Expected a typed paint object");
		VectorPaint result;
		const auto type = value["type"].asString();
		if (type == "none") Fields(value,path,{"type","extensions"});
		else if (type == "solid") {
			Fields(value,path,{"type","color","extensions"});
			result.type = PaintType::Solid; result.colour = Colour(value["color"],path+"/color");
		} else if (type == "linear") {
			Fields(value,path,{"type","from","to","stops","extensions"});
			result.type = PaintType::Linear;
			result.from = Point(value["from"],path+"/from"); result.to = Point(value["to"],path+"/to");
			Require(value["stops"].isArray() && value["stops"].size() >= 2 && value["stops"].size() <= 256,value["stops"],path+"/stops","Gradient requires 2..256 stops");
			double previous = -1;
			for (Json::ArrayIndex i = 0; i < value["stops"].size(); ++i) {
				const auto& stop = value["stops"][i]; const auto p = path+"/stops/"+std::to_string(i);
				Fields(stop,p,{"at","color","extensions"});
				const double at = Numeric(stop["at"],p+"/at",0,1);
				Require(at > previous,stop["at"],p+"/at","Gradient stop positions must increase strictly");
				previous = at; result.stops.push_back({at,Colour(stop["color"],p+"/color")});
			}
			Require(result.stops.front().at == 0 && result.stops.back().at == 1,value["stops"],path+"/stops","Gradient requires stops at both zero and one");
		} else Require(false,value["type"],path+"/type","Unsupported paint type; expected none, solid or linear");
		return result;
	}
	VectorPath ReadPath(const Json::Value& value, const std::string& path) {
		Fields(value,path,{"id","commands","fillRule","fill","stroke","extensions"});
		VectorPath result;
		result.id = Id(value["id"],path+"/id");
		if (value.isMember("fillRule")) {
			Require(value["fillRule"] == "nonzero" || value["fillRule"] == "evenodd",value["fillRule"],path+"/fillRule","Expected nonzero or evenodd fill rule");
			if (value["fillRule"] == "evenodd") result.fillRule = FillRule::EvenOdd;
		}
		if (value.isMember("fill")) result.fill = Paint(value["fill"],path+"/fill");
		if (value.isMember("stroke")) {
			const auto& stroke = value["stroke"]; const auto p = path+"/stroke";
			Fields(stroke,p,{"paint","widthDp","minimumPixels","miterLimit","cap","join","extensions"});
			result.stroke.paint = Paint(stroke["paint"],p+"/paint");
			if (stroke.isMember("widthDp")) result.stroke.widthDp = Numeric(stroke["widthDp"],p+"/widthDp",0,4096);
			if (stroke.isMember("minimumPixels")) result.stroke.minimumPixels = Numeric(stroke["minimumPixels"],p+"/minimumPixels",0,4);
			if (stroke.isMember("miterLimit")) result.stroke.miterLimit = Numeric(stroke["miterLimit"],p+"/miterLimit",1,64);
			if (stroke.isMember("cap")) {
				Require(stroke["cap"] == "butt" || stroke["cap"] == "square" || stroke["cap"] == "round",stroke["cap"],p+"/cap","Expected butt, square or round cap");
				result.stroke.cap = stroke["cap"] == "round" ? StrokeCap::Round : stroke["cap"] == "square" ? StrokeCap::Square : StrokeCap::Butt;
			}
			if (stroke.isMember("join")) {
				Require(stroke["join"] == "miter" || stroke["join"] == "bevel" || stroke["join"] == "round",stroke["join"],p+"/join","Expected miter, bevel or round join");
				result.stroke.join = stroke["join"] == "round" ? StrokeJoin::Round : stroke["join"] == "bevel" ? StrokeJoin::Bevel : StrokeJoin::Miter;
			}
		}
		Require(value["commands"].isArray() && !value["commands"].empty() && value["commands"].size() <= 65536,value["commands"],path+"/commands","Path requires 1..65536 commands");
		std::set<std::string> ids;
		for (Json::ArrayIndex i = 0; i < value["commands"].size(); ++i) {
			const auto& command = value["commands"][i]; const auto p = path+"/commands/"+std::to_string(i);
			Fields(command,p,{"id","op","points","extensions"});
			PathCommand parsed; parsed.id = Id(command["id"],p+"/id");
			Require(ids.insert(parsed.id).second,command["id"],p+"/id","Duplicate path command ID");
			const std::map<std::string,std::pair<PathOperation,unsigned>> operations = {
				{"move",{PathOperation::Move,1}},{"line",{PathOperation::Line,1}},{"quadratic",{PathOperation::Quadratic,2}},
				{"cubic",{PathOperation::Cubic,3}},{"close",{PathOperation::Close,0}}};
			Require(command["op"].isString(),command["op"],p+"/op","Expected path operation");
			auto operation = operations.find(command["op"].asString());
			Require(operation != operations.end(),command["op"],p+"/op","Unsupported path operation");
			parsed.op = operation->second.first;
			Require(i != 0 || parsed.op == PathOperation::Move,command,p,"Path must begin with move");
			const unsigned points = operation->second.second;
			Require((points == 0 && !command.isMember("points")) || (command["points"].isArray() && command["points"].size() == points),command["points"],p+"/points","Incorrect point count for path operation");
			for (unsigned j = 0; j < points; ++j) parsed.points[j] = Point(command["points"][j],p+"/points/"+std::to_string(j));
			result.commands.push_back(std::move(parsed));
		}
		return result;
	}
	void Property(const std::string& name, const Value& value, const Json::Value& at, const std::string& path) {
		static const std::set<std::string> lengths = {"left","right","top","bottom","width","height","min-width","max-width","min-height","max-height","padding","padding-left","padding-right","padding-top","padding-bottom","margin","margin-left","margin-right","margin-top","margin-bottom","font-size","line-height","letter-spacing","border-width","border-left-width","border-right-width","border-top-width","border-bottom-width","row-gap","column-gap"};
		static const std::set<std::string> colours = {"color","background-color","border-color","border-left-color","border-right-color","border-top-color","border-bottom-color"};
		static const std::map<std::string,std::set<std::string>> keywords = {
			{"position",{"absolute","relative"}}, {"display",{"block","inline","inline-block","flex","none"}},
			{"overflow",{"visible","hidden","auto","scroll"}}, {"text-align",{"left","center","right"}},
			{"white-space",{"normal","pre","nowrap","pre-wrap","pre-line"}},
			{"flex-direction",{"row","row-reverse","column","column-reverse"}},
			{"flex-wrap",{"nowrap","wrap","wrap-reverse"}},
			{"justify-content",{"flex-start","flex-end","center","space-between","space-around","space-evenly"}},
			{"align-items",{"stretch","flex-start","flex-end","center","baseline"}},
			{"box-sizing",{"content-box","border-box"}}};
		bool valid = false;
		if (lengths.contains(name)) {
			valid = value.type == ValueType::Length;
			if (value.type == ValueType::Keyword && value.text == "auto") valid = name == "left" || name == "right" || name == "top" || name == "bottom" || name == "width" || name == "height" || name.starts_with("margin");
			const bool mayNegative = name == "left" || name == "right" || name == "top" || name == "bottom" || name.starts_with("margin") || name == "letter-spacing";
			if (value.type == ValueType::Length && !mayNegative && value.data[0] < 0) valid = false;
			if ((name == "font-size" || name == "line-height" || name == "letter-spacing" || name.find("border") == 0) && value.unit == "%") valid = false;
		} else if (colours.contains(name)) valid = value.type == ValueType::Colour;
		else if (auto entry = keywords.find(name); entry != keywords.end()) valid = value.type == ValueType::Keyword && entry->second.contains(value.text);
		else if (name == "opacity") valid = value.type == ValueType::Number && value.data[0] >= 0 && value.data[0] <= 1;
		else if (name == "flex-grow" || name == "flex-shrink") valid = value.type == ValueType::Number && value.data[0] >= 0;
		else if (name == "font-family") valid = value.type == ValueType::Font;
		else if (name == "text") valid = value.type == ValueType::Text;
		else if (name == "transform") valid = value.type == ValueType::Transform;
		Require(valid,at,path,"Unsupported property or invalid value for '"+name+"'");
	}
	Node ReadNode(const Json::Value& value, const std::string& path, unsigned depth) {
		Require(depth <= 48 && ++nodeCount <= 65536,value,path,"Document hierarchy exceeds the node/depth limit");
		Fields(value,path,{"id","type","properties","children","paths","extensions"});
		Node result;
		result.id = Id(value["id"],path+"/id");
		Require(nodeIds.insert(result.id).second,value["id"],path+"/id","Duplicate node ID '"+result.id+"'");
		Require(value["type"] == "group" || value["type"] == "text" || value["type"] == "vector",value["type"],path+"/type","Supported node types are group, text and vector; unsupported nodes cannot be silently rendered");
		result.type = value["type"].asString();
		if (result.type == "vector") {
			Require(value["paths"].isArray(),value["paths"],path+"/paths","Vector node requires a path array");
			std::set<std::string> pathIds;
			for (Json::ArrayIndex i = 0; i < value["paths"].size(); ++i) {
				const auto p = path+"/paths/"+std::to_string(i);
				auto shape = ReadPath(value["paths"][i],p);
				Require(pathIds.insert(shape.id).second,value["paths"][i]["id"],p+"/id","Duplicate path ID within vector node");
				result.paths.push_back(std::move(shape));
			}
		} else Require(!value.isMember("paths"),value["paths"],path+"/paths","Paths require a vector node");
		if (value.isMember("properties")) {
			Require(value["properties"].isObject(),value["properties"],path+"/properties","Expected a property object");
			for (const auto& name : value["properties"].getMemberNames()) {
				const auto propertyPath = path+"/properties/"+PointerPart(name);
				auto property = Typed(value["properties"][name],propertyPath);
				Property(name,property,value["properties"][name],propertyPath);
				Require(name != "text" || result.type == "text",value["properties"][name],propertyPath,"Text content requires a text node");
				result.properties[name] = std::move(property);
			}
			// JSON member order is not cascade order. Resolve shorthands first,
			// then explicit sides, and give motion one owner per effective leaf.
			std::map<std::string,Value> resolved;
			for (const auto& [name,property] : result.properties) {
				const auto expanded = ExpandedProperties(name);
				if (expanded.size() > 1) for (const auto& leaf : expanded) resolved[leaf] = property;
			}
			for (const auto& [name,property] : result.properties)
				if (ExpandedProperties(name).size() == 1) resolved[name] = property;
			result.properties = std::move(resolved);
		}
		if (value.isMember("children")) {
			Require(value["children"].isArray(),value["children"],path+"/children","Expected a child array");
			Require(result.type != "text" || value["children"].empty(),value["children"],path+"/children","Text nodes cannot own child nodes");
			for (Json::ArrayIndex i = 0; i < value["children"].size(); ++i) result.children.push_back(ReadNode(value["children"][i],path+"/children/"+std::to_string(i),depth+1));
		}
		return result;
	}
	Timeline ReadTimeline(const Json::Value& value, const std::string& path) {
		Fields(value,path,{"id","durationMs","iterations","essential","tracks","extensions"});
		Timeline timeline;
		timeline.id = Id(value["id"],path+"/id");
		timeline.durationMs = Numeric(value["durationMs"],path+"/durationMs",0.001,86400000);
		if (value.isMember("iterations")) {
			Require(value["iterations"].isUInt() && value["iterations"].asUInt() <= 1000000,value["iterations"],path+"/iterations","Expected an integer iteration count 0..1000000 (0 repeats until cancelled)");
			timeline.iterations = value["iterations"].asUInt();
		}
		if (value.isMember("essential")) {
			Require(value["essential"].isBool(),value["essential"],path+"/essential","Expected a boolean");
			timeline.essential = value["essential"].asBool();
		}
		Require(value["tracks"].isArray() && !value["tracks"].empty(),value["tracks"],path+"/tracks","Timeline requires tracks");
		std::set<std::pair<std::string,std::string>> targets;
		for (Json::ArrayIndex i = 0; i < value["tracks"].size(); ++i) {
			const auto& entry = value["tracks"][i];
			const auto p = path+"/tracks/"+std::to_string(i);
			Fields(entry,p,{"node","property","keys","extensions"});
			Track track;
			track.node = Id(entry["node"],p+"/node");
			track.property = Id(entry["property"],p+"/property");
			const auto* node = model.FindNode(track.node);
			Require(node != nullptr,entry["node"],p+"/node","Timeline target node does not exist");
			const auto properties = ExpandedProperties(track.property);
			for (const auto& property : properties) {
				Require(node->properties.contains(property),entry,p,"Animated properties require an explicit base value on the target node");
				Require(targets.insert({track.node,property}).second,entry,p,"Duplicate effective property owner within a timeline");
			}
			Require(entry["keys"].isArray() && entry["keys"].size() >= 2,entry["keys"],p+"/keys","Track requires at least two keys");
			double previous = -1;
			for (Json::ArrayIndex k = 0; k < entry["keys"].size(); ++k) {
				const auto& key = entry["keys"][k];
				const auto kp = p+"/keys/"+std::to_string(k);
				Fields(key,kp,{"atMs","value","easing","extensions"});
				Keyframe frame;
				frame.atMs = Numeric(key["atMs"],kp+"/atMs",0,timeline.durationMs);
				Require(frame.atMs > previous && (k != 0 || frame.atMs == 0),key["atMs"],kp+"/atMs","Key times must increase strictly and begin at zero");
				previous = frame.atMs;
				frame.value = Typed(key["value"],kp+"/value");
				Property(track.property,frame.value,key["value"],kp+"/value");
				for (const auto& property : properties)
					Require(node->properties.at(property).CanInterpolate(frame.value),key["value"],kp+"/value","Track values must interpolate with each effective base type and unit");
				if (key.isMember("easing")) {
					Require(key["easing"].isArray() && key["easing"].size() == 4,key["easing"],kp+"/easing","Easing requires four cubic-Bezier coordinates");
					frame.easing = {Numeric(key["easing"][0],kp+"/easing/0",0,1),Numeric(key["easing"][1],kp+"/easing/1",0,1),Numeric(key["easing"][2],kp+"/easing/2",0,1),Numeric(key["easing"][3],kp+"/easing/3",0,1)};
				}
				track.keys.push_back(std::move(frame));
			}
			for (const auto& property : properties) {
				Track resolved = track;
				resolved.property = property;
				timeline.tracks.push_back(std::move(resolved));
			}
		}
		return timeline;
	}
	const std::string& source;
	std::vector<Diagnostic>& diagnostics;
	DocumentModel model;
	std::set<std::string> nodeIds;
	unsigned nodeCount = 0;
};
const Node* Find(const Node& node, const std::string& id) {
	if (node.id == id) return &node;
	for (const auto& child : node.children) if (auto* found = Find(child,id)) return found;
	return nullptr;
}
const Json::Value* Resolve(const Json::Value& root, const std::string& pointer) {
	const Json::Value* value = &root;
	if (pointer.empty()) return value;
	if (pointer[0] != '/') return nullptr;
	size_t start = 1;
	while (start <= pointer.size()) {
		const auto end = pointer.find('/',start);
		const auto part = pointer.substr(start,end == std::string::npos ? end : end-start);
		std::string key;
		for (size_t i = 0; i < part.size(); ++i) {
			if (part[i] != '~') { key += part[i]; continue; }
			if (++i >= part.size() || (part[i] != '0' && part[i] != '1')) return nullptr;
			key += part[i] == '0' ? '~' : '/';
		}
		if (value->isObject()) {
			if (!value->isMember(key)) return nullptr;
			value = &(*value)[key];
		} else if (value->isArray()) {
			unsigned index = 0;
			if (key.empty() || (key.size() > 1 && key[0] == '0')) return nullptr;
			const auto result = std::from_chars(key.data(),key.data()+key.size(),index);
			if (result.ec != std::errc{} || result.ptr != key.data()+key.size() || index >= value->size()) return nullptr;
			value = &(*value)[index];
		} else return nullptr;
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return value;
}
void MarkupNode(const Node& node, std::string& output) {
	const std::string tag = node.type == "vector" ? "q4-vector" : "div";
	output += "<"+tag+" id=\""+node.id+"\" style=\"";
	for (const auto& [name,value] : node.properties) if (name != "text") output += name+":"+value.Css()+";";
	output += "\">";
	for (const auto& child : node.children) MarkupNode(child,output);
	output += "</"+tag+">";
}
} // namespace

bool Value::CanInterpolate(const Value& other) const {
	return type == other.type && unit == other.unit && (type == ValueType::Number || type == ValueType::Length || type == ValueType::Colour || type == ValueType::Transform);
}
Value Value::Interpolate(const Value& other, double fraction) const {
	if (!CanInterpolate(other)) return fraction < 1 ? *this : other;
	Value result = *this;
	const double t = std::clamp(fraction,0.0,1.0);
	for (size_t i = 0; i < data.size(); ++i) result.data[i] = data[i] + (other.data[i]-data[i])*t;
	if (type == ValueType::Colour) for (size_t i = 0; i < 3; ++i) {
		const double premultiplied = data[i]*data[3]*(1-t)+other.data[i]*other.data[3]*t;
		result.data[i] = result.data[3] > 0 ? premultiplied/result.data[3] : 0;
	}
	return result;
}
std::string Value::Css() const {
	switch (type) {
	case ValueType::Number: return Number(data[0]);
	case ValueType::Length: return Number(data[0])+unit;
	// RmlUi's rgba() uses 0..255 for *all four* channels, unlike browser CSS.
	case ValueType::Colour: return "rgba("+Number(std::round(data[0]*255))+","+Number(std::round(data[1]*255))+","+Number(std::round(data[2]*255))+","+Number(std::round(data[3]*255))+")";
	case ValueType::Transform: return "translate("+Number(data[0])+unit+","+Number(data[1])+unit+") scale("+Number(data[2])+","+Number(data[3])+") rotate("+Number(data[4])+"deg)";
	case ValueType::Text: return {}; // Never generate source markup from display text.
	default: return text;
	}
}
double Easing::Evaluate(double fraction) const {
	const double x = std::clamp(fraction,0.0,1.0);
	if (x == 0 || x == 1 || (x1 == y1 && x2 == y2)) return x;
	auto bezier = [](double t, double p1, double p2) { const double s = 1-t; return 3*s*s*t*p1+3*s*t*t*p2+t*t*t; };
	// Bisection handles flat derivatives and all allowed monotone x curves.
	double low = 0, high = 1;
	for (int i = 0; i < 40; ++i) {
		const double mid = (low+high)*.5;
		if (bezier(mid,x1,x2) < x) low = mid; else high = mid;
	}
	return bezier((low+high)*.5,y1,y2);
}
const Node* DocumentModel::FindNode(const std::string& id) const { return Find(root,id); }
struct Document::Impl { std::string source; Json::Value root; DocumentModel model; };
Document::Document() : impl(std::make_unique<Impl>()) {}
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;
bool Document::Load(const std::string& source, std::vector<Diagnostic>& diagnostics) {
	diagnostics.clear();
	auto candidate = std::make_unique<Impl>();
	candidate->source = source;
	if (!Parse(source,candidate->root,diagnostics)) return false;
	try { candidate->model = Validator(source,diagnostics).Read(candidate->root); }
	catch (const ValidationError&) { return false; }
	impl = std::move(candidate);
	return true;
}
bool Document::ReplaceValue(const std::string& pointer, const std::string& replacement, std::vector<Diagnostic>& diagnostics) {
	diagnostics.clear();
	const auto* value = Resolve(impl->root,pointer);
	if (!value || impl->source.empty()) {
		Diagnose(diagnostics,impl->source,pointer,"JSON pointer does not resolve to an existing value",0); return false;
	}
	Json::Value fragment;
	if (!Parse(replacement,fragment,diagnostics)) return false;
	const auto start = value->getOffsetStart(), end = value->getOffsetLimit();
	if (start < 0 || end < start || static_cast<size_t>(end) > impl->source.size()) {
		Diagnose(diagnostics,impl->source,pointer,"Parser returned an invalid source span",0); return false;
	}
	// Only insert the parsed value span. A trailing line comment in a supplied
	// fragment must not swallow the containing document's comma/closing brace.
	const auto fragmentStart = static_cast<size_t>(fragment.getOffsetStart());
	const auto fragmentEnd = static_cast<size_t>(fragment.getOffsetLimit());
	std::string edited = impl->source;
	edited.replace(static_cast<size_t>(start),static_cast<size_t>(end-start),replacement.substr(fragmentStart,fragmentEnd-fragmentStart));
	return Load(edited,diagnostics);
}
const std::string& Document::Source() const { return impl->source; }
const DocumentModel& Document::Model() const { return impl->model; }
std::string Document::BuildMarkup() const {
	std::string result = "<rml><head><style>body{margin:0;width:100%;height:100%;font-family:marine;font-size:16dp;color:#fff;}div,q4-vector{display:block;}</style></head><body>";
	MarkupNode(impl->model.root,result);
	return result+"</body></rml>";
}

} // namespace openq4::ui
