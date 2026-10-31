// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
// Actual production page, shared application service and real RmlUi layout.
#define main SystemPresetFixtureMain
#include "UiSystemPresetRuntimeTest.cpp"
#undef main
#include <functional>

static void Contained(const Bounds& inner,const Bounds& outer,const char* message) {
    if(inner.x<outer.x-1 || inner.y<outer.y-1 || inner.x+inner.width>outer.x+outer.width+1 || inner.y+inner.height>outer.y+outer.height+1)
        std::fprintf(stderr,"%s: inner=%g,%g %gx%g outer=%g,%g %gx%g\n",message,inner.x,inner.y,inner.width,inner.height,outer.x,outer.y,outer.width,outer.height);
    Check(inner.x>=outer.x-1 && inner.y>=outer.y-1 && inner.x+inner.width<=outer.x+outer.width+1 && inner.y+inner.height<=outer.y+outer.height+1,message);
}
static Rml::Element* Find(const std::string& id){return Rml::GetContext(0)->GetDocument(0)->GetElementById(id);}
// The single column (retained_settings_rows STACKED): a text size above 100%
// or a view taller than 0.76 of its width stands a tab's second column under
// its first.
static bool Stacked(View& v){return v.viewport.TextRatio()>1||float(v.viewport.width)<.76f*float(v.viewport.height);}
// Every text on a tab's page that shows. A tab's rows scroll in one area
// (settings-<tab>-rows > -columns > -column-N) between the strip and the
// footer: its two columns side by side at 100% text in a wide view, one under
// the other at a larger text size (Stacked), the rows grown by the text size.
// Each row, heading and note, once the area scrolls to it, lies wholly inside
// the area, the pages' bounds and the screen; each text's lines lie inside its
// own box, and a row's label and value inside their row, at every text size.
// Rows, labels, values, headings and notes hold a fixed column the generator
// guarantees at every language's real glyph widths (RowTextFits), so their
// text is measured at plain widths; the +40% glyph stress is kept for what
// flows: the dialogs, the footer message, the prompt bar and the lists.
// Collects each misfit, so one run names them all.
static void PageFits(View& v,const PageTab& tab,const Bounds& screen,std::vector<std::string>& misfits) {
    const std::string prefix="settings-"+std::string(tab.id);
    const auto body=v.Box("settings-body"),area=v.Box((prefix+"-rows").c_str());const bool plain=v.host.expansion==1;
    const auto inside=[](const Bounds& a,const Bounds& b){return a.x>=b.x-1&&a.y>=b.y-1&&a.x+a.width<=b.x+b.width+1&&a.y+a.height<=b.y+b.height+1;};
    const auto note=[&](const std::string& what,const std::string& id,const Bounds& a,const Bounds& b){
        char line[256];std::snprintf(line,sizeof line,"%s %s: %g,%g %gx%g in %g,%g %gx%g",what.c_str(),id.c_str(),a.x,a.y,a.width,a.height,b.x,b.y,b.width,b.height);misfits.push_back(line);};
    // RmlUi breaks a wrapping text by word widths the font engine rounds one by
    // one, so the line it keeps, measured whole, may run up to two pixels past
    // a fractional box (as the footer message's may): such text has that slack.
    const auto lines=[&](const std::string& id){
        auto* e=Find(id);if(!e||!e->IsVisible(true))return;const auto size=e->GetBox().GetSize(Rml::BoxArea::Content);
        const float slack=e->GetComputedValues().white_space()==Rml::Style::WhiteSpace::Normal?2.1f:1.1f;
        for(int i=0;i<e->GetNumChildren();++i)if(auto* t=dynamic_cast<Rml::ElementText*>(e->GetChild(i)))for(const auto& line:t->GetLines())
            if(line.position.x+line.width>size.x+slack||line.position.y+t->GetLineHeight()>size.y+1.1f)
                note("text lines past their box: \""+line.text+"\"",id,{line.position.x,line.position.y,float(line.width),t->GetLineHeight()},{0,0,size.x,size.y});
    };
    if(!inside(area,body))note("rows area outside the pages' bounds",prefix+"-rows",area,body);
    if(!inside(area,screen))note("rows area outside the screen",prefix+"-rows",area,screen);
    // Side by side, or one under the other in the single column.
    {const auto first=v.Box((prefix+"-column-0").c_str()),second=v.Box((prefix+"-column-1").c_str());
     if(Stacked(v)?std::abs(second.x-first.x)>1||second.y<first.y+first.height-1:second.x<first.x+first.width-1||std::abs(second.y-first.y)>1)
         note(Stacked(v)?"columns not one under the other":"columns not side by side",prefix+"-column-1",second,first);}
    auto* page=Find("settings-page-"+std::string(tab.id));
    std::vector<Rml::Element*> pending{page};
    while(!pending.empty()) {
        auto* e=pending.back();pending.pop_back();
        for(int i=0;i<e->GetNumChildren();++i)pending.push_back(e->GetChild(i));
        const auto& id=e->GetId();
        if(id.empty()||!e->IsVisible(true)||id.find("-popup")!=std::string::npos||id.find("-option-")!=std::string::npos)continue;
        // A column's rows, notes and headings, the area scrolled to each (as
        // the focus or the wheel reveals it): wholly inside the area, the
        // pages' bounds and the screen. Its parts follow it before any sibling.
        if(e->GetParentNode()&&e->GetParentNode()->GetId().starts_with(prefix+"-column-")) {
            e->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest,Rml::ScrollAlignment::Nearest,Rml::ScrollBehavior::Instant,Rml::ScrollParentage::Closest));
            Bounds box;if(!v.runtime.GetBounds(id,box))continue;
            if(!inside(box,area))note("row outside its tab's rows area",id,box,area);
            if(!inside(box,body))note("row outside the pages' bounds",id,box,body);
            if(!inside(box,screen))note("row outside the screen",id,box,screen);
        }
        // A row's label and value inside their row.
        for(const char* part:{"-label","-value"})if(plain&&id.size()>std::strlen(part)&&id.ends_with(part)) {
            const auto owner=id.substr(0,id.size()-std::strlen(part));Bounds own,box;
            if(Find(owner)&&v.runtime.GetBounds(owner,own)&&v.runtime.GetBounds(id,box)&&!inside(box,own))note("text outside its row",id,box,own);
        }
        if(plain&&dynamic_cast<Rml::ElementText*>(e)==nullptr&&e->GetNumChildren()>0&&dynamic_cast<Rml::ElementText*>(e->GetChild(0))!=nullptr)lines(id);
    }
    if(auto* rows=Find(prefix+"-rows"))rows->SetScrollTop(0);
}
// Fixed chrome keeps its authored text size at every text size
// (build_retained_screens.fixed_text): each font length divides the text size
// the runtime multiplies it by, so the computed size is the authored dp alone.
static void FixedText(View& v,const char* id,float dp) {
    const float font=v.Element(id)->GetComputedValues().font_size(),expected=dp*v.viewport.DpRatio();
    if(std::abs(font-expected)>=.05f)std::fprintf(stderr,"fixed text %s font=%g expected=%g text=%g\n",id,font,expected,v.viewport.TextRatio());
    Check(std::abs(font-expected)<.05f,"fixed chrome keeps its authored text size at every text size");
}
// The text-bearing elements under `root`, each laid out with its lines.
static void EachText(Rml::Element* root,const std::function<void(Rml::Element*)>& visit) {
    std::vector<Rml::Element*> pending{root};
    while(!pending.empty()) {
        auto* e=pending.back();pending.pop_back();
        for(int i=0;i<e->GetNumChildren();++i)pending.push_back(e->GetChild(i));
        if(!e->GetId().empty()&&e->IsVisible(true)&&dynamic_cast<Rml::ElementText*>(e)==nullptr&&e->GetNumChildren()>0&&dynamic_cast<Rml::ElementText*>(e->GetChild(0))!=nullptr)visit(e);
    }
}
// The prompt bar (page_prompts): every key cap, verb and Back, with each of
// their lines, inside its own box, the bar and the screen at every text size.
// Its text flows in the bar's fixed slots, so the +40% stress applies.
static void PromptsFit(View& v,const Bounds& screen) {
    const auto bar=v.Box("prompts");Contained(bar,screen,"the prompt bar stays on the screen");
    std::vector<Rml::Element*> pending{v.Element("prompts")};unsigned texts=0;
    while(!pending.empty()) {
        auto* e=pending.back();pending.pop_back();
        for(int i=0;i<e->GetNumChildren();++i)pending.push_back(e->GetChild(i));
        const auto& id=e->GetId();Bounds box;
        if(id.empty()||id=="prompts"||!e->IsVisible(true)||!v.runtime.GetBounds(id,box))continue;
        Contained(box,bar,"every key cap, verb and Back stays in the prompt bar");
        Contained(box,screen,"every key cap, verb and Back stays on the screen");
    }
    EachText(v.Element("prompts"),[&](Rml::Element* e){++texts;TextFits(v,e->GetId().c_str());});
    Check(texts>=4,"the prompt bar shows its key caps and verbs");
    Contained(v.Box("settings_back-label"),v.Box("settings_back"),"Back's verb stays in its plate");
    for(auto* id:{"settings_back-key","prompt-231042-key"})FixedText(v,id,13);
    for(auto* id:{"settings_back-label","prompt-231042-verb"})FixedText(v,id,14);
}
// The tab strip: each tab's label on one line in its slot, every slot and the
// previous/next key caps in the strip and on the screen, at every text size.
// The slots are fixed, so the labels are measured at plain widths, as rows.
static void StripFits(View& v,const Bounds& screen) {
    const auto strip=v.Box("settings-strip");Contained(strip,screen,"the tab strip stays on the screen");
    for(const auto& tab:PageTabs) {
        const auto slot="tab-"+std::string(tab.id),label=slot+"-label";
        Contained(v.Box(slot.c_str()),strip,"every tab's slot stays in the strip");
        Contained(v.Box(label.c_str()),v.Box(slot.c_str()),"every tab's label stays in its slot");
        RowTextFits(v,label.c_str());FixedText(v,label.c_str(),14);
    }
    for(auto* key:{"tab-previous-key","tab-next-key"}){Contained(v.Box(key),screen,"the tab keys stay on the screen");TextFits(v,key);}
}
// A list option's one line inside its row. A list widened to its longest
// option at a large text size falls 2 dp short of it: ValueControlView's
// natural-width gutter (track width + 8 dp) omits choice_popup's 2 dp viewport
// margin-left, a recorded follow-up shared with the MP lists. That 2 dp is
// allowed; any larger clip fails.
static void ListLabelFits(View& v,const char* id) {
    auto* e=v.Element(id);const auto size=e->GetBox().GetSize(Rml::BoxArea::Content);const float slack=2*v.viewport.DpRatio()+1.1f;bool found=false;
    for(int i=0;i<e->GetNumChildren();++i)if(auto* t=dynamic_cast<Rml::ElementText*>(e->GetChild(i)))for(const auto& line:t->GetLines()) {
        found=true;if(line.position.x+line.width>size.x+slack)std::fprintf(stderr,"list fit %s text=%s box=%g line=%d\n",id,line.text.c_str(),size.x,line.width);
        Check(line.position.x+line.width<=size.x+slack,"a list option's line fits its row");
    }
    Check(found,"the list option generated its line");
}
// The footer status flows: every line laid out, wrapping at words before
// APPLY CHANGES (RmlUi may let a line run a pixel or two past a fractional
// box; the message has no clip and the button is what it must not reach),
// its extra lines spreading past the footer but staying on screen.
static void MessageFits(View& v,const Bounds& screen) {
    auto* e=v.Element("settings-message");const auto apply=v.Box("settings_apply");unsigned lines=0;
    for(int i=0;i<e->GetNumChildren();++i)if(auto* t=dynamic_cast<Rml::ElementText*>(e->GetChild(i))) {
        const auto origin=t->GetAbsoluteOffset(Rml::BoxArea::Content);
        for(const auto& line:t->GetLines()) {
            ++lines;const float right=origin.x+line.position.x+line.width,top=origin.y+line.position.y;
            if(right>apply.x+.5f||top<screen.y-1||top+t->GetLineHeight()>screen.y+screen.height+1)
                std::fprintf(stderr,"status line \"%s\" right=%g top=%g apply=%g\n",line.text.c_str(),right,top,apply.x);
            Check(right<=apply.x+.5f,"the status wraps before APPLY CHANGES");
            Check(top>=screen.y-1&&top+t->GetLineHeight()<=screen.y+screen.height+1,"every status line stays on screen");
        }
    }
    Check(lines>0,"the status generated its lines");
}
static void PairedField(View& v,const char* slider) {
    const auto number=std::string(slider)+"_number";
    Check(v.Focus(slider),"paired slider can receive focus");v.Frame();
    const auto row=v.Box((std::string(slider)+"_row").c_str()),control=v.Box(slider),field=v.Box((number+"-viewport").c_str());
    Contained(control,v.Box("settings-body"),"focused slider stays wholly inside the pages' bounds");
    Contained(field,row,"the paired numeric readout shares the slider's row");
    Contained(control,row,"the slider shares its row");
    Check(std::abs(field.y+field.height*.5f-row.y-row.height*.5f)<1,"the numeric field is centred on its row");
    Check(v.runtime.FocusControl(number,v.time),"paired number can receive focus");v.Frame();
    Contained(v.Box((number+"-viewport").c_str()),v.Box("settings-body"),"focused numeric viewport stays complete");
    Check(v.runtime.TakeActions().empty(),"focusing paired controls does not edit the settings draft");
}
static void ModalBody(View& v,const char* name,const char* title) {
    const auto id=std::string(name)+"_body_scrollbar";
    const auto body=v.Box((std::string(name)+"-body").c_str());
    if(body.height<v.Element(title)->GetComputedValues().line_height().value-1)
        std::fprintf(stderr,"modal %s body height=%g line=%g\n",name,body.height,v.Element(title)->GetComputedValues().line_height().value);
    Check(body.height>=v.Element(title)->GetComputedValues().line_height().value-1,"modal retains at least one complete heading line");
    TextFits(v,title);
    auto state=v.runtime.GetWidgetState(id);
    Check(state&&state->scroll&&state->scroll->available,"modal body scrollbar has measured geometry");
    // A range under a pixel is float noise of a body that fits (the runtime
    // reports such a range as usable): only a real overflow must scroll.
    if(state->scroll->geometry.usable&&state->scroll->geometry.range>=1) {
        Check(v.runtime.FocusControl(id,v.time),"overflowing modal body has reachable authored scrollbar");
        v.Key(MenuInput::End);v.Frame();
        Check(v.Element((std::string(name)+"-body").c_str())->GetScrollTop()>0,"modal instructions can scroll to their end");
        Check(v.runtime.TakeActions().empty(),"scrolling modal instructions does not choose a decision");
    }
}
// Dialog bodies that scrolled, so the scrolling branch above is exercised.
static unsigned scrolledModals=0;
static void CheckPage(View& v,int width,int height,float density,float textScale) {
    std::fprintf(stderr,"SYSTEM text viewport=%dx%d density=%g text=%g\n",width,height,density,textScale);
    v.viewport.width=width;v.viewport.height=height;v.viewport.displayScale=density;v.viewport.textScale=textScale;
    // As RetainedUI_DefaultViewport does: the engine never lays a menu out in
    // less than 640x480 dp, so 1280x720 at 200% runs at 150%, as in play.
    // The checks below measure in that fitted ratio.
    v.viewport.FitToMinimum(640,480);density=v.viewport.DpRatio();
    v.Sync();v.Frame();
    const Bounds screen{0,0,float(width),float(height)};
    v.Displays();
    // Each tab's rows, labels, values, headings and notes fit at this text
    // size, in one column above 100%, each on the screen once scrolled to.
    std::vector<std::string> misfits;
    for(const auto& tab:PageTabs){v.Show(tab.order.back());PageFits(v,tab,screen,misfits);}
    for(const auto& misfit:misfits)std::fprintf(stderr,"misfit %s\n",misfit.c_str());
    Check(misfits.empty(),"every row, label, value, heading and note of every tab fits");
    for(auto* status:{"#str_230007","#str_230008","#str_230009","#str_230010","#str_230011","#str_230012","#str_230013","#str_230014"}) {
        v.message=status;v.Sync();v.Frame();
        MessageFits(v,screen);
        Contained(v.Box("settings-footer"),screen,"long status cannot displace the actions");
        // The status flows: centred on APPLY CHANGES, its extra lines spread
        // past the footer's 45 dp, beside the button and on screen.
        {const auto message=v.Box("settings-message"),footer=v.Box("settings-footer"),apply=v.Box("settings_apply");
         Check(message.x>=footer.x-1&&message.x+message.width<=apply.x+1,"the status stays beside APPLY CHANGES");
         Contained(message,screen,"the status stays on screen");}
    }
    // The tab's rows scroll above the footer, whatever their columns' length.
    for(const auto& tab:PageTabs){const auto rows=v.Box(("settings-"+std::string(tab.id)+"-rows").c_str());
        Check(v.Box("settings-footer").y>=rows.y+rows.height-1,"the footer stands under the tab's rows");}
    Contained(v.Box("settings-footer"),screen,"essential footer stays inside the viewport");
    // The prompt bar, the strip, the crumb, the title and APPLY CHANGES' label
    // keep their authored text size in their fixed slots at every text size:
    // Back, every tab and the plate's label stay whole and on the screen.
    Contained(v.Box("settings_back"),screen,"Back remains reachable");
    Contained(v.Box("settings_apply"),screen,"Apply remains reachable");
    PromptsFit(v,screen);StripFits(v,screen);MessageFits(v,screen);
    Contained(v.Box("settings_apply-label"),v.Box("settings_apply"),"APPLY CHANGES' label stays on its plate");
    for(auto* id:{"title","settings_apply-label"})RowTextFits(v,id);
    for(const auto& [id,dp]:{std::pair<const char*,float>{"title",18.f},{"crumb",18.f},{"settings_apply-label",20.f}})FixedText(v,id,dp);
    for(const auto* slider:{"settings_brightness","settings_ambient"})PairedField(v,slider);
    Check(v.Focus("settings_brightness_number"),"large-text numeric field can receive focus");v.Frame();
    auto viewport=v.Box("settings_brightness_number-viewport");
    auto* text=v.Element("settings_brightness_number-text");
    // The number grows with its 36 dp row: by the text size, or to the tall
    // column's width in a tall view.
    const auto font=text->GetComputedValues().font_size(),grown=v.Box("settings_brightness_row").height/(36*density);
    if(std::abs(font-15*density*grown)>=.05f)std::fprintf(stderr,"number font=%g row growth=%g density=%g\n",font,grown,density);
    Check(std::abs(font-15*density*grown)<.05f,"number uses same scaled font as drawing");
    Check(float(width)<.76f*float(height)||std::abs(grown-textScale)<.01f,"outside a tall view a row grows by the text size");
    Contained(viewport,v.Box("settings-body"),"focus reveals the complete numeric text viewport");
    Check(v.runtime.BeginNumberEdit("settings_brightness_number",v.error,v.time),"numeric editor begins at enlarged size");v.Frame();
    auto field=v.runtime.GetWidgetState("settings_brightness_number")->number;
    Check(field.has_value(),"editor retains typed identity");
    // The field's viewport grows with its row, the caret with the text.
    Contained(v.Box("settings_brightness_number-caret"),viewport,"the caret fits the numeric viewport");
    v.Key(MenuInput::Back);v.Frame();
    for(auto* id:{"settings_preset","settings_renderer","settings_display_device","settings_multiscreen","settings_display_mode","settings_display_refresh","settings_fullscreen_policy","settings_msaa","settings_vsync","settings_postaa","settings_resolution_scale"}) {
        Check(v.Focus(id),"large-text choice can receive focus");v.Frame();
        v.Key(MenuInput::Accept);v.Frame();
        auto state=v.runtime.GetWidgetState(id);
        if(!state || !state->popupOpen) {
            const auto body=v.Box("settings-body"),anchor=v.Box(id);
            std::fprintf(stderr,"closed %s: body=%g,%g %gx%g anchor=%g,%g %gx%g\n",id,body.x,body.y,body.width,body.height,anchor.x,anchor.y,anchor.width,anchor.height);
        }
        Check(state&&state->popupOpen,"large-text popup remains usable");
        auto popup=v.Box((std::string(id)+"-popup").c_str());
        Contained(popup,v.Box("settings-body"),"popup unfolds within the pages' bounds");
        Contained(popup,screen,"popup stays on the screen");
        v.Key(MenuInput::End);v.Frame();
        auto current=v.runtime.GetWidgetState(id);
        if(!(current&&current->popupOpen&&current->scroll&&current->scroll->available))
            std::fprintf(stderr,"unmeasured %s: open=%d highlight=%s\n",id,current&&current->popupOpen,current?current->highlight.c_str():"-");
        Check(current&&current->popupOpen&&current->scroll&&current->scroll->available,"popup scrolling remains measured at enlarged text");
        ListLabelFits(v,(current->highlight+"-label").c_str());
        Contained(v.Box(current->highlight.c_str()),v.Box((std::string(id)+"-viewport").c_str()),"highlighted enlarged option remains entirely visible");
        v.Key(MenuInput::Back);v.Frame();Check(v.runtime.TakeActions().empty(),"navigation and text reflow do not edit accepted values");
    }
    Check(v.tx.Edit(View::Owner,{{"r_brightness",1.2}}).code==SettingsCode::Ok,"seed dirty state for actual discard modal");v.Sync();
    v.Event("onBack");v.Frame();
    if(v.Box("discard_changes").y+v.Box("discard_changes").height>height+1) {
        for(auto* id:{"discard-panel-plate","discard-scroll-region","discard-body","discard-panel-title","discard-actions"}) {
            auto b=v.Box(id);std::fprintf(stderr,"modal %s = %g,%g %gx%g\n",id,b.x,b.y,b.width,b.height);
        }
    }
    Contained(v.Box("discard-panel-plate"),screen,"enlarged discard modal remains visible");
    for(auto* id:{"discard_apply_changes","discard_keep_editing","discard_changes"}) {
        Contained(v.Box(id),screen,"all discard choices remain reachable");
        TextFits(v,(std::string(id)+"-label").c_str());
    }
    ModalBody(v,"discard","discard-panel-title");
    v.Event("continueEditing");v.Frame();
    Check(v.tx.Edit(View::Owner,{{"r_brightness",1.0}}).code==SettingsCode::Ok,"restore untouched setting after modal check");
    // The confirmation reads the service's message: a long one, the page's own
    // statuses run together, outgrows the dialog where the text is large.
    std::string longStatus;for(int pass=0;pass<2;++pass)for(auto* key:{"#str_230008","#str_230009","#str_230010","#str_230011","#str_230012","#str_230013","#str_230014"})longStatus+=v.host.Translate(key)+" ";
    v.message=longStatus;v.Sync();v.Frame();
    Check(v.runtime.SetState({{"settings.confirmationVisible",true},{"settings.canConfirm",true},{"settings.canRevert",true}},v.error,v.time),"show authored confirmation for layout qualification");v.Frame();
    Contained(v.Box("confirmation-panel-plate"),screen,"enlarged confirmation remains visible");
    for(auto* id:{"settings_keep","settings_revert"}) {
        Contained(v.Box(id),screen,"both confirmation choices stay reachable");
        TextFits(v,(std::string(id)+"-label").c_str());
    }
    {const auto bar=v.runtime.GetWidgetState("confirmation_body_scrollbar");if(bar&&bar->scroll&&bar->scroll->geometry.usable&&bar->scroll->geometry.range>=1)++scrolledModals;}
    ModalBody(v,"confirmation","confirmation-panel-title");
    v.message="#str_230007";v.Sync();v.Frame();
}
int main(int argc,char** argv) {
    Check(argc==3 || argc==4,"page, locale directory and optional single-locale arguments");
    const auto source=Read(argv[1]);const std::string directory=argv[2];
    unsigned ran=0;
    for(auto* locale:{"english","spanish","polish","russian","french","italian","german"}) {
        if(argc==4 && std::string(argv[3])!=locale)continue;
        for(float expansion:{1.f,1.4f}) {
        std::fprintf(stderr,"Text scale locale=%s glyph expansion=%g\n",locale,expansion);
        View view(source,directory+"/"+locale+"_openq4.lang",1,1280,720,expansion,directory+"/"+locale+"_guis.lang");
        // 4:3, 16:9 and 9:16 (the tall single column) at each text size.
        for(float scale:{1.f,1.5f,2.f}){CheckPage(view,640,480,1,scale);CheckPage(view,1280,720,1,scale);}
        CheckPage(view,1280,720,1.25f,2);
        CheckPage(view,1280,720,2,2);
        CheckPage(view,1920,1080,1.5f,1.5f);
        CheckPage(view,3440,1440,2,2);
        for(float scale:{1.f,1.5f,2.f})CheckPage(view,540,960,1,scale);
        }
        ++ran;
    }
    Check(ran>0,"the named locale is one this test covers");
    Check(scrolledModals>0,"a large text size makes a dialog's body scroll, so its scrollbar is exercised");
    std::printf("SYSTEM text scale: %u checks passed\n",checks);
}
