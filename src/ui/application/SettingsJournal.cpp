// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "SettingsJournal.h"
#include "../../imagetools/ImageRecoveryEnvelope.h"
#include <charconv>
#include <cmath>
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
		if (!SettingsValueEqual(old->second,target->second)) expected.emplace(key,target->second);
	}
	if (expected.empty() || !SettingsValuesEqual(expected,journal.patch)) return Fail(error,"Settings journal patch does not match its snapshots");
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
			std::string encoded;
			const double canonical = SettingsValueEqual(value,StateValue(0.0)) ? 0.0 : *number;
			if (!SettingsNumberText(canonical,SettingsNumberFormat::GeneralRoundTrip,encoded))
				return Fail(error,"Cannot serialize settings journal number");
			payload += encoded;
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

namespace {
constexpr const char* EffectMagic = "openQ4 settings recovery 2\n";
bool EffectFail(std::string& error,const char* message) noexcept {
 try { error=message; } catch (...) { error.clear(); } return false;
}
bool EffectMap(const StateValues& map,std::string& error) {
 if (!ValidMap(map,SettingsEffectMetadataMaxEntries,error)) return false;
 std::size_t bytes=0;
 for (const auto& [key,value]:map) {
  bytes+=key.size()+(std::holds_alternative<std::string>(value)?std::get<std::string>(value).size():sizeof(double));
  if (key.size()>SettingsEffectMetadataMaxKeyBytes || bytes>SettingsEffectMetadataMaxBytes)
   return Fail(error,"Settings effect metadata exceeds its key or byte budget");
 }
 return true;
}
bool ImageEffectMap(const StateValues& map,unsigned direction,const std::string& attempt,std::string& error) {
 const auto codec=map.find("codec");
 if (codec==map.end()) return EffectMap(map,error);
 // A reserved codec field cannot silently fall back to the opaque legacy map.
 std::string raw(0,'\0');
 if (!imageRecovery::Unpack(map,direction,attempt,raw)) return Fail(error,"Invalid versioned image recovery envelope");
 return true;
}
bool Placement2(const StateValues& fields,const StateValues& baseline,const StateValues& target,std::string& error) {
 if (fields.size()!=18) return Fail(error,"Settings effect placement requires exactly eighteen fields");
 for (const auto* direction:{"baseline.","target."}) {
  const std::string prefix=direction;
  for (const auto* name:{"x","y","width","height","normalX","normalY","normalWidth","normalHeight"}) {
   const auto i=fields.find(prefix+name);
   if (i==fields.end() || !std::holds_alternative<double>(i->second)) return Fail(error,"Invalid settings effect placement field");
   const double n=std::get<double>(i->second);
   if (n<(std::numeric_limits<int>::min)() || n>(std::numeric_limits<int>::max)() ||
    !SettingsValueEqual(i->second,StateValue(double(static_cast<int>(n)))))
    return Fail(error,"Settings effect placement is not an exact integer");
  }
  const auto valid=fields.find(prefix+"normalValid");
  if (valid==fields.end() || !std::holds_alternative<bool>(valid->second)) return Fail(error,"Invalid settings effect normal placement");
  const auto n=[&](const char* key){return std::get<double>(fields.at(prefix+key));};
  if (n("width")<320 || n("width")>16384 || n("height")<240 || n("height")>16384 ||
   (std::get<bool>(valid->second) && (n("normalWidth")<=0 || n("normalWidth")>16384 || n("normalHeight")<=0 || n("normalHeight")>16384)))
   return Fail(error,"Settings effect placement dimensions are invalid");
  const auto& values=prefix=="baseline."?baseline:target;
  if (!SettingsValueEqual(fields.at(prefix+"width"),values.at("r_windowWidth")) ||
   !SettingsValueEqual(fields.at(prefix+"height"),values.at("r_windowHeight")))
   return Fail(error,"Settings effect placement contradicts catalog window dimensions");
 }
 return true;
}
bool Valid2(const SettingsEffectRecoveryJournal& j,const std::map<std::string,std::size_t>& catalog,std::string& error) {
 if (j.attempt.size()!=32 || j.attempt.find_first_not_of("0123456789abcdef")!=std::string::npos || j.attempt==std::string(32,'0'))
  return Fail(error,"Invalid persistent settings attempt identity");
 if (j.state!=SettingsJournalState::Pending && j.state!=SettingsJournalState::Confirmed)
  return Fail(error,"Unknown settings journal state");
 if (!ValidateSettingsEffectPlan(j.plan,j.baseline,j.target,catalog,error)) return false;
 for (const auto* map:{&j.baseline,&j.target,&j.patch}) if (!ValidMap(*map,SettingsTransaction::MaxSettings,error)) return false;
 StateValues expected;
 for (const auto& [key,value]:j.target) if (!SettingsValueEqual(j.baseline.at(key),value)) expected.emplace(key,value);
 if (!SettingsValuesEqual(expected,j.patch)) return Fail(error,"Settings effect journal patch does not match its snapshots");
 struct Metadata { const StateValues* map; unsigned domains; };
 const Metadata metadata[]={{&j.displayRestore,19},{&j.displayTarget,19},{&j.placement,19},
  {&j.imageRestore,2},{&j.imageTarget,2},{&j.resourceRestore,16},{&j.resourceTarget,16},
  {&j.audioRestore,4},{&j.audioTarget,4},{&j.deferredRestore,8},{&j.deferredTarget,8}};
 for (const auto& item:metadata) {
  const bool required=(j.plan.domainMask & item.domains)!=0;
  if (required==item.map->empty()) return Fail(error,"Settings effect journal has missing or surplus domain metadata");
  if (item.map==&j.imageRestore || item.map==&j.imageTarget) {
   if (!ImageEffectMap(*item.map,item.map==&j.imageRestore?1u:2u,j.attempt,error)) return false;
  } else if (!EffectMap(*item.map,error)) return false;
 }
 if ((j.plan.domainMask & 19u) && !Placement2(j.placement,j.baseline,j.target,error)) return false;
 return true;
}
void AppendEffect(StateValues& flat,const char* prefix,const StateValues& values) {
 for (const auto& [key,value]:values) {
  std::string name(prefix); name+=key;
  flat.emplace(name,value); // Copy stable lvalues; debug-STL moves may allocate in noexcept.
 }
}
bool EncodeEffectFlat(const StateValues& flat,std::string& bytes,std::string& error) {
 if (flat.size()>4096) return Fail(error,"Settings effect journal exceeds its flattened entry budget");
 std::string payload="{\n";
 for (const auto& [key,value]:flat) {
  if (payload.size()>2) payload+=",\n";
  payload+=Quote(key); payload+=':';
  if (const auto number=std::get_if<double>(&value)) {
   std::string encoded(0,'\0');
   const double canonical=SettingsValueEqual(value,StateValue(0.0))?0.0:*number;
   if (!SettingsNumberText(canonical,SettingsNumberFormat::GeneralRoundTrip,encoded)) return Fail(error,"Cannot serialize settings effect journal number");
   payload+=encoded;
  } else if (const auto boolean=std::get_if<bool>(&value)) payload+=*boolean?"true":"false";
  else payload+=Quote(std::get<std::string>(value));
 }
 payload+="\n}\n";
 std::string candidate(EffectMagic); candidate+=Checksum(payload); candidate+='\n'; candidate+=payload;
 if (candidate.size()>SettingsJournalMaxBytes) return Fail(error,"Settings effect journal exceeds its byte budget");
 bytes.swap(candidate); error.clear(); return true;
}
bool ReadUnsigned(const StateValues& flat,const char* key,unsigned maximum,unsigned& output) {
 const auto i=flat.find(key); if (i==flat.end() || !std::holds_alternative<double>(i->second)) return false;
 const double value=std::get<double>(i->second);
 if (value<0 || value>maximum) return false;
 const auto number=static_cast<unsigned>(value);
 if (!SettingsValueEqual(i->second,StateValue(double(number)))) return false;
 output=number; return true;
}
bool ReadText(const StateValues& flat,const char* key,std::string& output) {
 const auto i=flat.find(key); if (i==flat.end() || !std::holds_alternative<std::string>(i->second)) return false;
 output=std::get<std::string>(i->second); return true;
}
bool DecodeEffect(const std::string& bytes,const std::map<std::string,std::size_t>& catalog,
 SettingsEffectRecoveryJournal& j,std::string& error) {
 const std::string magic=EffectMagic;
 if (bytes.size()>SettingsJournalMaxBytes || bytes.size()<=magic.size()+9 || !bytes.starts_with(magic) || bytes[magic.size()+8]!='\n')
  return Fail(error,"Invalid settings effect journal header or byte budget");
 const auto payload=bytes.substr(magic.size()+9);
 if (Checksum(payload)!=bytes.substr(magic.size(),8)) return Fail(error,"Settings effect journal checksum mismatch");
 StateValues flat; std::vector<Diagnostic> diagnostics(0);
 if (!ParseStateValues(payload,flat,diagnostics)) return Fail(error,"Invalid typed settings effect journal payload");
 unsigned schema=0; std::string state(0,'\0'),completion(0,'\0'),strategy(0,'\0');
 if (!ReadUnsigned(flat,"schema",2,schema) || schema!=2 || !ReadText(flat,"attempt",j.attempt) || !ReadText(flat,"state",state) ||
  !ReadUnsigned(flat,"plan.version",1,j.plan.version) || j.plan.version!=1 ||
  !ReadUnsigned(flat,"plan.changeMask",63,j.plan.changeMask) || !ReadUnsigned(flat,"plan.domainMask",31,j.plan.domainMask) ||
  !ReadText(flat,"plan.completion",completion) || !ReadText(flat,"plan.rendererStrategy",strategy))
  return Fail(error,"Invalid settings effect journal schema or plan");
 if (state!="pending" && state!="confirmed") return Fail(error,"Unknown settings effect commit state");
 if (completion!="display-confirmed" && completion!="automatic") return Fail(error,"Unknown settings effect completion");
 if (strategy!="none" && strategy!="coalesced-device") return Fail(error,"Unknown settings effect renderer strategy");
 j.state=state=="pending"?SettingsJournalState::Pending:SettingsJournalState::Confirmed;
 j.plan.completion=completion=="display-confirmed"?SettingsEffectCompletion::DisplayConfirmed:SettingsEffectCompletion::Automatic;
 j.plan.rendererStrategy=strategy=="none"?SettingsRendererStrategy::None:SettingsRendererStrategy::CoalescedDevice;
 for (const auto& [key,value]:flat) {
  if (key=="schema" || key=="state" || key=="attempt" || key=="plan.version" || key=="plan.changeMask" ||
   key=="plan.domainMask" || key=="plan.completion" || key=="plan.rendererStrategy") continue;
  const auto dot=key.find('.');
  if (dot==std::string::npos) return Fail(error,"Unknown settings effect journal field");
  const auto prefix=key.substr(0,dot); StateValues* map=nullptr;
  if (prefix=="baseline") map=&j.baseline;
  else if (prefix=="target") map=&j.target;
  else if (prefix=="patch") map=&j.patch;
  else if (prefix=="displayRestore") map=&j.displayRestore;
  else if (prefix=="displayTarget") map=&j.displayTarget;
  else if (prefix=="placement") map=&j.placement;
  else if (prefix=="imageRestore") map=&j.imageRestore;
  else if (prefix=="imageTarget") map=&j.imageTarget;
  else if (prefix=="resourceRestore") map=&j.resourceRestore;
  else if (prefix=="resourceTarget") map=&j.resourceTarget;
  else if (prefix=="audioRestore") map=&j.audioRestore;
  else if (prefix=="audioTarget") map=&j.audioTarget;
  else if (prefix=="deferredRestore") map=&j.deferredRestore;
  else if (prefix=="deferredTarget") map=&j.deferredTarget;
  else return Fail(error,"Unknown settings effect journal map");
  const auto subkey=key.substr(dot+1); map->emplace(subkey,value);
 }
 std::string canonical(0,'\0');
 if (!EncodeSettingsEffectJournal(j,catalog,canonical,error)) return false;
 if (canonical!=bytes) return Fail(error,"Settings effect journal is not canonical");
 return true;
}
} // namespace
bool EncodeSettingsEffectJournal(const SettingsEffectRecoveryJournal& journal,
 const std::map<std::string,std::size_t>& catalog,std::string& bytes,std::string& error) {
 try {
  if (!Valid2(journal,catalog,error)) return false;
  StateValues flat;
  const auto text=[&](const char* key,const char* value) {
   flat.emplace(std::piecewise_construct,std::forward_as_tuple(key),
    std::forward_as_tuple(std::in_place_type<std::string>,value));
  };
  flat.emplace("schema",2.0); text("state",journal.state==SettingsJournalState::Pending?"pending":"confirmed");
  flat.emplace("attempt",journal.attempt);
  flat.emplace("plan.version",double(journal.plan.version)); flat.emplace("plan.changeMask",double(journal.plan.changeMask));
  flat.emplace("plan.domainMask",double(journal.plan.domainMask));
  text("plan.completion",journal.plan.completion==SettingsEffectCompletion::DisplayConfirmed?"display-confirmed":"automatic");
  text("plan.rendererStrategy",journal.plan.rendererStrategy==SettingsRendererStrategy::None?"none":"coalesced-device");
  AppendEffect(flat,"baseline.",journal.baseline); AppendEffect(flat,"target.",journal.target); AppendEffect(flat,"patch.",journal.patch);
  AppendEffect(flat,"displayRestore.",journal.displayRestore); AppendEffect(flat,"displayTarget.",journal.displayTarget); AppendEffect(flat,"placement.",journal.placement);
  AppendEffect(flat,"imageRestore.",journal.imageRestore); AppendEffect(flat,"imageTarget.",journal.imageTarget);
  AppendEffect(flat,"resourceRestore.",journal.resourceRestore); AppendEffect(flat,"resourceTarget.",journal.resourceTarget);
  AppendEffect(flat,"audioRestore.",journal.audioRestore); AppendEffect(flat,"audioTarget.",journal.audioTarget);
  AppendEffect(flat,"deferredRestore.",journal.deferredRestore); AppendEffect(flat,"deferredTarget.",journal.deferredTarget);
  return EncodeEffectFlat(flat,bytes,error);
 } catch (...) { return EffectFail(error,"Settings effect journal allocation failed"); }
}
bool EncodeSettingsJournalRecord(const SettingsJournalRecord& journal,
 const std::map<std::string,std::size_t>& catalog,std::string& bytes,std::string& error) {
 try {
  if (!journal.Value()) return Fail(error,"Settings journal record is empty");
  if (const auto old=std::get_if<SettingsRecoveryJournal>(journal.Value())) return EncodeSettingsJournal(*old,catalog,bytes,error);
  return EncodeSettingsEffectJournal(std::get<SettingsEffectRecoveryJournal>(*journal.Value()),catalog,bytes,error);
 } catch (...) { return EffectFail(error,"Settings journal record allocation failed"); }
}
bool DecodeSettingsJournalRecord(const std::string& bytes,
 const std::map<std::string,std::size_t>& catalog,SettingsJournalRecord& journal,std::string& error) {
 try {
  // Construct the staged alternative before touching the previous owned value.
  // Do not move a map-bearing variant into a live output under debug iterators.
  std::unique_ptr<SettingsJournalValue> candidate;
  if (bytes.starts_with(Magic)) {
   candidate.reset(new SettingsJournalValue(std::in_place_type<SettingsRecoveryJournal>));
   if (!DecodeSettingsJournal(bytes,catalog,std::get<SettingsRecoveryJournal>(*candidate),error)) return false;
  } else if (bytes.starts_with(EffectMagic)) {
   candidate.reset(new SettingsJournalValue(std::in_place_type<SettingsEffectRecoveryJournal>));
   if (!DecodeEffect(bytes,catalog,std::get<SettingsEffectRecoveryJournal>(*candidate),error)) return false;
  } else return Fail(error,"Unknown settings journal schema header");
  std::unique_ptr<const SettingsJournalValue> immutable=std::move(candidate);
  journal.value.swap(immutable); error.clear(); return true;
 } catch (...) { return EffectFail(error,"Settings journal record allocation failed"); }
}
bool PackSettingsImageRecovery(const std::string& raw,unsigned direction,const std::string& attempt,StateValues& output) {
 return imageRecovery::Pack(raw,direction,attempt,output);
}
bool UnpackSettingsImageRecovery(const StateValues& fields,unsigned direction,const std::string& attempt,std::string& output) {
 return imageRecovery::Unpack(fields,direction,attempt,output);
}
} // namespace openq4::ui
