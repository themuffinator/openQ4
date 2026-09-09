// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextEditCommand.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numeric>

using namespace openq4::ui;
static unsigned checks;
static void Check(bool value,const char* message) {
	++checks;if (!value) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
static bool Same(const TextEditState& a,const TextEditState& b) {return a.text==b.text && a.anchor==b.anchor && a.caret==b.caret;}
static bool Same(const TextEditOperation& a,const TextEditOperation& b) {
	return a.kind==b.kind && a.anchor==b.anchor && a.caret==b.caret && a.replacement==b.replacement && a.changed==b.changed;
}
static TextEditBoundaryMap Ascii(std::string_view text) {
	TextEditBoundaryMap map;map.text=text;map.visualCarets.resize(text.size()+1);
	std::iota(map.visualCarets.begin(),map.visualCarets.end(),0);map.deletionStops=map.visualCarets;return map;
}
static TextEditOperation Run(const TextEditState& state,const TextEditBoundaryMap& map,TextEditCommand command,bool extend=false) {
	const auto before=state;TextEditOperation out;std::string error="old";
	Check(EvaluateTextEditCommand(state,map,command,extend,out,error),"valid edit command");
	Check(error.empty() && Same(before,state),"evaluation never mutates source state");return out;
}
static void Selection(const TextEditOperation& op,std::size_t anchor,std::size_t caret,bool changed=true) {
	Check(op.kind==TextEditOperation::Kind::Selection && op.anchor==anchor && op.caret==caret && op.replacement.empty() && op.changed==changed,"exact selection proposal");
}
static void Deleted(const TextEditState& state,const TextEditOperation& op,const std::string& expected,std::size_t caret) {
	Check(op.kind==TextEditOperation::Kind::Replace && op.changed && op.replacement.empty(),"deletion is an explicit replacement range");
	TextEditBuffer buffer;std::string error;
	Check(buffer.Reset(state.text,{TextInputMaxBytes,false,false},error) && buffer.SetSelection(op.anchor,op.caret,error) && buffer.ReplaceSelection(op.replacement,error),"production edit buffer accepts supplied deletion operation");
	Check(buffer.State().text==expected && buffer.State().anchor==caret && buffer.State().caret==caret,"exact deletion and collapsed caret");
	Check(buffer.Undo(error) && buffer.State().text==state.text,"deletion integrates as one undoable local edit");
}
static void BasicMovement() {
	const std::string text="abc def";auto map=Ascii(text);
	Selection(Run({text,3,3},map,TextEditCommand::Left),2,2);
	Selection(Run({text,3,3},map,TextEditCommand::Right),4,4);
	Selection(Run({text,3,3},map,TextEditCommand::Home),0,0);
	Selection(Run({text,3,3},map,TextEditCommand::End),7,7);
	Selection(Run({text,0,0},map,TextEditCommand::Left),0,0,false);
	Selection(Run({text,7,7},map,TextEditCommand::Right),7,7,false);
	for (const auto& state:{TextEditState{text,2,5},TextEditState{text,5,2}}) {
		Selection(Run(state,map,TextEditCommand::Left),2,2);
		Selection(Run(state,map,TextEditCommand::Right),5,5);
		Selection(Run(state,map,TextEditCommand::Home,true),state.anchor,0);
		Selection(Run(state,map,TextEditCommand::End,true),state.anchor,7);
	}
	Selection(Run({text,5,2},map,TextEditCommand::Left,true),5,1);
	Selection(Run({text,5,2},map,TextEditCommand::Right,true),5,3);
	Selection(Run({text,5,0},map,TextEditCommand::Left,true),5,0,false);
	Selection(Run({text,5,7},map,TextEditCommand::Right,true),5,7,false);
	Selection(Run({text,4,1},map,TextEditCommand::SelectAll),0,7);
	Selection(Run({text,0,7},map,TextEditCommand::SelectAll),0,7,false);
	map.visualWordStops=std::vector<std::size_t>{0,3,4,7};
	Selection(Run({text,1,1},map,TextEditCommand::WordLeft),0,0);
	Selection(Run({text,1,1},map,TextEditCommand::WordRight),3,3);
	Selection(Run({text,3,3},map,TextEditCommand::WordRight),4,4);
	Selection(Run({text,4,4},map,TextEditCommand::WordLeft),3,3);
	Selection(Run({text,7,7},map,TextEditCommand::WordRight),7,7,false);
	Selection(Run({text,0,0},map,TextEditCommand::WordLeft),0,0,false);
	Selection(Run({text,5,2},map,TextEditCommand::WordLeft),2,2);
	Selection(Run({text,5,2},map,TextEditCommand::WordRight),5,5);
	Selection(Run({text,5,2},map,TextEditCommand::WordRight,true),5,3);
}
static void ExplicitDeletionUnits() {
	const std::string combined="e\xcc\x81X";
	TextEditBoundaryMap map{combined,{0,3,4},{0,3,4},{}};
	Selection(Run({combined,3,3},map,TextEditCommand::Left),0,0);
	Selection(Run({combined,0,0},map,TextEditCommand::Right),3,3);
	Deleted({combined,3,3},Run({combined,3,3},map,TextEditCommand::Backspace),"X",0);
	Deleted({combined,0,0},Run({combined,0,0},map,TextEditCommand::Delete),"X",0);
	Deleted({combined,4,3},Run({combined,4,3},map,TextEditCommand::Delete),"e\xcc\x81",3);
	Deleted({combined,3,0},Run({combined,3,0},map,TextEditCommand::Backspace),"X",0);
	Selection(Run({combined,0,0},map,TextEditCommand::Backspace),0,0,false);
	Selection(Run({combined,4,4},map,TextEditCommand::Delete),4,4,false);
	// The caller supplies whole flag/ZWJ deletion units. No special-case emoji
	// rules in the command helper are needed or claimed by these fixtures.
	for (const std::string& unit:{std::string("\xf0\x9f\x87\xac\xf0\x9f\x87\xa7"),std::string("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb")}) {
		const auto text=unit+"!";TextEditBoundaryMap units{text,{0,unit.size(),text.size()},{0,unit.size(),text.size()},{}};
		Deleted({text,unit.size(),unit.size()},Run({text,unit.size(),unit.size()},units,TextEditCommand::Backspace),"!",0);
		Deleted({text,0,0},Run({text,0,0},units,TextEditCommand::Delete),"!",0);
	}
	const std::string ascii="abc";auto separate=Ascii(ascii);
	Deleted({ascii,2,2},Run({ascii,2,2},separate,TextEditCommand::Backspace),"ac",1);
	separate.deletionStops={0,3};
	TextEditOperation unchanged{TextEditOperation::Kind::Replace,91,92,"sentinel",true};const auto before=unchanged;std::string error;
	Check(!EvaluateTextEditCommand({ascii,1,1},separate,TextEditCommand::Delete,false,unchanged,error) && Same(unchanged,before),"legal visual caret cannot authorize partial deletion of a supplied unit");
}
static void VisualOrder() {
	const std::string text="abc";TextEditBoundaryMap rtl{text,{3,2,1,0},{0,1,2,3},std::vector<std::size_t>{3,1,0}};
	Selection(Run({text,1,1},rtl,TextEditCommand::Left),2,2);
	Selection(Run({text,1,1},rtl,TextEditCommand::Right),0,0);
	Selection(Run({text,1,1},rtl,TextEditCommand::Home),3,3);
	Selection(Run({text,1,1},rtl,TextEditCommand::End),0,0);
	Selection(Run({text,3,1},rtl,TextEditCommand::Left),3,3);
	Selection(Run({text,1,3},rtl,TextEditCommand::Right),1,1);
	Selection(Run({text,3,1},rtl,TextEditCommand::Left,true),3,2);
	Selection(Run({text,2,2},rtl,TextEditCommand::WordLeft),3,3);
	Selection(Run({text,2,2},rtl,TextEditCommand::WordRight),1,1);
	Selection(Run({text,1,1},rtl,TextEditCommand::SelectAll),0,3);
	Deleted({text,2,2},Run({text,2,2},rtl,TextEditCommand::Backspace),"ac",1);
	// Enumerate every unique visual permutation of a short buffer. This checks
	// traversal independently from logical offsets, without claiming full bidi
	// shaping or the duplicate-affinity caret states absent from this contract.
	std::vector<std::size_t> permutation{0,1,2,3};
	do {
		TextEditBoundaryMap visual{text,permutation,{0,1,2,3},{}};
		for (std::size_t i=0;i<permutation.size();++i) {
			const TextEditState state{text,permutation[i],permutation[i]};
			const auto left=Run(state,visual,TextEditCommand::Left),right=Run(state,visual,TextEditCommand::Right);
			Check(left.caret==permutation[i?i-1:0] && right.caret==permutation[std::min(i+1,permutation.size()-1)],"navigation uses supplied visual order, not sorted logical offsets");
		}
	} while (std::next_permutation(permutation.begin(),permutation.end()));
}
static void AtomicValidation() {
	const std::string text="abcd";const auto valid=Ascii(text);
	std::vector<std::function<void(TextEditBoundaryMap&)>> bad{
		[](auto& m){m.text="abce";},[](auto& m){m.visualCarets.clear();},[](auto& m){m.deletionStops.clear();},
		[](auto& m){m.visualCarets={0,1,1,4};},[](auto& m){m.visualCarets={0,1,3,999};},
		[](auto& m){m.visualCarets={1,2,3,4};},[](auto& m){m.visualCarets={0,1,2,3};},
		[](auto& m){m.deletionStops={1,2,3,4};},[](auto& m){m.deletionStops={0,1,2,3};},
		[](auto& m){m.deletionStops={0,2,1,4};},[](auto& m){m.deletionStops={0,2,2,4};},
		[](auto& m){m.deletionStops={0,1,4,999};},[](auto& m){m.visualWordStops=std::vector<std::size_t>{};},
		[](auto& m){m.visualWordStops=std::vector<std::size_t>{0,2,2,4};},
		[](auto& m){m.visualWordStops=std::vector<std::size_t>{0,3,2,4};},
		[](auto& m){m.visualWordStops=std::vector<std::size_t>{1,2,4};},
		[](auto& m){m.visualWordStops=std::vector<std::size_t>{0,2,3};},
		[](auto& m){m.visualWordStops=std::vector<std::size_t>{0,999,4};}
	};
	TextEditOperation output{TextEditOperation::Kind::Replace,91,92,"sentinel",true};const auto unchanged=output;std::string error;
	for (const auto& mutate:bad) {
		auto map=valid;mutate(map);
		Check(!EvaluateTextEditCommand({text,2,2},map,TextEditCommand::Right,false,output,error) && !error.empty() && Same(output,unchanged),"malformed boundary table leaves output unchanged");
	}
	const TextEditBoundaryMap ambiguous{text,{0,1,1,4},{0,4},{}};
	Check(!EvaluateTextEditCommand({text,0,0},ambiguous,TextEditCommand::Right,false,output,error) && Same(output,unchanged),"duplicate visual affinities are rejected even when other maps and selection are valid");
	for (const auto& state:{TextEditState{text,99,2},TextEditState{text,2,std::numeric_limits<std::size_t>::max()}})
		Check(!EvaluateTextEditCommand(state,valid,TextEditCommand::Left,false,output,error) && Same(output,unchanged),"invalid selection is atomic");
	const TextEditBoundaryMap restricted{text,{0,2,4},{0,2,4},{}};
	for (const auto& state:{TextEditState{text,1,2},TextEditState{text,2,3}})
		Check(!EvaluateTextEditCommand(state,restricted,TextEditCommand::Left,false,output,error) && Same(output,unchanged),"valid scalar selection must still appear in legal caret map");
	for (const auto command:{TextEditCommand::Backspace,TextEditCommand::Delete,TextEditCommand::SelectAll})
		Check(!EvaluateTextEditCommand({text,2,2},valid,command,true,output,error) && Same(output,unchanged),"unsupported selection modifier rejected");
	Check(!EvaluateTextEditCommand({text,2,2},valid,static_cast<TextEditCommand>(99),false,output,error) && Same(output,unchanged),"unknown semantic command rejected");
	for (const auto command:{TextEditCommand::WordLeft,TextEditCommand::WordRight})
		Check(!EvaluateTextEditCommand({text,2,2},valid,command,false,output,error) && Same(output,unchanged),"missing explicit word service fails without whitespace guessing");
	for (const std::string& invalid:{std::string("a\0b",3),std::string("\xed\xa0\x80"),std::string("\xc0\xaf"),std::string(65537,'x')}) {
		auto map=Ascii(invalid);
		Check(!EvaluateTextEditCommand({invalid,0,0},map,TextEditCommand::Right,false,output,error) && Same(output,unchanged),"invalid or oversized text is rejected atomically");
	}
	const std::string unicode="\xc3\xa9X";TextEditBoundaryMap split{unicode,{0,1,2,3},{0,2,3},{}};
	Check(!EvaluateTextEditCommand({unicode,0,0},split,TextEditCommand::Right,false,output,error) && Same(output,unchanged),"visual boundary cannot split a UTF-8 scalar");
	TextEditBoundaryMap missing{unicode,{0,3},{0,2,3},{}};
	Check(!EvaluateTextEditCommand({unicode,0,0},missing,TextEditCommand::Right,false,output,error) && Same(output,unchanged),"deletion boundary must also be a supplied caret");
}
static void EmptyAndLimits() {
	const std::string empty;auto map=Ascii(empty);map.visualWordStops=std::vector<std::size_t>{0};
	for (const auto command:{TextEditCommand::Left,TextEditCommand::Right,TextEditCommand::Home,TextEditCommand::End,
		TextEditCommand::WordLeft,TextEditCommand::WordRight,TextEditCommand::SelectAll,TextEditCommand::Backspace,TextEditCommand::Delete})
		Selection(Run({empty,0,0},map,command),0,0,false);
	const std::string text(65536,'x');map=Ascii(text);
	Selection(Run({text,65535,65535},map,TextEditCommand::Right),65536,65536);
	Selection(Run({text,65536,65536},map,TextEditCommand::End),65536,65536,false);
	Deleted({text,0,text.size()},Run({text,0,text.size()},map,TextEditCommand::Delete),"",0);
}
int main() {
	BasicMovement();ExplicitDeletionUnits();VisualOrder();AtomicValidation();EmptyAndLimits();
	std::printf("Text edit commands: %u checks passed (supplied boundary semantics; no native input or shaping claim)\n",checks);return 0;
}
