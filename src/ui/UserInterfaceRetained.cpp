// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "../idlib/precompiled.h"
#include "UserInterfaceRetained.h"
#ifndef ID_DEDICATED
#include "RetainedUI.h"
#include "SettingsService.h"
#include "retained/Runtime.h"
#include "retained/Input.h"
#include "application/SettingsTransaction.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>

#if defined(USE_SDL3)
bool Sys_SDL_IsGameWindowFocused(void);
#endif

namespace {
using namespace openq4::ui;
constexpr const char* ActionMarker = "openq4-retained-actions";
constexpr unsigned SaveTag = 0x49553451; // Q4UI, little endian through idFile.
constexpr int MaxStateEntries = 4096, MaxStringBytes = 65536;
constexpr int MaxStateBytes = 16 * 1024 * 1024;
constexpr int MaxFrameBytes = static_cast<int>(Runtime::MaxSnapshotBytes) + MaxStateBytes + MaxStateEntries*8 + 64;
std::vector<idUserInterfaceRetained*> diagnosticViews;

bool ConvertState(const char* text, size_t type, StateValue& value) {
	if (!text) return false;
	if (type == 2) value = std::string(text);
	else if (type == 1) {
		if (!idStr::Cmp(text,"1") || !idStr::Cmp(text,"true")) value = true;
		else if (!idStr::Cmp(text,"0") || !idStr::Cmp(text,"false")) value = false;
		else return false;
	} else {
		char* end = nullptr;
		const double number = std::strtod(text,&end);
		if (end == text || *end || !std::isfinite(number)) return false;
		value = number;
	}
	return ValidStateValue(value);
}

bool ApplicationState(const DocumentModel& model, const idDict& dictionary, StateValues& result, std::string& error) {
	for (const auto& [name,declaration] : model.state) {
		if (!declaration.cvar.empty() || name.starts_with("settings.")) continue;
		StateValue value = declaration.initial;
		const auto* entry = dictionary.FindKey(name.c_str());
		if (entry && !ConvertState(entry->GetValue().c_str(),declaration.initial.index(),value)) {
			error = "Invalid application state: " + name; return false;
		}
		result.emplace(name,std::move(value));
	}
	return true;
}

bool ValidOperation(const Action& action) {
	if (action.operation.starts_with("settings.system.")) { std::string error; return UI_SettingsOperation(action,error); }
	if (action.operation == "ui.dismiss") return action.arguments.empty();
	const auto value = action.arguments.find("value");
	if (action.arguments.size() != 1 || value == action.arguments.end()) return false;
	return (action.operation == "settings.brightness.set" && value->second.type == 0) ||
		(action.operation == "settings.shadows.set" && value->second.type == 1);
}

bool ValidInvocation(const ActionInvocation& invocation, std::string& error) {
	if (invocation.operation.starts_with("settings.system.")) return UI_SettingsInvocation(invocation,error);
	if (invocation.operation == "ui.dismiss" && invocation.arguments.empty()) return true;
	const auto value = invocation.arguments.find("value");
	if (invocation.arguments.size() == 1 && value != invocation.arguments.end() && ValidStateValue(value->second)) {
		if (invocation.operation == "settings.brightness.set" && std::holds_alternative<double>(value->second)) {
			const double number = std::get<double>(value->second);
			if (number >= .5 && number <= 2) return true;
			error = "Brightness request must be between 0.5 and 2.0"; return false;
		}
		if (invocation.operation == "settings.shadows.set" && std::holds_alternative<bool>(value->second)) return true;
	}
	error = "Unsupported application invocation or arguments: "+invocation.action; return false;
}

void TraceInvocation(const char* path, const ActionInvocation& invocation, bool close) {
	if (!cvarSystem->GetCVarBool("ui_retainedTrace")) return;
	std::string text = "-";
	const auto value = invocation.arguments.find("value");
	if (value != invocation.arguments.end()) {
		PresentationValue number;
		if (std::holds_alternative<double>(value->second)) number.data[0] = std::get<double>(value->second);
		else if (std::holds_alternative<bool>(value->second)) { number.type = PresentationType::Boolean; number.data[0] = std::get<bool>(value->second) ? 1 : 0; }
		text = FormatPresentationValue(number);
	}
	common->Printf("RETAINED_GUI_DISPATCH path=%s operation=%s value=%s brightness=%.6f shadows=%d close=%d\n",
		path,invocation.operation.c_str(),text.c_str(),cvarSystem->GetCVarFloat("r_brightness"),
		cvarSystem->GetCVarBool("r_shadows") ? 1 : 0,close ? 1 : 0);
}

bool ValidateApplication(const DocumentModel& model, std::string& error) {
	idDict names;
	for (const auto& [name,declaration] : model.state) {
		if (!idStr::Icmp(name.c_str(),"name")) { error = "State ID is reserved by the GUI source contract: " + name; return false; }
		if (names.FindKey(name.c_str())) { error = "State IDs collide in the game dictionary: " + name; return false; }
		if (!idStr::Icmpn(name.c_str(),"settings.",9)) {
			const auto& schema = UI_SettingsStateSchema(); const auto field = schema.find(name);
			if (field == schema.end() || field->second != declaration.initial.index() || !declaration.cvar.empty()) {
				error = "Invalid service-owned settings state declaration: " + name; return false;
			}
		}
		names.Set(name.c_str(),"1");
	}
	for (const auto& [name,event] : model.events) {
		std::vector<const EventStep*> steps;
		for (const auto& step : event.steps) steps.push_back(&step);
		while (!steps.empty()) {
			const auto* step = steps.back(); steps.pop_back();
			for (const auto& [key,value] : step->values) if (!idStr::Icmpn(key.c_str(),"settings.",9)) {
				error = "Programs cannot overwrite service-owned settings state: " + key; return false;
			}
			for (const auto& child : step->thenSteps) steps.push_back(&child);
			for (const auto& child : step->elseSteps) steps.push_back(&child);
		}
	}
	for (const auto& [name,action] : model.actions) {
		if (!ValidOperation(action)) { error = "Unsupported application operation or arguments: " + name; return false; }
	}
	std::vector<const Node*> pending{&model.root};
	while (!pending.empty()) {
		const auto* node = pending.back(); pending.pop_back();
		if (node->control && (node->control->event.empty() ? !model.actions.contains(node->control->action) :
			!model.events.contains(PresentationAliasKey(node->control->event)))) {
			error = "Control has no typed application action or event: " + node->id; return false;
		}
		for (const auto& child : node->children) pending.push_back(&child);
	}
	return true;
}

bool HasControls(const Node& node) {
	if (node.control) return true;
	for (const auto& child : node.children) if (HasControls(child)) return true;
	return false;
}

bool NonInteractive(const idDict& dictionary) {
	StateValue value;
	if (ConvertState(dictionary.GetString("noninteractive","0"),1,value)) return std::get<bool>(value);
	// Undeclared legacy caller flags retain their numeric dictionary semantics.
	return dictionary.GetBool("noninteractive");
}

// The existing game/SDL interface transports a 640x480 aspect-corrected
// cursor. Invert that transform once at the retained window-unit boundary.
struct CursorTransform {
	float sx = 1, sy = 1, ox = 0, oy = 0;
	explicit CursorTransform(const Viewport& viewport) {
		sx = viewport.width / 640.f; sy = viewport.height / 480.f;
		if (cvarSystem->GetCVarBool("ui_aspectCorrection")) {
			sx = sy = Min(sx,sy);
			ox = (viewport.width-640.f*sx)*.5f; oy = (viewport.height-480.f*sy)*.5f;
		}
	}
};

bool ReadString(idFile& file, std::string& value, int limit, int& budget) {
	int length = 0;
	if (file.ReadInt(length) != 4 || length < 0 || length > limit || length > budget ||
		length > file.Length()-file.Tell()) return false;
	std::string candidate(static_cast<size_t>(length),'\0');
	if (length && file.Read(candidate.data(),length) != length) return false;
	if (candidate.find('\0') != std::string::npos) return false;
	budget -= length; value = std::move(candidate); return true;
}
bool WriteString(idFile& file, const std::string& value) {
	return value.size() <= static_cast<size_t>((std::numeric_limits<int>::max)()) &&
		file.WriteInt(static_cast<int>(value.size())) == 4 &&
		(value.empty() || file.Write(value.data(),static_cast<int>(value.size())) == static_cast<int>(value.size()));
}
}

