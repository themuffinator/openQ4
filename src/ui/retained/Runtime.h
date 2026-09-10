// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Document.h"
#include "Motion.h"
#include "Behavior.h"
#include "Interaction.h"

namespace openq4::ui {

// The retained library owns layout in physical pixels. Engine conversion to
// its 640x480 submission space happens only at the final drawing boundary.
struct Viewport {
	int width = 1280, height = 720;
	float displayScale = 1, userScale = 1;
	float pixelDensityX = 1, pixelDensityY = 1;
	float originX = 0, originY = 0;
	float DpRatio() const;
	void WindowToDocument(float x, float y, float& outX, float& outY) const;
};

struct Vertex {
	float x = 0, y = 0, u = 0, v = 0;
	// Premultiplied sRGB, kept floating point while clipping.
	float r = 1, g = 1, b = 1, a = 1;
};

struct Bounds { float x = 0, y = 0, width = 0, height = 0; };
struct NumberTextGeometry {
	std::string control;
	NumberEditIdentity identity;
	Bounds caret, viewport; // Projected document pixels, before engine conversion.
	float scroll = 0;
};
struct NumberEditorContext {
	std::string control;
	NumberEditView editor;
	std::uint64_t modalToken = 0;
};
struct FontMetrics { float ascent = 0, descent = 0, lineSpacing = 0, xHeight = 0; };
struct Glyph {
	float advance = 0, left = 0, top = 0, width = 0, height = 0;
	float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	std::string material;
};
// CPU submission measurements, not GPU timings. Counts reset for each Frame;
// resident geometry counts track resource lifetime, including hidden elements.
struct RuntimeStatistics {
	double frameMilliseconds = 0, updateMilliseconds = 0, renderMilliseconds = 0;
	double vectorCompileMilliseconds = 0, vectorUploadMilliseconds = 0;
	std::uint64_t vectorElements = 0, vectorPathsCompiled = 0, vectorCacheHits = 0, vectorUploads = 0;
	std::uint64_t geometryCompiles = 0, drawCalls = 0, submittedVertices = 0, submittedIndices = 0;
	std::uint64_t residentGeometryCount = 0, residentGeometryBytes = 0;
	std::uint64_t visibleVectorCacheBytes = 0;
	std::uint64_t layerPushes = 0, layerComposites = 0, peakLayerDepth = 0;
	std::uint64_t maskSnapshots = 0, maskApplications = 0, peakLayerTargets = 0;
	// Shared service residency, sampled by Statistics(); idle backends are reused.
	std::uint64_t activeContexts = 0, residentBackends = 0;
};

class Host {
public:
	Host();
	virtual ~Host();
	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;
	virtual bool ReadFile(const std::string& path, std::string& contents) = 0;
	virtual std::string Translate(const std::string& text) = 0;
	// Read-only external state. Return false for unavailable/invalid sources;
	// the runtime retains the last valid snapshot and reports the source.
	virtual bool ReadCVar(const std::string& name, size_t type, StateValue& value) = 0;
	virtual void Log(bool error, const std::string& message) = 0;
	virtual std::uintptr_t LoadMaterial(const std::string& name, int& width, int& height) = 0;
	virtual void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t material) = 0;
	// Identifies the host's current submission frame, shared by ALL contexts.
	// A target used in this frame cannot be resized until the token changes.
	// Constant tokens are safe but prevent recycling differently sized targets.
	virtual std::uint64_t RenderFrame() const = 0;
	// Physical-pixel, transparent, premultiplied targets. The runtime owns slot
	// leases (including immutable mask snapshots); zero is the caller's output.
	// BeginLayer clears a reused slot. EndLayer only changes the active target.
	virtual bool BeginLayer(std::uint32_t id, int width, int height) = 0;
	virtual void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const Bounds& clip) = 0;
	// Multiply destination RGBA by the mask alpha, including zero outside its
	// paths. Source and destination are distinct; neither is the base surface.
	virtual void MaskLayer(std::uint32_t mask, std::uint32_t destination, const Bounds& clip) = 0;
	virtual void EndLayer(std::uint32_t restore) = 0;
	virtual FontMetrics GetFontMetrics(const std::string& family, int pixelSize) = 0;
	virtual Glyph GetGlyph(const std::string& family, int pixelSize, std::uint32_t codepoint) = 0;
