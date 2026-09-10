// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Interaction.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace openq4::ui {
namespace {
// Process-local tokens are never recycled by Reset, restore, or another GUI.
std::uint64_t ProposalToken() {
	static std::atomic<std::uint64_t> next{1};
	auto token = next.load(std::memory_order_relaxed);
	while (token != std::numeric_limits<std::uint64_t>::max()) {
		if (next.compare_exchange_weak(token,token+1,std::memory_order_relaxed)) return token;
	}
	return 0;
}
bool ValidNumber(const Control& control) {
	const auto* spec = std::get_if<NumberSpec>(&control.widget);
	return spec && control.value && control.value->type == 0 && std::isfinite(spec->minimum) &&
		std::isfinite(spec->maximum) && spec->minimum <= spec->maximum && spec->maxBytes > 0 && spec->maxBytes <= TextInputMaxBytes;
}
// JSON numeric authoring expresses a decimal lattice. Floating multiply/add
// can land one ULP beside that lattice (3 * .025 -> .07500000000000001).
// Recover its scale from the shortest authored values, not display decimals,
// then canonicalize only generated ticks. Custom readbacks and Number edits
// never pass through this path. At most 324 fractional digits are required.
int SliderDecimalPlaces(double value) {
	char text[64]; const auto formatted=std::to_chars(text,text+sizeof(text),value);
	if (formatted.ec!=std::errc{}) return 0;
	const auto end=formatted.ptr; const auto exponent=std::find(text,end,'e');
	int power=0;
	if (exponent!=end) {
		const char* first=exponent+1; if (first!=end && *first=='+') ++first;
		const auto parsed=std::from_chars(first,static_cast<const char*>(end),power);
		if (parsed.ec!=std::errc{} || parsed.ptr!=end) return 0;
	}
	const auto point=std::find(text,exponent,'.');
	return std::max(0,static_cast<int>(point==exponent ? 0 : exponent-point-1)-power);
}
double SliderTickValue(const SliderSpec& spec,long double tick) {
	if (tick<=0) return spec.minimum;
	const double value=std::clamp(static_cast<double>(spec.minimum+tick*spec.step),spec.minimum,spec.maximum);
	if (value==spec.minimum || value==spec.maximum) return value;
	char text[768]; const int places=std::max(SliderDecimalPlaces(spec.minimum),SliderDecimalPlaces(spec.step));
	const auto formatted=std::to_chars(text,text+sizeof(text),value,std::chars_format::fixed,places);
	if (formatted.ec!=std::errc{}) return value;
	double canonical=0;
	const auto parsed=std::from_chars(text,formatted.ptr,canonical,std::chars_format::fixed);
	if (parsed.ec!=std::errc{} || parsed.ptr!=formatted.ptr || !std::isfinite(canonical)) return value;
	return std::clamp(canonical,spec.minimum,spec.maximum);
}
TextNumberPolicy NumberPolicy(const Control& control) {
	const auto& spec = std::get<NumberSpec>(control.widget);
	return {spec.minimum,spec.maximum,spec.exponent};
}
bool NativeUnsettled(const NativeTextEditorView& view) {
	return view.barrier.collectionOpen || view.barrier.group || view.awaitingSettlement || !view.presentation.compositions.empty();
}
}
// Keep copy preparation separate from native authority. All plain semantic
// fields are copied; the mutable reconciler is moved only by checked adoption
// or an actual move construction. Native views themselves are immutable.
#define OQ4_INTERACTION_FIELDS(X) \
	X(document) X(focused) X(hovered) X(armed) X(pointerHeld) X(acceptHeld) X(backHeld) X(pointerArm) X(overflowed) \
	X(items) X(order) X(parents) X(modals) X(authoredModals) X(modalToken) X(numberEpoch) X(modalBlocked) X(focusPending) \
	X(pendingFocus) X(heldNavigation) X(blockedNavigation) X(feedback) X(actions) X(dragging) X(popup) X(highlight) \
	X(pointerOption) X(armedOption) X(pointerFraction) X(dragPreview) X(popupAcceptArm) X(nativeControl)
