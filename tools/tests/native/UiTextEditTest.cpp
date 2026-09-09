// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/TextEdit.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
using namespace openq4::ui;
static unsigned checks = 0;
static void Check(bool value, const char* message) {
	++checks;
	if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static bool Same(const TextEditState& a, const TextEditState& b) {
	return a.text == b.text && a.anchor == b.anchor && a.caret == b.caret;
}
static TextInputEvent Commit(const std::string& text) {
	TextInputEvent result; std::string error;
	Check(MakeTextInputCommit(text,result,error),"construct committed UTF8"); return result;
}
static TextInputEvent Preedit(const std::string& text, std::int64_t start, std::int64_t length) {
	TextInputEvent result; std::string error;
	Check(MakeTextInputPreedit(text,TextIndexUnit::UnicodeScalars,start,length,result,error),"construct preedit"); return result;
}
static void SelectionAndHistory() {
	TextEditBuffer edit; std::string error;
	const std::string unicode = "A\xc3\xa9\xf0\x9f\x98\x80Z";
	Check(edit.Reset(unicode,{},error),"load Unicode buffer");
	const auto initial = edit.State();
	for (std::size_t offset : {2u,4u,5u,6u,9u}) {
		Check(!edit.SetSelection(offset,offset,error),"reject split scalar/out-of-range caret");
		Check(Same(initial,edit.State()) && !error.empty(),"rejected selection is atomic");
	}
	Check(edit.SetSelection(7,1,error),"reverse scalar-aligned selection");
	const auto selected = edit.State();
	Check(edit.Apply(Commit("xy"),error),"one committed input replaces selection");
	Check(edit.State().text=="AxyZ" && edit.State().anchor==3 && edit.State().caret==3,"replacement and caret");
	const auto changed = edit.State();
	Check(edit.Undo(error) && Same(edit.State(),selected),"undo restores original text and reverse selection");
	Check(edit.Redo(error) && Same(edit.State(),changed),"redo restores edit and caret");
	Check(edit.Undo(error),"return to prior edit");
	const auto history = edit.HistoryEntries();
	Check(!edit.ReplaceSelection(std::string("\xed\xa0\x80",3),error),"reject encoded surrogate");
	Check(Same(edit.State(),selected) && edit.CanRedo() && edit.HistoryEntries()==history,"invalid insertion preserves redo and buffer");
	Check(edit.ReplaceSelection("!",error) && !edit.CanRedo(),"new edit abandons redo");
	Check(edit.State().text=="A!Z","edited branch correct");
	Check(edit.SetSelection(0,3,error) && edit.Apply(Commit(""),error),"empty native commit accepted");
	Check(edit.State().text=="A!Z" && edit.State().anchor==0 && edit.State().caret==3,"empty native commit never erases selected text");
	Check(edit.ReplaceSelection("",error) && edit.State().text.empty(),"explicit deletion erases selection");
	Check(edit.Undo(error) && edit.State().text=="A!Z","delete is undoable");
	Check(edit.Reset("ok",{},error) && !edit.CanUndo() && !edit.CanRedo(),"new editing lifetime retires history");
}
static void Composition() {
	TextEditBuffer edit; std::string error;
	Check(edit.Reset("abcd",{},error) && edit.SetSelection(1,3,error),"composition fixture");
	const auto selected=edit.State();
	Check(edit.Apply(Preedit("\xc3\xa9",0,1),error),"start preedit");
	Check(Same(edit.State(),selected) && edit.PresentedText()=="a\xc3\xa9" "d" && !edit.CanUndo(),"preedit presentation without committing buffer");
	Check(edit.Composition()->selectionStart==0 && edit.Composition()->selectionLength==2,"canonical byte selection retained");
	Check(!edit.SetSelection(0,0,error) && !edit.ReplaceSelection("q",error) && !edit.Undo(error),"editor operations cannot steal active composition");
	Check(Same(edit.State(),selected) && edit.PresentedText()=="a\xc3\xa9" "d","rejected operations preserve composition");
	Check(edit.Apply(Preedit("xyz",1,1),error) && edit.PresentedText()=="axyzd","preedit replaces previous preedit, never appends");
	TextInputEvent bad=Preedit("xyz",0,1); bad.selectionLength=200;
	Check(!edit.Apply(bad,error) && edit.PresentedText()=="axyzd","malformed preedit preserves prior valid composition");
	Check(edit.Apply(Commit("Q"),error) && edit.State().text=="aQd" && !edit.Composition(),"commit replaces original selected range exactly once");
	Check(edit.HistoryEntries()==1 && edit.Undo(error) && Same(edit.State(),selected),"one commit is one undo step, preedit is not history");
	Check(edit.Apply(Preedit("z",-1,-1),error),"unknown candidate selection preserved");
	Check(!edit.Composition()->selectionStart && !edit.Composition()->selectionLength,"no invented preedit cursor");
	Check(edit.Apply(Preedit("",0,0),error) && !edit.Composition() && Same(edit.State(),selected),"empty preedit clears without committing");
	Check(edit.Apply(Preedit("x",0,1),error) && edit.Apply(Commit(""),error),"empty commit clears outstanding preedit");
	Check(!edit.Composition() && Same(edit.State(),selected),"empty commit leaves original selection intact");
	Check(edit.Apply(Preedit("x",0,1),error),"composition cancellation fixture");
	TextInputEvent cancel; MakeTextInputCancel(cancel);
	Check(edit.Apply(cancel,error) && Same(edit.State(),selected) && !edit.Composition(),"explicit cancellation no commit");
	Check(edit.Reset("abcd",{},error) && edit.SetSelection(3,1,error),"reverse selection native result fixture");
	const auto reverse=edit.State();
	Check(edit.Apply(Preedit("xy",0,2),error) && edit.Apply(Preedit("",0,0),error) && edit.Apply(Commit("\xc3\xa9"),error),"native preedit-clear-result ordering");
	Check(edit.State().text=="a\xc3\xa9" "d" && edit.State().caret==3 && edit.HistoryEntries()==1,"native result replaces original reverse selection once");
	Check(edit.Undo(error) && Same(edit.State(),reverse),"native result has one undo step");
	Check(edit.Apply(Preedit("x",0,1),error),"failed Reset composition fixture");
	const auto entries=edit.HistoryEntries();
	Check(!edit.Reset("\n",{},error) && Same(edit.State(),reverse) && edit.Composition() && edit.PresentedText()=="axd" && edit.HistoryEntries()==entries,"failed Reset preserves buffer, composition and redo");
	edit.CancelComposition();
	Check(edit.CanRedo(),"failed Reset retains redo eligibility");
	Check(edit.Reset("abcdef",{},error) && edit.SetSelection(1,5,error),"aliased replacement fixture");
	const std::string_view alias(edit.State().text.data()+2,3);
	Check(edit.ReplaceSelection(alias,error) && edit.State().text=="acdef","replacement safely copies a source view into its own buffer");
}
static void LimitsAndLines() {
	TextEditBuffer edit; std::string error;
	Check(edit.Reset("abc",{4,false,false},error),"small field");
	const auto prior=edit.State();
	for (const auto& value : {"xx","\r\n","\n","\t","\x7f","\xc2\x85","\xe2\x80\xa8","\xe2\x80\xa9"}) {
		Check(!edit.ReplaceSelection(value,error) && Same(edit.State(),prior),"invalid single-line/over-limit replacement is atomic");
	}
	Check(!edit.Apply(Preedit("xx",0,1),error) && !edit.Composition(),"composition total size includes unselected buffer");
	Check(edit.SetSelection(0,3,error) && edit.Apply(Preedit("xxxx",0,4),error),"selected bytes make room for preedit");
	Check(!edit.Apply(Commit("xxxxx"),error) && edit.Composition() && edit.PresentedText()=="xxxx","oversized commit preserves composition");
	edit.CancelComposition();
	Check(edit.ReplaceSelection("\xc3\xa9\xc3\xa9",error) && edit.State().text.size()==4,"field limits bytes rather than scalar count");
	const auto full=edit.State();
	Check(!edit.Reset("x",{0,false,false},error) && Same(full,edit.State()),"invalid policy preserves old editing lifetime");
	Check(!edit.Reset("x",{TextInputMaxBytes+1,false,false},error),"transport upper bound remains mandatory");
	Check(!edit.Reset("x",{10,false,true},error),"tab requires multiline policy");
	Check(edit.Reset("ok",{64,false,false},error),"single-line separators have ample byte capacity");
	const auto roomy=edit.State();
	for (const auto& separator : {"\xc2\x85","\xe2\x80\xa8","\xe2\x80\xa9"}) {
		Check(!edit.Reset(separator,{64,false,false},error) && Same(edit.State(),roomy),"single-line Reset rejects Unicode line separator independently of size");
		Check(!edit.ReplaceSelection(separator,error) && Same(edit.State(),roomy),"single-line replacement rejects Unicode line separator independently of size");
		Check(!edit.Apply(Preedit(separator,0,1),error) && Same(edit.State(),roomy) && !edit.Composition(),"single-line preedit rejects Unicode line separator independently of size");
	}
	Check(edit.Reset("",{20,true,true},error) && edit.ReplaceSelection("a\r\nb\rc\td",error),"multiline insertion normalizes CRLF and CR");
	Check(edit.State().text=="a\nb\nc\td" && edit.State().caret==7,"normalization adjusts caret to resulting bytes");
	Check(!edit.Apply(Preedit("\r\n",0,2),error),"preedit bytes are not rewritten under indexed ranges");
	const std::string separators="\xc2\x85\xe2\x80\xa8\xe2\x80\xa9";
	Check(edit.Reset(separators,{20,true,false},error) && edit.State().text==separators,"multiline preserves Unicode line separators");
	Check(edit.Reset("",{},error) && edit.ReplaceSelection("\xe2\x80\x8e\xe2\x80\x8f",error),"line policy does not strip bidi format characters");
	Check(edit.Reset("",{},error),"history cap fixture");
	for (int i=0;i<200;++i) {
		Check(edit.ReplaceSelection("x",error),"repeated committed edit");
		Check(edit.HistoryEntries()<=TextEditBuffer::MaxHistoryEntries && edit.HistoryTextBytes()<=TextEditBuffer::MaxHistoryTextBytes,"bounded aggregate history");
	}
	unsigned undos=0;
	while(edit.CanUndo()) { Check(edit.Undo(error),"bounded undo"); ++undos; }
	Check(undos==TextEditBuffer::MaxHistoryEntries,"nearest undo endpoints survive trimming");
	while(edit.CanRedo()) Check(edit.Redo(error),"bounded redo");
	Check(edit.State().text==std::string(200,'x'),"bounded redo restores latest text");
	std::string large(TextInputMaxBytes,'a');
	Check(edit.Reset(large,{},error),"maximum buffer fixture");
	for (int i=0;i<50;++i) {
		Check(edit.SetSelection(0,1,error) && edit.ReplaceSelection(i%2 ? "b" : "c",error),"large buffer edit");
		Check(edit.HistoryTextBytes()<=TextEditBuffer::MaxHistoryTextBytes,"history byte bound independent of entry limit");
	}
	const auto latest=edit.State();
	while(edit.CanUndo()) Check(edit.Undo(error),"large undo");
	while(edit.CanRedo()) Check(edit.Redo(error),"large redo");
	Check(Same(edit.State(),latest),"large buffer endpoints retained");
	Check(edit.Reset("x",{},error),"mixed-size history fixture");
	for (int i=0;i<50;++i) {
		Check(edit.SetSelection(0,edit.State().text.size(),error) && edit.ReplaceSelection(i%2 ? "x" : large,error),"alternating large and small history entries");
		Check(edit.HistoryTextBytes()<=TextEditBuffer::MaxHistoryTextBytes,"mixed-size edit byte cap");
	}
	const auto mixedLatest=edit.State();
	while(edit.CanUndo()) {
		Check(edit.Undo(error),"mixed-size undo transfer");
		Check(edit.HistoryEntries()<=TextEditBuffer::MaxHistoryEntries && edit.HistoryTextBytes()<=TextEditBuffer::MaxHistoryTextBytes,"mixed-size undo transfer remains bounded");
	}
	while(edit.CanRedo()) {
		Check(edit.Redo(error),"mixed-size redo transfer");
		Check(edit.HistoryEntries()<=TextEditBuffer::MaxHistoryEntries && edit.HistoryTextBytes()<=TextEditBuffer::MaxHistoryTextBytes,"mixed-size redo transfer remains bounded");
	}
	Check(Same(edit.State(),mixedLatest),"mixed-size history retains latest endpoint");
}
static void Numbers() {
	const TextNumberPolicy policy{-1000,1000,true};
	double value=123.0;
	Check(ParseTextNumber("",policy,value)==TextNumberStatus::Empty && value==123,"empty numeric buffer not a proposal");
	for (const auto& text : {"+","-",".","+.","-.","1e","1e+","1e-","1.E-"})
		Check(ParseTextNumber(text,policy,value)==TextNumberStatus::Incomplete && value==123,"incomplete numeric buffer preserved");
	for (const auto& text : {" "," 1","1 ","NaN","inf","0x1p0","1,5","--1","+e1",".e1","1e+z","1e2e3","1\n","1_000"})
		Check(ParseTextNumber(text,policy,value)==TextNumberStatus::Invalid && value==123,"reject malformed/non-decimal numeric input");
	for (const auto& text : {"1001","-1001","1e999999","1e-999999"})
		Check(ParseTextNumber(text,policy,value)==TextNumberStatus::OutOfRange && value==123,"reject range/overflow/underflow without mutation");
	Check(ParseTextNumber("1e2",{-1000,1000,false},value)==TextNumberStatus::Invalid && value==123,"field exponent policy");
	Check(ParseTextNumber("2",{10,1,true},value)==TextNumberStatus::InvalidPolicy && value==123,"invalid numeric bounds");
	Check(ParseTextNumber("2",{0,std::numeric_limits<double>::infinity(),true},value)==TextNumberStatus::InvalidPolicy,"finite numeric policy");
	for (const auto& sample : std::vector<std::pair<std::string,double>>{{"+1.25",1.25},{"-.5",-.5},{"1.",1},{"2e2",200},{".1e1",1},{"-1000",-1000},{"1000",1000},{"1.05",1.05}})
		Check(ParseTextNumber(sample.first,policy,value)==TextNumberStatus::Valid && value==sample.second,"exact numeric value, no step quantization");
	std::string text,error;
	Check(FormatTextNumber(1.1,policy,text,error) && text=="1.1","shortest readable numeric representation");
	Check(FormatTextNumber(-0.0,policy,text,error) && ParseTextNumber(text,policy,value)==TextNumberStatus::Valid && std::signbit(value),"signed zero round trip");
	const TextNumberPolicy any{-std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),true};
	for (bool exponent : {true,false}) {
		auto formatPolicy=any; formatPolicy.exponent=exponent;
		for (double original : {1.0/3.0,1e-20,1e20,std::nextafter(1.0,2.0),std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),-std::numeric_limits<double>::min()}) {
			Check(FormatTextNumber(original,formatPolicy,text,error) && ParseTextNumber(text,formatPolicy,value)==TextNumberStatus::Valid,"full finite range round trip under the field syntax policy");
			Check(std::memcmp(&original,&value,sizeof(double))==0,"round trip preserves exact double bits");
			Check(exponent || text.find_first_of("eE")==std::string::npos,"exponent-disabled fields format an accepted syntax");
		}
	}
	Check(FormatTextNumber(2,{0,1,false},text,error) && text=="2","out-of-bounds readback remains visible without clamping");
	const auto before=text;
	Check(!FormatTextNumber(std::numeric_limits<double>::quiet_NaN(),policy,text,error) && text==before,"failed numeric formatting leaves output unchanged");
	Check(!FormatTextNumber(0,{1,0,false},text,error) && text==before,"invalid formatting policy preserves output");
}
int main() {
	SelectionAndHistory(); Composition(); LimitsAndLines(); Numbers();
	std::printf("PASS %u text edit/model checks (no live routing, shaping, IME or clipboard)\n",checks);
}
