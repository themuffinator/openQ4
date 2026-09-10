// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Document.h"
#include "TextEdit.h"
#include "TextEditCommand.h"
#include "NativeTextEditor.h"
#include <cstdint>
#include <functional>
#include <set>

namespace openq4::ui {
enum class MenuInput { Next, Previous, Up, Down, Left, Right, Accept, Back, Home, End, PageUp, PageDown };
struct ControlBounds { float x = 0, y = 0, width = 0, height = 0; bool visible = false; };
struct ControlAction {
	enum class Kind { Activate, Back } kind = Kind::Activate;
	std::string document, node, action, event;
	std::optional<StateValue> proposal;
	std::uint64_t proposalToken = 0;
	std::uint64_t modalToken = 0; // Transient scope identity for every Back record.
	std::uint64_t editSession = 0, editRevision = 0; // Number proposals only; never persisted.
};
struct ControlFeedback { std::string node, timeline; ControlState state = ControlState::Default; };
struct InteractionSnapshot {
	struct Modal { std::string root, restore; bool authored = false; };
	std::string focus;
	std::vector<Modal> modals;
	std::map<std::string,bool> enabled;
	std::map<std::string,ControlState> presented;
	bool focusPending = false;
	std::string pendingFocus;
};
struct NumberEditIdentity {
	std::uint64_t session = 0, revision = 0;
	bool operator==(const NumberEditIdentity&) const = default;
};
enum class NumberEditNotice { None, ClipboardReadFailed, ClipboardWriteFailed, ClipboardRejected };
struct NumberEditView {
	NumberEditNotice notice = NumberEditNotice::None;
	TextEditState state;
	std::optional<TextInputEvent> composition;
	NumberEditIdentity identity;
	TextNumberStatus status = TextNumberStatus::Empty;
	bool dirty = false, conflict = false, canUndo = false, canRedo = false;
	bool active = false; // Restored text waits for fresh eligible focus.
	// Full native bytes/ranges, separate from stable state and the legacy overlay.
	std::optional<NativeTextSnapshot> nativePresentation;
	bool nativeUnsettled = false;
};
struct NumberDraftStamp {
	std::string control;
	std::uint64_t lifetime = 0, revision = 0;
	bool operator==(const NumberDraftStamp&) const = default;
};
struct NumberDraftBarrier {
	std::uint64_t instance = 0;
	// Every extant editor, including clean/inactive editors, in document order.
	std::vector<NumberDraftStamp> editors;
	bool operator==(const NumberDraftBarrier&) const = default;
};
struct NumberDraftStatus {
	std::string control;
	TextNumberStatus status = TextNumberStatus::Empty;
	bool dirty = false, conflict = false, pending = false, composing = false, active = false;
	// Collection/group presentation has not settled into stable local history.
	bool nativeUnsettled = false;
};
struct NumberDraftSummary {
	NumberDraftBarrier barrier;
	std::vector<NumberDraftStatus> blocking;
};
struct WidgetViewState {
	ControlRole role = ControlRole::Button;
	StateValue accepted;
	bool mixed = false;
	std::optional<StateValue> preview, pending, rejected;
	std::uint64_t proposalToken = 0;
	bool popupOpen = false;
	std::string highlight;
	unsigned firstVisible = 0;
	std::optional<NumberEditView> number;
};
struct NumberEditorSnapshot {
	TextEditState state;
	std::vector<TextEditState> undo, redo;
	double baselineValue = 0;
	std::string baselineText;
	bool conflict = false;
};
struct ValueWidgetSnapshot {
	static constexpr std::size_t MaxNumberEditors = 256;
	static constexpr std::size_t MaxNumberTextBytes = 16 * 1024 * 1024;
	struct Widget {
		ControlRole role = ControlRole::Button;
		unsigned firstVisible = 0;
		std::optional<NumberEditorSnapshot> number;
	};
	unsigned version = 2;
	std::map<std::string,Widget> widgets;
};

// Pure semantic interaction shared by the game and editor. The layout adapter
// supplies current projected boxes and hit IDs; this class never reads a device.
class Interaction {
public:
	Interaction();
	~Interaction();
	Interaction(const Interaction&);
	Interaction(Interaction&&) noexcept;
	Interaction& operator=(const Interaction&) = delete;
	Interaction& operator=(Interaction&&) = delete;
	// Copies are pure preparation views, never native authorities. Validate the
	// original complete draft/native barriers before replacing the live state.
	// Adopt stages retirement first; success retires any live native binding.
	// Save-only copies and failed adoption cannot retire the originating binding.
	bool CanAdopt(const Interaction& candidate, std::string& error) const;
	bool Adopt(Interaction&& candidate, std::string& error);
	void Reset(const DocumentModel& model);
	void SetBounds(const std::map<std::string,ControlBounds>& bounds, bool freshLayout = true);
	void InvalidateLayout();
	// Called before incoming bindings/bounds can invalidate the old selection.
	// Visible authored scopes must form one outer-to-inner ancestry chain. A
	// rejected chain blocks all input until a valid synchronization succeeds.
	bool SyncAuthoredModals(const std::vector<std::string>& roots, std::string& error);
	bool HasAuthoredModals() const { return !authoredModals.empty(); }
	bool CanDispatchModalBack(const ControlAction& action) const;
	std::uint64_t ModalToken() const { return modalBlocked ? 0 : modalToken; }
	bool CanDispatchControlAction(const ControlAction& action) const;
	bool AllowsNode(const std::string& id) const { return !modalBlocked && Within(id,Modal()); }
	// Complete evaluated value-control map; invalid input leaves all interaction
	// state unchanged. Values outside slider bounds or choice lists remain data.
	bool SetReadbacks(const std::map<std::string,ControlReadback>& readbacks, std::string& error);
	bool SetEnabled(const std::string& id, bool enabled);
	bool Focus(const std::string& id);
	void Hover(const std::string& id);
	// Layout supplies a track fraction (increasing toward maximum, including
	// outside-track values during capture) or a stable popup option ID.
	void PointerPart(const std::string& id, std::optional<double> trackFraction = {}, const std::string& option = {});
	std::string CapturedPointerControl() const { return dragging; }
	void Pointer(bool down);
	void Input(MenuInput input, bool down);
	// A wheel pulse is not a physical source release. Preserve held-source
	// quarantine while issuing one independent navigation step.
	void NavigationPulse(MenuInput input);
	// Cancel arms without forgetting held inputs. Their eventual releases must
	// not activate a different control after replacement, focus loss or a modal.
	void Cancel();
	// After Cancel/Restore and adapter source quarantine, release logical held
	// latches without changing focus, arms, feedback, actions or presentation.
	void ReleaseInputSources();
	bool PushModal(const std::string& root);
	bool PopModal();
	std::string Focused() const { return focused; }
	std::string Modal() const { return modals.empty() ? std::string{} : modals.back().root; }
	std::optional<ControlState> State(const std::string& id) const;
	std::vector<ControlFeedback> TakeFeedback();
	std::vector<ControlAction> TakeActions();
	bool CanActivate(const std::string& id) const { return !focusPending && Eligible(id); }
	bool Overflowed() const { return overflowed; }
	// Capture persistent semantics only. Restore cancels queued actions/arms and
	// hover, preserves receiving-instance held latches, and awaits fresh bounds.
	InteractionSnapshot Capture() const;
	bool Restore(const InteractionSnapshot& snapshot, std::string& error);
	std::optional<WidgetViewState> Widget(const std::string& id) const;
	bool AcknowledgeProposal(const std::string& id, std::uint64_t token, bool accepted);
	// Explicit local editing. Begin requires focused eligibility; repeated Begin
	// preserves an existing non-conflicted buffer. No method writes accepted state.
	// Every mutation checks the exact session/revision exposed by Widget().number.
	bool BeginNumberEdit(const std::string& id, std::string& error);
	// Explicit conflict decision, separate from focusing/resuming a draft.
	// Keep adopts the current baseline without committing text; Reload replaces
	// local text/history. Both require the exact active edit identity.
	bool ResolveNumberConflict(const std::string& id, NumberEditIdentity expected,
		bool keepDraft, std::string& error);
	bool SetNumberSelection(const std::string& id, NumberEditIdentity expected,
		std::size_t anchor, std::size_t caret, std::string& error);
	bool ApplyNumberInput(const std::string& id, NumberEditIdentity expected,
		const TextInputEvent& event, std::string& error);
	// Presentation only: checked exact editor, no history/revision/proposal change.
	bool SetNumberNotice(const std::string& id, NumberEditIdentity expected,
		NumberEditNotice notice, std::string& error);
	bool ReplaceNumberSelection(const std::string& id, NumberEditIdentity expected,
		std::string_view text, std::string& error);
	// Checked command result only; no native input authority is acquired. Empty-text
	// replacements delete atomically, preserving the original undo selection.
	// A validated no-op keeps the current revision and rejected presentation.
	bool ApplyNumberOperation(const std::string& id, NumberEditIdentity expected,
		const TextEditOperation& operation, std::string& error);
	bool UndoNumberEdit(const std::string& id, NumberEditIdentity expected, bool redo, std::string& error);
	// Requires Valid parsing and no preedit, conflict or outstanding proposal.
	// Queues one exact double, independent of a sibling slider's step/decimals.
	bool CommitNumberEdit(const std::string& id, NumberEditIdentity expected, std::string& error);
	// Cancellation is an engine-owner operation; a supplied identity additionally
	// protects a deferred cancel. For a native binding the first cancel restores
	// stable local text and retires that binding; a later local cancel discards it.
	// The host must retire its external native document when this binding dies.
	bool CancelNumberEdit(const std::string& id, NumberEditIdentity expected = {});
	// Pure native reconciliation only. Full owner identity must match this exact
	// active Number editor and modal. The caller separately proves native queue,
	// provider and host ownership before each call and executes returned receipts.
	// No keyboard/clipboard/setting authority is conferred by these methods.
	// Bound local edits/commands/commit require explicit retirement first.
	bool AttachNumberNative(const std::string& id, NumberEditIdentity expected,
		const TextEditorIdentity& owner, NativeTextIdentity native,
		NativeTextEditorBarrier& out, std::string& error);
	bool QueryNumberNative(const std::string& id, const TextEditorIdentity& expected,
		NativeTextEditorView& out, std::string& error) const;
	// Callback/allocation-free check after foreign provider/owner observations.
	bool IsNumberNativeCurrent(const NativeTextEditorBarrier& expected) const noexcept;
	bool BeginNumberNativeCollection(const NativeTextEditorBarrier& expected,
		const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error);
	bool CompleteNumberNativeCollection(const NativeTextEditorBarrier& expected,
		const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error);
	bool ApplyNumberNative(const NativeTextEditorBarrier& expected, const NativeTextOffer& offer,
		NativeTextEditorReceipt& out, std::string& error);
	// Stage settlement before a foreign native SyncEngine call. The sealed object
	// owns every allocation; it confers no native authority and may be discarded.
	// Publish rechecks this live originating instance, exact barrier and current
	// eligibility, then installs without allocation. Copies cannot prepare/publish.
	class NativeSettlement {
	public:
		~NativeSettlement();
		NativeSettlement(const NativeSettlement&) = delete;
		NativeSettlement& operator=(const NativeSettlement&) = delete;
		const NativeTextEditorReceipt& Receipt() const;
		const NativeTextSnapshot& Presentation() const;
	private:
		NativeSettlement();
		struct Impl;
		std::unique_ptr<Impl> impl;
		friend class Interaction;
	};
	std::unique_ptr<NativeSettlement> PrepareNumberNativeSettlement(const NativeTextEditorBarrier& expected,
		std::string& error);
	bool PublishNumberNativeSettlement(NativeSettlement&, NativeTextEditorReceipt& out) noexcept;
	bool SettleNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error);
	bool RetireNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error);
	// Emergency teardown matches the original native lease and every owner field
	// except the evolving editing revision. It preserves stable text/history and
	// never allocates, refreshes eligibility or touches a replacement attachment.
	bool RetireNumberNativeExact(NativeTextIdentity, const TextEditorIdentity&) noexcept;
	// Process-local exact barriers, never serialized. Save alone does not retire
	// a barrier; restore/reset, inventory ABA and relevant editor changes do.
	// Query copies status/stamps only, not buffers/history. Failure preserves out.
	bool QueryNumberDrafts(NumberDraftSummary& out, std::string& error) const;
	// Validate the whole inventory before discarding any local draft. Accepted
	// readbacks and application state are unchanged; queued Number proposals die.
	bool DiscardNumberDrafts(const NumberDraftBarrier& expected, std::string& error);
	// Focus/resume one blocking draft after exact inventory + eligibility checks.
	// Never rebase a conflict or commit a value. Failure preserves interaction.
	bool FocusNumberDraft(const NumberDraftBarrier& expected, const std::string& control, std::string& error);
	ValueWidgetSnapshot CaptureWidgets() const;
	// Checked production save boundary; exceeding the aggregate draft budgets
	// fails without copying/truncating drafts or replacing out.
	bool CaptureWidgets(ValueWidgetSnapshot& out, std::string& error) const;
	bool RestoreWidgets(const ValueWidgetSnapshot& snapshot, std::string& error);
