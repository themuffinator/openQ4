// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "PlayerSetupRequest.h"
#include "State.h"
#include <json/json.h>
#include <cmath>
#include <limits>

namespace openq4::ui {
namespace {
std::optional<PlayerSetupField> Field(const std::string& name) {
    if (name=="name") return PlayerSetupField::Name;
    if (name=="clan") return PlayerSetupField::Clan;
    if (name=="model") return PlayerSetupField::Model;
    if (name=="page") return PlayerSetupField::Page;
    if (name=="rail") return PlayerSetupField::Rail;
    if (name=="rotation") return PlayerSetupField::Rotation;
    if (name=="force_model") return PlayerSetupField::ForceModel;
    return {};
}
bool Literal(const Expression& expression) {
    return expression.op.empty() && expression.state.empty() && expression.presentation.empty() && !expression.inputValue;
}
bool ValidFieldValue(PlayerSetupField field, const StateValue& value) {
    if (!ValidStateValue(value)) return false;
    if (field==PlayerSetupField::Name || field==PlayerSetupField::Clan) {
        const auto* text=std::get_if<std::string>(&value);
        if (!text || text->size()>(field==PlayerSetupField::Name ? 23u : 12u) || (field==PlayerSetupField::Name && text->empty())) return false;
        for (unsigned char c:*text) if (c<32 || c==127) return false;
        return true;
    }
    const auto* number=std::get_if<double>(&value);
    if (!number || !std::isfinite(*number)) return false;
    if (field==PlayerSetupField::Rotation) return *number>=0 && *number<=360;
    if (*number!=std::floor(*number)) return false;
    if (field==PlayerSetupField::Model) return *number>=0 && *number<PlayerSetupModelRows;
    // A row of the catalog page, or -1 for no forced model.
    if (field==PlayerSetupField::ForceModel) return *number>=-1 && *number<PlayerSetupModelRows;
    if (field==PlayerSetupField::Page) return *number==-1 || *number==1;
    return *number>=0 && *number<7;
}
}
bool ValidPlayerSetupAction(const Action& action) {
    if (action.operation!="player.setup.apply" || action.arguments.size()!=4) return false;
    const auto field=action.arguments.find("field"), value=action.arguments.find("value"), catalog=action.arguments.find("catalog"), expected=action.arguments.find("expected");
    if (field==action.arguments.end() || value==action.arguments.end() || catalog==action.arguments.end() || expected==action.arguments.end() ||
        !Literal(field->second) || field->second.type!=2 || !std::holds_alternative<std::string>(field->second.literal) || catalog->second.type!=0 || catalog->second.inputValue ||
        expected->second.type!=2 || expected->second.inputValue) return false;
    const auto name=Field(std::get<std::string>(field->second.literal));
    if (!name || value->second.type!=(*name==PlayerSetupField::Name || *name==PlayerSetupField::Clan ? 2u : 0u)) return false;
    // The proposal is the invocation input or an authored literal, never a
    // mutable GUI-state read deferred until another event overwrites it.
    return value->second.inputValue || (Literal(value->second) && ValidFieldValue(*name,value->second.literal));
}
bool ResolvePlayerSetupRequest(const StateValues& values, PlayerSetupRequest& out, std::string& error) {
    error.clear();
    const auto field=values.find("field"), value=values.find("value"), catalog=values.find("catalog"), expected=values.find("expected");
    if (values.size()!=4 || field==values.end() || value==values.end() || catalog==values.end() || expected==values.end() ||
        !std::holds_alternative<std::string>(field->second) || !std::holds_alternative<double>(catalog->second) ||
        !std::holds_alternative<std::string>(expected->second)) { error="Malformed Player Setup proposal"; return false; }
    const auto name=Field(std::get<std::string>(field->second));
    const double epoch=std::get<double>(catalog->second);
    if (!name || !ValidFieldValue(*name,value->second) || !std::isfinite(epoch) || epoch<1 || epoch>std::numeric_limits<std::uint32_t>::max() ||
        epoch!=std::floor(epoch) || !ValidStateValue(expected->second) || std::get<std::string>(expected->second).size()>1024) {
        error="Player Setup proposal exceeds its field or catalog bounds"; return false;
    }
    PlayerSetupRequest candidate; candidate.field=*name; candidate.value=value->second;
    candidate.catalog=static_cast<std::uint32_t>(epoch); candidate.expected=std::get<std::string>(expected->second);
    out=std::move(candidate); return true;
}
bool EncodePlayerSetupRequest(const StateValues& values, std::string& out, std::string& error) {
    PlayerSetupRequest checked;
    if (!ResolvePlayerSetupRequest(values,checked,error)) return false;
    Json::Value object(Json::objectValue);
    for (const auto& [name,value]:values) {
        if (const auto* text=std::get_if<std::string>(&value)) object[name]=*text;
        else object[name]=std::get<double>(value);
    }
    Json::StreamWriterBuilder writer; writer["indentation"]="";
    auto candidate=Json::writeString(writer,object);
    if (candidate.size()>PlayerSetupRequestBytes) { error="Player Setup proposal payload is too large"; return false; }
    out=std::move(candidate); return true;
}
bool DecodePlayerSetupRequest(const std::string& source, PlayerSetupRequest& out, std::string& error) {
    if (source.size()>PlayerSetupRequestBytes) { error="Player Setup proposal payload is too large"; return false; }
    StateValues values; std::vector<Diagnostic> diagnostics;
    if (!ParseStateValues(source,values,diagnostics)) { error="Malformed Player Setup JSON payload"; return false; }
    return ResolvePlayerSetupRequest(values,out,error);
}
}
