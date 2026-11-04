// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "Document.h"
#include "Motion.h"
#include "Behavior.h"
#include "Interaction.h"
#include "DocumentEdit.h"
#include "Hud.h"

namespace openq4::ui {

// The retained library owns layout in physical pixels. Engine conversion to
// its 640x480 submission space happens only at the final drawing boundary.
struct Viewport {
	int width = 1280, height = 720;
	float displayScale = 1, userScale = 1;
	float pixelDensityX = 1, pixelDensityY = 1;
	float originX = 0, originY = 0;
	// Typography scales separately from layout density and pointer coordinates.
	float textScale = 1;
	// Optional root-menu safety fit, separate from the saved preferences.
	// Surface/editor viewports retain 1 unless their owner requests fitting.
	float fitScale = 1;
	// A document's view-height canvas (DocumentModel::canvasHeight): when
	// positive, one canvas height of dp spans the physical height, replacing
	// display density and user scale. Typography keeps its own text scale.
	float canvasHeight = 0;
	float canvasMinWidth = 0;
	// The window's safe area as physical-pixel insets from the view's edges
	// (camera cutouts, rounded corners, status and gesture bars). A view-height
	// canvas fits the safe area, and documents read the insets in dp through
	// the safe view sources; nothing else moves.
	float safeLeft = 0, safeTop = 0, safeRight = 0, safeBottom = 0;
	float SafeWidth() const;
	float SafeHeight() const;
	float DpRatio() const;
	float TextRatio() const;
	void FitToMinimum(float widthDp, float heightDp);
	void WindowToDocument(float x, float y, float& outX, float& outY) const;
};

struct Vertex {
	float x = 0, y = 0, u = 0, v = 0;
	// Premultiplied sRGB, kept floating point while clipping.
	float r = 1, g = 1, b = 1, a = 1;
};

struct Bounds { float x = 0, y = 0, width = 0, height = 0; };
struct TextFit {
	bool overflow = false;
	int maxIndex = -1, lastWhitespace = -1; // UTF-8 byte indices, as in the public GUI API.
};
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
struct RuntimeCanvasIdentity {
    std::uint64_t lifetime=0,revision=0;
    bool operator==(const RuntimeCanvasIdentity&) const = default;
};
using RuntimeNativeVacancy=Interaction::DocumentVacancy;
struct RuntimeDocumentOptions {
    std::string sourcePath;
    Viewport viewport;
    double seconds=0;
    bool reducedMotion=false;
    StateValues application;
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
	bool timingsEnabled = true;
	std::chrono::steady_clock::time_point Now() const {
		return timingsEnabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
	}
	double frameMilliseconds = 0, updateMilliseconds = 0, renderMilliseconds = 0;
	double vectorCompileMilliseconds = 0, vectorUploadMilliseconds = 0;
	double presentationMilliseconds = 0, layoutMilliseconds = 0;
	double sourceMilliseconds = 0, motionMilliseconds = 0, applyMilliseconds = 0, hostDrawMilliseconds = 0;
	std::uint64_t layoutUpdates = 0, interactionUpdates = 0;
	std::uint64_t vectorElements = 0, vectorPathsCompiled = 0, vectorCacheHits = 0, vectorUploads = 0;
	std::uint64_t vectorHitPathsCompiled = 0, vectorHitCacheHits = 0;
	std::uint64_t geometryCompiles = 0, drawCalls = 0, submittedVertices = 0, submittedIndices = 0;
	std::uint64_t residentGeometryCount = 0, residentGeometryBytes = 0;
	std::uint64_t visibleVectorCacheBytes = 0;
	// Pushed layers that received a host target, their composites, and the
	// deepest stack pushed; layers composited at zero opacity or clipped away
	// are dropped before any target is drawn.
	std::uint64_t layerPushes = 0, layerComposites = 0, peakLayerDepth = 0;
	std::uint64_t layerElisions = 0;
	std::uint64_t layerCacheHits = 0;
	std::uint64_t frameReplays = 0, liveTextRuns = 0;
	std::uint64_t directMaskDraws = 0;
	std::uint64_t maskSnapshots = 0, maskApplications = 0, peakLayerTargets = 0;
	std::uint64_t multiplyFallbacks = 0; // Multiply paths drawn as luminance darkening.
	// Soft-focus backdrops drawn by the host, and those it could not draw.
	std::uint64_t backdropComposites = 0, backdropFallbacks = 0;
	// Overflow clips under a transform: exact rectangles, and shapes clipped to their bounds.
	std::uint64_t clipMasks = 0, clipMaskFallbacks = 0;
	// Shared service residency, sampled by Statistics(); idle backends are reused.
	std::uint64_t activeContexts = 0, residentBackends = 0;
	// Authored property values the frame's presentation passes walked: none
	// while nothing they read has changed since the last complete pass.
	std::uint64_t motionInputsScanned = 0;
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
	// Immutable RGBA8 pixels with premultiplied alpha, copied before returning.
	// A nonzero handle is unique among live generated images on this host and
	// draws through the ordinary material boundary. Release must preserve any
	// already queued draws; a replacement obtains a separate live handle.
	virtual std::uintptr_t GenerateTexture(std::span<const std::uint8_t> pixels, int width, int height) { return 0; }
	virtual void ReleaseGeneratedTexture(std::uintptr_t texture) {}
	// Called after layout and before any UI paint. The returned live image is
	// sampled by the ordinary retained geometry/composition path. The owner
	// lease is unique across all contexts; retirement preserves queued draws.
	// Providers must either prepare the whole scene or fail with no UI output.
	virtual bool RenderModelScene(std::uint64_t lease, const ModelScene& scene, int width, int height,
		double seconds, std::string& image, std::string& error) { error="Model previews are unavailable on this host"; return false; }
	virtual void ReleaseModelScene(std::uint64_t lease) {}
	virtual void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t material) = 0;
	// Optional retained submission: while `revision` is unchanged, `mesh` names
	// the same vertices, indices and material as its previous draw, so a host
	// may reuse what it derived from them. Identities are never reused, and
	// ReleaseMesh retires one. The defaults draw immediately and keep nothing.
	virtual void DrawMesh(std::uint64_t mesh, std::uint64_t revision, const std::vector<Vertex>& vertices,
		const std::vector<int>& indices, std::uintptr_t material) { Draw(vertices,indices,material); }
	// Optional exact opacity of one solid mesh, after proving its coverage
	// does not overlap. False must submit nothing; ordinary composition follows.
	virtual bool DrawMeshOpacity(std::uint64_t mesh, std::uint64_t revision, float opacity) { return false; }
	// Read-only admission check for a recorded direct-opacity draw. A false
	// result rebuilds the frame before any output has been submitted.
	virtual bool CanDrawMeshOpacity(std::uint64_t mesh, std::uint64_t revision) const { return false; }
	virtual void ReleaseMesh(std::uint64_t mesh) {}
	// Identifies the host's current submission frame, shared by ALL contexts.
	// A target used in this frame cannot be resized until the token changes.
	// Constant tokens are safe but prevent recycling differently sized targets.
	virtual std::uint64_t RenderFrame() const = 0;
	// Physical-pixel, transparent, premultiplied targets. The runtime owns slot
	// leases (including immutable mask snapshots); zero is the caller's output.
	// BeginLayer clears a reused slot. EndLayer only changes the active target.
	virtual bool BeginLayer(std::uint32_t id, int width, int height) = 0;
	// Optional pressure relief for an unleased target from an older host frame.
	// Discard its pixels/storage without changing the current drawing target.
	virtual bool TrimLayer(std::uint32_t id) { return false; }
	// Opt in only when layer pixels survive submission frames until BeginLayer
	// clears/reallocates that slot or the runtime is destroyed for device reset.
	virtual bool CanCacheLayers() const { return false; }
	virtual bool CanReplayFrames() const { return true; }
	// Counts remain available without clock queries around every draw. Tools
	// default to measuring; game hosts can enable timings only while profiling.
	virtual bool CollectTimings() const { return true; }
	virtual void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const Bounds& clip) = 0;
	// Composite a cached source through disjoint analytic mask cells directly.
	// Mask alpha modulates all premultiplied source channels; the source target
	// remains unchanged and the base surface is left bound.
	virtual bool CanCompositeMaskedLayer() const { return false; }
	virtual void CompositeMaskedLayer(std::uint32_t source, const std::vector<Vertex>& mask,
		const std::vector<int>& indices, float opacity) {}
	// Exact pixel-area coverage of a small convex linear-alpha mask. Source 0
	// paints white coverage into the current target; a cached source is sampled
	// once through the mask onto the base surface. The source stays immutable.
	virtual bool CanDrawLinearMask() const { return false; }
	virtual void DrawLinearMask(std::uint32_t source, const LinearMask& mask, const Bounds& clip, float opacity) {}
	// Multiply destination RGBA by the mask alpha, including zero outside its
	// paths. Source and destination are distinct; neither is the base surface.
	virtual void MaskLayer(std::uint32_t mask, std::uint32_t destination, const Bounds& clip) = 0;
	virtual void EndLayer(std::uint32_t restore) = 0;
	virtual FontMetrics GetFontMetrics(const std::string& family, int pixelSize) = 0;
	virtual Glyph GetGlyph(const std::string& family, int pixelSize, std::uint32_t codepoint) = 0;
	// Optional migration text layout. Quads are local source units, before the
	// node transform; 1 source unit is 1.5 canvas dp. The engine uses the same
	// atlas selection, line height, advances, clipping and padding as stock GUIs.
	virtual bool LayoutLegacyText(const std::string& text, const std::string& family, float scale,
		float width, float height, int align, bool clip, std::vector<HudPrimitive>& output) { return false; }
	// Untextured material that multiplies the destination by each vertex colour
	// (straight RGB; white leaves it unchanged). Zero means unsupported: the
	// runtime then darkens by the colour's luminance with ordinary composition.
	virtual std::uintptr_t MultiplyMaterial() { return 0; }
	// Soft focus (backdrop-filter): draw the base surface, as composed so far,
	// Gaussian-blurred by `sigma` physical pixels and saturated by `saturation`
	// (1 keeps colour; luma is preserved, so it never dims) into `destination`,
	// opaque within `region`. `destination` is left bound. False means
	// unsupported: the runtime leaves the layer transparent and counts it, and
	// the document's scrim fallback stands in.
	virtual bool SoftenBackdrop(std::uint32_t destination, float sigma, float saturation, const Bounds& region) { return false; }
	// A settled, opaque full-view backdrop can finish directly on the base
	// surface. The read-only query guarantees its resources are already ready;
	// a false draw result must leave that surface unchanged.
	virtual bool CanSoftenBackdropDirect(float sigma, float saturation, const Bounds& region) const { return false; }
	virtual bool SoftenBackdropDirect(float sigma, float saturation, const Bounds& region) { return false; }
	// The speed streak (backdrop-filter: streak()), The Awakening's speeder
	// blur: draw the view into `destination`, opaque within `region`, each pixel
	// averaging six samples toward the view's centre, 1 to 1.5 times its reach
	// away. The reach follows the expansion's square frame, from nothing a
	// quarter of the view in to `strength` times 5% of the view at its edges.
	// False means unsupported: the layer stays transparent and the view
	// unchanged, and the runtime counts it.
	virtual bool StreakBackdrop(std::uint32_t destination, float strength, const Bounds& region) { return false; }
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
    class PreparedDocument final {
    public:
        ~PreparedDocument();
        PreparedDocument(const PreparedDocument&)=delete;
        PreparedDocument& operator=(const PreparedDocument&)=delete;
    private:
        struct Data;
        explicit PreparedDocument(std::shared_ptr<Data>);
        std::shared_ptr<Data> data;
        friend class Runtime;
    };
    RuntimeCanvasIdentity CanvasIdentity() const noexcept;
    RuntimeNativeVacancy NativeVacancy() const noexcept;
    bool HasActiveCanvasCallback() const noexcept;
    // Registered view destruction revokes input now; resource cleanup waits
    // until an active callback has unwound. This flag cannot be reset.
    void RetireCanvasOwner() noexcept;
    bool ObserveNumberNativeCandidate(NumberNativeCandidate&) const noexcept;
    // Source/path are borrowed until any mutating operation, never native authority.
    bool SourceMatches(const std::string&,const std::string&) const noexcept;
    std::unique_ptr<PreparedDocument> PrepareDocument(RuntimeCanvasIdentity,
        const DocumentEdit::Prepared&,RuntimeDocumentOptions,std::vector<Diagnostic>&) noexcept;
    bool CanPublishDocument(const DocumentEdit&,const DocumentEdit::Prepared&,const PreparedDocument&) const noexcept;
    bool PublishDocument(DocumentEdit&,DocumentEdit::Prepared&,PreparedDocument&,DocumentEditReceipt&) noexcept;
    // Invalidates/cleans pending contexts before shared host resources reset.
    // In-flight preparation is invalidated but cleanup waits for its stack.
    bool AbortPreparedDocument() noexcept;
	explicit Runtime(Host& host);
	~Runtime();
	Runtime(const Runtime&) = delete;
	Runtime& operator=(const Runtime&) = delete;
	bool Initialize();
	void Shutdown();
	// Integration-spike entry point, not the canonical editor serialization.
	bool LoadMarkup(const std::string& markup, const std::string& sourcePath);
	bool LoadDocument(const std::string& source, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics);
	// The HUD bridge supplies presentation data to an explicit artwork catalog.
	// This does not edit authored nodes or grant any input/command authority.
	bool SetHudPresentation(const std::vector<HudPrimitive>& items, const Viewport&, std::string& error);
	// A document its caller already loaded, shared rather than parsed again.
	bool LoadDocument(const Document& document, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics);
	bool SetState(const StateValues& changes, std::string& error, double monotonicSeconds);
	StateValues GetState(bool includeHostSources = true) const;
	// One state's value without copying the rest; false when it is undeclared.
	bool GetStateValue(const std::string& id, StateValue& value) const;
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
	// Application writes of timeline completion programs run by frames since the
	// last call; programs completing inside RunEvent join its effects instead.
	// The adapter publishes them as it publishes event effects.
	StateValues TakeCompletionWrites();
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
	// False when layout or composition cannot present; callers can draw their
	// fallback in the same frame instead of accepting a partial HUD.
	bool Frame(const Viewport& viewport, double monotonicSeconds);
	// Device adapters provide events; these calls never query/control a device.
	// Pointer coordinates are window units, converted once using the last frame.
	void PointerMove(float windowX, float windowY, double monotonicSeconds);
	void PointerButton(bool down, double monotonicSeconds);
	void PointerWheel(int rows, double monotonicSeconds);
	// The right stick (section 13.5) scrolls the detail area dp density-
	// independent pixels, positive toward its end: the first scrolling
	// container that shows, that input may reach and that holds no control,
	// such as the pause's objectives. False when none can move.
	bool ScrollDetail(float dp, double monotonicSeconds);
	// Whether the pointer is a finger (section 13.6): a touch that moves past
	// 8 dp is no longer a tap, so its release activates nothing. In a
	// scrolling container it drags the content, which keeps its release
	// velocity and lights the list's rim at either end; across a tab strip it
	// swipes; resting 500 ms on a button that names a detail it opens that
	// detail instead of activating.
	void SetTouchPointer(bool touch);
	// The document events touch gestures asked for, oldest first: a swipe
	// across a tab strip asks for onTabPrevious or onTabNext. The adapter
	// runs them as it runs Q and E.
	std::vector<std::string> TakeGestureEvents();
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
	// Whether a modal scope, an authored dialog or a pushed one, holds the
	// input, so that Back closes it rather than the document.
	bool ModalOpen() const;
	std::string FocusedControl() const;
	// The control under the pointer that would take a press, or empty.
	std::string HoveredControl() const;
	// The verb a prompt bar shows for Accept (section 13.4): the focused
	// button's prompt, `fallback` for a control that names none, or empty
	// while nothing focused can be activated.
	std::string AcceptPrompt(const std::string& fallback, double monotonicSeconds);
	std::optional<ControlState> GetControlState(const std::string& id) const;
	std::optional<WidgetViewState> GetWidgetState(const std::string& id) const;
    // Semantic popup presentation only. No device event, option acceptance or
    // application action is generated. The opening identity survives layout
    // refresh; scrolling additionally requires fresh actual popup geometry.
    bool OpenChoicePopup(const std::string& id, double seconds);
    bool CloseChoicePopup(const std::string& id, std::uint64_t openToken, double seconds);
    bool ScrollChoicePopup(const std::string& id, std::uint64_t openToken, ScrollStep step, double seconds);
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
	// Includes attached native presentation and unsettled-group blockers. The
	// caller separately owns provider queues and external native retirement.
	bool QueryNumberDrafts(NumberDraftSummary& out, std::string& error, double seconds);
	bool DiscardNumberDrafts(const NumberDraftBarrier& expected, std::string& error, double seconds);
	bool FocusNumberDraft(const NumberDraftBarrier& expected, const std::string& control, std::string& error, double seconds);
	std::optional<NumberTextGeometry> GetNumberGeometry(const std::string& id) const;
	// Refresh host values and current eligibility, then copy the focused active
	// ordinary editor. Attached native presentation has its own authority and is
	// unavailable here, including after settlement. Never starts/rebases an
	// inactive draft or serializes a live token.
	std::optional<NumberEditorContext> QueryNumberEditor(std::string& error, double seconds);
	// Native attachment and observation refresh host state. The caller validates
	// its managed allocation, window and provider collection separately. No native
	// device is activated here, and attachment never starts an inactive editor.
	bool AttachNumberNative(const TextEditorIdentity& owner, NativeTextIdentity native,
		NativeTextEditorBarrier& out, std::string& error, double seconds);
	bool RefreshNumberNative(const NativeTextEditorBarrier& expected,
		NativeTextEditorView& out, std::string& error, double seconds);
	bool IsNumberNativeCurrent(const NativeTextEditorBarrier&) const noexcept;
	// These model operations invoke no Host callbacks. A collection coordinator
	// must refresh and validate the live owner/provider before each operation,
	// then execute returned native receipts in the same checked boundary.
	bool BeginNumberNativeCollection(const NativeTextEditorBarrier&, const NativeTextCollection&,
		NativeTextEditorBarrier& out, std::string& error);
	bool ApplyNumberNative(const NativeTextEditorBarrier&, const NativeTextOffer&,
		NativeTextEditorReceipt& out, std::string& error);
	bool CompleteNumberNativeCollection(const NativeTextEditorBarrier&, const NativeTextCollection&,
		NativeTextEditorBarrier& out, std::string& error);
	bool SettleNumberNative(const NativeTextEditorBarrier&, NativeTextEditorReceipt& out, std::string& error);
	std::unique_ptr<Interaction::NativeSettlement> PrepareNumberNativeSettlement(const NativeTextEditorBarrier&, std::string& error);
	bool PublishNumberNativeSettlement(Interaction::NativeSettlement&, NativeTextEditorReceipt& out) noexcept;
	bool RetireNumberNative(const NativeTextEditorBarrier&, NativeTextEditorReceipt& out, std::string& error);
	bool RetireNumberNativeExact(NativeTextIdentity, const TextEditorIdentity&) noexcept;
	NativeTextPresence QueryNumberNativePresence(NativeTextIdentity, const TextEditorIdentity&) const noexcept;
	std::vector<ControlAction> TakeActions();
	// Recheck queued activations after earlier programs may change eligibility.
	bool CanActivateControl(const std::string& id, double monotonicSeconds);
	// Validate every queued Back against the exact still-active scope lifetime.
	bool CanDispatchModalBack(const ControlAction& action, double monotonicSeconds);
	bool CanDispatchControlAction(const ControlAction& action, double monotonicSeconds);
	bool GetBounds(const std::string& id, Bounds& bounds) const;
	// Measure a plain, unwrapped scalar string with a visible text node's last
	// laid-out content width and font. A first overwide scalar stays whole so
	// callers can progress. Unavailable/invalid queries preserve the output.
	// This does not implement grapheme/shaping clusters or legacy text escapes.
	bool QueryTextFit(const std::string& id, std::string_view text, TextFit& result) const;
	bool SetProperty(const std::string& id, const std::string& property, const std::string& value);
	bool SetText(const std::string& id, const std::string& text);
	bool IsLoaded() const;
	RuntimeStatistics Statistics() const;
private:
    struct Canvas;
    std::shared_ptr<Canvas> canvas;
    bool Mutate(bool cleanup=false) noexcept;
    bool Layout(const Viewport&,double);
    bool LoadCanonical(std::unique_ptr<Document> candidate, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics);
    struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
