// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "ValueControlView.h"
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace openq4::ui {
namespace {
std::string Number(double value, int decimals = -1) {
	std::ostringstream stream; stream.imbue(std::locale::classic());
	if (decimals >= 0) { if (std::abs(value) < .5*std::pow(10.0,-decimals)) value = 0; stream << std::fixed << std::setprecision(decimals); }
	else stream << std::setprecision(std::numeric_limits<double>::max_digits10);
	stream << value; return stream.str();
}
std::string Pixels(float value) { return Number(std::round(value*1000.0)/1000.0)+"px"; }
bool Within(Rml::Element* element, Rml::Element* root) {
	for (; element; element = element->GetParentNode()) if (element == root) return true;
	return false;
}
float CssExtent(Rml::Element* element, float borderExtent, bool vertical) {
	if (element->GetComputedValues().box_sizing() == Rml::Style::BoxSizing::BorderBox) return std::max(0.0f,borderExtent);
	// CSS content sizes exclude both the border and padding edges.
	const auto frame = element->GetBox().GetFrameSize(Rml::BoxArea::Border) +
		element->GetBox().GetFrameSize(Rml::BoxArea::Padding);
	return std::max(0.0f,borderExtent-(vertical ? frame.y : frame.x));
}
}
struct ValueControlView::Impl {
	struct Entry {
		Control control;
		Rml::Element* anchor = nullptr;
		Rml::Element* inheritedFrom = nullptr;
		std::vector<std::string> opacityAncestry;
	};
	struct Applied { std::string requested, actual; };
	Rml::ElementDocument* document = nullptr;
	std::function<std::string(const std::string&)> translate;
	std::map<std::string,Entry> entries;
	std::map<std::string,Rml::Element*> elements;
	std::map<std::string,std::map<std::string,Value>> authored;
	std::map<std::pair<std::string,std::string>,Applied> applied;
	Rml::Element* Element(const std::string& id) const { const auto found = elements.find(id); return found == elements.end() ? nullptr : found->second; }
	bool Property(const std::string& id, const std::string& property, const std::string& value) {
		auto* element = Element(id); if (!element) return false;
		auto& previous = applied[{id,property}]; const auto* current = element->GetLocalProperty(property);
		if (previous.requested == value && current && previous.actual == current->ToString()) return false;
		if (!element->SetProperty(property,value)) return false;
		previous.requested = value; current = element->GetLocalProperty(property); previous.actual = current ? current->ToString() : std::string{};
		return true;
	}
	bool Text(const std::string& id, const std::string& value) {
		auto* element = Element(id); if (!element) return false;
		auto& previous = applied[{id,"text"}];
		if (previous.requested == value && previous.actual == element->GetInnerRML()) return false;
		element->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
		previous = {value,element->GetInnerRML()}; return true;
	}
	bool Display(const std::string& id, bool visible) {
		if (id.empty()) return false;
		const auto& properties = authored.at(id); const auto found = properties.find("display");
		const auto shown = found != properties.end() && found->second.text != "none" ? found->second.text : "block";
		return Property(id,"display",visible ? shown : "none");
	}
	std::string Translate(const std::string& value) const { return translate ? translate(value) : value; }
	std::string Label(const ChoiceOption& option) const {
		const auto label = Translate(option.label);
		if (!option.labelIndex) return label;
		size_t begin = 0;
		for (unsigned index = 0; index < *option.labelIndex; ++index) {
			const auto separator = label.find(';',begin); if (separator == std::string::npos) return {};
			begin = separator+1;
		}
		return label.substr(begin,label.find(';',begin)-begin);
	}
	std::string ValueText(const StateValue& value) const {
		if (const auto number = std::get_if<double>(&value)) return Number(*number);
		if (const auto text = std::get_if<std::string>(&value)) return Translate(*text);
		return std::get<bool>(value) ? "1" : "0";
	}
	float AuthoredLength(const std::string& id, const std::string& property, float ratio, float fallback) const {
		const auto found = authored.at(id).find(property);
		if (found == authored.at(id).end() || found->second.type != ValueType::Length) return fallback;
		if (found->second.unit == "dp") return static_cast<float>(found->second.data[0])*ratio;
		if (found->second.unit == "px") return static_cast<float>(found->second.data[0]);
		return fallback;
	}
	bool Inheritance(const Entry& entry, const ChoiceSpec& choice) {
		if (!entry.inheritedFrom) return false;
		bool changed = false; const auto& own = authored.at(choice.popup); const auto& computed = entry.inheritedFrom->GetComputedValues();
		const auto set = [&](const std::string& key, const std::string& value) { if (!own.contains(key)) changed |= Property(choice.popup,key,value); };
		set("font-family",computed.font_family()); set("font-size",Pixels(computed.font_size()));
		set("line-height",Pixels(computed.line_height().value)); set("letter-spacing",Pixels(computed.letter_spacing()));
		for (const auto* key : {"font-weight","font-style","color","text-align","white-space","word-break","direction"}) {
			if (const auto* property = entry.inheritedFrom->GetProperty(key)) set(key,property->ToString());
		}
		return changed;
	}
	bool Popup(const Entry& entry, const ChoiceSpec& choice, const WidgetViewState& view, int width, int height, float ratio,
		const std::function<double(const std::string&)>& opacity) {
		bool changed = Inheritance(entry,choice);
		changed |= Property(choice.popup,"position","absolute");
		changed |= Property(choice.popup,"z-index","1000000");
		changed |= Property(choice.viewport,"overflow","hidden");
		// Absolute row content can leave RmlUi's scroll extent equal to its
		// client extent. This viewport still always clips its authored rows.
		changed |= Property(choice.viewport,"clip","always");
		double alpha = 1;
		for (const auto& id : entry.opacityAncestry) alpha *= std::clamp(opacity(id),0.0,1.0);
		changed |= Property(choice.popup,"filter",alpha < 1 ? "opacity("+Number(alpha)+")" : "none");
		const bool open = view.popupOpen && entry.anchor->IsVisible(true) && width > 0 && height > 0;
		changed |= Display(choice.popup,open); if (!open) return changed;
		auto* popup = Element(choice.popup); auto* viewport = Element(choice.viewport); auto* content = Element(choice.content);
		Rml::Rectanglef anchor;
		// Runtime synchronizes stacking geometry and transform caches after
		// layout, before this placement pass, without an extra rendering pass.
		if (!Rml::ElementUtilities::GetBoundingBox(anchor,entry.anchor,Rml::BoxArea::Border) ||
			!std::isfinite(anchor.Left()) || !std::isfinite(anchor.Top()) || !std::isfinite(anchor.Width()) || !std::isfinite(anchor.Height()))
			return Display(choice.popup,false) || changed;
		std::vector<float> starts, ends; float next = 0;
		for (const auto& option : choice.options) {
			auto* row = Element(option.node); const float measured = row->GetBox().GetSize(Rml::BoxArea::Border).y;
			const float rowHeight = std::max(1.0f,measured > 0 ? measured : AuthoredLength(option.node,"height",ratio,32*ratio));
			const float offset = row->GetAbsoluteOffset(Rml::BoxArea::Border).y-content->GetAbsoluteOffset(Rml::BoxArea::Content).y;
			const float start = measured > 0 && std::isfinite(offset) ? std::max(next,offset) : next;
			starts.push_back(start); ends.push_back(start+rowHeight); next = start+rowHeight;
		}
		if (starts.empty()) return Display(choice.popup,false) || changed;
		const size_t first = std::min<size_t>(view.firstVisible,starts.size()-1);
		const size_t last = std::min(starts.size(),first+choice.visibleRows)-1;
		const auto popupFrame = popup->GetBox().GetFrameSize(Rml::BoxArea::Border);
		const auto viewportFrame = viewport->GetBox().GetFrameSize(Rml::BoxArea::Border);
		const float margin = std::min(4*ratio,std::min(width,height)*.1f);
		const float viewportTop = viewport->GetAbsoluteOffset(Rml::BoxArea::Border).y-popup->GetAbsoluteOffset(Rml::BoxArea::Content).y;
		const float chrome = std::max(0.0f,popupFrame.y+viewportFrame.y+std::max(0.0f,viewportTop));
		const float desired = ends[last]-starts[first]+chrome;
		const float below = std::max(0.0f,height-margin-anchor.Bottom());
		const float above = std::max(0.0f,anchor.Top()-margin);
		const bool flip = desired > below && above > below;
		const float room = flip ? above : below;
		const float outerHeight = std::max(0.0f,std::min(desired,room));
		const float visibleHeight = std::max(0.0f,outerHeight-chrome);
		const float outerWidth = std::max(0.0f,std::min(std::max(anchor.Width(),1.0f),width-2*margin));
		if (visibleHeight < 1 || outerWidth < 1) return Display(choice.popup,false) || changed;
		const float x = std::clamp(anchor.Left(),margin,std::max(margin,width-margin-outerWidth));
		const float y = std::clamp(flip ? anchor.Top()-outerHeight : anchor.Bottom(),margin,std::max(margin,height-margin-outerHeight));
		float scroll = starts[first];
		for (size_t i = 0; i < choice.options.size(); ++i) if (choice.options[i].id == view.highlight) {
			if (ends[i] > scroll+visibleHeight) scroll = ends[i]-visibleHeight;
			if (starts[i] < scroll) scroll = starts[i];
			break;
		}
		const auto documentOffset = document->GetAbsoluteOffset(Rml::BoxArea::Padding);
		changed |= Property(choice.popup,"left",Pixels(x-documentOffset.x-popup->GetBox().GetEdge(Rml::BoxArea::Margin,Rml::BoxEdge::Left)));
		changed |= Property(choice.popup,"top",Pixels(y-documentOffset.y-popup->GetBox().GetEdge(Rml::BoxArea::Margin,Rml::BoxEdge::Top)));
		changed |= Property(choice.popup,"width",Pixels(CssExtent(popup,outerWidth,false)));
		changed |= Property(choice.popup,"height",Pixels(CssExtent(popup,outerHeight,true)));
		changed |= Property(choice.viewport,"height",Pixels(CssExtent(viewport,visibleHeight+viewportFrame.y,true)));
		changed |= Property(choice.content,"top",Pixels(-scroll));
		return changed;
	}
};
ValueControlView::ValueControlView() : impl(std::make_unique<Impl>()) {}
ValueControlView::~ValueControlView() = default;
void ValueControlView::Reset() { impl = std::make_unique<Impl>(); }
bool ValueControlView::Initialize(const DocumentModel& model, Rml::ElementDocument& document,
	std::function<std::string(const std::string&)> translate, std::string& error) {
	error.clear(); auto candidate = std::make_unique<Impl>(); candidate->document = &document; candidate->translate = std::move(translate);
	std::vector<const Node*> pending{&model.root};
	while (!pending.empty()) {
		const auto* node = pending.back(); pending.pop_back();
		auto* element = document.GetElementById(node->id);
		if (!element) { error = "Missing canonical value-control view node '"+node->id+"'"; return false; }
		candidate->elements[node->id] = element; candidate->authored[node->id] = node->properties;
		if (node->control && node->control->role != ControlRole::Button) candidate->entries[node->id] = {*node->control,element,nullptr};
		for (const auto& child : node->children) pending.push_back(&child);
	}
	for (auto& [id,entry] : candidate->entries) if (const auto* choice = std::get_if<ChoiceSpec>(&entry.control.widget)) {
		auto* popup = candidate->Element(choice->popup);
		if (!popup || !popup->GetParentNode()) { error = "Choice popup has no derived parent"; return false; }
		entry.inheritedFrom = popup->GetParentNode();
		for (auto* ancestor = popup; ancestor && ancestor != &document; ancestor = ancestor->GetParentNode())
			if (candidate->authored.contains(ancestor->GetId())) entry.opacityAncestry.push_back(ancestor->GetId());
	}
	for (const auto& [id,entry] : candidate->entries) if (const auto* choice = std::get_if<ChoiceSpec>(&entry.control.widget)) {
		auto* popup = candidate->Element(choice->popup);
		// A derived DOM portal escapes the canonical ancestors' clipping. Source,
		// stable IDs and the native editor's canonical hierarchy are unchanged.
		document.AppendChild(popup->GetParentNode()->RemoveChild(popup));
		candidate->Property(choice->popup,"position","absolute");
		candidate->Property(choice->popup,"z-index","1000000");
		candidate->Property(choice->viewport,"overflow","hidden");
		candidate->Property(choice->viewport,"clip","always");
		candidate->Display(choice->popup,false);
	}
	impl = std::move(candidate); return true;
}
bool ValueControlView::Paint(const Interaction& interaction, const std::map<std::string,ControlReadback>& readbacks,
	int width, int height, float ratio, const std::function<double(const std::string&)>& opacity) {
	if (!impl->document || !std::isfinite(ratio) || ratio <= 0) return false;
	bool changed = false;
	for (const auto& [id,entry] : impl->entries) {
		const auto readback = readbacks.find(id); const auto view = interaction.Widget(id);
		if (readback == readbacks.end() || !view) continue;
		const auto& accepted = readback->second;
		if (const auto* toggle = std::get_if<ToggleSpec>(&entry.control.widget)) {
			changed |= impl->Display(toggle->checkedPart,!accepted.mixed && std::get<bool>(accepted.value));
			changed |= impl->Display(toggle->mixedPart,accepted.mixed);
		} else if (const auto* slider = std::get_if<SliderSpec>(&entry.control.widget)) {
			auto* track = impl->Element(slider->track); auto* thumb = impl->Element(slider->thumb);
			const double value = std::get<double>(view->preview ? *view->preview : accepted.value);
			const double fraction = std::clamp((value-slider->minimum)/(slider->maximum-slider->minimum),0.0,1.0);
			const auto trackSize = track->GetBox().GetSize(Rml::BoxArea::Content); const auto thumbSize = thumb->GetBox().GetSize(Rml::BoxArea::Border);
			const float extent = slider->vertical ? trackSize.y : trackSize.x; const float knob = slider->vertical ? thumbSize.y : thumbSize.x;
			const float travel = std::max(0.0f,extent-knob);
			const auto offset = track->GetAbsoluteOffset(Rml::BoxArea::Content);
			const auto parent = thumb->GetOffsetParent(); const auto parentOffset = parent ? parent->GetAbsoluteOffset(Rml::BoxArea::Padding) : Rml::Vector2f{};
			const float margin = thumb->GetBox().GetEdge(Rml::BoxArea::Margin,slider->vertical ? Rml::BoxEdge::Top : Rml::BoxEdge::Left);
			const float start = (slider->vertical ? offset.y-parentOffset.y : offset.x-parentOffset.x)-margin;
			changed |= impl->Property(slider->fill,slider->vertical ? "height" : "width",Pixels(CssExtent(impl->Element(slider->fill),static_cast<float>(extent*fraction),slider->vertical)));
			changed |= impl->Property(slider->thumb,slider->vertical ? "top" : "left",Pixels(start+static_cast<float>(travel*(slider->vertical ? 1-fraction : fraction))));
			changed |= impl->Text(slider->valueText,Number(value,static_cast<int>(slider->decimals)));
		} else if (const auto* choice = std::get_if<ChoiceSpec>(&entry.control.widget)) {
			std::string value = impl->ValueText(accepted.value);
			for (const auto& option : choice->options) {
				const auto label = impl->Label(option); const bool selected = option.value == accepted.value;
				if (selected) value = label;
				changed |= impl->Text(option.labelPart,label);
				changed |= impl->Display(option.selectedPart,selected);
				changed |= impl->Display(option.highlightPart,view->popupOpen && view->highlight == option.id);
			}
			changed |= impl->Text(choice->valueText,value);
			changed |= impl->Popup(entry,*choice,*view,width,height,ratio,opacity);
		}
	}
	return changed;
}
PointerPartResult ValueControlView::PointerPart(Rml::Element* hit, float x, float y, const Interaction& interaction) const {
	PointerPartResult result;
	if (!impl->document) return result;
	const auto captured = interaction.CapturedPointerControl();
	for (const auto& [id,entry] : impl->entries) {
		if (!captured.empty() && captured != id) continue;
		if (const auto* slider = std::get_if<SliderSpec>(&entry.control.widget)) {
			auto* track = impl->Element(slider->track);
			if (captured != id && (!interaction.CanActivate(id) || !Within(hit,track))) continue;
			result.control = id;
			Rml::Vector2f point(x,y);
			const auto size = track->GetBox().GetSize(Rml::BoxArea::Content); const auto thumb = impl->Element(slider->thumb)->GetBox().GetSize(Rml::BoxArea::Border);
			const float extent = slider->vertical ? size.y : size.x; const float knob = slider->vertical ? thumb.y : thumb.x;
			if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(extent) || !std::isfinite(knob) || extent <= knob || !track->Project(point)) { result.invalidProjection = true; return result; }
			point -= track->GetAbsoluteOffset(Rml::BoxArea::Content);
			const double fraction = ((slider->vertical ? point.y : point.x)-knob*.5)/(extent-knob);
			if (!std::isfinite(fraction)) result.invalidProjection = true;
			else result.fraction = slider->vertical ? 1-fraction : fraction;
			return result;
		}
		if (captured.empty()) if (const auto* choice = std::get_if<ChoiceSpec>(&entry.control.widget)) {
			const auto view = interaction.Widget(id);
			if (!view || !view->popupOpen || !Within(hit,impl->Element(choice->popup))) continue;
			Rml::Vector2f point(x,y);
			auto* popup = impl->Element(choice->popup);
			if (!popup->Project(point) || !popup->IsPointWithinElement(point)) return result;
			result.control = id;
			auto* viewport = impl->Element(choice->viewport);
			point = {x,y};
			if (!viewport->Project(point)) return result;
			const auto offset = viewport->GetAbsoluteOffset(Rml::BoxArea::Padding);
			const auto extent = viewport->GetBox().GetSize(Rml::BoxArea::Padding);
			if (point.x < offset.x || point.y < offset.y || point.x >= offset.x+extent.x || point.y >= offset.y+extent.y) return result;
			for (const auto& option : choice->options) if (Within(hit,impl->Element(option.node))) { result.option = option.id; break; }
			return result;
		}
	}
	return result;
}
} // namespace openq4::ui
