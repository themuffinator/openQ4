// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "NumberControlView.h"
#include "TextRun.h"
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace openq4::ui {
namespace {
std::string Pixels(float value) {
	std::ostringstream out; out.imbue(std::locale::classic());
	out << std::setprecision(9) << value << "px"; return out.str();
}
bool Within(Rml::Element* element,Rml::Element* root) {
	for (;element;element=element->GetParentNode()) if (element==root) return true;
	return false;
}
Bounds BoundsOf(const Rml::Rectanglef& rect) { return {rect.Left(),rect.Top(),rect.Width(),rect.Height()}; }
bool Finite(const Bounds& box) {
	return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) && std::isfinite(box.height);
}
bool Same(float a,float b) { return std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=.01f; }
std::string Presented(const NumberEditView& view) {
	if (!view.active || !view.composition) return view.state.text;
	const auto first=std::min(view.state.anchor,view.state.caret), last=std::max(view.state.anchor,view.state.caret);
	return view.state.text.substr(0,first)+view.composition->text+view.state.text.substr(last);
}
}
struct NumberControlView::Impl {
	struct Entry {
		NumberSpec spec;
		std::shared_ptr<const TextRun> run;
		NumberEditIdentity identity;
		float scroll=0, baselineX=0, baselineY=0;
		std::uintptr_t font=0;
		float letterSpacing=0;
		Bounds localViewport,localCaret;
		double blinkEpoch=0;
		bool geometryReady=false,wasActive=false;
	};
	struct Applied { std::string requested,actual; };
	Rml::ElementDocument* document=nullptr;
	NumberTextRunQuery query;
	std::function<std::string(const std::string&)> translate;
	NumberValidationText validation;
	std::map<std::string,Entry> entries;
	std::map<std::string,std::map<std::string,Value>> authored;
	std::map<std::pair<std::string,std::string>,Applied> applied;
	Rml::Element* Element(const std::string& id) const { return document ? document->GetElementById(id) : nullptr; }
	bool Property(const std::string& id,const std::string& key,const std::string& value) {
		auto* element=Element(id); if (!element) return false;
		auto& previous=applied[{id,key}]; const auto* current=element->GetLocalProperty(key);
		if (previous.requested==value && current && previous.actual==current->ToString()) return false;
		if (!element->SetProperty(key,value)) return false;
		current=element->GetLocalProperty(key); previous={value,current ? current->ToString() : std::string{}}; return true;
	}
	bool Text(const std::string& id,const std::string& value) {
		auto* element=Element(id); if (!element) return false;
		auto& previous=applied[{id,"text"}];
		if (previous.requested==value && previous.actual==element->GetInnerRML()) return false;
		element->SetInnerRML(Rml::StringUtilities::EncodeRml(value)); previous={value,element->GetInnerRML()}; return true;
	}
	bool Display(const std::string& id,bool visible) {
		const auto found=authored.at(id).find("display");
		const auto shown=found!=authored.at(id).end() && found->second.text!="none" ? found->second.text : "block";
		return Property(id,"display",visible ? shown : "none");
	}
	bool HideEditing(Entry& entry) {
		entry.geometryReady=false; entry.run.reset();
		bool changed=Display(entry.spec.caret,false);
		changed|=Display(entry.spec.selection,false); changed|=Display(entry.spec.composition,false);
		return changed;
	}
	// A Paint pass may set offsets which RmlUi has not laid out yet. Queries
	// become usable only when the actual line, viewport and caret match that pass.
	bool Fresh(const Entry& entry) const {
		if (!entry.geometryReady || !entry.run) return false;
		auto* viewport=Element(entry.spec.viewport); auto* parent=Element(entry.spec.text); auto* caret=Element(entry.spec.caret);
		if (!viewport || !parent || !caret || !viewport->IsVisible(true) || !parent->IsVisible(true) || !caret->IsVisible(true)) return false;
		auto* typography=parent;
		Rml::Vector2f line;
		if (!entry.run->text.empty()) {
			if (parent->GetNumChildren()!=1 || parent->GetChild(0)->GetTagName()!="#text") return false;
			auto* ink=static_cast<Rml::ElementText*>(parent->GetChild(0));
			if (ink->GetLines().size()!=1 || ink->GetLines()[0].text!=entry.run->text) return false;
			typography=ink; line=ink->GetLines()[0].position;
		} else {
			const auto font=parent->GetFontFaceHandle(); if (!font || font!=entry.font) return false;
			const auto& metrics=Rml::GetFontEngineInterface()->GetFontMetrics(font);
			line={0,(parent->GetComputedValues().line_height().value-metrics.ascent-metrics.descent)*.5f+metrics.ascent};
		}
		if (typography->GetFontFaceHandle()!=entry.font || typography->GetComputedValues().letter_spacing()!=entry.letterSpacing) return false;
		const auto baseline=typography->GetAbsoluteOffset(Rml::BoxArea::Content).Round()+line;
		const auto vp=viewport->GetAbsoluteOffset(Rml::BoxArea::Content),vs=viewport->GetBox().GetSize(Rml::BoxArea::Content);
		const auto cp=caret->GetAbsoluteOffset(Rml::BoxArea::Border),cs=caret->GetBox().GetSize(Rml::BoxArea::Border);
		return Same(baseline.x,entry.baselineX) && Same(baseline.y,entry.baselineY) &&
			Same(vp.x,entry.localViewport.x) && Same(vp.y,entry.localViewport.y) && Same(vs.x,entry.localViewport.width) && Same(vs.y,entry.localViewport.height) &&
			Same(cp.x,entry.localCaret.x) && Same(cp.y,entry.localCaret.y) && Same(cs.x,entry.localCaret.width) && Same(cs.y,entry.localCaret.height);
	}
	float Length(const std::string& id,const std::string& key,float ratio,float reference,float fallback) const {
		const auto found=authored.at(id).find(key); if (found==authored.at(id).end() || found->second.type!=ValueType::Length) return fallback;
		const auto& value=found->second;
		return static_cast<float>(value.data[0])*(value.unit=="dp" ? ratio : value.unit=="%" ? reference*.01f : 1.f);
	}
	bool Rectangle(const Entry& entry,const std::string& id,float x,float y,float width,float height) {
		auto* viewport=Element(entry.spec.viewport); auto* element=Element(id);
		const auto paddingOrigin=viewport->GetAbsoluteOffset(Rml::BoxArea::Padding);
		const auto& box=element->GetBox(); const auto frame=box.GetFrameSize(Rml::BoxArea::Border);
		// Absolute children use the viewport's padding box as their containing
		// block. CSS left/top locate the margin edge, not the painted border edge.
		const float left=x-paddingOrigin.x+viewport->GetScrollLeft()-box.GetEdge(Rml::BoxArea::Margin,Rml::BoxEdge::Left);
		const float top=y-paddingOrigin.y+viewport->GetScrollTop()-box.GetEdge(Rml::BoxArea::Margin,Rml::BoxEdge::Top);
		const bool borderBox=element->GetComputedValues().box_sizing()==Rml::Style::BoxSizing::BorderBox;
		bool changed=Property(id,"left",Pixels(left)); changed|=Property(id,"top",Pixels(top));
		changed|=Property(id,"width",Pixels(std::max(0.f,width-(borderBox ? 0.f : frame.x))));
		changed|=Property(id,"height",Pixels(std::max(0.f,height-(borderBox ? 0.f : frame.y)))); return changed;
	}
};
NumberControlView::NumberControlView():impl(std::make_unique<Impl>()) {}
NumberControlView::~NumberControlView()=default;
void NumberControlView::Reset() { impl=std::make_unique<Impl>(); }
bool NumberControlView::Initialize(const DocumentModel& model,Rml::ElementDocument& document,NumberTextRunQuery query,
	std::function<std::string(const std::string&)> translate,NumberValidationText validation,std::string& error) {
	Reset(); impl->document=&document; impl->query=std::move(query); impl->translate=std::move(translate); impl->validation=std::move(validation);
	std::vector<const Node*> nodes{&model.root};
	while (!nodes.empty()) {
		const auto* node=nodes.back(); nodes.pop_back(); impl->authored[node->id]=node->properties;
		if (node->control) if (const auto* spec=std::get_if<NumberSpec>(&node->control->widget)) {
			if (!impl->query) { error="Number controls require the shared text-run service"; Reset(); return false; }
			for (const auto& id : {spec->viewport,spec->text,spec->selection,spec->caret,spec->composition,spec->validation})
				if (!impl->Element(id)) { error="Missing number part: "+id; Reset(); return false; }
			impl->entries[node->id].spec=*spec;
		}
		for (const auto& child:node->children) nodes.push_back(&child);
	}
	error.clear(); return true;
}
bool NumberControlView::Paint(const Interaction& interaction,float ratio,double seconds,bool steadyCaret) {
	if (!impl->document) return false;
	bool changed=false;
	if (!std::isfinite(ratio) || ratio<=0 || !std::isfinite(seconds)) {
		for (auto& item:impl->entries) changed|=impl->HideEditing(item.second);
		return changed;
	}
	for (auto& [id,entry]:impl->entries) {
		entry.geometryReady=false; entry.run.reset();
		const auto fail=[&] { changed|=impl->HideEditing(entry); };
		const auto view=interaction.Widget(id); if (!view || !std::holds_alternative<double>(view->accepted)) { fail(); continue; }
		const auto& spec=entry.spec; const auto* edit=view->number ? &*view->number : nullptr;
		const bool active=edit && edit->active && interaction.Focused()==id && interaction.CanActivate(id);
		std::string text,error;
		if (edit) text=Presented(*edit);
		else if (!FormatTextNumber(std::get<double>(view->accepted),{spec.minimum,spec.maximum,spec.exponent},text,error)) { fail(); continue; }
		const bool edited=edit && edit->identity!=entry.identity;
		if (edited) entry.identity=edit->identity;
		if (active && (edited || !entry.wasActive)) entry.blinkEpoch=seconds;
		entry.wasActive=active;
		if (!edit) entry.identity={};
		changed|=impl->Property(spec.viewport,"overflow","hidden"); changed|=impl->Property(spec.viewport,"clip","always");
		changed|=impl->Property(spec.text,"white-space","pre"); changed|=impl->Property(spec.text,"text-align","left");
		changed|=impl->Property(spec.text,"text-transform","none");
		for (const auto& part:{spec.text,spec.selection,spec.caret,spec.composition}) {
			changed|=impl->Property(part,"position","absolute"); changed|=impl->Property(part,"transform","none");
		}
		const bool invalid=edit && (edit->conflict || edit->status!=TextNumberStatus::Valid || view->rejected);
		std::string message;
		if (invalid) {
			if (impl->validation) message=impl->validation(spec,*edit,view->rejected.has_value());
			else { message=impl->authored.at(spec.validation).at("text").text; if (impl->translate) message=impl->translate(message); }
		}
		changed|=impl->Text(spec.validation,message); changed|=impl->Display(spec.validation,invalid);
		const bool textChanged=impl->Text(spec.text,text); changed|=textChanged;
		if (textChanged) { fail(); continue; } // Actual formatted line position follows layout.
		auto* viewport=impl->Element(spec.viewport); auto* parent=impl->Element(spec.text);
		const auto viewportSize=viewport->GetBox().GetSize(Rml::BoxArea::Content);
		if (!parent->IsVisible(true) || !std::isfinite(viewportSize.x) || !std::isfinite(viewportSize.y) || viewportSize.x<=0 || viewportSize.y<=0) { fail(); continue; }
		const float baseLeft=impl->Length(spec.text,"left",ratio,viewport->GetBox().GetSize(Rml::BoxArea::Padding).x,0);
		if (!active) {
			entry.scroll=0; changed|=impl->Property(spec.text,"left",Pixels(baseLeft));
			fail(); continue;
		}
		Rml::ElementText* ink=nullptr;
		if (parent->GetNumChildren()==1 && parent->GetChild(0)->GetTagName()=="#text") ink=static_cast<Rml::ElementText*>(parent->GetChild(0));
		if (!text.empty() && (!ink || ink->GetLines().size()!=1 || ink->GetLines()[0].text!=text)) { fail(); continue; }
		auto* typography=ink ? static_cast<Rml::Element*>(ink) : parent;
		const auto font=typography->GetFontFaceHandle(); if (!font) { fail(); continue; }
		entry.font=font; entry.letterSpacing=typography->GetComputedValues().letter_spacing();
		entry.run=impl->query(font,text,entry.letterSpacing); if (!entry.run || !entry.run->monotonicLtr) { fail(); continue; }
		const auto& metrics=Rml::GetFontEngineInterface()->GetFontMetrics(font);
		// ElementText submits its local glyph mesh through Geometry::Render,
		// which rounds the content translation before the element transform.
		// Caret/selection use that same painted origin, retaining fractional pens.
		const auto origin=typography->GetAbsoluteOffset(Rml::BoxArea::Content).Round();
		const auto line=ink && !ink->GetLines().empty() ? ink->GetLines()[0].position :
			Rml::Vector2f(0,(typography->GetComputedValues().line_height().value-metrics.ascent-metrics.descent)*.5f+metrics.ascent);
		entry.baselineX=origin.x+line.x; entry.baselineY=origin.y+line.y;
		std::size_t caret=edit ? edit->state.caret : text.size(), anchor=edit ? edit->state.anchor : caret;
		std::size_t compositionFirst=0,compositionLast=0;
		if (edit && edit->composition) {
			compositionFirst=std::min(edit->state.anchor,edit->state.caret); compositionLast=compositionFirst+edit->composition->text.size();
			caret=compositionFirst+edit->composition->selectionStart.value_or(edit->composition->text.size());
			anchor=caret;
			if (edit->composition->selectionLength) caret+=*edit->composition->selectionLength;
		}
		float caretX=0,anchorX=0;
		if (!entry.run->CaretPosition(caret,caretX) || !entry.run->CaretPosition(anchor,anchorX)) { fail(); continue; }
		const auto viewportOrigin=viewport->GetAbsoluteOffset(Rml::BoxArea::Content);
		entry.localViewport={viewportOrigin.x,viewportOrigin.y,viewportSize.x,viewportSize.y};
		if (!Finite(entry.localViewport) || !std::isfinite(entry.baselineX) || !std::isfinite(entry.baselineY) ||
			!std::isfinite(metrics.ascent) || !std::isfinite(metrics.descent) || !std::isfinite(metrics.ascent+metrics.descent) || metrics.ascent+metrics.descent<=0) { fail(); continue; }
		const float caretWidth=std::max(1.f,impl->Length(spec.caret,"width",ratio,viewportSize.x,ratio));
		const float inset=std::min(2.f*ratio,viewportSize.x*.25f);
		float scroll=entry.scroll;
		const float unscrolledOrigin=entry.baselineX-viewportOrigin.x+entry.scroll;
		if (edit) {
			const float x=unscrolledOrigin+caretX-scroll;
			// Match RmlUi's physical-pixel translation grid. An integer shift
			// commutes with its rounded glyph origin, so one layout settles the
			// text and editing ink without rounding the run's fractional pens.
			if (x<inset) scroll=std::floor(scroll+x-inset);
			else if (x+caretWidth>viewportSize.x-inset) scroll=std::ceil(scroll+x+caretWidth-(viewportSize.x-inset));
			scroll=std::clamp(scroll,0.f,std::ceil(std::max(0.f,unscrolledOrigin+entry.run->width+caretWidth+inset-viewportSize.x)));
		}
		changed|=impl->Property(spec.text,"left",Pixels(baseLeft-scroll));
		const float scrollDelta=scroll-entry.scroll; entry.scroll=scroll;
		entry.baselineX-=scrollDelta;
		const float top=entry.baselineY-metrics.ascent, height=metrics.ascent+metrics.descent;
		entry.localCaret={entry.baselineX+caretX,top,caretWidth,height};
		changed|=impl->Rectangle(entry,spec.caret,entry.baselineX+caretX,top,caretWidth,height);
		changed|=impl->Display(spec.caret,edit!=nullptr);
		const bool blink=steadyCaret || (edit && edit->composition) || std::fmod(std::max(0.0,seconds-entry.blinkEpoch),1.0)<.5;
		changed|=impl->Property(spec.caret,"opacity",blink ? "1" : "0");
		changed|=impl->Rectangle(entry,spec.selection,entry.baselineX+std::min(anchorX,caretX),top,std::abs(caretX-anchorX),height);
		changed|=impl->Display(spec.selection,edit && anchor!=caret);
		float firstX=0,lastX=0;
		const bool preedit=edit && edit->composition && entry.run->CaretPosition(compositionFirst,firstX) && entry.run->CaretPosition(compositionLast,lastX);
		if (preedit) changed|=impl->Rectangle(entry,spec.composition,entry.baselineX+firstX,entry.baselineY+metrics.descent,
			lastX-firstX,std::max(1.f,impl->Length(spec.composition,"height",ratio,viewportSize.y,ratio)));
		changed|=impl->Display(spec.composition,preedit);
		entry.geometryReady=scrollDelta==0;
	}
	return changed;
}
std::optional<NumberTextHit> NumberControlView::Hit(Rml::Element* hit,float x,float y,const Interaction& interaction,const std::string& captured) const {
	if (!std::isfinite(x) || !std::isfinite(y)) return {};
	for (const auto& [id,entry]:impl->entries) {
		if (!captured.empty() && captured!=id) continue;
		const auto view=interaction.Widget(id); if (!impl->Fresh(entry) || !view || !view->number || !view->number->active ||
			interaction.Focused()!=id || !interaction.CanActivate(id) || view->number->identity!=entry.identity) continue;
		auto* viewport=impl->Element(entry.spec.viewport); if (!viewport || !viewport->IsVisible(true)) continue;
		if (captured.empty() && !Within(hit,viewport)) continue;
		Rml::Vector2f point(x,y); if (!viewport->Project(point) || !std::isfinite(point.x) || !std::isfinite(point.y)) return {};
		const auto origin=viewport->GetAbsoluteOffset(Rml::BoxArea::Content),size=viewport->GetBox().GetSize(Rml::BoxArea::Content);
		if (captured.empty() && (point.x<origin.x || point.y<origin.y || point.x>=origin.x+size.x || point.y>=origin.y+size.y)) continue;
		const auto byte=entry.run->HitTestLtr(point.x-entry.baselineX); if (!byte) return {};
		return NumberTextHit{id,entry.identity,*byte};
	}
	return {};
}
std::optional<NumberTextGeometry> NumberControlView::Geometry(const std::string& id,const Interaction& interaction) const {
	const auto found=impl->entries.find(id); if (found==impl->entries.end() || !found->second.geometryReady) return {};
	const auto& entry=found->second; const auto view=interaction.Widget(id);
	if (!impl->Fresh(entry) || !view || !view->number || !view->number->active || interaction.Focused()!=id ||
		!interaction.CanActivate(id) || view->number->identity!=entry.identity) return {};
	Rml::Rectanglef caret,viewport;
	if (!Rml::ElementUtilities::GetBoundingBox(caret,impl->Element(entry.spec.caret),Rml::BoxArea::Border) ||
		!Rml::ElementUtilities::GetBoundingBox(viewport,impl->Element(entry.spec.viewport),Rml::BoxArea::Content)) return {};
	if (!Finite(BoundsOf(caret)) || !Finite(BoundsOf(viewport)) || caret.Width()<=0 || caret.Height()<=0 || viewport.Width()<=0 || viewport.Height()<=0) return {};
	return NumberTextGeometry{id,entry.identity,BoundsOf(caret),BoundsOf(viewport),entry.scroll};
}
std::shared_ptr<const TextRun> NumberControlView::CommandRun(const std::string& id,NumberEditIdentity expected,
	const Interaction& interaction,std::string& error) const {
	error.clear(); const auto found=impl->entries.find(id); const auto view=interaction.Widget(id);
	if (found==impl->entries.end() || !view || !view->number || !view->number->active ||
		!expected.session || !expected.revision || view->number->identity!=expected ||
		interaction.Focused()!=id || !interaction.CanActivate(id) || view->number->composition ||
		view->number->conflict || view->pending) { error="Number command editor is stale or unavailable"; return {}; }
	auto* parent=impl->Element(found->second.spec.text);
	if (!parent || !parent->IsVisible(true)) { error="Number command typography is unavailable"; return {}; }
	// The generated #text child inherits the wrapper's resolved typography, just
	// as in Paint. Its line text can still be an earlier edit; never measure that.
	auto* typography=parent;
	if (parent->GetNumChildren()==1 && parent->GetChild(0)->GetTagName()=="#text") typography=parent->GetChild(0);
	const auto font=typography->GetFontFaceHandle();
	if (!font || !impl->query) { error="Number command font is unavailable"; return {}; }
	auto run=impl->query(font,view->number->state.text,typography->GetComputedValues().letter_spacing());
	if (!run || !run->monotonicLtr || run->text!=view->number->state.text) {
		error="Number command requires a current scalar LTR text run"; return {};
	}
	return run;
}
} // namespace openq4::ui
