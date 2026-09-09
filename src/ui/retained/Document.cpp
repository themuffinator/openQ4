// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Document.h"
#include "State.h"
#include <json/json.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace openq4::ui {
bool ValidProperty(const std::string& name, const Value& value) {
	for (double component : value.data) if (!std::isfinite(component) || std::abs(component) > 1000000) return false;
	if (value.type == ValueType::Colour) for (size_t i = 0; i < 4; ++i) if (value.data[i] < 0 || value.data[i] > 1) return false;
	if (!ValidStateValue(StateValue(value.text))) return false;
	if (value.type == ValueType::Font) {
		if (value.text.empty() || value.text.size() > 128) return false;
		for (unsigned char c : value.text) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
	}

		static const std::set<std::string> lengths = {"left","right","top","bottom","width","height","min-width","max-width","min-height","max-height","padding","padding-left","padding-right","padding-top","padding-bottom","margin","margin-left","margin-right","margin-top","margin-bottom","font-size","line-height","letter-spacing","border-width","border-left-width","border-right-width","border-top-width","border-bottom-width","row-gap","column-gap"};
		static const std::set<std::string> colours = {"color","background-color","border-color","border-left-color","border-right-color","border-top-color","border-bottom-color"};
		static const std::map<std::string,std::set<std::string>> keywords = {
			{"position",{"absolute","relative"}}, {"display",{"block","inline","inline-block","flex","none"}},
			{"pointer-events",{"auto","none"}},
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
		return valid;
	}
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
		Fields(root,"",{"format","version","id","tokens","root","timelines","state","bindings","actions","presentationVariables","aliases","events","editor","extensions"});
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
		ReadState(root);
		ReadPresentationVariables(root);
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
		ReadBindings(root);
		ReadAliases(root);
		ReadActions(root);
		ReadEvents(root);
		ValidateControls(root["root"],model.root,"/root",false);
		State initial; std::string stateError;
		Require(initial.Reset(model,stateError),root["bindings"],"/bindings",stateError);
		std::string lastModal;
		ValidateInitialModals(root["root"],model.root,"/root",initial.Properties(),true,{},lastModal);
		return std::move(model);
	}
