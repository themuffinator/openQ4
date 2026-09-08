// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "LegacyGuiImport.h"
#include <string>

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