private:
	struct NumberEditor {
		NumberEditNotice notice = NumberEditNotice::None;
		TextEditBuffer buffer;
		std::string baselineText;
		double baselineValue = 0;
		NumberEditIdentity identity;
		std::uint64_t draftLifetime = 0, draftRevision = 0;
		bool conflict = false;
		bool detached = true;
		std::shared_ptr<const NativeTextEditorView> native;
	};
	struct Item {
		Control control; ControlBounds bounds; ControlState state = ControlState::Default; bool known = false;
		std::optional<ControlReadback> readback;
		std::optional<StateValue> pending, rejected;
		std::uint64_t proposalToken = 0;
		unsigned firstVisible = 0;
		std::optional<NumberEditor> number;
	};
	struct ModalScope { std::string root, restore; bool authored = false; };
	bool Within(const std::string& id, const std::string& root) const;
	bool Eligible(const std::string& id) const;
	std::string First() const;
	void Navigate(MenuInput input);
	void Activate(const std::string& id);
	bool Propose(const std::string& id, const StateValue& value);
	Item* EditableNumber(const std::string& id, NumberEditIdentity expected, std::string& error);
	bool ChangeNumber(const std::string& id, NumberEditIdentity expected,
		const std::function<bool(TextEditBuffer&,std::string&)>& change, std::string& error);
	bool RebaseNumber(Item& item, std::string& error);
	void RetireNumber(const std::string& id, Item& item);
	void DetachNumber(const std::string& id, Item& item);
	void DropNumberNative(const std::string& id, Item& item);
	Item* NativeNumber(const NativeTextEditorBarrier& expected, std::string& error);
	bool PublishNumberNative(const NativeTextEditorBarrier& expected, std::unique_ptr<NativeTextEditor> prepared,
		std::shared_ptr<const NativeTextEditorView> view, NumberEditor&& editor, std::string& error);
	bool NativeCollection(bool begin, const NativeTextEditorBarrier& expected,
		const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error);
	bool NativeCompletion(bool retire, const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error);
	void MoveFields(Interaction& source) noexcept;
	std::uint64_t authority = 0;
	bool candidate = false;
	NumberDraftBarrier originDrafts;
	std::optional<NativeTextEditorBarrier> originNative;
	NumberDraftBarrier parentDrafts;
	std::optional<NativeTextEditorBarrier> parentNative;
	std::unique_ptr<NativeTextEditor> nativeModel;
	std::string nativeControl;
	const StateValue& EditingValue(const Item& item) const;
	void CancelGesture();
	void ModalChanged();
	void ResolvePendingFocus();
	void OpenPopup(const std::string& id);
	void PopupNavigate(MenuInput input);
	bool OptionEligible(const Item& item, size_t index) const;
	void KeepHighlightVisible();
	void SliderKey(MenuInput input);
	double SliderValue(const SliderSpec& spec, double fraction) const;
	void Refresh();
	void Queue(ControlAction action);
	std::string document, focused, hovered, armed;
	bool pointerHeld = false, acceptHeld = false, backHeld = false, pointerArm = false, overflowed = false;
	std::map<std::string,Item> items;
	std::vector<std::string> order;
	std::map<std::string,std::string> parents;
	std::vector<ModalScope> modals;
	std::map<std::string,ModalSpec> authoredModals;
	std::uint64_t modalToken = 0;
	std::uint64_t numberEpoch = 0;
	bool modalBlocked = false, focusPending = false;
	std::string pendingFocus;
	std::set<MenuInput> heldNavigation, blockedNavigation;
	std::vector<ControlFeedback> feedback;
	std::vector<ControlAction> actions;
	std::string dragging, popup, highlight, pointerOption, armedOption;
	std::optional<double> pointerFraction, dragPreview;
	bool popupAcceptArm = false;
};
} // namespace openq4::ui
