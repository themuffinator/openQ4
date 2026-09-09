// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#ifndef ID_DEDICATED
#include "UserInterfaceManaged.h"
#include <memory>

// Semantic diagnostics for a manager-created retained view. They never read
// or inject physical input. The session dispatches any resulting typed queue.
bool UI_RetainedDiagnostic(idUserInterface* gui, const idCmdArgs& args);
bool UI_RetainedSettingsDocument(idUserInterface* gui);
bool UI_RetainedSettingsCanReturn(idUserInterface* gui);

// Engine adapter for explicit .q4ui resources. The public game ABI remains
// idUserInterface; RmlUi, canonical nodes and device ownership stay private.
class idUserInterfaceRetained final : public idUserInterfaceManaged {
	friend bool UI_RetainedDiagnostic(idUserInterface*, const idCmdArgs&);
	friend bool UI_RetainedSettingsDocument(idUserInterface*);
	friend bool UI_RetainedSettingsCanReturn(idUserInterface*);
public:
	explicit idUserInterfaceRetained(bool managed = true);
	~idUserInterfaceRetained() override;
	const char* Name() const override;
	const char* Comment() const override;
	bool IsInteractive() const override;
	void SetInteractive(bool value) override;
	bool IsUniqued() const override;
	void SetUniqued(bool value) override;
	bool InitFromFile(const char* path, bool rebuild = true, bool cache = true) override;
	const char* HandleEvent(const sysEvent_t* event, int time, bool* updateVisuals = NULL) override;
	void HandleNamedEvent(const char* name) override;
	void Redraw(int time, bool useAspectCorrection = true) override;
	void DrawCursor() override;
	const idDict& State() const override;
	void DeleteStateVar(const char* name) override;
	void SetStateString(const char* name, const char* value) override;
	void SetStateBool(const char* name, bool value) override;
	void SetStateInt(const char* name, int value) override;
	void SetStateFloat(const char* name, float value) override;
	void SetStateVec4(const char* name, const idVec4& value) override;
	const char* GetStateString(const char* name, const char* fallback = "") const override;
	bool GetStateBool(const char* name, const char* fallback = "0") const override;
	int GetStateInt(const char* name, const char* fallback = "0") const override;
	float GetStateFloat(const char* name, const char* fallback = "0") const override;
	bool GetPresentationValue(const char* name, idStr& value) const override;
	bool SetPresentationValue(const char* name, const char* value, bool overrideExpression = true) override;
	bool GetTextInputState(idRectangle& area, float& cursorOffset) const override;
	void StateChanged(int time, bool redraw = false) override;
	const char* Activate(bool activate, int time) override;
	void Trigger(int time) override;
	bool WriteToSaveGame(idFile* file) const override;
	bool ReadFromSaveGame(idFile* file) override;
	void SetKeyBindingNames() override;
	void SetCursor(float x, float y) override;
	float CursorX() override;
	float CursorY() override;
	bool GetMaxTextIndex(const char* name, const char* text, wrapInfo_t& info) const override;
	const char* GetSourceFile() const override;
	ID_TIME_T GetTimeStamp() const override;
	bool Active() const override;
	bool HasInteractiveOverride() const override;
	bool IsMenuGui() const override;
	bool AlwaysThink() const override;
	void RunTimeEvents(int time) override;
	size_t Size() override;
	int NumTransitions() override;
	bool DispatchApplicationActions(const char* command, bool& closeRequested) override;
	const char* PendingApplicationCommand() const override;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
#endif