private:
	void ValidateInitialModals(const Json::Value& sourceNode, const Node& node, const std::string& path,
		const PropertyValues& bound, bool inherited, const std::string& parent, std::string& last) {
		const auto expression = bound.find({node.id,"display"});
		const auto base = node.properties.find("display");
		const auto* display = expression != bound.end() ? &expression->second : base != node.properties.end() ? &base->second : nullptr;
		const bool visible = inherited && (!display || display->text != "none");
		std::string scope = parent;
		if (visible && node.modal) {
			Require(last.empty() || last == parent,sourceNode,path+"/modal","Initially visible authored modals must form one nested ancestry chain");
			last = scope = node.id;
		}
		for (size_t i = 0; i < node.children.size(); ++i)
			ValidateInitialModals(sourceNode["children"][static_cast<Json::ArrayIndex>(i)],node.children[i],path+"/children/"+std::to_string(i),bound,visible,scope,last);
	}
	void ReadState(const Json::Value& root) {
		if (!root.isMember("state")) return;
		const auto& values = root["state"];
		Require(values.isObject() && values.size() <= 4096,values,"/state","Expected at most 4096 state declarations");
		for (const auto& name : values.getMemberNames()) {
			const auto p = "/state/"+PointerPart(name); const auto& v = values[name];
			Require(Identifier(name),v,p,"Invalid state ID");
			Fields(v,p,{"type","initial","cvar","extensions"});
			Require(v["type"].isString(),v,p+"/type","Expected a state type");
			const auto type = v["type"].asString(); StateDeclaration declaration;
			if (type == "number") declaration.initial = Numeric(v["initial"],p+"/initial",-1000000000000.0,1000000000000.0);
			else if (type == "boolean") {
				Require(v["initial"].isBool(),v["initial"],p+"/initial","Expected a boolean"); declaration.initial = v["initial"].asBool();
			} else if (type == "string") {
				Require(v["initial"].isString(),v["initial"],p+"/initial","Expected a string"); declaration.initial = v["initial"].asString();
				const auto& text = std::get<std::string>(declaration.initial);
				Require(text.empty() || (text.starts_with("#str_") && Identifier(text.substr(1))),v["initial"],p+"/initial","Authored string state starts empty or with a localization key; application data arrives through state updates");
			} else Require(false,v["type"],p+"/type","Expected number, boolean or string state");
			Require(ValidStateValue(declaration.initial),v["initial"],p+"/initial","Invalid state value");
			if (v.isMember("cvar")) declaration.cvar = Id(v["cvar"],p+"/cvar");
			model.state.emplace(name,std::move(declaration));
		}
	}
	Expression ReadExpression(const Json::Value& value, const std::string& path, unsigned depth = 0, bool allowPresentation = false,
		std::optional<size_t> inputType = std::nullopt) {
		Require(depth <= 32 && ++expressionCount <= 65536,value,path,"Expression node/depth budget exceeded");
		Expression result;
		if (value.isNumeric()) result.literal = Numeric(value,path,-1000000000000.0,1000000000000.0);
		else if (value.isBool()) result.literal = value.asBool();
		else if (value.isString()) result.literal = value.asString();
		else if (value.isObject() && value.isMember("input")) {
			Fields(value,path,{"input","extensions"});
			Require(inputType.has_value() && value["input"] == "value",value,path,"Invocation input requires a typed action declaration and input 'value'");
			result.inputValue = true; result.type = *inputType; return result;
		} else if (value.isObject() && value.isMember("state")) {
			Fields(value,path,{"state","extensions"}); result.state = Id(value["state"],path+"/state");
			const auto found = model.state.find(result.state);
			Require(found != model.state.end(),value,path,"Unknown state reference '"+result.state+"'");
			result.type = found->second.initial.index(); return result;
		} else if (value.isObject() && value.isMember("presentation")) {
			Fields(value,path,{"presentation","component","extensions"});
			Require(allowPresentation,value,path,"Presentation references are allowed only in actions and events");
			Require(value["presentation"].isString(),value["presentation"],path+"/presentation","Expected a presentation alias name");
			result.presentation = value["presentation"].asString();
			const auto type = PresentationAliasType(model,result.presentation);
			Require(type.has_value(),value["presentation"],path+"/presentation","Unknown or unsupported presentation alias");
			if (*type >= PresentationType::Vector2) {
				const int count = static_cast<int>(*type)-static_cast<int>(PresentationType::Vector2)+2;
				Require((value["component"].type() == Json::intValue || value["component"].type() == Json::uintValue) &&
					value["component"].isUInt() && value["component"].asUInt() < static_cast<unsigned>(count),
					value["component"],path+"/component","Vector presentation reads require an in-range component index");
				result.component = value["component"].asInt(); result.type = 0;
			} else {
				Require(!value.isMember("component"),value["component"],path+"/component","Scalar presentation reads cannot select a component");
				result.type = *type == PresentationType::Boolean ? 1 : *type == PresentationType::String ? 2 : 0;
			}
			return result;
		} else {
			Fields(value,path,{"op","args","decimals","extensions"});
			Require(value["op"].isString(),value,path+"/op","Expected an expression operation");
			result.op = value["op"].asString();
			static const std::map<std::string,unsigned> arities = {{"+",2},{"-",2},{"*",2},{"/",2},{"%",2},
				{"min",2},{"max",2},{"abs",1},{"floor",1},{"ceil",1},{"round",1},{"clamp",3},
				{"==",2},{"!=",2},{"<",2},{"<=",2},{">",2},{">=",2},{"&&",2},{"||",2},{"!",1},{"select",3},{"numberText",1}};
			const auto arity = arities.find(result.op);
			Require(arity != arities.end(),value["op"],path+"/op","Unknown expression operation");
			Require(value["args"].isArray() && value["args"].size() == arity->second,value["args"],path+"/args","Incorrect expression arity");
			for (Json::ArrayIndex i = 0; i < value["args"].size(); ++i) result.args.push_back(ReadExpression(value["args"][i],path+"/args/"+std::to_string(i),depth+1,allowPresentation,inputType));
			auto types = [&](size_t expected) { return std::all_of(result.args.begin(),result.args.end(),[&](const Expression& arg) { return arg.type == expected; }); };
			if (result.op == "select") {
				Require(result.args[0].type == 1 && result.args[1].type == result.args[2].type,value,path,"Select requires a boolean condition and matching branches"); result.type = result.args[1].type;
			} else if (result.op == "==" || result.op == "!=") {
				Require(result.args[0].type == result.args[1].type,value,path,"Equality operands must have the same type"); result.type = 1;
			} else if (result.op == "&&" || result.op == "||" || result.op == "!") {
				Require(types(1),value,path,"Logical operators require boolean operands"); result.type = 1;
			} else {
				Require(types(0),value,path,"Operation requires numeric operands");
				result.type = result.op == "numberText" ? 2 : (result.op == "<" || result.op == "<=" || result.op == ">" || result.op == ">=") ? 1 : 0;
			}
			if (value.isMember("decimals")) {
				Require(result.op == "numberText" && value["decimals"].isUInt() && value["decimals"].asUInt() <= 6,value["decimals"],path+"/decimals","Number text supports 0..6 decimal places"); result.decimals = value["decimals"].asUInt();
			}
			return result;
		}
		Require(ValidStateValue(result.literal),value,path,"Invalid expression literal"); result.type = result.literal.index(); return result;
	}
	void ReadPresentationVariables(const Json::Value& root) {
		if (!root.isMember("presentationVariables")) return;
		const auto& values = root["presentationVariables"];
		Require(values.isObject() && values.size() <= 4096,values,"/presentationVariables","Expected at most 4096 presentation variables");
		StateValues initialState;
		for (const auto& [id,state] : model.state) initialState.emplace(id,state.initial);
		for (const auto& id : values.getMemberNames()) {
			const auto path = "/presentationVariables/"+PointerPart(id); const auto& value = values[id];
			Require(Identifier(id),value,path,"Invalid presentation variable ID");
			Fields(value,path,{"type","initial","value","extensions"});
			Require(value["type"].isString(),value["type"],path+"/type","Expected a presentation variable type");
			static const std::map<std::string,PresentationType> types = {
				{"number",PresentationType::Number},{"boolean",PresentationType::Boolean},{"string",PresentationType::String},
				{"vector2",PresentationType::Vector2},{"vector3",PresentationType::Vector3},{"vector4",PresentationType::Vector4}};
			const auto type = types.find(value["type"].asString());
			Require(type != types.end(),value["type"],path+"/type","Unknown presentation variable type");
			PresentationVariable variable; variable.initial.type = type->second;
			const bool vector = type->second >= PresentationType::Vector2;
			const size_t count = vector ? static_cast<size_t>(type->second)-static_cast<size_t>(PresentationType::Vector2)+2 : 1;
			const auto& initial = value["initial"];
			if (vector) Require(initial.isArray() && initial.size() == count,initial,path+"/initial","Presentation vector initial value has the wrong component count");
			if (type->second == PresentationType::String) {
				Require(initial.isString(),initial,path+"/initial","Expected a presentation string"); variable.initial.text = initial.asString();
			} else if (type->second == PresentationType::Boolean) {
				Require(initial.isBool(),initial,path+"/initial","Expected a presentation boolean"); variable.initial.data[0] = initial.asBool() ? 1 : 0;
			} else for (size_t i = 0; i < count; ++i) {
				variable.initial.data[i] = Numeric(vector ? initial[static_cast<Json::ArrayIndex>(i)] : initial,
					path+"/initial"+(vector ? "/"+std::to_string(i) : ""),-1000000000000.0,1000000000000.0);
			}
			Require(ValidPresentationValue(variable.initial),initial,path+"/initial","Invalid presentation initial value");
			if (value.isMember("value")) {
				const auto& expressions = value["value"];
				Require(!vector || (expressions.isArray() && expressions.size() == count),expressions,path+"/value","Presentation vector expression has the wrong component count");
				const size_t expected = type->second == PresentationType::Boolean ? 1 : type->second == PresentationType::String ? 2 : 0;
				PresentationValue evaluated; evaluated.type = type->second;
				for (size_t i = 0; i < count; ++i) {
					const auto at = path+"/value"+(vector ? "/"+std::to_string(i) : "");
					const auto& sourceValue = vector ? expressions[static_cast<Json::ArrayIndex>(i)] : expressions;
					auto expression = ReadExpression(sourceValue,at);
					Require(expression.type == expected,sourceValue,at,"Expression type does not match its presentation variable");
					StateValue result; std::string error;
					Require(EvaluateStateExpression(expression,initialState,result,error),sourceValue,at,error);
					if (expected == 2) evaluated.text = std::get<std::string>(result);
					else if (expected == 1) evaluated.data[0] = std::get<bool>(result) ? 1 : 0;
					else evaluated.data[i] = std::get<double>(result);
					variable.expressions.push_back(std::move(expression));
				}
				Require(ValidPresentationValue(evaluated),expressions,path+"/value","Invalid initial presentation expression result");
			}
			model.presentationVariables.emplace(id,std::move(variable));
		}
	}
	void ReadAliases(const Json::Value& root) {
		if (!root.isMember("aliases")) return;
		const auto& aliases = root["aliases"];
		Require(aliases.isObject() && aliases.size() <= 8192,aliases,"/aliases","Expected at most 8192 presentation aliases");
		for (const auto& name : aliases.getMemberNames()) {
			const auto path = "/aliases/"+PointerPart(name); const auto& value = aliases[name];
			const auto separator = name.find("::");
			const bool qualified = separator != std::string::npos;
			Require(qualified ? Identifier(name.substr(0,separator)) && Identifier(name.substr(separator+2)) : Identifier(name),
				value,path,"Alias names require one stable ID or two IDs separated by ::");
			const auto key = PresentationAliasKey(name);
			Require(!key.empty() && !key.starts_with("gui::"),value,path,"The gui:: application-state namespace is not a presentation alias");
			Require(!model.aliases.contains(key),value,path,"Presentation aliases collide after case folding");
			Fields(value,path,{"node","property","variable","shown","extensions"});
			PresentationAlias alias;
			if (value.isMember("variable")) {
				Require(!value.isMember("node") && !value.isMember("property") && !value.isMember("shown"),value,path,"A variable alias cannot also target rendered properties");
				alias.variable = Id(value["variable"],path+"/variable");
				Require(model.presentationVariables.contains(alias.variable),value["variable"],path+"/variable","Unknown presentation variable target");
			} else {
				alias.node = Id(value["node"],path+"/node"); alias.property = Id(value["property"],path+"/property");
				const auto* node = model.FindNode(alias.node);
				Require(node != nullptr,value["node"],path+"/node","Unknown presentation node target");
				Require(alias.property == "visible" || !value.isMember("shown"),value,path,"Only visible aliases declare a shown display value");
				if (alias.property == "rect") {
					for (const auto* property : {"left","top","width","height"}) {
						const auto found = node->properties.find(property);
						Require(found != node->properties.end() && found->second.type == ValueType::Length && found->second.unit == "dp",
							value,path,"Rectangle aliases require explicit left/top/width/height lengths in dp");
					}
				} else if (alias.property == "visible" || alias.property == "noevents") {
					const std::string property = alias.property == "visible" ? "display" : "pointer-events";
					const auto found = node->properties.find(property);
					Require(found != node->properties.end() && found->second.type == ValueType::Keyword,value,path,"Semantic aliases require an explicit canonical keyword base");
					if (alias.property == "visible") {
						Require(value["shown"].isString(),value["shown"],path+"/shown","Visible aliases require a shown display keyword");
						alias.shown = value["shown"].asString(); Value shown; shown.type = ValueType::Keyword; shown.text = alias.shown;
						Require(alias.shown != "none" && ValidProperty("display",shown),value["shown"],path+"/shown","Invalid shown display keyword");
					}
				} else {
					const auto found = node->properties.find(alias.property);
					Require(found != node->properties.end(),value["property"],path+"/property","Presentation aliases require an explicit canonical property base");
					const auto& base = found->second;
					const bool supported = base.type == ValueType::Number || base.type == ValueType::Length || base.type == ValueType::Colour ||
						base.type == ValueType::Text || base.type == ValueType::Keyword || base.type == ValueType::Font;
					Require(supported && (base.type != ValueType::Length || base.unit == "dp" || base.unit == "px"),
						value,path,"Presentation property type or unit is unsupported");
				}
			}
			model.aliases.emplace(key,std::move(alias));
		}
	}
	void ReadActions(const Json::Value& root) {
		if (!root.isMember("actions")) return;
		const auto& actions = root["actions"];
		Require(actions.isObject() && actions.size() <= 4096,actions,"/actions","Expected at most 4096 action descriptors");
		for (const auto& id : actions.getMemberNames()) {
			const auto& value = actions[id]; const auto path = "/actions/"+PointerPart(id);
			Require(Identifier(id),value,path,"Invalid action ID");
			Fields(value,path,{"operation","arguments","input","extensions"});
			Action action; action.operation = Id(value["operation"],path+"/operation");
			if (value.isMember("input")) {
				Require(value["input"].isString(),value["input"],path+"/input","Expected an action input type");
				const std::map<std::string,size_t> types = {{"number",0},{"boolean",1},{"string",2}};
				const auto found = types.find(value["input"].asString());
				Require(found != types.end(),value["input"],path+"/input","Expected number, boolean or string input");
				action.inputType = found->second;
			}
			const auto& arguments = value["arguments"];
			Require(arguments.isObject() && arguments.size() <= 32,arguments,path+"/arguments","Expected at most 32 named action arguments");
			for (const auto& name : arguments.getMemberNames()) {
				const auto at = path+"/arguments/"+PointerPart(name);
				Require(Identifier(name),arguments[name],at,"Invalid action argument name");
				action.arguments.emplace(name,ReadExpression(arguments[name],at,0,true,action.inputType));
			}
			model.actions.emplace(id,std::move(action));
		}
	}
	std::string EventName(const Json::Value& value, const std::string& path) {
		Require(value.isString(),value,path,"Expected a public event or alias name");
		const auto key = PresentationAliasKey(value.asString());
		Require(!key.empty(),value,path,"Expected one identifier or a qualified name outside gui::");
		return key;
	}
	std::vector<EventStep> ReadSteps(const Json::Value& values, const std::string& path, unsigned depth) {
		Require(values.isArray(),values,path,"Expected an ordered event step array");
		Require(depth <= 32,values,path,"Event structural depth exceeds 32");
		std::vector<EventStep> result;
		for (Json::ArrayIndex i = 0; i < values.size(); ++i) {
			const auto& value = values[i]; const auto at = path+"/"+std::to_string(i);
			Require(++eventStepCount <= 8192,value,at,"Event step count exceeds 8192");
			Require(value.isObject() && value["op"].isString(),value,at,"Expected an event operation object");
			EventStep step; const auto op = value["op"].asString();
			if (op == "setState") {
				Fields(value,at,{"op","values","extensions"}); step.op = EventOp::SetState;
				const auto& batch = value["values"];
				Require(batch.isObject() && batch.size() <= 4096,batch,at+"/values","Expected at most 4096 application state values");
				for (const auto& id : batch.getMemberNames()) {
					const auto p = at+"/values/"+PointerPart(id); const auto found = model.state.find(id);
					Require(found != model.state.end() && found->second.cvar.empty(),batch[id],p,"Event writes require declared application-owned state");
					auto expression = ReadExpression(batch[id],p,0,true);
					Require(expression.type == found->second.initial.index(),batch[id],p,"Event state expression has the wrong type");
					step.values.emplace(id,std::move(expression));
				}
			} else if (op == "setPresentation") {
				Fields(value,at,{"op","alias","value","overrideExpression","extensions"}); step.op = EventOp::SetPresentation;
				step.target = EventName(value["alias"],at+"/alias");
				const auto type = PresentationAliasType(model,step.target);
				Require(type.has_value(),value["alias"],at+"/alias","Unknown or unsupported presentation alias");
				Require(value["overrideExpression"].isBool(),value["overrideExpression"],at+"/overrideExpression","Presentation writes require an explicit Boolean ownership flag");
				step.overrideExpression = value["overrideExpression"].asBool();
				const bool vector = *type >= PresentationType::Vector2;
				const size_t count = vector ? static_cast<size_t>(*type)-static_cast<size_t>(PresentationType::Vector2)+2 : 1;
				const size_t expected = *type == PresentationType::Boolean ? 1 : *type == PresentationType::String ? 2 : 0;
				const auto& expressions = value["value"];
				Require(!vector || (expressions.isArray() && expressions.size() == count),expressions,at+"/value","Presentation event tuple has the wrong component count");
				for (size_t j = 0; j < count; ++j) {
					const auto p = at+"/value"+(vector ? "/"+std::to_string(j) : "");
					const auto& part = vector ? expressions[static_cast<Json::ArrayIndex>(j)] : expressions;
					auto expression = ReadExpression(part,p,0,true);
					Require(expression.type == expected,part,p,"Presentation event expression has the wrong type");
					const auto& alias = model.aliases.at(step.target);
					if (alias.property == "text") Require(LocalizedTextResult(expression),part,p,"Literal display text must use localization keys");
					step.presentation.push_back(std::move(expression));
				}
			} else if (op == "action") {
				Fields(value,at,{"op","action","extensions"}); step.op = EventOp::Action;
				step.target = Id(value["action"],at+"/action");
				Require(model.actions.contains(step.target),value["action"],at+"/action","Unknown event action descriptor");
				Require(!model.actions.at(step.target).inputType,value["action"],at+"/action","An event cannot invoke an input-bearing action without an operand");
			} else if (op == "call") {
				Fields(value,at,{"op","event","extensions"}); step.op = EventOp::Call;
				step.target = EventName(value["event"],at+"/event");
				Require(model.events.contains(step.target),value["event"],at+"/event","Unknown called event");
			} else if (op == "if") {
				Fields(value,at,{"op","condition","then","else","extensions"}); step.op = EventOp::If;
				step.condition = ReadExpression(value["condition"],at+"/condition",0,true);
				Require(step.condition.type == 1,value["condition"],at+"/condition","Event condition requires a Boolean expression");
				step.thenSteps = ReadSteps(value["then"],at+"/then",depth+1);
				if (value.isMember("else")) step.elseSteps = ReadSteps(value["else"],at+"/else",depth+1);
			} else {
				const bool cancel = op == "cancelTimeline";
				if (cancel) Fields(value,at,{"op","timeline","policy","extensions"});
				else Fields(value,at,{"op","timeline","extensions"});
				static const std::map<std::string,EventOp> timelineOps = {{"playTimeline",EventOp::PlayTimeline},
					{"pauseTimeline",EventOp::PauseTimeline},{"resumeTimeline",EventOp::ResumeTimeline},{"cancelTimeline",EventOp::CancelTimeline}};
				const auto found = timelineOps.find(op);
				Require(found != timelineOps.end(),value["op"],at+"/op","Unknown event operation"); step.op = found->second;
				step.target = Id(value["timeline"],at+"/timeline");
				Require(std::any_of(model.timelines.begin(),model.timelines.end(),[&](const Timeline& item) { return item.id == step.target; }),
					value["timeline"],at+"/timeline","Unknown event timeline");
				if (cancel) {
					Require(value["policy"] == "hold" || value["policy"] == "base",value["policy"],at+"/policy","Timeline cancellation requires hold or base policy");
					step.restoreBase = value["policy"] == "base";
				}
			}
			result.push_back(std::move(step));
		}
		return result;
	}
	void ReadEvents(const Json::Value& root) {
		if (!root.isMember("events")) return;
		const auto& events = root["events"];
		Require(events.isObject() && events.size() <= 1024,events,"/events","Expected at most 1024 named event programs");
		// Declare every name first: forward and recursive calls are legal. The
		// behavior executor bounds dynamic call depth and total executed steps.
		for (const auto& name : events.getMemberNames()) {
			const auto path = "/events/"+PointerPart(name); const auto key = PresentationAliasKey(name);
			Require(!key.empty(),events[name],path,"Expected one event identifier or a qualified name outside gui::");
			Require(!model.events.contains(key),events[name],path,"Event names collide after case folding");
			model.events.emplace(key,EventProgram{name,{}});
		}
		for (const auto& name : events.getMemberNames())
			model.events.at(PresentationAliasKey(name)).steps = ReadSteps(events[name],"/events/"+PointerPart(name),0);
	}
	bool LocalizedTextResult(const Expression& expression) const {
		if (!expression.state.empty() || !expression.presentation.empty() || expression.op == "numberText") return true;
		if (expression.op == "select") return LocalizedTextResult(expression.args[1]) && LocalizedTextResult(expression.args[2]);
		if (!expression.op.empty() || expression.type != 2) return false;
		const auto& text = std::get<std::string>(expression.literal);
		return text.empty() || (text.starts_with("#str_") && Identifier(text.substr(1)));
	}
	void ReadBindings(const Json::Value& root) {
		if (!root.isMember("bindings")) return;
		const auto& bindings = root["bindings"];
		Require(bindings.isArray() && bindings.size() <= 8192,bindings,"/bindings","Expected at most 8192 bindings");
		std::set<std::string> ids; std::set<std::pair<std::string,std::string>> targets;
		for (Json::ArrayIndex i = 0; i < bindings.size(); ++i) {
			const auto& value = bindings[i]; const auto path = "/bindings/"+std::to_string(i);
			Fields(value,path,{"id","node","property","value","extensions"});
			Binding binding; binding.id = Id(value["id"],path+"/id"); binding.node = Id(value["node"],path+"/node"); binding.property = Id(value["property"],path+"/property");
			Require(ids.insert(binding.id).second,value,path,"Duplicate binding ID");
			const Node* node = model.FindNode(binding.node);
			Require(node != nullptr,value["node"],path+"/node","Unknown binding target");
			const bool enabled = binding.property == "enabled";
			Require(!enabled || node->control.has_value(),value,path,"Enabled binding requires a semantic control");
			const auto properties = enabled ? std::vector<std::string>{"enabled"} : ExpandedProperties(binding.property);
			for (const auto& property : properties) {
				Require(enabled || node->properties.contains(property),value,path,"Bound properties require explicit base values");
				Require(targets.insert({binding.node,property}).second,value,path,"Multiple bindings own the same effective property");
				for (const auto& timeline : model.timelines) for (const auto& track : timeline.tracks)
					Require(track.node != binding.node || track.property != property,value,path,"A binding and timeline cannot own the same property; use separate presentation/content nodes");
				Binding effective = binding; effective.property = property;
				if (!enabled) effective.prototype = node->properties.at(property);
				const auto type = effective.prototype.type;
				const size_t expected = enabled ? 1 : (type == ValueType::Text || type == ValueType::Keyword || type == ValueType::Font) ? 2 : 0;
				const size_t count = !enabled && type == ValueType::Colour ? 4 : !enabled && type == ValueType::Transform ? 5 : 1;
				Require(count == 1 || (value["value"].isArray() && value["value"].size() == count),value["value"],path+"/value","Binding component count does not match its target");
				for (size_t component = 0; component < count; ++component) {
					const auto at = count == 1 ? path+"/value" : path+"/value/"+std::to_string(component);
					auto expression = ReadExpression(count == 1 ? value["value"] : value["value"][static_cast<Json::ArrayIndex>(component)],at);
					Require(expression.type == expected,value["value"],at,"Expression type does not match its target");
					if (!enabled && type == ValueType::Text) Require(LocalizedTextResult(expression),value["value"],at,"Literal display text must use localization keys");
					effective.values.push_back(std::move(expression));
				}
				model.bindings.push_back(std::move(effective));
			}
		}
	}
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
	std::string ControlLabel(const Json::Value& value, const std::string& path) {
		Require(value.isString(),value,path,"Control labels require a #str_ localization key");
		const auto label = value.asString();
		Require(label.starts_with("#str_") && Identifier(label.substr(1)),value,path,"Control labels require a #str_ localization key");
		return label;
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
		Require(ValidProperty(name,value),at,path,"Unsupported property or invalid value for '"+name+"'");
	}

	Node ReadNode(const Json::Value& value, const std::string& path, unsigned depth) {
		Require(depth <= 48 && ++nodeCount <= 65536,value,path,"Document hierarchy exceeds the node/depth limit");
		Fields(value,path,{"id","type","properties","children","paths","mask","control","modal","extensions"});
		Node result;
		result.id = Id(value["id"],path+"/id");
		Require(nodeIds.insert(result.id).second,value["id"],path+"/id","Duplicate node ID '"+result.id+"'");
		Require(value["type"] == "group" || value["type"] == "text" || value["type"] == "vector",value["type"],path+"/type","Supported node types are group, text and vector; unsupported nodes cannot be silently rendered");
		result.type = value["type"].asString();
		if (value.isMember("modal")) {
			const auto& modal = value["modal"]; const auto p = path+"/modal";
			Fields(modal,p,{"initialFocus","back","extensions"});
			Require(result.type == "group" && !value.isMember("control"),modal,p,"A modal must be a group without its own control");
			result.modal.emplace(); result.modal->initialFocus = Id(modal["initialFocus"],p+"/initialFocus");
			if (modal.isMember("back")) result.modal->backEvent = EventName(modal["back"],p+"/back");
		}
		if (value.isMember("control")) {
			const auto& control = value["control"]; const auto p = path+"/control";
			Require(control.isObject() && control["role"].isString(),control,p,"Expected a semantic control role");
			const auto role = control["role"].asString();
			if (role == "button") Fields(control,p,{"role","action","event","label","enabled","states","navigation","extensions"});
			else if (role == "toggle") Fields(control,p,{"role","action","label","enabled","states","navigation","value","mixed","parts","extensions"});
			else if (role == "slider") Fields(control,p,{"role","action","label","enabled","states","navigation","value","minimum","maximum","step","decimals","orientation","parts","extensions"});
			else if (role == "choice") Fields(control,p,{"role","action","label","enabled","states","navigation","value","parts","visibleRows","options","extensions"});
			else Require(false,control["role"],p+"/role","Supported roles are button, toggle, slider and choice");
			result.control.emplace(); auto& parsed = *result.control;
			Require(control.isMember("action") != control.isMember("event"),control,p,"Controls require exactly one action or event");
			if (control.isMember("action")) parsed.action = Id(control["action"],p+"/action");
			else parsed.event = EventName(control["event"],p+"/event");
			parsed.label = ControlLabel(control["label"],p+"/label");
			if (role != "button") {
				parsed.value = ReadExpression(control["value"],p+"/value");
				const auto& parts = control["parts"]; const auto at = p+"/parts";
				if (role == "toggle") {
					parsed.role = ControlRole::Toggle;
					Require(parsed.value->type == 1,control["value"],p+"/value","Toggle values must be Boolean");
					Fields(parts,at,{"checked","mixed","extensions"});
					ToggleSpec spec; spec.checkedPart = Id(parts["checked"],at+"/checked");
					if (parts.isMember("mixed")) spec.mixedPart = Id(parts["mixed"],at+"/mixed");
					if (control.isMember("mixed")) {
						spec.mixed = ReadExpression(control["mixed"],p+"/mixed");
						Require(spec.mixed->type == 1,control["mixed"],p+"/mixed","Mixed state must be Boolean");
						Require(!spec.mixedPart.empty(),parts,at,"Mixed state requires an authored mixed part");
					}
					parsed.widget = std::move(spec);
				} else if (role == "slider") {
					parsed.role = ControlRole::Slider;
					Require(parsed.value->type == 0,control["value"],p+"/value","Slider values must be numeric");
					Fields(parts,at,{"track","fill","thumb","value","extensions"});
					SliderSpec spec;
					spec.minimum = Numeric(control["minimum"],p+"/minimum",-1e12,1e12);
					spec.maximum = Numeric(control["maximum"],p+"/maximum",-1e12,1e12);
					spec.step = Numeric(control["step"],p+"/step",0,1e12);
					Require(spec.minimum < spec.maximum && spec.step > 0 && spec.minimum+spec.step > spec.minimum &&
						spec.maximum-spec.step < spec.maximum,control,p,"Slider requires ordered bounds and a representable positive step");
					const double ticks = (spec.maximum-spec.minimum)/spec.step;
					Require(std::isfinite(ticks) && ticks >= 1 && ticks <= 1000000 &&
						std::abs(ticks-std::round(ticks)) <= 32*std::numeric_limits<double>::epsilon()*std::max(1.0,ticks),control,p,"Slider bounds must span 1..1000000 whole steps");
					if (control.isMember("decimals")) {
						Require(control["decimals"].isUInt() && control["decimals"].asUInt() <= 6,control["decimals"],p+"/decimals","Slider decimals must be 0..6");
						spec.decimals = control["decimals"].asUInt();
					}
					if (control.isMember("orientation")) {
						Require(control["orientation"] == "horizontal" || control["orientation"] == "vertical",control["orientation"],p+"/orientation","Expected horizontal or vertical orientation");
						spec.vertical = control["orientation"] == "vertical";
					}
					spec.track = Id(parts["track"],at+"/track"); spec.fill = Id(parts["fill"],at+"/fill");
					spec.thumb = Id(parts["thumb"],at+"/thumb"); spec.valueText = Id(parts["value"],at+"/value");
					parsed.widget = std::move(spec);
				} else {
					parsed.role = ControlRole::Choice;
					Fields(parts,at,{"popup","viewport","content","value","extensions"});
					ChoiceSpec spec; spec.popup = Id(parts["popup"],at+"/popup"); spec.viewport = Id(parts["viewport"],at+"/viewport");
					spec.content = Id(parts["content"],at+"/content"); spec.valueText = Id(parts["value"],at+"/value");
					if (control.isMember("visibleRows")) {
						Require(control["visibleRows"].isUInt() && control["visibleRows"].asUInt() >= 1 && control["visibleRows"].asUInt() <= 32,
							control["visibleRows"],p+"/visibleRows","Choice visible rows must be 1..32");
						spec.visibleRows = control["visibleRows"].asUInt();
					}
					Require(control["options"].isArray() && !control["options"].empty() && control["options"].size() <= 256,
						control["options"],p+"/options","Choice requires 1..256 authored options");
					std::set<std::string> ids; std::set<StateValue> values;
					for (Json::ArrayIndex i = 0; i < control["options"].size(); ++i) {
						const auto& value = control["options"][i]; const auto opt = p+"/options/"+std::to_string(i);
						Fields(value,opt,{"id","node","label","labelIndex","value","enabled","parts","extensions"});
						ChoiceOption option; option.id = Id(value["id"],opt+"/id"); option.node = Id(value["node"],opt+"/node");
						Require(ids.insert(option.id).second,value["id"],opt+"/id","Duplicate choice option ID");
						option.label = ControlLabel(value["label"],opt+"/label");
						if (value.isMember("labelIndex")) {
							Require(value["labelIndex"].isUInt() && value["labelIndex"].asUInt() <= 255,value["labelIndex"],opt+"/labelIndex","Localized label index must be 0..255");
							option.labelIndex = value["labelIndex"].asUInt();
						}
						Require(value["value"].isNumeric() || value["value"].isBool() || value["value"].isString(),value["value"],opt+"/value","Choice values must be typed literals");
						const auto expression = ReadExpression(value["value"],opt+"/value"); option.value = expression.literal;
						Require(expression.type == parsed.value->type,value["value"],opt+"/value","Choice options must match the authoritative value type");
						Require(values.insert(option.value).second,value["value"],opt+"/value","Duplicate choice option value");
						if (value.isMember("enabled")) option.enabled = ReadExpression(value["enabled"],opt+"/enabled");
						Require(option.enabled.type == 1,value["enabled"],opt+"/enabled","Choice option availability must be Boolean");
						Fields(value["parts"],opt+"/parts",{"label","selected","highlight","extensions"});
						option.labelPart = Id(value["parts"]["label"],opt+"/parts/label");
						option.selectedPart = Id(value["parts"]["selected"],opt+"/parts/selected");
						option.highlightPart = Id(value["parts"]["highlight"],opt+"/parts/highlight");
						spec.options.push_back(std::move(option));
					}
					parsed.widget = std::move(spec);
				}
			}
			if (control.isMember("enabled")) {
				Require(control["enabled"].isBool(),control["enabled"],p+"/enabled","Expected a boolean");
				parsed.enabled = control["enabled"].asBool();
			}
			Fields(control["states"],p+"/states",{"default","hover","focus","pressed","disabled","extensions"});
			const std::map<std::string,ControlState> states = {{"default",ControlState::Default},{"hover",ControlState::Hover},
				{"focus",ControlState::Focus},{"pressed",ControlState::Pressed},{"disabled",ControlState::Disabled}};
			for (const auto& [name,state] : states) parsed.states[state] = Id(control["states"][name],p+"/states/"+name);
			if (control.isMember("navigation")) {
				Fields(control["navigation"],p+"/navigation",{"next","previous","up","down","left","right","extensions"});
				for (const auto& name : control["navigation"].getMemberNames()) if (name != "extensions")
					parsed.navigation[name] = Id(control["navigation"][name],p+"/navigation/"+name);
			}
		}
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
		if (value.isMember("mask")) {
			const auto& mask = value["mask"]; const auto p = path+"/mask";
			Fields(mask,p,{"paths","extensions"});
			Require(mask["paths"].isArray(),mask["paths"],p+"/paths","Mask requires a path array");
			result.mask.emplace();
			std::set<std::string> ids;
			for (Json::ArrayIndex i = 0; i < mask["paths"].size(); ++i) {
				const auto at = p+"/paths/"+std::to_string(i);
				auto shape = ReadPath(mask["paths"][i],at);
				Require(ids.insert(shape.id).second,mask["paths"][i]["id"],at+"/id","Duplicate path ID within mask");
				result.mask->push_back(std::move(shape));
			}
		}
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
	bool Descendant(const std::string& child, const std::string& ancestor) const {
		const auto* root = model.FindNode(ancestor);
		if (!root || child == ancestor) return false;
		std::vector<const Node*> nodes;
		for (const auto& next : root->children) nodes.push_back(&next);
		while (!nodes.empty()) {
			const auto* current = nodes.back(); nodes.pop_back();
			if (current->id == child) return true;
			for (const auto& next : current->children) nodes.push_back(&next);
		}
		return false;
	}
	void ValidateWidgetParts(const Json::Value& value, const Node& owner, const std::string& path) {
		const auto& control = *owner.control;
		std::set<std::string> parts;
		std::set<std::pair<std::string,std::string>> owned;
		const auto part = [&](const std::string& id, const std::string& parent, const char* type, const std::string& at) {
			const auto* node = model.FindNode(id);
			Require(node && Descendant(id,parent),value,at,"Widget parts must be strict descendants of their authored owner");
			Require(parts.insert(id).second,value,at,"Widget parts must name distinct nodes");
			Require(type ? node->type == type : (node->type == "group" || node->type == "vector"),value,at,"Widget part has the wrong node type");
		};
		const auto reserve = [&](const std::string& id, const std::string& property) {
			const auto* node = model.FindNode(id); const auto found = node->properties.find(property);
			const auto expected = property == "text" ? ValueType::Text : property == "display" ? ValueType::Keyword : ValueType::Length;
			Require(found != node->properties.end() && found->second.type == expected,value,path,
				"Runtime-owned widget property requires an explicit typed base: '"+id+"."+property+"'");
			owned.emplace(id,property);
		};
		const auto separate = [&](const std::string& a, const std::string& b) {
			Require(!Descendant(a,b) && !Descendant(b,a),value,path,"Independent widget paint parts cannot contain one another");
		};
		const auto keyword = [&](const std::string& id, const char* property, const char* expected) {
			const auto& props = model.FindNode(id)->properties; const auto found = props.find(property);
			Require(found != props.end() && found->second.type == ValueType::Keyword && found->second.text == expected,
				value,path,"Widget geometry requires '"+id+"."+property+":"+expected+"'");
		};
		const auto zero = [&](const std::string& id, const char* property) {
			const auto& props = model.FindNode(id)->properties; const auto found = props.find(property);
			Require(found != props.end() && found->second.type == ValueType::Length && found->second.data[0] == 0,
				value,path,"Widget geometry requires a zero '"+id+"."+property+"' anchor");
		};
		if (const auto* toggle = std::get_if<ToggleSpec>(&control.widget)) {
			part(toggle->checkedPart,owner.id,nullptr,path+"/parts/checked"); reserve(toggle->checkedPart,"display");
			if (!toggle->mixedPart.empty()) {
				part(toggle->mixedPart,owner.id,nullptr,path+"/parts/mixed"); reserve(toggle->mixedPart,"display");
				separate(toggle->checkedPart,toggle->mixedPart);
			}
		} else if (const auto* slider = std::get_if<SliderSpec>(&control.widget)) {
			part(slider->track,owner.id,"group",path+"/parts/track");
			part(slider->fill,slider->track,nullptr,path+"/parts/fill");
			part(slider->thumb,slider->track,nullptr,path+"/parts/thumb");
			part(slider->valueText,owner.id,"text",path+"/parts/value");
			separate(slider->fill,slider->thumb);
			keyword(slider->thumb,"position","absolute");
			if (slider->vertical) {
				keyword(slider->fill,"position","absolute"); zero(slider->fill,"bottom");
				const auto& props = model.FindNode(slider->fill)->properties;
				if (props.contains("top")) keyword(slider->fill,"top","auto");
			}
			reserve(slider->fill,slider->vertical ? "height" : "width");
			reserve(slider->thumb,slider->vertical ? "top" : "left"); reserve(slider->valueText,"text");
		} else if (const auto* choice = std::get_if<ChoiceSpec>(&control.widget)) {
			part(choice->popup,owner.id,"group",path+"/parts/popup");
			part(choice->viewport,choice->popup,"group",path+"/parts/viewport");
			part(choice->content,choice->viewport,"group",path+"/parts/content");
			part(choice->valueText,owner.id,"text",path+"/parts/value");
			Require(!Descendant(choice->valueText,choice->popup),value,path+"/parts/value","Collapsed choice text must remain outside its popup");
			keyword(choice->content,"position","absolute"); zero(choice->content,"left");
			const auto& viewportProps = model.FindNode(choice->viewport)->properties;
			const auto position = viewportProps.find("position");
			Require(position != viewportProps.end() && position->second.type == ValueType::Keyword &&
				(position->second.text == "absolute" || position->second.text == "relative"),value,path,
				"Choice viewport must establish the content's positioned containing block");
			for (const auto* property : {"display","left","top","width","height"}) reserve(choice->popup,property);
			// Portal placement and clipping are derived invariants. They have no
			// required authored base, but cannot have a competing dynamic owner.
			owned.emplace(choice->popup,"position"); owned.emplace(choice->popup,"z-index");
			owned.emplace(choice->viewport,"overflow");
			owned.emplace(choice->viewport,"clip");
			reserve(choice->viewport,"height"); reserve(choice->content,"top"); reserve(choice->valueText,"text");
			for (size_t i = 0; i < choice->options.size(); ++i) {
				const auto& option = choice->options[i]; const auto at = path+"/options/"+std::to_string(i);
				part(option.node,choice->content,"group",at+"/node");
				part(option.labelPart,option.node,"text",at+"/parts/label");
				part(option.selectedPart,option.node,nullptr,at+"/parts/selected");
				part(option.highlightPart,option.node,nullptr,at+"/parts/highlight");
				separate(option.selectedPart,option.highlightPart);
				separate(option.labelPart,option.selectedPart); separate(option.labelPart,option.highlightPart);
				reserve(option.labelPart,"text"); reserve(option.selectedPart,"display"); reserve(option.highlightPart,"display");
				for (size_t j = 0; j < i; ++j) separate(option.node,choice->options[j].node);
			}
		}
		for (const auto& binding : model.bindings)
			Require(!owned.contains({binding.node,binding.property}),value,path,"A binding cannot own a runtime widget property");
		for (const auto& timeline : model.timelines) for (const auto& track : timeline.tracks)
			Require(!owned.contains({track.node,track.property}),value,path,"A timeline cannot own a runtime widget property");
		for (const auto& [name,alias] : model.aliases) {
			if (alias.node.empty()) continue;
			const auto targets = alias.property == "rect" ? std::vector<std::string>{"left","top","width","height"} :
				std::vector<std::string>{alias.property == "visible" ? "display" : alias.property == "noevents" ? "pointer-events" : alias.property};
			for (const auto& property : targets)
				Require(!owned.contains({alias.node,property}),value,path,"A presentation alias cannot own a runtime widget property");
		}
	}
	void ValidateControls(const Json::Value& sourceNode, const Node& node, const std::string& path, bool ancestorControl) {
		if (node.modal) {
			const auto& value = sourceNode["modal"]; const auto p = path+"/modal";
			Require(!ancestorControl,value,p,"A modal cannot be nested inside a semantic control");
			const auto* focus = model.FindNode(node.modal->initialFocus);
			Require(focus && focus->control && Descendant(focus->id,node.id),value["initialFocus"],p+"/initialFocus","Initial focus must be a control inside this modal");
			std::vector<const Node*> pending;
			for (const auto& child : node.children) pending.push_back(&child);
			while (!pending.empty()) {
				const auto* child = pending.back(); pending.pop_back();
				if (child->modal) Require(!Descendant(focus->id,child->id),value["initialFocus"],p+"/initialFocus","Initial focus cannot belong to a nested modal");
				for (const auto& next : child->children) pending.push_back(&next);
			}
			Require(node.modal->backEvent.empty() || model.events.contains(node.modal->backEvent),value["back"],p+"/back","Unknown modal Back event");
		}
		if (node.control) {
			const auto& value = sourceNode["control"]; const auto p = path+"/control";
			Require(!ancestorControl,value,p,"Semantic controls cannot be nested inside another control");
			Require(node.control->event.empty() || model.events.contains(node.control->event),value["event"],p+"/event","Unknown control event");
			const auto action = model.actions.find(node.control->action);
			if (node.control->role == ControlRole::Button) {
				Require(action == model.actions.end() || !action->second.inputType,value["action"],p+"/action","A button cannot invoke an input-bearing action without an operand");
			} else {
				Require(action != model.actions.end() && action->second.inputType && *action->second.inputType == node.control->value->type,
					value["action"],p+"/action","Value controls require an action with matching typed input");
				ValidateWidgetParts(value,node,p);
			}
			std::set<std::string> descendants;
			std::vector<const Node*> nodes{&node};
			while (!nodes.empty()) { const auto* child = nodes.back(); nodes.pop_back(); descendants.insert(child->id); for (const auto& next : child->children) nodes.push_back(&next); }
			std::set<std::pair<std::string,std::string>> coverage;
			for (const auto& name : {"default","hover","focus","pressed","disabled"}) {
				const std::string id = value["states"][name].asString();
				const auto timeline = std::find_if(model.timelines.begin(),model.timelines.end(),[&](const Timeline& item) { return item.id == id; });
				const auto at = p+"/states/"+name;
				Require(timeline != model.timelines.end(),value["states"][name],at,"Control state timeline does not exist");
				Require(timeline->iterations == 1,value["states"][name],at,"Control state timelines must run once");
				std::set<std::pair<std::string,std::string>> targets;
				for (const auto& track : timeline->tracks) {
					Require(descendants.contains(track.node),value["states"][name],at,"Control feedback can target only its own subtree");
					const bool ownPaint = track.property == "opacity" || track.property == "color" || track.property == "background-color" ||
						(track.property.starts_with("border-") && track.property.ends_with("color"));
					Require(track.node != node.id || ownPaint,value["states"][name],at,"Feedback must keep the button hit box stable; animate geometry on child parts");
					targets.insert({track.node,track.property});
				}
				if (coverage.empty()) coverage = targets;
				else Require(coverage == targets,value["states"][name],at,"Every control state must cover the same properties so interrupted feedback can restore them");
			}
			for (const auto& [name,target] : node.control->navigation) {
				const auto* next = model.FindNode(target);
				Require(next && next->control,value["navigation"][name],p+"/navigation/"+name,"Navigation target must be a control in this document");
			}
		}
		for (size_t i = 0; i < node.children.size(); ++i)
			ValidateControls(sourceNode["children"][static_cast<Json::ArrayIndex>(i)],node.children[i],path+"/children/"+std::to_string(i),ancestorControl || node.control.has_value());
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
	size_t expressionCount = 0;
	size_t eventStepCount = 0;
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
	const std::string tag = node.type == "vector" ? "q4-vector" : "q4-node";
	output += "<"+tag+" id=\""+node.id+"\" style=\"";
	// Canonical opacity isolates the complete node subtree. RmlUi's ordinary
	// opacity is an inherited primitive tint; its filter supplies the needed
	// stacking/render boundary without changing the editable source format.
	output += "opacity:1;";
	// Button parts (focus rail, marker, label) use the button's local box even
	// when the button itself participates in normal document flow.
	if (node.control) output += "position:relative;";
	if (node.mask) output += "mask-image:q4-mask(alpha);";
	for (const auto& [name,value] : node.properties) if (name != "text") {
		if (name == "opacity") { if (value.data[0] < 1) output += "filter:opacity("+value.Css()+");"; }
		else output += name+":"+value.Css()+";";
	}
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
bool ValidStateValue(const StateValue& value) {
	if (const auto number = std::get_if<double>(&value)) return std::isfinite(*number) && std::abs(*number) <= 1000000000000.0;
	if (const auto text = std::get_if<std::string>(&value)) { size_t bad = 0; return text->size() <= 65536 && Utf8(*text,bad); }
	return true;
}
bool ParseStateValues(const std::string& source, StateValues& values, std::vector<Diagnostic>& diagnostics) {
	diagnostics.clear(); Json::Value root;
	if (!Parse(source,root,diagnostics)) return false;
	if (!root.isObject() || root.size() > 4096) { Diagnose(diagnostics,source,"","Expected at most 4096 state values",0); return false; }
	StateValues candidate;
	for (const auto& name : root.getMemberNames()) {
		const auto& value = root[name]; StateValue parsed;
		if (value.isBool()) parsed = value.asBool();
		else if (value.isNumeric()) parsed = value.asDouble();
		else if (value.isString()) parsed = value.asString();
		else { Diagnose(diagnostics,source,"/"+PointerPart(name),"State values must be numbers, booleans or strings",value.getOffsetStart()); return false; }
		if (!Identifier(name) || !ValidStateValue(parsed)) {
			Diagnose(diagnostics,source,"/"+PointerPart(name),"Invalid state ID/type/value",value.getOffsetStart()); return false;
		}
		candidate.emplace(name,std::move(parsed));
	}
	values = std::move(candidate); return true;
}
const Node* DocumentModel::FindNode(const std::string& id) const { return Find(root,id); }
std::optional<PresentationType> PresentationAliasType(const DocumentModel& model, const std::string& name) {
	const auto key = PresentationAliasKey(name);
	if (key.empty()) return std::nullopt;
	const auto found = model.aliases.find(key);
	if (found == model.aliases.end()) return std::nullopt;
	const auto& alias = found->second;
	if (!alias.variable.empty()) {
		const auto variable = model.presentationVariables.find(alias.variable);
		if (!alias.node.empty() || !alias.property.empty() || !alias.shown.empty() || variable == model.presentationVariables.end() ||
			!ValidPresentationValue(variable->second.initial)) return std::nullopt;
		return variable->second.initial.type;
	}
	const auto* node = model.FindNode(alias.node);
	if (!node || (alias.property != "visible" && !alias.shown.empty())) return std::nullopt;
	if (alias.property == "rect") {
		for (const auto* property : {"left","top","width","height"}) {
			const auto value = node->properties.find(property);
			if (value == node->properties.end() || value->second.type != ValueType::Length ||
				value->second.unit != "dp" || !ValidProperty(property,value->second)) return std::nullopt;
		}
		return PresentationType::Vector4;
	}
	const std::string property = alias.property == "visible" ? "display" : alias.property == "noevents" ? "pointer-events" : alias.property;
	const auto value = node->properties.find(property);
	if (value == node->properties.end() || !ValidProperty(property,value->second)) return std::nullopt;
	const auto& base = value->second;
	if (alias.property == "visible" || alias.property == "noevents") {
		if (base.type != ValueType::Keyword) return std::nullopt;
		if (alias.property == "visible") {
			Value shown = base; shown.text = alias.shown;
			if (shown.text == "none" || !ValidProperty("display",shown)) return std::nullopt;
		}
		return PresentationType::Boolean;
	}
	if (base.type == ValueType::Number || (base.type == ValueType::Length && (base.unit == "dp" || base.unit == "px"))) return PresentationType::Number;
	if (base.type == ValueType::Colour) return PresentationType::Vector4;
	if (base.type == ValueType::Text || base.type == ValueType::Keyword || base.type == ValueType::Font) return PresentationType::String;
	return std::nullopt;
}
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
	// Native unstyled scrollbars default to the containing width and can consume
	// the entire client area. Scrolling stays semantic until authored scrollbar
	// parts provide the editable vector control and its pointer interactions.
	std::string result = "<rml><head><style>body{margin:0;width:100%;height:100%;font-family:marine;font-size:16dp;color:#fff;}div,q4-node,q4-vector{display:block;}scrollbarvertical{width:0;}scrollbarhorizontal{height:0;}</style></head><body>";
	MarkupNode(impl->model.root,result);
	return result+"</body></rml>";
}

} // namespace openq4::ui