struct idUserInterfaceRetained::Impl {
	idDict state;
	idStr path;
	ID_TIME_T stamp = 0;
	Document document;
	retainedUIView_t* view = nullptr;
	Input input;
	std::set<int> held;
	struct PendingAction {
		ActionInvocation invocation;
		bool cancellable = true;
		std::string control;
		std::uint64_t proposalToken = 0;
	};
	std::vector<PendingAction> actions;
	bool interactive = true, interactiveSet = false, unique = false, active = false;
	bool suspended = false, pointerVisible = false, close = false, worldReported = false;
	bool unavailable = false;
	bool initialized = false;
	bool settingsFields = false, settingsClosePending = false;
	std::uint64_t settingsOwner = UI_SettingsCreateOwner();
	float cursorX = 320, cursorY = 240;
	std::string lastError;
	std::string checkpoint;

	~Impl() { UI_SettingsReleaseOwner(settingsOwner); RetainedUI_DestroyView(view); }
	Runtime* RuntimeView() const { return RetainedUI_ViewRuntime(view); }
	void Error(const std::string& message) {
		if (message != lastError) common->Warning("retained GUI %s: %s",path.c_str(),message.c_str());
		lastError = message;
	}
	void Quarantine(bool forget = false, bool cancelRuntime = true, bool discardPrograms = false) {
		input.Cancel(forget); input.Take(); held.clear(); close = false; pointerVisible = false;
		// Completed programs retain their immutable invocations through input
		// suspension. Save/resource/source replacement explicitly discards them.
		actions.erase(std::remove_if(actions.begin(),actions.end(),[&](const PendingAction& action) {
			const bool discard = discardPrograms || action.cancellable;
			if (discard && action.proposalToken) if (auto* runtime = RuntimeView())
				runtime->AcknowledgeControlProposal(action.control,action.proposalToken,false);
			return discard;
		}),actions.end());
		if (auto* runtime = RuntimeView()) {
			if (cancelRuntime) runtime->CancelInput(RetainedUI_PresentationTime());
			runtime->ReleaseInputSources(); runtime->TakeActions();
		}
	}
	static void ResourceEvent(void* owner, retainedUIViewEvent_t event) {
		auto& self = *static_cast<Impl*>(owner);
		if (event == retainedUIViewEvent_t::BeforeResourceReset) self.Quarantine(false,false,true);
		else if (event == retainedUIViewEvent_t::Failed) self.Quarantine(false,false,true);
		if (event != retainedUIViewEvent_t::BeforeResourceReset) common->Printf("RETAINED_GUI_RESOURCE path=%s event=%s\n",
			self.path.c_str(),event == retainedUIViewEvent_t::Restored ? "restored" : "failed");
	}
	bool Prepare() {
		const bool ready = view && RetainedUI_PrepareView(view);
		if (!ready && !unavailable) Quarantine();
		unavailable = !ready;
		return ready && SyncSettings();
	}
	bool SyncSettings() {
		if (!settingsFields) return true;
		StateValues current;
		if (!UI_SettingsRead(settingsOwner,current)) return false;
		StateValues updates, published; const auto live = RuntimeView()->GetState(false);
		for (const auto& [key,declaration] : document.Model().state) if (key.starts_with("settings.")) {
			const auto source = current.find(key);
			const auto& value = source == current.end() ? declaration.initial : source->second;
			published.emplace(key,value);
			const auto before = live.find(key);
			if (before == live.end() || before->second != value) updates.emplace(key,value);
		}
		std::string error;
		if (!updates.empty() && !RuntimeView()->SetState(updates,error,RetainedUI_PresentationTime())) { Error(error); return false; }
		for (const auto& [key,value] : published) {
			PresentationValue text;
			if (std::holds_alternative<std::string>(value)) { text.type = PresentationType::String; text.text = std::get<std::string>(value); }
			else if (std::holds_alternative<bool>(value)) { text.type = PresentationType::Boolean; text.data[0] = std::get<bool>(value) ? 1 : 0; }
			else text.data[0] = std::get<double>(value);
			state.Set(key.c_str(),FormatPresentationValue(text).c_str());
		}
		return true;
	}
	void ApplyInput() {
		for (const auto& event : input.Take()) {
			if (event.kind == RoutedInput::Kind::Cancel) RuntimeView()->CancelInput(RetainedUI_PresentationTime());
			else if (event.kind == RoutedInput::Kind::PointerButton) RuntimeView()->PointerButton(event.down,RetainedUI_PresentationTime());
			else RuntimeView()->MenuAction(event.menu,event.down,RetainedUI_PresentationTime());
		}
	}
	bool RunEvent(const std::string& name) {
		StateValues application; std::string error;
		if (!ApplicationState(document.Model(),state,application,error)) { Error(error); return false; }
		Runtime::EventEffects effects;
		if (!RuntimeView()->RunEvent(name,RetainedUI_PresentationTime(),effects,error,application,ValidInvocation,256-actions.size())) {
			Error(error); return false;
		}
		// Preserve undeclared and unrelated pending dictionary keys. Publish only
		// the program's explicit application writes, using round-trip numbers.
		for (const auto& [id,value] : effects.stateChanges) {
			PresentationValue text;
			if (std::holds_alternative<std::string>(value)) { text.type = PresentationType::String; text.text = std::get<std::string>(value); }
			else if (std::holds_alternative<bool>(value)) { text.type = PresentationType::Boolean; text.data[0] = std::get<bool>(value) ? 1 : 0; }
			else text.data[0] = std::get<double>(value);
			state.Set(id.c_str(),FormatPresentationValue(text).c_str());
		}
		const auto count = effects.actions.size();
		for (auto& action : effects.actions) actions.push_back({std::move(action),false});
		if (!interactiveSet) {
			interactive = HasControls(document.Model().root) && !NonInteractive(state);
			if (!interactive) Quarantine();
		}
		lastError.clear();
		if (cvarSystem->GetCVarBool("ui_retainedTrace")) common->Printf("RETAINED_GUI_EVENT name=%s actions=%u writes=%u\n",name.c_str(),
			static_cast<unsigned>(count),static_cast<unsigned>(effects.stateChanges.size()));
		return true;
	}
	void CollectActions(bool semantic = false) {
		for (const auto& event : RuntimeView()->TakeActions()) {
			const auto reject = [&] { if (event.proposalToken)
				RuntimeView()->AcknowledgeControlProposal(event.node,event.proposalToken,false); };
			if (!interactive || event.document != document.Model().id) { reject(); continue; }
			if (event.kind == ControlAction::Kind::Back) {
				if (!RuntimeView()->PopModal(RetainedUI_PresentationTime())) {
					if (RuntimeView()->HasEvent("onBack")) RunEvent("onBack");
					else if (semantic && actions.size() < 256) actions.push_back({{"","ui.dismiss",{}},false});
					else if (!semantic) close = true;
				}
				continue;
			}
			if (!RuntimeView()->CanActivateControl(event.node,RetainedUI_PresentationTime())) { reject(); continue; }
			if (!event.event.empty()) { RunEvent(event.event); continue; }
			ActionInvocation invocation; std::string error;
			if (!RuntimeView()->ResolveAction(event.action,invocation,error,event.proposal ? &*event.proposal : nullptr) ||
				!ValidInvocation(invocation,error)) { reject(); Error(error); continue; }
			if (actions.size() >= 256) { reject(); Quarantine(); Error("Application action queue exceeded 256 requests"); return; }
			actions.push_back({std::move(invocation),!semantic,event.node,event.proposalToken});
		}
	}
	bool AcceptInput() {
		bool focus = true;
#if defined(USE_SDL3)
		focus = Sys_SDL_IsGameWindowFocused();
#endif
		const bool pause = !active || !interactive || !focus || console->Active();
		if (pause && !suspended) Quarantine(!focus);
		suspended = pause;
		return !pause;
	}
	void Pointer() {
		Viewport viewport;
		if (!RetainedUI_DefaultViewport(viewport)) return;
		const CursorTransform transform(viewport);
		RuntimeView()->PointerMove((cursorX*transform.sx+transform.ox+viewport.originX)/viewport.pixelDensityX,
			(cursorY*transform.sy+transform.oy+viewport.originY)/viewport.pixelDensityY,RetainedUI_PresentationTime());
	}
};

