// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#ifndef ID_DEDICATED
#include "application/SettingsDisplayController.h"
#include "application/SettingsJournal.h"
#include "application/SystemDisplay.h"
#include "application/SystemSettingsHost.h"
#include "../framework/DurableFile.h"
#include "../renderer/RendererSettingsReports.h"
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
	// Active backend policy, not a promise that every sample count is supported
	// by the current device: a ready OpenGL-family or Vulkan renderer can apply
	// an MSAA change. OpenGL's strict Apply verifies its window's actual count;
	// Vulkan's scene targets take the highest supported count up to the request.
	bool SupportsMultisampling() const;
	// The active back end multisamples its window (the OpenGL family). Vulkan
	// presents a single-sample swapchain, so its display requests ask for none.
	bool WindowMultisampling() const;
	// An automatic attempt proves its effect with later presented frames, so it
	// needs a ready renderer and window presenting now.
	bool ReadyForAutomatic() const;
	// The renderer's latest light-grid load receipt and its engine serial.
	bool LightGridLoad(renderLightGridLoadReceipt_t& receipt, std::uint64_t& serial) const;
	// A renderer fallback change restarts the device and proves itself with the
	// renderer's own selection report, so it needs a presenting OpenGL renderer
	// that has already reported one for its current module.
	bool SupportsRendererSelection() const;
	// A combined attempt (a Performance Preset, or any batch that mixes apply
	// classes) proves itself with later presented frames, so it needs a
	// presenting renderer; one that changes r_renderer also needs the
	// renderer's selection report.
	bool SupportsCombined(const openq4::ui::StateValues& baseline, const openq4::ui::StateValues& target) const;
	// The renderer's latest selection report for its current module.
	bool RendererSelection(renderRendererSelection_t& selection, std::uint64_t& serial) const;
	// The display lists' inputs. The stamp moves with the display topology, the
	// window's display and the labels in effect, so the settings service
	// recaptures only when one of them changes; reading it is cheap.
	void CatalogStamp(openq4::ui::SystemDisplayCatalogStamp& stamp) const;
	bool CaptureCatalogInput(const openq4::ui::SystemDisplayCatalogStamp& stamp,
		openq4::ui::SystemDisplayCatalogInput& input, std::string& error) const;

private:
	bool Paths(std::string& error);
	bool WriteJournal(std::string& error);
	bool VerifyJournal(bool allowMissing, std::string& error);
	bool Place(const sysWindowPlacementSnapshot_t& finalState, std::string& error);
	bool ReleasePlacement(std::string& error);
	bool FinishJournal(std::string& error);
	bool ValidateLive(const openq4::ui::StateValues& target, std::string& error);
	bool ValidateOwned(const openq4::ui::StateValues& owned, const openq4::ui::StateValues& expected, std::string& error);
	bool CommitConfiguration(std::string& error);
	bool NewAttemptIdentity(std::string& attempt, std::string& error);
	bool PrepareDeferred(const openq4::ui::SettingsAttempt&, std::string& error);
	bool StartupDeferred(const std::string& bytes, const openq4::ui::SettingsEffectRecoveryJournal& saved, std::string& error);
	bool PrepareRenderer(const openq4::ui::SettingsAttempt&, std::string& error);
	bool StartupRenderer(const std::string& bytes, const openq4::ui::SettingsEffectRecoveryJournal& record, std::string& error);
	bool PrepareCombined(const openq4::ui::SettingsAttempt&, std::string& error);
	bool StartupCombined(const std::string& bytes, const openq4::ui::SettingsEffectRecoveryJournal& record, std::string& error);
	// The display half of a cold start, for the record now held in journal.
	bool StartupDisplay(const std::string& bytes, bool combined, std::string& error);
	// The attempt restarts the renderer device; changes the display itself
	// (Keep/Revert, and the window may take the target's size); or changes
	// r_renderer, which the restarted renderer must report it resolved.
	bool DeviceWork() const;
	bool DisplayOwned() const;
	bool RendererOwned() const;
	// A combined attempt that changed the speaker layout or EFX reopens the
	// sound device; its attempt waits for that restart and fails if the device
	// does not come back.
	void RestartSound(bool restoring);
	bool SoundReady(bool& ready, std::string& error) const;
	// The renderer resolved r_renderer as asked; serial names its report.
	bool RendererAgrees(const std::string& request, std::uint64_t& serial, std::string& error) const;
	bool RendererCurrent(bool restoring, std::string& error) const;
	std::string RendererRequest(bool restoring) const;
	const openq4::ui::StateValues& SavedDisplay(bool restoring) const;
	void Clear();
	openq4::ui::SystemSettingsHost& settings;
	openq4::DurableFileLease processLease;
	openq4::ui::SettingsRecoveryJournal journal;
	// A deferred attempt (the next-map light-grid preload) journals a schema-2
	// record with only its deferred domain; nothing restarts, and a later
	// presented frame proves the engine still runs before it commits. A
	// renderer attempt (the renderer fallback) journals a schema-2 record with
	// the renderer domain and restarts the device on the same display. A
	// combined attempt journals a schema-2 record with every domain its batch
	// changes: one device restart covers its display, image and renderer
	// resource changes, the sound device reopens for a layout or EFX change,
	// and load-policy changes wait for the loads that read them.
	enum class Kind { Display, Deferred, Renderer, Combined };
	Kind kind = Kind::Display;
	std::uint64_t selectionSerial = 0; // the selection report the last restart produced
	openq4::ui::SettingsEffectRecoveryJournal effects;
	// A deferred or renderer record, and a combined one that changed no
	// display, replays catalog values only: the engine starts the normal way,
	// and the first full frame commits.
	bool catalogStartup = false;
	openq4::ui::SystemDisplayPlan targetPlan, restorePlan;
	rendererDisplayState_t baselineDevice{}, currentDevice{};
	sysWindowPlacementSnapshot_t placement{}, expectedPlacement{}, committedPlacement{};
	openq4::ui::StateValues startupTarget;
	std::uint64_t placementToken = 0;
	std::string journalPath, lockPath, writtenBytes, attemptedBytes, recoveryError;
	bool ownsJournal = false, placed = false, blocked = false, startup = false, startupReady = false;
	bool soundRequested = false; // this attempt's last restart asked the sound device to reopen
	bool startupConfirmed = false;
	double startupDeadline = 0, startupLastTime = -1;
};
#endif
