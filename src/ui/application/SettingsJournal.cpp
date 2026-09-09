// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsJournal.h"
#include <charconv>
#include <limits>

namespace openq4::ui {
namespace {
constexpr const char* Magic = "openQ4 settings recovery 1\n";
bool Fail(std::string& error, const char* message) { error = message; return false; }
std::string Quote(const std::string& text) {
	constexpr char hex[] = "0123456789abcdef";
	std::string out = "\"";
	for (unsigned char c : text) {
		if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
		else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
		else out += static_cast<char>(c);
	}
	return out + '"';
}
std::string Checksum(const std::string& text) {
	std::uint32_t crc = 0xffffffffu;
	for (unsigned char c : text) {
		crc ^= c;
		for (unsigned bit = 0; bit != 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	crc ^= 0xffffffffu;
	std::string out(8,'0');
	for (unsigned i = 0; i != 8; ++i) out[7-i] = "0123456789abcdef"[(crc >> (i*4)) & 15];
	return out;
}
bool ValidMap(const StateValues& values, std::size_t count, std::string& error) {
	if (values.size() > count) return Fail(error,"Settings journal map exceeds its entry budget");
	std::size_t size = 0;
	for (const auto& [key,value] : values) {
		// ParseStateValues bounds the flattened identifier to 128 bytes. Reserve
		// the longest prefix (displayRestore.) so every encoding is decodable.
		if (key.empty() || key.size() > 113 || !ValidStateValue(StateValue(key)) ||
			value.valueless_by_exception() || !ValidStateValue(value)) return Fail(error,"Invalid settings journal key or value");
		for (unsigned char c : key) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return Fail(error,"Invalid settings journal key spelling");
		size += key.size() + (std::holds_alternative<std::string>(value) ? std::get<std::string>(value).size() : sizeof(double));
		if (size > SettingsTransaction::MaxSnapshotBytes) return Fail(error,"Settings journal map exceeds its byte budget");
	}
	return true;
}
bool Valid(const SettingsRecoveryJournal& journal, const std::map<std::string,std::size_t>& catalog, std::string& error) {
	if (journal.attempt.size() != 32 || journal.attempt.find_first_not_of("0123456789abcdef") != std::string::npos ||
		journal.attempt == std::string(32,'0')) return Fail(error,"Invalid persistent settings attempt identity");
	if (journal.state != SettingsJournalState::Pending && journal.state != SettingsJournalState::Confirmed)
		return Fail(error,"Unknown settings journal state");
	if (catalog.empty() || catalog.size() > SettingsTransaction::MaxSettings ||
		journal.baseline.size() != catalog.size() || journal.target.size() != catalog.size())
		return Fail(error,"Settings journal catalog changed");
	for (const auto* values : {&journal.baseline,&journal.target,&journal.patch})
		if (!ValidMap(*values,SettingsTransaction::MaxSettings,error)) return false;
	for (const auto* values : {&journal.displayRestore,&journal.displayTarget,&journal.placement})
		if (values->empty() || !ValidMap(*values,512,error)) return Fail(error,"Missing or invalid settings recovery metadata");
	StateValues expected;
	for (const auto& [key,type] : catalog) {
		const auto old = journal.baseline.find(key), target = journal.target.find(key);
		if (old == journal.baseline.end() || target == journal.target.end() || old->second.index() != type || target->second.index() != type)
			return Fail(error,"Settings journal catalog keys or types changed");
		if (old->second != target->second) expected.emplace(key,target->second);
	}
	if (expected.empty() || expected != journal.patch) return Fail(error,"Settings journal patch does not match its snapshots");
	return true;
}
void Append(StateValues& flat, const char* prefix, const StateValues& values) {
	for (const auto& [key,value] : values) flat.emplace(std::string(prefix)+key,value);
}
} // namespace

bool EncodeSettingsJournal(const SettingsRecoveryJournal& journal,
	const std::map<std::string,std::size_t>& catalog, std::string& bytes, std::string& error) {
	if (!Valid(journal,catalog,error)) return false;
	StateValues flat{{"schema",1.0},{"state",std::string(journal.state == SettingsJournalState::Pending ? "pending" : "confirmed")},
		{"attempt",journal.attempt}};
	Append(flat,"baseline.",journal.baseline); Append(flat,"target.",journal.target); Append(flat,"patch.",journal.patch);
	Append(flat,"displayRestore.",journal.displayRestore); Append(flat,"displayTarget.",journal.displayTarget); Append(flat,"placement.",journal.placement);
	if (flat.size() > 4096) return Fail(error,"Settings journal exceeds its flattened entry budget");
	std::string payload = "{\n";
	for (const auto& [key,value] : flat) {
		if (payload.size() > 2) payload += ",\n";
		payload += Quote(key) + ":";
		if (const auto number = std::get_if<double>(&value)) {
			char buffer[64];
			const auto result = std::to_chars(buffer,buffer+sizeof(buffer),*number,std::chars_format::general,std::numeric_limits<double>::max_digits10);
			if (result.ec != std::errc()) return Fail(error,"Cannot serialize settings journal number");
			payload.append(buffer,result.ptr);
		} else if (const auto boolean = std::get_if<bool>(&value)) payload += *boolean ? "true" : "false";
		else payload += Quote(std::get<std::string>(value));
	}
	payload += "\n}\n";
	std::string candidate = std::string(Magic) + Checksum(payload) + '\n' + payload;
	if (candidate.size() > SettingsJournalMaxBytes) return Fail(error,"Settings journal exceeds its byte budget");
	bytes = std::move(candidate); error.clear(); return true;
}

bool DecodeSettingsJournal(const std::string& bytes,
	const std::map<std::string,std::size_t>& catalog, SettingsRecoveryJournal& journal, std::string& error) {
	const std::string magic = Magic;
	if (bytes.size() > SettingsJournalMaxBytes || bytes.size() <= magic.size()+9 || !bytes.starts_with(magic) || bytes[magic.size()+8] != '\n')
		return Fail(error,"Invalid settings journal header or byte budget");
	const auto payload = bytes.substr(magic.size()+9);
	if (Checksum(payload) != bytes.substr(magic.size(),8)) return Fail(error,"Settings journal checksum mismatch");
	StateValues flat; std::vector<Diagnostic> diagnostics;
	if (!ParseStateValues(payload,flat,diagnostics)) return Fail(error,"Invalid typed settings journal payload");
	SettingsRecoveryJournal candidate;
	if (!flat.contains("schema") || flat.at("schema") != StateValue(1.0) || !flat.contains("state") ||
		!std::holds_alternative<std::string>(flat.at("state")) || !flat.contains("attempt") || !std::holds_alternative<std::string>(flat.at("attempt")))
		return Fail(error,"Unknown settings journal schema");
	const auto state = std::get<std::string>(flat.at("state"));
	if (state != "pending" && state != "confirmed") return Fail(error,"Unknown settings journal commit state");
	candidate.state = state == "pending" ? SettingsJournalState::Pending : SettingsJournalState::Confirmed;
	candidate.attempt = std::get<std::string>(flat.at("attempt"));
	for (const auto& [key,value] : flat) {
		if (key == "schema" || key == "state" || key == "attempt") continue;
		const auto dot = key.find('.');
		if (dot == std::string::npos) return Fail(error,"Unknown settings journal field");
		const auto prefix = key.substr(0,dot); StateValues* values = nullptr;
		if (prefix == "baseline") values = &candidate.baseline;
		else if (prefix == "target") values = &candidate.target;
		else if (prefix == "patch") values = &candidate.patch;
		else if (prefix == "displayRestore") values = &candidate.displayRestore;
		else if (prefix == "displayTarget") values = &candidate.displayTarget;
		else if (prefix == "placement") values = &candidate.placement;
		else return Fail(error,"Unknown settings journal map");
		values->emplace(key.substr(dot+1),value);
	}
	std::string canonical;
	if (!EncodeSettingsJournal(candidate,catalog,canonical,error)) return false;
	if (canonical != bytes) return Fail(error,"Settings journal is not canonical");
	journal = std::move(candidate); error.clear(); return true;
}
} // namespace openq4::ui