idUserInterfaceRetained::idUserInterfaceRetained(bool managed) : idUserInterfaceManaged(managed), impl(std::make_unique<Impl>()) { diagnosticViews.push_back(this); }
idUserInterfaceRetained::~idUserInterfaceRetained() { diagnosticViews.erase(std::remove(diagnosticViews.begin(),diagnosticViews.end(),this),diagnosticViews.end()); }
const char* idUserInterfaceRetained::Name() const { return impl->path.c_str(); }
const char* idUserInterfaceRetained::Comment() const { return "Canonical retained UI"; }
const char* idUserInterfaceRetained::GetSourceFile() const { return Name(); }
ID_TIME_T idUserInterfaceRetained::GetTimeStamp() const { return impl->stamp; }
bool idUserInterfaceRetained::Active() const { return impl->active; }
bool idUserInterfaceRetained::HasInteractiveOverride() const { return impl->interactiveSet; }
bool idUserInterfaceRetained::IsMenuGui() const { return true; }
bool idUserInterfaceRetained::AlwaysThink() const { return false; }
bool idUserInterfaceRetained::IsInteractive() const { return impl->interactive; }
void idUserInterfaceRetained::SetInteractive(bool value) { impl->interactiveSet = true; impl->interactive = value; if (!value) impl->Quarantine(); }
bool idUserInterfaceRetained::IsUniqued() const { return impl->unique; }
void idUserInterfaceRetained::SetUniqued(bool value) { impl->unique = value; }
size_t idUserInterfaceRetained::Size() { return sizeof(*this)+sizeof(Impl)+impl->state.Allocated()+impl->document.Source().size(); }
int idUserInterfaceRetained::NumTransitions() { return static_cast<int>(impl->document.Model().timelines.size()); }

