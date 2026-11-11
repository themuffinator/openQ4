// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#define main LegacyValueRuntimeMain
#include "UiValueRuntimeTest.cpp"
#undef main
#include <fstream>

static Json::Value ScrollChoice() {
    auto source=Parse(Source());auto* choice=BoxFrameNode(source["root"],"choice");
    auto& bar=(*choice)["control"]["scrollbar"];bar["track"]="choice-track";bar["thumb"]="choice-thumb";
    bar["minimumThumb"]=24;bar["lineStep"]=36;
    BoxFrameNode(source["root"],"popup")->operator[]("properties")["display"]=Typed("keyword","none");
    auto track=Box("choice-track",144,0,36,80);auto thumb=Box("choice-thumb",10,0,16,24);
    Colour(track,{.11,.22,.33,1});Colour(thumb,{.9,.8,.7,1});track["children"].append(thumb);
    BoxFrameNode(source["root"],"popup")->operator[]("children").append(track);
    return source;
}
static void ChoiceBar(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f}) {
        View view(host,Text(ScrollChoice()));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=ratio;view.Frame();
        Check(view.runtime.FocusControl("choice",view.time),"focus bar choice");view.Key(MenuInput::Accept);view.Frame();
        auto w=view.Widget("choice");
        Check(w.popupOpen&&w.scroll&&w.scroll->available&&w.scroll->geometry.usable,"first frame publishes authored popup scrollbar");
        auto track=view.BoxOf("choice-track"),thumb=view.BoxOf("choice-thumb"),viewport=view.BoxOf("viewport");
        Check(Near(thumb.height,w.scroll->geometry.thumb)&&Near(track.height,w.scroll->geometry.track),"actual thumb and track match readback");
        Check(viewport.x+viewport.width<=track.x-7.9*ratio,"authored gutter remains outside row clipping");
        auto accepted=w.accepted;auto highlight=w.highlight;
        view.Point(track.x+track.width*.5f,track.y+track.height-2);view.runtime.PointerButton(true,view.time);view.runtime.PointerButton(false,view.time);view.Frame();
        w=view.Widget("choice");Check(w.popupOpen&&w.popupOffsetDp>0&&w.highlight==highlight,"track pages locally without closing or changing highlight");
        Check(w.accepted==accepted&&view.runtime.TakeActions().empty(),"track release cannot activate a row");
        const auto offset=w.popupOffsetDp;view.Frame();Check(Near(view.Widget("choice").popupOffsetDp,offset),"ordinary paint does not reveal stale highlight");
        view.runtime.PointerWheel(1,view.time);view.Frame();Check(view.Widget("choice").popupOffsetDp>offset,"bar wheel scrolls rows without selection");
        Check(view.Widget("choice").highlight==highlight,"wheel preserves highlighted option");
        view.Key(MenuInput::Home);view.Frame();Check(Near(view.Widget("choice").popupOffsetDp,0),"explicit Home reveals first eligible option");
        track=view.BoxOf("choice-track");thumb=view.BoxOf("choice-thumb");
        view.Point(thumb.x+thumb.width/2,thumb.y+thumb.height*.25f);view.runtime.PointerButton(true,view.time);
        view.Point(track.x+track.width/2,track.y+track.height*.8f);view.Frame();
        Check(view.Widget("choice").popupOpen&&view.Widget("choice").popupOffsetDp>0,"thumb drag retains open popup");
        view.runtime.PointerButton(false,view.time);view.Frame();Check(view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"thumb release has no option activation");
        const auto savedOffset=view.Widget("choice").popupOffsetDp;std::string saved,error;
        Check(view.runtime.SaveSnapshot(saved,error,view.time),"save continuous popup offset");auto json=Parse(saved);
        Check(json["widgets"]["version"].asUInt()==3,"bar choice selects nested widget version three");
        view.Key(MenuInput::Back);view.Frame();Check(!view.Widget("choice").popupOpen,"Back closes popup");
        Check(view.runtime.RestoreSnapshot(saved,error,view.time),"restore bar choice snapshot");view.Frame();
        Check(!view.Widget("choice").popupOpen,"snapshot does not reopen popup or drag");
        Check(view.runtime.FocusControl("choice",view.time),"focus restored bar choice");view.Key(MenuInput::Accept);view.Frame();
        Check(Near(view.Widget("choice").popupOffsetDp,savedOffset),"first restored opening retains logical offset");
        const auto densityOffset=view.Widget("choice").popupOffsetDp;view.viewport.displayScale=ratio==2?1.f:2.f;view.Frame();
        Check(Near(view.Widget("choice").popupOffsetDp,densityOffset),"live density retains logical popup offset");
    }
}
static void FramesAndLifecycle(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f})for(bool borderBox:{false,true}) {
        auto source=ScrollChoice();auto* thumb=BoxFrameNode(source["root"],"choice-thumb");
        (*thumb)["properties"]["box-sizing"]=Typed("keyword",borderBox?"border-box":"content-box");
        (*thumb)["properties"]["border-width"]=Typed("length",10,"dp");
        (*thumb)["properties"]["padding-top"]=Typed("length",9,"dp");(*thumb)["properties"]["padding-bottom"]=Typed("length",11,"dp");
        (*thumb)["properties"]["margin-top"]=Typed("length",3,"dp");
        for(unsigned i=0;i<6;++i) {
            const auto id="option-"+std::to_string(i);source["aliases"][id+"::rect"]["node"]=id;source["aliases"][id+"::rect"]["property"]="rect";
        }
        View view(host,Text(source));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=ratio;view.Frame();
        Check(view.runtime.FocusControl("choice",view.time),"focus framed popup");view.Key(MenuInput::Accept);view.Frame();
        auto w=view.Widget("choice");Check(w.scroll&&w.scroll->available,"framed popup has current readback");
        const double frameMinimum=2*std::ceil(10*ratio)+20*ratio;
        Check(Near(w.scroll->geometry.thumb,frameMinimum)&&Near(view.BoxOf("choice-thumb").height,frameMinimum),"thumb minimum includes full border and padding in both box models");
        auto track=view.BoxOf("choice-track"),knob=view.BoxOf("choice-thumb");
        view.Point(knob.x+knob.width/2,knob.y+knob.height*.25f);view.runtime.PointerButton(true,view.time);
        view.Point(track.x+track.width/2,track.y+track.height*.65f);view.Frame();
        w=view.Widget("choice");double expected=0;
        Check(ScrollDragOffset(w.scroll->geometry,w.scroll->geometry.track*.65,.25,expected)&&Near(w.popupOffsetDp,expected/ratio),"actual framed drag preserves quarter-thumb grab");
        std::string error;
        for(unsigned i=0;i<6;++i) {
            const auto id="option-"+std::to_string(i);const auto rect="0 "+std::to_string(i*60)+" 180 60";
            Check(view.runtime.SetPresentationAlias(id+"::rect",rect,true,error),"resize actual authored row while dragging");
        }
        view.Frame();w=view.Widget("choice");
        Check(w.popupOpen&&w.scroll&&w.scroll->available&&Near(w.scroll->geometry.viewport,120*ratio),"first resize frame measures actual rows");
        Check(ScrollDragOffset(w.scroll->geometry,track.y+track.height*.65-view.BoxOf("choice-track").y,.25,expected)&&Near(w.popupOffsetDp,expected/ratio),"resize retains original grab fraction and current pointer coordinate");
        view.runtime.PointerButton(false,view.time);view.Frame();Check(view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"resized drag release remains local");
        // Captured source invalidation never retargets a release to the new value.
        knob=view.BoxOf("choice-thumb");view.Point(knob.x+knob.width/2,knob.y+knob.height/2);view.runtime.PointerButton(true,view.time);
        view.State({{"choice",4.0}});view.Frame();view.runtime.PointerButton(false,view.time);
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"changed authoritative choice source cancels original popup gesture");
        view.Key(MenuInput::Accept);view.Frame();knob=view.BoxOf("choice-thumb");view.Point(knob.x+knob.width/2,knob.y+knob.height/2);view.runtime.PointerButton(true,view.time);
        view.viewport.width=0;view.Frame();view.runtime.PointerButton(false,view.time);view.viewport.width=1280;view.Frame();
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"invalid viewport cancels capture without restoring gesture");
    }
    {
        auto source=ScrollChoice();auto* thumb=BoxFrameNode(source["root"],"choice-thumb");
        (*thumb)["properties"]["border-width"]=Typed("length",50,"dp");
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        auto w=view.Widget("choice");Check(w.scroll&&!w.scroll->available,"frame larger than track refuses geometry");
        auto track=view.BoxOf("choice-track");view.Click(track.x+10,track.y+10);view.runtime.PointerWheel(1,view.time);view.Frame();
        Check(view.Widget("choice").popupOpen&&Near(view.Widget("choice").popupOffsetDp,0)&&view.runtime.TakeActions().empty(),"oversized scrollbar is inert without native replacement artwork");
    }
}
static void Schema() {
    const auto source=ScrollChoice();Document document;std::vector<Diagnostic> diagnostics;
    Check(document.Load(Text(source),diagnostics),"canonical authored choice bar schema accepts");const auto original=document.Source();
    for(unsigned fault=0;fault<13;++fault) {
        auto bad=source;auto* choice=BoxFrameNode(bad["root"],"choice");auto& bar=(*choice)["control"]["scrollbar"];
        auto* track=BoxFrameNode(bad["root"],"choice-track");auto* thumb=BoxFrameNode(bad["root"],"choice-thumb");
        switch(fault) {
            case 0:bar["action"]="select";break;
            case 1:bar["lineStep"]=0;break;
            case 2:bar["minimumThumb"]=4097;break;
            case 3:bar["track"]="viewport";break;
            case 4:bar["thumb"]="choice-track";break;
            case 5:(*thumb)["properties"]["transform"]=Typed("transform",Array({0,0,1,1,2}),"dp");break;
            case 6:(*thumb)["properties"]["max-height"]=Typed("length",90,"dp");break;
            case 7:(*track)["properties"].removeMember("left");break;
            case 8:bad["aliases"]["thumb::rect"]["node"]="choice-thumb";bad["aliases"]["thumb::rect"]["property"]="rect";break;
            case 9:PropertyTimeline(bad,"bad-height","choice-thumb","height",Typed("length",24,"dp"),Typed("length",40,"dp"),100);break;
            case 10:(*thumb)["control"]=(*choice)["control"];break;
            case 11:{auto removed=(*track)["children"][0];(*track)["children"].clear();BoxFrameNode(bad["root"],"popup")->operator[]("children").append(removed);break;}
            case 12:{Json::Value binding;binding["id"]="collision";binding["node"]="choice-thumb";binding["property"]="height";binding["value"]=15;bad["bindings"].append(binding);break;}
        }
        Check(!document.Load(Text(bad),diagnostics)&&!diagnostics.empty(),"invalid local ownership or derived geometry refuses");
        Check(document.Source()==original,"failed scrollbar schema replacement keeps original document");
    }
}
static Rml::Vector2f QuadPoint(Rml::Element* element,float across,float along) {
    Rml::Array<Rml::Vector2f,4> quad;Check(Rml::ElementUtilities::GetBorderBoxQuad(quad,element),"actual transformed part quad is available");
    return quad[0]+(quad[1]-quad[0])*across+(quad[3]-quad[0])*along;
}
static void TransformedBar(TestHost& host) {
    auto source=ScrollChoice();auto* trackNode=BoxFrameNode(source["root"],"choice-track");
    (*trackNode)["properties"]["transform"]=Typed("transform",Array({0,0,1.2,.9,15}),"dp");
    View view(host,Text(source));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=1.25f;view.Frame();
    view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
    auto* document=Rml::GetContext(0)->GetDocument(0);auto* track=document->GetElementById("choice-track");auto* thumb=document->GetElementById("choice-thumb");
    auto point=QuadPoint(thumb,.5f,.25f);view.Point(point.x,point.y);view.runtime.PointerButton(true,view.time);
    point=QuadPoint(track,.5f,.75f);view.Point(point.x,point.y);view.Frame();
    auto w=view.Widget("choice");double expected=0;Check(w.scroll&&w.scroll->available,"transformed track has fresh geometry");
    Check(ScrollDragOffset(w.scroll->geometry,w.scroll->geometry.track*.75,.25,expected)&&Near(w.popupOffsetDp,expected/w.scroll->dpRatio),"composed inverse projection matches quarter-grab drag");
    view.runtime.PointerButton(false,view.time);view.Frame();Check(view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"transformed release does not activate row");
    const auto old=w.popupOffsetDp;
    Check(view.runtime.PlayTimeline("move-choice-parent",view.time),"move choice anchor while popup stays open");view.time+=.04;view.Frame();
    Check(Near(view.Widget("choice").popupOffsetDp,old),"moving portal anchor does not reset logical offset");
    auto saved=view.Widget("choice");std::string error;Check(view.runtime.SetPresentationAlias("parent::noevents","1",true,error),"suspend canonical popup owner");view.Frame();
    Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"hidden or suspended canonical owner retires popup hit authority");
}
static void LeadingRows(TestHost& host) {
    auto source=ScrollChoice();for(unsigned i=0;i<6;++i)BoxFrameNode(source["root"],("option-"+std::to_string(i)).c_str())->operator[]("properties")["top"]=Typed("length",10+i*40,"dp");
    View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
    auto w=view.Widget("choice");Check(w.scroll&&w.scroll->available&&Near(w.scroll->geometry.viewport,90),"actual leading row space is included in requested visible extent");
    auto viewport=view.BoxOf("viewport"),last=view.BoxOf("option-1");
    Check(last.y+last.height<=viewport.y+viewport.height+.05,"second row is wholly inside measured popup clipping");
}
static void PopupFrames(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f})for(bool borderBox:{false,true})for(int offset:{0,14}) {
        auto source=ScrollChoice();
        for(const auto* id:{"popup","viewport"}) {
            auto& p=(*BoxFrameNode(source["root"],id))["properties"];
            p["box-sizing"]=Typed("keyword",borderBox?"border-box":"content-box");
            p["border-width"]=Typed("length",3,"dp");
            p["padding-top"]=Typed("length",7,"dp");p["padding-bottom"]=Typed("length",11,"dp");
            p["padding-left"]=Typed("length",5,"dp");p["padding-right"]=Typed("length",9,"dp");
        }
        (*BoxFrameNode(source["root"],"viewport"))["properties"]["top"]=Typed("length",offset,"dp");
        View view(host,Text(source));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=ratio;view.Frame();
        view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        auto* document=Rml::GetContext(0)->GetDocument(0);auto* viewport=document->GetElementById("viewport");
        auto check=[&](const char* rowId) {
            auto w=view.Widget("choice");Check(w.scroll&&w.scroll->available,"framed popup first frame is current");
            const auto origin=viewport->GetAbsoluteOffset(Rml::BoxArea::Padding),extent=viewport->GetBox().GetSize(Rml::BoxArea::Padding);
            const auto track=view.BoxOf("choice-track"),row=view.BoxOf(rowId),popup=view.BoxOf("popup");
            Check(Near(w.scroll->geometry.viewport,extent.y)&&Near(track.y,origin.y)&&Near(track.height,extent.y),"track and readback match actual viewport padding clip");
            Check(row.y>=origin.y-.05&&row.y+row.height<=origin.y+extent.y+.05,"highlighted end row fits actual padded viewport");
            Check(track.y>=popup.y&&track.y+track.height<=popup.y+popup.height+.05,"framed popup contains the whole track");
            Rml::Rectanglei clip;Check(Rml::ElementUtilities::GetClippingRegion(document->GetElementById(rowId),clip),"Rml produces actual row scissor");
            Check(row.y>=clip.Top()-.51&&row.y+row.height<=clip.Bottom()+.51,"revealed row remains within actual snapped scissor");
        };
        check("option-0");view.Key(MenuInput::End);view.Frame();check("option-5");
        view.Key(MenuInput::Home);view.Frame();check("option-0");
    }
    auto source=ScrollChoice();(*BoxFrameNode(source["root"],"viewport"))["properties"]["top"]=Typed("length",-4,"dp");
    View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
    auto w=view.Widget("choice");Check(!w.scroll||!w.scroll->available,"viewport before popup padding is unavailable");
    view.runtime.PointerWheel(1,view.time);view.Frame();Check(Near(view.Widget("choice").popupOffsetDp,0)&&view.runtime.TakeActions().empty(),"invalid popup offset never admits local input");
}
static void ChangedRowTransform(TestHost& host) {
    View view(host,Text(ScrollChoice()));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
    const auto before=view.Widget("choice");auto* context=Rml::GetContext(0);auto* row=context->GetDocument(0)->GetElementById("option-5");
    const auto box=row->GetBox().GetSize(Rml::BoxArea::Border);
    Check(row->SetProperty("transform","translateY(20px)"),"actual row-only transform changes after paint");
    context->Update();context->GetRootElement()->UpdateGeometryForProjection();
    Check(row->GetBox().GetSize(Rml::BoxArea::Border)==box,"row transform leaves untransformed layout box unchanged");
    Check(!view.runtime.ScrollChoicePopup("choice",before.popupToken,ScrollStep::End,view.time),"semantic scroll refuses stale transformed row extent before paint");
    Check(Near(view.Widget("choice").popupOffsetDp,before.popupOffsetDp),"stale semantic intent preserves local offset");
    view.runtime.PointerWheel(1,view.time);
    Check(Near(view.Widget("choice").popupOffsetDp,before.popupOffsetDp),"changed transformed row invalidates the previously painted scroll extent");
    view.Frame();auto w=view.Widget("choice");Check(w.scroll&&w.scroll->available&&Near(w.scroll->geometry.range+w.scroll->geometry.viewport,260),"fresh paint publishes changed transformed extent");
    view.runtime.PointerWheel(1,view.time);view.Frame();Check(view.Widget("choice").popupOffsetDp>before.popupOffsetDp,"fresh transformed extent restores local input");
}
// The SYSTEM page reads sys_lang for its prompt glyphs, which the value
// fixture's host does not publish: its own counted host, otherwise alike.
struct SystemHost final : Host {
    unsigned errors=0; std::uint64_t frame=1;
    bool ReadFile(const std::string&,std::string&) override{return false;}
    bool ReadCVar(const std::string& name,size_t type,StateValue& value) override{
        if(name=="sys_lang"&&type==2){value=std::string("english");return true;}
        if(name=="ui_retainedSoftFocus"&&type==1){value=false;return true;}
        return false;
    }
    std::string Translate(const std::string& text) override{return text;}
    void Log(bool error,const std::string& text) override{if(error){++errors;std::fprintf(stderr,"Runtime: %s\n",text.c_str());}}
    std::uintptr_t LoadMaterial(const std::string&,int& width,int& height) override{width=height=256;return 1;}
    void Draw(const std::vector<Vertex>& values,const std::vector<int>& indices,std::uintptr_t) override{
        Check(indices.size()%3==0,"triangle output");for(auto index:indices)Check(index>=0&&static_cast<size_t>(index)<values.size(),"bounded geometry indices");
        for(auto value:values)Check(std::isfinite(value.x)&&std::isfinite(value.y),"finite widget geometry");
    }
    std::uint64_t RenderFrame() const override{return frame;}
    bool BeginLayer(std::uint32_t,int,int) override{return true;}
    void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&) override{}
    void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&) override{}
    void EndLayer(std::uint32_t) override{}
    FontMetrics GetFontMetrics(const std::string&,int size) override{return {size*.8f,size*.2f,size*1.2f,size*.5f};}
    Glyph GetGlyph(const std::string&,int size,std::uint32_t) override{return {size*.5f,0,-size*.8f,size*.5f,float(size),0,0,1,1,"font"};}
};
static void SystemCandidate(const std::string& source) {
    // The engine lays the page out on its 720 dp canvas: the view's size sets the dp ratio.
    // A 9:16 view takes the tall single column, its list rows a fractional 25.3105 px.
    for(const auto& [width,height]:{std::pair{1280,720},std::pair{1920,1080},std::pair{800,600},std::pair{540,960}}) {
        SystemHost host;Runtime runtime(host);std::vector<Diagnostic> diagnostics;std::string error;double time=1;
        Check(runtime.LoadDocument(source,"guis/menu/settings/system.rui",diagnostics),"SYSTEM candidate loads");
        Viewport viewport;viewport.width=width;viewport.height=height;viewport.canvasHeight=720;viewport.canvasMinWidth=960;
        const auto frame=[&]{time+=.01;++host.frame;runtime.Frame(viewport,time);Check(host.errors==0,"SYSTEM authored choice layout has no runtime errors");};
        const auto key=[&](MenuInput input){runtime.MenuAction(input,true,time);runtime.MenuAction(input,false,time);};
        const auto widget=[&](const std::string& id){auto result=runtime.GetWidgetState(id);Check(result.has_value(),"widget state available");return *result;};
        const auto box=[&](const std::string& id){Bounds result;Check(runtime.GetBounds(id,result),"authored part has real layout bounds");return result;};
        runtime.SetReducedMotion(true,time);
        Check(runtime.SetState({{"settings.open",true},{"settings.phase",1.},{"settings.busy",false},{"settings.confirmationVisible",false},{"settings.renderer.available",true},
            {"settings.render.canChoose",true},{"settings.msaaAvailable",true},
            {"settings.display.available",true},{"settings.display.spanAvailable",true},{"settings.display.count",2.},{"settings.display.optionCount",3.},
            {"settings.display.0.label",std::string("1: Primary monitor")},{"settings.display.1.label",std::string("2: Secondary monitor")},
            {"settings.display.mode.optionCount",4.},{"settings.display.mode.selected",0.},{"settings.display.mode.0.label",std::string("Desktop Native (1920 \xC3\x97 1080)")},{"settings.display.mode.1.label",std::string("1280 \xC3\x97 720 (16:9)")},{"settings.display.mode.2.label",std::string("2560 \xC3\x97 1080 (21:9)")},{"settings.display.mode.3.label",std::string("Custom (1600 \xC3\x97 900)")},{"settings.display.refresh.optionCount",3.},{"settings.display.refresh.selected",0.},{"settings.display.refresh.0.label",std::string("Auto")},{"settings.display.refresh.1.label",std::string("60 Hz")},{"settings.display.refresh.2.label",std::string("144 Hz")}},error,time),
            "SYSTEM editing state and display lists");frame();
        auto* document=Rml::GetContext(0)->GetDocument(0);
        unsigned scrolled=0;
        for(const auto* id:{"settings_preset","settings_backend","settings_gpu","settings_renderer","settings_ui_scale","settings_text_scale",
                            "settings_display_device","settings_multiscreen","settings_fullscreen_policy","settings_msaa","settings_vsync","settings_display_mode","settings_display_refresh",
                            "settings_postaa","settings_resolution_scale"}) {
            // A row acts only while its tab is current: run the tab_<id> event of the page holding it.
            std::string tab;for(auto* e=document->GetElementById(id);e;e=e->GetParentNode())if(e->GetId().starts_with("settings-page-")){tab=e->GetId().substr(14);break;}
            Runtime::EventEffects effects;Check(!tab.empty()&&runtime.RunEvent("tab_"+tab,time,effects,error),"SYSTEM choice's tab shows");for(int i=0;i<10;++i)frame();
            Check(runtime.FocusControl(id,time),"SYSTEM choice can receive explicit focus");frame();key(MenuInput::Accept);frame();
            auto w=widget(id);
            Check(w.popupOpen&&w.scroll&&w.scroll->available,"SYSTEM authored choice has first-frame measured scroll geometry");
            const auto thumb=box(std::string(id)+"-scroll-thumb");
            Check(Near(thumb.height,w.scroll->geometry.thumb),"SYSTEM editable vector thumb follows actual layout");
            auto* ink=document->GetElementById(std::string(id)+"-scroll-ink");
            Check(ink&&ink->GetTagName()=="q4-vector","SYSTEM scrollbar uses authored vector art");
            Check(!document->GetElementById(std::string(id)+"-popup-scroll-thumb")&&!document->GetElementById(std::string(id)+"-popup-scroll-base"),"the list's scrollbar is the shared list's track and thumb");
            const auto accepted=w.accepted;const auto highlight=w.highlight;
            if(w.scroll->geometry.usable) {
                ++scrolled;const double before=w.popupOffsetDp;runtime.PointerWheel(w.scroll->geometry.offset<w.scroll->geometry.range?1:-1,time);frame();
                Check(widget(id).popupOffsetDp!=before,"SYSTEM wheel changes actual popup offset");
            }
            Check(widget(id).accepted==accepted&&widget(id).highlight==highlight&&runtime.TakeActions().empty(),"SYSTEM popup scrolling does not draft settings");
            // End reveals the last option, and the reveal settles with the list's
            // scroll measured again: rows measured through the scrolled content
            // must not move it every pass (a fractional row at a rounding edge).
            key(MenuInput::End);frame();
            w=widget(id);
            if(!(w.scroll&&w.scroll->available))std::fprintf(stderr,"%dx%d %s: End's reveal left the scroll unmeasured at %s\n",width,height,id,w.highlight.c_str());
            Check(w.scroll&&w.scroll->available,"End's reveal settles with measured scroll geometry");
            if(w.scroll->geometry.usable)Check(Near(w.popupOffsetDp*w.scroll->dpRatio,w.scroll->geometry.range),"End's reveal scrolls the list to its last option");
            key(MenuInput::Back);frame();Check(!widget(id).popupOpen,"SYSTEM popup closes with Back");
        }
        // Resolution Scale's nine steps outgrow the eight rows a list shows: it scrolls.
        Check(scrolled>0,"some SYSTEM list is long enough to scroll");
        std::string saved;Check(runtime.SaveSnapshot(saved,error,time),"mixed SYSTEM numeric/generic/choice widgets save");
        Check(runtime.RestoreSnapshot(saved,error,time),"mixed SYSTEM widget table restores atomically");frame();
    }
}
static void SemanticPopupRuntime(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f}) {
        View view(host,Text(ScrollChoice()));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=ratio;view.Frame();
        Check(!view.runtime.OpenChoicePopup("choice",view.time),"semantic opening requires exact focused choice");
        Check(view.runtime.FocusControl("choice",view.time),"semantic choice focused");
        Check(!view.runtime.OpenChoicePopup("choice",-1),"invalid presentation time cannot open popup");
        Check(view.runtime.OpenChoicePopup("choice",view.time),"semantic popup opens without device input");view.Frame();
        auto before=view.Widget("choice");Check(before.popupOpen&&before.scroll&&before.scroll->available,"semantic popup has actual measured layout");
        Check(!view.runtime.OpenChoicePopup("choice",view.time),"open cannot replace existing identity");
        Check(!view.runtime.CloseChoicePopup("choice",before.popupToken+1,view.time),"stale close refuses");
        Check(!view.runtime.ScrollChoicePopup("choice",before.popupToken+1,ScrollStep::End,view.time),"stale local scroll refuses");
        Check(view.runtime.ScrollChoicePopup("choice",before.popupToken,ScrollStep::End,view.time),"end uses fresh layout geometry during semantic call");view.Frame();
        auto after=view.Widget("choice");Check(Near(after.popupOffsetDp,after.scroll->geometry.range/ratio),"semantic end reaches measured bottom");
        Check(after.accepted==before.accepted&&after.pending==before.pending&&after.proposalToken==before.proposalToken&&after.highlight==before.highlight&&view.runtime.TakeActions().empty(),"semantic scroll preserves settings and option highlight");
        view.viewport.displayScale=ratio==2?1.f:2.f;view.Frame();
        Check(view.runtime.ScrollChoicePopup("choice",before.popupToken,ScrollStep::Start,view.time),"same opening uses new density geometry");view.Frame();
        Check(Near(view.Widget("choice").popupOffsetDp,0),"semantic start reaches first row");
        Check(view.runtime.CloseChoicePopup("choice",before.popupToken,view.time),"semantic exact close");
        Check(view.runtime.OpenChoicePopup("choice",view.time),"semantic reopen");view.Frame();
        const auto successor=view.Widget("choice").popupToken;
        Check(successor!=before.popupToken&&!view.runtime.CloseChoicePopup("choice",before.popupToken,view.time),"old opening cannot close successor");
        view.viewport.width=0;view.Frame();
        Check(!view.runtime.ScrollChoicePopup("choice",successor,ScrollStep::End,view.time)&&!view.runtime.CloseChoicePopup("choice",successor,view.time),"zero viewport refuses semantic operations");
        view.viewport.width=1280;view.Frame();
        std::vector<Diagnostic> diagnostics;
        Check(view.runtime.LoadDocument(Text(ScrollChoice()),"replacement.rui",diagnostics),"replace canonical source");view.Frame();
        Check(view.runtime.FocusControl("choice",view.time)&&view.runtime.OpenChoicePopup("choice",view.time),"replacement gets its own popup");view.Frame();
        Check(!view.runtime.CloseChoicePopup("choice",successor,view.time),"source replacement invalidates original opening");
        Check(view.runtime.SetControlEnabled("choice",false,view.time),"disable unbound choice");view.Frame();
        Check(!view.Widget("choice").popupOpen&&!view.runtime.OpenChoicePopup("choice",view.time),"disabled choice remains closed");
        Check(view.runtime.SetControlEnabled("choice",true,view.time),"reenable choice");view.Frame();
        Check(view.runtime.FocusControl("choice",view.time),"focus visible choice");
        auto* choice=Rml::GetContext(0)->GetDocument(0)->GetElementById("choice");
        Check(choice&&choice->SetProperty("display","none"),"hide actual choice element");view.Frame();
        Check(!view.runtime.OpenChoicePopup("choice",view.time)&&view.runtime.TakeActions().empty(),"hidden choice cannot open or propose");
    }
}
int main(int argc,char** argv) {
    Schema();{TestHost host;ChoiceBar(host);FramesAndLifecycle(host);TransformedBar(host);LeadingRows(host);PopupFrames(host);ChangedRowTransform(host);SemanticPopupRuntime(host);Check(host.errors==0,"choice bar runtime has no errors");}
    LegacyValueRuntimeMain();if(argc>1){std::ifstream file(argv[1],std::ios::binary);Check(bool(file),"SYSTEM candidate readable");SystemCandidate(std::string{std::istreambuf_iterator<char>(file),{}});}
    std::printf("PASS %u checks\n",checks);return 0;
}
