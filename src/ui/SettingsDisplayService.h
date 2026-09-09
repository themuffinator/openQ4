// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#ifndef ID_DEDICATED
#include "application/SettingsDisplayController.h"
#include "application/SettingsJournal.h"
#include "application/SystemDisplay.h"
#include "application/SystemSettingsHost.h"
#include "../framework/DurableFile.h"
#include "../sys/WindowSettings.h"

// Native adapter for the application controller. Its lifetime exceeds GUI and
// renderer resources. Recovery identities on disk never contain process tokens.
class EngineSettingsDisplayHost final : public openq4::ui::SettingsDisplayHost {
public:
	explicit EngineSettingsDisplayHost(openq4::ui::SystemSettingsHost& host) : settings(host) {}
	bool Prepare(const openq4::ui::SettingsAttempt&, std::string& error) override;
	bool CancelPreparation(std::string& error) override;
	bool Restart(bool restoring, openq4::ui::SettingsDisplayObservation&, std::string& error) override;
	bool Observe(bool restoring, openq4::ui::SettingsDisplayObservation&, std::string& error) override;
	bool PersistConfirmation(const openq4::ui::SettingsAttempt&, std::string& error) override;
	bool Finish(bool restoring, std::string& error) override;
	// Config/preferences have settled. Returns false without deleting evidence.
	bool Startup(std::string& error);
	bool InitializeDisplay(std::string& error);
	void StartupFrame(double now, bool allowWork);
	void Shutdown();
	bool RecoveryActive() const noexcept { return startup || blocked || processLease.IsHeld(); }
	bool StartupActive() const noexcept { return startup; }
	const std::string& RecoveryError() const noexcept { return recoveryError; }

private:
	bool Paths(std::string& error);
	bool WriteJournal(std::string& error);
	bool VerifyJournal(bool allowMissing, std::string& error);
	bool Place(const sysWindowPlacementSnapshot_t& finalState, std::string& error);
	bool ReleasePlacement(std::string& error);
	bool FinishJournal(std::string& error);
	bool ValidateLive(const openq4::ui::StateValues& target, std::string& error);
	bool CommitConfiguration(std::string& error);
	void Clear();
	openq4::ui::SystemSettingsHost& settings;
	openq4::DurableFileLease processLease;
	openq4::ui::SettingsRecoveryJournal journal;
	openq4::ui::SystemDisplayPlan targetPlan, restorePlan;
	rendererDisplayState_t baselineDevice{}, currentDevice{};
	sysWindowPlacementSnapshot_t placement{}, expectedPlacement{}, committedPlacement{};
	openq4::ui::StateValues startupTarget;
	std::uint64_t placementToken = 0;
	std::string journalPath, lockPath, writtenBytes, attemptedBytes, recoveryError;
	bool ownsJournal = false, placed = false, blocked = false, startup = false, startupReady = false;
	bool startupConfirmed = false;
	double startupDeadline = 0, startupLastTime = -1;
};
#endif
