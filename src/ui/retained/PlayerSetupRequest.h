// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Document.h"

namespace openq4::ui {

enum class PlayerSetupField { Name, Clan, Model, Page, Rail, Rotation, ForceModel };
struct PlayerSetupRequest {
    PlayerSetupField field = PlayerSetupField::Name;
    StateValue value;
    std::uint32_t catalog = 0;
    std::string expected;
};
inline constexpr unsigned PlayerSetupModelRows = 128;
inline constexpr std::size_t PlayerSetupRequestBytes = 4096;
inline constexpr const char PlayerSetupRequestPrefix[] = "playerSetupApply ";

// A bounded typed proposal, transported inside the existing engine-private
// session-request FIFO. The JSON payload is data, never console command text.
bool ValidPlayerSetupAction(const Action&);
bool ResolvePlayerSetupRequest(const StateValues&, PlayerSetupRequest&, std::string& error);
bool EncodePlayerSetupRequest(const StateValues&, std::string&, std::string& error);
bool DecodePlayerSetupRequest(const std::string&, PlayerSetupRequest&, std::string& error);

}