bool idUserInterfaceRetained::InitFromFile(const char* qpath, bool rebuild, bool cache) {
	if (!UI_IsRetainedPath(qpath)) return false;
	const std::string path(qpath);
	if (path.find("..") != std::string::npos || path.find(':') != std::string::npos || path[0] == '/' || path[0] == '\\') return false;
	void* data = nullptr; ID_TIME_T stamp = 0;
	const int expectedSize = fileSystem->ReadFile(path.c_str(),nullptr);
	if (expectedSize <= 0 || expectedSize > 16*1024*1024) return false;
	const int size = fileSystem->ReadFile(path.c_str(),&data,&stamp);
	if (size < 0 || !data) return false;
	if (size > 16*1024*1024) { fileSystem->FreeFile(data); return false; }
	const std::string source(static_cast<const char*>(data),static_cast<size_t>(size));
	fileSystem->FreeFile(data);
	Document candidate; std::vector<Diagnostic> diagnostics; std::string error;
	if (!candidate.Load(source,diagnostics)) {
		for (const auto& diagnostic : diagnostics) common->Warning("retained GUI %s:%u:%u %s: %s",path.c_str(),
			static_cast<unsigned>(diagnostic.line),static_cast<unsigned>(diagnostic.column),diagnostic.pointer.c_str(),diagnostic.message.c_str());
		return false;
	}
	if (!ValidateApplication(candidate.Model(),error)) { impl->Error(error); return false; }
	StateValues application;
	std::string snapshot;
	const bool same = impl->Prepare() && path == impl->path.c_str() && source == impl->document.Source();
	if (!same && !ApplicationState(candidate.Model(),impl->state,application,error)) { impl->Error(error); return false; }
	if (same && !impl->RuntimeView()->SaveSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	auto* view = RetainedUI_CreateView(Impl::ResourceEvent,impl.get());
	if (!view) return false;
	bool valid = RetainedUI_LoadView(view,source,path,diagnostics);
	if (valid) valid = snapshot.empty() ? RetainedUI_ViewRuntime(view)->SetState(application,error,RetainedUI_PresentationTime()) :
		RetainedUI_ViewRuntime(view)->RestoreSnapshot(snapshot,error,RetainedUI_PresentationTime());
	if (!valid) {
		RetainedUI_DestroyView(view);
		if (!error.empty()) impl->Error(error);
		for (const auto& diagnostic : diagnostics) impl->Error(diagnostic.message);
		return false;
	}
	impl->Quarantine(false,false,true);
	if (same && impl->settingsClosePending) UI_SettingsCloseOwner(impl->settingsOwner);
	impl->settingsClosePending = false;
	RetainedUI_DestroyView(impl->view); impl->view = view;
	impl->RuntimeView()->ReleaseInputSources();
	impl->document = std::move(candidate); impl->path = path.c_str(); impl->stamp = stamp;
	if (!same) {
		impl->initialized = false;
		UI_SettingsReleaseOwner(impl->settingsOwner); impl->settingsOwner = UI_SettingsCreateOwner();
	}
	impl->settingsFields = std::any_of(impl->document.Model().state.begin(),impl->document.Model().state.end(),
		[](const auto& field) { return field.first.starts_with("settings."); });
	UI_SettingsConfirmationDocument(impl->settingsOwner,impl->document.Model());
	impl->state.Set("name",path.c_str()); impl->lastError.clear();
	if (!impl->interactiveSet) impl->interactive = HasControls(impl->document.Model().root) && !NonInteractive(impl->state);
	RegisterLoaded(); RefreshThinking();
	common->Printf("RETAINED_GUI_LOADED %s\n",path.c_str());
	return true;
}

const idDict& idUserInterfaceRetained::State() const { return impl->state; }
void idUserInterfaceRetained::DeleteStateVar(const char* name) { impl->state.Delete(name); }
void idUserInterfaceRetained::SetStateString(const char* name, const char* value) { impl->state.Set(name,value); }
void idUserInterfaceRetained::SetStateBool(const char* name, bool value) { impl->state.SetBool(name,value); }
void idUserInterfaceRetained::SetStateInt(const char* name, int value) { impl->state.SetInt(name,value); }
void idUserInterfaceRetained::SetStateFloat(const char* name, float value) { impl->state.SetFloat(name,value); }
void idUserInterfaceRetained::SetStateVec4(const char* name, const idVec4& value) { impl->state.SetVec4(name,value); }
const char* idUserInterfaceRetained::GetStateString(const char* name, const char* fallback) const { return impl->state.GetString(name,fallback); }
bool idUserInterfaceRetained::GetStateBool(const char* name, const char* fallback) const { return impl->state.GetBool(name,fallback); }
int idUserInterfaceRetained::GetStateInt(const char* name, const char* fallback) const { return impl->state.GetInt(name,fallback); }
float idUserInterfaceRetained::GetStateFloat(const char* name, const char* fallback) const { return impl->state.GetFloat(name,fallback); }
void idUserInterfaceRetained::StateChanged(int time, bool redraw) {
	if (!impl->Prepare()) return;
	StateValues values; std::string error;
	if (!ApplicationState(impl->document.Model(),impl->state,values,error) ||
		!impl->RuntimeView()->SetState(values,error,RetainedUI_PresentationTime())) impl->Error(error);
	else {
		impl->lastError.clear();
		if (!impl->interactiveSet) {
			impl->interactive = HasControls(impl->document.Model().root) && !NonInteractive(impl->state);
			if (!impl->interactive) impl->Quarantine();
		}
	}
	if (redraw) Redraw(time);
}

bool idUserInterfaceRetained::GetPresentationValue(const char* name, idStr& value) const {
	if (!name || !impl->Prepare()) return false;
	std::string exported;
	if (impl->RuntimeView()->GetPresentationAlias(name,exported)) { value = exported.c_str(); return true; }
	const std::string alias(name); const auto separator = alias.find("::");
	if (separator == std::string::npos) return false;
	if (idStr::Icmp(alias.substr(0,separator).c_str(),"gui") == 0) return false;
	const auto property = impl->RuntimeView()->PresentedValue(alias.substr(0,separator),alias.substr(separator+2));
	if (!property) return false;
	value = (property->type == ValueType::Text ? property->text : property->Css()).c_str(); return true;
}
bool idUserInterfaceRetained::SetPresentationValue(const char* name, const char* value, bool overrideExpression) {
	if (!name || !value || !impl->Prepare()) return false;
	std::string error;
	if (!impl->RuntimeView()->SetPresentationAlias(name,value,overrideExpression,error)) { impl->Error(error); return false; }
	impl->lastError.clear(); return true;
}
bool idUserInterfaceRetained::GetTextInputState(idRectangle&, float&) const { return false; }
bool idUserInterfaceRetained::GetMaxTextIndex(const char*, const char*, wrapInfo_t&) const { return false; }
void idUserInterfaceRetained::SetKeyBindingNames() {}

void idUserInterfaceRetained::SetCursor(float x, float y) {
	Viewport viewport; RetainedUI_DefaultViewport(viewport);
	const CursorTransform transform(viewport);
	if (transform.sx <= 0 || transform.sy <= 0) return;
	impl->cursorX = std::isfinite(x) ? idMath::ClampFloat(-transform.ox/transform.sx,(viewport.width-transform.ox)/transform.sx,x) : 0;
	impl->cursorY = std::isfinite(y) ? idMath::ClampFloat(-transform.oy/transform.sy,(viewport.height-transform.oy)/transform.sy,y) : 0;
}
float idUserInterfaceRetained::CursorX() { return impl->cursorX; }
float idUserInterfaceRetained::CursorY() { return impl->cursorY; }

const char* idUserInterfaceRetained::HandleEvent(const sysEvent_t* event, int time, bool* updateVisuals) {
	if (updateVisuals) *updateVisuals = false;
	if (!event) return "";
	if (!impl->Prepare()) {
		if (event->evType == SE_KEY && !event->evValue2) impl->input.ReleaseQuarantined(event->evValue);
		// Failure must never strand the session behind an unavailable menu.
		if (impl->active && event->evType == SE_KEY && event->evValue2 &&
			(event->evValue == K_ESCAPE || event->evValue == K_JOY4 || event->evValue == K_JOY7 || event->evValue == K_JOY8)) impl->close = true;
		return impl->close ? ActionMarker : "";
	}
	if (!impl->AcceptInput()) {
		if (event->evType == SE_KEY && !event->evValue2) impl->input.ReleaseQuarantined(event->evValue);
		return "";
	}
	if (event->evType == SE_MOUSE) {
		SetCursor(impl->cursorX+event->evValue,impl->cursorY+event->evValue2);
		impl->pointerVisible = true; impl->Pointer();
	} else if (event->evType == SE_KEY && event->evValue > 0 && event->evValue < K_LAST_KEY) {
		const int key = event->evValue; const bool down = event->evValue2 != 0;
		const bool repeated = down && impl->held.contains(key);
		if (down) impl->held.insert(key); else impl->held.erase(key);
		if (key == K_MOUSE1) {
			impl->Pointer(); impl->input.Pointer(key,down,RetainedUI_PresentationTime());
		} else if (key == K_MWHEELUP || key == K_MWHEELDOWN) {
			if (down && !repeated) {
				impl->pointerVisible = true; impl->Pointer();
				impl->RuntimeView()->PointerWheel(key == K_MWHEELUP ? -1 : 1,RetainedUI_PresentationTime());
			}
		} else {
			MenuInput action; bool mapped = true;
			switch (key) {
				case K_TAB: action = (impl->held.contains(K_SHIFT) || idKeyInput::IsDown(K_SHIFT)) ? MenuInput::Previous : MenuInput::Next; break;
				case K_UPARROW: case K_JOY9: action = MenuInput::Up; break;
				case K_DOWNARROW: case K_JOY10: action = MenuInput::Down; break;
				case K_LEFTARROW: case K_JOY12: action = MenuInput::Left; break;
				case K_RIGHTARROW: case K_JOY11: action = MenuInput::Right; break;
				case K_HOME: action = MenuInput::Home; break;
				case K_END: action = MenuInput::End; break;
				case K_PGUP: action = MenuInput::PageUp; break;
				case K_PGDN: action = MenuInput::PageDown; break;
				case K_ENTER: case K_KP_ENTER: case K_SPACE: case K_JOY3: action = MenuInput::Accept; break;
				case K_ESCAPE: case K_JOY4: case K_JOY7: case K_JOY8: action = MenuInput::Back; break;
				default: mapped = false; break;
			}
			if (mapped) { impl->pointerVisible = false; impl->input.Menu(key,action,down,repeated,RetainedUI_PresentationTime()); }
		}
	}
	impl->input.Advance(RetainedUI_PresentationTime()); impl->ApplyInput(); impl->CollectActions();
	if (updateVisuals) *updateVisuals = true;
	return impl->close || !impl->actions.empty() ? ActionMarker : "";
}

void idUserInterfaceRetained::HandleNamedEvent(const char* name) {
	if (!name || !impl->Prepare()) return;
	if (impl->RuntimeView()->HasEvent(name)) impl->RunEvent(name);
	else impl->RuntimeView()->PlayTimeline(name,RetainedUI_PresentationTime());
}
const char* idUserInterfaceRetained::Activate(bool value, int time) {
	if (impl->active != value) impl->Quarantine();
	impl->active = value;
	impl->settingsClosePending = !value;
	HandleNamedEvent(value ? "onActivate" : "onDeactivate");
	if (!value && impl->actions.empty()) {
		UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false;
		if (impl->view) impl->Prepare();
	}
	return PendingApplicationCommand();
}
void idUserInterfaceRetained::Trigger(int time) { HandleNamedEvent("onTrigger"); }
void idUserInterfaceRetained::RunTimeEvents(int time) { /* Retained motion advances on the presentation clock in Frame. */ }
void idUserInterfaceRetained::Redraw(int time, bool useAspectCorrection) {
	if (!useAspectCorrection) {
		if (!impl->worldReported) { impl->Error("World-surface output is not implemented for retained menu documents"); impl->worldReported = true; }
		return;
	}
	Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport) || !impl->Prepare()) return;
	if (!impl->initialized) {
		if (!impl->RuntimeView()->HasEvent("onInit") || impl->RunEvent("onInit")) impl->initialized = true;
	}
	impl->AcceptInput();
	if (RetainedUI_DrawViewRoot(impl->view,viewport) && impl->active && impl->interactive) {
		// A restored world frame alone cannot arm Keep. Require this owner and
		// its actually activatable Revert control to have reached the draw path.
		if (impl->settingsFields && impl->RuntimeView()->CanActivateControl("settings_revert",RetainedUI_PresentationTime()))
			UI_SettingsOwnerDrawn(impl->settingsOwner,impl->state.GetString("settings.request"));
		DrawCursor();
	}
}
void idUserInterfaceRetained::DrawCursor() {
	if (!impl->pointerVisible || impl->suspended) return;
	Viewport viewport;
	if (!RetainedUI_DefaultViewport(viewport)) return;
	const CursorTransform transform(viewport);
	const float px = impl->cursorX*transform.sx+transform.ox, py = impl->cursorY*transform.sy+transform.oy;
	const float size = 20.f*viewport.DpRatio();
	const float x = px*640.f/viewport.width, y = py*480.f/viewport.height;
	const float dx = size*640.f/viewport.width, dy = size*480.f/viewport.height;
	const bool oldViewport = renderSystem->GetUseUIViewportFor2D();
	renderSystem->SetUseUIViewportFor2D(true); renderSystem->SetColor4(.8f,.95f,1.f,1.f);
	const auto* material = declManager->FindMaterial("_white");
	renderSystem->DrawStretchTri(idVec2(x,y),idVec2(x+dx,y+dy),idVec2(x,y+dy*.75f),vec2_origin,vec2_origin,vec2_origin,material);
	renderSystem->FlushGui(); renderSystem->SetUseUIViewportFor2D(oldViewport); renderSystem->SetColor4(1,1,1,1);
}