private:
	// Target identities must survive a last-context close/reopen within one
	// host submission frame, even though RmlUi services can be shut down then.
	struct Shared;
	std::unique_ptr<Shared> shared;
	friend class Runtime;
};

// One independent document/context. Live runtimes share process-wide services
// and must use the same Host, which outlives all of them. Call on one engine
// thread, without reentering from Host callbacks. State, input, clocks, geometry
// and composition leases are per context; closing one leaves the others alive.
// Runtime contains no platform window, GL or input capture.
class Runtime {
public:
	explicit Runtime(Host& host);
	~Runtime();
	Runtime(const Runtime&) = delete;
	Runtime& operator=(const Runtime&) = delete;
	bool Initialize();
	void Shutdown();
	// Integration-spike entry point, not the canonical editor serialization.
	bool LoadMarkup(const std::string& markup, const std::string& sourcePath);
	bool LoadDocument(const std::string& source, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics);
	bool SetState(const StateValues& changes, std::string& error, double monotonicSeconds);
	StateValues GetState(bool includeHostSources = true) const;
	std::optional<Value> PresentedValue(const std::string& node, const std::string& property) const;
	// Public legacy names are explicitly mapped by the canonical document.
	// Values are data, never CSS/markup. Failures leave outputs/state untouched.
	bool GetPresentationAlias(const std::string& name, std::string& value) const;
	bool SetPresentationAlias(const std::string& name, const std::string& value,
		bool overrideExpression, std::string& error);
	struct EventEffects {
		StateValues stateChanges;
		std::vector<ActionInvocation> actions;
	};
	bool HasEvent(const std::string& name) const;
	// Pending caller values and fresh host sources enter the same transaction as
	// the program. Effects contain only committed explicit writes/invocations;
	// the adapter publishes them without replay during restore or resource reset.
	bool RunEvent(const std::string& name, double monotonicSeconds, EventEffects& effects,
		std::string& error, const StateValues& application = {},
		const ActionValidator& validate = {}, size_t maxActions = 256);
	bool ResolveAction(const std::string& id, ActionInvocation& invocation, std::string& error,
		const StateValue* input = nullptr) const;
	std::uint64_t StateRevision() const;
	// Versioned instance data for the exact canonical source/path already loaded.
	// Snapshot failure leaves output unchanged. Restore is transactional and
	// reanchors presentation progress at the supplied monotonic time. Host CVar
	// values, pending actions and transient pointer/press state are not restored.
	static constexpr size_t MaxSnapshotBytes = 128u * 1024u * 1024u;
	bool SaveSnapshot(std::string& snapshot, std::string& error, double monotonicSeconds) const;
	bool RestoreSnapshot(const std::string& snapshot, std::string& error, double monotonicSeconds);
	bool PlayTimeline(const std::string& id, double monotonicSeconds);
	void PauseTimeline(const std::string& id, double monotonicSeconds);
	void ResumeTimeline(const std::string& id, double monotonicSeconds);
	void CancelTimeline(const std::string& id, CancelPolicy policy, double monotonicSeconds);
	void SetReducedMotion(bool enabled, double monotonicSeconds);
	void CloseDocument();
	void Frame(const Viewport& viewport, double monotonicSeconds);
	// Device adapters provide events; these calls never query/control a device.
	// Pointer coordinates are window units, converted once using the last frame.
	void PointerMove(float windowX, float windowY, double monotonicSeconds);
	void PointerButton(bool down, double monotonicSeconds);
	void PointerWheel(int rows, double monotonicSeconds);
	void MenuAction(MenuInput input, bool down, double monotonicSeconds);
	void CancelInput(double monotonicSeconds);
	// After successful RestoreSnapshot: quarantine adapter sources with
	// Input::Cancel(false), discard its routed cancellation events, then call
	// this before routing fresh input. Clears logical held latches only, so
	// suppressed old releases cannot strand them or restart restored feedback.
	// Also valid after CancelInput when the adapter already quarantined sources.
	void ReleaseInputSources();
	bool FocusControl(const std::string& id, double monotonicSeconds);
	bool SetControlEnabled(const std::string& id, bool enabled, double monotonicSeconds);
	bool PushModal(const std::string& root, double monotonicSeconds);
	bool PopModal(double monotonicSeconds);
	std::string FocusedControl() const;
	std::optional<ControlState> GetControlState(const std::string& id) const;
	std::optional<WidgetViewState> GetWidgetState(const std::string& id) const;
	bool AcknowledgeControlProposal(const std::string& id, std::uint64_t token, bool accepted);
	// Local edit operations require focused eligibility and the current exact
	// edit identity. They never write an accepted setting or access a device.
	bool BeginNumberEdit(const std::string& id, std::string& error, double seconds);
	bool SetNumberSelection(const std::string& id, NumberEditIdentity expected,
		std::size_t anchor, std::size_t caret, std::string& error, double seconds);
	bool ApplyNumberInput(const std::string& id, NumberEditIdentity expected,
		const TextInputEvent& event, std::string& error, double seconds);
	bool SetNumberNotice(const std::string& id, NumberEditIdentity expected,
		NumberEditNotice notice, std::string& error, double seconds);
	bool ReplaceNumberSelection(const std::string& id, NumberEditIdentity expected,
		std::string_view text, std::string& error, double seconds);
	// Local scalar-LTR editing only, using current shared font-run boundaries.
	// Does not draw, commit accepted state, or acquire native keyboard authority.
	// Word commands fail until the run provider supplies explicit word stops.
	bool NumberCommand(const std::string& id, NumberEditIdentity expected,
		TextEditCommand command, bool extendSelection, std::string& error, double seconds);
	bool UndoNumberEdit(const std::string& id, NumberEditIdentity expected, bool redo,
		std::string& error, double seconds);
	bool CommitNumberEdit(const std::string& id, NumberEditIdentity expected,
		std::string& error, double seconds);
	// Explicit conflict recovery. Keeping a draft only acknowledges the current
	// baseline; a separate validated commit still proposes the local number.
	bool ResolveNumberConflict(const std::string& id, NumberEditIdentity expected,
		bool keepDraft, std::string& error, double seconds);
	bool CancelNumberEdit(const std::string& id, NumberEditIdentity expected, double seconds);
	// Observe fresh host readbacks before querying/consuming a local-draft barrier.
	// These guard local buffers only; native queued/document state is not attached.
	bool QueryNumberDrafts(NumberDraftSummary& out, std::string& error, double seconds);
	bool DiscardNumberDrafts(const NumberDraftBarrier& expected, std::string& error, double seconds);
	bool FocusNumberDraft(const NumberDraftBarrier& expected, const std::string& control, std::string& error, double seconds);
	std::optional<NumberTextGeometry> GetNumberGeometry(const std::string& id) const;
	// Refresh host values and current eligibility, then copy the focused active
	// editor. Never starts/rebases an inactive draft or serializes a live token.
	std::optional<NumberEditorContext> QueryNumberEditor(std::string& error, double seconds);
	std::vector<ControlAction> TakeActions();
	// Recheck queued activations after earlier programs may change eligibility.
	bool CanActivateControl(const std::string& id, double monotonicSeconds);
	// Validate every queued Back against the exact still-active scope lifetime.
	bool CanDispatchModalBack(const ControlAction& action, double monotonicSeconds);
	bool CanDispatchControlAction(const ControlAction& action, double monotonicSeconds);
	bool GetBounds(const std::string& id, Bounds& bounds) const;
	bool SetProperty(const std::string& id, const std::string& property, const std::string& value);
	bool SetText(const std::string& id, const std::string& text);
	bool IsLoaded() const;
	RuntimeStatistics Statistics() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