Interaction::Interaction() : authority(ProposalToken()) {}
Interaction::~Interaction() = default;
Interaction::Interaction(const Interaction& other) : authority(other.authority), candidate(true) {
#define COPY(field) field = other.field;
	OQ4_INTERACTION_FIELDS(COPY)
#undef COPY
	std::string error;
	NumberDraftSummary summary;
	if (other.numberEpoch && !other.QueryNumberDrafts(summary,error)) throw std::runtime_error(error);
	parentDrafts = std::move(summary.barrier);
	if (!other.nativeControl.empty()) {
		const auto& item = other.items.at(other.nativeControl);
		if (item.number && item.number->native) parentNative = item.number->native->barrier;
	}
	originDrafts = other.candidate ? other.originDrafts : parentDrafts;
	originNative = other.candidate ? other.originNative : parentNative;
}
void Interaction::MoveFields(Interaction& other) noexcept {
#define MOVE(field) static_assert(std::is_nothrow_move_assignable_v<decltype(field)>); field = std::move(other.field);
	OQ4_INTERACTION_FIELDS(MOVE)
#undef MOVE
	nativeModel = std::move(other.nativeModel);
}
#undef OQ4_INTERACTION_FIELDS
Interaction::Interaction(Interaction&& other) noexcept : authority(other.authority), candidate(other.candidate),
	originDrafts(std::move(other.originDrafts)), originNative(std::move(other.originNative)),
	parentDrafts(std::move(other.parentDrafts)), parentNative(std::move(other.parentNative)) {
	MoveFields(other); other.authority = 0; other.candidate = true;
}
bool Interaction::CanAdopt(const Interaction& prepared, std::string& error) const {
	error.clear();
	if (this == &prepared || !authority || prepared.authority != authority || !prepared.candidate) {
		error = "Interaction candidate has no matching live origin"; return false;
	}
	NumberDraftSummary current;
	if (numberEpoch && !QueryNumberDrafts(current,error)) return false;
	std::optional<NativeTextEditorBarrier> native;
	if (!nativeControl.empty()) {
		const auto& item = items.at(nativeControl);
		if (item.number && item.number->native) native = item.number->native->barrier;
	}
	const auto& expectedDrafts = candidate ? prepared.parentDrafts : prepared.originDrafts;
	const auto& expectedNative = candidate ? prepared.parentNative : prepared.originNative;
	if (current.barrier != expectedDrafts || native != expectedNative ||
		(candidate && (originDrafts != prepared.originDrafts || originNative != prepared.originNative))) {
		error = "Interaction candidate predates the current draft or native collection"; return false;
	}
	return true;
}
bool Interaction::Adopt(Interaction&& prepared, std::string& error) {
	if (!CanAdopt(prepared,error)) return false;
	// Work only on the candidate until every retirement revision is available.
	// Generic state publication never transfers a native lease from a copy.
	for (auto& [id,item] : prepared.items) if (item.number && item.number->native) {
		(void)id;
		const auto revision = ProposalToken();
		if (!revision) { error = "Number native retirement identity exhausted"; return false; }
		item.number->native.reset(); item.number->draftRevision = revision;
		if (!item.number->detached) item.number->identity.revision = revision;
	}
	prepared.nativeControl.clear(); prepared.nativeModel.reset();
	MoveFields(prepared); prepared.authority = 0; return true;
}
void Interaction::Reset(const DocumentModel& model) {
	nativeModel.reset(); nativeControl.clear();
	document = model.id; focused.clear(); hovered.clear(); armed.clear();
	CancelGesture(); pointerOption.clear(); pointerFraction.reset();
	items.clear(); order.clear(); parents.clear(); modals.clear(); feedback.clear(); actions.clear(); overflowed = false;
	authoredModals.clear(); modalBlocked = focusPending = false; pendingFocus.clear();
	modalToken = ProposalToken(); numberEpoch = ProposalToken(); blockedNavigation = heldNavigation;
	std::vector<std::pair<const Node*,std::string>> pending{{&model.root,{}}};
	while (!pending.empty()) {
		const auto [node,parent] = pending.back(); pending.pop_back(); parents[node->id] = parent;
		if (node->control) { items[node->id].control = *node->control; order.push_back(node->id); }
		if (node->modal) authoredModals.emplace(node->id,*node->modal);
		for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) pending.push_back({&*it,node->id});
	}
	Refresh();
}
bool Interaction::SetReadbacks(const std::map<std::string,ControlReadback>& readbacks, std::string& error) {
	error.clear();
	size_t expected = 0;
	for (const auto& [id,item] : items) {
		if (item.control.role == ControlRole::Button) continue;
		++expected;
		const auto found = readbacks.find(id);
		if (found == readbacks.end()) { error = "Missing control readback"; return false; }
		const auto& value = found->second;
		if (item.control.role == ControlRole::Number && !ValidNumber(item.control)) { error = "Invalid number control policy"; return false; }
		if (!ValidStateValue(value.value) || !item.control.value || value.value.index() != item.control.value->type ||
			(item.control.role != ControlRole::Toggle && value.mixed)) { error = "Invalid control readback type"; return false; }
		if (item.control.role == ControlRole::Choice) {
			if (value.enabledOptions.size() != std::get<ChoiceSpec>(item.control.widget).options.size()) { error = "Invalid choice availability"; return false; }
		} else if (!value.enabledOptions.empty()) { error = "Unexpected choice availability"; return false; }
	}
	if (expected != readbacks.size()) { error = "Unknown control readback"; return false; }
	// Reserve every changed draft stamp before publishing any new readback.
	// Matching pending readback is still a change until acknowledgement rebases.
	std::map<std::string,std::uint64_t> revisions;
	for (const auto& [id,value] : readbacks) {
		const auto& item = items.at(id);
		if (item.number && (!item.readback || item.readback->value != value.value)) {
			const auto revision = ProposalToken();
			if (!revision) { error = "Number draft identity exhausted"; return false; }
			revisions.emplace(id,revision);
		}
	}
	for (const auto& [id,value] : readbacks) {
		auto& item = items.at(id);
		const bool changed = !item.readback || item.readback->value != value.value;
		item.readback = value;
		if (changed && item.number) item.number->draftRevision = revisions.at(id);
		if (changed && item.number && item.number->native) DropNumberNative(id,item);
		if (changed && item.number && (!item.pending || *item.pending != value.value)) {
			item.pending.reset(); item.proposalToken = 0; item.rejected.reset();
			std::erase_if(actions,[&](const ControlAction& action) { return action.node == id && action.editSession != 0; });
			if (item.number->buffer.State().text == item.number->baselineText && !item.number->buffer.Composition()) {
				std::string unused;
				if (!RebaseNumber(item,unused)) RetireNumber(id,item);
			} else {
				// Preserve the local text for review, but invalidate its former
				// owner revision and queued proposal when authoritative data wins.
				item.number->buffer.CancelComposition(); item.number->conflict = true; item.number->notice = NumberEditNotice::None;
				if (!item.number->detached) item.number->identity.revision = item.number->draftRevision;
			}
		}
	}
	if (!popup.empty()) {
		const auto& item = items.at(popup); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
		bool eligible = false;
		for (size_t i = 0; i < options.size(); ++i) if (options[i].id == highlight && OptionEligible(item,i)) eligible = true;
		if (!eligible) {
			highlight.clear(); armed.clear(); armedOption.clear(); popupAcceptArm = false;
			for (size_t i = 0; i < options.size(); ++i) if (OptionEligible(item,i)) { highlight = options[i].id; break; }
			KeepHighlightVisible();
		}
	}
	Refresh(); return true;
}
const StateValue& Interaction::EditingValue(const Item& item) const { return item.pending ? *item.pending : item.readback->value; }
bool Interaction::Propose(const std::string& id, const StateValue& value) {
	auto& item = items.at(id);
	if (!Eligible(id) || !item.readback || !ValidStateValue(value) || value.index() != item.readback->value.index()) return false;
	if (actions.size() >= 256) { overflowed = true; return false; }
	const auto token = ProposalToken(); if (!token) { overflowed = true; return false; }
	ControlAction action{ControlAction::Kind::Activate,document,id,item.control.action,item.control.event,value,token};
	Queue(std::move(action)); item.pending = value; item.proposalToken = token; item.rejected.reset();
	if (item.number) item.number->draftRevision = token;
	return true;
}
void Interaction::Activate(const std::string& id) {
	auto& item = items.at(id);
	switch (item.control.role) {
		case ControlRole::Button: Queue({ControlAction::Kind::Activate,document,id,item.control.action,item.control.event}); break;
		case ControlRole::Toggle: Propose(id,item.readback->mixed && !item.pending ? true : !std::get<bool>(EditingValue(item))); break;
		case ControlRole::Choice: OpenPopup(id); break;
		case ControlRole::Slider: break; // Value changes have their own semantics.
		case ControlRole::Number: { std::string unused; BeginNumberEdit(id,unused); break; }
	}
}
bool Interaction::AcknowledgeProposal(const std::string& id, std::uint64_t token, bool accepted) {
	const auto found = items.find(id);
	if (found == items.end() || !token || found->second.proposalToken != token || !found->second.pending) return false;
	auto& item = found->second;
	if (item.control.role == ControlRole::Number && item.number) {
		if (accepted && item.readback && item.readback->value == *item.pending) {
			std::string unused;
			if (!RebaseNumber(item,unused)) RetireNumber(id,item);
		} else {
			const auto revision = ProposalToken();
			if (!revision) return false;
			item.number->identity.revision = item.number->draftRevision = revision;
			// An acknowledgement alone cannot manufacture an accepted value.
			if (accepted) item.number->conflict = true;
		}
	}
	if (accepted) item.rejected.reset(); else item.rejected = item.pending;
	item.pending.reset(); item.proposalToken = 0; return true;
}
std::optional<WidgetViewState> Interaction::Widget(const std::string& id) const {
	const auto found = items.find(id);
	if (found == items.end() || found->second.control.role == ControlRole::Button || !found->second.readback) return {};
	const auto& item = found->second;
	WidgetViewState view;
	view.role = item.control.role; view.accepted = item.readback->value; view.mixed = item.readback->mixed;
	view.pending = item.pending; view.rejected = item.rejected; view.proposalToken = item.proposalToken;
	if (dragging == id && dragPreview) view.preview = *dragPreview;
	view.popupOpen = popup == id; if (view.popupOpen) view.highlight = highlight;
	view.firstVisible = item.firstVisible;
	if (item.number) {
		const auto& editor = *item.number;
		NumberEditView number;
		number.state = editor.buffer.State(); number.composition = editor.buffer.Composition(); number.identity = editor.identity;
		double parsed = 0;
		number.status = ParseTextNumber(number.state.text,NumberPolicy(item.control),parsed);
		number.dirty = number.state.text != editor.baselineText || number.composition.has_value();
		number.conflict = editor.conflict; number.canUndo = editor.buffer.CanUndo(); number.canRedo = editor.buffer.CanRedo();
		number.active = !editor.detached; number.notice = editor.notice;
		if (editor.native) {
			number.nativePresentation = editor.native->presentation;
			number.nativeUnsettled = NativeUnsettled(*editor.native);
			number.status = ParseTextNumber(number.nativePresentation->text,NumberPolicy(item.control),parsed);
			number.dirty = number.dirty || number.nativePresentation->text != editor.baselineText;
			number.canUndo = number.canRedo = false;
		}
		view.number = std::move(number);
	}
	return view;
}
bool Interaction::RebaseNumber(Item& item, std::string& error) {
	NumberEditor editor;
	editor.draftLifetime = editor.draftRevision = ProposalToken();
	if (!editor.draftLifetime) { error = "Number draft identity exhausted"; return false; }
	const auto& spec = std::get<NumberSpec>(item.control.widget);
	editor.baselineValue = std::get<double>(item.readback->value);
	editor.detached = !item.number || item.number->detached;
	if (!FormatTextNumber(editor.baselineValue,NumberPolicy(item.control),editor.baselineText,error) ||
		!editor.buffer.Reset(editor.baselineText,{spec.maxBytes,false,false},error) ||
		!editor.buffer.SetSelection(0,editor.baselineText.size(),error)) return false;
	if (!editor.detached) {
		editor.identity = {ProposalToken(),ProposalToken()};
		if (!editor.identity.session || !editor.identity.revision) { error = "Number edit identity exhausted"; return false; }
	}
	if (item.number && item.number->native) DropNumberNative(item.number->native->barrier.editor.control,item);
	item.number = std::move(editor); return true;
}
bool Interaction::BeginNumberEdit(const std::string& id, std::string& error) {
	error.clear(); const auto found = items.find(id);
	if (found == items.end() || found->second.control.role != ControlRole::Number || !ValidNumber(found->second.control) ||
		focusPending || focused != id || !Eligible(id) || !found->second.readback || found->second.pending) {
		error = "Number control is not available for editing"; return false;
	}
	auto& item = found->second;
	std::optional<NumberEditor> created;
	if (!item.number) {
		Item staged; staged.control = item.control; staged.readback = item.readback;
		if (!RebaseNumber(staged,error)) return false;
		created = std::move(staged.number);
	}
	auto& editor = item.number ? *item.number : *created;
	if (editor.detached) {
		const NumberEditIdentity identity{ProposalToken(),ProposalToken()};
		if (!identity.session || !identity.revision) { error = "Number edit identity exhausted"; return false; }
		editor.identity = identity; editor.draftRevision = identity.revision; editor.detached = false;
	}
	if (created) item.number = std::move(created);
	item.rejected.reset(); return true;
}
bool Interaction::ResolveNumberConflict(const std::string& id, NumberEditIdentity expected,
	bool keepDraft, std::string& error) {
	error.clear(); const auto found = items.find(id);
	if (found == items.end() || !found->second.number || !found->second.number->conflict ||
		!expected.session || !expected.revision || expected != found->second.number->identity ||
		found->second.number->detached || focused != id || focusPending || !Eligible(id) ||
		found->second.pending || !found->second.readback) {
		error = "Numeric conflict identity is stale or unavailable"; return false;
	}
	auto& item = found->second; NumberEditor candidate = *item.number;
	const auto& spec = std::get<NumberSpec>(item.control.widget);
	candidate.baselineValue = std::get<double>(item.readback->value);
	if (!FormatTextNumber(candidate.baselineValue,NumberPolicy(item.control),candidate.baselineText,error)) return false;
	TextEditBuffer baseline;
	if (!baseline.Reset(candidate.baselineText,{spec.maxBytes,false,false},error) ||
		!baseline.SetSelection(0,candidate.baselineText.size(),error)) return false;
	if (keepDraft) candidate.buffer.CancelComposition(); else candidate.buffer = std::move(baseline);
	const auto revision = ProposalToken(); if (!revision) { error = "Number edit identity exhausted"; return false; }
	candidate.identity.revision = candidate.draftRevision = revision; candidate.conflict = false; candidate.notice = NumberEditNotice::None;
	item.number = std::move(candidate); item.pending.reset(); item.rejected.reset(); item.proposalToken = 0;
	std::erase_if(actions,[&](const ControlAction& action) { return action.node == id && action.editSession != 0; });
	return true;
}
Interaction::Item* Interaction::EditableNumber(const std::string& id, NumberEditIdentity expected, std::string& error) {
	error.clear(); const auto found = items.find(id);
	if (found == items.end() || !found->second.number || !expected.session || !expected.revision ||
		expected != found->second.number->identity || found->second.number->detached || focused != id || focusPending || !Eligible(id) ||
		found->second.number->conflict || found->second.pending) {
		error = "Number edit identity is stale or unavailable"; return nullptr;
	}
	if (found->second.number->native) { error = "Retire the native Number binding before local editing"; return nullptr; }
	return &found->second;
}
bool Interaction::AttachNumberNative(const std::string& id, NumberEditIdentity expected,
	const TextEditorIdentity& owner, NativeTextIdentity native, NativeTextEditorBarrier& out, std::string& error) {
	if (candidate || !authority || nativeModel) { error = "Native Number attachment requires an unbound live Interaction"; return false; }
	auto* item = EditableNumber(id,expected,error); if (!item) return false;
	if (item->number->buffer.Composition() || owner.control != id || owner.session != expected.session ||
		owner.revision != expected.revision || owner.modal != modalToken) {
		error = "Native Number attachment does not match the complete active editor"; return false;
	}
	auto model = std::make_unique<NativeTextEditor>();
	if (!model->Open(native,owner,item->number->buffer,error)) return false;
	auto view = std::make_shared<const NativeTextEditorView>(model->View());
	NativeTextEditorBarrier receipt = view->barrier;
	std::string control = id;
	const auto revision = ProposalToken();
	if (!revision) { error = "Number native attachment identity exhausted"; return false; }
	item->number->native = std::move(view); item->number->draftRevision = revision;
	nativeControl = std::move(control); nativeModel = std::move(model);
	out = std::move(receipt); error.clear(); return true;
}
bool Interaction::IsNumberNativeCurrent(const NativeTextEditorBarrier& expected) const noexcept {
	const auto found = items.find(expected.editor.control);
	if (candidate || !authority || !nativeModel || nativeControl != expected.editor.control ||
		found == items.end() || !found->second.number || !found->second.number->native ||
		!nativeModel->Active() || found->second.number->native->barrier != expected ||
		found->second.number->identity != NumberEditIdentity{expected.editor.session,expected.editor.revision} ||
		found->second.number->detached || found->second.number->conflict || found->second.pending ||
		focused != nativeControl || focusPending || modalBlocked || !found->second.control.enabled ||
		!found->second.bounds.visible || !found->second.readback || expected.editor.modal != modalToken) return false;
	if (modals.empty()) return true;
	for (const std::string* current = &nativeControl; !current->empty();) {
		if (*current == modals.back().root) return true;
		const auto parent = parents.find(*current);
		if (parent == parents.end()) break;
		current = &parent->second;
	}
	return false;
}
Interaction::Item* Interaction::NativeNumber(const NativeTextEditorBarrier& expected, std::string& error) {
	error.clear();
	if (!IsNumberNativeCurrent(expected)) {
		error = "Native Number authority, editor or collection barrier is stale"; return nullptr;
	}
	return &items.find(expected.editor.control)->second;
}
bool Interaction::QueryNumberNative(const std::string& id, const TextEditorIdentity& expected,
	NativeTextEditorView& out, std::string& error) const {
	error.clear(); const auto found = items.find(id);
	if (candidate || !nativeModel || nativeControl != id || id != expected.control ||
		found == items.end() || !found->second.number || !found->second.number->native ||
		found->second.number->native->barrier.editor != expected ||
		found->second.number->identity != NumberEditIdentity{expected.session,expected.revision} ||
		found->second.number->detached || found->second.number->conflict || found->second.pending ||
		focused != id || focusPending || !Eligible(id) || expected.modal != modalToken ||
		nativeModel->Barrier() != found->second.number->native->barrier) {
		error = "Native Number query has no matching live editor"; return false;
	}
	NativeTextEditorView result = *found->second.number->native;
	out = std::move(result); return true;
}
bool Interaction::PublishNumberNative(const NativeTextEditorBarrier& expected,
	std::unique_ptr<NativeTextEditor> prepared, std::shared_ptr<const NativeTextEditorView> view,
	NumberEditor&& editor, std::string& error) {
	auto* item = NativeNumber(expected,error); if (!item) return false;
	if (!prepared || !view || prepared->Barrier() != view->barrier ||
		editor.identity != NumberEditIdentity{view->barrier.editor.session,view->barrier.editor.revision} ||
		!nativeModel->Swap(expected,*prepared)) {
		error = "Native Number candidate does not originate at this exact publication"; return false;
	}
	static_assert(std::is_nothrow_move_assignable_v<NumberEditor>);
	item->number = std::move(editor);
	if (!view->active) { nativeModel.reset(); nativeControl.clear(); }
	return true;
}
bool Interaction::NativeCollection(bool begin, const NativeTextEditorBarrier& expected,
	const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error) {
	auto* item = NativeNumber(expected,error); if (!item) return false;
	auto prepared = nativeModel->Clone(); NativeTextEditorBarrier receipt;
	if (!(begin ? prepared->BeginCollection(expected,collection,receipt,error) :
		prepared->CompleteCollection(expected,collection,receipt,error))) return false;
	auto view = std::make_shared<const NativeTextEditorView>(prepared->View());
	NumberEditor editor = *item->number; editor.native = view;
	const auto revision = ProposalToken();
	if (!revision) { error = "Number native collection identity exhausted"; return false; }
	// Protocol changes invalidate draft/candidate guards without manufacturing
	// native acknowledgement or changing the current engine editing revision.
	editor.draftRevision = revision;
	if (!PublishNumberNative(expected,std::move(prepared),std::move(view),std::move(editor),error)) return false;
	out = std::move(receipt); error.clear(); return true;
}
bool Interaction::BeginNumberNativeCollection(const NativeTextEditorBarrier& expected,
	const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error) {
	return NativeCollection(true,expected,collection,out,error);
}
bool Interaction::CompleteNumberNativeCollection(const NativeTextEditorBarrier& expected,
	const NativeTextCollection& collection, NativeTextEditorBarrier& out, std::string& error) {
	return NativeCollection(false,expected,collection,out,error);
}
bool Interaction::ApplyNumberNative(const NativeTextEditorBarrier& expected, const NativeTextOffer& offer,
	NativeTextEditorReceipt& out, std::string& error) {
	auto* item = NativeNumber(expected,error); if (!item) return false;
	if (!expected.collectionOpen) { error = "Native Number offer requires a verified open collection"; return false; }
	const auto revision = ProposalToken();
	if (!revision) { error = "Number native revision exhausted"; return false; }
	auto prepared = nativeModel->Clone(); NativeTextEditorReceipt receipt;
	if (!prepared->Apply(offer,expected.editor,revision,receipt,error)) return false;
	auto view = std::make_shared<const NativeTextEditorView>(prepared->View());
	NumberEditor editor = *item->number;
	// Collection Apply changes only native presentation; stable local history is
	// deliberately untouched until explicit complete-collection settlement.
	editor.native = view; editor.identity.revision = editor.draftRevision = revision;
	editor.notice = NumberEditNotice::None;
	if (!PublishNumberNative(expected,std::move(prepared),std::move(view),std::move(editor),error)) return false;
	out = std::move(receipt); error.clear(); return true;
}
bool Interaction::NativeCompletion(bool retire, const NativeTextEditorBarrier& expected,
	NativeTextEditorReceipt& out, std::string& error) {
	auto* item = NativeNumber(expected,error); if (!item) return false;
	const auto revision = ProposalToken();
	if (!revision) { error = "Number native completion identity exhausted"; return false; }
	auto prepared = nativeModel->Clone(); NativeTextEditorReceipt receipt;
	if (!(retire ? prepared->Retire(expected,revision,receipt,error) : prepared->SettleComposition(expected,revision,receipt,error))) return false;
	auto view = std::make_shared<const NativeTextEditorView>(prepared->View());
	NumberEditor editor = *item->number;
	if (!retire && !editor.buffer.RestoreHistory(view->draft,view->history,view->policy,error)) return false;
	editor.native = retire ? nullptr : view;
	editor.identity.revision = editor.draftRevision = revision; editor.notice = NumberEditNotice::None;
	if (!PublishNumberNative(expected,std::move(prepared),std::move(view),std::move(editor),error)) return false;
	out = std::move(receipt); error.clear(); return true;
}
bool Interaction::SettleNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
	return NativeCompletion(false,expected,out,error);
}
struct Interaction::NativeSettlement::Impl {
	std::uint64_t authority = 0;
	std::shared_ptr<const NativeTextEditorView> origin;
	std::shared_ptr<const NativeTextEditorView> view;
	std::unique_ptr<NativeTextEditor> model;
	NumberEditor editor;
	NativeTextEditorReceipt receipt;
	NativeTextEditorReceipt outputReceipt;
};
Interaction::NativeSettlement::NativeSettlement() : impl(std::make_unique<Impl>()) {}
Interaction::NativeSettlement::~NativeSettlement() = default;
const NativeTextEditorReceipt& Interaction::NativeSettlement::Receipt() const { return impl->receipt; }
const NativeTextSnapshot& Interaction::NativeSettlement::Presentation() const { return impl->view->presentation; }
std::unique_ptr<Interaction::NativeSettlement> Interaction::PrepareNumberNativeSettlement(
	const NativeTextEditorBarrier& expected, std::string& error) {
	auto* item = NativeNumber(expected,error); if (!item) return {};
	auto staged = std::unique_ptr<NativeSettlement>(new NativeSettlement());
	auto& data = *staged->impl;
	data.authority = authority; data.origin = item->number->native;
	const auto revision = ProposalToken();
	if (!revision) { error = "Number native settlement identity exhausted"; return {}; }
	data.model = nativeModel->Clone();
	if (!data.model->SettleComposition(expected,revision,data.receipt,error)) return {};
	auto view = std::make_shared<const NativeTextEditorView>(data.model->View());
	data.view = view; data.outputReceipt = data.receipt;
	data.editor = *item->number;
	if (!data.editor.buffer.RestoreHistory(view->draft,view->history,view->policy,error)) return {};
	data.editor.native = std::move(view);
	data.editor.identity.revision = data.editor.draftRevision = revision;
	data.editor.notice = NumberEditNotice::None;
	error.clear(); return staged;
}
bool Interaction::PublishNumberNativeSettlement(NativeSettlement& staged, NativeTextEditorReceipt& out) noexcept {
	auto& data = *staged.impl;
	const auto& before = data.receipt.before;
	const auto found = items.find(before.editor.control);
	if (!IsNumberNativeCurrent(before) || authority != data.authority || !data.model ||
		found->second.number->native != data.origin ||
		!nativeModel->Swap(before,*data.model)) return false;
	static_assert(std::is_nothrow_move_assignable_v<NumberEditor>);
	static_assert(std::is_nothrow_move_assignable_v<NativeTextEditorReceipt>);
	found->second.number = std::move(data.editor);
	out = std::move(data.outputReceipt); data.authority = 0; data.model.reset(); return true;
}
bool Interaction::RetireNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
	return NativeCompletion(true,expected,out,error);
}
bool Interaction::RetireNumberNativeExact(NativeTextIdentity native, const TextEditorIdentity& owner) noexcept {
	const auto found = items.find(owner.control);
	if (candidate || !authority || !nativeModel || nativeControl != owner.control || found == items.end() ||
		!found->second.number || !found->second.number->native) return false;
	const auto& before = found->second.number->native->barrier;
	const auto& current = before.editor;
	if (before.native != native || current.allocation != owner.allocation || current.backend != owner.backend ||
		current.document != owner.document || current.modal != owner.modal || current.window != owner.window ||
		current.session != owner.session || current.control != owner.control) return false;
	DropNumberNative(owner.control,found->second);
	auto& editor = *found->second.number;
	editor.identity.revision = editor.draftRevision = ProposalToken();
	if (!editor.identity.revision) editor.detached = true;
	return true;
}
void Interaction::DropNumberNative(const std::string& id, Item& item) {
	const bool owns = nativeControl == id; // id may refer to the immutable view.
	if (item.number) item.number->native.reset();
	if (owns) { nativeModel.reset(); nativeControl.clear(); }
}
bool Interaction::ChangeNumber(const std::string& id, NumberEditIdentity expected,
	const std::function<bool(TextEditBuffer&,std::string&)>& change, std::string& error) {
	auto* item = EditableNumber(id,expected,error); if (!item) return false;
	TextEditBuffer candidate = item->number->buffer;
	if (!change(candidate,error)) return false;
	const auto revision = ProposalToken(); if (!revision) { error = "Number edit identity exhausted"; return false; }
	item->number->buffer = std::move(candidate); item->number->identity.revision = item->number->draftRevision = revision;
	item->number->notice = NumberEditNotice::None;
	item->rejected.reset(); return true;
}
bool Interaction::SetNumberSelection(const std::string& id, NumberEditIdentity expected,
	std::size_t anchor, std::size_t caret, std::string& error) {
	return ChangeNumber(id,expected,[&](TextEditBuffer& buffer,std::string& e) { return buffer.SetSelection(anchor,caret,e); },error);
}
bool Interaction::ApplyNumberInput(const std::string& id, NumberEditIdentity expected, const TextInputEvent& event, std::string& error) {
	return ChangeNumber(id,expected,[&](TextEditBuffer& buffer,std::string& e) { return buffer.Apply(event,e); },error);
}
bool Interaction::SetNumberNotice(const std::string& id, NumberEditIdentity expected, NumberEditNotice notice, std::string& error) {
	auto* item = EditableNumber(id,expected,error); if (!item) return false;
	if (notice < NumberEditNotice::None || notice > NumberEditNotice::ClipboardRejected || item->number->buffer.Composition()) {
		error = "Number clipboard notice is invalid or composing"; return false;
	}
	item->number->notice = notice; return true;
}
bool Interaction::ReplaceNumberSelection(const std::string& id, NumberEditIdentity expected, std::string_view text, std::string& error) {
	return ChangeNumber(id,expected,[&](TextEditBuffer& buffer,std::string& e) { return buffer.ReplaceSelection(text,e); },error);
}
bool Interaction::UndoNumberEdit(const std::string& id, NumberEditIdentity expected, bool redo, std::string& error) {
	return ChangeNumber(id,expected,[&](TextEditBuffer& buffer,std::string& e) { return redo ? buffer.Redo(e) : buffer.Undo(e); },error);
}
bool Interaction::ApplyNumberOperation(const std::string& id, NumberEditIdentity expected,
	const TextEditOperation& operation, std::string& error) {
	auto* item = EditableNumber(id,expected,error); if (!item) return false;
	if (item->number->buffer.Composition()) { error = "Number command is unavailable during composition"; return false; }
	if (!operation.replacement.empty()) { error = "Number commands cannot insert replacement text"; return false; }
	const auto& before = item->number->buffer.State();
	TextEditBuffer candidate = item->number->buffer;
	switch (operation.kind) {
		case TextEditOperation::Kind::Selection:
			if (!candidate.SetSelection(operation.anchor,operation.caret,error)) return false;
			break;
		case TextEditOperation::Kind::Replace:
			if (operation.anchor >= operation.caret) { error = "Number command has an invalid deletion range"; return false; }
			if (!candidate.ReplaceRange(operation.anchor,operation.caret,operation.replacement,error)) return false;
			break;
		default: error = "Unknown number command operation"; return false;
	}
	const auto& after = candidate.State();
	const bool changed = before.text != after.text || before.anchor != after.anchor || before.caret != after.caret;
	if (changed != operation.changed) { error = "Number command change flag does not match its operation"; return false; }
	if (!changed) { item->number->notice = NumberEditNotice::None; return true; }
	const auto revision = ProposalToken(); if (!revision) { error = "Number edit identity exhausted"; return false; }
	item->number->buffer = std::move(candidate); item->number->identity.revision = item->number->draftRevision = revision;
	item->number->notice = NumberEditNotice::None;
	item->rejected.reset(); return true;
}
bool Interaction::CommitNumberEdit(const std::string& id, NumberEditIdentity expected, std::string& error) {
	auto* item = EditableNumber(id,expected,error); if (!item) return false;
	double value = 0;
	if (item->number->buffer.Composition() || ParseTextNumber(item->number->buffer.State().text,NumberPolicy(item->control),value) != TextNumberStatus::Valid) {
		error = "Number text is incomplete, invalid, out of range or composing"; return false;
	}
	if (!modalToken || !Propose(id,value)) { error = "Number proposal could not be queued"; return false; }
	item->number->notice = NumberEditNotice::None;
	actions.back().editSession = expected.session; actions.back().editRevision = expected.revision; return true;
}
void Interaction::RetireNumber(const std::string& id, Item& item) {
	if (item.control.role != ControlRole::Number) return;
	if (item.number && item.number->native) {
		// The first cancel abandons transient native presentation, preserving the
		// stable local edit. A later explicit local cancel may discard that draft.
		const auto revision = ProposalToken();
		DropNumberNative(id,item); item.number->draftRevision = revision;
		if (!item.number->detached) item.number->identity.revision = revision;
		return;
	}
	// Inventory epochs prevent an empty -> editor -> empty ABA from authorizing
	// a previously captured discard. Exhaustion fails all future guard queries.
	if (item.number) numberEpoch = ProposalToken();
	item.number.reset(); item.pending.reset(); item.rejected.reset(); item.proposalToken = 0;
	std::erase_if(actions,[&](const ControlAction& action) { return action.node == id && action.editSession != 0; });
}
void Interaction::DetachNumber(const std::string& id, Item& item) {
	if (!item.number) return;
	DropNumberNative(id,item);
	if (!item.number->detached || item.number->buffer.Composition() || item.pending || item.proposalToken)
		item.number->draftRevision = ProposalToken();
	item.number->buffer.CancelComposition(); item.number->identity = {}; item.number->detached = true;
	item.number->notice = NumberEditNotice::None;
	item.pending.reset(); item.rejected.reset(); item.proposalToken = 0;
	std::erase_if(actions,[&](const ControlAction& action) { return action.node == id && action.editSession != 0; });
}
bool Interaction::CancelNumberEdit(const std::string& id, NumberEditIdentity expected) {
	const auto found = items.find(id);
	if (found == items.end() || !found->second.number ||
		((expected.session || expected.revision) && expected != found->second.number->identity)) return false;
	RetireNumber(id,found->second); return true;
}
bool Interaction::QueryNumberDrafts(NumberDraftSummary& out, std::string& error) const {
	error.clear();
	if (!numberEpoch) { error = "Number draft barrier is unavailable"; return false; }
	NumberDraftSummary candidate; candidate.barrier.instance = numberEpoch;
	for (const auto& id : order) {
		const auto& item = items.at(id);
		if (!item.number) continue;
		const auto& editor = *item.number;
		if (!editor.draftLifetime || !editor.draftRevision || candidate.barrier.editors.size() >= ValueWidgetSnapshot::MaxNumberEditors) {
			error = "Number draft barrier exceeds its identity or count budget"; return false;
		}
		candidate.barrier.editors.push_back({id,editor.draftLifetime,editor.draftRevision});
		const bool nativeUnsettled = editor.native && NativeUnsettled(*editor.native);
		const bool composing = editor.buffer.Composition().has_value() || (editor.native && !editor.native->presentation.compositions.empty());
		const auto& text = editor.native ? editor.native->presentation.text : editor.buffer.State().text;
		const bool dirty = editor.buffer.State().text != editor.baselineText || text != editor.baselineText || composing;
		if (!dirty && !editor.conflict && !item.pending && !composing && !nativeUnsettled) continue;
		double value = 0;
		candidate.blocking.push_back({id,ParseTextNumber(text,NumberPolicy(item.control),value),
			dirty,editor.conflict,item.pending.has_value(),composing,!editor.detached,nativeUnsettled});
	}
	out = std::move(candidate); return true;
}
bool Interaction::DiscardNumberDrafts(const NumberDraftBarrier& expected, std::string& error) {
	NumberDraftSummary current;
	if (!QueryNumberDrafts(current,error)) return false;
	if (expected != current.barrier) { error = "Number draft barrier is stale"; return false; }
	const auto epoch = ProposalToken();
	if (!epoch) { error = "Number draft identity exhausted"; return false; }
	// All fallible validation precedes deletion. One new epoch invalidates even
	// an empty accepted inventory, without introducing per-field partial failure.
	for (auto& [id,item] : items) if (item.number) {
		item.number.reset(); item.pending.reset(); item.rejected.reset(); item.proposalToken = 0;
	}
	nativeModel.reset(); nativeControl.clear();
	std::erase_if(actions,[](const ControlAction& action) { return action.editSession != 0; });
	numberEpoch = epoch; return true;
}
bool Interaction::FocusNumberDraft(const NumberDraftBarrier& expected, const std::string& control, std::string& error) {
	NumberDraftSummary current;
	if (!QueryNumberDrafts(current,error)) return false;
	if (expected != current.barrier) { error = "Number draft barrier is stale"; return false; }
	if (std::none_of(current.blocking.begin(),current.blocking.end(),[&](const NumberDraftStatus& status) { return status.control == control; })) {
		error = "Number control has no blocking local draft"; return false;
	}
	if (items.at(control).number && items.at(control).number->native) {
		if (focused == control && !focusPending && Eligible(control)) return true;
		error = "Retire the native Number binding before changing its focus"; return false;
	}
	// Focus can detach another editor. Stage the entire semantic change so a
	// disabled/modal-ineligible/pending target or identity failure changes none.
	Interaction candidate = *this;
	if (!candidate.Focus(control)) { error = "Number draft is not eligible for focus"; return false; }
	if (!candidate.BeginNumberEdit(control,error)) return false;
	return Adopt(std::move(candidate),error);
}
double Interaction::SliderValue(const SliderSpec& spec, double fraction) const {
	fraction = std::clamp(fraction,0.0,1.0);
	if (fraction == 1) return spec.maximum;
	const long double ticks = std::round(static_cast<long double>(fraction)*(spec.maximum-spec.minimum)/spec.step);
	return SliderTickValue(spec,ticks);
}
void Interaction::SliderKey(MenuInput input) {
	auto& item = items.at(focused); const auto& spec = std::get<SliderSpec>(item.control.widget);
	armed.clear();
	double value = std::clamp(std::get<double>(EditingValue(item)),spec.minimum,spec.maximum);
	if (input == MenuInput::Home) value = spec.minimum;
	else if (input == MenuInput::End) value = spec.maximum;
	else {
		const bool increase = input == MenuInput::Right || input == MenuInput::Up || input == MenuInput::PageUp;
		const auto ticks = (static_cast<long double>(value)-spec.minimum)/spec.step;
		// Recognize the canonical tick even when subtracting a large minimum
		// loses relative precision. Keep the existing tiny round-trip tolerance;
		// off-grid/custom values move to the next tick in the requested direction.
		const auto near = std::round(ticks);
		const auto anchored = value == SliderTickValue(spec,near) || std::abs(ticks-near) <= 1e-9L ? near : ticks;
		const int count = input == MenuInput::PageUp || input == MenuInput::PageDown ? 10 : 1;
		const auto next = increase ? std::floor(anchored)+count : std::ceil(anchored)-count;
		value = SliderTickValue(spec,next);
	}
	if (value != std::get<double>(EditingValue(item))) Propose(focused,value);
}
bool Interaction::OptionEligible(const Item& item, size_t index) const {
	return item.readback && index < item.readback->enabledOptions.size() && item.readback->enabledOptions[index];
}
void Interaction::OpenPopup(const std::string& id) {
	CancelGesture(); popup = id; focused = id;
	const auto& item = items.at(id); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
	for (size_t i = 0; i < options.size(); ++i) if (OptionEligible(item,i)) {
		if (highlight.empty()) highlight = options[i].id;
		if (options[i].value == EditingValue(item)) { highlight = options[i].id; break; }
	}
	KeepHighlightVisible();
}
void Interaction::KeepHighlightVisible() {
	if (popup.empty()) return;
	auto& item = items.at(popup); const auto& spec = std::get<ChoiceSpec>(item.control.widget);
	const auto rows = std::min<size_t>(spec.visibleRows,spec.options.size());
	item.firstVisible = std::min<unsigned>(item.firstVisible,static_cast<unsigned>(spec.options.size()-rows));
	for (size_t i = 0; i < spec.options.size(); ++i) if (spec.options[i].id == highlight) {
		if (i < item.firstVisible) item.firstVisible = static_cast<unsigned>(i);
		else if (i >= item.firstVisible+rows) item.firstVisible = static_cast<unsigned>(i-rows+1);
		break;
	}
}
void Interaction::PopupNavigate(MenuInput input) {
	if (input != MenuInput::Up && input != MenuInput::Down && input != MenuInput::Home && input != MenuInput::End && input != MenuInput::PageUp && input != MenuInput::PageDown) return;
	armed.clear(); armedOption.clear(); popupAcceptArm = false;
	const auto& item = items.at(popup); const auto& spec = std::get<ChoiceSpec>(item.control.widget);
	std::vector<size_t> eligible;
	size_t current = 0;
	for (size_t i = 0; i < spec.options.size(); ++i) if (OptionEligible(item,i)) {
		if (spec.options[i].id == highlight) current = eligible.size();
		eligible.push_back(i);
	}
	if (eligible.empty()) { highlight.clear(); return; }
	if (input == MenuInput::Home) current = 0;
	else if (input == MenuInput::End) current = eligible.size()-1;
	else {
		const size_t amount = input == MenuInput::PageUp || input == MenuInput::PageDown ? spec.visibleRows : 1;
		if (input == MenuInput::Up || input == MenuInput::PageUp) current = current > amount ? current-amount : 0;
		else current = std::min(current+amount,eligible.size()-1);
	}
	highlight = spec.options[eligible[current]].id; KeepHighlightVisible();
}
ValueWidgetSnapshot Interaction::CaptureWidgets() const {
	ValueWidgetSnapshot result; std::string error;
	if (!CaptureWidgets(result,error)) throw std::runtime_error(error);
	return result;
}
bool Interaction::CaptureWidgets(ValueWidgetSnapshot& out, std::string& error) const {
	error.clear(); std::size_t count = 0, bytes = 0;
	for (const auto& [id,item] : items) if (item.number) {
		if (++count > ValueWidgetSnapshot::MaxNumberEditors) { error = "Too many saved numeric drafts"; return false; }
		for (const auto size : {item.number->buffer.State().text.size(),item.number->baselineText.size(),item.number->buffer.HistoryTextBytes()}) {
			if (size > ValueWidgetSnapshot::MaxNumberTextBytes-bytes) { error = "Saved numeric drafts exceed the text budget"; return false; }
			bytes += size;
		}
	}
	ValueWidgetSnapshot result;
	for (const auto& [id,item] : items) if (item.control.role != ControlRole::Button) {
		ValueWidgetSnapshot::Widget widget{item.control.role,item.firstVisible,{}};
		if (item.number) {
			const auto& editor = *item.number; auto history = editor.buffer.CaptureHistory();
			widget.number = NumberEditorSnapshot{editor.buffer.State(),std::move(history.undo),std::move(history.redo),
				editor.baselineValue,editor.baselineText,editor.conflict};
		}
		result.widgets.emplace(id,std::move(widget));
	}
	out = std::move(result); return true;
}
bool Interaction::RestoreWidgets(const ValueWidgetSnapshot& snapshot, std::string& error) {
	error.clear();
	std::size_t expected = 0, count = 0, bytes = 0;
	for (const auto& [id,item] : items) if (item.control.role != ControlRole::Button) ++expected;
	if ((snapshot.version != 1 && snapshot.version != 2) || snapshot.widgets.size() != expected) { error = "Invalid widget snapshot version or count"; return false; }
	// Preflight aggregate budgets before allocating candidate editors/history.
	for (const auto& [id,widget] : snapshot.widgets) if (widget.number) {
		if (snapshot.version != 2 || widget.role != ControlRole::Number || ++count > ValueWidgetSnapshot::MaxNumberEditors) {
			error = "Invalid restored numeric draft count or version"; return false;
		}
		const auto& editor = *widget.number;
		if (editor.undo.size() > TextEditBuffer::MaxHistoryEntries || editor.redo.size() > TextEditBuffer::MaxHistoryEntries-editor.undo.size()) {
			error = "Restored numeric history exceeds the entry limit"; return false;
		}
		auto add = [&](std::size_t size) {
			if (size > ValueWidgetSnapshot::MaxNumberTextBytes-bytes) { error = "Restored numeric drafts exceed the text budget"; return false; }
			bytes += size; return true;
		};
		if (!add(editor.state.text.size()) || !add(editor.baselineText.size())) return false;
		for (const auto* history : {&editor.undo,&editor.redo}) for (const auto& entry : *history) if (!add(entry.text.size())) return false;
	}
	std::map<std::string,NumberEditor> editors;
	for (const auto& [id,value] : snapshot.widgets) {
		const auto found = items.find(id);
		if (found == items.end() || value.role == ControlRole::Button || value.role != found->second.control.role) { error = "Restored widget role does not match"; return false; }
		unsigned maximum = 0;
		if (value.role == ControlRole::Choice) {
			const auto& spec = std::get<ChoiceSpec>(found->second.control.widget);
			maximum = static_cast<unsigned>(spec.options.size()-std::min<size_t>(spec.visibleRows,spec.options.size()));
		}
		if (value.firstVisible > maximum) { error = "Restored widget scroll is outside its range"; return false; }
		if (value.number) {
			const auto& item = found->second; const auto& saved = *value.number;
			if (!ValidNumber(item.control) || !item.readback || !std::holds_alternative<double>(item.readback->value) ||
				!ValidStateValue(saved.baselineValue)) { error = "Invalid restored numeric source or baseline"; return false; }
			const auto& spec = std::get<NumberSpec>(item.control.widget);
			std::string canonical;
			if (!FormatTextNumber(saved.baselineValue,NumberPolicy(item.control),canonical,error)) return false;
			if (canonical != saved.baselineText) { error = "Restored numeric baseline is not canonical for its source policy"; return false; }
			TextEditBuffer baseline;
			if (!baseline.Reset(saved.baselineText,{spec.maxBytes,false,false},error)) return false;
			NumberEditor editor;
			editor.draftLifetime = editor.draftRevision = ProposalToken();
			if (!editor.draftLifetime) { error = "Number draft identity exhausted"; return false; }
			if (!editor.buffer.RestoreHistory(saved.state,{saved.undo,saved.redo},{spec.maxBytes,false,false},error)) return false;
			editor.baselineText = saved.baselineText; editor.baselineValue = saved.baselineValue; editor.conflict = saved.conflict;
			if (std::get<double>(item.readback->value) != saved.baselineValue) {
				if (saved.state.text != saved.baselineText || saved.conflict) editor.conflict = true;
				else {
					editor.baselineValue = std::get<double>(item.readback->value);
					if (!FormatTextNumber(editor.baselineValue,NumberPolicy(item.control),editor.baselineText,error) ||
						!editor.buffer.Reset(editor.baselineText,{spec.maxBytes,false,false},error) ||
						!editor.buffer.SetSelection(0,editor.baselineText.size(),error)) return false;
				}
			}
			editors.emplace(id,std::move(editor));
		}
	}
	Interaction candidate = *this;
	candidate.numberEpoch = ProposalToken();
	if (!candidate.numberEpoch) { error = "Number draft identity exhausted"; return false; }
	candidate.CancelGesture(); candidate.hovered.clear(); candidate.pointerOption.clear(); candidate.pointerFraction.reset();
	candidate.actions.clear(); candidate.feedback.clear(); candidate.overflowed = false;
	for (auto& [id,item] : candidate.items) {
		item.pending.reset(); item.rejected.reset(); item.proposalToken = 0; item.number.reset();
		if (item.control.role != ControlRole::Button) item.firstVisible = snapshot.widgets.at(id).firstVisible;
		if (auto editor = editors.find(id); editor != editors.end()) item.number = std::move(editor->second);
		// Widget restoration changes durable local values, not the persistent
		// focus/modal semantics restored just before it. The receiving instance
		// may not have a first layout yet; do not consult stale bounds here.
		const auto state = !item.control.enabled ? ControlState::Disabled :
			candidate.focused == id ? ControlState::Focus : ControlState::Default;
		if (!item.known || item.state != state) {
			item.known = true; item.state = state;
			candidate.feedback.push_back({id,item.control.states.at(state),state});
		}
	}
	return Adopt(std::move(candidate),error);
}
bool Interaction::Within(const std::string& id, const std::string& root) const {
	if (root.empty()) return true;
	for (std::string current = id; !current.empty();) {
		if (current == root) return true;
		const auto parent = parents.find(current);
		if (parent == parents.end()) break;
		current = parent->second;
	}
	return false;
}
bool Interaction::Eligible(const std::string& id) const {
	const auto found = items.find(id);
	return !modalBlocked && found != items.end() && found->second.control.enabled && found->second.bounds.visible && Within(id,Modal()) &&
		(found->second.control.role == ControlRole::Button || found->second.readback.has_value());
}
std::string Interaction::First() const { for (const auto& id : order) if (Eligible(id)) return id; return {}; }
void Interaction::SetBounds(const std::map<std::string,ControlBounds>& bounds, bool freshLayout) {
	for (auto& [id,item] : items) {
		const auto found = bounds.find(id); item.bounds = found == bounds.end() ? ControlBounds{} : found->second;
		const auto& b = item.bounds;
		item.bounds.visible = b.visible && std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.width) && std::isfinite(b.height) && b.width > 0 && b.height > 0;
	}
	if (freshLayout && focusPending) ResolvePendingFocus();
	if (!focusPending && !modals.empty() && !Eligible(focused)) focused = First();
	Refresh();
}
bool Interaction::SetEnabled(const std::string& id, bool enabled) {
	auto found = items.find(id); if (found == items.end()) return false;
	found->second.control.enabled = enabled;
	if (!focusPending && !modals.empty() && !Eligible(focused)) focused = First();
	Refresh(); return true;
}
void Interaction::InvalidateLayout() {
	if (!focusPending && !focused.empty()) { pendingFocus = focused; focusPending = true; }
	ModalChanged(); SetBounds({},false);
}
bool Interaction::Focus(const std::string& id) {
	if (focusPending && !id.empty()) return false;
	if (!id.empty() && !Eligible(id)) return false;
	focusPending = false; pendingFocus.clear();
	if (focused != id) { CancelGesture(); focused = id; }
	Refresh(); return true;
}
void Interaction::Hover(const std::string& id) { PointerPart(id); }
void Interaction::PointerPart(const std::string& id, std::optional<double> fraction, const std::string& option) {
	hovered = !focusPending && Eligible(id) ? id : std::string{};
	pointerFraction = fraction && std::isfinite(*fraction) ? fraction : std::nullopt;
	pointerOption = option;
	if (!dragging.empty() && dragging == id) {
		if (pointerFraction) dragPreview = SliderValue(std::get<SliderSpec>(items.at(dragging).control.widget),*pointerFraction);
		else CancelGesture(); // A lost/singular projection cannot commit a stale preview.
	}
	if (!popup.empty() && hovered == popup && !option.empty()) {
		const auto& item = items.at(popup); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
		for (size_t i = 0; i < options.size(); ++i) if (options[i].id == option && OptionEligible(item,i)) {
			highlight = option; KeepHighlightVisible(); break;
		}
	}
	Refresh();
}
void Interaction::Pointer(bool down) {
	if (down) {
		if (pointerHeld) return;
		pointerHeld = true;
		if (!popup.empty()) {
			if (hovered != popup) { CancelGesture(); Refresh(); return; }
			if (!armed.empty()) return; // The matching keyboard release owns this arm.
			const auto& item = items.at(popup); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
			if (pointerOption.empty()) { armed = popup; pointerArm = true; armedOption.clear(); }
			else for (size_t i = 0; i < options.size(); ++i) if (options[i].id == pointerOption && OptionEligible(item,i)) {
				armed = popup; pointerArm = true; armedOption = pointerOption; break;
			}
		} else if (Eligible(hovered) && armed.empty()) {
			const auto id = hovered; Focus(id); armed = id; pointerArm = true;
			if (items.at(id).control.role == ControlRole::Slider) {
				if (pointerFraction) { dragging = id; dragPreview = SliderValue(std::get<SliderSpec>(items.at(id).control.widget),*pointerFraction); }
				else armed.clear();
			}
		}
	} else {
		if (!pointerHeld) return;
		pointerHeld = false;
		if (!dragging.empty()) {
			const auto id = dragging; const auto value = dragPreview;
			CancelGesture(); if (value && Eligible(id)) Propose(id,*value);
		} else if (pointerArm && !armed.empty()) {
			const auto id = armed; const auto option = armedOption; armed.clear(); armedOption.clear();
			if (id == hovered && Eligible(id)) {
				if (!popup.empty()) {
					if (option.empty() && pointerOption.empty()) CancelGesture();
					else if (!option.empty() && option == pointerOption) {
						const auto& item = items.at(id); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
						for (size_t i = 0; i < options.size(); ++i) if (options[i].id == option && OptionEligible(item,i)) {
							const auto value = options[i].value; CancelGesture(); Propose(id,value); break;
						}
					}
				} else Activate(id);
			}
		}
	}
	Refresh();
}
void Interaction::Input(MenuInput input, bool down) {
	if (input != MenuInput::Accept && input != MenuInput::Back) {
		if (!down) { heldNavigation.erase(input); blockedNavigation.erase(input); return; }
		heldNavigation.insert(input);
		if (blockedNavigation.contains(input) || modalBlocked || focusPending) return;
	}
	if (input == MenuInput::Accept) {
		if (down) {
			if (acceptHeld) return;
			acceptHeld = true;
			if (Eligible(focused) && armed.empty()) { armed = focused; pointerArm = false; popupAcceptArm = !popup.empty(); armedOption = highlight; }
		} else {
			if (!acceptHeld) return;
			acceptHeld = false;
			if (!pointerArm && !armed.empty()) {
				const auto id = armed; const auto option = armedOption; const bool choosing = popupAcceptArm;
				armed.clear(); armedOption.clear(); popupAcceptArm = false;
				if (id == focused && Eligible(id)) {
					if (choosing && popup == id && option == highlight) {
						const auto& item = items.at(id); const auto& options = std::get<ChoiceSpec>(item.control.widget).options;
						for (size_t i = 0; i < options.size(); ++i) if (options[i].id == option && OptionEligible(item,i)) {
							const auto value = options[i].value; CancelGesture(); Propose(id,value); break;
						}
					} else if (!choosing) Activate(id);
				}
			}
		}
	} else if (input == MenuInput::Back) {
		if (down && !backHeld) {
			if (!popup.empty() || !dragging.empty()) CancelGesture();
			else if (items.contains(focused) && items.at(focused).number) {
				auto& item = items.at(focused);
				if (item.number->buffer.Composition()) {
					item.number->buffer.CancelComposition(); item.number->identity.revision = item.number->draftRevision = ProposalToken();
				} else RetireNumber(focused,item);
			}
			else if (!modalBlocked) {
				armed.clear();
				const bool authored = !modals.empty() && modals.back().authored;
				const auto event = authored ? authoredModals.at(Modal()).backEvent : std::string{};
				if (!authored || !event.empty()) Queue({ControlAction::Kind::Back,document,Modal(),{},event,{},0,modalToken});
			}
		}
		backHeld = down;
	} else if (down) {
		if (!popup.empty()) PopupNavigate(input);
		else if (!dragging.empty()) {} // A captured pointer owns its gesture.
		else if (Eligible(focused) && items.at(focused).control.role == ControlRole::Slider &&
			(input == MenuInput::Home || input == MenuInput::End || input == MenuInput::PageUp || input == MenuInput::PageDown ||
			(std::get<SliderSpec>(items.at(focused).control.widget).vertical ? input == MenuInput::Up || input == MenuInput::Down : input == MenuInput::Left || input == MenuInput::Right))) SliderKey(input);
		else Navigate(input);
	}
	Refresh();
}
void Interaction::CancelGesture() {
	armed.clear(); dragging.clear(); dragPreview.reset(); popup.clear(); highlight.clear(); armedOption.clear(); popupAcceptArm = false;
}
void Interaction::Cancel() {
	CancelGesture(); hovered.clear(); pointerOption.clear(); pointerFraction.reset();
	for (auto& [id,item] : items) DetachNumber(id,item);
	Refresh();
}
void Interaction::ReleaseInputSources() { pointerHeld = acceptHeld = backHeld = false; heldNavigation.clear(); blockedNavigation.clear(); }
void Interaction::NavigationPulse(MenuInput input) {
	if (input == MenuInput::Accept || input == MenuInput::Back) return;
	const bool held = heldNavigation.erase(input) != 0;
	const bool blocked = blockedNavigation.erase(input) != 0;
	Input(input,true); Input(input,false);
	if (held) heldNavigation.insert(input);
	if (blocked) blockedNavigation.insert(input);
}
void Interaction::ModalChanged() {
	modalToken = ProposalToken(); CancelGesture(); hovered.clear(); pointerOption.clear(); pointerFraction.reset();
	for (auto& [id,item] : items) DetachNumber(id,item);
	blockedNavigation = heldNavigation;
}
void Interaction::ResolvePendingFocus() {
	if (!focusPending || modalBlocked) return;
	focused = Eligible(pendingFocus) ? pendingFocus : First();
	if (!focused.empty()) { focusPending = false; pendingFocus.clear(); }
}
bool Interaction::SyncAuthoredModals(const std::vector<std::string>& roots, std::string& error) {
	error.clear(); std::string previous;
	for (const auto& root : roots) {
		if (!authoredModals.contains(root) || root == previous || !Within(root,previous)) {
			error = "Visible authored modals must form one nested ancestry chain";
			if (!modalBlocked) {
				if (!focusPending) { pendingFocus = focused; focusPending = true; }
				modalBlocked = true; ModalChanged(); Refresh();
			}
			return false;
		}
		previous = root;
	}
	std::vector<std::string> current;
	for (const auto& scope : modals) if (scope.authored) current.push_back(scope.root);
	const bool wasBlocked = modalBlocked; modalBlocked = false;
	if (current == roots) { if (wasBlocked) ModalChanged(); return true; }
	size_t common = 0;
	while (common < current.size() && common < roots.size() && current[common] == roots[common]) ++common;
	std::string next = focusPending ? pendingFocus : focused;
	// Manual scopes can be nested above an authored scope. An authored change
	// replaces those transient scopes, preserving their exact return chain.
	while (modals.size() > common) { next = modals.back().restore; modals.pop_back(); }
	for (size_t i = common; i < roots.size(); ++i) {
		modals.push_back({roots[i],next,true}); next = authoredModals.at(roots[i]).initialFocus;
	}
	pendingFocus = next; focusPending = true; focused.clear(); ModalChanged(); Refresh(); return true;
}
bool Interaction::CanDispatchModalBack(const ControlAction& action) const {
	if (modalBlocked || action.kind != ControlAction::Kind::Back || action.document != document ||
		!action.modalToken || action.modalToken != modalToken || action.node != Modal()) return false;
	const bool authored = !modals.empty() && modals.back().authored;
	return authored ? !action.event.empty() && action.event == authoredModals.at(Modal()).backEvent : action.event.empty();
}
bool Interaction::CanDispatchControlAction(const ControlAction& action) const {
	if (action.kind == ControlAction::Kind::Back) return CanDispatchModalBack(action);
	if (focusPending || action.document != document || !action.modalToken || action.modalToken != modalToken || !Eligible(action.node)) return false;
	const auto& item = items.at(action.node); const auto& control = item.control;
	if (action.action != control.action || action.event != control.event) return false;
	if (control.role == ControlRole::Number)
		return focused == action.node && item.number && !item.number->detached && !item.number->conflict && item.pending && action.proposal == item.pending &&
			action.proposalToken != 0 && action.proposalToken == item.proposalToken &&
			action.editSession == item.number->identity.session && action.editRevision == item.number->identity.revision;
	return true;
}
bool Interaction::PushModal(const std::string& root) {
	if (modalBlocked || authoredModals.contains(root) || !parents.contains(root) || !Within(root,Modal()) || root == Modal()) return false;
	modals.push_back({root,focusPending ? pendingFocus : focused}); ModalChanged();
	focusPending = false; pendingFocus.clear(); focused = First(); Refresh(); return true;
}
bool Interaction::PopModal() {
	if (modalBlocked || modals.empty() || modals.back().authored) return false;
	const auto restore = modals.back().restore; modals.pop_back(); ModalChanged();
	focused = Eligible(restore) ? restore : First(); Refresh(); return true;
}
void Interaction::Navigate(MenuInput input) {
	static const char* names[] = {"next","previous","up","down","left","right"};
	if (input < MenuInput::Next || input > MenuInput::Right) return;
	if (!Eligible(focused)) {
		if (input == MenuInput::Previous) { for (auto it = order.rbegin(); it != order.rend(); ++it) if (Eligible(*it)) { Focus(*it); return; } }
		Focus(First()); return;
	}
	const auto& item = items.at(focused);
	const auto explicitTarget = item.control.navigation.find(names[static_cast<unsigned>(input)]);
	if (explicitTarget != item.control.navigation.end() && Eligible(explicitTarget->second)) { Focus(explicitTarget->second); return; }
	if (input == MenuInput::Next || input == MenuInput::Previous) {
		const auto current = std::find(order.begin(),order.end(),focused)-order.begin();
		for (size_t n = 1; n <= order.size(); ++n) {
			const auto i = (current+order.size()+(input == MenuInput::Next ? n : order.size()-n))%order.size();
			if (Eligible(order[i])) { Focus(order[i]); break; }
		}
		return;
	}
	const auto& a = item.bounds;
	const double ax = a.x+a.width*.5, ay = a.y+a.height*.5;
	double best = std::numeric_limits<double>::infinity(); std::string next;
	for (const auto& id : order) if (id != focused && Eligible(id)) {
		const auto& b = items.at(id).bounds;
		const double dx = b.x+b.width*.5-ax, dy = b.y+b.height*.5-ay;
		const bool horizontal = input == MenuInput::Left || input == MenuInput::Right;
		const double forward = (horizontal ? dx : dy)*(input == MenuInput::Left || input == MenuInput::Up ? -1 : 1);
		if (forward <= .001) continue;
		const double lateral = std::abs(horizontal ? dy : dx);
		const double score = forward*forward+4*lateral*lateral;
		if (score < best) { best = score; next = id; }
	}
	if (!next.empty()) Focus(next);
}
void Interaction::Refresh() {
	if ((!dragging.empty() && !Eligible(dragging)) || (!popup.empty() && !Eligible(popup))) CancelGesture();
	if (focusPending || !Eligible(focused)) focused.clear();
	if (!Eligible(hovered)) hovered.clear();
	if (!Eligible(armed)) armed.clear();
	for (auto& [id,item] : items) {
		if (item.number && !item.number->detached && (focused != id || focusPending || !Eligible(id))) DetachNumber(id,item);
		const auto state = !item.control.enabled ? ControlState::Disabled :
			armed == id && (!pointerArm || hovered == id || dragging == id) ? ControlState::Pressed :
			focused == id ? ControlState::Focus : hovered == id ? ControlState::Hover : ControlState::Default;
		if (!item.known || item.state != state) { item.known = true; item.state = state; feedback.push_back({id,item.control.states.at(state),state}); }
	}
}
std::optional<ControlState> Interaction::State(const std::string& id) const { const auto found = items.find(id); return found == items.end() ? std::nullopt : std::optional(found->second.state); }
void Interaction::Queue(ControlAction action) {
	action.modalToken = modalToken;
	if (!modalToken || actions.size() >= 256) overflowed = true;
	else actions.push_back(std::move(action));
}
std::vector<ControlFeedback> Interaction::TakeFeedback() { std::vector<ControlFeedback> result; result.swap(feedback); return result; }
std::vector<ControlAction> Interaction::TakeActions() { std::vector<ControlAction> result; result.swap(actions); overflowed = false; return result; }
InteractionSnapshot Interaction::Capture() const {
	InteractionSnapshot result;
	result.focus = focused;
	for (const auto& scope : modals) result.modals.push_back({scope.root,scope.restore,scope.authored});
	result.focusPending = focusPending; result.pendingFocus = pendingFocus;
	for (const auto& [id,item] : items) { result.enabled[id] = item.control.enabled; result.presented[id] = item.state; }
	return result;
}
bool Interaction::Restore(const InteractionSnapshot& snapshot, std::string& error) {
	error.clear();
	auto reject = [&](const char* message) { error = message; return false; };
	if (snapshot.enabled.size() != items.size() || snapshot.presented.size() != items.size()) return reject("Restored controls do not match the document");
	for (const auto& [id,enabled] : snapshot.enabled) if (!items.contains(id)) return reject("Unknown restored control");
	for (const auto& [id,state] : snapshot.presented) {
		if (!items.contains(id) || (state != ControlState::Default && state != ControlState::Focus && state != ControlState::Disabled) ||
			(state == ControlState::Focus) != (id == snapshot.focus)) return reject("Invalid restored persistent control presentation");
	}
	if (!snapshot.focus.empty() && !items.contains(snapshot.focus)) return reject("Unknown restored focus control");
	std::string previous;
	for (const auto& scope : snapshot.modals) {
		if (scope.root.empty() || !parents.contains(scope.root) || scope.root == previous || !Within(scope.root,previous))
			return reject("Invalid restored modal ancestry");
		if (scope.authored != authoredModals.contains(scope.root)) return reject("Invalid restored modal ownership");
		if (!scope.restore.empty() && (!items.contains(scope.restore) || !Within(scope.restore,previous)))
			return reject("Invalid restored modal return focus");
		previous = scope.root;
	}
	if (!snapshot.focus.empty() && !Within(snapshot.focus,previous)) return reject("Restored focus is outside its modal");
	if ((!snapshot.focusPending && !snapshot.pendingFocus.empty()) ||
		(snapshot.focusPending && !snapshot.focus.empty()) ||
		(!snapshot.pendingFocus.empty() && (!items.contains(snapshot.pendingFocus) || !Within(snapshot.pendingFocus,previous))))
		return reject("Invalid restored pending modal focus");
	Interaction candidate = *this;
	candidate.numberEpoch = ProposalToken();
	if (!candidate.numberEpoch) return reject("Number draft identity exhausted");
	candidate.focused = snapshot.focus;
	candidate.modals.clear();
	for (const auto& scope : snapshot.modals) candidate.modals.push_back({scope.root,scope.restore,scope.authored});
	candidate.modalBlocked = false; candidate.modalToken = ProposalToken(); candidate.blockedNavigation = candidate.heldNavigation;
	candidate.focusPending = snapshot.focusPending; candidate.pendingFocus = snapshot.pendingFocus;
	if (candidate.HasAuthoredModals() && (candidate.focusPending || !candidate.focused.empty() || !candidate.modals.empty())) {
		// Restored authored scopes must wait for this instance's first projected
		// layout. Neither old geometry nor a newly enabled default may steal the
		// exact saved selection while rebuilding resources.
		if (!candidate.focusPending) {
			candidate.focusPending = true; candidate.pendingFocus = candidate.focused;
		}
		candidate.focused.clear();
	}
	candidate.CancelGesture(); candidate.hovered.clear(); candidate.pointerArm = false;
	candidate.pointerOption.clear(); candidate.pointerFraction.reset();
	for (auto& [id,item] : candidate.items) { item.pending.reset(); item.rejected.reset(); item.proposalToken = 0; item.number.reset(); }
	candidate.feedback.clear(); candidate.actions.clear(); candidate.overflowed = false;
	for (auto& [id,item] : candidate.items) item.control.enabled = snapshot.enabled.at(id);
	// A host-source update may disable the saved selection. Fresh projected
	// bounds choose the fallback; stale pre-restore geometry is not consulted.
	if (!candidate.focused.empty() && !candidate.items.at(candidate.focused).control.enabled) candidate.focused.clear();
	for (auto& [id,item] : candidate.items) {
		item.state = !item.control.enabled ? ControlState::Disabled : candidate.focused == id ? ControlState::Focus : ControlState::Default;
		item.known = true;
		if (item.state != snapshot.presented.at(id)) candidate.feedback.push_back({id,item.control.states.at(item.state),item.state});
	}
	return Adopt(std::move(candidate),error);
}
} // namespace openq4::ui
