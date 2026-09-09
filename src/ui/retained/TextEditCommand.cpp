// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "TextEditCommand.h"
#include <algorithm>
#include <limits>
#include <iterator>
#include <utility>

namespace openq4::ui {
namespace {
bool Fail(std::string& error,const char* message) { error=message;return false; }
bool ByteBoundary(std::string_view text,std::size_t offset) {
	return offset<=text.size() && (offset==text.size() || (static_cast<unsigned char>(text[offset])&0xc0)!=0x80);
}
}
bool EvaluateTextEditCommand(const TextEditState& state,const TextEditBoundaryMap& boundaries,
	TextEditCommand command,bool extendSelection,TextEditOperation& out,std::string& error) {
	switch (command) {
		case TextEditCommand::Left: case TextEditCommand::Right: case TextEditCommand::Home: case TextEditCommand::End:
		case TextEditCommand::WordLeft: case TextEditCommand::WordRight: break;
		case TextEditCommand::SelectAll: case TextEditCommand::Backspace: case TextEditCommand::Delete:
			if (extendSelection) return Fail(error,"Selection extension is unsupported for this edit command");
			break;
		default: return Fail(error,"Unknown text edit command");
	}
	if (!ValidateTextInputUtf8(state.text,error)) return false;
	if (boundaries.text!=state.text) return Fail(error,"Text boundaries belong to another buffer revision");
	const auto& carets=boundaries.visualCarets;const auto& stops=boundaries.deletionStops;
	if (carets.empty() || carets.size()>state.text.size()+1 || stops.empty() || stops.size()>carets.size())
		return Fail(error,"Missing or oversized text boundary table");
	constexpr auto absent=std::numeric_limits<std::size_t>::max();
	std::vector<std::size_t> positions(state.text.size()+1,absent);
	for (std::size_t i=0;i<carets.size();++i) {
		const auto offset=carets[i];
		if (!ByteBoundary(state.text,offset) || positions[offset]!=absent) return Fail(error,"Invalid or duplicate visual caret boundary");
		positions[offset]=i;
	}
	if (positions[0]==absent || positions[state.text.size()]==absent || stops.front()!=0 || stops.back()!=state.text.size())
		return Fail(error,"Text boundary tables omit a logical buffer endpoint");
	for (std::size_t i=0;i<stops.size();++i) {
		if (!ByteBoundary(state.text,stops[i]) || positions[stops[i]]==absent || (i && stops[i-1]>=stops[i]))
			return Fail(error,"Invalid or unordered logical deletion boundary");
	}
	if (!ByteBoundary(state.text,state.anchor) || !ByteBoundary(state.text,state.caret) ||
		positions[state.anchor]==absent || positions[state.caret]==absent)
		return Fail(error,"Current selection is outside the legal caret boundaries");
	if (boundaries.visualWordStops) {
		const auto& words=*boundaries.visualWordStops;
		if (words.empty() || words.size()>carets.size() || words.front()!=carets.front() || words.back()!=carets.back())
			return Fail(error,"Invalid or incomplete visual word boundary table");
		std::size_t previous=0;
		for (std::size_t i=0;i<words.size();++i) {
			if (!ByteBoundary(state.text,words[i]) || positions[words[i]]==absent || (i && positions[words[i]]<=previous))
				return Fail(error,"Invalid or unordered visual word boundary");
			previous=positions[words[i]];
		}
	}
	const bool word=command==TextEditCommand::WordLeft || command==TextEditCommand::WordRight;
	if (word && !boundaries.visualWordStops) return Fail(error,"Word movement requires explicit word boundaries");
	TextEditOperation candidate;candidate.anchor=state.anchor;candidate.caret=state.caret;
	if (command==TextEditCommand::Backspace || command==TextEditCommand::Delete) {
		const auto low=std::min(state.anchor,state.caret),high=std::max(state.anchor,state.caret);
		const auto start=std::lower_bound(stops.begin(),stops.end(),low),end=std::lower_bound(stops.begin(),stops.end(),high);
		if (start==stops.end() || *start!=low || end==stops.end() || *end!=high)
			return Fail(error,"Deletion selection splits a supplied deletion unit");
		std::size_t begin=low,finish=high;
		if (low==high) {
			if (command==TextEditCommand::Backspace && start!=stops.begin()) begin=*std::prev(start);
			if (command==TextEditCommand::Delete && end+1!=stops.end()) finish=*std::next(end);
		}
		if (begin!=finish) { candidate.kind=TextEditOperation::Kind::Replace;candidate.anchor=begin;candidate.caret=finish;candidate.changed=true; }
	} else if (command==TextEditCommand::SelectAll) {
		candidate.anchor=0;candidate.caret=state.text.size();
		candidate.changed=candidate.anchor!=state.anchor || candidate.caret!=state.caret;
	} else {
		const bool left=command==TextEditCommand::Left || command==TextEditCommand::WordLeft;
		std::size_t target=positions[state.caret];
		if (command==TextEditCommand::Home) target=0;
		else if (command==TextEditCommand::End) target=carets.size()-1;
		else if (!extendSelection && state.anchor!=state.caret)
			target=left ? std::min(positions[state.anchor],target) : std::max(positions[state.anchor],target);
		else if (word) {
			const auto& words=*boundaries.visualWordStops;
			if (left) {
				for (auto it=words.rbegin();it!=words.rend();++it) if (positions[*it]<target) {target=positions[*it];break;}
			} else {
				for (const auto offset:words) if (positions[offset]>target) {target=positions[offset];break;}
			}
		} else if (left) { if (target) --target; }
		else { if (target+1<carets.size()) ++target; }
		candidate.caret=carets[target];if (!extendSelection) candidate.anchor=candidate.caret;
		candidate.changed=candidate.anchor!=state.anchor || candidate.caret!=state.caret;
	}
	out=std::move(candidate);error.clear();return true;
}
} // namespace openq4::ui
