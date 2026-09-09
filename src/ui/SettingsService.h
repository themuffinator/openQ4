// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <string>

// Engine-private application service. Owners are process-unique tokens, not
// window pointers, and survive renderer/font recreation. Release never writes
// CVars or recreates a device from a GUI destructor; Frame owns abandonment.
std::uint64_t UI_SettingsCreateOwner();
void UI_SettingsReleaseOwner(std::uint64_t owner);
void UI_SettingsCloseOwner(std::uint64_t owner);
void UI_SettingsFrame(bool allowWork = true);
bool UI_SettingsBlocksConfigWrite();
bool UI_SettingsStartup(std::string& error);
bool UI_SettingsInitializeDisplay(std::string& error);
bool UI_SettingsStartupActive();
void UI_SettingsShutdown();

// Main/video-thread scope for one synchronous renderer BeginFrame/EndFrame.
// Submitting brackets the exact EndFrame call; Presented follows its successful
// return. Destruction without Presented, nesting, or an intervening submission
// discards every owner draw. This is an API-present receipt, not scanout proof.
class UI_SettingsRenderFrame {
public:
    UI_SettingsRenderFrame();
    ~UI_SettingsRenderFrame();
    UI_SettingsRenderFrame(const UI_SettingsRenderFrame&) = delete;
    UI_SettingsRenderFrame& operator=(const UI_SettingsRenderFrame&) = delete;
    void Submitting();
    void Presented();
};

#ifndef ID_DEDICATED
#include "retained/Document.h"
void UI_SettingsConfirmationDocument(std::uint64_t owner, const openq4::ui::DocumentModel& document);
void UI_SettingsOwnerDrawn(std::uint64_t owner, const std::string& displayedRequest);
bool UI_SettingsOperation(const openq4::ui::Action& action, std::string& error);
bool UI_SettingsInvocation(const openq4::ui::ActionInvocation& action, std::string& error);
bool UI_SettingsDispatch(std::uint64_t owner, const openq4::ui::ActionInvocation& action,
    std::string& error);
// Service-owned values use the reserved settings.* namespace. A document may
// bind any subset, but must declare the exact types and cannot supply CVar
// sources for those keys. Other dictionary values are never replaced.
const std::map<std::string,std::size_t>& UI_SettingsStateSchema();
bool UI_SettingsRead(std::uint64_t owner, openq4::ui::StateValues& values);
#endif
