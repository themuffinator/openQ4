// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Document.h"
#include "DocumentSource.h"
#include "FloatChars.h"
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

		static const std::set<std::string> lengths = {"left","right","top","bottom","width","height","min-width","max-width","min-height","max-height","padding","padding-left","padding-right","padding-top","padding-bottom","margin","margin-left","margin-right","margin-top","margin-bottom","font-size","line-height","letter-spacing","border-width","border-left-width","border-right-width","border-top-width","border-bottom-width","row-gap","column-gap","backdrop-blur"};
		static const std::set<std::string> colours = {"color","background-color","border-color","border-left-color","border-right-color","border-top-color","border-bottom-color","image-color"};
		static const std::map<std::string,std::set<std::string>> keywords = {
			{"position",{"absolute","relative"}}, {"display",{"block","inline","inline-block","flex","none"}},
			{"pointer-events",{"auto","none"}},
			{"image-fit",{"fill","contain","cover","scale-none","scale-down"}}, {"image-blend",{"normal","additive"}},
			{"image-align-x",{"left","center","right"}}, {"image-align-y",{"top","center","bottom"}},
			{"overflow",{"visible","hidden","auto","scroll"}}, {"text-align",{"left","center","right"}},
			{"white-space",{"normal","pre","nowrap","pre-wrap","pre-line"}},
			{"word-break",{"normal","break-all","break-word"}},
			// Text cut by its node's overflow ends in an ellipsis rather than mid-letter.
			{"text-overflow",{"clip","ellipsis"}},
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
			if (name == "backdrop-blur" && value.unit != "dp" && value.unit != "px") valid = false;
		} else if (colours.contains(name)) valid = value.type == ValueType::Colour;
		else if (auto entry = keywords.find(name); entry != keywords.end()) valid = value.type == ValueType::Keyword && entry->second.contains(value.text);
		else if (name == "opacity" || name == "backdrop-saturate" || name == "backdrop-streak") valid = value.type == ValueType::Number && value.data[0] >= 0 && value.data[0] <= 1;
		else if (name == "flex-grow" || name == "flex-shrink") valid = value.type == ValueType::Number && value.data[0] >= 0;
		else if (name == "image-offset-x" || name == "image-offset-y") valid = value.type == ValueType::Number;
		else if (name == "image-scale-x" || name == "image-scale-y") valid = value.type == ValueType::Number && value.data[0] > 0;
		else if (name == "font-family") valid = value.type == ValueType::Font;
		else if (name == "text") valid = value.type == ValueType::Text;
		else if (name == "transform") valid = value.type == ValueType::Transform;
		else if (name == "image") valid = value.type == ValueType::Image && ValidImageSource(value.text);
		return valid;
	}
