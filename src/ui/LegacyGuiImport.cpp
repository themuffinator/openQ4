// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "LegacyGuiImport.h"
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <cmath>
#include <charconv>
#include <limits>
#include <stdexcept>
#include "DeviceContext.h"
#include "Window.h"
#include "UserInterfaceLocal.h"

namespace {
bool ImportPath(const char* path) {
	if (!path || !*path || *path == '/' || strstr(path,"..")) return false;
	for (const unsigned char* p = reinterpret_cast<const unsigned char*>(path); *p; ++p)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') || *p == '/' || *p == '_' || *p == '-' || *p == '.')) return false;
	return true;
}
void ImportString(std::string& out, const char* text, size_t length) {
	static const char hex[] = "0123456789abcdef";
	out += '"';
	for (size_t i = 0; i < length; ++i) {
		const unsigned char c = static_cast<unsigned char>(text[i]);
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if (c < 32 || c >= 127) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
		else out += c;
	}
	out += '"';
}
void ImportString(std::string& out, const char* text) { ImportString(out,text,strlen(text)); }
}

void RetainedUI_ExportLegacy(const idCmdArgs& args) {
	if (args.Argc() != 3 || !ImportPath(args.Argv(1)) || !ImportPath(args.Argv(2)) ||
		idStr::Icmpn(args.Argv(1),"guis/",5) != 0 || idStr::Icmpn(args.Argv(2),"ui-import/",10) != 0 ||
		!idStr::CheckExtension(args.Argv(2),"json")) {
		common->Printf("usage: ui_exportLegacy <guis/source.gui> <ui-import/output.json>\n"); return;
	}
	common->Printf("UI_GUI_EXPORT_BEGIN %s\n",args.Argv(1));
	void* bytes = NULL;
	const int length = fileSystem->ReadFile(args.Argv(1),&bytes);
	if (length < 0 || !bytes || length > 8*1024*1024) {
		if (bytes) fileSystem->FreeFile(bytes);
		common->Warning("UI import: missing or oversized source %s",args.Argv(1));
		common->Printf("UI_GUI_EXPORT_END failed\n"); return;
	}
	const std::string sourceBytes(static_cast<const char*>(bytes),static_cast<size_t>(length));
	fileSystem->FreeFile(bytes);
	// Match idUserInterfaceLocal::InitFromFile, including native include/macro
	// expansion and escaped-string handling. Do not instantiate legacy windows
	// or run GUI scripts while collecting translation input.
	const int flags = LEXFL_NOFATALERRORS | LEXFL_NOSTRINGCONCAT |
		LEXFL_ALLOWMULTICHARLITERALS | LEXFL_ALLOWBACKSLASHSTRINGCONCAT;
	idParser parser(flags);
	std::string output = "{\"format\":1,\"encoding\":\"byte-preserving-latin-1\",\"source\":";
	ImportString(output,args.Argv(1)); output += ",\"source_bytes\":";
	ImportString(output,sourceBytes.data(),sourceBytes.size());
	output += ",\"parser_flags\":" + std::to_string(flags) + ",\"tokens\":[";
	// Preprocess the exact VFS bytes recorded above, even if a loose file is
	// edited during export. The source name preserves native include lookup.
	if (!parser.LoadMemory(sourceBytes.c_str(),length,args.Argv(1))) {
		common->Warning("UI import: cannot preprocess %s",args.Argv(1));
		common->Printf("UI_GUI_EXPORT_END failed\n"); return;
	}
	idToken token;
	unsigned count = 0;
	bool bounded = true;
	while (parser.ReadToken(&token)) {
		if (++count > 1000000 || output.size() > 64*1024*1024) { bounded = false; break; }
		if (count > 1) output += ',';
		output += '[' + std::to_string(token.type) + ',' + std::to_string(token.subtype) + ',' +
			std::to_string(token.line) + ',' + std::to_string(token.linesCrossed) + ',';
		// This identifies the active parser source. Macro definition provenance
		// is not represented by idToken and must not be inferred from this field.
		ImportString(output,parser.GetFileName()); output += ',';
		ImportString(output,token.c_str(),static_cast<size_t>(token.Length())); output += ']';
	}
	output += "]}";
	if (!bounded || output.size() > 64*1024*1024 ||
		fileSystem->WriteFile(args.Argv(2),output.data(),static_cast<int>(output.size())) != static_cast<int>(output.size())) {
		common->Warning("UI import: token budget or output write failed for %s",args.Argv(1));
		common->Printf("UI_GUI_EXPORT_END failed\n"); return;
	}
	// idParser reports non-fatal diagnostics through the engine log. The
	// importing harness must bind this bracketed diagnostic interval to the
	// token file; an exported file alone does not prove preprocessing succeeded.
	common->Printf("UI_GUI_EXPORT_END exported %u %s\n",count,args.Argv(2));
}