const char* idUserInterfaceRetained::PendingApplicationCommand() const {
	return impl->close || impl->settingsClosePending || !impl->actions.empty() ? ActionMarker : "";
}
bool idUserInterfaceRetained::DispatchApplicationActions(const char* command, bool& closeRequested) {
	closeRequested = false;
	// The marker carries no untrusted parameters. Only this live instance's
	// typed queue can request host operations; all other commands are consumed.
	if (!command || idStr::Cmp(command,ActionMarker)) return true;
	if (!impl->Prepare()) {
		closeRequested = impl->close; impl->close = false; impl->actions.clear();
		if (impl->settingsClosePending) { UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false; }
		return true;
	}
	// The session pump precedes ordinary input delivery. Observe focus/console
	// suspension now so it cannot dispatch a stale physical activation first.
	// Completed programs and explicit semantic diagnostics remain committed.
	impl->AcceptInput();
	auto actions = std::move(impl->actions); impl->actions.clear();
	closeRequested = impl->close; impl->close = false;
	for (const auto& pending : actions) {
		const auto& invocation = pending.invocation;
		if (invocation.operation == "ui.dismiss") {
			closeRequested = true;
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,true);
			TraceInvocation(Name(),invocation,true); continue;
		}
		if (invocation.operation.starts_with("settings.system.")) {
			std::string error;
			const bool accepted = UI_SettingsDispatch(impl->settingsOwner,invocation,error);
			if (!accepted) impl->Error(error);
			else impl->lastError.clear();
			impl->SyncSettings();
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
			continue;
		}
		const auto value = invocation.arguments.find("value");
		bool accepted = false;
		if (invocation.arguments.size() != 1 || value == invocation.arguments.end()) {
			if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,false);
			continue;
		}
		if (invocation.operation == "settings.brightness.set" && std::holds_alternative<double>(value->second)) {
			const double number = std::get<double>(value->second);
			if (!std::isfinite(number) || number < .5 || number > 2) impl->Error("Brightness request must be between 0.5 and 2.0");
			else {
				cvarSystem->SetCVarFloat("r_brightness",static_cast<float>(number));
				accepted = cvarSystem->GetCVarFloat("r_brightness") == static_cast<float>(number);
				if (!accepted) impl->Error("Brightness request was not applied");
			}
		} else if (invocation.operation == "settings.shadows.set" && std::holds_alternative<bool>(value->second)) {
			cvarSystem->SetCVarBool("r_shadows",std::get<bool>(value->second));
			accepted = cvarSystem->GetCVarBool("r_shadows") == std::get<bool>(value->second);
			if (!accepted) impl->Error("Shadows request was not applied");
		}
		if (pending.proposalToken) impl->RuntimeView()->AcknowledgeControlProposal(pending.control,pending.proposalToken,accepted);
		TraceInvocation(Name(),invocation,closeRequested);
	}
	if (impl->settingsClosePending) {
		UI_SettingsCloseOwner(impl->settingsOwner); impl->settingsClosePending = false;
		impl->SyncSettings();
	}
	return true;
}

