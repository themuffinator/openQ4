// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "src/ui/retained/Interaction.h"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace openq4::ui;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)
static double Parse(const std::string& text){double value=0;auto r=std::from_chars(text.data(),text.data()+text.size(),value);CHECK(r.ec==std::errc{}&&r.ptr==text.data()+text.size());return value;}
static std::string Decimal(long long units,unsigned places){const bool negative=units<0;std::string digits=std::to_string(negative?-units:units);while(digits.size()<=places)digits.insert(digits.begin(),'0');if(places)digits.insert(digits.end()-places,'.');if(negative)digits.insert(digits.begin(),'-');return digits;}
struct Fixture{
 Interaction input;std::string error;double accepted;SliderSpec spec;
 Fixture(double minimum,double maximum,double step,double seed,unsigned displayDecimals=0):accepted(seed){
  spec.minimum=minimum;spec.maximum=maximum;spec.step=step;spec.decimals=displayDecimals;
  DocumentModel model;model.id="decimal-slider";model.root.id="root";
  for(const char* id:{"slider","number"}){Node n;n.id=id;Control c;c.role=std::string(id)=="slider"?ControlRole::Slider:ControlRole::Number;c.action="edit";Expression e;e.type=0;c.value=e;
   for(auto state:{ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})c.states[state]="feedback";
   if(c.role==ControlRole::Slider)c.widget=spec;else{NumberSpec number;number.minimum=minimum;number.maximum=maximum;number.maxBytes=128;number.exponent=true;c.widget=number;}n.control=c;model.root.children.push_back(n);
  }
  input.Reset(model);Read(seed);input.SetBounds({{"slider",{0,0,100,20,true}},{"number",{0,30,100,20,true}}});
 }
 void Read(double value){CHECK(input.SetReadbacks({{"slider",{value,false,{}}},{"number",{value,false,{}}}},error));accepted=value;}
 void CheckProposal(double expected,bool pointer=false){auto actions=input.TakeActions();if(expected==accepted&&!pointer){CHECK(actions.empty());return;}if(actions.size()!=1||!actions[0].proposal)std::fprintf(stderr,"proposal count=%zu pointer=%d current=%.17g expected=%.17g min=%.17g step=%.17g\n",actions.size(),pointer,accepted,expected,spec.minimum,spec.step);CHECK(actions.size()==1&&actions[0].proposal);CHECK(std::get<double>(*actions[0].proposal)==expected);Read(expected);CHECK(input.AcknowledgeProposal(actions[0].node,actions[0].proposalToken,true));CHECK(std::get<double>(input.Widget("number")->accepted)==expected);}
 void Point(double fraction,double expected){input.PointerPart("slider",fraction);input.Pointer(true);input.Pointer(false);CheckProposal(expected,true);}
 void Key(MenuInput key,double expected){CHECK(input.Focus("slider"));input.Input(key,true);input.Input(key,false);CheckProposal(expected);}
};
static void AuthoredLattices(){
 struct Lattice{long long first,step;unsigned places,ticks;};
 // Decimals deliberately differ from lattice precision: the former is only
 // the old slider label's display setting, never a Number rounding policy.
 for(const auto lattice:{Lattice{5,1,1,15},Lattice{0,25,3,40},Lattice{1375,250,4,20},Lattice{-250,25,3,20},Lattice{0,1,7,10},Lattice{100000000000000LL,25,3,10}}){
  const double minimum=Parse(Decimal(lattice.first,lattice.places)),step=Parse(Decimal(lattice.step,lattice.places));
  const double maximum=Parse(Decimal(lattice.first+lattice.ticks*lattice.step,lattice.places));
  for(unsigned index=0;index<=lattice.ticks;++index){const double expected=Parse(Decimal(lattice.first+index*lattice.step,lattice.places));Fixture f(minimum,maximum,step,minimum,index%7);f.Point(double(index)/lattice.ticks,expected);}
  Fixture f(minimum,maximum,step,minimum,0);for(unsigned index=1;index<=lattice.ticks;++index)f.Key(MenuInput::Right,Parse(Decimal(lattice.first+index*lattice.step,lattice.places)));
  for(unsigned index=lattice.ticks;index-->0;)f.Key(MenuInput::Left,Parse(Decimal(lattice.first+index*lattice.step,lattice.places)));
  f.Key(MenuInput::End,maximum);f.Key(MenuInput::Home,minimum);f.Point(-.5,minimum);f.Point(1.5,maximum);
 }
}
static void CustomNumbersRemainExact(){
 for(const auto seed:{1.375,.1375,std::nextafter(.1375,1.0)}){
  const bool brightness=seed>.5;Fixture f(brightness?.5:0,brightness?2.0:1.0,brightness?.1:.025,seed);
  CHECK(std::get<double>(f.input.Widget("slider")->accepted)==seed&&std::get<double>(f.input.Widget("number")->accepted)==seed);
  CHECK(f.input.Focus("number")&&f.input.BeginNumberEdit("number",f.error));auto edit=*f.input.Widget("number")->number;std::string formatted;
  CHECK(FormatTextNumber(seed,{brightness?.5:0,brightness?2.0:1.0,true},formatted,f.error)&&edit.state.text==formatted);
  const double exact=std::nextafter(seed,brightness?2.0:1.0);CHECK(FormatTextNumber(exact,{0,2,true},formatted,f.error));
  CHECK(f.input.ReplaceNumberSelection("number",edit.identity,formatted,f.error));edit=*f.input.Widget("number")->number;
  CHECK(edit.state.text==formatted&&f.input.CommitNumberEdit("number",edit.identity,f.error));auto actions=f.input.TakeActions();CHECK(actions.size()==1&&actions[0].proposal&&std::get<double>(*actions[0].proposal)==exact);
  CHECK(std::get<double>(f.input.Widget("slider")->accepted)==seed);
 }
 Fixture up(0,1,.025,.1375),down(0,1,.025,.1375);up.Key(MenuInput::Right,.15);down.Key(MenuInput::Left,.125);
 Fixture pages(0,1,.025,.1375);pages.Key(MenuInput::PageUp,.375);pages.Key(MenuInput::PageDown,.125);
}
int main(){AuthoredLattices();CustomNumbersRemainExact();std::printf("UiSliderDecimalTest: %u checks passed\n",checks);}
