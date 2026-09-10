// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ScrollbarView.h"
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Property.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <limits>
#include <tuple>
namespace openq4::ui {
namespace {
std::uint64_t NextGeometry() {
    static std::atomic<std::uint64_t> next{1};auto value=next.load();
    while(value!=(std::numeric_limits<std::uint64_t>::max)())if(next.compare_exchange_weak(value,value+1))return value;
    return 0;
}
bool Within(Rml::Element* child,Rml::Element* parent) {for(;child;child=child->GetParentNode())if(child==parent)return true;return false;}
bool Same(const ScrollGeometry& a,const ScrollGeometry& b) {
    return std::tie(a.viewport,a.range,a.offset,a.track,a.thumb,a.position,a.travel,a.usable)==
        std::tie(b.viewport,b.range,b.offset,b.track,b.thumb,b.position,b.travel,b.usable);
}
std::string Pixels(double value) {char text[64];const auto r=std::to_chars(text,text+sizeof(text),value);return r.ec==std::errc{}?std::string(text,r.ptr)+"px":"0px";}
float CssExtent(Rml::Element* element,float border,bool vertical) {
    if(element->GetComputedValues().box_sizing()==Rml::Style::BoxSizing::BorderBox)return std::max(0.f,border);
    const auto frame=element->GetBox().GetFrameSize(Rml::BoxArea::Border)+element->GetBox().GetFrameSize(Rml::BoxArea::Padding);return std::max(0.f,border-(vertical?frame.y:frame.x));
}
}
struct ScrollbarView::Impl {
    struct Entry {
        ScrollSpec spec;Rml::Element* owner=nullptr;Rml::Element* viewport=nullptr;Rml::Element* track=nullptr;Rml::Element* thumb=nullptr;
        ScrollReadback readback;Rml::Array<Rml::Vector2f,4> quad{};
        std::optional<double> densityOffset;
        std::map<std::string,std::pair<std::string,std::string>> applied;
    };
    std::map<std::string,Entry> entries;
    double ratio=0;
    static double Offset(const Entry& e) {return e.spec.vertical?e.viewport->GetScrollTop():e.viewport->GetScrollLeft();}
    static bool SetOffset(Entry& e,double value) {
        const auto before=Offset(e);if(e.spec.vertical)e.viewport->SetScrollTop(float(value));else e.viewport->SetScrollLeft(float(value));return before!=Offset(e);
    }
    static bool Property(Entry& e,const char* property,const std::string& value) {
        auto& previous=e.applied[property];const auto* current=e.thumb->GetLocalProperty(property);
        if(previous.first==value && current && previous.second==current->ToString())return false;
        if(!e.thumb->SetProperty(property,value))return false;
        current=e.thumb->GetLocalProperty(property);previous={value,current?current->ToString():std::string{}};return true;
    }
    static void Observe(Entry& e,double ratio) {
        ScrollReadback value;value.dpRatio=ratio;value.lineStep=e.spec.lineStep*ratio;
        const auto size=e.track->GetBox().GetSize(Rml::BoxArea::Content);
        const auto frame=e.thumb->GetBox().GetFrameSize(Rml::BoxArea::Border)+e.thumb->GetBox().GetFrameSize(Rml::BoxArea::Padding);
        const double frameExtent=e.spec.vertical?frame.y:frame.x;
        const double trackExtent=e.spec.vertical?size.y:size.x;
        // CSS cannot shrink border+padding below its frame, even with
        // border-box sizing. The measured thumb must match that real minimum.
        const double minimum=std::max(e.spec.minimumThumb*ratio,frameExtent);
        const double client=e.spec.vertical?e.viewport->GetClientHeight():e.viewport->GetClientWidth();
        const double content=e.spec.vertical?e.viewport->GetScrollHeight():e.viewport->GetScrollWidth();
        const auto overflow=e.spec.vertical?e.viewport->GetComputedValues().overflow_y():e.viewport->GetComputedValues().overflow_x();
        Rml::Array<Rml::Vector2f,4> quad{};
        const bool measured=MeasureScroll({client,content,Offset(e),trackExtent,minimum},value.geometry);
        value.available=measured && std::isfinite(frameExtent) && frameExtent>=0 && frameExtent<=trackExtent && e.owner->IsVisible(true) && e.viewport->IsVisible(true) && e.track->IsVisible(true) && e.thumb->IsVisible(true) &&
            overflow!=Rml::Style::Overflow::Visible && overflow!=Rml::Style::Overflow::Hidden &&
            e.viewport->GetComputedValues().pointer_events()!=Rml::Style::PointerEvents::None &&
            Rml::ElementUtilities::GetBorderBoxQuad(quad,e.track);
        if(!e.readback.geometryToken || value.available!=e.readback.available || value.dpRatio!=e.readback.dpRatio ||
            value.lineStep!=e.readback.lineStep || !Same(value.geometry,e.readback.geometry) || quad!=e.quad) value.geometryToken=NextGeometry();
        else value.geometryToken=e.readback.geometryToken;
        e.readback=value;e.quad=quad;
    }
    static bool Paint(Entry& e) {
        const auto& g=e.readback.geometry;const bool vertical=e.spec.vertical;
        const auto start=e.track->GetAbsoluteOffset(Rml::BoxArea::Content);
        auto* parent=e.thumb->GetOffsetParent();const auto origin=parent?parent->GetAbsoluteOffset(Rml::BoxArea::Padding):Rml::Vector2f{};
        const float margin=e.thumb->GetBox().GetEdge(Rml::BoxArea::Margin,vertical?Rml::BoxEdge::Top:Rml::BoxEdge::Left);
        const double position=(vertical?start.y-origin.y:start.x-origin.x)-margin+g.position;
        bool changed=Property(e,vertical?"height":"width",Pixels(CssExtent(e.thumb,float(g.thumb),vertical)));
        changed|=Property(e,vertical?"top":"left",Pixels(position));return changed;
    }
};
ScrollbarView::ScrollbarView():impl(std::make_unique<Impl>()){}
ScrollbarView::~ScrollbarView()=default;
void ScrollbarView::Reset(){impl=std::make_unique<Impl>();}
bool ScrollbarView::Initialize(const DocumentModel& model,Rml::ElementDocument& document,std::string& error) {
    auto staged=std::make_unique<Impl>();std::vector<const Node*> pending{&model.root};
    while(!pending.empty()) {const auto* node=pending.back();pending.pop_back();
        if(node->control)if(const auto* spec=std::get_if<ScrollSpec>(&node->control->widget)) {
            Impl::Entry entry;entry.spec=*spec;entry.owner=document.GetElementById(node->id);entry.viewport=document.GetElementById(spec->viewport);
            entry.track=document.GetElementById(spec->track);entry.thumb=document.GetElementById(spec->thumb);
            if(!entry.owner||!entry.viewport||!entry.track||!entry.thumb){error="Missing authored scrollbar parts";return false;}
            staged->entries.emplace(node->id,std::move(entry));
        }
        for(const auto& child:node->children)pending.push_back(&child);
    }
    impl=std::move(staged);error.clear();return true;
}
bool ScrollbarView::HasScrollbars() const noexcept {return !impl->entries.empty();}
bool ScrollbarView::OwnsAxis(const Rml::Element* viewport,bool vertical) const noexcept {
    for(const auto& [id,e]:impl->entries)if(e.viewport==viewport && e.spec.vertical==vertical)return true;
    return false;
}
void ScrollbarView::PreserveDensity(double next) {
    if(!std::isfinite(next)||next<=0||impl->ratio<=0||impl->ratio==next)return;
    for(auto& [id,e]:impl->entries)if(!e.densityOffset)e.densityOffset=Impl::Offset(e)/impl->ratio;
}
bool ScrollbarView::Sync(Interaction& interaction,double ratio,bool fresh,std::string& error,bool deferRefresh) {
    if(!std::isfinite(ratio)||ratio<=0){error="Invalid scrollbar layout density";return false;}
    impl->ratio=ratio;bool changed=false;std::map<std::string,ScrollReadback> readbacks;
    for(auto& [id,e]:impl->entries) {
        Impl::Observe(e,ratio);
        const auto restored=interaction.PendingScrollOffset(id);const auto desired=restored?restored:e.densityOffset;
        if(fresh && desired && e.viewport->IsVisible(true) && e.readback.geometry.viewport>0) {
            const double offset=std::clamp(*desired*ratio,0.0,e.readback.geometry.range);
            changed|=Impl::SetOffset(e,offset);
            if(restored)interaction.AcknowledgeScrollRestore(id,*restored);
            e.densityOffset.reset();Impl::Observe(e,ratio);
        }
        changed|=Impl::Paint(e);readbacks.emplace(id,e.readback);
    }
    if(!interaction.SetScrollReadbacks(readbacks,error,deferRefresh))return false;
    return changed;
}
std::map<std::string,double> ScrollbarView::CaptureOffsets(const Interaction& interaction) const {
    std::map<std::string,double> out;
    for(const auto& [id,e]:impl->entries) out[id]=interaction.PendingScrollOffset(id).value_or(e.densityOffset.value_or(impl->ratio>0?Impl::Offset(e)/impl->ratio:0));
    return out;
}
bool ScrollbarView::ApplyCommands(Interaction& interaction) {
    bool changed=false;
    for(const auto& command:interaction.TakeScrollCommands()) {
        const auto found=impl->entries.find(command.control);if(found==impl->entries.end())continue;
        auto& e=found->second;Impl::Observe(e,impl->ratio);
        if(e.readback.geometryToken!=command.geometryToken || !interaction.CanDispatchScrollCommand(command) ||
            !interaction.AllowsNode(e.spec.viewport) || !e.readback.available)continue;
        double offset=0;const auto& r=e.readback;
        if(!(command.drag?ScrollDragOffset(r.geometry,command.pointer,command.grabFraction,offset):ScrollStepOffset(r.geometry,command.step,r.lineStep,offset)))continue;
        changed|=Impl::SetOffset(e,offset);
    }
    return changed;
}
ScrollbarView::PointerResult ScrollbarView::PointerPart(Rml::Element* hit,float x,float y,const Interaction& interaction) const {
    PointerResult result;const auto captured=interaction.CapturedPointerControl();
    for(const auto& [id,e]:impl->entries) {
        if(!captured.empty() && captured!=id)continue;
        if(captured!=id && (!interaction.CanActivate(id)||!Within(hit,e.track)))continue;
        result.control=id;result.thumb=Within(hit,e.thumb);Rml::Vector2f point{x,y};
        if(!e.readback.available || e.readback.geometry.track<=0 || !std::isfinite(x)||!std::isfinite(y)||!e.track->Project(point)) {
            result.invalidProjection=true;return result;
        }
        point-=e.track->GetAbsoluteOffset(Rml::BoxArea::Content);
        const double fraction=(e.spec.vertical?point.y:point.x)/e.readback.geometry.track;
        if(!std::isfinite(fraction))result.invalidProjection=true;else result.fraction=fraction;
        return result;
    }
    return result;
}
std::string ScrollbarView::WheelTarget(Rml::Element* hit,const Interaction& interaction) const {
    for(auto* current=hit;current;current=current->GetParentNode()) {
        std::string horizontal;
        for(const auto& [id,e]:impl->entries) {
            if(!interaction.CanActivate(id) || !interaction.AllowsNode(e.spec.viewport))continue;
            // Direct artwork selects its own axis. Shared content uses the
            // nearest usable vertical owner, with horizontal-only fallback.
            if(current==e.owner)return id;
            if(current==e.viewport) {
                if(e.spec.vertical)return id;
                horizontal=id;
            }
        }
        if(!horizontal.empty())return horizontal;
    }
    return {};
}
}