// LEGACY_OBSERVATION_CORE_BEGIN
namespace {
struct LegacyObservedWindow {
	idWindow* window = nullptr;
	unsigned identity = 0;
	bool began = false, finished = false, success = false, fixed = false, destroyed = false;
};
struct LegacyObservedTerm {
	unsigned window = 0, sequence = 0;
	unsigned fixupSequence = 0, resolvedSequence = 0;
	std::string spelling{"",0}, parserSource{"",0};
	int line = 0, tokenType = 0, table = -1, operation = -1, result = -1, type = -1;
	intptr_t marker = 0;
	idWinVar* variable = nullptr;
	bool emitted = false, fixupLookup = false, resolved = false;
	std::string variableType{"",0};
};
struct LegacyObservedEvaluation {
	unsigned window = 0, sequence = 0;
	bool desktopFixed = false, forced = false, mapped = false, enabled = false, eval = false, dictionary = false;
	int reg = -1;
	float registerValue = 0, alpha = 0;
};
struct LegacyObservation {
	static constexpr size_t MaxWindows = 512, MaxTerms = 128, MaxEvaluations = 4096;
	idUserInterfaceLocal* gui = nullptr;
	idWindow** evalCache = nullptr;
	std::string source{"",0}, sourceBytes{"",0};
	std::vector<LegacyObservedWindow> windows;
	std::vector<LegacyObservedTerm> terms;
	std::vector<LegacyObservedEvaluation> evaluations;
	unsigned sequence = 0;
	int parserFlags = 0;
	bool sourceRechecked = false;
	bool failed = false, loaded = false, loadAttempted = false, desktopFixed = false, forced = false;
	LegacyObservation() {
		windows.reserve(MaxWindows); terms.reserve(MaxTerms); evaluations.reserve(MaxEvaluations);
	}
	LegacyObservedWindow* Find(idWindow* window) noexcept {
		for (auto& item : windows) if (item.window == window) return &item;
		return nullptr;
	}
};
// The registered command and all its native calls are synchronous. A scoped
// thread-local pointer neither grants another thread observation authority nor
// outlives the owned GUI. No native pointer is written to the receipt.
thread_local LegacyObservation* legacyObservation = nullptr;
thread_local bool legacyObservationCommand = false;
struct LegacyObservationScope {
	LegacyObservation& state;
	explicit LegacyObservationScope(LegacyObservation& value) : state(value) { legacyObservation = &value; }
	~LegacyObservationScope() {
		// EvalRegs' shared cache must not retain a temporary diagnostic window.
		// A later foreign-window evaluation retains its own cache unchanged.
		if (state.evalCache && state.Find(*state.evalCache)) *state.evalCache = nullptr;
		legacyObservation = nullptr;
	}
};
const char* LegacyVariableType(idWinVar* value) noexcept {
	if (!value) return "null";
	if (dynamic_cast<idWinFloat*>(value)) return "float";
	if (dynamic_cast<idWinVec4*>(value)) return "vec4";
	if (dynamic_cast<idWinInt*>(value)) return "int";
	if (dynamic_cast<idWinBool*>(value)) return "bool";
	if (dynamic_cast<idWinStr*>(value)) return "string";
	return "other";
}
}
bool UI_LegacyObservationActive(idWindow* window) noexcept {
	return legacyObservation && window && window->GetGui() == legacyObservation->gui;
}
void UI_LegacyObservationParse(idWindow* window, bool begin, bool success) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	try {
		auto* found = state.Find(window);
		if (begin) {
			if (found || state.windows.size() == LegacyObservation::MaxWindows) { state.failed = true; return; }
			LegacyObservedWindow item; item.window = window; item.identity = static_cast<unsigned>(state.windows.size()+1); item.began = true;
			state.windows.push_back(item);
		} else {
			if (!found || found->finished || found->destroyed) { state.failed = true; return; }
			found->finished = true; found->success = success;
			if (!success) state.failed = true;
		}
	} catch (...) { state.failed = true; }
}
unsigned UI_LegacyObservationTerm(idWindow* window, idParser* parser, const idToken& token, int tableIndex) noexcept {
	if (!UI_LegacyObservationActive(window) || strcmp(token.c_str(),"\\")) return 0;
	auto& state = *legacyObservation;
	try {
		auto* owner = state.Find(window);
		if (!owner || owner->finished || owner->destroyed || state.terms.size() == LegacyObservation::MaxTerms) { state.failed = true; return 0; }
		LegacyObservedTerm term; term.window = owner->identity; term.sequence = ++state.sequence;
		term.spelling = token.c_str(); term.parserSource = parser->GetFileName(); term.line = token.line; term.tokenType = token.type; term.table = tableIndex;
		state.terms.push_back(term); return static_cast<unsigned>(state.terms.size());
	} catch (...) { state.failed = true; return 0; }
}
void UI_LegacyObservationOp(idWindow* window, unsigned token, int operation, int result, int type, intptr_t a, intptr_t b) noexcept {
	if (!token || !UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	try {
		auto* owner = state.Find(window);
		if (!owner || token > state.terms.size()) { state.failed = true; return; }
		auto& term = state.terms[token-1];
		if (term.window != owner->identity || term.emitted || operation < 0 || result < 0) { state.failed = true; return; }
		term.operation = operation; term.result = result; term.type = type; term.marker = b; term.emitted = true;
		if (type == WOP_TYPE_TABLE) {
			if (term.table < 0 || a != term.table) state.failed = true;
		} else if (type == WOP_TYPE_VAR || type == WOP_TYPE_VARF || type == WOP_TYPE_VARI || type == WOP_TYPE_VARB || type == WOP_TYPE_VARS) {
			if (term.table != -1) state.failed = true;
			if (b != -2) { term.variable = reinterpret_cast<idWinVar*>(a); term.variableType = LegacyVariableType(term.variable); }
		} else state.failed = true;
	} catch (...) { state.failed = true; }
}
void UI_LegacyObservationFixup(idWindow* window, int operation, const char* spelling, idWinVar* variable) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	try {
		auto* owner = state.Find(window); if (!owner) { state.failed = true; return; }
		for (auto& term : state.terms) if (term.window == owner->identity && term.operation == operation) {
			if (!term.emitted || term.marker != -2 || term.fixupLookup || term.spelling != spelling || owner->fixed) { state.failed = true; return; }
			term.fixupLookup = true; term.fixupSequence = ++state.sequence; term.variable = variable; term.variableType = LegacyVariableType(variable);
		}
	} catch (...) { state.failed = true; }
}
void UI_LegacyObservationResolved(idWindow* window, int operation, intptr_t a, intptr_t b) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation; auto* owner = state.Find(window);
	if (!owner) { state.failed = true; return; }
	for (auto& term : state.terms) if (term.window == owner->identity && term.operation == operation) {
		if (!term.fixupLookup || term.resolved || reinterpret_cast<intptr_t>(term.variable) != a || b != -1) { state.failed = true; return; }
		term.resolved = true; term.marker = b; term.resolvedSequence = ++state.sequence;
	}
}
void UI_LegacyObservationFixed(idWindow* window) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation; auto* owner = state.Find(window);
	if (!owner || !owner->finished || !owner->success || owner->fixed || owner->destroyed) { state.failed = true; return; }
	for (const auto& term : state.terms) if (term.window == owner->identity && term.fixupLookup && !term.resolved) state.failed = true;
	owner->fixed = true;
	if (window == state.gui->GetDesktop()) state.desktopFixed = true;
}
void UI_LegacyObservationCache(idWindow* window, idWindow** cache) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	if (!cache || (state.evalCache && state.evalCache != cache)) { state.failed = true; return; }
	state.evalCache = cache;
}
void UI_LegacyObservationEvaluation(idWindow* window, const float* registers, int count, idWinVar* expected, float alpha) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	try {
		auto* owner = state.Find(window);
		if (!owner || owner->destroyed || state.evaluations.size() == LegacyObservation::MaxEvaluations) { state.failed = true; return; }
		auto* reg = window->RegList()->FindReg("matcolor"); // Read-only register-table lookup, never variable fixup.
		if (!reg) return;
		LegacyObservedEvaluation value; value.window = owner->identity; value.sequence = ++state.sequence;
		value.desktopFixed = state.desktopFixed; value.forced = state.forced; value.alpha = alpha;
		value.mapped = reg->type == idRegister::VEC4 && reg->regCount == 4 && reg->var == expected && count > 0 && count <= MAX_EXPRESSION_REGISTERS && reg->regs[3] < count;
		value.enabled = reg->enabled; value.eval = expected && expected->GetEval(); value.dictionary = expected && expected->GetDict();
		if (value.mapped) { value.reg = reg->regs[3]; value.registerValue = registers[value.reg]; }
		state.evaluations.push_back(value);
	} catch (...) { state.failed = true; }
}
void UI_LegacyObservationDestroyed(idWindow* window) noexcept {
	if (!UI_LegacyObservationActive(window)) return;
	auto& state = *legacyObservation;
	if (auto* owner = state.Find(window)) { owner->destroyed = true; state.failed = true; }
}
bool UI_LegacyObservationLoad(idUserInterfaceLocal* gui, idParser& parser, const char* path) {
	if (!legacyObservation || legacyObservation->gui != gui) return false;
	auto& state = *legacyObservation;
	if (state.loadAttempted || state.source != path) { state.failed = true; return true; }
	state.loadAttempted = true; state.parserFlags = parser.GetFlags();
	// Only this explicit diagnostic instance loads the copied VFS root. Native
	// includes/macros still use the existing parser and original source name.
	if (!parser.LoadMemory(state.sourceBytes.c_str(),static_cast<int>(state.sourceBytes.size()),state.source.c_str())) state.failed = true;
	return true;
}
void UI_LegacyObservationLoaded(idUserInterfaceLocal* gui, bool loaded) {
	if (!legacyObservation || legacyObservation->gui != gui) return;
	auto& state = *legacyObservation;
	if (state.loaded || !state.loadAttempted || !loaded || !state.desktopFixed) state.failed = true;
	state.loaded = loaded;
}
// LEGACY_OBSERVATION_CORE_END

