// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#define main LegacyPopupValueMain
#include "UiValueRuntimeTest.cpp"
#undef main
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/ElementText.h>
#include <fstream>
#include <iomanip>
#include <sstream>

static Json::Value BoundedChoice(bool bar=true) {
    auto source=Parse(Source());
    auto& parent=*BoxFrameNode(source["root"],"clipped-parent");
    for(const auto& [name,value]:std::map<std::string,double>{{"left",80},{"top",50},{"width",480},{"height",300}})
        parent["properties"][name]=Typed("length",value,"dp");
    auto& choice=*BoxFrameNode(source["root"],"choice");
    choice["control"]["placementBounds"]="clipped-parent";
    choice["properties"]["left"]=Typed("length",40,"dp");choice["properties"]["top"]=Typed("length",240,"dp");
    choice["properties"]["width"]=Typed("length",360,"dp");choice["properties"]["height"]=Typed("length",40,"dp");
    auto& popup=*BoxFrameNode(source["root"],"popup");
    popup["properties"]["display"]=Typed("keyword","none");popup["properties"]["padding"]=Typed("length",8,"dp");
    popup["properties"]["border-width"]=Typed("length",1,"dp");popup["properties"]["box-sizing"]=Typed("keyword","border-box");
    if(bar) {
        auto& scroll=choice["control"]["scrollbar"];scroll["track"]="choice-track";scroll["thumb"]="choice-thumb";
        scroll["minimumThumb"]=36;scroll["lineStep"]=36;
        auto track=Box("choice-track",0,0,36,80),thumb=Box("choice-thumb",10,0,16,36);
        Colour(track,{.11,.22,.33,1});Colour(thumb,{.9,.8,.7,1});track["children"].append(thumb);popup["children"].append(track);
    }
    for(const char* id:{"clipped-parent","choice"}) {
        source["aliases"][std::string(id)+"::rect"]["node"]=id;
        source["aliases"][std::string(id)+"::rect"]["property"]="rect";
    }
    return source;
}
static Rml::Element* Element(const char* id) {auto* value=Rml::GetContext(0)->GetDocument(0)->GetElementById(id);Check(value!=nullptr,"actual Rml element exists");return value;}
static void Inside(View& view,const char* boundsId="clipped-parent",const char* popupId="popup") {
    Rml::Array<Rml::Vector2f,4> bounds,popup;
    Check(Rml::ElementUtilities::GetBorderBoxQuad(bounds,Element(boundsId)),"bounds have valid projected quad");
    Check(Rml::ElementUtilities::GetBorderBoxQuad(popup,Element(popupId)),"popup has valid projected quad");
    double area=0;for(unsigned i=0;i<4;++i)area+=bounds[i].x*bounds[(i+1)%4].y-bounds[i].y*bounds[(i+1)%4].x;
    for(const auto& p:popup) {
        Check(p.x>=4*view.viewport.DpRatio()-.001&&p.y>=4*view.viewport.DpRatio()-.001&&p.x<=view.viewport.width-4*view.viewport.DpRatio()+.001&&p.y<=view.viewport.height-4*view.viewport.DpRatio()+.001,"actual popup corner inside inset display");
        for(unsigned i=0;i<4;++i) {
            const auto a=bounds[i],b=bounds[(i+1)%4];const double dx=b.x-a.x,dy=b.y-a.y;
            const double distance=(area>0?1:-1)*(dx*(p.y-a.y)-dy*(p.x-a.x))/std::hypot(dx,dy);
            Check(distance>=4*view.viewport.DpRatio()-.001,"actual popup corner inside inset projected ancestor edge");
        }
    }
}
static void TransformedBounds(TestHost& host) {
    for(const auto& css:{"rotate(17deg)","skewX(19deg)","perspective(900px) rotateY(17deg)","scaleX(-1) rotate(8deg)"}) {
        View view(host,Text(BoundedChoice()));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=1.25f;view.Frame();
        Check(Element("clipped-parent")->SetProperty("transform",css),"actual Rml bounds transform accepted");
        Rml::GetContext(0)->Update();Rml::GetContext(0)->GetRootElement()->UpdateGeometryForProjection();view.Frame();
        view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        Check(view.Widget("choice").popupOpen,"transformed bounded opening fits a complete row");Inside(view);
        Check(view.Widget("choice").scroll&&view.Widget("choice").scroll->available,"projected placement settles before interaction");
        view.Key(MenuInput::End);view.Frame();Inside(view);Check(view.Widget("choice").highlight=="option-5","transformed bounds preserve local list navigation");
    }
}
static void ChangedBounds(TestHost& host) {
    for(unsigned change=0;change<6;++change) {
        auto source=BoundedChoice();
        if(change==5) {
            auto spacer=Box("flow-spacer",0,0,480,50);spacer["properties"]["position"]=Typed("keyword","relative");source["root"]["children"].insert(0,spacer);
            auto& parent=(*BoxFrameNode(source["root"],"clipped-parent"))["properties"];parent["position"]=Typed("keyword","relative");parent["top"]=Typed("length",0,"dp");
        }
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        const auto row=view.BoxOf("option-0");view.Point(row.x+20,row.y+20);view.runtime.PointerButton(true,view.time);
        std::string error;
        if(change==0)Check(view.runtime.SetPresentationAlias("clipped-parent::rect","90 50 450 280",true,error),"bounds change after pointer down");
        if(change==1)Check(Element("clipped-parent")->SetProperty("transform","rotate(4deg)"),"bounds transform changes after pointer down");
        if(change==2)Check(Element("clipped-parent")->SetProperty("display","none"),"bounds visibility changes after pointer down");
        if(change==3){view.viewport.displayScale=1.25f;view.Frame();}
        if(change==4)Check(Element("clipped-parent")->SetProperty("font-size","21px"),"inherited type changes after pointer down");
        if(change==5)Check(Element("flow-spacer")->SetProperty("height","80px"),"unrelated flow sibling changes before layout");
        view.runtime.PointerButton(false,view.time);
        // Geometry that moved cancels the gesture: the press named rows that no
        // longer describe the screen, so the release selects nothing. Only a
        // plate that is gone entirely retires the opening; a moved one is
        // re-placed by the next paint and stays usable.
        Check(view.runtime.TakeActions().empty(),"changed placement releases a held row without selection");
        Check(view.Widget("choice").popupOpen==(change!=2),"only an unavailable plate retires the opening");
        if(change!=2) {
            view.Frame();
            Check(view.Widget("choice").popupOpen,"the moved opening survives its re-placement");
            view.Key(MenuInput::Accept);
            Check(!view.runtime.TakeActions().empty(),"the re-placed opening accepts an option again");
        }
    }
    {
        View view(host,Text(BoundedChoice()));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        view.runtime.MenuAction(MenuInput::Accept,true,view.time);std::string error;
        Check(view.runtime.SetPresentationAlias("clipped-parent::rect","90 50 450 280",true,error),"bounds change after keyboard Accept down");
        view.runtime.MenuAction(MenuInput::Accept,false,view.time);
        Check(view.runtime.TakeActions().empty(),"keyboard release cannot accept a stale placement");
        Check(view.Widget("choice").popupOpen,"the refused keyboard release keeps the moved opening");
        view.Frame();view.Key(MenuInput::Accept);
        Check(!view.runtime.TakeActions().empty(),"the re-placed opening accepts the same option afterwards");
    }
    for(bool bar:{false,true}) {
        auto source=BoundedChoice(bar);auto& bounds=(*BoxFrameNode(source["root"],"clipped-parent"))["properties"];
        bounds["height"]=Typed("length",35,"dp");(*BoxFrameNode(source["root"],"choice"))["properties"]["top"]=Typed("length",0,"dp");
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"cannot fit one full framed row closes exact opening without application edit");
    }
    {
        auto source=BoundedChoice();(*BoxFrameNode(source["root"],"choice"))["control"]["options"][3]["label"]="#str_"+std::string(70,'x');
        (*BoxFrameNode(source["root"],"choice"))["control"]["options"][3].removeMember("labelIndex");
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"unreadably wide localized row refuses opening rather than shrinking type or clipping label");
    }
    for(const char* property:{"max-height","max-width","min-height"}) {
        auto source=BoundedChoice();(*BoxFrameNode(source["root"],"popup"))["properties"][property]=Typed("length",std::string(property)=="min-height"?600:20,"dp");
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"CSS constraints cannot publish mismatched or spilling popup geometry");
    }
    for(bool trackOutside:{false,true}) {
        auto source=BoundedChoice();
        if(trackOutside)(*BoxFrameNode(source["root"],"choice-track"))["properties"]["transform"]=Typed("transform",Array({400,0,1,1,0}),"dp");
        else (*BoxFrameNode(source["root"],"viewport"))["properties"]["left"]=Typed("length",-30,"dp");
        View view(host,Text(source));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);view.Frame();
        Check(!view.Widget("choice").popupOpen&&view.runtime.TakeActions().empty(),"actual viewport and transformed local track cannot escape constrained frame");
    }
}
static void UnpaintedOpening(TestHost& host) {
    for(bool bar:{false,true}) {
        View view(host,Text(BoundedChoice(bar)));view.runtime.FocusControl("choice",view.time);view.Key(MenuInput::Accept);
        Check(view.Widget("choice").popupOpen,"initial semantic opening can await paint");
        view.Key(MenuInput::Accept);Check(view.runtime.TakeActions().empty(),"rapid Accept cannot commit before a measured constrained frame");
        Check(view.Widget("choice").popupOpen,"the refused rapid Accept preserves the unmeasured opening");
        view.Frame();view.Key(MenuInput::Back);view.Frame();
        Check(view.runtime.OpenChoicePopup("choice",view.time),"explicit opening starts a new allocation-free popup token");view.Frame();
        const auto first=view.Widget("choice").popupToken;
        Check(view.runtime.CloseChoicePopup("choice",first,view.time)&&view.runtime.OpenChoicePopup("choice",view.time),"close and reopen without intermediate paint");
        Check(view.Widget("choice").popupToken!=first,"reopened popup identity is fresh");
        view.Key(MenuInput::Accept);Check(view.runtime.TakeActions().empty(),"previous opening geometry cannot authorize newly reopened selection");
        Check(view.Widget("choice").popupOpen,"the reopened unmeasured opening survives its refused acceptance");
        view.Frame();Inside(view);Check(view.Widget("choice").popupOpen,"the reopened opening reaches its own measured frame");
        view.Key(MenuInput::Accept);Check(!view.runtime.TakeActions().empty(),"a measured frame restores ordinary acceptance");
    }
}
static void InputBeforeFirstPaint(TestHost& host) {
    // The engine pumps pointer motion and paired key releases between the
    // opening and the next painted frame. An opening still awaiting its first
    // measured layout is not stale presentation and must survive that input.
    for(bool bar:{false,true}) {
        {View view(host,Text(BoundedChoice(bar)));view.runtime.FocusControl("choice",view.time);
         view.Key(MenuInput::Accept);const auto token=view.Widget("choice").popupToken;
         Check(view.Widget("choice").popupOpen,"opening precedes its first paint");
         view.Point(5,5);
         Check(view.Widget("choice").popupOpen&&view.Widget("choice").popupToken==token,"pointer motion before the first paint preserves the opening");
         view.Frame();Inside(view);
         Check(view.Widget("choice").popupOpen&&view.Widget("choice").popupToken==token,"the preserved opening reaches its first painted frame");
         view.Point(5,5);Check(view.Widget("choice").popupOpen&&view.Widget("choice").popupToken==token,"motion after the first paint still preserves the measured opening");
         view.Key(MenuInput::Accept);Check(!view.runtime.TakeActions().empty(),"the painted opening accepts its highlighted option");}
        {View view(host,Text(BoundedChoice(bar)));view.runtime.FocusControl("choice",view.time);
         view.Key(MenuInput::Accept);const auto token=view.Widget("choice").popupToken;
         view.Key(MenuInput::Down);
         Check(view.Widget("choice").popupOpen&&view.Widget("choice").popupToken==token,"navigation before the first paint preserves the opening");
         Check(view.runtime.TakeActions().empty(),"unmeasured navigation proposes nothing");
         view.Frame();Inside(view);Check(view.Widget("choice").popupOpen,"navigated opening still reaches its first painted frame");}
    }
}
static void SchemaPlacement() {
    auto source=BoundedChoice();Document document;std::vector<Diagnostic> errors;
    const bool loaded=document.Load(Text(source),errors);for(const auto& e:errors)std::fprintf(stderr,"%s: %s\n",e.pointer.c_str(),e.message.c_str());
    Check(loaded,"proper ancestor bounds schema loads");
    for(unsigned fault=0;fault<8;++fault) {
        auto bad=source;auto& control=(*BoxFrameNode(bad["root"],"choice"))["control"];
        if(fault<4)control["placementBounds"]=std::array<const char*,4>{"","missing","choice","popup"}[fault];
        if(fault==4)(*BoxFrameNode(bad["root"],"popup"))["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");
        if(fault==5)TransformTimeline(bad,"bad-transform","popup",Array({0,0,1,1,0}),Array({0,0,1,1,1}),1);
        if(fault==6){bad["aliases"]["popup::transform"]["node"]="popup";bad["aliases"]["popup::transform"]["property"]="transform";}
        if(fault==7){(*BoxFrameNode(bad["root"],"popup"))["properties"]["transform"]=Typed("transform",Array({0,0,1,1,0}),"dp");Json::Value binding;binding["id"]="theme-transform";binding["node"]="popup";binding["property"]="transform";binding["value"]=Array({0,0,1,1,0});binding["value"][0]=Ref("level");bad["bindings"].append(binding);}
        errors.clear();Check(!document.Load(Text(bad),errors)&&!errors.empty(),"invalid constrained placement is diagnosed");
    }
    source=Parse(Source());(*BoxFrameNode(source["root"],"popup"))["properties"]["transform"]=Typed("transform",Array({0,0,1,1,3}),"dp");
    Check(document.Load(Text(source),errors),"omitted bounds retains legacy popup transform authoring");
}
static std::string ReadPopupSource(const std::string& path){std::ifstream f(path,std::ios::binary);Check(bool(f),"source-bound authored input readable");return {std::istreambuf_iterator<char>(f),{}};}
struct PopupPageHost final:Host {
    std::map<std::string,std::string> strings;unsigned errors=0,draws=0,masks=0;std::uint64_t frame=1;float expansion=1;bool proportional=false;
    bool ReadFile(const std::string&,std::string&)override{return false;}
    bool ReadCVar(const std::string&,size_t,StateValue&)override{return false;}
    std::string Translate(const std::string& id)override{return strings.contains(id)?strings.at(id):id.starts_with("#str_")?"Localized label":id;}
    void Log(bool bad,const std::string& message)override{if(bad){++errors;std::fprintf(stderr,"Runtime: %s\n",message.c_str());}}
    std::uintptr_t LoadMaterial(const std::string&,int& w,int& h)override{w=h=256;return 1;}
    void Draw(const std::vector<Vertex>& vertices,const std::vector<int>& indices,std::uintptr_t)override{++draws;bool valid=indices.size()%3==0;for(auto i:indices)valid&=i>=0&&size_t(i)<vertices.size();for(const auto& v:vertices)valid&=std::isfinite(v.x)&&std::isfinite(v.y);Check(valid,"finite authored SYSTEM indexed draw");}
    std::uint64_t RenderFrame()const override{return frame;}
    bool BeginLayer(std::uint32_t,int,int)override{return true;}
    void CompositeLayer(std::uint32_t,std::uint32_t,float,const Bounds&)override{}
    void MaskLayer(std::uint32_t,std::uint32_t,const Bounds&)override{++masks;}
    void EndLayer(std::uint32_t)override{}
    FontMetrics GetFontMetrics(const std::string&,int size)override{return {size*.78f,size*.22f,size*1.4f,size*.5f};}
    Glyph GetGlyph(const std::string&,int size,std::uint32_t cp)override{const float factor=proportional?(cp==' '?.28f:cp>='A'&&cp<='Z'?1.2f:.2f):(cp==' '?.28f:.51f);const float a=(size*factor+.125f)*expansion;return {a,0,-size*.78f,a*.9f,float(size),0,0,1,1,"test-font"};}
    void Locale(const std::string& path){std::istringstream lines(ReadPopupSource(path));std::string line;while(std::getline(lines,line)){std::istringstream in(line);std::string key,value;in>>std::quoted(key)>>std::quoted(value);if(key.starts_with("#str_"))strings[key]=value;}Check(strings.contains("#str_229976"),"actual locale strings loaded");}
};
static void RenderedTextWidth() {
    for(unsigned mode=0;mode<5;++mode) {
        auto source=BoundedChoice();(*BoxFrameNode(source["root"],"choice"))["properties"]["width"]=Typed("length",120,"dp");
        PopupPageHost host;host.proportional=true;host.strings["#str_options"]="iiiiiiiiiiii;one;two;three;four;five";
        if(mode==2){host.strings["#str_options"]="i"+std::string(40,' ')+"i;one;two;three;four;five";(*BoxFrameNode(source["root"],"clipped-parent"))["properties"]["width"]=Typed("length",180,"dp");}
        if(mode==3)host.strings["#str_options"]="iiii\niiii;one;two;three;four;five";
        if(mode==4)host.strings["#str_options"]=";one;two;three;four;five";
        for(unsigned i=0;i<6;++i) {
            const auto id="option-"+std::to_string(i);auto& row=(*BoxFrameNode(source["root"],id.c_str()))["properties"];row["width"]=Typed("length",100,"%");
            auto& label=(*BoxFrameNode(source["root"],(id+"-label").c_str()))["properties"];label["left"]=Typed("length",0,"dp");label["padding-left"]=Typed("length",12,"dp");label["width"]=Typed("length",100,"%");label["box-sizing"]=Typed("keyword","border-box");
            if(mode==3){label["white-space"]=Typed("keyword","pre-line");row["height"]=Typed("length",72,"dp");row["top"]=Typed("length",i*72,"dp");}
        }
        Runtime runtime(host);std::vector<Diagnostic> ds;const bool loaded=runtime.LoadDocument(Text(source),"proportional.q4ui",ds);for(const auto& d:ds)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"proportional fixture loads");Viewport vp;vp.width=640;vp.height=400;double time=1;
        auto frame=[&]{time+=.02;++host.frame;runtime.Frame(vp,time);};auto key=[&](MenuInput input){runtime.MenuAction(input,true,time);runtime.MenuAction(input,false,time);};frame();if(mode==1)Check(Element("clipped-parent")->SetProperty("text-transform","uppercase"),"actual inherited Rml text transform accepted");if(mode==0)for(unsigned i=0;i<6;++i)Check(Element(("option-"+std::to_string(i)+"-label").c_str())->SetProperty("text-transform","uppercase"),"actual Rml label transform accepted");frame();runtime.FocusControl("choice",time);key(MenuInput::Accept);frame();
        auto w=runtime.GetWidgetState("choice");Check(w&&w->popupOpen&&w->scroll&&w->scroll->available,"processed text fits without compressing or falsely refusing row");
        auto* label=Element("option-0-label");auto* clip=Element("viewport");const auto at=clip->GetAbsoluteOffset(Rml::BoxArea::Padding),size=clip->GetBox().GetSize(Rml::BoxArea::Padding);unsigned count=0;
        for(int i=0;i<label->GetNumChildren();++i)if(auto* text=dynamic_cast<Rml::ElementText*>(label->GetChild(i)))for(const auto& line:text->GetLines()) {
            ++count;const auto origin=text->GetAbsoluteOffset(Rml::BoxArea::Content).Round();
            if(mode<2)Check(line.text=="IIIIIIIIIIII","actual authored/inherited uppercase is preserved through portal");
            if(mode==2)Check(line.text=="i i","Rml whitespace collapse is preserved");
            Check(origin.x+line.position.x+line.width<=at.x+size.x+.51f,"actual transformed proportional glyph line fits clipped row");
        }
        Check(count==(mode==4?0u:mode==3?2u:1u),"actual line break count preserved");Check(runtime.TakeActions().empty()&&host.errors==0,"text measurement has no application mutation or runtime error");
    }
}
static void ProductionPopup(const std::string& source,const std::string& locales) {
    Document model;std::vector<Diagnostic> modelErrors;Check(model.Load(source,modelErrors),"production source parses independently");
    for(const char* locale:{"english","french","russian","italian","spanish","polish"})for(float expansion:{1.f,1.4f}) {
        std::fprintf(stderr,"popup locale=%s glyph-width=%g\n",locale,expansion);
        PopupPageHost host;host.Locale(locales+"/"+locale+"_openq4.lang");host.Locale(locales+"/"+locale+"_guis.lang");host.expansion=expansion;Runtime runtime(host);std::vector<Diagnostic> diagnostics;
        const bool loaded=runtime.LoadDocument(source,"guis/menu/settings/system.q4ui",diagnostics);
        for(const auto& d:diagnostics)std::fprintf(stderr,"%s: %s\n",d.pointer.c_str(),d.message.c_str());Check(loaded,"production framed SYSTEM loads");
        std::string error;double time=1;Viewport vp;vp.width=1280;vp.height=720;vp.displayScale=expansion==1?1.25f:2.f;
        runtime.SetReducedMotion(true,time);Check(runtime.SetState({{"settings.open",true},{"settings.phase",1.},{"settings.busy",false},{"settings.confirmationVisible",false}},error,time),"real SYSTEM editing state");
        auto frame=[&]{time+=.02;++host.frame;runtime.Frame(vp,time);Check(host.errors==0,"no SYSTEM Runtime error");};
        auto key=[&](MenuInput input){runtime.MenuAction(input,true,time);runtime.MenuAction(input,false,time);};frame();
        for(unsigned layout=0;layout<(std::string(locale)=="english"?2u:1u);++layout) {
        if(layout){vp.width=800;vp.height=600;vp.displayScale=1;frame();}
        for(const char* id:{"settings_preset","settings_postaa","settings_resolution_scale","settings_vsync"}) {
            // The engine opens a dropdown while revealing the newly focused row is
            // still scrolling the panel, and pumps pointer motion before the next
            // paint. Neither may retire the opening before it is ever seen.
            Check(runtime.FocusControl(id,time),"production choice focusable");
            key(MenuInput::Accept);
            Check(runtime.GetWidgetState(id)->popupOpen,"unpainted production opening survives its reveal scroll");
            runtime.PointerMove(4,4,time);
            Check(runtime.GetWidgetState(id)->popupOpen,"pointer motion before the first paint preserves it");
            frame();
            {const auto first=runtime.GetWidgetState(id);
             Check(first&&first->popupOpen&&first->scroll&&first->scroll->available,
                "the preserved production opening is measured on its own first painted frame");}
            key(MenuInput::Back);frame();
            Check(runtime.FocusControl(id,time),"production choice reachable");frame();key(MenuInput::Accept);frame();
            auto widget=runtime.GetWidgetState(id);Check(widget&&widget->popupOpen&&widget->scroll&&widget->scroll->available,"production bounded popup first frame is usable");
            const auto& choice=std::get<ChoiceSpec>(model.Model().FindNode(id)->control->widget);
            for(const auto& option:choice.options) {
                Check(host.Translate(option.label)!=option.label,"every production option localization key resolves");
                const auto inner=Element(option.labelPart.c_str())->GetInnerRML();
                Check(!inner.empty()&&inner.find("#str_")==std::string::npos,"every essential production option has nonempty translated text");
            }
            const auto validate=[&]{
                Rml::Array<Rml::Vector2f,4> body,popup;Check(Rml::ElementUtilities::GetBorderBoxQuad(body,Element("settings-body"))&&Rml::ElementUtilities::GetBorderBoxQuad(popup,Element(choice.popup.c_str())),"production popup and body measured");
                for(auto p:popup){Check(p.x>=body[0].x+4*vp.DpRatio()-.001&&p.y>=body[0].y+4*vp.DpRatio()-.001&&p.x<=body[2].x-4*vp.DpRatio()+.001&&p.y<=body[2].y-4*vp.DpRatio()+.001,"production popup excludes footer and rail inside actual body");}
                const auto current=runtime.GetWidgetState(id);auto* row=Element(current->highlight.c_str());auto* clip=Element(choice.viewport.c_str());
                const auto rowAt=row->GetAbsoluteOffset(Rml::BoxArea::Border),rowSize=row->GetBox().GetSize(Rml::BoxArea::Border);
                const auto at=clip->GetAbsoluteOffset(Rml::BoxArea::Padding),size=clip->GetBox().GetSize(Rml::BoxArea::Padding);
                Check(rowAt.y>=at.y-.05&&rowAt.y+rowSize.y<=at.y+size.y+.05,"production highlighted row remains entirely readable vertically");
                const auto option=std::find_if(choice.options.begin(),choice.options.end(),[&](const auto& item){return item.id==current->highlight;});Check(option!=choice.options.end(),"visible highlighted option is authored");
                auto* label=Element(option->labelPart.c_str());unsigned lines=0;
                for(int child=0;child<label->GetNumChildren();++child)if(auto* text=dynamic_cast<Rml::ElementText*>(label->GetChild(child))) {
                    const auto origin=text->GetAbsoluteOffset(Rml::BoxArea::Content).Round();
                    for(const auto& line:text->GetLines()) {++lines;Check(!line.text.empty()&&line.width>0,"actual highlighted text generated a nonempty line");
                        Check(origin.x+line.position.x>=at.x-.51f&&origin.x+line.position.x+line.width<=at.x+size.x+.51f,"actual glyph line remains inside row viewport horizontally");}
                }
                Check(lines>0,"actual translated highlighted label was rendered");
                auto* plate=Element((std::string(id)+"-popup-frame").c_str());const auto frameBox=plate->GetBox().GetSize(Rml::BoxArea::Border),popupBox=Element(choice.popup.c_str())->GetBox().GetSize(Rml::BoxArea::Border);
                Check(plate->GetTagName()=="q4-vector"&&Near(frameBox.x,popupBox.x)&&Near(frameBox.y,popupBox.y),"editable frame spans the entire popup box");
                Check(model.Model().FindNode(choice.popup)->mask.has_value(),"popup canonical cut mask remains editable");
            };
            validate();key(MenuInput::End);frame();validate();Check(runtime.TakeActions().empty(),"SYSTEM placement/highlight generates no setting edits");key(MenuInput::Back);frame();
            Check(!runtime.GetWidgetState(id)->popupOpen,"SYSTEM bounded popup closes normally");
        }
        }
        Check(host.masks>0,"actual Rml cut-mask decorator submitted to host");
    }
}
static void BoundedRuntime(TestHost& host) {
    for(float ratio:{1.f,1.25f,2.f})for(bool borderBox:{false,true}) {
        auto source=BoundedChoice();(*BoxFrameNode(source["root"],"popup"))["properties"]["box-sizing"]=Typed("keyword",borderBox?"border-box":"content-box");
        View view(host,Text(source));view.viewport.width=1280;view.viewport.height=900;view.viewport.displayScale=ratio;view.Frame();
        Check(view.runtime.FocusControl("choice",view.time),"focus bounded choice");view.Key(MenuInput::Accept);view.Frame();
        auto widget=view.Widget("choice");
        Check(widget.popupOpen,"first bounded opening remains open");
        Check(widget.scroll&&widget.scroll->available,"first bounded frame has current local scroll geometry");
        Inside(view);const auto popup=view.BoxOf("popup"),anchor=view.BoxOf("choice");
        Check(popup.y+popup.height<=anchor.y+.001,"lower anchor flips popup above inside body");
        Check(view.BoxOf("viewport").height>=80*ratio-.01,"frame preserves two complete rows");
        view.Frame();Inside(view);Check(view.Widget("choice").popupOpen,"unchanged second frame preserves opening");
        view.Key(MenuInput::End);view.Frame();Inside(view);widget=view.Widget("choice");
        Check(widget.highlight=="option-5"&&widget.popupOffsetDp>0,"keyboard End reveals final row without accepting");
        Check(view.runtime.TakeActions().empty(),"placement and scroll never generate application proposals");
        view.Key(MenuInput::Accept);auto action=view.Proposal("choice",5);view.State({{"choice",5.0}});view.Ack(action,true);view.Frame();
        Check(!view.Widget("choice").popupOpen&&std::get<double>(view.Widget("choice").accepted)==5,"only explicit release and authoritative acknowledgement accept value");
    }
}
int main(int argc,char** argv) {
    const bool productionOnly=argc==4&&std::string(argv[3])=="production-only";
    if(!productionOnly){SchemaPlacement();{TestHost host;BoundedRuntime(host);TransformedBounds(host);ChangedBounds(host);UnpaintedOpening(host);InputBeforeFirstPaint(host);Check(host.errors==0,"no Runtime errors");}RenderedTextWidth();}
    if(argc>=3)ProductionPopup(ReadPopupSource(argv[1]),argv[2]);
    std::printf("UiPopupPlacementRuntimeTest: %u checks passed\n",checks);return 0;
}
