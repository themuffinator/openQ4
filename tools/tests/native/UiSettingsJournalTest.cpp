// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/application/SettingsJournal.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace openq4::ui;
#undef assert
#define assert(expression) do { if (!(expression)) { std::fprintf(stderr,"Journal check failed at line %d: %s\n",__LINE__,#expression); std::abort(); } } while (false)
static const std::map<std::string,std::size_t> Catalog{{"brightness",0},{"fullscreen",1},{"mode",2}};
static SettingsRecoveryJournal Sample() {
	SettingsRecoveryJournal journal;
	journal.attempt = "0123456789abcdef0123456789abcdef";
	journal.baseline = {{"brightness",1.0},{"fullscreen",false},{"mode",std::string("custom")}};
	journal.target = journal.baseline; journal.target["brightness"] = 1.25;
	journal.patch = {{"brightness",1.25}};
	journal.displayRestore = {{"name",std::string("Display \\" + std::string("\"\n\t") + "é")},{"width",1280.0}};
	journal.displayTarget = journal.displayRestore; journal.displayTarget["width"] = 960.0;
	journal.placement = {{"x",-1920.0},{"normal",true}};
	return journal;
}
static void RoundTrip() {
	std::string bytes,error; const auto original = Sample();
	assert(EncodeSettingsJournal(original,Catalog,bytes,error)); assert(error.empty());
	SettingsRecoveryJournal decoded;
	assert(DecodeSettingsJournal(bytes,Catalog,decoded,error));
	assert(decoded.attempt == original.attempt && decoded.baseline == original.baseline && decoded.target == original.target);
	assert(decoded.patch == original.patch && decoded.placement == original.placement && decoded.displayRestore == original.displayRestore);
	std::string repeated; assert(EncodeSettingsJournal(decoded,Catalog,repeated,error) && repeated == bytes);
	decoded.state = SettingsJournalState::Confirmed;
	assert(EncodeSettingsJournal(decoded,Catalog,bytes,error)); assert(DecodeSettingsJournal(bytes,Catalog,decoded,error));
	assert(decoded.state == SettingsJournalState::Confirmed);
}
static void Corruption() {
	std::string bytes,error; assert(EncodeSettingsJournal(Sample(),Catalog,bytes,error));
	for (std::size_t i = 0; i != bytes.size(); ++i) {
		auto bad = bytes; bad[i] ^= 1;
		auto destination = Sample(); destination.attempt = std::string(32,'f');
		assert(!DecodeSettingsJournal(bad,Catalog,destination,error)); assert(!error.empty());
		assert(destination.attempt == std::string(32,'f'));
	}
	for (std::size_t i = 0; i != bytes.size(); ++i) {
		auto destination = Sample(); assert(!DecodeSettingsJournal(bytes.substr(0,i),Catalog,destination,error));
	}
	auto destination = Sample();
	assert(!DecodeSettingsJournal(bytes+"garbage",Catalog,destination,error));
	assert(!DecodeSettingsJournal(std::string(SettingsJournalMaxBytes+1,'x'),Catalog,destination,error));
	auto changed = Catalog; changed["brightness"] = 1;
	assert(!DecodeSettingsJournal(bytes,changed,destination,error));
}
static void InvalidSnapshots() {
	std::string bytes = "untouched",error;
	for (int scenario = 0; scenario != 12; ++scenario) {
		auto value = Sample();
		switch (scenario) {
		case 0: value.patch.clear(); break;
		case 1: value.patch["brightness"] = 1.5; break;
		case 2: value.target.erase("mode"); break;
		case 3: value.target["brightness"] = true; break;
		case 4: value.attempt = std::string(32,'0'); break;
		case 5: value.attempt[0] = 'G'; break;
		case 6: value.displayRestore.clear(); break;
		case 7: value.placement.clear(); break;
		case 8: value.displayTarget["width"] = std::numeric_limits<double>::infinity(); break;
		case 9: value.baseline["mode"] = std::string(65537,'a'); break;
		case 10: value.displayRestore["bad/key"] = 0.0; break;
		case 11: value.state = static_cast<SettingsJournalState>(99); break;
		}
		assert(!EncodeSettingsJournal(value,Catalog,bytes,error)); assert(bytes == "untouched");
	}
}
int main() { RoundTrip(); Corruption(); InvalidSnapshots(); std::puts("Settings journal canonical typed roundtrip, corruption, truncation, catalog and budget checks passed"); }