bool idUserInterfaceRetained::WriteToSaveGame(idFile* file) const {
	if (!file || !impl->Prepare() || impl->state.GetNumKeyVals() > MaxStateEntries) return false;
	std::string snapshot, error;
	if (!impl->RuntimeView()->SaveSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	idFile_Memory payload; int budget = MaxStateBytes;
	if (payload.WriteInt(impl->state.GetNumKeyVals()) != 4) return false;
	for (int i = 0; i < impl->state.GetNumKeyVals(); ++i) {
		const auto* entry = impl->state.GetKeyVal(i);
		if (entry->GetKey().IsEmpty()) return false;
		for (const auto* string : {&entry->GetKey(),&entry->GetValue()}) {
			if (string->Length() > MaxStringBytes || string->Length() > budget) return false;
			budget -= string->Length();
			if (!WriteString(payload,string->c_str())) return false;
		}
	}
	if (payload.WriteInt((impl->active ? 1 : 0) | (impl->interactive ? 2 : 0) | (impl->unique ? 4 : 0) | (impl->interactiveSet ? 8 : 0)) != 4 ||
		payload.WriteFloat(impl->cursorX) != 4 || payload.WriteFloat(impl->cursorY) != 4 || !WriteString(payload,snapshot)) return false;
	if (payload.Length() > MaxFrameBytes) return false;
	return file->WriteUnsignedInt(SaveTag) == 4 && file->WriteInt(1) == 4 && file->WriteInt(payload.Length()) == 4 &&
		file->Write(payload.GetDataPtr(),payload.Length()) == payload.Length();
}

bool idUserInterfaceRetained::ReadFromSaveGame(idFile* file) {
	if (!file || !impl->Prepare()) return false;
	unsigned tag = 0; int version = 0, length = 0;
	if (file->ReadUnsignedInt(tag) != 4 || file->ReadInt(version) != 4 || file->ReadInt(length) != 4 ||
		tag != SaveTag || version != 1 || length < 0 || length > MaxFrameBytes || length > file->Length()-file->Tell()) return false;
	std::string bytes(static_cast<size_t>(length),'\0');
	if (length && file->Read(bytes.data(),length) != length) return false;
	idFile_Memory payload("retained-gui-save",static_cast<const char*>(bytes.data()),length);
	idDict state; int count = 0, budget = MaxStateBytes;
	if (payload.ReadInt(count) != 4 || count < 0 || count > MaxStateEntries) return false;
	for (int i = 0; i < count; ++i) {
		std::string key,value;
		if (!ReadString(payload,key,MaxStringBytes,budget) || key.empty() || state.FindKey(key.c_str()) ||
			!ReadString(payload,value,MaxStringBytes,budget)) return false;
		state.Set(key.c_str(),value.c_str());
	}
	int flags = 0; float x = 0, y = 0; std::string snapshot,error;
	budget = static_cast<int>(Runtime::MaxSnapshotBytes);
	if (payload.ReadInt(flags) != 4 || flags < 0 || flags > 15 || payload.ReadFloat(x) != 4 || payload.ReadFloat(y) != 4 ||
		!std::isfinite(x) || !std::isfinite(y) || !ReadString(payload,snapshot,budget,budget) || payload.Tell() != payload.Length()) return false;
	// The public dictionary may contain a pending SetState* batch that the
	// caller has not committed with StateChanged yet. Preserve that distinction:
	// the snapshot is committed presentation state; the dictionary is pending
	// caller input. Restoring must not implicitly commit it or replay actions.
	// Snapshot validation is atomic; outer dictionary/flags follow on success.
	if (!impl->RuntimeView()->RestoreSnapshot(snapshot,error,RetainedUI_PresentationTime())) { impl->Error(error); return false; }
	impl->Quarantine(false,false,true); impl->state = state; impl->state.Set("name",Name());
	impl->settingsClosePending = false;
	// Save restoration suppresses automatic initialization just as legacy load
	// does. Lifecycle/program side effects never replay while restoring a GUI.
	impl->initialized = true;
	impl->active = (flags & 1) != 0; impl->interactive = (flags & 2) != 0; impl->unique = (flags & 4) != 0;
	impl->interactiveSet = (flags & 8) != 0;
	// A snapshot cannot rewind an engine-owned settings draft. An inactive
	// restore does close the existing session, even when this cached GUI never
	// receives another Activate(false); failed restores leave it untouched.
	if (!impl->active) UI_SettingsCloseOwner(impl->settingsOwner);
	SetCursor(x,y); return true;
}

bool UI_RetainedSettingsDocument(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return false;
	auto& impl = *(*found)->impl;
	if (!impl.Prepare()) return false;
	const auto& model = impl.document.Model();
	if (model.id != "openq4.system" || !model.events.contains("onactivate") || !model.events.contains("onback")) return false;
	for (const auto& [key,type] : UI_SettingsStateSchema()) {
		const auto declaration = model.state.find(key);
		if (declaration == model.state.end() || declaration->second.initial.index() != type || !declaration->second.cvar.empty()) return false;
	}
	for (const auto* operation : {"settings.system.begin","settings.system.edit","settings.system.apply","settings.system.cancel"}) {
		if (std::none_of(model.actions.begin(),model.actions.end(),[&](const auto& entry) { return entry.second.operation == operation; })) return false;
	}
	return true;
}

bool UI_RetainedSettingsCanReturn(idUserInterface* gui) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end()) return false;
	StateValues live;
	if (!UI_SettingsRead((*found)->impl->settingsOwner,live)) return false;
	// Read the actual service owner, never pending GUI dictionary values.
	if (!std::get<bool>(live.at("settings.open"))) return true;
	return !std::get<bool>(live.at("settings.dirty")) && !std::get<bool>(live.at("settings.busy")) &&
		std::get<double>(live.at("settings.phase")) == static_cast<double>(openq4::ui::SettingsPhase::Editing);
}

