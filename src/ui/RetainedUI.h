// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <string>
#include <vector>
#include "retained/DocumentEdit.h"

namespace openq4::ui { class Runtime; struct Viewport; struct Diagnostic; }
namespace openq4::ui { struct RuntimeDocumentOptions; }
struct retainedUIView_t;
enum class retainedUIViewEvent_t { BeforeResourceReset, Restored, Failed };
using retainedUIViewCallback_t = void (*)(void*, retainedUIViewEvent_t);

// Engine-thread registration for independent retained documents. Each view owns
// a stable Runtime object sharing the engine's font/material/composition host.
// Callbacks may quarantine owner input/commands; they must not create/destroy
// views, render, or reenter this service. Destroy a view before its owner dies.
retainedUIView_t* RetainedUI_CreateView(retainedUIViewCallback_t callback = nullptr, void* owner = nullptr);
void RetainedUI_DestroyView(retainedUIView_t* view);
openq4::ui::Runtime* RetainedUI_ViewRuntime(retainedUIView_t* view);
bool RetainedUI_LoadView(retainedUIView_t* view, const std::string& source, const std::string& path,
	std::vector<openq4::ui::Diagnostic>& diagnostics);
// Prepare before calling Runtime operations. Resource changes rebuild all live
// views together. A failed view remains registered/address-stable but unloaded.
bool RetainedUI_PrepareView(retainedUIView_t* view);
// Exact, process-local canvas/history publication. Prepared storage must be
// explicitly destroyed after success/failure; no borrowed old model survives
// successful publication. This never performs native owner retirement.
struct retainedUIEditIdentity_t {
    std::uint64_t view=0,revision=0,resources=0,canvasLifetime=0,canvasRevision=0;
    bool operator==(const retainedUIEditIdentity_t&) const = default;
};
struct retainedUIPreparedEdit_t;
bool RetainedUI_QueryEditIdentity(retainedUIView_t*,retainedUIEditIdentity_t&) noexcept;
retainedUIPreparedEdit_t* RetainedUI_PrepareEdit(retainedUIView_t*,retainedUIEditIdentity_t,
    openq4::ui::DocumentEdit&,openq4::ui::DocumentEditIdentity,std::span<const openq4::ui::DocumentEditOperation>,
    const openq4::ui::RuntimeDocumentOptions&,std::vector<openq4::ui::Diagnostic>&);
retainedUIPreparedEdit_t* RetainedUI_PrepareHistory(retainedUIView_t*,retainedUIEditIdentity_t,
    openq4::ui::DocumentEdit&,openq4::ui::DocumentEditIdentity,bool redo,
    const openq4::ui::RuntimeDocumentOptions&,std::vector<openq4::ui::Diagnostic>&);
bool RetainedUI_PublishEdit(retainedUIView_t*,retainedUIPreparedEdit_t*,openq4::ui::DocumentEditReceipt&) noexcept;
void RetainedUI_DestroyPreparedEdit(retainedUIPreparedEdit_t*);
// Root/UI-viewport output only. This does not render world surfaces or permit
// callers to reuse layer zero as an arbitrary render texture.
bool RetainedUI_DrawViewRoot(retainedUIView_t* view, const openq4::ui::Viewport& viewport);
bool RetainedUI_DefaultViewport(openq4::ui::Viewport& viewport);
double RetainedUI_PresentationTime();
// Call immediately after renderSystem->EndFrame has consumed its front-end
// command chain. Allows a deferred resource rebuild before the next draw; this
// is not a GPU fence and does not replace renderer-owned resource retirement.
void RetainedUI_FrameSubmitted();

struct sysEvent_s;
struct retainedUIInput_t {
	enum kind_t : int { KEY, POINTER, POINTER_BUTTON, FOCUS, POINTER_LEAVE, CANCEL } kind = KEY;
	int source = 0, key = 0;
	int down = 0, repeated = 0;
	float x = 0, y = 0;
};

void RetainedUI_Init();
void RetainedUI_Shutdown();
void RetainedUI_Draw();
void RetainedUI_Close();
// Called after the shared language dictionary finishes loading, even when
// its language/code page is unchanged. Resources rebuild on the engine thread.
void RetainedUI_LanguageChanged();
// IsOpen is safe for the async usercmd thread; all other entry points run on
// the engine thread. Preview documents do not acquire application input.
bool RetainedUI_IsOpen();
unsigned RetainedUI_InputGeneration();
void RetainedUI_FrameInput();
bool RetainedUI_ProcessEvent(const sysEvent_s* event);
void RetainedUI_QueueInput(const retainedUIInput_t& input, int time);
// Engine-owned bridge, called with the usercmd critical section held.
void Usercmd_RetainedInputChanged();