namespace {
bool LegacyReadRoot(const char* source, std::string& out) {
	void* bytes = nullptr; const int size = fileSystem->ReadFile(source,&bytes);
	struct Release { void* data; ~Release() { if (data) fileSystem->FreeFile(data); } } release{bytes};
	if (size <= 0 || size > 8*1024*1024 || !bytes || memchr(bytes,0,static_cast<size_t>(size))) return false;
	out.assign(static_cast<const char*>(bytes),static_cast<size_t>(size)); return true;
}
void LegacyFindWindow(idWindow* root, const std::string& path, unsigned& visited, unsigned& matches, idWindow*& result) {
	if (!root || ++visited > LegacyObservation::MaxWindows) return;
	const auto slash = path.find('/'); const std::string name = path.substr(0,slash);
	if (name != root->GetName()) return;
	if (slash == std::string::npos) { ++matches; result = root; return; }
	const std::string tail = path.substr(slash+1);
	for (int i = 0; i < root->GetChildCount() && visited <= LegacyObservation::MaxWindows; ++i)
		LegacyFindWindow(root->GetChild(i),tail,visited,matches,result);
}
idWindow* LegacyExactWindow(idWindow* root, const std::string& path, unsigned& visited) {
	unsigned matches = 0; idWindow* result = nullptr;
	LegacyFindWindow(root,path,visited,matches,result);
	return matches == 1 && visited <= LegacyObservation::MaxWindows ? result : nullptr;
}
void LegacyFloat(std::string& out, float value) {
	if (!std::isfinite(value)) { out += "null"; return; }
	char text[64]; const auto result = std::to_chars(text,text+sizeof(text),value,std::chars_format::general,std::numeric_limits<float>::max_digits10);
	if (result.ec != std::errc{}) throw std::runtime_error("float formatting failed");
	out.append(text,result.ptr);
}
unsigned LegacyFloatBits(float value) { unsigned bits = 0; static_assert(sizeof(bits) == sizeof(value)); memcpy(&bits,&value,sizeof(bits)); return bits; }
std::string LegacyObservationJson(const LegacyObservation& state, const std::string& ancestry, unsigned target, bool complete) {
	std::string out = "{\"format\":1,\"observation\":\"legacy-alpha\",\"replacement_acceptance\":false,\"diagnostic_interval_required\":true,\"include_closure_qualified\":false,\"root_loader\":\"diagnostic-frozen-vfs-root-memory\",\"structural_complete\":";
	out += complete ? "true" : "false"; out += ",\"source_rechecked_equal\":"; out += state.sourceRechecked ? "true" : "false"; out += ",\"parser_flags\":"+std::to_string(state.parserFlags); out += ",\"source\":"; ImportString(out,state.source.c_str());
	out += ",\"source_bytes\":"; ImportString(out,state.sourceBytes.data(),state.sourceBytes.size());
	out += ",\"encoding\":\"byte-preserving-latin-1\",\"ancestry\":"; ImportString(out,ancestry.c_str());
	out += ",\"window_identity\":"+std::to_string(target)+",\"desktop_fixup_complete\":"+(state.desktopFixed ? "true" : "false")+",\"terms\":[";
	bool first = true;
	for (const auto& term : state.terms) if (term.window == target) {
		if (!first) out += ','; first = false;
		out += "{\"sequence\":"+std::to_string(term.sequence)+",\"fixup_sequence\":"+std::to_string(term.fixupSequence)+",\"resolved_sequence\":"+std::to_string(term.resolvedSequence)+",\"spelling\":"; ImportString(out,term.spelling.c_str());
		out += ",\"parser_source\":"; ImportString(out,term.parserSource.c_str());
		out += ",\"line\":"+std::to_string(term.line)+",\"token_type\":"+std::to_string(term.tokenType)+",\"table_index\":"+std::to_string(term.table)+",\"operation\":"+std::to_string(term.operation)+",\"result_register\":"+std::to_string(term.result)+",\"operation_type\":"+std::to_string(term.type)+",\"final_marker\":"+std::to_string(term.marker)+",\"fixup_lookup\":"+(term.fixupLookup ? "true" : "false")+",\"fixup_completed\":"+(term.resolved ? "true" : "false")+",\"variable_type\":";
		ImportString(out,term.variableType.c_str()); out += '}';
	}
	out += "],\"evaluations\":["; first = true;
	for (const auto& value : state.evaluations) if (value.window == target) {
		if (!first) out += ','; first = false;
		out += "{\"sequence\":"+std::to_string(value.sequence)+",\"desktop_fixed\":"+(value.desktopFixed ? "true" : "false")+",\"diagnostic_forced\":"+(value.forced ? "true" : "false")+",\"mapped\":"+(value.mapped ? "true" : "false")+",\"enabled\":"+(value.enabled ? "true" : "false")+",\"eval\":"+(value.eval ? "true" : "false")+",\"dictionary\":"+(value.dictionary ? "true" : "false")+",\"register\":"+std::to_string(value.reg)+",\"register_value\":";
		LegacyFloat(out,value.registerValue); out += ",\"register_bits\":"+std::to_string(LegacyFloatBits(value.registerValue))+",\"matcolor_alpha\":";
		LegacyFloat(out,value.alpha); out += ",\"matcolor_alpha_bits\":"+std::to_string(LegacyFloatBits(value.alpha))+'}';
	}
	out += "]}"; return out;
}
}