bool UI_RetainedDiagnostic(idUserInterface* gui, const idCmdArgs& args) {
	const auto found = std::find(diagnosticViews.begin(),diagnosticViews.end(),gui);
	if (found == diagnosticViews.end() || args.Argc() < 2) return false;
	auto& owner = **found; auto& impl = *owner.impl;
	if (!impl.Prepare()) return false;
	const std::string verb(args.Argv(1)); bool okay = false;
	if (verb == "report" && args.Argc() == 2) {
		common->Printf("RETAINED_GUI path=%s focus=%s revision=%llu active=%d brightness=%.6f shadows=%d contexts=%llu\n",
			owner.Name(),impl.RuntimeView()->FocusedControl().c_str(),static_cast<unsigned long long>(impl.RuntimeView()->StateRevision()),
			impl.active ? 1 : 0,cvarSystem->GetCVarFloat("r_brightness"),cvarSystem->GetCVarBool("r_shadows") ? 1 : 0,
			static_cast<unsigned long long>(impl.RuntimeView()->Statistics().activeContexts));
		return true;
	} else if (verb == "inspect" && args.Argc() == 3) {
		const std::string id(args.Argv(2));
		if (id.empty() || id.size() > 128 || std::any_of(id.begin(),id.end(),[](unsigned char c) {
			return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-');
		})) return false;
		const auto* node = impl.document.Model().FindNode(id);
		Bounds bounds;
		if (!node || !impl.RuntimeView()->GetBounds(id,bounds)) return false;
		const auto opacity = impl.RuntimeView()->PresentedValue(id,"opacity");
		const auto display = impl.RuntimeView()->PresentedValue(id,"display");
		const double alpha = opacity && opacity->type == ValueType::Number ? opacity->data[0] : 1;
		if (!std::isfinite(alpha) || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
			!std::isfinite(bounds.width) || !std::isfinite(bounds.height)) return false;
		const std::set<std::string> displays{"block","none","flex","inline","inline-block"};
		const char* shown = !display ? "default" : display->type == ValueType::Keyword && displays.contains(display->text) ? display->text.c_str() : "invalid";
		const auto stats = impl.RuntimeView()->Statistics();
		// Bounds/properties are this node's current layout and canonical state;
		// counters describe the last complete view frame, not this node's ink.
		common->Printf("RETAINED_GUI_NODE id=%s bounds=%.9g,%.9g,%.9g,%.9g opacity=%.9g display=%s authoredPaths=%llu statistics=view vectorElements=%llu pathsCompiled=%llu uploads=%llu cacheHits=%llu vertices=%llu triangles=%llu\n",
			id.c_str(),bounds.x,bounds.y,bounds.width,bounds.height,alpha,shown,static_cast<unsigned long long>(node->paths.size()),
			static_cast<unsigned long long>(stats.vectorElements),static_cast<unsigned long long>(stats.vectorPathsCompiled),
			static_cast<unsigned long long>(stats.vectorUploads),static_cast<unsigned long long>(stats.vectorCacheHits),
			static_cast<unsigned long long>(stats.submittedVertices),static_cast<unsigned long long>(stats.submittedIndices/3));
		return true;
	} else if (verb == "widget" && args.Argc() == 3) {
		const std::string id(args.Argv(2));
		const auto widget = impl.RuntimeView()->GetWidgetState(id);
		if (!widget) return false;
		const auto number = [](const StateValue& value) {
			if (const auto* numeric = std::get_if<double>(&value)) return *numeric;
			if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? 1.0 : 0.0;
			return 0.0;
		};
		// String content stays out of the record; type identifies numeric/bool
		// readbacks, and authored strings remain available through aliases.
		common->Printf("RETAINED_GUI_WIDGET id=%s role=%u type=%u accepted=%.17g pending=%d proposed=%.17g rejected=%d token=%llu popup=%d firstVisible=%u\n",
			id.c_str(),static_cast<unsigned>(widget->role),static_cast<unsigned>(widget->accepted.index()),number(widget->accepted),
			widget->pending ? 1 : 0,widget->pending ? number(*widget->pending) : 0,widget->rejected ? 1 : 0,
			static_cast<unsigned long long>(widget->proposalToken),widget->popupOpen ? 1 : 0,widget->firstVisible);
		return true;
	} else if (verb == "focus" && args.Argc() == 3) {
		okay = impl.RuntimeView()->FocusControl(args.Argv(2),RetainedUI_PresentationTime());
	} else if (verb == "menu" && args.Argc() == 4 && (!idStr::Cmp(args.Argv(3),"0") || !idStr::Cmp(args.Argv(3),"1"))) {
		const std::map<std::string,MenuInput> inputs = {{"next",MenuInput::Next},{"previous",MenuInput::Previous},
			{"up",MenuInput::Up},{"down",MenuInput::Down},{"left",MenuInput::Left},{"right",MenuInput::Right},{"accept",MenuInput::Accept},{"back",MenuInput::Back},
			{"home",MenuInput::Home},{"end",MenuInput::End},{"pageUp",MenuInput::PageUp},{"pageDown",MenuInput::PageDown}};
		const auto input = inputs.find(args.Argv(2));
		if (input != inputs.end()) {
			impl.RuntimeView()->MenuAction(input->second,args.Argv(3)[0] == '1',RetainedUI_PresentationTime());
			impl.CollectActions(true); okay = true;
		}
	} else if (verb == "state" && args.Argc() == 4) {
		owner.SetStateString(args.Argv(2),args.Argv(3)); owner.StateChanged(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "pending" && args.Argc() == 4) {
		owner.SetStateString(args.Argv(2),args.Argv(3)); okay = true;
	} else if (verb == "event" && args.Argc() == 3) {
		owner.HandleNamedEvent(args.Argv(2)); okay = impl.lastError.empty();
	} else if (verb == "trigger" && args.Argc() == 2) {
		owner.Trigger(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "presentation" && args.Argc() == 5 && (!idStr::Cmp(args.Argv(4),"0") || !idStr::Cmp(args.Argv(4),"1"))) {
		okay = owner.SetPresentationValue(args.Argv(2),args.Argv(3),args.Argv(4)[0] == '1');
	} else if (verb == "update" && args.Argc() == 2) {
		owner.StateChanged(common->GetPresentationTime()); okay = impl.lastError.empty();
	} else if (verb == "save" && args.Argc() == 2) {
		idFile_Memory save;
		if (owner.WriteToSaveGame(&save) && save.WriteUnsignedInt(0x53454e54) == 4) {
			impl.checkpoint.assign(save.GetDataPtr(),save.Length()); okay = true;
		}
	} else if (verb == "restore" && args.Argc() == 2 && !impl.checkpoint.empty()) {
		idFile_Memory save("retained-gui-checkpoint",static_cast<const char*>(impl.checkpoint.data()),static_cast<int>(impl.checkpoint.size()));
		unsigned sentinel = 0;
		okay = owner.ReadFromSaveGame(&save) && save.ReadUnsignedInt(sentinel) == 4 && sentinel == 0x53454e54 && save.Tell() == save.Length();
	}
	common->Printf("RETAINED_GUI_OPERATION %s %s\n",verb.c_str(),okay ? "passed" : "failed");
	return okay;
}
#endif
