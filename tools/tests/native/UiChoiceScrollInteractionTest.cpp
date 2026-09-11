// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Interaction.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
using namespace openq4::ui;
static unsigned checks=0;
#define CHECK(v) do{++checks;if(!(v)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);std::exit(1);}}while(false)
struct Fixture {
    DocumentModel model;Interaction input;std::string error;ScrollReadback read;
    Fixture(bool bar=true) {
        model.id="popup-scroll";model.root.id="root";Node choice;choice.id="choice";choice.control=Control{};
        auto& control=*choice.control;control.role=ControlRole::Choice;control.action="select";control.value=Expression{};control.value->type=0;
        for(auto state:{ControlState::Default,ControlState::Hover,ControlState::Focus,ControlState::Pressed,ControlState::Disabled})control.states[state]="paint";
        ChoiceSpec spec;spec.visibleRows=2;if(bar)spec.scrollbar=ScrollSpec{"viewport","track","thumb",true,36,24};
        for(unsigned i=0;i<6;++i){ChoiceOption option;option.id="row"+std::to_string(i);option.value=double(i);spec.options.push_back(option);}
        control.widget=spec;model.root.children.push_back(choice);Node modal;modal.id="modal";model.root.children.push_back(modal);
        input.Reset(model);CHECK(input.SetReadbacks({{"choice",{0.,false,{true,true,true,true,true,true}}}},error));
        input.SetBounds({{"choice",{0,0,180,40,true}}});CHECK(input.Focus("choice"));Open();
    }
    void Key(MenuInput key){input.Input(key,true);input.Input(key,false);}
    void Open(){Key(MenuInput::Accept);CHECK(input.Widget("choice")->popupOpen);}
    WidgetViewState View(){return *input.Widget("choice");}
    void Layout(double viewport=80,double content=240,double track=80) {
        const auto view=View();CHECK(MeasureScroll({viewport,content,view.popupOffsetDp,track,24},read.geometry));
        read.available=true;read.dpRatio=1;read.lineStep=36;++read.geometryToken;
        CHECK(input.SetChoiceScrollReadback("choice",view.popupToken,view.popupRevision,read,true));
    }
};
static void IdentityAndShape() {
    Fixture f;f.Layout();const auto before=f.View();
    for(int fault=0;fault<15;++fault) {
        auto r=f.read;auto token=before.popupToken,revision=before.popupRevision;std::string id="choice";
        switch(fault) {
            case 0:token=0;break;case 1:++token;break;case 2:++revision;break;case 3:id="missing";break;
            case 4:r.geometryToken=0;break;case 5:r.dpRatio=0;break;case 6:r.lineStep=0;break;
            case 7:r.geometry.offset=241;break;case 8:r.geometry.travel=-1;break;case 9:r.geometry.usable=false;break;
            case 10:r.geometry.position=99;break;case 11:r.geometry.thumb=81;break;
            case 12:r.geometry.range=std::numeric_limits<double>::infinity();break;
            case 13:r.available=false;break;case 14:r.dpRatio=2;break;
        }
        CHECK(!f.input.SetChoiceScrollReadback(id,token,revision,r,false));
        CHECK(f.View().popupOffsetDp==before.popupOffsetDp&&f.View().popupRevision==before.popupRevision&&f.View().scroll->available);
    }
    ++f.read.geometryToken;CHECK(f.input.SetChoiceScrollReadback("choice",before.popupToken,before.popupRevision,f.read,false));
    auto old=f.read;--old.geometryToken;CHECK(!f.input.SetChoiceScrollReadback("choice",before.popupToken,before.popupRevision,old,false));
    CHECK(!f.input.ChoiceScrollPulse("choice",static_cast<ScrollStep>(255)));
    CHECK(f.input.ChoiceScrollPulse("choice",ScrollStep::PageForward));CHECK(f.View().popupOffsetDp==44);
    CHECK(!f.input.ChoiceScrollPulse("choice",ScrollStep::LineForward)); // not painted at this revision
    CHECK(!f.input.SetChoiceScrollReadback("choice",before.popupToken,before.popupRevision,f.read,false));
    f.Layout();CHECK(f.input.ChoiceScrollReady("choice"));CHECK(f.input.TakeActions().empty()&&f.input.TakeScrollCommands().empty());
    f.Key(MenuInput::Back);f.Open();const auto reopened=f.View();CHECK(reopened.popupToken!=before.popupToken);
    ++f.read.geometryToken;CHECK(!f.input.SetChoiceScrollReadback("choice",before.popupToken,reopened.popupRevision,f.read,false));
    f.Layout();CHECK(f.input.PushModal("modal"));CHECK(!f.input.ChoiceScrollReady("choice"));CHECK(f.input.PopModal());
    CHECK(!f.View().popupOpen&&f.input.TakeActions().empty());
}
static void CaptureAndHistory() {
    Fixture f;f.Layout();auto v=f.View();
    f.input.PointerPart("choice",.9,{},false,true);f.input.Pointer(true);f.input.Pointer(false);
    CHECK(f.View().popupOpen&&f.View().popupOffsetDp==44&&f.input.Focused()=="choice"&&f.input.TakeActions().empty());
    f.Layout();auto saved=f.input.CaptureWidgets();CHECK(saved.version==3&&saved.widgets.at("choice").scrollOffsetDp==44);
    for(int fault=0;fault<6;++fault){auto bad=saved;if(fault==0)bad.version=2;if(fault==1)bad.widgets.at("choice").scrollOffsetDp.reset();if(fault==2)bad.widgets.at("choice").scrollOffsetDp=-1;if(fault==3)bad.widgets.at("choice").scrollOffsetDp=1e13;if(fault==4)bad.widgets.at("choice").role=ControlRole::Toggle;if(fault==5)bad.widgets.at("choice").firstVisible=1;CHECK(!f.input.RestoreWidgets(bad,f.error));CHECK(f.View().popupOpen&&f.View().popupOffsetDp==44);}
    CHECK(f.input.RestoreWidgets(saved,f.error));CHECK(!f.View().popupOpen&&f.input.CapturedPointerControl().empty());
    f.input.SetBounds({{"choice",{0,0,180,40,true}}});f.input.ReleaseInputSources();CHECK(f.input.Focus("choice"));f.Open();f.Layout();
    CHECK(f.View().popupOffsetDp==44&&f.View().revealRevision==0);
    v=f.View();const double grab=.25;f.input.PointerPart("choice",(f.read.geometry.position+f.read.geometry.thumb*grab)/f.read.geometry.track,{},true,true);
    f.input.Pointer(true);CHECK(f.input.CapturedPointerControl()=="choice");f.input.PointerPart("choice",.8,{},false,true);
    double expected=0;CHECK(ScrollDragOffset(f.read.geometry,.8*f.read.geometry.track,grab,expected));CHECK(std::abs(f.View().popupOffsetDp-expected)<1e-9);
    f.Layout(100,600,150);f.input.PointerPart("choice",.6,{},false,true);CHECK(ScrollDragOffset(f.read.geometry,90,grab,expected));CHECK(std::abs(f.View().popupOffsetDp-expected)<1e-9);
    f.input.Pointer(false);CHECK(f.View().popupOpen&&f.input.TakeActions().empty());
    f.Layout();f.input.PointerPart("choice",0,{},true,true);f.input.Pointer(true);
    CHECK(f.input.SetReadbacks({{"choice",{1.,false,{true,true,true,true,true,true}}}},f.error));CHECK(!f.View().popupOpen);
    f.input.Pointer(false);CHECK(f.input.TakeActions().empty());
    Fixture legacy(false);const auto old=legacy.input.CaptureWidgets();CHECK(old.version==2&&!old.widgets.at("choice").scrollOffsetDp);
    auto incompatible=old;incompatible.version=3;incompatible.widgets.at("choice").scrollOffsetDp=0;CHECK(!legacy.input.RestoreWidgets(incompatible,legacy.error));
    CHECK(legacy.input.RestoreWidgets(old,legacy.error));
}
static void SemanticPopup() {
    Fixture legacy(false);const auto legacyOpening=legacy.View();
    CHECK(legacyOpening.popupToken!=0);
    CHECK(legacy.input.CloseChoicePopup("choice",legacyOpening.popupToken));
    CHECK(legacy.input.OpenChoicePopup("choice"));
    CHECK(legacy.View().popupToken!=legacyOpening.popupToken);
    CHECK(!legacy.input.CloseChoicePopup("choice",legacyOpening.popupToken));
    CHECK(legacy.input.CloseChoicePopup("choice",legacy.View().popupToken));
    CHECK(legacy.input.TakeActions().empty());
    Fixture f;f.Layout();auto original=f.View();
    CHECK(!f.input.OpenChoicePopup("choice")); // never replace an existing opening
    CHECK(!f.input.CloseChoicePopup("choice",0));
    CHECK(!f.input.CloseChoicePopup("choice",original.popupToken+1));
    CHECK(!f.input.ScrollChoicePopup("choice",original.popupToken+1,f.read.geometryToken,ScrollStep::End));
    CHECK(!f.input.ScrollChoicePopup("choice",original.popupToken,f.read.geometryToken+1,ScrollStep::End));
    CHECK(!f.input.ScrollChoicePopup("choice",original.popupToken,f.read.geometryToken,static_cast<ScrollStep>(255)));
    CHECK(f.View().popupOffsetDp==original.popupOffsetDp&&f.View().popupToken==original.popupToken);
    CHECK(f.input.ScrollChoicePopup("choice",original.popupToken,f.read.geometryToken,ScrollStep::End));
    CHECK(f.View().popupOffsetDp==160);f.Layout();
    CHECK(f.input.ScrollChoicePopup("choice",original.popupToken,f.read.geometryToken,ScrollStep::Start));
    CHECK(f.View().popupOffsetDp==0&&f.View().accepted==original.accepted&&!f.View().pending&&f.View().proposalToken==original.proposalToken);
    CHECK(f.input.TakeActions().empty()&&f.input.TakeScrollCommands().empty());
    CHECK(f.input.CloseChoicePopup("choice",original.popupToken));
    CHECK(f.input.OpenChoicePopup("choice"));auto reopened=f.View();
    CHECK(reopened.popupToken!=original.popupToken);
    CHECK(!f.input.CloseChoicePopup("choice",original.popupToken));
    CHECK(f.input.CloseChoicePopup("choice",reopened.popupToken));
    for(auto held:{MenuInput::Accept,MenuInput::Back,MenuInput::Down}) {
        f.input.QuarantineInput(held,true);CHECK(!f.input.OpenChoicePopup("choice"));
        f.input.QuarantineInput(held,false);CHECK(f.input.OpenChoicePopup("choice"));f.Layout();
        f.input.QuarantineInput(held,true);const auto open=f.View();
        CHECK(!f.input.CloseChoicePopup("choice",open.popupToken));
        CHECK(!f.input.ScrollChoicePopup("choice",open.popupToken,f.read.geometryToken,ScrollStep::End));
        CHECK(f.View().popupToken==open.popupToken&&f.View().popupOffsetDp==open.popupOffsetDp);
        f.input.QuarantineInput(held,false);CHECK(f.input.CloseChoicePopup("choice",open.popupToken));
    }
    f.input.Pointer(true);CHECK(!f.input.OpenChoicePopup("choice"));f.input.Pointer(false);
    CHECK(f.input.SetEnabled("choice",false));CHECK(!f.input.OpenChoicePopup("choice"));
    CHECK(f.input.SetEnabled("choice",true));CHECK(f.input.Focus("choice"));
    CHECK(f.input.SetReadbacks({{"choice",{0.,false,{false,false,false,false,false,false}}}},f.error));
    CHECK(!f.input.OpenChoicePopup("choice"));
    CHECK(f.input.SetReadbacks({{"choice",{0.,false,{true,true,true,true,true,true}}}},f.error));
    CHECK(f.input.Focus(""));CHECK(!f.input.OpenChoicePopup("choice"));CHECK(f.input.Focus("choice"));
    f.input.SetBounds({});CHECK(!f.input.OpenChoicePopup("choice"));
    CHECK(f.input.TakeActions().empty()&&f.input.TakeScrollCommands().empty());
}
static void ExactInvalidation() {
    for(bool bar:{false,true})for(int held=0;held<4;++held) {
        Fixture f(bar);if(bar)f.Layout();const auto before=f.View();
        if(held==0){f.input.PointerPart("choice",{},"row2");f.input.Pointer(true);}
        else f.input.QuarantineInput(held==1?MenuInput::Accept:held==2?MenuInput::Back:MenuInput::Down,true);
        CHECK(!f.input.CloseChoicePopup("choice",before.popupToken));
        CHECK(!f.input.InvalidateChoicePopup("choice",0));
        CHECK(!f.input.InvalidateChoicePopup("choice",before.popupToken+1));
        CHECK(!f.input.InvalidateChoicePopup("missing",before.popupToken));
        CHECK(f.View().popupToken==before.popupToken);
        CHECK(f.input.InvalidateChoicePopup("choice",before.popupToken));
        CHECK(!f.View().popupOpen&&f.View().accepted==before.accepted&&f.View().proposalToken==before.proposalToken&&!f.View().pending);
        CHECK(f.input.CapturedPointerControl().empty()&&f.input.TakeActions().empty());
        CHECK(!f.input.OpenChoicePopup("choice")); // still held, not rearmed by invalidation
        if(held==0)f.input.Pointer(false);else f.input.QuarantineInput(held==1?MenuInput::Accept:held==2?MenuInput::Back:MenuInput::Down,false);
        CHECK(f.input.TakeActions().empty());CHECK(f.input.OpenChoicePopup("choice"));
        CHECK(!f.input.InvalidateChoicePopup("choice",before.popupToken));
        CHECK(f.View().popupToken!=before.popupToken&&f.View().popupOpen);
    }
    Fixture f;Node number;number.id="number";number.control=f.model.root.children[0].control;
    number.control->role=ControlRole::Number;NumberSpec spec;spec.maximum=10;number.control->widget=spec;
    f.model.root.children.push_back(number);f.input.Reset(f.model);
    CHECK(f.input.SetReadbacks({{"choice",{0.,false,{true,true,true,true,true,true}}},{"number",{1.}}},f.error));
    f.input.SetBounds({{"choice",{0,0,180,40,true}},{"number",{0,50,180,40,true}}});
    CHECK(f.input.Focus("number")&&f.input.BeginNumberEdit("number",f.error));
    CHECK(f.input.ReplaceNumberSelection("number",f.input.Widget("number")->number->identity,"2.5",f.error));
    CHECK(f.input.Focus("choice")&&f.input.OpenChoicePopup("choice"));const auto saved=f.input.CaptureWidgets();
    CHECK(saved.widgets.at("number").number.has_value());
    CHECK(f.input.InvalidateChoicePopup("choice",f.View().popupToken));
    const auto after=f.input.CaptureWidgets();
    const auto& a=*after.widgets.at("number").number;const auto& b=*saved.widgets.at("number").number;
    CHECK(a.state.text==b.state.text&&a.state.anchor==b.state.anchor&&a.state.caret==b.state.caret&&a.undo.size()==b.undo.size()&&a.redo.size()==b.redo.size());
    CHECK(a.baselineText==b.baselineText&&a.baselineValue==b.baselineValue&&a.conflict==b.conflict);
    CHECK(f.input.TakeActions().empty());
}

int main(){ExactInvalidation();IdentityAndShape();CaptureAndHistory();SemanticPopup();std::printf("PASS %u checks; pure actual Choice Interaction and ScrollGeometry.\n",checks);return 0;}