std::string BackdropFilterCss(const Value* blur, const Value* saturate, const Value* streak) {
	const bool blurs = blur && blur->data[0] > 0, desaturates = saturate && saturate->data[0] < 1;
	const bool streaks = streak && streak->data[0] > 0;
	if (!blurs && !desaturates && !streaks) return "none";
	std::string css = blurs ? "blur("+blur->Css()+")" : "";
	if (desaturates) css += std::string(css.empty() ? "" : " ")+"saturate("+saturate->Css()+")";
	if (streaks) css += std::string(css.empty() ? "" : " ")+"streak("+streak->Css()+")";
	return css;
}
bool ValidImageSource(const std::string& source) {
	if (source.empty()) return true;
	if (source.size() > 256 || source.front() == '/' || source.back() == '/' || source.front() == '.' ||
		source.find("..") != std::string::npos || source.find("//") != std::string::npos) return false;
	for (unsigned char c : source) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return false;
	return true;
}
namespace {
constexpr size_t MaxSourceBytes = 16 * 1024 * 1024;
std::string Number(double value) {
	char buffer[64];
	const auto result = FloatToChars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 10);
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
	if (text.find_first_of("~/") == std::string::npos) return text;
	std::string result;
	for (char c : text) {
		if (c == '~') result += "~0";
		else if (c == '/') result += "~1";
		else result += c;
	}
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
bool Utf8(std::string_view text, size_t& bad) {
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
// JsonCpp is permissive about numeric spellings and surrogate pairs (and,
// before 1.9.8, raw string controls). Enforce JSON's lexical forms before its
// DOM parser.
// `treeCheck` is set when a token could parse to a value the parsed tree
// must still be checked for: a number whose magnitude may overflow a double
// (its integer digits and exponent reach 309) or a NUL escape. Every other
// value of a source this and Utf8 accept is finite and valid, so a document
// without such a token skips the validator's walk of its tree.
bool LexicalForms(const std::string& source, size_t& bad, bool& treeCheck) {
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
			long magnitude = 0; // Integer digits plus the exponent: the value is below 10 to this power.
			if (source[p] == '0') ++p; else while (digit()) { ++p; if (magnitude < 100000) ++magnitude; }
			if (p < source.size() && source[p] == '.') { ++p; if (!digit()) return false; while (digit()) ++p; }
			if (p < source.size() && (source[p] == 'e' || source[p] == 'E')) {
				++p;
				bool negative = false;
				if (p < source.size() && (source[p] == '+' || source[p] == '-')) negative = source[p++] == '-';
				if (!digit()) return false;
				long exponent = 0;
				while (digit()) { if (exponent < 100000) exponent = exponent*10+(source[p]-'0'); ++p; }
				magnitude += negative ? -exponent : exponent;
			}
			if (magnitude >= 309) treeCheck = true;
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
		if (cp == 0) treeCheck = true;
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
// `treeCheck`, when given, receives whether the parsed tree needs the
// validator's finiteness and string check (LexicalForms).
bool Parse(const std::string& source, Json::Value& root, std::vector<Diagnostic>& diagnostics, bool* treeCheck = nullptr) {
	if (source.size() > MaxSourceBytes) {
		Diagnose(diagnostics,source,"","Document exceeds the 16 MiB source limit",0); return false;
	}
	size_t bad = 0;
	bool suspect = false;
	if (!Utf8(source,bad) || !LexicalForms(source,bad,suspect)) {
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
	std::string paddedBom(0,'\0');
	const char* input = source.data();
	if (source.starts_with("\xef\xbb\xbf")) { paddedBom = source; paddedBom.replace(0,3,"   "); input = paddedBom.data(); }
	if (treeCheck) *treeCheck = suspect;
	try {
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
		if (reader->parse(input,input+source.size(),&root,nullptr)) return true;
		for (const auto& error : reader->getStructuredErrors())
			Diagnose(diagnostics,source,"",error.message,static_cast<size_t>(std::max<ptrdiff_t>(0,error.offset_start)));
	} catch (const std::exception& e) { Diagnose(diagnostics,source,"",e.what(),0); }
	return false;
}
struct ValidationError {};
const std::map<std::string,std::vector<std::string>>& PropertyShorthands() {
	static const std::map<std::string,std::vector<std::string>> shorthands = {
		{"margin",{"margin-top","margin-right","margin-bottom","margin-left"}},
		{"padding",{"padding-top","padding-right","padding-bottom","padding-left"}},
		{"border-width",{"border-top-width","border-right-width","border-bottom-width","border-left-width"}},
		{"border-color",{"border-top-color","border-right-color","border-bottom-color","border-left-color"}}};
	return shorthands;
}
// The leaves of a shorthand property, or null for a property that is its own leaf.
const std::vector<std::string>* ShorthandLeaves(const std::string& name) {
	const auto& shorthands = PropertyShorthands();
	const auto found = shorthands.find(name);
	return found == shorthands.end() ? nullptr : &found->second;
}
// A string value without copying it.
std::string_view StringView(const Json::Value& value) {
	const char* begin = nullptr; const char* end = nullptr;
	return value.isString() && value.getString(&begin,&end) ? std::string_view(begin,static_cast<size_t>(end-begin)) : std::string_view();
}
// A passing check builds neither its pointer nor its message. A document runs
// hundreds of thousands of checks, and building each one's diagnostic before
// it ran was most of the validation time.
#define Require(condition, at, path, message) do { if (!(condition)) Fail(at,path,message); } while (false)
class Validator {
public:
	// Reads into `output`, the document's own model, so the parts that keep
	// the model (the initial state checked below, the runtime's) share it.
	Validator(const std::string& text, std::vector<Diagnostic>& errors, std::shared_ptr<DocumentModel> output)
		: source(text), diagnostics(errors), owner(std::move(output)), model(*owner) {}
	// `treeCheck`: the source has a token that may have parsed to a non-finite
	// number or an invalid string (Parse); otherwise the tree walk finds none.
	// Returns the validated initial state, which the document keeps.
	State Read(const Json::Value& root, bool treeCheck) {
		if (treeCheck) FiniteTree(root);
		Fields(root,"",{"format","version","id","canvas","tokens","symbols","root","timelines","state","bindings","actions","presentationVariables","aliases","events","detail","editor","extensions"});
		Require(root["format"] == "openq4-ui",root,"/format","Expected format 'openq4-ui'");
		Require(root["version"].isUInt() && root["version"].asUInt() == 1,root["version"],"/version","Unsupported document version; expected 1");
		model.id = Id(root["id"],"/id");
		if (root.isMember("extensions")) {
			Json::StreamWriterBuilder writer; writer["indentation"] = ""; writer["precision"] = 17; writer["commentStyle"] = "None";
			model.extensions = Json::writeString(writer,root["extensions"]);
		}
		if (root.isMember("editor")) Require(root["editor"].isObject(),root["editor"],"/editor","Editor metadata must be an object");
		if (root.isMember("canvas")) {
			// A view-height canvas: the document's dp scale follows the view's height.
			Fields(root["canvas"],"/canvas",{"height","minWidth","extensions"});
			model.canvasHeight = Numeric(root["canvas"]["height"],"/canvas/height",240,4320);
			if (root["canvas"].isMember("minWidth")) model.canvasMinWidth = Numeric(root["canvas"]["minWidth"],"/canvas/minWidth",240,7680);
		}
		if (root.isMember("tokens")) {
			Require(root["tokens"].isObject(),root["tokens"],"/tokens","Expected a token object");
			for (const auto& name : root["tokens"].getMemberNames()) {
				Require(Identifier(name),root["tokens"][name],"/tokens","Invalid token ID");
				model.tokens[name] = Typed(root["tokens"][name],"/tokens/"+PointerPart(name),false);
			}
		}
		ReadState(root);
		ReadPresentationVariables(root);
		if (root.isMember("symbols")) {
			// Vector art that many nodes draw, such as a key glyph a page repeats
			// in every row: a vector node that names a symbol draws its paths, so
			// a document carries the art once.
			const auto& shared = root["symbols"];
			Require(shared.isObject(),shared,"/symbols","Expected a symbol object");
			for (const auto& name : shared.getMemberNames()) {
				const auto at = "/symbols/"+PointerPart(name);
				Require(Identifier(name),shared[name],at,"Invalid symbol ID");
				Require(shared[name].isArray(),shared[name],at,"A symbol is a path array");
				std::set<std::string> ids;
				auto& paths = symbols[name];
				paths.reserve(shared[name].size());
				for (Json::ArrayIndex i = 0; i < shared[name].size(); ++i) {
					const auto p = at+"/"+std::to_string(i);
					auto shape = ReadPath(shared[name][i],p);
					Require(ids.insert(shape.id).second,shared[name][i]["id"],p+"/id","Duplicate path ID within symbol");
					paths.push_back(std::move(shape));
				}
			}
		}
		model.root = ReadNode(root["root"],"/root",0);
		model.IndexNodes(); // The tree is complete; validation resolves its references.
		if (root.isMember("timelines")) {
			Require(root["timelines"].isArray(),root["timelines"],"/timelines","Expected a timeline array");
			std::set<std::string> ids;
			model.timelines.reserve(root["timelines"].size());
			for (Json::ArrayIndex i = 0; i < root["timelines"].size(); ++i) {
				auto timeline = ReadTimeline(root["timelines"][i],"/timelines/"+std::to_string(i));
				Require(ids.insert(timeline.id).second,root["timelines"][i],"/timelines/"+std::to_string(i),"Duplicate timeline ID");
				model.timelines.push_back(std::move(timeline));
			}
		}
		for (const auto& timeline : model.timelines) for (const auto& track : timeline.tracks) trackTargets.insert({track.node,track.property});
		// The timelines are complete: index them for the lookups below, which
		// searched the whole list for each of a control's five states.
		for (const auto& timeline : model.timelines) timelineIndex.emplace(timeline.id,&timeline);
		ReadBindings(root);
		ReadAliases(root);
		ReadActions(root);
		ReadEvents(root);
		ValidateCompletions(root);
		ValidateControls(root["root"],model.root,"/root",false);
		ReadDetail(root);
		State initial; std::string stateError;
		Require(initial.Reset(std::shared_ptr<const DocumentModel>(owner),stateError),root["bindings"],"/bindings",stateError);
		std::string lastModal;
		ValidateInitialModals(root["root"],model.root,"/root",initial.Properties(),true,{},lastModal);
		return initial;
	}
private:
	// The detail card: a declared numeric state, a card node holding no
	// controls whose left and top the runtime owns, and a definition whenever a
	// control names a detail. Without a card the state alone follows the
	// subject, for content the page keeps in place.
	void ReadDetail(const Json::Value& root) {
		if (!root.isMember("detail")) {
			Require(detailControls.empty(),root,detailControls.empty() ? "" : detailControls.front().second,
				"A control's detail requires the document's detail definition");
			return;
		}
		const auto& detail = root["detail"];
		Fields(detail,"/detail",{"state","card","none","extensions"});
		DetailSpec spec;
		spec.state = Id(detail["state"],"/detail/state");
		if (detail.isMember("none")) spec.none = Numeric(detail["none"],"/detail/none",-1e9,1e9);
		const auto declared = model.state.find(spec.state);
		Require(declared != model.state.end() && std::holds_alternative<double>(declared->second.initial),detail["state"],"/detail/state",
			"The detail state must be a declared number");
		if (!detail.isMember("card")) {
			model.detail = std::move(spec);
			return;
		}
		spec.card = Id(detail["card"],"/detail/card");
		const auto* card = model.FindNode(spec.card);
		Require(card != nullptr && card->id != model.root.id,detail["card"],"/detail/card","The detail card must be a node below the root");
		Require(!HasControl(*card),detail["card"],"/detail/card","The detail card cannot hold controls");
		for (const auto* property : {"left","top"}) {
			Require(!trackTargets.contains({card->id,property}),detail["card"],"/detail/card","The runtime places the detail card; its left and top cannot be animated");
			for (const auto& binding : model.bindings)
				Require(!(binding.node == card->id && binding.property == property),detail["card"],"/detail/card","The runtime places the detail card; its left and top cannot be bound");
		}
		for (const auto& [id,path] : detailControls)
			Require(!Descendant(id,card->id),root,path,"A control naming a detail cannot sit inside the detail card");
		model.detail = std::move(spec);
	}
	static bool HasControl(const Node& node) {
		if (node.control) return true;
		for (const auto& child : node.children) if (HasControl(child)) return true;
		return false;
	}
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
			Fields(v,p,{"type","initial","cvar","view","keyBinding","extensions"});
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
			if (v.isMember("view")) {
				static const std::set<std::string> views{"aspect","text","safeLeft","safeTop","safeRight","safeBottom"};
				Require(v["view"].isString() && views.contains(v["view"].asString()),v["view"],p+"/view",
					"Expected the view source 'aspect', 'text', 'safeLeft', 'safeTop', 'safeRight' or 'safeBottom'");
				Require(type == "number" && !v.isMember("cvar"),v,p,"A view source is a number state without a CVar source");
				declaration.view = v["view"].asString();
			}
			if (v.isMember("keyBinding")) {
				const auto& binding = v["keyBinding"];
				Require(type == "string" && !v.isMember("cvar") && !v.isMember("view"),v,p,
					"A key binding requires an application string state");
				Require(binding.isString(),binding,p+"/keyBinding","Expected a key binding lookup string");
				declaration.keyBinding = binding.asString();
				Require(!declaration.keyBinding.empty() && declaration.keyBinding.size() <= 1024 &&
					ValidStateValue(declaration.keyBinding) &&
					declaration.keyBinding.find('\0') == std::string::npos &&
					declaration.keyBinding.find_first_of("\r\n") == std::string::npos,binding,p+"/keyBinding",
					"Expected a nonempty single-line key binding of at most 1024 bytes");
			}
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
			result.args.reserve(value["args"].size());
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
		result.reserve(values.size());
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
					Require(found != model.state.end() && !found->second.HostSourced(),batch[id],p,"Event writes require declared application-owned state");
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
			} else if (op == "focus") {
				Fields(value,at,{"op","control","extensions"}); step.op = EventOp::Focus;
				step.target = Id(value["control"],at+"/control");
				const auto* node = model.FindNode(step.target);
				Require(node && node->control,value["control"],at+"/control","An event can focus only a declared control");
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
	// A completion program runs from the presentation frame, with no application
	// caller to deliver host work to: it may change state, presentation and
	// motion but never invoke an action. Completions also may not chain back to
	// a timeline already on their path, so one frame always settles.
	void ValidateCompletions(const Json::Value& root) {
		std::set<std::string> settled;
		for (size_t i = 0; i < model.timelines.size(); ++i) {
			const auto& timeline = model.timelines[i];
			if (timeline.complete.empty()) continue;
			const auto& at = root["timelines"][static_cast<Json::ArrayIndex>(i)]["complete"];
			const auto path = "/timelines/"+std::to_string(i)+"/complete";
			Require(model.events.contains(timeline.complete),at,path,"Unknown completion event");
			std::set<std::string> visited;
			Require(!InvokesAction(timeline.complete,visited),at,path,"A completion program cannot invoke actions");
			std::vector<std::string> chain{timeline.id};
			Require(!CompletionCycle(timeline,chain,settled),at,path,"Completion programs lead back to a timeline already completing");
		}
	}
	bool InvokesAction(const std::string& event, std::set<std::string>& visited) const {
		if (!visited.insert(event).second) return false;
		const std::function<bool(const std::vector<EventStep>&)> scan = [&](const std::vector<EventStep>& steps) {
			for (const auto& step : steps) {
				if (step.op == EventOp::Action) return true;
				if (step.op == EventOp::Call && InvokesAction(step.target,visited)) return true;
				if (step.op == EventOp::If && (scan(step.thenSteps) || scan(step.elseSteps))) return true;
			}
			return false;
		};
		return scan(model.events.at(event).steps);
	}
	// Every timeline a program can play, through calls and both branches.
	void PlayedTimelines(const std::string& event, std::set<std::string>& visited, std::set<std::string>& played) const {
		if (!visited.insert(event).second) return;
		const std::function<void(const std::vector<EventStep>&)> scan = [&](const std::vector<EventStep>& steps) {
			for (const auto& step : steps) {
				if (step.op == EventOp::PlayTimeline) played.insert(step.target);
				if (step.op == EventOp::Call) PlayedTimelines(step.target,visited,played);
				if (step.op == EventOp::If) { scan(step.thenSteps); scan(step.elseSteps); }
			}
		};
		scan(model.events.at(event).steps);
	}
	bool CompletionCycle(const Timeline& timeline, std::vector<std::string>& chain, std::set<std::string>& settled) const {
		if (timeline.complete.empty() || settled.contains(timeline.id)) return false;
		std::set<std::string> visited, played;
		PlayedTimelines(timeline.complete,visited,played);
		for (const auto& id : played) {
			if (std::find(chain.begin(),chain.end(),id) != chain.end()) return true;
			const auto* next = FindTimeline(id);
			chain.push_back(id);
			if (next && CompletionCycle(*next,chain,settled)) return true;
			chain.pop_back();
		}
		settled.insert(timeline.id);
		return false;
	}
	// Application strings are checked when a binding evaluates; literals here.
	bool ImageSourceResult(const Expression& expression) const {
		if (!expression.state.empty() || !expression.presentation.empty()) return true;
		if (expression.op == "select") return ImageSourceResult(expression.args[1]) && ImageSourceResult(expression.args[2]);
		if (!expression.op.empty() || expression.type != 2) return false;
		return ValidImageSource(std::get<std::string>(expression.literal));
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
		model.bindings.reserve(bindings.size());
		for (Json::ArrayIndex i = 0; i < bindings.size(); ++i) {
			const auto& value = bindings[i]; const auto path = "/bindings/"+std::to_string(i);
			Fields(value,path,{"id","node","property","value","extensions"});
			Binding binding; binding.id = Id(value["id"],path+"/id"); binding.node = Id(value["node"],path+"/node"); binding.property = Id(value["property"],path+"/property");
			Require(ids.insert(binding.id).second,value,path,"Duplicate binding ID");
			const Node* node = model.FindNode(binding.node);
			Require(node != nullptr,value["node"],path+"/node","Unknown binding target");
			const bool enabled = binding.property == "enabled";
			Require(!enabled || node->control.has_value(),value,path,"Enabled binding requires a semantic control");
			// A shorthand binds each of its leaves; any other property binds itself.
			const auto* leaves = enabled ? nullptr : ShorthandLeaves(binding.property);
			const auto readProperty = [&](const std::string& property) {
				Require(enabled || node->properties.contains(property),value,path,"Bound properties require explicit base values");
				Require(targets.insert({binding.node,property}).second,value,path,"Multiple bindings own the same effective property");
				Require(!trackTargets.contains({binding.node,property}),value,path,"A binding and timeline cannot own the same property; use separate presentation/content nodes");
				Binding effective = binding; effective.property = property;
				if (!enabled) effective.prototype = node->properties.at(property);
				const auto type = effective.prototype.type;
				const size_t expected = enabled ? 1 : (type == ValueType::Text || type == ValueType::Keyword || type == ValueType::Font || type == ValueType::Image) ? 2 : 0;
				const size_t count = !enabled && type == ValueType::Colour ? 4 : !enabled && type == ValueType::Transform ? 5 : 1;
				Require(count == 1 || (value["value"].isArray() && value["value"].size() == count),value["value"],path+"/value","Binding component count does not match its target");
				for (size_t component = 0; component < count; ++component) {
					const auto at = count == 1 ? path+"/value" : path+"/value/"+std::to_string(component);
					auto expression = ReadExpression(count == 1 ? value["value"] : value["value"][static_cast<Json::ArrayIndex>(component)],at);
					Require(expression.type == expected,value["value"],at,"Expression type does not match its target");
					if (!enabled && type == ValueType::Text) Require(LocalizedTextResult(expression),value["value"],at,"Literal display text must use localization keys");
					if (!enabled && type == ValueType::Image) Require(ImageSourceResult(expression),value["value"],at,"Literal image sources must be relative VFS image paths");
					effective.values.push_back(std::move(expression));
				}
				model.bindings.push_back(std::move(effective));
			};
			if (leaves) for (const auto& property : *leaves) readProperty(property);
			else readProperty(binding.property);
		}
		bindingTargets = std::move(targets);
	}
	[[noreturn]] void Fail(const Json::Value& at, const std::string& path, const std::string& message) {
		Diagnose(diagnostics,source,path,message,static_cast<size_t>(std::max<ptrdiff_t>(0,at.getOffsetStart())));
		throw ValidationError{};
	}
	static std::string_view MemberName(const Json::Value::const_iterator& member) {
		const char* end = nullptr;
		const char* begin = member.memberName(&end);
		return begin ? std::string_view(begin,static_cast<size_t>(end-begin)) : std::string_view();
	}
	void Fields(const Json::Value& value, const std::string& path, std::initializer_list<std::string_view> allowed) {
		Require(value.isObject(),value,path,"Expected an object");
		for (auto member = value.begin(); member != value.end(); ++member)
			Require(std::find(allowed.begin(),allowed.end(),MemberName(member)) != allowed.end(),*member,path+"/"+PointerPart(std::string(MemberName(member))),"Unknown field; extension data belongs in 'extensions'");
		if (value.isMember("extensions")) Require(value["extensions"].isObject(),value["extensions"],path+"/extensions","Extensions must be an object");
	}
	// The first non-finite number, invalid string or invalid key, visiting
	// members in key order and keys before their values; a key's fault is its
	// object's. A passing tree builds no pointers.
	static const Json::Value* FiniteFault(const Json::Value& v, const char*& message) {
		size_t bad = 0;
		if (v.isNumeric() && !std::isfinite(v.asDouble())) { message = "Non-finite numbers are not supported"; return &v; }
		const char* begin = nullptr; const char* end = nullptr;
		if (v.isString() && v.getString(&begin,&end) && !Utf8(std::string_view(begin,static_cast<size_t>(end-begin)),bad)) {
			message = "Invalid Unicode or NUL in string"; return &v;
		}
		if (v.isObject()) for (auto member = v.begin(); member != v.end(); ++member) {
			if (!Utf8(MemberName(member),bad)) { message = "Invalid Unicode or NUL in object key"; return &v; }
			if (const auto* fault = FiniteFault(*member,message)) return fault;
		}
		if (v.isArray()) for (Json::ArrayIndex i = 0; i < v.size(); ++i) if (const auto* fault = FiniteFault(v[i],message)) return fault;
		return nullptr;
	}
	static bool PointerTo(const Json::Value& v, const Json::Value* target, std::string& path) {
		if (&v == target) return true;
		const size_t length = path.size();
		if (v.isObject()) for (auto member = v.begin(); member != v.end(); ++member) {
			path += "/"+PointerPart(std::string(MemberName(member)));
			if (PointerTo(*member,target,path)) return true;
			path.resize(length);
		}
		if (v.isArray()) for (Json::ArrayIndex i = 0; i < v.size(); ++i) {
			path += "/"+std::to_string(i);
			if (PointerTo(v[i],target,path)) return true;
			path.resize(length);
		}
		return false;
	}
	void FiniteTree(const Json::Value& root) {
		const char* message = nullptr;
		const auto* fault = FiniteFault(root,message);
		if (!fault) return;
		std::string path;
		PointerTo(root,fault,path);
		Fail(*fault,path,message);
	}
	std::string Id(const Json::Value& value, const std::string& path) {
		Require(value.isString() && Identifier(value.asString()),value,path,"Expected a stable ID (1..128 ASCII letters, digits, '.', '_' or '-')");
		return value.asString();
	}
	// The hot readers name a value by its parent's pointer and a leaf, so a
	// passing check builds no pointer; a failing one reports the same pointer
	// and message as the string overload.
	std::string Id(const Json::Value& value, const std::string& parent, const char* leaf) {
		if (value.isString()) { std::string id = value.asString(); if (Identifier(id)) return id; }
		return Id(value,parent+leaf);
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
	double Numeric(const Json::Value& value, const std::string& parent, const char* leaf, double low, double high) {
		if (value.isNumeric()) { const double result = value.asDouble(); if (std::isfinite(result) && result >= low && result <= high) return result; }
		return Numeric(value,parent+leaf,low,high);
	}
	// A pointer's indexed leaf ("/points/3"), built only by a failing check.
	static std::string Indexed(const std::string& parent, const char* leaf, Json::ArrayIndex index) {
		return parent+leaf+std::to_string(index);
	}
	// An object's members found in one walk, for the readers of the many
	// small objects (typed values, path commands) that otherwise look each
	// field up: the named fields, by position in `names`, and the first member
	// outside them in key order. A missing field reads as JSON null, as a
	// lookup of it would.
	struct Members {
		std::array<const Json::Value*,4> fields{};
		const Json::Value* other = nullptr;
		std::string_view otherName;
		const Json::Value& Field(size_t index) const { return fields[index] ? *fields[index] : Json::Value::nullSingleton(); }
	};
	static Members FindMembers(const Json::Value& value, std::initializer_list<std::string_view> names) {
		Members result;
		if (!value.isObject()) return result;
		for (auto member = value.begin(); member != value.end(); ++member) {
			const auto name = MemberName(member);
			size_t index = 0; bool named = false;
			for (const auto& candidate : names) { if (name == candidate) { result.fields[index] = &*member; named = true; break; } ++index; }
			if (!named && !result.other) { result.other = &*member; result.otherName = name; }
		}
		return result;
	}
	// Fields() for members found at once: the first member outside the
	// allowed set fails, in key order (`disallowed` is a named field the
	// allowed set leaves out), then extensions must be an object.
	void CheckFields(const Members& members, const std::string& path, const Json::Value* extensions,
		const Json::Value* disallowed = nullptr, std::string_view disallowedName = {}) {
		const Json::Value* unknown = members.other; std::string_view unknownName = members.otherName;
		if (disallowed && (!unknown || disallowedName < unknownName)) { unknown = disallowed; unknownName = disallowedName; }
		Require(!unknown,*unknown,path+"/"+PointerPart(std::string(unknownName)),"Unknown field; extension data belongs in 'extensions'");
		if (extensions) Require(extensions->isObject(),*extensions,path+"/extensions","Extensions must be an object");
	}
	Value Typed(const Json::Value& value, const std::string& path, bool allowToken = true) {
		const auto members = FindMembers(value,{"type","value","unit","extensions"});
		const auto* typeField = members.fields[0];
		Require(typeField && typeField->isString(),value,path,"Expected a typed value object");
		const auto type = StringView(*typeField);
		const bool length = type == "length", transform = type == "transform";
		// A unit belongs to lengths and transforms only.
		CheckFields(members,path,members.fields[3],length || transform ? nullptr : members.fields[2],"unit");
		const auto& inner = members.Field(1);
		Value result;
		if (type == "token") {
			const auto id = Id(inner,path,"/value");
			Require(allowToken,value,path,"Tokens must contain a concrete value, not another token reference");
			auto token = model.tokens.find(id);
			Require(token != model.tokens.end(),value,path,"Unknown design token '"+id+"'");
			return token->second;
		}
		if (length || transform) {
			const auto& unit = members.Field(2);
			Require(unit.isString(),value,path+"/unit","Length/transform requires an explicit unit");
			result.unit = unit.asString();
			Require(result.unit == "dp" || result.unit == "px" || (length && (result.unit == "%" || result.unit == "em")),unit,path+"/unit","Expected dp, px, em or a layout percentage; transforms require dp or px");
		}
		if (type == "number" || length) {
			result.type = length ? ValueType::Length : ValueType::Number;
			result.data[0] = Numeric(inner,path,"/value",-1000000,1000000);
		} else if (type == "color" || transform) {
			result.type = transform ? ValueType::Transform : ValueType::Colour;
			const unsigned count = transform ? 5 : 4;
			Require(inner.isArray() && inner.size() == count,inner,path+"/value","Incorrect component count");
			for (unsigned i = 0; i < count; ++i) {
				const auto& component = inner[i];
				const double low = transform ? -1000000 : 0, high = transform ? 1000000 : 1;
				if (component.isNumeric()) { const double number = component.asDouble(); if (std::isfinite(number) && number >= low && number <= high) { result.data[i] = number; continue; } }
				result.data[i] = Numeric(component,Indexed(path,"/value/",i),low,high);
			}
		} else {
			Require(type == "keyword" || type == "font" || type == "text" || type == "image",value,path+"/type","Unsupported value type '"+std::string(type)+"'");
			Require(inner.isString(),inner,path+"/value","Expected a string");
			result.text = inner.asString();
			result.type = type == "keyword" ? ValueType::Keyword : type == "font" ? ValueType::Font :
				type == "image" ? ValueType::Image : ValueType::Text;
			if (type == "text") Require(result.text.starts_with("#str_") && Identifier(result.text.substr(1)),inner,path+"/value","Display text must reference a #str_ localization key");
			else if (type == "image") Require(ValidImageSource(result.text),inner,path+"/value","Image sources are empty or a relative VFS image path of letters, digits, '_', '-', '.' and '/'");
			else Require(Identifier(result.text),inner,path+"/value","Expected a single keyword or font identifier");
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
	// A point of two plain numbers in range, the common case, builds no
	// pointer; any other point takes the general reader with the same pointer.
	PathPoint Point(const Json::Value& value, const std::string& parent, const char* leaf, Json::ArrayIndex index) {
		if (value.isArray() && value.size() == 2) {
			const auto& x = value[0]; const auto& y = value[1];
			if (x.isNumeric() && y.isNumeric()) {
				const double px = x.asDouble(), py = y.asDouble();
				if (std::isfinite(px) && px >= -1000000 && px <= 1000000 && std::isfinite(py) && py >= -1000000 && py <= 1000000) return {{0,px},{0,py}};
			}
		}
		return Point(value,Indexed(parent,leaf,index));
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
		} else if (type == "linear" || type == "radial" || type == "diamond" || type == "conic") {
			Fields(value,path,{"type","from","to","stops","extensions"});
			result.type = type == "radial" ? PaintType::Radial : type == "diamond" ? PaintType::Diamond : type == "conic" ? PaintType::Conic : PaintType::Linear;
			result.from = Point(value["from"],path+"/from"); result.to = Point(value["to"],path+"/to");
			Require(value["stops"].isArray() && value["stops"].size() >= 2 && value["stops"].size() <= 256,value["stops"],path+"/stops","Gradient requires 2..256 stops");
			double previous = -1;
			result.stops.reserve(value["stops"].size());
			for (Json::ArrayIndex i = 0; i < value["stops"].size(); ++i) {
				const auto& stop = value["stops"][i]; const auto p = path+"/stops/"+std::to_string(i);
				Fields(stop,p,{"at","color","extensions"});
				const double at = Numeric(stop["at"],p+"/at",0,1);
				Require(at > previous,stop["at"],p+"/at","Gradient stop positions must increase strictly");
				previous = at; result.stops.push_back({at,Colour(stop["color"],p+"/color")});
			}
			Require(result.stops.front().at == 0 && result.stops.back().at == 1,value["stops"],path+"/stops","Gradient requires stops at both zero and one");
		} else Require(false,value["type"],path+"/type","Unsupported paint type; expected none, solid, linear, radial or diamond");
		return result;
	}
	VectorPath ReadPath(const Json::Value& value, const std::string& path, bool mask = false) {
		Fields(value,path,{"id","commands","fillRule","fill","stroke","blend","antialias","extensions"});
		VectorPath result;
		result.id = Id(value["id"],path+"/id");
		if (value.isMember("antialias")) {
			Require(value["antialias"].isBool(),value["antialias"],path+"/antialias","Expected a boolean antialias");
			result.antialias = value["antialias"].asBool();
		}
		if (value.isMember("fillRule")) {
			Require(value["fillRule"] == "nonzero" || value["fillRule"] == "evenodd",value["fillRule"],path+"/fillRule","Expected nonzero or evenodd fill rule");
			if (value["fillRule"] == "evenodd") result.fillRule = FillRule::EvenOdd;
		}
		if (value.isMember("blend")) {
			Require(value["blend"] == "normal" || value["blend"] == "additive" || value["blend"] == "multiply",value["blend"],path+"/blend","Expected normal, additive or multiply blend");
			// Mask coverage is alpha only; a composition mode cannot apply to it.
			Require(!mask || value["blend"] == "normal",value["blend"],path+"/blend","Mask paths contribute alpha only and cannot blend");
			result.blend = value["blend"] == "additive" ? PathBlend::Additive : value["blend"] == "multiply" ? PathBlend::Multiply : PathBlend::Normal;
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
		const auto& commands = value["commands"];
		Require(commands.isArray() && !commands.empty() && commands.size() <= 65536,commands,path+"/commands","Path requires 1..65536 commands");
		std::set<std::string> ids;
		result.commands.reserve(commands.size());
		for (Json::ArrayIndex i = 0; i < commands.size(); ++i) {
			const auto& command = commands[i]; const auto p = path+"/commands/"+std::to_string(i);
			Require(command.isObject(),command,p,"Expected an object");
			const auto members = FindMembers(command,{"id","op","points","extensions"});
			CheckFields(members,p,members.fields[3]);
			PathCommand parsed; parsed.id = Id(members.Field(0),p,"/id");
			Require(ids.insert(parsed.id).second,members.Field(0),p+"/id","Duplicate path command ID");
			const auto& op = members.Field(1);
			Require(op.isString(),op,p+"/op","Expected path operation");
			const auto name = StringView(op);
			unsigned points = 0;
			if (name == "move") { parsed.op = PathOperation::Move; points = 1; }
			else if (name == "line") { parsed.op = PathOperation::Line; points = 1; }
			else if (name == "quadratic") { parsed.op = PathOperation::Quadratic; points = 2; }
			else if (name == "cubic") { parsed.op = PathOperation::Cubic; points = 3; }
			else if (name == "close") { parsed.op = PathOperation::Close; points = 0; }
			else Require(false,op,p+"/op","Unsupported path operation");
			Require(i != 0 || parsed.op == PathOperation::Move,command,p,"Path must begin with move");
			const auto& pointValues = members.Field(2);
			Require((points == 0 && !members.fields[2]) || (pointValues.isArray() && pointValues.size() == points),pointValues,p+"/points","Incorrect point count for path operation");
			for (unsigned j = 0; j < points; ++j) parsed.points[j] = Point(pointValues[j],p,"/points/",j);
			result.commands.push_back(std::move(parsed));
		}
		return result;
	}
	void Property(const std::string& name, const Value& value, const Json::Value& at, const std::string& path) {
		Require(ValidProperty(name,value),at,path,"Unsupported property or invalid value for '"+name+"'");
	}
	// The pointer built only by a failing check.
	void Property(const std::string& name, const Value& value, const Json::Value& at, const std::string& parent, const char* leaf) {
		Require(ValidProperty(name,value),at,parent+leaf,"Unsupported property or invalid value for '"+name+"'");
	}

	template<std::size_t N>
	void ReadModelVector(const Json::Value& value, const std::string& path, std::array<Expression,N>& out) {
		Require(value.isArray() && value.size()==N,value,path,"Incorrect model vector component count");
		for (Json::ArrayIndex i=0;i<N;++i) {
			out[i]=ReadExpression(value[i],path+"/"+std::to_string(i));
			Require(out[i].type==0,value[i],path,"Model vectors require numeric expressions");
		}
	}
	ModelSceneSpec ReadModelScene(const Json::Value& value, const std::string& path) {
		Require(++modelCount<=32,value,path,"A document supports at most 32 model scenes");
		Fields(value,path,{"parts","lights","origin","rotation","camera","fov","outline","outlineWidth","rimlight","brightSkin","extensions"});
		ModelSceneSpec result;
		const auto& parts=value["parts"];
		Require(parts.isArray() && !parts.empty() && parts.size()<=3,parts,path+"/parts","A model scene needs one root and at most two attachments");
		for (Json::ArrayIndex i=0;i<parts.size();++i) {
			const auto& part=parts[i]; const auto p=path+"/parts/"+std::to_string(i);
			Fields(part,p,{"model","skin","anim","animClass","joint","extensions"});
			Require(part.isMember("model"),part,p,"A model part requires its model expression");
			ModelPartSpec parsed;
			for (const auto& [key,target] : std::array<std::pair<const char*,Expression*>,4>{{
				{"model",&parsed.model},{"skin",&parsed.skin},{"anim",&parsed.animation},{"animClass",&parsed.animationClass}}}) {
				if (part.isMember(key)) *target=ReadExpression(part[key],p+"/"+key);
				Require(target->type==2,part,p,"Model assets require string expressions");
			}
			if (part.isMember("joint")) {
				Require(part["joint"].isString(),part,p+"/joint","An attachment joint must be a string");
				parsed.joint=part["joint"].asString();
			}
			result.parts.push_back(std::move(parsed));
		}
		if (value.isMember("lights")) {
			const auto& lights=value["lights"];
			Require(lights.isArray() && lights.size()<=5,lights,path+"/lights","A model scene supports at most five lights");
			for (Json::ArrayIndex i=0;i<lights.size();++i) {
				const auto& light=lights[i]; const auto p=path+"/lights/"+std::to_string(i);
				Fields(light,p,{"origin","colour","extensions"});
				ModelLightSpec parsed;
				if (light.isMember("origin")) ReadModelVector(light["origin"],p+"/origin",parsed.origin);
				if (light.isMember("colour")) ReadModelVector(light["colour"],p+"/colour",parsed.colour);
				result.lights.push_back(std::move(parsed));
			}
		} else result.lights.emplace_back();
		for (const auto& [key,target] : std::array<std::pair<const char*,std::array<Expression,3>*>,3>{{
			{"origin",&result.origin},{"rotation",&result.rotation},{"camera",&result.camera}}})
			if (value.isMember(key)) ReadModelVector(value[key],path+"/"+key,*target);
		for (const auto& [key,target] : std::array<std::pair<const char*,std::array<Expression,4>*>,3>{{
			{"outline",&result.outline},{"rimlight",&result.rimlight},{"brightSkin",&result.brightSkin}}})
			if (value.isMember(key)) ReadModelVector(value[key],path+"/"+key,*target);
		for (const auto& [key,target] : std::array<std::pair<const char*,Expression*>,2>{{
			{"fov",&result.fov},{"outlineWidth",&result.outlineWidth}}}) {
			if (value.isMember(key)) *target=ReadExpression(value[key],path+"/"+key);
			Require(target->type==0,value,path,"Model camera and effect widths require numeric expressions");
		}
		StateValues initial;
		for (const auto& [id,state]:model.state) initial.emplace(id,state.initial);
		ModelScene resolved; std::string error;
		Require(ResolveModelScene(result,initial,resolved,error),value,path,error);
		return result;
	}
	std::map<std::string,std::vector<VectorPath>> symbols; // The document's shared vector art, by ID.
	Node ReadNode(const Json::Value& value, const std::string& path, unsigned depth) {
		Require(depth <= 48 && ++nodeCount <= 65536,value,path,"Document hierarchy exceeds the node/depth limit");
		Fields(value,path,{"id","type","properties","children","paths","symbol","mask","control","modal","legacyText","scene","extensions"});
		Node result;
		result.id = Id(value["id"],path,"/id");
		Require(nodeIds.insert(result.id).second,value["id"],path+"/id","Duplicate node ID '"+result.id+"'");
		const auto& typeValue = value["type"];
		Require(typeValue == "group" || typeValue == "text" || typeValue == "vector" || typeValue == "image" || typeValue == "model",typeValue,path+"/type","Supported node types are group, text, vector, image and model; unsupported nodes cannot be silently rendered");
		result.type = typeValue.asString();
		if (result.type=="model") result.model=ReadModelScene(value["scene"],path+"/scene");
		else Require(!value.isMember("scene"),value,path+"/scene","A model scene requires a model node");
		if (value.isMember("legacyText")) {
			Require(value["legacyText"].isString() && result.type == "text" &&
				(value["legacyText"].asString()=="window" || value["legacyText"].asString()=="simple"),
				value,path+"/legacyText","Legacy text layout requires a text node and window or simple");
			result.legacyText = true;
			result.legacyTextClip = value["legacyText"].asString()=="window";
		}
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
			if (role == "button") Fields(control,p,{"role","action","event","label","enabled","states","navigation","detail","context","prompt","extensions"});
			else if (role == "toggle") Fields(control,p,{"role","action","label","enabled","states","navigation","value","mixed","parts","extensions"});
			else if (role == "slider") Fields(control,p,{"role","action","label","enabled","states","navigation","value","minimum","maximum","step","decimals","orientation","parts","extensions"});
			else if (role == "choice") Fields(control,p,{"role","action","label","enabled","states","navigation","value","context","parts","visibleRows","options","optionCount","scrollbar","placementBounds","extensions"});
			else if (role == "number") Fields(control,p,{"role","action","label","enabled","states","navigation","value","minimum","maximum","exponent","integer","decimals","maxBytes","parts","extensions"});
			else if (role == "text") Fields(control,p,{"role","action","label","enabled","states","navigation","value","maxBytes","allowEmpty","masked","parts","extensions"});
			else if (role == "scrollbar") Fields(control,p,{"role","label","enabled","states","navigation","viewport","orientation","lineStep","minimumThumb","parts","extensions"});
            else Require(false,control["role"],p+"/role","Supported roles are button, toggle, slider, choice, number, text and scrollbar");
			result.control.emplace(); auto& parsed = *result.control;
			Require(role == "scrollbar" || control.isMember("action") != control.isMember("event"),control,p,"Controls require exactly one action or event");
			if (control.isMember("action")) parsed.action = Id(control["action"],p+"/action");
			else if (role != "scrollbar") parsed.event = EventName(control["event"],p+"/event");
			parsed.label = ControlLabel(control["label"],p+"/label");
			if (control.isMember("context")) {
				parsed.context = ReadExpression(control["context"],p+"/context");
				Require(parsed.context->type == 2,control["context"],p+"/context","Control context must be a string");
			}
			if (control.isMember("prompt")) {
				parsed.prompt = ReadExpression(control["prompt"],p+"/prompt");
				Require(parsed.prompt->type == 2 && LocalizedTextResult(*parsed.prompt),control["prompt"],p+"/prompt",
					"A prompt verb must be a localization key or a string expression");
			}
			if (control.isMember("detail")) {
				parsed.detail = Numeric(control["detail"],p+"/detail",-1e9,1e9);
				detailControls.emplace_back(result.id,p+"/detail");
			}
			if (role == "scrollbar") {
                parsed.role = ControlRole::Scrollbar;ScrollSpec spec;
                spec.viewport = Id(control["viewport"],p+"/viewport");
                Fields(control["parts"],p+"/parts",{"track","thumb","extensions"});
                spec.track = Id(control["parts"]["track"],p+"/parts/track");
                spec.thumb = Id(control["parts"]["thumb"],p+"/parts/thumb");
                if (control.isMember("orientation")) {
                    Require(control["orientation"] == "horizontal" || control["orientation"] == "vertical",control["orientation"],p+"/orientation","Expected horizontal or vertical orientation");
                    spec.vertical = control["orientation"] == "vertical";
                }
                if (control.isMember("lineStep")) spec.lineStep = Numeric(control["lineStep"],p+"/lineStep",1,4096);
                if (control.isMember("minimumThumb")) spec.minimumThumb = Numeric(control["minimumThumb"],p+"/minimumThumb",1,4096);
                parsed.widget = std::move(spec);
            } else if (role != "button") {
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
				} else if (role == "number" || role == "text") {
					// A text field shares the Number editor; its value is its text.
					parsed.role = ControlRole::Number;
					const bool textField = role == "text";
					Require(parsed.value->type == (textField ? 2u : 0u),control["value"],p+"/value",
						textField ? "Text values must be strings" : "Number values must be numeric");
					Fields(parts,at,{"viewport","text","selection","caret","composition","validation","extensions"});
					NumberSpec spec;
					spec.textField = textField;
					if (!textField) {
						spec.minimum = Numeric(control["minimum"],p+"/minimum",-1e12,1e12);
						spec.maximum = Numeric(control["maximum"],p+"/maximum",-1e12,1e12);
						Require(spec.minimum <= spec.maximum,control,p,"Number bounds must be ordered");
					}
					if (control.isMember("exponent")) {
						Require(control["exponent"].isBool(),control["exponent"],p+"/exponent","Number exponent policy must be Boolean");
						spec.exponent = control["exponent"].asBool();
					}
					if (control.isMember("integer")) {
						Require(control["integer"].isBool(),control["integer"],p+"/integer","Number integer policy must be Boolean");
						spec.integer = control["integer"].asBool();
					}
					Require(!spec.integer || std::ceil(spec.minimum) <= std::floor(spec.maximum),control,p,"Integer number bounds must include an integer");
					if (control.isMember("decimals")) {
						Require(control["decimals"].isUInt() && control["decimals"].asUInt() >= 1 && control["decimals"].asUInt() <= 6 &&
							!spec.integer && !spec.exponent,control["decimals"],p+"/decimals",
							"Number decimal places must be 1..6, on a field without integer or exponent policy");
						spec.decimals = static_cast<int>(control["decimals"].asUInt());
					}
					if (control.isMember("maxBytes")) {
						Require(control["maxBytes"].isUInt() && control["maxBytes"].asUInt() >= 1 && control["maxBytes"].asUInt() <= 65536,
							control["maxBytes"],p+"/maxBytes","Number edit size must be 1..65536 bytes");
						spec.maxBytes = control["maxBytes"].asUInt();
					}
					// Text-only policies; Fields refuses them on a number.
					for (const auto& [key,flag] : {std::pair{"allowEmpty",&spec.allowEmpty},std::pair{"masked",&spec.masked}})
						if (control.isMember(key)) {
							Require(control[key].isBool(),control[key],p+"/"+key,"Text field policy must be Boolean");
							*flag = control[key].asBool();
						}
					spec.viewport = Id(parts["viewport"],at+"/viewport"); spec.text = Id(parts["text"],at+"/text");
					spec.selection = Id(parts["selection"],at+"/selection"); spec.caret = Id(parts["caret"],at+"/caret");
					spec.composition = Id(parts["composition"],at+"/composition"); spec.validation = Id(parts["validation"],at+"/validation");
					parsed.widget = std::move(spec);
				} else {
					parsed.role = ControlRole::Choice;
					Fields(parts,at,{"popup","viewport","content","value","extensions"});
					ChoiceSpec spec; spec.popup = Id(parts["popup"],at+"/popup"); spec.viewport = Id(parts["viewport"],at+"/viewport");
					spec.content = Id(parts["content"],at+"/content"); spec.valueText = Id(parts["value"],at+"/value");
					if (control.isMember("placementBounds")) spec.placementBounds = Id(control["placementBounds"],p+"/placementBounds");
					if (control.isMember("scrollbar")) {
						const auto& bar = control["scrollbar"]; const auto where = p+"/scrollbar";
						Fields(bar,where,{"track","thumb","lineStep","minimumThumb","extensions"});
						ScrollSpec scroll; scroll.viewport = spec.viewport;
						scroll.track = Id(bar["track"],where+"/track"); scroll.thumb = Id(bar["thumb"],where+"/thumb");
						if (bar.isMember("lineStep")) scroll.lineStep = Numeric(bar["lineStep"],where+"/lineStep",1,4096);
						if (bar.isMember("minimumThumb")) scroll.minimumThumb = Numeric(bar["minimumThumb"],where+"/minimumThumb",1,4096);
						spec.scrollbar = std::move(scroll);
					}
					if (control.isMember("visibleRows")) {
						Require(control["visibleRows"].isUInt() && control["visibleRows"].asUInt() >= 1 && control["visibleRows"].asUInt() <= 32,
							control["visibleRows"],p+"/visibleRows","Choice visible rows must be 1..32");
						spec.visibleRows = control["visibleRows"].asUInt();
					}
					Require(control["options"].isArray() && !control["options"].empty() && control["options"].size() <= 256,
						control["options"],p+"/options","Choice requires 1..256 authored options");
					if (control.isMember("optionCount")) {
						spec.optionCount = ReadExpression(control["optionCount"],p+"/optionCount");
						Require(spec.optionCount->type == 0,control["optionCount"],p+"/optionCount","A choice's option count must be numeric");
					}
					std::set<std::string> ids; std::set<StateValue> values;
					spec.options.reserve(control["options"].size());
					for (Json::ArrayIndex i = 0; i < control["options"].size(); ++i) {
						const auto& value = control["options"][i]; const auto opt = p+"/options/"+std::to_string(i);
						Fields(value,opt,{"id","node","label","labelIndex","value","enabled","parts","extensions"});
						ChoiceOption option; option.id = Id(value["id"],opt+"/id"); option.node = Id(value["node"],opt+"/node");
						Require(ids.insert(option.id).second,value["id"],opt+"/id","Duplicate choice option ID");
						if (value["label"].isObject()) {
							// Text the application publishes, such as a display's name.
							const auto at = opt+"/label";
							Fields(value["label"],at,{"state","extensions"});
							option.labelState = Id(value["label"]["state"],at+"/state");
							const auto found = model.state.find(option.labelState);
							Require(found != model.state.end() && std::holds_alternative<std::string>(found->second.initial),value["label"],at,
								"A state label names declared string state");
							Require(!value.isMember("labelIndex"),value["labelIndex"],opt+"/labelIndex","A state label has no translated list index");
						} else option.label = ControlLabel(value["label"],opt+"/label");
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
			static const std::map<std::string,ControlState> states = {{"default",ControlState::Default},{"hover",ControlState::Hover},
				{"focus",ControlState::Focus},{"pressed",ControlState::Pressed},{"disabled",ControlState::Disabled}};
			for (const auto& [name,state] : states) parsed.states[state] = Id(control["states"][name],p+"/states/"+name);
			if (control.isMember("navigation")) {
				Fields(control["navigation"],p+"/navigation",{"next","previous","up","down","left","right","extensions"});
				for (const auto& name : control["navigation"].getMemberNames()) if (name != "extensions")
					parsed.navigation[name] = Id(control["navigation"][name],p+"/navigation/"+name);
			}
		}
		if (result.type == "vector" && value.isMember("symbol")) {
			Require(!value.isMember("paths"),value["paths"],path+"/paths","A vector node draws its own paths or a symbol's, not both");
			Require(value["symbol"].isString(),value["symbol"],path+"/symbol","Expected a symbol ID");
			const auto found = symbols.find(value["symbol"].asString());
			Require(found != symbols.end(),value["symbol"],path+"/symbol","Unknown symbol '"+value["symbol"].asString()+"'");
			result.paths = found->second;
		} else if (result.type == "vector") {
			const auto& paths = value["paths"];
			Require(paths.isArray(),paths,path+"/paths","Vector node requires a path array");
			std::set<std::string> pathIds;
			result.paths.reserve(paths.size());
			for (Json::ArrayIndex i = 0; i < paths.size(); ++i) {
				const auto p = path+"/paths/"+std::to_string(i);
				auto shape = ReadPath(paths[i],p);
				Require(pathIds.insert(shape.id).second,paths[i]["id"],p+"/id","Duplicate path ID within vector node");
				result.paths.push_back(std::move(shape));
			}
		} else Require(!value.isMember("paths") && !value.isMember("symbol"),value,path,"Paths and symbols require a vector node");
		if (value.isMember("mask")) {
			const auto& mask = value["mask"]; const auto p = path+"/mask";
			Fields(mask,p,{"paths","extensions"});
			Require(mask["paths"].isArray(),mask["paths"],p+"/paths","Mask requires a path array");
			result.mask.emplace();
			std::set<std::string> ids;
			result.mask->reserve(mask["paths"].size());
			for (Json::ArrayIndex i = 0; i < mask["paths"].size(); ++i) {
				const auto at = p+"/paths/"+std::to_string(i);
				auto shape = ReadPath(mask["paths"][i],at,true);
				Require(ids.insert(shape.id).second,mask["paths"][i]["id"],at+"/id","Duplicate path ID within mask");
				result.mask->push_back(std::move(shape));
			}
		}
		if (value.isMember("properties")) {
			const auto& properties = value["properties"];
			Require(properties.isObject(),properties,path+"/properties","Expected a property object");
			// JSON member order is not cascade order. Resolve shorthands first,
			// then explicit sides, and give motion one owner per effective leaf.
			// Members read in key order, the order a property map keeps, so a
			// later shorthand or side wins as it did when they were collected.
			const auto propertiesPath = path+"/properties/";
			std::vector<std::pair<std::string_view,Value>> sides;
			for (auto member = properties.begin(); member != properties.end(); ++member) {
				const std::string name(MemberName(member));
				const auto propertyPath = propertiesPath+PointerPart(name);
				auto property = Typed(*member,propertyPath);
				Property(name,property,*member,propertyPath);
				Require(name != "text" || result.type == "text",*member,propertyPath,"Text content requires a text node");
				// Pictorial content is the specification's bitmap exception: levelshots,
				// backdrops and previews. Furniture stays vector art on other nodes.
				Require(!name.starts_with("image") || result.type == "image",*member,propertyPath,"Image properties require an image node");
				if (const auto* leaves = ShorthandLeaves(name)) for (const auto& leaf : *leaves) result.properties[leaf] = property;
				else sides.emplace_back(MemberName(member),std::move(property));
			}
			for (auto& [name,property] : sides) result.properties[std::string(name)] = std::move(property);
		}
		Require(result.type != "image" || result.properties.contains("image"),value,path,
			"An image node requires an explicit image source property; an empty source shows no picture");
		if (HasImageCoordinates(result)) {
			const auto fit=result.properties.find("image-fit");
			Require(fit==result.properties.end() || fit->second.text=="fill",value,path+"/properties/image-fit",
				"Repeating image coordinates require fill sizing");
		}
		if (value.isMember("children")) {
			Require(value["children"].isArray(),value["children"],path+"/children","Expected a child array");
			Require(result.type != "text" || value["children"].empty(),value["children"],path+"/children","Text nodes cannot own child nodes");
			const auto& children = value["children"];
			result.children.reserve(children.size());
			for (Json::ArrayIndex i = 0; i < children.size(); ++i) result.children.push_back(ReadNode(children[i],path+"/children/"+std::to_string(i),depth+1));
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
		} else if (const auto* scroll = std::get_if<ScrollSpec>(&control.widget)) {
            const auto parentOf = [&](const std::string& id) {
                const Node* found=nullptr;std::vector<const Node*> pending{&model.root};
                while(!pending.empty()) {const auto* next=pending.back();pending.pop_back();
                    for(const auto& child:next->children) {if(child.id==id) found=next;pending.push_back(&child);}}
                return found;
            };
            const auto* viewport=model.FindNode(scroll->viewport);
            Require(viewport && viewport->type=="group" && !viewport->control && !viewport->modal &&
                viewport!=&owner && parentOf(viewport->id)==parentOf(owner.id),value,path+"/viewport",
                "Scrollbar viewport must be a sibling group in the same modal scope");
            const auto overflow=viewport->properties.find("overflow");
            Require(overflow!=viewport->properties.end() && overflow->second.type==ValueType::Keyword &&
                (overflow->second.text=="auto" || overflow->second.text=="scroll"),value,path+"/viewport",
                "Scrollbar viewport requires authored auto or scroll overflow");
            std::vector<const Node*> pending{&model.root};
            while(!pending.empty()) {const auto* next=pending.back();pending.pop_back();
                if(next!=&owner && next->control) if(const auto* other=std::get_if<ScrollSpec>(&next->control->widget))
                    Require(other->viewport!=scroll->viewport || other->vertical!=scroll->vertical,value,path,
                        "A viewport axis has exactly one authored scrollbar owner");
                for(const auto& child:next->children) pending.push_back(&child);
            }
            part(scroll->track,owner.id,"group",path+"/parts/track");
            part(scroll->thumb,scroll->track,nullptr,path+"/parts/thumb");
            Require(parentOf(scroll->track)==&owner && parentOf(scroll->thumb)==model.FindNode(scroll->track),value,path,
                "Scrollbar track and thumb must be direct authored children of their containing part");
            const auto* track=model.FindNode(scroll->track);const auto position=track->properties.find("position");
            Require(position!=track->properties.end() && position->second.type==ValueType::Keyword &&
                (position->second.text=="relative" || position->second.text=="absolute"),value,path,
                "Scrollbar track must establish a positioned containing block");
            owned.emplace(scroll->track,"position");keyword(scroll->thumb,"position","absolute");owned.emplace(scroll->thumb,"position");
            const auto* thumb=model.FindNode(scroll->thumb);
            Require(!thumb->properties.contains("transform"),value,path,"Scrollbar thumb cannot have an independent transform");
            owned.emplace(scroll->thumb,"transform");
            const auto* axis=scroll->vertical?"height":"width";const auto* offset=scroll->vertical?"top":"left";
            const auto* opposite=scroll->vertical?"bottom":"right";
            reserve(scroll->thumb,axis);reserve(scroll->thumb,offset);
            if(thumb->properties.contains(opposite)) keyword(scroll->thumb,opposite,"auto");
            owned.emplace(scroll->thumb,opposite);
            for(const std::string prefix:{"min-","max-"}) {
                Require(!thumb->properties.contains(prefix+axis),value,path,"Scrollbar thumb axis constraints are derived from actual scroll extent");
                owned.emplace(scroll->thumb,prefix+axis);
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
		} else if (const auto* number = std::get_if<NumberSpec>(&control.widget)) {
			part(number->viewport,owner.id,"group",path+"/parts/viewport");
			const auto* viewport = model.FindNode(number->viewport);
			const auto position = viewport->properties.find("position");
			Require(position != viewport->properties.end() && position->second.type == ValueType::Keyword &&
				(position->second.text == "absolute" || position->second.text == "relative"),value,path,
				"Number viewport must establish an absolute or relative containing block");
			owned.emplace(number->viewport,"position");
			// One common containing block keeps glyphs, selection, composition
			// and caret in the same scroll/transform frame. Transform the viewport.
			const auto fieldPaint = [&](const std::string& id) {
				Require(std::any_of(viewport->children.begin(),viewport->children.end(),[&](const Node& child) { return child.id == id; }),
					value,path,"Number paint parts must be direct children of the viewport");
				Require(!model.FindNode(id)->properties.contains("transform"),value,path,"Number paint parts cannot have individual transforms");
				owned.emplace(id,"transform");
			};
			part(number->text,number->viewport,"text",path+"/parts/text");
			fieldPaint(number->text);
			part(number->validation,owner.id,"text",path+"/parts/validation");
			Require(!Descendant(number->validation,number->viewport),value,path+"/parts/validation","Number validation must remain outside the clipped text viewport");
			keyword(number->viewport,"overflow","hidden"); owned.emplace(number->viewport,"overflow"); owned.emplace(number->viewport,"clip");
			keyword(number->text,"position","absolute"); keyword(number->text,"white-space","pre"); keyword(number->text,"text-align","left");
			owned.emplace(number->text,"position"); owned.emplace(number->text,"white-space"); owned.emplace(number->text,"text-align");
			owned.emplace(number->text,"text-transform");
			reserve(number->text,"left"); reserve(number->text,"text");
			reserve(number->validation,"text"); reserve(number->validation,"display");
			std::vector<std::string> paint{number->text};
			for (const auto& id : {number->selection,number->caret,number->composition}) {
				part(id,number->viewport,nullptr,path+"/parts"); fieldPaint(id); keyword(id,"position","absolute"); owned.emplace(id,"position");
				for (const auto* property : {"left","top","width","height","display"}) reserve(id,property);
				for (const auto& other : paint) separate(id,other);
				paint.push_back(id);
			}
			const auto& caretProperties = model.FindNode(number->caret)->properties;
			const auto opacity = caretProperties.find("opacity");
			Require(opacity != caretProperties.end() && opacity->second.type == ValueType::Number,value,path,
				"Number caret blink requires an explicit numeric opacity base");
			owned.emplace(number->caret,"opacity");
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
			if (!choice->placementBounds.empty()) {
				Require(choice->placementBounds != owner.id && model.FindNode(choice->placementBounds) &&
					Descendant(owner.id,choice->placementBounds),value,path+"/placementBounds",
					"Choice placement bounds must identify a canonical proper ancestor of its owner");
				Require(!model.FindNode(choice->popup)->properties.contains("transform"),value,path,
					"A constrained choice popup root cannot have an independent transform");
				owned.emplace(choice->popup,"transform");
			}
			owned.emplace(choice->viewport,"overflow");
			owned.emplace(choice->viewport,"clip");
			reserve(choice->viewport,"height"); reserve(choice->content,"top"); reserve(choice->valueText,"text");
			if (choice->scrollbar) {
				const auto& scroll = *choice->scrollbar;
				part(scroll.track,choice->popup,"group",path+"/scrollbar/track");
				part(scroll.thumb,scroll.track,nullptr,path+"/scrollbar/thumb");
				separate(scroll.track,choice->viewport);
				const auto direct = [&](const std::string& child, const std::string& parent) {
					const auto& children = model.FindNode(parent)->children;
					return std::any_of(children.begin(),children.end(),[&](const Node& node) { return node.id == child; });
				};
				Require(direct(choice->viewport,choice->popup) && direct(scroll.track,choice->popup) && direct(scroll.thumb,scroll.track),
					value,path,"Choice scrollbar track and viewport must be direct popup children, with a direct thumb child");
				keyword(scroll.track,"position","absolute"); owned.emplace(scroll.track,"position");
                reserve(scroll.track,"left");
				keyword(scroll.thumb,"position","absolute"); owned.emplace(scroll.thumb,"position");
				reserve(choice->viewport,"width");
				for (const auto& id : {scroll.track,scroll.thumb}) {
					reserve(id,"height"); reserve(id,"top");
					const auto& properties = model.FindNode(id)->properties;
					if (properties.contains("bottom")) keyword(id,"bottom","auto");
					owned.emplace(id,"bottom");
					for (const std::string prefix : {"min-","max-"}) {
						Require(!properties.contains(prefix+"height"),value,path,"Choice scrollbar height constraints are derived from measured popup geometry");
						owned.emplace(id,prefix+"height");
					}
				}
				Require(!model.FindNode(scroll.thumb)->properties.contains("transform"),value,path,"Choice scrollbar thumb cannot have an independent transform");
				owned.emplace(scroll.thumb,"transform");
			}
			for (size_t i = 0; i < choice->options.size(); ++i) {
				const auto& option = choice->options[i]; const auto at = path+"/options/"+std::to_string(i);
				part(option.node,choice->content,"group",at+"/node");
				part(option.labelPart,option.node,"text",at+"/parts/label");
				part(option.selectedPart,option.node,nullptr,at+"/parts/selected");
				part(option.highlightPart,option.node,nullptr,at+"/parts/highlight");
				separate(option.selectedPart,option.highlightPart);
				separate(option.labelPart,option.selectedPart); separate(option.labelPart,option.highlightPart);
				reserve(option.labelPart,"text"); reserve(option.selectedPart,"display"); reserve(option.highlightPart,"display");
				// A counted list hides the options past its count.
				if (choice->optionCount) reserve(option.node,"display");
				// An option that can become unavailable dims its label's colour.
				const auto* label = model.FindNode(option.labelPart);
				if (option.Conditional() && label->properties.contains("color") && label->properties.at("color").type == ValueType::Colour)
					owned.emplace(option.labelPart,"color");
				for (size_t j = 0; j < i; ++j) separate(option.node,choice->options[j].node);
			}
		}
		for (const auto& target : owned)
			Require(!bindingTargets.contains(target),value,path,"A binding cannot own a runtime widget property");
		for (const auto& target : owned)
			Require(!trackTargets.contains(target),value,path,"A timeline cannot own a runtime widget property");
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
			if (node.control->role == ControlRole::Scrollbar) {
                ValidateWidgetParts(value,node,p);
            } else if (node.control->role == ControlRole::Button) {
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
				const auto* timeline = FindTimeline(id);
				const auto at = p+"/states/"+name;
				Require(timeline != nullptr,value["states"][name],at,"Control state timeline does not exist");
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
		Fields(value,path,{"id","durationMs","iterations","essential","complete","tracks","extensions"});
		Timeline timeline;
		timeline.id = Id(value["id"],path+"/id");
		timeline.durationMs = Numeric(value["durationMs"],path+"/durationMs",0.001,86400000);
		if (value.isMember("iterations")) {
			Require(value["iterations"].isUInt() && value["iterations"].asUInt() <= 1000000,value["iterations"],path+"/iterations","Expected an integer iteration count 0..1000000 (0 repeats until cancelled)");
			timeline.iterations = value["iterations"].asUInt();
		}
		// Events are read after timelines; ValidateCompletions resolves the name.
		if (value.isMember("complete")) {
			Require(timeline.iterations != 0,value["complete"],path+"/complete","A timeline that repeats until cancelled never completes");
			timeline.complete = EventName(value["complete"],path+"/complete");
		}
		if (value.isMember("essential")) {
			Require(value["essential"].isBool(),value["essential"],path+"/essential","Expected a boolean");
			timeline.essential = value["essential"].asBool();
		}
		const auto& tracks = value["tracks"];
		Require(tracks.isArray() && !tracks.empty(),tracks,path+"/tracks","Timeline requires tracks");
		std::set<std::pair<std::string,std::string>> targets;
		timeline.tracks.reserve(tracks.size());
		for (Json::ArrayIndex i = 0; i < tracks.size(); ++i) {
			const auto& entry = tracks[i];
			const auto p = path+"/tracks/"+std::to_string(i);
			Fields(entry,p,{"node","property","keys","extensions"});
			Track track;
			track.node = Id(entry["node"],p+"/node");
			track.property = Id(entry["property"],p+"/property");
			const auto* node = model.FindNode(track.node);
			Require(node != nullptr,entry["node"],p+"/node","Timeline target node does not exist");
			// A shorthand animates each of its leaves; any other property animates itself.
			const auto* leaves = ShorthandLeaves(track.property);
			const auto eachProperty = [&](auto&& check) {
				if (leaves) { for (const auto& property : *leaves) check(property); } else check(track.property);
			};
			eachProperty([&](const std::string& property) {
				Require(node->properties.contains(property),entry,p,"Animated properties require an explicit base value on the target node");
				Require(targets.insert({track.node,property}).second,entry,p,"Duplicate effective property owner within a timeline");
			});
			const auto& keys = entry["keys"];
			Require(keys.isArray() && keys.size() >= 2,keys,p+"/keys","Track requires at least two keys");
			double previous = -1;
			track.keys.reserve(keys.size());
			for (Json::ArrayIndex k = 0; k < keys.size(); ++k) {
				const auto& key = keys[k];
				const auto kp = p+"/keys/"+std::to_string(k);
				Require(key.isObject(),key,kp,"Expected an object");
				const auto members = FindMembers(key,{"atMs","value","easing","extensions"});
				CheckFields(members,kp,members.fields[3]);
				const auto& atMs = members.Field(0); const auto& keyValue = members.Field(1);
				Keyframe frame;
				frame.atMs = Numeric(atMs,kp,"/atMs",0,timeline.durationMs);
				Require(frame.atMs > previous && (k != 0 || frame.atMs == 0),atMs,kp+"/atMs","Key times must increase strictly and begin at zero");
				previous = frame.atMs;
				frame.value = Typed(keyValue,kp+"/value");
				Property(track.property,frame.value,keyValue,kp,"/value");
				eachProperty([&](const std::string& property) {
					Require(node->properties.at(property).CanInterpolate(frame.value),keyValue,kp+"/value","Track values must interpolate with each effective base type and unit");
				});
				if (members.fields[2]) {
					const auto& easing = *members.fields[2];
					Require(easing.isArray() && easing.size() == 4,easing,kp+"/easing","Easing requires four cubic-Bezier coordinates");
					frame.easing = {Numeric(easing[0],kp,"/easing/0",0,1),Numeric(easing[1],kp,"/easing/1",0,1),Numeric(easing[2],kp,"/easing/2",0,1),Numeric(easing[3],kp,"/easing/3",0,1)};
				}
				track.keys.push_back(std::move(frame));
			}
			if (!leaves) { timeline.tracks.push_back(std::move(track)); continue; }
			for (const auto& property : *leaves) {
				Track resolved = track;
				resolved.property = property;
				timeline.tracks.push_back(std::move(resolved));
			}
		}
		return timeline;
	}
	const std::string& source;
	std::vector<Diagnostic>& diagnostics;
	std::shared_ptr<DocumentModel> owner;
	DocumentModel& model;
	size_t expressionCount = 0;
	size_t eventStepCount = 0;
	std::set<std::string> nodeIds;
	unsigned nodeCount = 0, modelCount = 0;
	std::vector<std::pair<std::string,std::string>> detailControls; // Controls naming a detail, with their JSON pointers.
	// The (node, property) pairs that timeline tracks and bindings own, so
	// ownership checks look a pair up instead of scanning every track.
	std::set<std::pair<std::string,std::string>> trackTargets, bindingTargets;
	// The timelines by ID once they are read (the first of an ID, as a search would find).
	std::unordered_map<std::string_view,const Timeline*> timelineIndex;
	const Timeline* FindTimeline(const std::string& id) const {
		const auto found = timelineIndex.find(id);
		return found == timelineIndex.end() ? nullptr : found->second;
	}
};
#undef Require
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
	// Appended piece by piece: a page's thousands of nodes and tens of
	// thousands of properties would otherwise build a temporary each.
	const char* tag = node.type == "vector" ? "q4-vector" : "q4-node";
	output += '<'; output += tag; output += " id=\""; output += node.id; output += "\" style=\"";
	// Canonical opacity isolates the complete node subtree. RmlUi's ordinary
	// opacity is an inherited primitive tint; its filter supplies the needed
	// stacking/render boundary without changing the editable source format.
	output += "opacity:1;";
	// Button parts (focus rail, marker, label) use the button's local box even
	// when the button itself participates in normal document flow.
	if (node.control) output += "position:relative;";
	if (node.mask) output += "mask-image:q4-mask(alpha);";
	const auto blur = node.properties.find("backdrop-blur"), saturate = node.properties.find("backdrop-saturate");
	const auto streak = node.properties.find("backdrop-streak");
	if (blur != node.properties.end() || saturate != node.properties.end() || streak != node.properties.end()) {
		const auto css = BackdropFilterCss(blur != node.properties.end() ? &blur->second : nullptr,
			saturate != node.properties.end() ? &saturate->second : nullptr, streak != node.properties.end() ? &streak->second : nullptr);
		if (css != "none") { output += "backdrop-filter:"; output += css; output += ';'; }
	}
	for (const auto& [name,value] : node.properties) if (name != "text") {
		if (name == "opacity") { if (value.data[0] < 1) { output += "filter:opacity("; output += value.Css(); output += ");"; } }
		else if (name == "backdrop-blur" || name == "backdrop-saturate" || name == "backdrop-streak") continue; // Folded into backdrop-filter.
		else if (name == "image") { if (!HasImageCoordinates(node)) { output += "decorator:"; output += ImageDecorator(node,value.text); output += ';'; } }
		else if (name == "image-offset-x" || name == "image-offset-y" || name == "image-scale-x" || name == "image-scale-y") continue;
		else if (name == "image-fit" || name == "image-align-x" || name == "image-align-y" || name == "image-blend") continue; // Folded into the decorator.
		else { output += name; output += ':'; output += value.Css(); output += ';'; }
	}
	output += "\">";
	for (const auto& child : node.children) MarkupNode(child,output);
	output += "</"; output += tag; output += '>';
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
		// Unpremultiplying can land a rounding step past 1, which a saved
		// presentation (a language or renderer reload mid-fade) refuses.
		result.data[i] = result.data[3] > 0 ? std::clamp(premultiplied/result.data[3],0.0,1.0) : 0;
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
const Node* DocumentModel::FindNode(const std::string& id) const {
	if (nodeIndex.nodes.empty()) return Find(root,id);
	const auto found = nodeIndex.nodes.find(id);
	return found == nodeIndex.nodes.end() ? nullptr : found->second;
}
void DocumentModel::IndexNodes() {
	// Pre-order, first ID wins: the order Find answers in.
	nodeIndex.nodes.clear();
	std::vector<const Node*> pending{&root};
	while (!pending.empty()) {
		const auto* node = pending.back(); pending.pop_back();
		nodeIndex.nodes.emplace(node->id,node);
		for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) pending.push_back(&*child);
	}
}
bool HasImageCoordinates(const Node& node) {
	return node.type=="model" || node.properties.contains("image-offset-x") || node.properties.contains("image-offset-y") ||
		node.properties.contains("image-scale-x") || node.properties.contains("image-scale-y");
}
std::string ImageDecorator(const Node& node, const std::string& source) {
	if (source.empty() || !ValidImageSource(source)) return "none";
	auto keyword = [&](const char* name, const char* fallback) {
		const auto found = node.properties.find(name);
		return found != node.properties.end() ? found->second.text : std::string(fallback);
	};
	// material: names resolve from the VFS root, never relative to the document.
	// Additive pictures (the stock wordmark's light) use the host's additive material.
	const std::string prefix = keyword("image-blend","normal") == "additive" ? "material:q4-add/" : "material:";
	return "image("+prefix+source+" none "+keyword("image-fit","fill")+" "+
		keyword("image-align-x","center")+" "+keyword("image-align-y","center")+")";
}
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
	if (base.type == ValueType::Text || base.type == ValueType::Keyword || base.type == ValueType::Font || base.type == ValueType::Image) return PresentationType::String;
	return std::nullopt;
}
bool detail::ParseDocumentSource(const std::string& source, Json::Value& root, std::vector<Diagnostic>& diagnostics) {
    return Parse(source,root,diagnostics);
}
const Json::Value* detail::ResolveDocumentSource(const Json::Value& root, const std::string& pointer) {
    return Resolve(root,pointer);
}
// The parsed tree is a load-time intermediate: the model copies what it keeps,
// and a tree of a large document costs several times its source. An edit
// parses the retained source again to find the span it replaces.
struct Document::Impl { std::string source; DocumentModel model; };
Document::Document() : impl(std::make_unique<Impl>()) {}
Document::~Document() = default;
Document::Document(Document&&) noexcept = default;
Document& Document::operator=(Document&&) noexcept = default;
bool Document::Load(const std::string& source, std::vector<Diagnostic>& diagnostics) {
	diagnostics.clear();
	auto candidate = std::make_shared<Impl>();
	candidate->source = source;
	{
		Json::Value root;
		bool treeCheck = false;
		if (!Parse(source,root,diagnostics,&treeCheck)) return false;
		// The validator reads into the model the document keeps, and indexes
		// its nodes once the tree is complete; nothing moves it afterwards.
		// Its initial state references that model (never the other way), so
		// the document can keep it beside the model.
		std::shared_ptr<const State> initial;
		try { initial = std::make_shared<const State>(Validator(source,diagnostics,std::shared_ptr<DocumentModel>(candidate,&candidate->model)).Read(root,treeCheck)); }
		catch (const ValidationError&) { return false; }
		initialState = std::move(initial);
	}
	impl = std::move(candidate);
	return true;
}
bool Document::ReplaceValue(const std::string& pointer, const std::string& replacement, std::vector<Diagnostic>& diagnostics) {
	diagnostics.clear();
	// The retained source parsed when it was loaded, so this parse finds the
	// value's span; an empty source (never loaded) has no value to replace.
	Json::Value root;
	if (!impl->source.empty() && !Parse(impl->source,root,diagnostics)) return false;
	const auto* value = impl->source.empty() ? nullptr : Resolve(root,pointer);
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
std::shared_ptr<const DocumentModel> Document::SharedModel() const { return {impl,&impl->model}; }
const State& Document::InitialState() const {
	static const State none;
	return initialState ? *initialState : none;
}
std::string Document::BuildMarkup() const {
	// Native unstyled scrollbars default to the containing width and can consume
	// the entire client area. Scrolling stays semantic until authored scrollbar
	// parts provide the editable vector control and its pointer interactions.
	std::string result = "<rml><head><style>body{margin:0;width:100%;height:100%;font-family:marine;font-size:16dp;color:#fff;}div,q4-node,q4-vector{display:block;}scrollbarvertical{width:0;}scrollbarhorizontal{height:0;}</style></head><body>";
	MarkupNode(impl->model.root,result);
	return result+"</body></rml>";
}

} // namespace openq4::ui