void UI_ObserveLegacy(const idCmdArgs& args) {
	if (legacyObservationCommand || legacyObservation || args.Argc() != 4 || !ImportPath(args.Argv(1)) || !ImportPath(args.Argv(2)) || !ImportPath(args.Argv(3)) ||
		idStr::Icmpn(args.Argv(1),"guis/",5) || !idStr::CheckExtension(args.Argv(1),"gui") ||
		idStr::Icmpn(args.Argv(3),"ui-import/",10) || !idStr::CheckExtension(args.Argv(3),"json")) {
		common->Printf("usage: ui_observeLegacy <guis/source.gui> <Desktop/ancestry> <ui-import/output.json>\n"); return;
	}
	struct CommandScope { CommandScope() { legacyObservationCommand = true; } ~CommandScope() { legacyObservationCommand = false; } } commandScope;
	const std::string path(args.Argv(1)), ancestry(args.Argv(2)), outputPath(args.Argv(3));
	common->Printf("UI_LEGACY_OBSERVATION_BEGIN %s\n",path.c_str());
	try {
		LegacyObservation state; state.source = path;
		if (!LegacyReadRoot(path.c_str(),state.sourceBytes)) throw std::runtime_error("missing, binary or oversized VFS root");
		// An explicitly fresh, managed legacy instance; never reload a gameplay
		// GUI or dispatch its activation, input, timelines, scripts or renderer.
		std::unique_ptr<idUserInterfaceLocal> gui(new idUserInterfaceLocal()); state.gui = gui.get(); gui->SetUniqued(true);
		bool complete = false; unsigned target = 0;
		{
			LegacyObservationScope scope(state);
			const bool loadReturned = gui->InitFromFile(path.c_str()); unsigned visited = 0;
			idWindow* window = LegacyExactWindow(gui->GetDesktop(),ancestry,visited);
			auto* owner = state.Find(window);
			if (owner) target = owner->identity;
			if (owner && !owner->destroyed && owner->success && owner->finished && owner->fixed && state.loaded && state.desktopFixed && loadReturned && !state.failed) {
				target = owner->identity; state.forced = true;
				window->EvalRegs(-1,true); state.forced = false;
				if (!state.evaluations.empty()) {
					const auto& last = state.evaluations.back(); unsigned matches = 0;
					for (const auto& term : state.terms) if (term.window == target && term.emitted && term.result == last.reg && (term.marker != -2 || term.resolved)) ++matches;
					complete = !state.failed && last.window == target && last.mapped && last.desktopFixed && last.forced && matches == 1 && std::isfinite(last.registerValue) && std::isfinite(last.alpha);
				}
			}
		}
		std::string after("",0); state.sourceRechecked = LegacyReadRoot(path.c_str(),after) && after == state.sourceBytes; complete = state.sourceRechecked && complete;
		const std::string output = LegacyObservationJson(state,ancestry,target,complete);
		if (output.size() > 64*1024*1024 || fileSystem->WriteFile(outputPath.c_str(),output.data(),static_cast<int>(output.size())) != static_cast<int>(output.size())) throw std::runtime_error("receipt write failed");
		common->Printf("UI_LEGACY_OBSERVATION_END %s %s (log interval must be diagnostic-free)\n",complete ? "observed" : "refused",outputPath.c_str());
	} catch (const std::exception& error) {
		common->Warning("Legacy observation: %s",error.what()); common->Printf("UI_LEGACY_OBSERVATION_END failed\n");
	}
}
