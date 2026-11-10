// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Runtime.h"
#include "State.h"
#include "VectorElement.h"
#include "HudElement.h"
#include "ValueControlView.h"
#include "ScrollbarView.h"
#include "TextRun.h"
#include "NumberControlView.h"
#include "FloatChars.h"
#include "GeneratedImagePool.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Filter.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StyleSheetSpecification.h>
#include <RmlUi/Core/Transform.h>
#include <RmlUi/Core/TransformPrimitive.h>
#include <json/json.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openq4::ui {
namespace {
bool SnapshotFields(const Json::Value& value, std::initializer_list<const char*> fields) {
	if (!value.isObject() || value.size() != fields.size()) return false;
	for (const auto* field : fields) if (!value.isMember(field)) return false;
	return true;
}
Json::Value SnapshotTextEditState(const TextEditState& state) {
	Json::Value result(Json::objectValue);
	result["text"] = state.text;
	result["anchor"] = Json::UInt64(state.anchor); result["caret"] = Json::UInt64(state.caret);
	return result;
}
// Bound decoded payloads before copying JSON strings into the edit model. The
// model additionally validates UTF-8 boundaries, field policy and history.
bool ReadSnapshotText(const Json::Value& source, std::string& text, std::size_t& remaining) {
	const char* begin = nullptr; const char* end = nullptr;
	if (!source.getString(&begin,&end)) return false;
	const auto size = static_cast<std::size_t>(end-begin);
	if (size > TextInputMaxBytes || size > remaining) return false;
	text.assign(begin,end); remaining -= size; return true;
}
bool ReadSnapshotTextEditState(const Json::Value& source, TextEditState& state, std::size_t& remaining) {
	if (!SnapshotFields(source,{"text","anchor","caret"}) || !source["anchor"].isUInt64() || !source["caret"].isUInt64() ||
		!ReadSnapshotText(source["text"],state.text,remaining) || source["anchor"].asUInt64() > state.text.size() ||
		source["caret"].asUInt64() > state.text.size()) return false;
	state.anchor = static_cast<std::size_t>(source["anchor"].asUInt64());
	state.caret = static_cast<std::size_t>(source["caret"].asUInt64()); return true;
}
Json::Value SnapshotNumberEditor(const NumberEditorSnapshot& editor) {
	Json::Value result(Json::objectValue); result["state"] = SnapshotTextEditState(editor.state);
	for (const auto* name : {"undo","redo"}) result[name] = Json::Value(Json::arrayValue);
	for (const auto& state : editor.undo) result["undo"].append(SnapshotTextEditState(state));
	for (const auto& state : editor.redo) result["redo"].append(SnapshotTextEditState(state));
	result["baselineValue"] = editor.baselineValue; result["baselineText"] = editor.baselineText; result["conflict"] = editor.conflict;
	return result;
}
bool ReadSnapshotNumberEditor(const Json::Value& source, NumberEditorSnapshot& editor, std::size_t& remaining) {
	if (!SnapshotFields(source,{"state","undo","redo","baselineValue","baselineText","conflict"}) ||
		!source["baselineValue"].isNumeric() || !std::isfinite(source["baselineValue"].asDouble()) || !source["conflict"].isBool() ||
		!source["undo"].isArray() || !source["redo"].isArray() || source["undo"].size() > TextEditBuffer::MaxHistoryEntries ||
		source["redo"].size() > TextEditBuffer::MaxHistoryEntries-source["undo"].size() ||
		!ReadSnapshotTextEditState(source["state"],editor.state,remaining) || !ReadSnapshotText(source["baselineText"],editor.baselineText,remaining)) return false;
	editor.baselineValue = source["baselineValue"].asDouble(); editor.conflict = source["conflict"].asBool();
	std::size_t historyRemaining = TextEditBuffer::MaxHistoryTextBytes;
	for (const auto* name : {"undo","redo"}) for (const auto& item : source[name]) {
		TextEditState state;
		if (!ReadSnapshotTextEditState(item,state,historyRemaining) || state.text.size() > remaining) return false;
		remaining -= state.text.size();
		(std::strcmp(name,"undo") == 0 ? editor.undo : editor.redo).push_back(std::move(state));
	}
	return true;
}
Json::Value SnapshotValue(const Value& value) {
	Json::Value result(Json::objectValue);
	result["type"] = unsigned(value.type); result["unit"] = value.unit; result["text"] = value.text;
	result["data"] = Json::Value(Json::arrayValue);
	for (double component : value.data) result["data"].append(component);
	return result;
}
bool ReadSnapshotValue(const Json::Value& source, Value& value) {
	if (!SnapshotFields(source,{"type","unit","text","data"}) || !source["type"].isUInt() || source["type"].asUInt() > unsigned(ValueType::Image) ||
		!source["unit"].isString() || !source["text"].isString() || !source["data"].isArray() || source["data"].size() != 5) return false;
	value.type = ValueType(source["type"].asUInt()); value.unit = source["unit"].asString(); value.text = source["text"].asString();
	for (Json::ArrayIndex i = 0; i < 5; ++i) {
		if (!source["data"][i].isNumeric() || !std::isfinite(source["data"][i].asDouble())) return false;
		value.data[i] = source["data"][i].asDouble();
	}
	return true;
}
Json::Value SnapshotPresentationValue(const PresentationValue& value) {
	Json::Value result(Json::objectValue);
	result["type"] = unsigned(value.type); result["text"] = value.text;
	result["data"] = Json::Value(Json::arrayValue);
	for (double part : value.data) result["data"].append(part);
	return result;
}
bool ReadSnapshotPresentationValue(const Json::Value& source, PresentationValue& value) {
	if (!SnapshotFields(source,{"type","text","data"}) || !source["type"].isUInt() || source["type"].asUInt() > unsigned(PresentationType::Vector4) ||
		!source["text"].isString() || !source["data"].isArray() || source["data"].size() != 4) return false;
	value.type = PresentationType(source["type"].asUInt()); value.text = source["text"].asString();
	for (Json::ArrayIndex i = 0; i < 4; ++i) {
		if (!source["data"][i].isNumeric()) return false;
		value.data[i] = source["data"][i].asDouble();
	}
	return ValidPresentationValue(value);
}
// JsonCpp accepts some non-JSON numeric spellings and raw string controls.
// Snapshot input is strict JSON; reject those forms before parsing the DOM.
bool SnapshotLexicalForms(const std::string& source) {
	bool string = false;
	auto hex4 = [&](size_t at) {
		if (at+4 > source.size()) return -1;
		int value = 0;
		for (size_t n = at; n < at+4; ++n) {
			const char c = source[n];
			const int digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
			if (digit < 0) return -1;
			value = value*16+digit;
		}
		return value;
	};
	for (size_t i = 0; i < source.size(); ++i) {
		const char c = source[i];
		if (c == '"') { string = !string; continue; }
		if (string) {
			if (static_cast<unsigned char>(c) < 0x20) return false;
			if (c == '\\') {
				if (++i >= source.size()) return false;
				if (source[i] == 'u') {
					const int cp = hex4(i+1);
					if (cp < 0 || (cp >= 0xdc00 && cp <= 0xdfff)) return false;
					if (cp >= 0xd800 && cp <= 0xdbff) {
						if (i+6 >= source.size() || source[i+5] != '\\' || source[i+6] != 'u') return false;
						const int low = hex4(i+7); if (low < 0xdc00 || low > 0xdfff) return false;
						i += 6;
					}
					i += 4;
				}
			}
			continue;
		}
		if (c != '-' && (c < '0' || c > '9')) continue;
		size_t p = i;
		auto digit = [&]() { return p < source.size() && source[p] >= '0' && source[p] <= '9'; };
		if (source[p] == '-') ++p;
		if (!digit()) return false;
		if (source[p] == '0') ++p; else while (digit()) ++p;
		if (p < source.size() && source[p] == '.') { ++p; if (!digit()) return false; while (digit()) ++p; }
		if (p < source.size() && (source[p] == 'e' || source[p] == 'E')) {
			++p; if (p < source.size() && (source[p] == '+' || source[p] == '-')) ++p;
			if (!digit()) return false; while (digit()) ++p;
		}
		if (p < source.size() && std::string_view(",]} \t\r\n").find(source[p]) == std::string_view::npos) return false;
		i = p-1;
	}
	return !string;
}
float Positive(float value, float fallback = 1) {
	return std::isfinite(value) && value > 0 ? value : fallback;
}

// Sutherland-Hodgman clipping of one triangle. Interpolate the entire vertex,
// including premultiplied colour; the legacy winding clip loses that payload.
Vertex Interpolate(const Vertex& a, const Vertex& b, float t) {
	return {a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
		a.u + (b.u-a.u)*t, a.v + (b.v-a.v)*t,
		a.r + (b.r-a.r)*t, a.g + (b.g-a.g)*t,
		a.b + (b.b-a.b)*t, a.a + (b.a-a.a)*t};
}

void Clip(std::vector<Vertex>& polygon, std::vector<Vertex>& output, int axis, float edge, bool greater) {
	if (polygon.empty()) return;
	output.clear();
	auto distance = [&](const Vertex& v) { return ((axis == 0 ? v.x : v.y) - edge) * (greater ? 1 : -1); };
	Vertex previous = polygon.back();
	float previousDistance = distance(previous);
	for (const Vertex& current : polygon) {
		const float d = distance(current);
		if ((d >= 0) != (previousDistance >= 0)) {
			output.push_back(Interpolate(previous, current, previousDistance / (previousDistance - d)));
		}
		if (d >= 0) output.push_back(current);
		previous = current;
		previousDistance = d;
	}
	polygon.swap(output);
}

// Clip against the half-plane a*x + b*y + c >= 0 (an edge of a convex clip).
void ClipHalfPlane(std::vector<Vertex>& polygon, std::vector<Vertex>& output, float a, float b, float c) {
	if (polygon.empty()) return;
	output.clear();
	auto distance = [&](const Vertex& v) { return a*v.x + b*v.y + c; };
	Vertex previous = polygon.back();
	float previousDistance = distance(previous);
	for (const Vertex& current : polygon) {
		const float d = distance(current);
		if ((d >= 0) != (previousDistance >= 0)) {
			output.push_back(Interpolate(previous, current, previousDistance / (previousDistance - d)));
		}
		if (d >= 0) output.push_back(current);
		previous = current;
		previousDistance = d;
	}
	polygon.swap(output);
}

// A convex clip region in output pixels, ordered so every edge keeps the
// region where a*x + b*y + c >= 0 (clockwise as seen on screen).
struct ClipPolygon {
	static constexpr int Capacity = 16;
	std::array<Rml::Vector2f,Capacity> points{};
	int count = 0;
	bool Same(const ClipPolygon& other) const {
		return count == other.count && std::memcmp(points.data(),other.points.data(),sizeof(Rml::Vector2f)*count) == 0;
	}
};

struct Geometry {
	std::vector<Rml::Vertex> vertices;
	std::vector<int> indices;
	// A static element renders the same geometry with the same state every
	// frame. RenderGeometry keeps its last submission and reuses it while all
	// of the inputs it was computed from are unchanged; the host receives the
	// geometry's identity and a revision that changes with that submission.
	std::uint64_t identity = 0, revision = 0;
	struct Submission {
		Rml::Vector2f translation;
		Rml::Matrix4f transform = Rml::Matrix4f::Identity();
		std::array<float,4> clip{};
		ClipPolygon polygon;
		Rml::TextureHandle texture = 0;
		std::uintptr_t multiply = 0;
		std::uint64_t firstFrame = 0;
		bool hasTransform = false, clipping = false;
		bool Matches(const Submission& other) const {
			return std::memcmp(&translation,&other.translation,sizeof(translation)) == 0 && hasTransform == other.hasTransform &&
				(!hasTransform || std::memcmp(&transform,&other.transform,sizeof(transform)) == 0) && clipping == other.clipping &&
				(!clipping || (std::memcmp(clip.data(),other.clip.data(),sizeof(clip)) == 0 && polygon.Same(other.polygon))) &&
				texture == other.texture && multiply == other.multiply;
		}
		std::vector<Vertex> vertices; // Transformed, or clipped with indices.
		std::vector<int> indices;
		std::uintptr_t material = 0;
		Bounds bounds;
		bool draws = false, inside = false, fallback = false;
	};
	std::shared_ptr<Submission> submission;
};
struct ImageSampling {
	std::shared_ptr<const ImageCoordinates> coordinates;
	Vertex origin{};
	float bx=0, by=0, cx=0, cy=0, determinant=1;
};
struct RecordedDraw {
	std::vector<Vertex> vertices;
	std::vector<int> indices;
	std::uintptr_t texture = 0;
	std::uint64_t mesh = 0, revision = 0;
	std::shared_ptr<const Geometry::Submission> retained;
	ImageSampling sampling;
	std::shared_ptr<const float> opacitySource;
	std::optional<LinearMask> linearMask;
	std::shared_ptr<const LinearMask> liveLinearMask;
	Bounds linearClip;
	const std::vector<Vertex>& Vertices() const { return retained ? retained->vertices : vertices; }
	const std::vector<int>& Indices() const { return retained ? retained->indices : indices; }
};
// The speed streak's RmlUi filter, `streak(<0..1>)` in a backdrop-filter: the
// renderer compiles it like soft focus and the host draws it.
class StreakFilter final : public Rml::Filter {
public:
	explicit StreakFilter(float strength) : strength(strength) {}
	Rml::CompiledFilter CompileFilter(Rml::Element* element) const override {
		return element->GetRenderManager()->CompileFilter("streak",Rml::Dictionary{{"value",Rml::Variant(strength)}});
	}
private:
	float strength;
};
class StreakFilterInstancer final : public Rml::FilterInstancer {
public:
	StreakFilterInstancer() {
		value = RegisterProperty("value","0").AddParser("number").GetId();
		RegisterShorthand("filter","value",Rml::ShorthandType::FallThrough);
	}
	Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String&, const Rml::PropertyDictionary& properties) override {
		const Rml::Property* property = properties.GetProperty(value);
		return property ? Rml::MakeShared<StreakFilter>(property->Get<float>()) : nullptr;
	}
private:
	Rml::PropertyId value = Rml::PropertyId::Invalid;
};

// A canonical leaf keeps the same isolated opacity semantics as RmlUi's
// opacity filter. The owner also supplies the live value for frame replay.
class LeafOpacityFilter final : public Rml::Filter {
public:
	explicit LeafOpacityFilter(float value) : value(value) {}
	Rml::CompiledFilter CompileFilter(Rml::Element* element) const override {
		return element->GetRenderManager()->CompileFilter("q4-opacity",Rml::Dictionary{
			{"value",Rml::Variant(value)},{"element",Rml::Variant(static_cast<void*>(element))}});
	}
private:
	float value;
};
class LeafOpacityFilterInstancer final : public Rml::FilterInstancer {
public:
	LeafOpacityFilterInstancer() {
		value=RegisterProperty("value","1").AddParser("number").GetId();
		RegisterShorthand("filter","value",Rml::ShorthandType::FallThrough);
	}
	Rml::SharedPtr<Rml::Filter> InstanceFilter(const Rml::String&, const Rml::PropertyDictionary& properties) override {
		const auto* property=properties.GetProperty(value);
		return property ? Rml::MakeShared<LeafOpacityFilter>(property->Get<float>()) : nullptr;
	}
private:
	Rml::PropertyId value=Rml::PropertyId::Invalid;
};

struct Filter {
	float opacity = 1;
	std::uint32_t mask = 0;
	int width = 0, height = 0;
	std::uint64_t generation = 0;
	// Soft focus: a backdrop blur sigma in physical pixels and a saturation;
	// or the speed streak's strength.
	float blur = 0, saturation = 1, streak = 0;
	bool backdrop = false;
	std::shared_ptr<const std::vector<RecordedDraw>> recording;
	std::shared_ptr<const float> opacitySource;
	bool textOpacity=false;
};

// The host's composition target IDs are process-wide. A mask may retain its
// lease after the stack layer that produced it has been popped.
class Renderer;
struct LayerPool {
	struct Slot { Renderer* owner = nullptr; int width = 0, height = 0; std::uint64_t frame = 0; bool cached = false; };
	std::array<Slot,49> slots{};
};

class Renderer final : public Rml::RenderInterface, public ImageSamplingInterface, public LinearMaskInterface {
public:
	Renderer(Host& h, RuntimeStatistics& statistics) : host(h), statistics(statistics) {}
	bool SubmitLinearMask(const LinearMask& mask, std::shared_ptr<const LinearMask> live) override {
		if (failed || stack.empty() || !stack.back().pending || !stack.back().draws.empty() ||
			(clipMaskEnabled && clipPolygon.count) || !host.CanDrawLinearMask()) return false;
		if (!live) InvalidateFrame();
		RecordedDraw draw; draw.linearMask=mask; draw.liveLinearMask=std::move(live); draw.linearClip=ClipBounds();
		stack.back().draws.push_back(std::move(draw)); return true;
	}
	void Attach(LayerPool& value) { pool = &value; }
	void Detach() {
		EndFrame();
		InvalidateFrame(); frameCommands.clear();
		ClearLayerCache();
		if (pool) for (auto& slot : pool->slots) if (slot.owner == this) {
			host.Log(true,"Retained context leaked a composition lease"); slot.owner = nullptr;
		}
		pool = nullptr;
	}
	void BeginFrame(int width, int height) {
		if (width != viewportWidth || height != viewportHeight) { ++viewportGeneration; InvalidateFrame(); ClearLayerCache(); }
		viewportWidth = width; viewportHeight = height; failed = false;
	}
	void ExternalPixelsChanged() { InvalidateFrame(); ClearLayerCache(); }
	void EndFrame() {
		if (!stack.empty()) {
			failed = true;
			host.Log(true,"Unbalanced retained composition layers"); host.EndLayer(0);
			for (const auto& layer : stack) ReleaseSlot(layer.slot);
			stack.clear();
		}
	}
	bool Succeeded() const { return !failed; }
	void SetImageSampling(std::shared_ptr<const ImageCoordinates> coordinates) override { imageSampling=std::move(coordinates); }
	void InvalidateFrame() { frameValid=false; recordFrame=false; }
	bool ReplayFrame() {
		if (!frameValid || failed) return false;
		for (const auto& command:frameCommands) if (command.draw.linearMask &&
			(!command.draw.liveLinearMask || !host.CanDrawLinearMask() || !host.CanCompositeMaskedLayer())) {
			InvalidateFrame(); return false;
		}
		for (const auto& command:frameCommands) if (command.directOpacity &&
			!host.CanDrawMeshOpacity(command.draw.mesh,command.draw.revision)) { InvalidateFrame(); return false; }
		// Reserve transient backdrop targets before drawing anything to the
		// destination. Allocation may evict a cached plate; in that case the
		// ordinary traversal rebuilds the recording before any stale slot is read.
		std::vector<std::uint32_t> backdrops;
		for (const auto& command:frameCommands) if (command.backdrop) {
			const auto& clip=command.clip;
			if (command.opacity==1 && command.streak==0 && clip.x<=0 && clip.y<=0 &&
				clip.x+clip.width>=viewportWidth && clip.y+clip.height>=viewportHeight &&
				host.CanSoftenBackdropDirect(command.blur,command.saturation,command.backdropRegion)) {
				backdrops.push_back(0); continue;
			}
			const auto slot=AllocateSlot(true);
			if (slot) backdrops.push_back(slot);
			if (!slot || !frameValid) {
				for (const auto target:backdrops) ReleaseSlot(target);
				host.EndLayer(0); return false;
			}
		}
		host.EndLayer(0);
		for (const auto& command:frameCommands) if (command.slot &&
			(!pool || pool->slots[command.slot].owner!=this || !pool->slots[command.slot].cached)) {
			for (const auto target:backdrops) ReleaseSlot(target);
			InvalidateFrame(); return false;
		}
		size_t backdropIndex=0;
		for (auto& command:frameCommands) {
			if (command.backdrop) {
				const auto slot=backdrops[backdropIndex++];
				const bool drawn=!slot ? host.SoftenBackdropDirect(command.blur,command.saturation,command.backdropRegion) :
					command.streak>0 ? host.StreakBackdrop(slot,command.streak,command.backdropRegion) :
					host.SoftenBackdrop(slot,command.blur,command.saturation,command.backdropRegion);
				if (drawn) ++statistics.backdropComposites; else ++statistics.backdropFallbacks;
				if (slot) { host.CompositeLayer(slot,0,command.opacity,command.clip); ++statistics.layerPushes; }
				else ++statistics.layerElisions;
				++statistics.layerComposites;
			} else if (command.directOpacity) {
				const auto& draw=command.draw;
				host.DrawMeshOpacity(draw.mesh,draw.revision,draw.opacitySource ? *draw.opacitySource : command.opacity);
				++statistics.drawCalls; statistics.submittedVertices+=draw.Vertices().size(); statistics.submittedIndices+=draw.Indices().size();
				++statistics.layerElisions; ++statistics.layerComposites;
			} else if (command.slot) {
				pool->slots[command.slot].frame=host.RenderFrame();
				for (auto& cached:layerCache) if (cached.slot==command.slot) cached.used=++cacheClock;
				if (command.draw.linearMask) {
					host.DrawLinearMask(command.slot,*command.draw.liveLinearMask,command.clip,command.opacity);
					++statistics.drawCalls; statistics.submittedVertices+=4; statistics.submittedIndices+=6;
					++statistics.maskApplications; ++statistics.directMaskDraws;
				} else host.CompositeLayer(command.slot,0,command.draw.opacitySource ? *command.draw.opacitySource : command.opacity,command.clip);
				++statistics.layerComposites; ++statistics.layerCacheHits;
			} else {
				auto& draw=command.draw;
				if (draw.opacitySource) for (size_t i=0;i<draw.vertices.size();++i) {
					auto& v=draw.vertices[i]; const auto& c=command.baseColours[i]; const float opacity=*draw.opacitySource;
					v.r=c[0]*opacity; v.g=c[1]*opacity; v.b=c[2]*opacity; v.a=c[3]*opacity;
				}
				if (draw.sampling.coordinates) {
					const auto& values=*draw.sampling.coordinates;
					const float u0=float(.5+values[0]-.5*values[2]), v0=float(.5+values[1]-.5*values[3]);
					const float u1=float(u0+values[2]), v1=float(v0+values[3]);
					for (size_t i=0;i<draw.vertices.size();++i) {
						draw.vertices[i].u=u0+command.imageFractions[i].x*(u1-u0);
						draw.vertices[i].v=v0+command.imageFractions[i].y*(v1-v0);
					}
				}
				DrawNow(draw.Vertices(),draw.Indices(),draw.texture,draw.mesh,draw.revision);
			}
		}
		for (const auto target:backdrops) ReleaseSlot(target);
		statistics.visibleVectorCacheBytes=frameVectorBytes;
		++statistics.frameReplays; return true;
	}
	void RecordFrame(bool enabled) { InvalidateFrame(); frameCommands.clear(); recordFrame=enabled; frameRecordedVertices=0; }
	void FinishRecording() { frameValid=recordFrame && !failed && statistics.liveTextRuns==0; recordFrame=false;
		frameVectorBytes=statistics.visibleVectorCacheBytes; if (!frameValid) frameCommands.clear(); }
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
		if (vertices.empty() || vertices.size() > 262144 || indices.empty() || indices.size() % 3 != 0) return 0;
		for (const int i : indices) if (i < 0 || static_cast<size_t>(i) >= vertices.size()) return 0;
		auto geometry = std::make_unique<Geometry>();
		geometry->vertices.assign(vertices.begin(), vertices.end());
		geometry->indices.assign(indices.begin(), indices.end());
		static std::uint64_t nextIdentity = 0;
		geometry->identity = ++nextIdentity;
		++statistics.geometryCompiles; ++statistics.residentGeometryCount;
		statistics.residentGeometryBytes += Bytes(*geometry);
		return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
		if (!handle) return;
		auto* geometry = reinterpret_cast<Geometry*>(handle);
		frameValid=false;
		// A replaced image geometry has not been submitted in this recording.
		// Its independently owned sampling values may remain in a replay.
		if (recordFrame) for (const auto& command:frameCommands)
			if (command.draw.mesh==geometry->identity) { recordFrame=false; break; }
		--statistics.residentGeometryCount; statistics.residentGeometryBytes -= Bytes(*geometry);
		if (geometry->submission) host.ReleaseMesh(geometry->identity);
		delete geometry;
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
		float left = 0, top = 0, right = 0, bottom = 0;
		const bool clipping = ActiveClip(left, top, right, bottom);
		if (failed || !handle || (clipping && (right <= left || bottom <= top))) return;
		Geometry& geometry = *reinterpret_cast<Geometry*>(handle);
		Geometry::Submission inputs;
		inputs.translation = translation; inputs.hasTransform = hasTransform; inputs.clipping = clipping; inputs.texture = texture;
		if (hasTransform) inputs.transform = transform;
		if (clipping) { inputs.clip = {left,top,right,bottom}; if (clipMaskEnabled) inputs.polygon = clipPolygon; }
		if (texture == MultiplyTexture) inputs.multiply = stack.empty() ? host.MultiplyMaterial() : 0;
		if (!geometry.submission || !geometry.submission->Matches(inputs)) {
			// A pending layer or immutable mask may still refer to the previous
			// submission. Copy on change, never copy its vertices every frame.
			if (!geometry.submission || geometry.submission.use_count()!=1) geometry.submission = std::make_shared<Geometry::Submission>();
			auto& output = *geometry.submission;
			output.firstFrame=host.RenderFrame();
			output.translation = inputs.translation; output.transform = inputs.transform; output.clip = inputs.clip; output.polygon = inputs.polygon;
			output.texture = inputs.texture; output.multiply = inputs.multiply;
			output.hasTransform = inputs.hasTransform; output.clipping = inputs.clipping;
			Prepare(geometry,output,translation,left,top,right,bottom);
			if (output.draws) {
				float x0=OpenClip,y0=OpenClip,x1=-OpenClip,y1=-OpenClip;
				for (const auto& v:output.vertices) { x0=std::min(x0,v.x); y0=std::min(y0,v.y); x1=std::max(x1,v.x); y1=std::max(y1,v.y); }
				output.bounds={x0,y0,x1-x0,y1-y0};
			}
			++geometry.revision;
		}
		const auto& output = *geometry.submission;
		if (output.fallback) ++statistics.multiplyFallbacks;
		ImageSampling sampling;
		if (imageSampling && output.draws) {
			const auto& vertices=output.vertices;
			if (output.inside && vertices.size()==4) {
				const auto& a=vertices[0]; const auto& b=vertices[1]; const auto& c=vertices[3];
				sampling.origin=a; sampling.bx=b.x-a.x; sampling.by=b.y-a.y; sampling.cx=c.x-a.x; sampling.cy=c.y-a.y;
				sampling.determinant=sampling.bx*sampling.cy-sampling.by*sampling.cx;
				if (std::abs(sampling.determinant)>1e-6f && std::abs(vertices[2].x-b.x-c.x+a.x)<.001f &&
					std::abs(vertices[2].y-b.y-c.y+a.y)<.001f) sampling.coordinates=imageSampling;
			}
			if (!sampling.coordinates) InvalidateFrame(); // Clipped/perspective sampling keeps the full paint path.
		}
		if (output.draws) Submit(output.vertices,output.indices,output.material,geometry.identity,geometry.revision,geometry.submission,sampling);
	}
	// Transform, clip and resolve one geometry's submission into its cache.
	void Prepare(const Geometry& geometry, Geometry::Submission& output, Rml::Vector2f translation, float left, float top, float right, float bottom) {
		output.draws = output.inside = output.fallback = false; output.indices.clear();
		const bool clipping = output.clipping;
		std::uintptr_t texture = output.texture;
		auto& vertices = output.vertices;
		vertices.resize(geometry.vertices.size());
		float minX = OpenClip, minY = OpenClip, maxX = -OpenClip, maxY = -OpenClip;
		// Without a transform w is 1, and dividing by it is exact.
		static const auto unit = [] { std::array<float,256> values{}; for (int i = 0; i < 256; ++i) values[i] = i / 255.f; return values; }();
		for (size_t i = 0; i < geometry.vertices.size(); ++i) {
			const auto& source = geometry.vertices[i];
			float x = source.position.x + translation.x, y = source.position.y + translation.y;
			if (output.hasTransform) {
				const auto transformed = output.transform * Rml::Vector4f(x, y, 0, 1);
				if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.w) || transformed.w <= 0) return;
				x = transformed.x / transformed.w; y = transformed.y / transformed.w;
			} else if (!std::isfinite(x) || !std::isfinite(y)) return;
			vertices[i] = {x, y, source.tex_coord.x, source.tex_coord.y,
				unit[source.colour.red], unit[source.colour.green], unit[source.colour.blue], unit[source.colour.alpha]};
			minX = std::min(minX,x); maxX = std::max(maxX,x);
			minY = std::min(minY,y); maxY = std::max(maxY,y);
		}
		// Whole geometry outside the clip draws nothing; inside, it needs no cut.
		if (clipping && (maxX <= left || minX >= right || maxY <= top || minY >= bottom)) return;
		const ClipPolygon& polygonClip = output.polygon;
		const bool edges = clipping && polygonClip.count >= 3;
		bool inside = !clipping || (minX >= left && maxX <= right && minY >= top && maxY <= bottom);
		if (inside && edges) {
			for (int e = 0; e < polygonClip.count && inside; ++e) {
				const auto& a = polygonClip.points[e]; const auto& b = polygonClip.points[(e+1) % polygonClip.count];
				const float na = a.y-b.y, nb = b.x-a.x, nc = -(na*a.x + nb*a.y);
				for (const auto& vertex : vertices) if (na*vertex.x + nb*vertex.y + nc < 0) { inside = false; break; }
			}
		}
		if (texture == MultiplyTexture) {
			// Multiply vertices carry the factor each pixel's destination takes.
			// That is exact only against the caller's own target: an isolated
			// layer starts transparent, and its composite is premultiplied over.
			// There, and on hosts without the material, darken by the factor's
			// luminance so the element still reads as a shadow cast in the light.
			const auto material = output.multiply;
			if (material) texture = material;
			else {
				texture = 0; output.fallback = true;
				for (auto& vertex : vertices) {
					const float factor = .2126f*vertex.r + .7152f*vertex.g + .0722f*vertex.b;
					vertex.r = vertex.g = vertex.b = 0; vertex.a = std::clamp(1-factor,0.f,1.f);
				}
			}
		}
		output.material = texture==CoverageTexture ? 0 : texture;
		if (inside) { output.indices=geometry.indices; output.draws = output.inside = true; return; }
		std::vector<Vertex> clipped, polygon, scratch;
		auto& indices = output.indices;
		for (size_t i = 0; i < geometry.indices.size(); i += 3) {
			polygon.assign({vertices[geometry.indices[i]], vertices[geometry.indices[i+1]], vertices[geometry.indices[i+2]]});
			Clip(polygon, scratch, 0, left, true);
			Clip(polygon, scratch, 0, right, false);
			Clip(polygon, scratch, 1, top, true);
			Clip(polygon, scratch, 1, bottom, false);
			for (int e = 0; edges && e < polygonClip.count && polygon.size() >= 3; ++e) {
				const auto& a = polygonClip.points[e]; const auto& b = polygonClip.points[(e+1) % polygonClip.count];
				const float na = a.y-b.y, nb = b.x-a.x;
				ClipHalfPlane(polygon, scratch, na, nb, -(na*a.x + nb*a.y));
			}
			if (polygon.size() < 3) continue;
			const int base = static_cast<int>(clipped.size());
			clipped.insert(clipped.end(), polygon.begin(), polygon.end());
			for (int j = 2; j < static_cast<int>(polygon.size()); ++j) {
				indices.insert(indices.end(), {base, base + j - 1, base + j});
			}
		}
		vertices.swap(clipped);
		output.draws = !indices.empty();
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
		if (source == MultiplySource) { dimensions = {1,1}; return MultiplyTexture; }
		if (source == CoverageSource) { dimensions = {1,1}; return CoverageTexture; }
		const auto handle=host.LoadMaterial(source, dimensions.x, dimensions.y);
		if (handle && source.starts_with("q4-add/")) additiveImages.insert(handle);
		return handle;
	}
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i dimensions) override {
		if (!GeneratedImagePool::ValidSize(dimensions.x,dimensions.y,pixels.size()) || !pixels.data()) {
			host.Log(true,"Invalid retained generated image dimensions or byte count"); return 0;
		}
		const auto handle=host.GenerateTexture({pixels.data(),pixels.size()},dimensions.x,dimensions.y);
		if (!handle) { host.Log(true,"Retained generated image upload unavailable or budget exhausted"); return 0; }
		generatedImages.insert(handle);
		return handle;
	}
	void ReleaseTexture(Rml::TextureHandle texture) override {
		// Ordinary materials remain engine-owned. Only retire this context's
		// generated leases, and never replay a frame that names a retired image.
		if (generatedImages.erase(texture)) {
			InvalidateFrame(); ClearLayerCache();
			host.ReleaseGeneratedTexture(texture);
		}
	}
	void EnableScissorRegion(bool enable) override { scissorEnabled = enable; }
	void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }
	// Under a transform RmlUi clips overflow with the clipping box's geometry
	// instead of a scissor. Translate and scale keep that box axis-aligned, so
	// the rectangle clip in RenderGeometry applies it exactly. Rotation and
	// shear turn it into a convex quadrilateral, which RenderGeometry clips to
	// edge by edge (the stock HUD's sheared gauge fills, section 14.3). Shapes
	// a convex outline cannot represent (rounded corners, inverse masks) clip
	// to their bounds instead and are counted.
	void EnableClipMask(bool enable) override { clipMaskEnabled = enable; if (!enable) clipPolygon.count = 0; }
	// Andrew's monotone chain hull of a small point set, ordered so the region
	// lies where a*x + b*y + c >= 0 for every edge (clockwise on screen).
	static bool ConvexOutline(std::vector<Rml::Vector2f> points, ClipPolygon& outline) {
		std::sort(points.begin(),points.end(),[](const auto& a, const auto& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
		points.erase(std::unique(points.begin(),points.end(),[](const auto& a, const auto& b) {
			return std::abs(a.x-b.x) < 1e-4f && std::abs(a.y-b.y) < 1e-4f; }),points.end());
		if (points.size() < 3) return false;
		const auto cross = [](const Rml::Vector2f& o, const Rml::Vector2f& a, const Rml::Vector2f& b) {
			return double(a.x-o.x)*double(b.y-o.y) - double(a.y-o.y)*double(b.x-o.x); };
		std::vector<Rml::Vector2f> hull(points.size()*2);
		size_t k = 0;
		for (size_t i = 0; i < points.size(); ++i) {
			while (k >= 2 && cross(hull[k-2],hull[k-1],points[i]) <= 0) --k;
			hull[k++] = points[i];
		}
		for (size_t i = points.size()-1, lower = k+1; i-- > 0;) {
			while (k >= lower && cross(hull[k-2],hull[k-1],points[i]) <= 0) --k;
			hull[k++] = points[i];
		}
		hull.resize(k-1);
		if (hull.size() < 3 || hull.size() > ClipPolygon::Capacity) return false;
		outline.count = static_cast<int>(hull.size());
		std::copy(hull.begin(),hull.end(),outline.points.begin());
		return true;
	}
	// Intersect two convex outlines (Sutherland-Hodgman keeps convexity).
	static void Intersect(ClipPolygon& subject, const ClipPolygon& clip) {
		std::vector<Vertex> polygon, scratch;
		for (int i = 0; i < subject.count; ++i) polygon.push_back({subject.points[i].x,subject.points[i].y,0,0,0,0,0,0});
		for (int e = 0; e < clip.count && polygon.size() >= 3; ++e) {
			const auto& a = clip.points[e]; const auto& b = clip.points[(e+1) % clip.count];
			const float na = a.y-b.y, nb = b.x-a.x;
			ClipHalfPlane(polygon,scratch,na,nb,-(na*a.x + nb*a.y));
		}
		std::vector<Rml::Vector2f> points;
		for (const auto& vertex : polygon) points.push_back({vertex.x,vertex.y});
		if (!ConvexOutline(points,subject)) subject.count = 0;
	}
	void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle handle, Rml::Vector2f translation) override {
		if (!handle) return;
		if (operation == Rml::ClipMaskOperation::SetInverse) {
			clipMask = {-OpenClip,-OpenClip,OpenClip,OpenClip}; clipPolygon.count = 0; ++statistics.clipMaskFallbacks; return;
		}
		const Geometry& geometry = *reinterpret_cast<const Geometry*>(handle);
		ClipRectangle bounds{OpenClip,OpenClip,-OpenClip,-OpenClip};
		std::vector<Rml::Vector2f> points;
		points.reserve(geometry.vertices.size());
		bool exact = true, convex = false;
		ClipPolygon outline;
		for (const auto& source : geometry.vertices) {
			const Rml::Vector4f p(source.position.x + translation.x, source.position.y + translation.y, 0, 1);
			const auto transformed = hasTransform ? transform * p : p;
			if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.w) || transformed.w <= 0) {
				exact = false; bounds = {}; points.clear(); break;
			}
			const Rml::Vector2f point(transformed.x / transformed.w, transformed.y / transformed.w);
			bounds = {std::min(bounds.left,point.x),std::min(bounds.top,point.y),std::max(bounds.right,point.x),std::max(bounds.bottom,point.y)};
			points.push_back(point);
		}
		if (points.empty()) bounds = {};
		else {
			// The triangles cover their bounds exactly only for an axis-aligned rectangle.
			double area = 0;
			for (size_t i = 0; i + 2 < geometry.indices.size(); i += 3) {
				const auto& a = points[geometry.indices[i]]; const auto& b = points[geometry.indices[i+1]]; const auto& c = points[geometry.indices[i+2]];
				area += std::abs(double(b.x-a.x)*double(c.y-a.y) - double(c.x-a.x)*double(b.y-a.y)) / 2;
			}
			const double box = double(bounds.right-bounds.left)*double(bounds.bottom-bounds.top);
			exact = std::abs(area-box) <= std::max(.5, box*1e-4);
			if (!exact && ConvexOutline(points,outline)) {
				// The triangles must cover their convex outline exactly.
				double hull = 0;
				for (int i = 0; i < outline.count; ++i) {
					const auto& a = outline.points[i]; const auto& b = outline.points[(i+1) % outline.count];
					hull += double(a.x)*double(b.y) - double(b.x)*double(a.y);
				}
				convex = std::abs(std::abs(hull)/2-area) <= std::max(.5, area*1e-4);
			}
		}
		++(exact || convex ? statistics.clipMasks : statistics.clipMaskFallbacks);
		// The rectangle bounds every clip; a convex outline also cuts by edge.
		const bool intersect = operation != Rml::ClipMaskOperation::Set;
		const ClipPolygon previous = clipPolygon;
		const ClipRectangle previousBounds = clipMask;
		clipMask = !intersect ? bounds : ClipRectangle{std::max(clipMask.left,bounds.left),
			std::max(clipMask.top,bounds.top),std::min(clipMask.right,bounds.right),std::min(clipMask.bottom,bounds.bottom)};
		if (convex) {
			if (intersect && previous.count >= 3) { clipPolygon = previous; Intersect(clipPolygon,outline); }
			else if (intersect) {
				// Against an axis-aligned rectangle clip, start from that rectangle.
				// Open bounds stay finite here, so edge equations cannot overflow.
				const float l = std::clamp(previousBounds.left,-1e6f,1e6f), tp = std::clamp(previousBounds.top,-1e6f,1e6f);
				const float r = std::clamp(previousBounds.right,-1e6f,1e6f), bt = std::clamp(previousBounds.bottom,-1e6f,1e6f);
				ClipPolygon box;
				box.count = 4;
				box.points = {Rml::Vector2f{l,tp},{r,tp},{r,bt},{l,bt}};
				Intersect(outline,box);
				clipPolygon = outline;
			}
			else clipPolygon = outline;
			if (clipPolygon.count < 3) clipMask = {};
		} else if (!intersect) clipPolygon.count = 0;
		// An axis-aligned Intersect keeps the earlier outline; the bounds cut it.
	}
	void SetTransform(const Rml::Matrix4f* value) override {
		// RmlUi restores an explicit identity between output-space vector draws
		// and can elide the next null transform. Both mean exactly the same
		// geometry; alternating their cache keys needlessly retires atlas tiles.
		hasTransform = value && *value != Rml::Matrix4f::Identity();
		if (value) transform = *value;
	}
	// A pushed layer stays pending, recording its draws, until something needs
	// its target: a nested layer, a mask snapshot, a backdrop or a visible
	// composite. Composited at zero opacity, or clipped away, it adds nothing,
	// so its draws are dropped without a target, a clear or a composite.
	// Handles are logical, nonzero even after an allocation failure, so RmlUi
	// can unwind its stack while this renderer suppresses the failed frame.
	Rml::LayerHandle PushLayer() override {
		if (++nextHandle == 0) nextHandle = 1;
		stack.push_back({nextHandle,0,true,{}});
		statistics.peakLayerDepth = std::max<std::uint64_t>(statistics.peakLayerDepth,stack.size());
		return stack.back().handle;
	}
	void PopLayer() override {
		if (stack.empty()) { host.Log(true,"Retained composition layer underflow"); failed = true; return; }
		// A layer that never got a target never changed the host's binding.
		const bool bound = !stack.back().pending;
		if (!stack.back().cached) ReleaseSlot(stack.back().slot);
		stack.pop_back();
		// Restore the base even after an allocation failure. No further draws
		// are accepted in a failed frame, preventing paint on the wrong target.
		if ((bound || failed) && (!failed || stack.empty())) host.EndLayer(TopLayer());
	}
	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override {
		const auto value = parameters.find("value");
		if ((name == "opacity" || name == "q4-opacity") && value != parameters.end()) {
			const float opacity = value->second.Get<float>();
			if (std::isfinite(opacity)) {
				auto* filter=new Filter{std::clamp(opacity,0.f,1.f)};
				const auto owner=parameters.find("element");
				if (name=="q4-opacity" && owner!=parameters.end()) {
					auto* element=static_cast<Rml::Element*>(owner->second.Get<void*>());
					if (element && (element->GetTagName()=="q4-node" || element->GetTagName()=="q4-vector")) {
						filter->opacitySource=static_cast<VectorElement*>(element)->ReplayOpacity();
						filter->textOpacity=static_cast<VectorElement*>(element)->ReplayOpacityIsText();
					}
				}
				return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
			}
		}
		// The speed streak (backdrop-filter: streak()): drawn by the host.
		if (name == "streak" && value != parameters.end()) {
			const float strength = value->second.Get<float>();
			if (std::isfinite(strength)) {
				auto* filter = new Filter; filter->backdrop = true; filter->streak = std::clamp(strength,0.f,1.f);
				return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
			}
		}
		// Soft focus (backdrop-filter: blur() saturate()): drawn by the host.
		const auto sigma = parameters.find("sigma");
		if ((name == "blur" && sigma != parameters.end()) || (name == "saturate" && value != parameters.end())) {
			const float amount = (name == "blur" ? sigma : value)->second.Get<float>();
			if (std::isfinite(amount)) {
				auto* filter = new Filter; filter->backdrop = true;
				if (name == "blur") filter->blur = std::clamp(amount,0.f,256.f); else filter->saturation = std::clamp(amount,0.f,1.f);
				return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
			}
		}
		host.Log(true,"Unsupported retained filter: "+name); return 0;
	}
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
		if (failed || stack.empty()) return 0;
		if (stack.back().pending) {
			// Freeze the drawing commands, not a full-viewport GPU copy. Their
			// shared submissions survive geometry edits/releases and layer reuse.
			auto* filter=new Filter{1,0,viewportWidth,viewportHeight,viewportGeneration};
			filter->recording=std::make_shared<const std::vector<RecordedDraw>>(stack.back().draws);
			++statistics.maskSnapshots;
			return reinterpret_cast<Rml::CompiledFilterHandle>(filter);
		}
		Materialize(stack.back());
		if (failed) return 0;
		const auto snapshot = AllocateSlot();
		if (!snapshot) return 0;
		// A copy owns its slot until ReleaseFilter: popping/reusing the source
		// layer, or drawing to it again, cannot change an already saved mask.
		host.CompositeLayer(TopLayer(),snapshot,1,ClipBounds());
		host.EndLayer(TopLayer());
		++statistics.maskSnapshots;
		return reinterpret_cast<Rml::CompiledFilterHandle>(new Filter{1,snapshot,viewportWidth,viewportHeight,viewportGeneration});
	}
	void ReleaseFilter(Rml::CompiledFilterHandle handle) override {
		const auto* filter = reinterpret_cast<const Filter*>(handle);
		if (!filter) return;
		ReleaseSlot(filter->mask); delete filter;
	}
	void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters) override {
		if (failed) return;
		// Soft focus. RmlUi reads the surface beneath an element through its
		// backdrop filters into a pushed layer; only the base surface is read.
		// A host that cannot leaves the layer transparent: nothing changes, and
		// the document's scrim fallback stands in.
		float blur = 0, saturation = 1, streak = 0; bool backdrop = false;
		for (auto handle : filters) {
			const auto* filter = reinterpret_cast<const Filter*>(handle);
			if (filter && filter->backdrop) {
				backdrop = true; blur = std::max(blur,filter->blur); saturation = std::min(saturation,filter->saturation);
				streak = std::max(streak,filter->streak);
			}
		}
		if (backdrop) {
			// A streak takes the backdrop; soft focus applies otherwise.
			const Bounds region = ClipBounds();
			if (auto* target = Find(destination)) {
				// An otherwise empty backdrop layer is one replayable operation.
				// Its pixels are captured afresh each frame, never frozen. Any
				// additional artwork or nested composition invalidates recording.
				target->backdrop=recordFrame && !source && mode==Rml::BlendMode::Blend && target->pending && target->draws.empty();
				target->blur=blur; target->saturation=saturation; target->streak=streak;
				target->backdropRegion=region;
				Materialize(*target,target->backdrop);
			}
			if (!failed && !source && destination && ActiveLayer(destination) && mode == Rml::BlendMode::Blend && region.width > 0 && region.height > 0 &&
				(streak > 0 ? host.StreakBackdrop(Slot(destination),streak,region) : host.SoftenBackdrop(Slot(destination),blur,saturation,region)))
				++statistics.backdropComposites;
			else ++statistics.backdropFallbacks;
			return;
		}
		if (!source || source == destination || !ActiveLayer(source) || !ActiveLayer(destination) || mode != Rml::BlendMode::Blend) {
			host.Log(true,"Unsupported retained layer composition"); failed = true; return;
		}
		if (recordFrame && destination) InvalidateFrame();
		float opacity = 1;
		std::shared_ptr<const float> opacitySource;
		bool textOpacity=false;
		std::vector<Filter*> maskFilters;
		for (auto handle : filters) {
			auto& filter = *reinterpret_cast<Filter*>(handle);
			opacity *= filter.opacitySource ? *filter.opacitySource : filter.opacity;
			if (filters.size()==1) { opacitySource=filter.opacitySource; textOpacity=filter.textOpacity; }
			if (filter.mask || filter.recording) {
				if (filter.generation != viewportGeneration) {
					host.Log(true,"Retained mask snapshot belongs to a different viewport"); failed = true; return;
				}
				maskFilters.push_back(&filter);
			}
		}
		Bounds clip = ClipBounds();
		auto& layer = *Find(source);
		// Transparent pixels outside a pending layer's artwork cannot affect the
		// destination. Small HUD panels must not blend a viewport-sized quad.
		if (layer.pending) clip=ArtworkClip(layer.draws,clip);
		// Over the premultiplied destination a zero opacity composite is exact
		// identity, so a layer still pending need never be drawn at all. Its
		// recording stays until the pop, should it be composited again.
		if (layer.pending && (opacity == 0 || clip.width <= 0 || clip.height <= 0)) { ++statistics.layerElisions; return; }
		// A ready coverage atlas already contains the exact pixels of one
		// non-overlapping solid mesh. Its host can apply group alpha directly,
		// avoiding a clear and two target transitions. Replay checks continued
		// atlas residency before any destination drawing begins.
		if (layer.pending && !destination && maskFilters.empty() && layer.draws.size()==1 &&
			!layer.draws[0].texture && layer.draws[0].mesh) {
			const auto& draw=layer.draws[0];
			host.EndLayer(0);
			const auto started=statistics.Now();
			const bool painted=host.DrawMeshOpacity(draw.mesh,draw.revision,opacity);
			statistics.hostDrawMilliseconds+=std::chrono::duration<double,std::milli>(statistics.Now()-started).count();
			if (painted) {
				if (recordFrame) {
					FrameCommand command; command.directOpacity=true; command.draw=draw; command.opacity=opacity;
					command.draw.opacitySource=opacitySource;
					frameCommands.push_back(std::move(command));
				}
				++statistics.drawCalls; statistics.submittedVertices+=draw.Vertices().size(); statistics.submittedIndices+=draw.Indices().size();
				++statistics.layerElisions; ++statistics.layerComposites; return;
			}
		}
		// An isolated image or a run of non-overlapping glyph quads needs no
		// intermediate target: opacity distributes over those disjoint pixels.
		// Overlapping glyphs, arbitrary meshes and masked groups keep composition.
		if (layer.pending && !destination && maskFilters.empty() && layer.draws.size()==1 &&
			layer.draws[0].texture && DisjointQuads(layer.draws[0])) {
			const auto& draw=layer.draws[0]; auto vertices=draw.Vertices();
			// A leaf's source belongs to its own single opacity filter. Never
			// attach it to a parent composite or a compound filter operation.
			for (auto& v:vertices) { v.r*=opacity; v.g*=opacity; v.b*=opacity; v.a*=opacity; }
			host.EndLayer(0);
			DrawNow(vertices,draw.Indices(),draw.texture,0,0,{},draw.sampling);
			if (recordFrame && opacitySource) {
				auto& command=frameCommands.back(); command.draw.opacitySource=opacitySource;
				for (const auto& v:draw.Vertices()) command.baseColours.push_back({v.r,v.g,v.b,v.a});
			}
			++statistics.layerElisions; ++statistics.layerComposites; return;
		}
		if (layer.pending && maskFilters.size()==1 && maskFilters[0]->recording &&
			DrawMaskedImages(layer.draws,*maskFilters[0]->recording,destination,opacity,clip)) {
			++statistics.maskApplications; ++statistics.layerComposites; ++statistics.directMaskDraws; return;
		}
		// Leaf vector panels have no time-varying texture content. Their final
		// masked pixels remain valid while every mesh revision and clip match.
		// Keep the authored vectors/editability, avoiding repeated GPU clears,
		// mask copies and composites for a small stationary HUD gauge.
		std::vector<std::pair<std::uint64_t,std::uint64_t>> cacheKey;
		const bool cacheText=opacitySource && textOpacity && maskFilters.empty();
		// A compound plate needs isolated alpha at the overlap. Large single
		// gradient meshes (the nailgun acquisition glow) also benefit: the solid
		// coverage atlas cannot represent their varying colour, and an opacity
		// change must not resubmit tens of thousands of unchanged vertices.
		// Small single meshes still prefer direct drawing/the coverage atlas.
		const bool cachePlate=maskFilters.empty() && (layer.draws.size()>1 ||
			(layer.draws.size()==1 && layer.draws.front().Vertices().size()>=1024));
		bool cacheable=host.CanCacheLayers() && layer.pending && !destination && (!maskFilters.empty() || cacheText || cachePlate) && LayerCacheLimit()>0;
		const auto appendKey=[&](const std::vector<RecordedDraw>& draws) {
			cacheKey.emplace_back(0,draws.size());
			for (const auto& draw:draws) {
				if (!draw.mesh || (draw.texture && !cacheText) || draw.sampling.coordinates) return false;
				// Fonts can queue their first GPU upload during the first draw.
				// Admit only a submission already painted in an earlier frame.
				if (!draw.retained || host.RenderFrame()<=draw.retained->firstFrame) return false;
				cacheKey.emplace_back(draw.mesh,draw.revision);
			}
			return true;
		};
		if (cacheable) cacheable=appendKey(layer.draws);
		for (const auto* filter:maskFilters) if (cacheable)
			cacheable=filter->recording && appendKey(*filter->recording);
		if (cacheable) for (auto& cached:layerCache) if (!cached.sourceOnly && cached.key==cacheKey &&
			cached.clip.x==clip.x && cached.clip.y==clip.y && cached.clip.width==clip.width && cached.clip.height==clip.height) {
			cached.used=++cacheClock;
			pool->slots[cached.slot].frame=host.RenderFrame();
			host.CompositeLayer(cached.slot,0,opacity,clip);
			if (recordFrame) {
				FrameCommand command; command.slot=cached.slot; command.opacity=opacity; command.clip=clip;
				command.draw.opacitySource=opacitySource;
				frameCommands.push_back(std::move(command));
			}
			++statistics.layerCacheHits; ++statistics.layerComposites;
			statistics.maskApplications+=maskFilters.size();
			return;
		}
		if (!destination && layer.pending && maskFilters.size()==1 &&
			CompositeCachedSource(layer,*maskFilters[0],opacity,clip)) return;
		// Do not churn a full cache with another plate or a changing mask.
		// Entries used this frame or the last one remain in the working set.
		if (cacheable && layerCache.size()>=LayerCacheLimit()) {
			const auto frame=host.RenderFrame();
			cacheable=std::any_of(layerCache.begin(),layerCache.end(),[&](const CachedLayer& cached) {
				const auto used=pool->slots[cached.slot].frame;
				return frame>used && frame-used>1;
			});
		}
		if (recordFrame && layer.backdrop && !destination && maskFilters.empty()) {
			FrameCommand command; command.backdrop=true; command.opacity=opacity; command.clip=clip;
			command.blur=layer.blur; command.saturation=layer.saturation; command.streak=layer.streak;
			command.backdropRegion=layer.backdropRegion;
			frameCommands.push_back(std::move(command));
		} else if (recordFrame && !layer.pending) InvalidateFrame();
		Materialize(layer);
		if (auto* target = Find(destination)) Materialize(*target);
		if (failed) return;
		if (clip.width > 0 && clip.height > 0) {
			std::uint32_t scratch = 0;
			if (!maskFilters.empty()) {
				scratch = AllocateSlot();
				if (!scratch) return;
				// Filters cannot mutate the source: it may be composited again.
				host.CompositeLayer(layer.slot,scratch,1,clip);
				for (auto* filter : maskFilters) {
					if (!filter->mask) {
						filter->mask=AllocateSlot();
						if (!filter->mask) { ReleaseSlot(scratch); return; }
						for (const auto& draw:*filter->recording) DrawRecorded(draw);
					}
					host.MaskLayer(filter->mask,scratch,clip); ++statistics.maskApplications;
				}
			}
			if (cacheable && maskFilters.empty()) {
				// Keep the completed glyph overlap exactly once. Images cannot use
				// this cache: their material stages may animate independently.
				// Transfer this leaf's target lease to the cache when its layer
				// pops, without another full-size target or intermediate copy.
				scratch=layer.slot;
				layer.cached=true;
			}
			host.CompositeLayer(scratch ? scratch : layer.slot,Slot(destination),opacity,clip);
			if (cacheable && scratch) {
				while (layerCache.size()>=LayerCacheLimit()) EvictLayerCache();
				layerCache.push_back({std::move(cacheKey),clip,scratch,++cacheClock,false});
				pool->slots[scratch].cached=true;
			} else ReleaseSlot(scratch);
			++statistics.layerComposites;
		}
	}
private:
	struct FrameCommand { RecordedDraw draw; std::vector<Rml::Vector2f> imageFractions; std::vector<std::array<float,4>> baseColours;
		std::uint32_t slot=0; float opacity=1; Bounds clip;
		bool backdrop=false, directOpacity=false; float blur=0, saturation=1, streak=0; Bounds backdropRegion; };
	std::shared_ptr<const ImageCoordinates> imageSampling;
	std::vector<FrameCommand> frameCommands;
	bool frameValid=false, recordFrame=false;
	size_t frameRecordedVertices=0;
	std::uint64_t frameVectorBytes=0;
	std::set<std::uintptr_t> additiveImages;
	std::set<std::uintptr_t> generatedImages;
	static bool DisjointQuads(const RecordedDraw& draw) {
		const auto& vertices=draw.Vertices(); const auto& indices=draw.Indices();
		if (vertices.empty() || vertices.size()>1024 || vertices.size()%4 || indices.size()!=vertices.size()/4*6) return false;
		std::vector<Bounds> boxes; boxes.reserve(vertices.size()/4);
		for (size_t base=0;base<vertices.size();base+=4) {
			const int a[6]={0,1,2,0,2,3}, b[6]={0,3,1,1,3,2}; bool first=true,second=true;
			for (int i=0;i<6;++i) { first &= indices[base/4*6+i]==int(base)+a[i]; second &= indices[base/4*6+i]==int(base)+b[i]; }
			if (!first && !second) return false;
			float left=vertices[base].x,right=left,top=vertices[base].y,bottom=top;
			for (int i=0;i<4;++i) {
				const auto& v=vertices[base+i]; const auto& next=vertices[base+(i+1)%4];
				if ((v.x==next.x)==(v.y==next.y)) return false;
				left=std::min(left,v.x); right=std::max(right,v.x); top=std::min(top,v.y); bottom=std::max(bottom,v.y);
			}
			if (!(right>left && bottom>top)) return false;
			for (const auto& box:boxes) if (left<box.x+box.width && right>box.x && top<box.y+box.height && bottom>box.y) return false;
			boxes.push_back({left,top,right-left,bottom-top});
		}
		return true;
	}
	static Bounds ArtworkClip(const std::vector<RecordedDraw>& draws, const Bounds& clip) {
		float left=clip.x+clip.width, top=clip.y+clip.height, right=clip.x, bottom=clip.y;
		for (const auto& draw:draws) {
			if (draw.linearMask) {
				for (const auto& point:draw.linearMask->points) {
					left=std::min(left,float(point.x)); top=std::min(top,float(point.y));
					right=std::max(right,float(point.x)); bottom=std::max(bottom,float(point.y));
				}
			} else if (draw.retained) {
				const auto& b=draw.retained->bounds;
				left=std::min(left,b.x); top=std::min(top,b.y); right=std::max(right,b.x+b.width); bottom=std::max(bottom,b.y+b.height);
			} else for (const auto& v:draw.Vertices()) {
				left=std::min(left,v.x); top=std::min(top,v.y); right=std::max(right,v.x); bottom=std::max(bottom,v.y);
			}
		}
		// Cover the complete pixel footprint, including bilinear target sampling.
		left=std::max(clip.x,std::floor(left)-1); top=std::max(clip.y,std::floor(top)-1);
		right=std::min(clip.x+clip.width,std::ceil(right)+1); bottom=std::min(clip.y+clip.height,std::ceil(bottom)+1);
		return {left,top,std::max(0.f,right-left),std::max(0.f,bottom-top)};
	}
	bool DrawMaskedImages(const std::vector<RecordedDraw>& images, const std::vector<RecordedDraw>& masks,
		Rml::LayerHandle destination, float opacity, const Bounds& clip) {
		// Addition distributes over a coverage mask. Texture the mask's existing
		// disjoint cells directly, preserving holes and analytic edge coverage.
		// Ordinary alpha images, intersecting mask paints and partial image/mask
		// intersections continue through the general composition path.
		if (images.empty() || masks.size()!=1 || !masks[0].retained || masks[0].retained->texture!=CoverageTexture) return false;
		const auto& mask=masks[0];
		std::vector<RecordedDraw> output;
		for (const auto& image:images) {
			const auto& quad=image.Vertices();
			if (!additiveImages.count(image.texture) || quad.size()!=4 || image.Indices()!=std::vector<int>{0,1,2,0,2,3}) return false;
			const auto& a=quad[0]; const auto& b=quad[1]; const auto& c=quad[3];
			const float bx=b.x-a.x, by=b.y-a.y, cx=c.x-a.x, cy=c.y-a.y, determinant=bx*cy-by*cx;
			if (std::abs(determinant)<1e-6f || std::abs(quad[2].x-b.x-c.x+a.x)>.001f ||
				std::abs(quad[2].y-b.y-c.y+a.y)>.001f) return false;
			for (const auto& v:quad) if (v.r!=a.r || v.g!=a.g || v.b!=a.b || v.a!=a.a) return false;
			RecordedDraw result; result.texture=image.texture; result.sampling=image.sampling;
			result.indices=mask.Indices(); result.vertices.reserve(mask.Vertices().size());
			for (const auto& v:mask.Vertices()) {
				const float x=v.x-a.x, y=v.y-a.y, s=(x*cy-y*cx)/determinant, t=(bx*y-by*x)/determinant;
				if (s<-.00001f || s>1.00001f || t<-.00001f || t>1.00001f ||
					v.x<clip.x || v.x>clip.x+clip.width || v.y<clip.y || v.y>clip.y+clip.height) return false;
				const float coverage=v.a*opacity;
				result.vertices.push_back({v.x,v.y,a.u+s*(b.u-a.u)+t*(c.u-a.u),a.v+s*(b.v-a.v)+t*(c.v-a.v),
					a.r*coverage,a.g*coverage,a.b*coverage,a.a*coverage});
			}
			output.push_back(std::move(result));
		}
		if (auto* target=Find(destination)) Materialize(*target);
		if (!failed) {
			host.EndLayer(Slot(destination));
			for (const auto& draw:output) DrawNow(draw.vertices,draw.indices,draw.texture,0,0,{},draw.sampling);
		}
		return true;
	}
	struct CachedLayer {
		std::vector<std::pair<std::uint64_t,std::uint64_t>> key;
		Bounds clip;
		std::uint32_t slot=0;
		std::uint64_t used=0;
		bool sourceOnly=false;
	};
	std::vector<CachedLayer> layerCache;
	std::uint64_t cacheClock=0;
	size_t LayerCacheLimit() const {
		const auto bytes=std::uint64_t(std::max(1,viewportWidth))*std::max(1,viewportHeight)*4;
		// All contexts share the host's target storage. Reserve transient space
		// after accounting for other views' cached pixels, including other DPIs.
		std::uint64_t budget=256*1024*1024;
		if (pool) for (const auto& slot:pool->slots) if (slot.owner!=this && slot.cached) {
			const auto used=std::uint64_t(slot.width)*slot.height*4;
			if (used>=budget) return 0;
			budget-=used;
		}
		const auto targets=budget/bytes;
		return targets>6 ? size_t(std::min<std::uint64_t>(8,targets-6)) : 0;
	}
	void EvictLayerCache() {
		if (layerCache.empty()) return;
		const auto oldest=std::min_element(layerCache.begin(),layerCache.end(),[](const auto& a,const auto& b){return a.used<b.used;});
		ReleaseSlot(oldest->slot); layerCache.erase(oldest);
	}
	void ClearLayerCache() { for (const auto& cached:layerCache) ReleaseSlot(cached.slot); layerCache.clear(); }
	struct Layer {
		Rml::LayerHandle handle = 0;
		std::uint32_t slot = 0; // Host target once materialized; zero after allocation failure.
		bool pending = true;
		bool cached = false;
		std::vector<RecordedDraw> draws; // Recorded while pending, replayed into the target.
		bool backdrop=false;
		float blur=0, saturation=1, streak=0;
		Bounds backdropRegion;
	};
	Layer* Find(Rml::LayerHandle handle) {
		for (auto& layer : stack) if (layer.handle == handle) return &layer;
		return nullptr;
	}
	std::uint32_t Slot(Rml::LayerHandle handle) {
		const auto* layer = handle ? Find(handle) : nullptr;
		return layer ? layer->slot : 0;
	}
	// A nested layer can remain pending until its first non-recordable operation.
	void Materialize(Layer& layer,bool preserveFrame=false) {
		if (!layer.pending) return;
		layer.pending = false;
		layer.slot = AllocateSlot(preserveFrame); // Binds and clears the target; fails the frame when exhausted.
		if (!layer.slot) { layer.draws.clear(); return; }
		++statistics.layerPushes;
		auto draws = std::move(layer.draws); layer.draws.clear();
		// Materializing a parent while a child is on the stack must draw into
		// that parent's bound target, never re-record into the child.
		for (const auto& draw : draws) DrawRecorded(draw);
	}
	void DrawRecorded(const RecordedDraw& draw) {
		if (draw.linearMask) {
			host.DrawLinearMask(0,*draw.linearMask,draw.linearClip,1);
			++statistics.drawCalls; statistics.submittedVertices+=4; statistics.submittedIndices+=6;
		} else DrawNow(draw.Vertices(),draw.Indices(),draw.texture,draw.mesh,draw.revision);
	}
	static bool ConstantPixelMask(const RecordedDraw& draw) {
		const auto& vertices=draw.Vertices(); const auto& indices=draw.Indices();
		if (vertices.empty() || vertices.size()%4 || indices.size()!=vertices.size()/4*6) return false;
		for (size_t base=0;base<vertices.size();base+=4) {
			const auto& a=vertices[base]; const auto& b=vertices[base+1];
			const auto& c=vertices[base+2]; const auto& d=vertices[base+3];
			const int order[6]={0,1,2,0,2,3};
			for (int i=0;i<6;++i) if (indices[base/4*6+i]!=int(base)+order[i]) return false;
			if (a.x!=std::floor(a.x) || a.y!=std::floor(a.y) || c.x!=std::floor(c.x) || c.y!=std::floor(c.y) ||
				a.x>=c.x || a.y>=c.y || b.x!=c.x || b.y!=a.y || d.x!=a.x || d.y!=c.y ||
				a.a!=b.a || a.a!=c.a || a.a!=d.a) return false;
		}
		return true;
	}
	bool CompositeCachedSource(Layer& layer,const Filter& filter,float opacity,const Bounds& clip) {
		if (!host.CanCacheLayers() || !host.CanCompositeMaskedLayer() || !filter.recording ||
			filter.recording->size()!=1 || !LayerCacheLimit()) return false;
		const auto& mask=filter.recording->front();
		if (!mask.linearMask && (!mask.retained || mask.retained->texture!=CoverageTexture)) return false;
		// A single-sample mask target resolves gradient subdivisions before it
		// modulates the source. Drawing those triangles straight into an MSAA
		// window instead resolves at different sample locations, visibly changing
		// sharp stops. Only whole-pixel constant cells distribute exactly; the
		// analytic shader evaluates the other supported masks per physical pixel.
		if (!mask.linearMask && !ConstantPixelMask(mask)) return false;
		// Stationary masks use the completed-layer cache above. This path keeps
		// only the stable artwork beneath a mask that is changing this frame.
		if (!mask.linearMask && host.RenderFrame()>mask.retained->firstFrame) return false;
		std::vector<std::pair<std::uint64_t,std::uint64_t>> key;
		for (const auto& draw:layer.draws) {
			if (!draw.mesh || draw.texture || draw.sampling.coordinates || !draw.retained ||
				host.RenderFrame()<=draw.retained->firstFrame) return false;
			key.emplace_back(draw.mesh,draw.revision);
		}
		if (key.empty()) return false;
		auto found=std::find_if(layerCache.begin(),layerCache.end(),[&](const auto& cached) {
			return cached.sourceOnly && cached.key==key && cached.clip.x==clip.x && cached.clip.y==clip.y &&
				cached.clip.width==clip.width && cached.clip.height==clip.height;
		});
		if (found==layerCache.end()) {
			if (layerCache.size()>=LayerCacheLimit()) {
				const auto frame=host.RenderFrame();
				if (std::none_of(layerCache.begin(),layerCache.end(),[&](const auto& cached) {
					const auto used=pool->slots[cached.slot].frame; return frame>used && frame-used>1;
				})) return false;
			}
			Materialize(layer);
			if (failed) return true;
			while (layerCache.size()>=LayerCacheLimit()) EvictLayerCache();
			layer.cached=true; pool->slots[layer.slot].cached=true;
			layerCache.push_back({std::move(key),clip,layer.slot,++cacheClock,true});
			found=std::prev(layerCache.end());
		} else ++statistics.layerCacheHits;
		found->used=++cacheClock; pool->slots[found->slot].frame=host.RenderFrame();
		const auto started=statistics.Now();
		if (mask.linearMask) {
			const auto& b=mask.linearClip;
			const float left=std::max(clip.x,b.x),top=std::max(clip.y,b.y);
			const float right=std::min(clip.x+clip.width,b.x+b.width),bottom=std::min(clip.y+clip.height,b.y+b.height);
			const Bounds maskClip{left,top,std::max(0.f,right-left),std::max(0.f,bottom-top)};
			host.DrawLinearMask(found->slot,*mask.linearMask,maskClip,opacity);
			if (recordFrame && mask.liveLinearMask) {
				FrameCommand command; command.slot=found->slot; command.draw=mask;
				command.opacity=opacity; command.clip=maskClip;
				frameCommands.push_back(std::move(command));
			} else InvalidateFrame();
		} else { InvalidateFrame(); host.CompositeMaskedLayer(found->slot,mask.Vertices(),mask.Indices(),opacity); }
		statistics.hostDrawMilliseconds+=std::chrono::duration<double,std::milli>(statistics.Now()-started).count();
		++statistics.drawCalls; statistics.submittedVertices+=mask.linearMask ? 4 : mask.Vertices().size();
		statistics.submittedIndices+=mask.linearMask ? 6 : mask.Indices().size();
		++statistics.maskApplications; ++statistics.layerComposites; ++statistics.directMaskDraws;
		return true;
	}
	std::uint32_t TopLayer() const { return stack.empty() ? 0 : stack.back().slot; }
	bool ActiveLayer(Rml::LayerHandle handle) {
		return !handle || Find(handle);
	}
	std::uint32_t AllocateSlot(bool preserveFrame=false) {
		if (!preserveFrame) InvalidateFrame(); // Arbitrary transient composition requires traversal.
		if (failed) return 0;
		if (!pool) return 0;
		const auto frame = host.RenderFrame();
		// Reuse the same dimensions first, then unused slots, then older-frame
		// targets. Resizing a target already referenced by this submission frame
		// would also resize the image seen by earlier deferred draw commands.
		for (int pass = 0; pass < 3; ++pass) {
			for (std::uint32_t id = 1; id < pool->slots.size(); ++id) {
				auto& slot = pool->slots[id];
				if (slot.owner) continue;
				const bool matches = slot.width == viewportWidth && slot.height == viewportHeight;
				const bool unused = slot.width == 0;
				if ((pass == 0 && !matches) || (pass == 1 && !unused) ||
					(pass == 2 && (matches || unused || slot.frame == frame))) continue;
				if (host.BeginLayer(id,viewportWidth,viewportHeight)) {
					slot = {this,viewportWidth,viewportHeight,frame};
					statistics.peakLayerTargets = std::max<std::uint64_t>(statistics.peakLayerTargets,
						std::count_if(pool->slots.begin(),pool->slots.end(),[this](const auto& value) { return value.owner == this; }));
					return id;
				}
			}
		}
		if (!layerCache.empty()) { EvictLayerCache(); return AllocateSlot(preserveFrame); }
		// A hidden/idle view must not pin cache targets needed by an active
		// compositor. Same-size reuse remains ordered by the host draw stream;
		// the frame guard above still prevents resizing a referenced image.
		for (const auto& slot:pool->slots) if (slot.cached && slot.owner && slot.owner!=this) {
			slot.owner->EvictLayerCache(); return AllocateSlot(preserveFrame);
		}
		// After a resolution increase, several free old targets can consume the
		// budget needed for one larger replacement. Releasing one lease is not
		// enough: the host must also retire the obsolete allocation. Never touch
		// a target used by any context in the current deferred submission frame.
		bool trimmed=false;
		for (std::uint32_t id=1; id<pool->slots.size(); ++id) {
			auto& slot=pool->slots[id];
			if (!slot.owner && slot.width>1 && slot.frame!=frame && host.TrimLayer(id)) {
				slot={nullptr,1,1,frame}; trimmed=true;
			}
		}
		if (trimmed) return AllocateSlot(preserveFrame);
		host.Log(true,"Cannot allocate retained composition layer"); failed = true; return 0;
	}
	void ReleaseSlot(std::uint32_t id) {
		if (pool && id && id < pool->slots.size() && pool->slots[id].owner == this) {
			if (pool->slots[id].cached) InvalidateFrame();
			pool->slots[id].owner = nullptr; pool->slots[id].cached=false;
		}
	}
	// The scissor and clip mask intersected; false when neither applies.
	bool ActiveClip(float& left, float& top, float& right, float& bottom) const {
		if (!scissorEnabled && !clipMaskEnabled) return false;
		left = top = -OpenClip; right = bottom = OpenClip;
		if (scissorEnabled) {
			left = float(scissor.Left()); top = float(scissor.Top()); right = float(scissor.Right()); bottom = float(scissor.Bottom());
		}
		if (clipMaskEnabled) {
			left = std::max(left,clipMask.left); top = std::max(top,clipMask.top);
			right = std::min(right,clipMask.right); bottom = std::min(bottom,clipMask.bottom);
		}
		return true;
	}
	Bounds ClipBounds() const {
		float left = 0, top = 0, right = float(viewportWidth), bottom = float(viewportHeight);
		if (ActiveClip(left,top,right,bottom)) {
			left = std::clamp(left,0.f,float(viewportWidth)); top = std::clamp(top,0.f,float(viewportHeight));
			right = std::clamp(right,left,float(viewportWidth)); bottom = std::clamp(bottom,top,float(viewportHeight));
		}
		return {left,top,right-left,bottom-top};
	}
	static size_t Bytes(const Geometry& geometry) {
		return sizeof(Geometry)+geometry.vertices.capacity()*sizeof(Rml::Vertex)+geometry.indices.capacity()*sizeof(int);
	}
	void Submit(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t texture,
		std::uint64_t mesh = 0, std::uint64_t revision = 0, std::shared_ptr<const Geometry::Submission> retained = {}, ImageSampling sampling={}) {
		if (!stack.empty() && stack.back().pending) {
			RecordedDraw draw; draw.texture=texture; draw.mesh=mesh; draw.revision=revision; draw.retained=std::move(retained);
			draw.sampling=std::move(sampling);
			if (!draw.retained) { draw.vertices=vertices; draw.indices=indices; }
			stack.back().draws.push_back(std::move(draw)); return;
		}
		DrawNow(vertices,indices,texture,mesh,revision,std::move(retained),std::move(sampling));
	}
	void DrawNow(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t texture,
		std::uint64_t mesh, std::uint64_t revision, std::shared_ptr<const Geometry::Submission> retained={}, ImageSampling sampling={}) {
		if (recordFrame && TopLayer()) InvalidateFrame();
		if (recordFrame) {
			frameRecordedVertices+=vertices.size();
			if (frameCommands.size()>=4096 || frameRecordedVertices>1048576) InvalidateFrame();
			else {
				FrameCommand command; auto& draw=command.draw;
				draw.texture=texture; draw.mesh=mesh; draw.revision=revision; draw.retained=std::move(retained);
				draw.sampling=std::move(sampling);
				if (draw.sampling.coordinates) {
					draw.mesh=draw.revision=0; draw.retained.reset();
					const auto& s=draw.sampling;
					for (const auto& v:vertices) {
						const float x=v.x-s.origin.x, y=v.y-s.origin.y;
						command.imageFractions.push_back({(x*s.cy-y*s.cx)/s.determinant,(s.bx*y-s.by*x)/s.determinant});
					}
				}
				if (!draw.retained) { draw.vertices=vertices; draw.indices=indices; }
				frameCommands.push_back(std::move(command));
			}
		}
		const auto started=statistics.Now();
		++statistics.drawCalls; statistics.submittedVertices += vertices.size(); statistics.submittedIndices += indices.size();
		if (mesh) host.DrawMesh(mesh,revision,vertices,indices,texture);
		else host.Draw(vertices,indices,texture);
		statistics.hostDrawMilliseconds+=std::chrono::duration<double,std::milli>(statistics.Now()-started).count();
	}
	Host& host;
	RuntimeStatistics& statistics;
	struct ClipRectangle { float left = 0, top = 0, right = 0, bottom = 0; };
	static constexpr float OpenClip = 1e30f;
	bool scissorEnabled = false, hasTransform = false, clipMaskEnabled = false;
	Rml::Rectanglei scissor;
	ClipRectangle clipMask;
	ClipPolygon clipPolygon;  // count 0 while the mask is a plain rectangle
	Rml::Matrix4f transform;
	int viewportWidth = 0, viewportHeight = 0;
	std::uint64_t viewportGeneration = 0;
	std::vector<Layer> stack;
	Rml::LayerHandle nextHandle = 0;
	LayerPool* pool = nullptr;
	bool failed = false;
};

class Files final : public Rml::FileInterface {
	struct File { std::string data; size_t position = 0; };
public:
	explicit Files(Host& h) : host(h) {}
	Rml::FileHandle Open(const Rml::String& path) override {
		auto file = std::make_unique<File>();
		if (!host.ReadFile(path, file->data)) return 0;
		return reinterpret_cast<Rml::FileHandle>(file.release());
	}
	void Close(Rml::FileHandle handle) override { delete reinterpret_cast<File*>(handle); }
	size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override {
		auto& file = *reinterpret_cast<File*>(handle);
		size = std::min(size, file.data.size() - file.position);
		if (size) std::memcpy(buffer, file.data.data() + file.position, size);
		file.position += size;
		return size;
	}
	bool Seek(Rml::FileHandle handle, long offset, int origin) override {
		auto& file = *reinterpret_cast<File*>(handle);
		if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) return false;
		const auto base = static_cast<std::int64_t>(origin == SEEK_SET ? 0 : origin == SEEK_CUR ? file.position : file.data.size());
		const auto position = base + static_cast<std::int64_t>(offset);
		if (position < 0 || static_cast<std::uint64_t>(position) > file.data.size()) return false;
		file.position = static_cast<size_t>(position);
		return true;
	}
	size_t Tell(Rml::FileHandle handle) override { return reinterpret_cast<File*>(handle)->position; }
private:
	Host& host;
};

class System final : public Rml::SystemInterface {
public:
	explicit System(Host& h) : host(h) {}
    const std::thread::id thread=std::this_thread::get_id();
    bool* preparationFailure=nullptr;
    void PreparationFault() noexcept {
        if(preparationFailure && thread==std::this_thread::get_id())*preparationFailure=true;
    }
	double GetElapsedTime() override { return time; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        const bool error=type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT || type == Rml::Log::LT_WARNING;
        if(error)PreparationFault();
		host.Log(error, message);
		return true;
	}
	int TranslateString(Rml::String& output, const Rml::String& input) override {
		output = host.Translate(input);
		return output != input ? 1 : 0;
	}
	// Engine material names are VFS-root identifiers, including generated font
	// pages. RML/RCSS references otherwise retain relative-path semantics.
	void JoinPath(Rml::String& output, const Rml::String& document, const Rml::String& path) override {
		if (path.compare(0, 9, "material:") == 0) output = path.substr(9);
		else Rml::SystemInterface::JoinPath(output, document, path);
	}
	double time = 0;
private:
	Host& host;
};

// Output-sized glyphs come from the host's retained font cache. Shared runs
// keep measurement, number-field geometry and submitted glyphs consistent.
// Style/weight selection and shaping remain separate font-service work.
class Fonts final : public Rml::FontEngineInterface {
	struct Face { std::string family; int size; Rml::FontMetrics metrics; };
public:
	explicit Fonts(Host& h,System& s) : host(h),system(s) {}
	Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle, Rml::Style::FontWeight, int size) override {
		size = std::clamp(size, 1, 512);
		const auto key = family + ":" + std::to_string(size);
		auto& face = faces[key];
		if (!face) {
			const auto metrics = host.GetFontMetrics(family, size);
			if (metrics.lineSpacing <= 0) { system.PreparationFault(); faces.erase(key); return 0; }
            if(system.preparationFailure && (!std::isfinite(metrics.ascent) || !std::isfinite(metrics.descent) ||
                !std::isfinite(metrics.lineSpacing) || !std::isfinite(metrics.xHeight))) {
                system.PreparationFault();faces.erase(key);return 0;
            }
			face = std::make_unique<Face>();
			face->family = family;
			face->size = size;
			face->metrics = {size, metrics.ascent, metrics.descent, metrics.lineSpacing, metrics.xHeight,
				metrics.descent * .5f, std::max(1.f, size / 16.f), false};
		}
		return reinterpret_cast<Rml::FontFaceHandle>(face.get());
	}
	const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override { return reinterpret_cast<Face*>(handle)->metrics; }
	// A 'pre' line reaches the engine with its line break, and a control
	// character has no glyph: it draws nothing and takes no room, rather than
	// the face's missing-glyph mark.
	Glyph Lookup(const Face& face, std::uint32_t codepoint) {
		if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0)) return {};
		return host.GetGlyph(face.family, face.size, codepoint);
	}
	// Number-field geometry queries this same immutable run as measurement and
	// font submission. Unknown/released handles and non-editable strings fail
	// without dereferencing a stale face or changing the legacy streaming path.
	std::shared_ptr<const TextRun> QueryRun(std::uintptr_t handle, std::string_view string, float letterSpacing) {
		const Face* face = nullptr;
		for (const auto& item : faces) if (reinterpret_cast<std::uintptr_t>(item.second.get()) == handle) { face = item.second.get(); break; }
		if (!face) return {};
		std::string error;
		return runs.Get(handle, string, letterSpacing,
			[&](std::uint32_t codepoint) { return Lookup(*face, codepoint); }, error);
	}
	int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& shaping, Rml::Character) override {
		const std::string_view text(string.begin(), string.size());
		if (const auto run = QueryRun(handle, text, shaping.letter_spacing)) return run->roundedWidth;
		const auto& face = *reinterpret_cast<Face*>(handle);
		const float width = WalkTextRun(text, shaping.letter_spacing,
			[&](std::uint32_t codepoint) { return Lookup(face, codepoint); });
		return static_cast<int>(std::lround(width));
	}
	int GenerateString(Rml::RenderManager& manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle,
		Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float,
		const Rml::TextShapingContext& shaping, Rml::TexturedMeshList& meshes) override {
		const std::string_view text(string.begin(), string.size());
		std::string lastMaterial;
		const auto emit = [&](const TextRunGlyph& record) {
			const auto& glyph = record.glyph;
			if (!glyph.material.empty() && glyph.width > 0 && glyph.height > 0) {
				if (meshes.empty() || lastMaterial != glyph.material) {
					meshes.push_back({{}, manager.LoadTexture("material:" + glyph.material)});
					lastMaterial = glyph.material;
				}
				auto& mesh = meshes.back().mesh;
				const int base = static_cast<int>(mesh.vertices.size());
				const float x = position.x + record.penX + glyph.left, y = position.y + glyph.top;
				mesh.vertices.insert(mesh.vertices.end(), {
					{{x,y},colour,{glyph.u0,glyph.v0}}, {{x+glyph.width,y},colour,{glyph.u1,glyph.v0}},
					{{x+glyph.width,y+glyph.height},colour,{glyph.u1,glyph.v1}}, {{x,y+glyph.height},colour,{glyph.u0,glyph.v1}}});
				mesh.indices.insert(mesh.indices.end(), {base,base+3,base+1,base+1,base+3,base+2});
			}
		};
		if (const auto run = QueryRun(handle, text, shaping.letter_spacing)) {
			for (const auto& record : run->glyphs) emit(record);
			return run->roundedWidth;
		}
		const auto& face = *reinterpret_cast<Face*>(handle);
		const float width = WalkTextRun(text, shaping.letter_spacing,
			[&](std::uint32_t codepoint) { return Lookup(face, codepoint); }, emit);
		return static_cast<int>(std::lround(width));
	}
	void ReleaseFontResources() override { runs.Clear(); faces.clear(); }
	void Shutdown() override { runs.Clear(); faces.clear(); }
private:
	Host& host;
	System& system;
	TextRunCache runs;
	std::map<std::string, std::unique_ptr<Face>> faces;
};

// openQ4: a canonical load builds its elements from the model with typed
// inline properties, without deriving markup and having RmlUi parse it. Each
// property is what RmlUi's parser produces from the value's CSS: the parser
// reads one representative declaration per property name and kind (a
// number, a length per unit, a colour, a font, a transform) and each keyword
// once, and that parse keeps the property's definition and parser; a value
// then takes the parsed template with the number, colour, string or
// transform the parser would read from its CSS. Shorthands (padding,
// border-width, overflow) and the effect properties (filters, decorators,
// the mask) still read their CSS, so their longhands and declaration lists
// are the parser's own. The cache lives with RmlUi's specification.
class InlineProperties {
public:
	// Declares `name: value` in `properties` as the parse of the value's CSS would.
	bool Declare(Rml::PropertyDictionary& properties, const std::string& name, const Value& value) {
		Name& entry = Lookup(name);
		if (entry.shorthand) return Rml::StyleSheetSpecification::ParsePropertyDeclaration(properties,name,value.Css());
		if (value.type == ValueType::Keyword) {
			auto found = entry.keywords.find(value.text);
			if (found == entry.keywords.end()) {
				auto parsed = Parse(name,value.text);
				if (!parsed) return Rml::StyleSheetSpecification::ParsePropertyDeclaration(properties,name,value.Css());
				found = entry.keywords.emplace(value.text,std::move(*parsed)).first;
			}
			properties.SetProperty(entry.id,found->second); return true;
		}
		const Rml::Property* base = nullptr;
		switch (value.type) {
		case ValueType::Number: base = Template(entry.number,name,"1"); break;
		case ValueType::Length: { const int unit = LengthUnit(value.unit); if (unit >= 0) base = Template(entry.lengths[unit],name,"1"+value.unit); break; }
		case ValueType::Colour: base = Template(entry.colour,name,"rgba(0,0,0,255)"); break;
		case ValueType::Font: base = Template(entry.font,name,"marine"); break;
		case ValueType::Transform: base = Template(entry.transform,name,"translate(0dp,0dp) scale(1,1) rotate(0deg)"); break;
		default: break;
		}
		if (!base) return Rml::StyleSheetSpecification::ParsePropertyDeclaration(properties,name,value.Css());
		Rml::Property property = *base;
		switch (value.type) {
		case ValueType::Number: case ValueType::Length: property.value = Rml::Variant(Number(value.data[0])); break;
		case ValueType::Colour: property.value = Rml::Variant(Colour(value)); break;
		case ValueType::Font: property.value = Rml::Variant(Rml::String(value.text)); break;
		case ValueType::Transform: property.value = Rml::Variant(Transform(value)); break;
		default: break;
		}
		properties.SetProperty(entry.id,property); return true;
	}
	// Declares an effect property (filter, backdrop-filter, decorator,
	// mask-image) from its CSS, parsed once per distinct declaration.
	bool DeclareEffect(Rml::PropertyDictionary& properties, const std::string& name, const std::string& css) {
		Name& entry = Lookup(name);
		if (entry.shorthand) return Rml::StyleSheetSpecification::ParsePropertyDeclaration(properties,name,css);
		auto found = entry.keywords.find(css);
		if (found == entry.keywords.end()) {
			auto parsed = Parse(name,css);
			if (!parsed) return Rml::StyleSheetSpecification::ParsePropertyDeclaration(properties,name,css);
			found = entry.keywords.emplace(css,std::move(*parsed)).first;
		}
		properties.SetProperty(entry.id,found->second); return true;
	}
	// The float RmlUi's number parser reads from a value's formatted number
	// (strtof of the digits Value::Css writes). An integer below 2^24 converts
	// exactly either way; any other number is formatted and read, once per
	// distinct value.
	float Number(double value) {
		if (value == std::trunc(value) && std::fabs(value) < 16777216.0) return static_cast<float>(value);
		std::uint64_t bits; std::memcpy(&bits,&value,sizeof bits);
		auto& slot = fractions[static_cast<size_t>((bits*0x9E3779B97F4A7C15ull)>>54)];
		if (slot.valid && slot.bits == bits) return slot.result;
		char buffer[64];
		const auto end = FloatToChars(buffer,buffer+sizeof(buffer)-1,value,std::chars_format::general,10).ptr;
		*end = 0;
		slot = {bits,std::strtof(buffer,nullptr),true};
		return slot.result;
	}
	// The colour RmlUi's parser reads from rgba(): each channel's CSS is the
	// decimal integer round(x*255), read back as that integer and clamped.
	static Rml::Colourb Colour(const Value& value) {
		Rml::byte channels[4];
		for (int i = 0; i < 4; ++i)
			channels[i] = static_cast<Rml::byte>(std::clamp(std::round(value.data[i]*255),0.0,255.0));
		return Rml::Colourb(channels[0],channels[1],channels[2],channels[3]);
	}
	// The parsed properties reference RmlUi's specification: cleared before it shuts down.
	void Clear() { names.clear(); fractions.fill({}); }
	// The transform RmlUi's parser builds from "translate(x,y) scale(x,y)
	// rotate(deg)": the same three primitives from the same parsed numbers.
	Rml::TransformPtr Transform(const Value& value) {
		const Rml::Unit unit = value.unit == "px" ? Rml::Unit::PX : Rml::Unit::DP;
		auto transform = Rml::MakeShared<Rml::Transform>();
		const Rml::NumericValue translate[2] = {{Number(value.data[0]),unit},{Number(value.data[1]),unit}};
		transform->AddPrimitive({Rml::Transforms::Translate2D(translate)});
		const Rml::NumericValue scale[2] = {{Number(value.data[2]),Rml::Unit::NUMBER},{Number(value.data[3]),Rml::Unit::NUMBER}};
		transform->AddPrimitive({Rml::Transforms::Scale2D(scale)});
		const Rml::NumericValue rotate[1] = {{Number(value.data[4]),Rml::Unit::DEG}};
		transform->AddPrimitive({Rml::Transforms::Rotate2D(rotate)});
		return transform;
	}
private:
	struct Name {
		Rml::PropertyId id = Rml::PropertyId::Invalid;
		bool shorthand = false; // Or unknown: its CSS is parsed.
		std::optional<Rml::Property> number, colour, font, transform;
		std::array<std::optional<Rml::Property>,4> lengths; // dp, px, %, em
		std::unordered_map<std::string,Rml::Property> keywords; // Also an effect property's declarations.
	};
	std::unordered_map<std::string,Name> names;
	struct Fraction { std::uint64_t bits = 0; float result = 0; bool valid = false; };
	std::array<Fraction,1024> fractions{};
	Name& Lookup(const std::string& name) {
		const auto found = names.find(name);
		if (found != names.end()) return found->second;
		Name entry;
		entry.id = Rml::StyleSheetSpecification::GetPropertyId(name);
		entry.shorthand = entry.id == Rml::PropertyId::Invalid;
		return names.emplace(name,std::move(entry)).first->second;
	}
	static int LengthUnit(const std::string& unit) {
		return unit == "dp" ? 0 : unit == "px" ? 1 : unit == "%" ? 2 : unit == "em" ? 3 : -1;
	}
	// Parses `name: css` and returns the one property it declares.
	static std::optional<Rml::Property> Parse(const std::string& name, const std::string& css) {
		Rml::PropertyDictionary parsed;
		if (!Rml::StyleSheetSpecification::ParsePropertyDeclaration(parsed,name,css) || parsed.GetNumProperties() != 1) return std::nullopt;
		return parsed.GetProperties().begin()->second;
	}
	static const Rml::Property* Template(std::optional<Rml::Property>& slot, const std::string& name, const std::string& css) {
		if (!slot) { auto parsed = Parse(name,css); if (!parsed) return nullptr; slot = std::move(*parsed); }
		return &*slot;
	}
};

struct Backend {
	explicit Backend(Host& host) : renderer(host,statistics) {}
	RuntimeStatistics statistics;
	Renderer renderer;
	bool leased = false;
};

// File, font and element factories belong to RmlUi's process lifetime. Contexts
// have separate renderers, state and clocks, but share the same engine host.
struct Services {
	Services(Host& host, LayerPool& layers) : host(host), files(host), system(host), fonts(host,system), layers(layers) {}
	Host& host;
	Files files;
	System system;
	Fonts fonts;
	LayerPool& layers;
	std::vector<std::unique_ptr<Backend>> backends;
	Rml::ElementInstancerGeneric<VectorElement> vectorInstancer;
	Rml::ElementInstancerGeneric<HudElement> hudInstancer;
	std::unique_ptr<VectorMaskInstancer> maskInstancer;
	std::unique_ptr<StreakFilterInstancer> streakInstancer;
	std::unique_ptr<LeafOpacityFilterInstancer> leafOpacityInstancer;
	InlineProperties inlineProperties; // The canonical load's parsed property templates.
	bool initialized = false;
	Backend* AcquireBackend() {
		for (auto& backend : backends) if (!backend->leased) {
			backend->leased = true; backend->statistics = {}; backend->renderer.Attach(layers);
			return backend.get();
		}
		backends.push_back(std::make_unique<Backend>(host));
		auto* backend = backends.back().get();
		backend->leased = true; backend->renderer.Attach(layers);
		return backend;
	}
	void ReleaseBackend(Backend& backend) {
		// Keep the render interface alive for RmlUi's cached render manager,
		// reusing idle backends instead of retaining one per departed document.
		// ReleaseRenderManagers would update EVERY live context on this view's
		// clock, so use the renderer-scoped resource release APIs instead.
		Rml::ReleaseTextures(&backend.renderer);
		Rml::ReleaseCompiledGeometry(&backend.renderer);
		backend.renderer.Detach();
		backend.leased = false;
	}
	bool Initialize() {
		Rml::SetRenderInterface(nullptr); // Every context supplies its own renderer.
		Rml::SetFileInterface(&files);
		Rml::SetSystemInterface(&system);
		Rml::SetFontEngineInterface(&fonts);
		if (!Rml::Initialise()) return false;
		initialized = true;
		maskInstancer = std::make_unique<VectorMaskInstancer>();
		streakInstancer = std::make_unique<StreakFilterInstancer>();
		leafOpacityInstancer = std::make_unique<LeafOpacityFilterInstancer>();
		Rml::Factory::RegisterElementInstancer("q4-vector",&vectorInstancer);
		Rml::Factory::RegisterElementInstancer("q4-node",&vectorInstancer);
		Rml::Factory::RegisterElementInstancer("q4-hud",&hudInstancer);
		Rml::Factory::RegisterDecoratorInstancer("q4-mask",maskInstancer.get());
		Rml::Factory::RegisterFilterInstancer("streak",streakInstancer.get());
		Rml::Factory::RegisterFilterInstancer("q4-opacity",leafOpacityInstancer.get());
		return true;
	}
	~Services() {
		inlineProperties.Clear();
		if (initialized) Rml::Shutdown();
		maskInstancer.reset();
		streakInstancer.reset();
		Rml::SetRenderInterface(nullptr);
		Rml::SetFileInterface(nullptr);
		Rml::SetSystemInterface(nullptr);
		Rml::SetFontEngineInterface(nullptr);
	}
};
std::weak_ptr<Services> activeServices;
std::uint64_t nextContext = 0;

// RmlUi callbacks must observe the clock of the view being evaluated. Restoring
// it also avoids a document load changing the clock of an outer operation.
struct ContextClock {
	System& system;
	double previous;
    bool* previousFailure;
	ContextClock(Services& services, double time,bool* failure) : system(services.system), previous(system.time),previousFailure(system.preparationFailure) {
        system.time = time;system.preparationFailure=failure;
    }
	~ContextClock() { system.time = previous;system.preparationFailure=previousFailure; }
};
} // namespace

struct Host::Shared { LayerPool layers; };
Host::Host() = default;
Host::~Host() = default;

// The safe area's size: the view less its insets, never less than a pixel,
// so a malformed inset cannot collapse the canvas.
static float SafeExtent(int extent, float lead, float trail) {
	const auto inset = [](float value) { return std::isfinite(value) ? std::max(value,0.f) : 0.f; };
	return extent > 0 ? std::max(static_cast<float>(extent)-inset(lead)-inset(trail),std::min(static_cast<float>(extent),1.f)) : 0.f;
}
float Viewport::SafeWidth() const { return SafeExtent(width,safeLeft,safeRight); }
float Viewport::SafeHeight() const { return SafeExtent(height,safeTop,safeBottom); }
float Viewport::DpRatio() const {
	// A view-height canvas follows the view like the HUD's center screen; the
	// 4:3 stock composition always fills the height and widens with the aspect.
	// Like section 13.3's size classes, it measures the safe area.
	if (canvasHeight > 0 && std::isfinite(canvasHeight) && height > 0) {
		const float scale = SafeHeight() / canvasHeight;
		return canvasMinWidth > 0 && std::isfinite(canvasMinWidth) && width > 0 ?
			std::min(scale,SafeWidth() / canvasMinWidth) : scale;
	}
	return Positive(displayScale) * std::clamp(Positive(userScale), .75f, 2.f) * std::min(Positive(fitScale), 1.f);
}
void Viewport::FitToMinimum(float widthDp, float heightDp) {
	fitScale = 1;
	if (width <= 0 || height <= 0 || !std::isfinite(widthDp) || !std::isfinite(heightDp) || widthDp <= 0 || heightDp <= 0) return;
	const float requested = DpRatio();
	fitScale = std::min({1.f, width / (widthDp * requested), height / (heightDp * requested)});
}
float Viewport::TextRatio() const { return std::clamp(Positive(textScale), 1.f, 2.f); }
void Viewport::WindowToDocument(float x, float y, float& outX, float& outY) const {
	outX = x * Positive(pixelDensityX) - originX;
	outY = y * Positive(pixelDensityY) - originY;
}

struct Runtime::Canvas {
    RuntimeCanvasIdentity identity;
    const std::thread::id thread=std::this_thread::get_id();
    bool alive=true,preparing=false,retiring=false;
    std::weak_ptr<PreparedDocument::Data> pending;
};

struct Runtime::Impl {
	explicit Impl(Host& host,std::shared_ptr<Canvas> owner) : host(host),ownerCanvas(std::move(owner)) {}
	Host& host;
    bool strictPreparation=false,preparationFailed=false;
    std::shared_ptr<Canvas> ownerCanvas;
	std::shared_ptr<Services> services;
	RuntimeStatistics statistics;
	Backend* backend = nullptr;
	RuntimeStatistics& Stats() { return backend ? backend->statistics : statistics; }
	double time = 0;
	std::string contextName, sourcePath;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	HudElement* hud = nullptr;
	std::unique_ptr<Document> canonical;
	struct ModelProjection { const ModelSceneSpec* spec=nullptr; VectorElement* element=nullptr; std::uint64_t lease=0; };
	std::vector<ModelProjection> models;
	void ReleaseModelScenes() {
		auto retired=std::move(models); models.clear();
		for (const auto& scene:retired) if (scene.lease) host.ReleaseModelScene(scene.lease);
	}
	// Immutable, JSON-escaped identity, rebuilt with each document replacement.
	// HUD source can be several MiB; checkpoints must not escape it every time.
	std::string snapshotDocumentIdentity;
	Motion motion;
	State state;
	struct StateSource { std::string id, cvar, view; size_t type; };
	std::vector<StateSource> stateSources;
	std::string stateError;
	std::uint64_t appliedStateRevision = 0;
	Interaction interaction;
	ValueControlView valueView;
	ScrollbarView scrollView;
	bool scrollLayoutDirty=false, preserveRestoredScroll=false;
	NumberControlView numberView;
	Viewport viewport;
	std::string focusLayoutId;
	std::vector<Rml::Vector2f> focusLayout;
	bool focusLayoutVisible = false;
	std::vector<std::string> controls;
	std::vector<Rml::Element*> layoutWidgets;
	float pointerX = 0, pointerY = 0;
	float windowPointerX = 0, windowPointerY = 0;
	bool pointerPresent = false;
	// A touch press (section 13.6) and what it becomes: a tap while it stays
	// within 8 dp; past that, a drag of the scrolling container it pressed in
	// (moved more up or down than across) or a swipe across the tab strip it
	// pressed on (more across); a long-press once it rests 500 ms on a button
	// that names a detail. Where and when it pressed, the container's offset
	// then, and the finger's recent heights for its release velocity.
	bool touchPointer = false;
	struct Touch {
		bool pressed = false, moved = false, dragging = false, swiping = false, strip = false;
		float x = 0, y = 0, offset = 0; double at = 0;
		std::string container, detail;
		std::vector<std::pair<double,float>> samples;
	} touch;
	// A released drag's inertia: its container, the offset it has reached and
	// its velocity in pixels a second, when it last advanced, and the focus and
	// modal it began under, either of which moving stops it.
	struct Fling { std::string container, focus, modal; double offset = 0, velocity = 0, at = 0; } fling;
	// The rim light at one end of a list (section 13.6): its container and
	// end, and its brightness, held by a finger pulling past that end or else
	// fading from `from` since `fadeAt`. The runtime draws it in its own
	// element over the document, which closing the document destroys.
	struct Rim { std::string container; bool end = false, fading = false; float strength = 0, from = 0; double fadeAt = 0; } rim;
	Rml::Element* rimElement = nullptr;
	bool rimElementEnd = false;
	// A long-pressed button whose detail stays shown until the next press,
	// navigation or mouse movement.
	std::string detailPin;
	std::vector<std::string> gestureEvents;
	bool pointerNavigation = false;
	// Which device leads (section 13.4): the pointer once it moves or
	// presses, the keyboard or controller once a menu input or a program's
	// focus arrives. The interaction shows focus only while they lead.
	void SetPointerNavigation(bool pointer) { pointerNavigation = pointer; interaction.SetPointerPresentation(pointer); }
	// The detail card (DocumentModel::detail): its subject, whether the pointer
	// chose it, and the card's last placement relative to its containing block.
	std::string detailSubject;
	bool detailByPointer = false;
	float detailLeft = 0, detailTop = 0;
	bool detailPlaced = false;
	// A program's focus request that the current layout could not take: its
	// control appears with the same state change. The next frame's layout
	// takes it, or it lapses.
	std::string pendingFocus;
	// The document's inherited default font size as last applied; empty
	// until the first pass.
	std::string documentFontCss;
	// ApplyMotion's last applied input for each motion value, by position,
	// whether a binding owns it, and the state revision and text ratio it was
	// formatted under, with the CSS it installed on its element (cssApplied).
	struct AppliedInput {
		// The authored key this slot last applied (the motion's authored map
		// keeps every key for the document's life), or none.
		const PropertyKey* key = nullptr;
		Value value; bool bound = false; Rml::Element* element = nullptr; bool deferred = false;
		// The CSS installed: `css`, or (cssAuthored) the authored formatting
		// of `value`, which the load built and no pass has formatted.
		std::string css; bool cssApplied = false, cssAuthored = false;
		void Set(const PropertyKey* k, const Value& v, bool owned, Rml::Element* e) { key = k; value = v; bound = owned; element = e; deferred = false; }
		// The formatting the load built the value with: its CSS, an opacity's filter.
		std::string AuthoredCss() const { return key && key->second == "opacity" ? "opacity("+value.Css()+")" : value.Css(); }
		bool InstalledCss(const std::string& string) const { return cssAuthored ? AuthoredCss() == string : css == string; }
	};
	std::vector<AppliedInput> appliedInputs;
	// A load builds every authored value into its element's inline
	// properties. They are current until the first complete pass, which
	// records a value still at its authored one as applied instead of
	// parsing it again; the nodes the value-control view styled at load (the
	// choice popups and viewports) take the ordinary path.
	bool inlineAuthored = false;
	std::set<std::string_view> inlineOverridden;
	std::vector<PropertyKey> deferredInputs;
	std::uint64_t appliedInputRevision = 0;
	// The motion stamp of the last pass that applied every value (0: none
	// yet, or a pass left a value for a later one).
	std::uint64_t appliedMotionRevision = 0;
	// Counts ApplyMotion's passes that can write presentation; value controls
	// reassert a settled closed list only after one (ValueControlView::Paint).
	std::uint64_t presentationPass = 1;
	// Each motion value's position in appliedInputs: the authored keys in
	// Values()' order (the live values keep the authored key set), found by
	// binary search, and the authored map they index.
	std::vector<const PropertyKey*> inputKeys;
	const PropertyValues* indexedKeys = nullptr;
	bool InputsIndexed() const { return indexedKeys && indexedKeys == &motion.Base() && inputKeys.size() == motion.Values().size(); }
	// The key's position, or inputKeys.size() for a key the inputs lack.
	size_t InputIndex(const PropertyKey& key) const {
		const auto found = std::lower_bound(inputKeys.begin(),inputKeys.end(),key,[](const PropertyKey* a, const PropertyKey& b) { return *a < b; });
		return found != inputKeys.end() && **found == key ? static_cast<size_t>(found-inputKeys.begin()) : inputKeys.size();
	}
	float appliedInputTextRatio = 0;
	bool presentationDirty = true;
	bool paintDirty = true;
	std::vector<VectorElement*> changedProjections;
	bool interactionLayoutDirty = true;
	// Only controls/modals and their ancestors can affect interaction.
	// Artwork-heavy screens keep most of their nodes outside these trees.
	// Canonical nodes/elements survive derived widget reparenting and text
	// changes; replacing/closing a document retires both trees and handles.
	struct InteractionBranch {
		const Node* node=nullptr;
		Rml::Element* element=nullptr;
		std::vector<InteractionBranch> children;
	};
	InteractionBranch inputBranches,modalBranches;
	std::set<Rml::Element*> interactionAncestors;
	std::unordered_map<std::string_view,Rml::Element*> elementIndex;
	// Canonical nodes survive text/property edits and widget reparenting.
	// Cache successful layout readbacks for the document's lifetime.
	mutable std::unordered_map<std::string,Rml::Element*> boundsElements;
	InteractionBranch BuildInteractionBranch(const Node& node,bool modals) {
		InteractionBranch result;
		for (const auto& child:node.children) {
			auto branch=BuildInteractionBranch(child,modals);
			if (branch.node) result.children.push_back(std::move(branch));
		}
		if ((modals ? bool(node.modal) : bool(node.control)) || !result.children.empty()) {
			result.node=&node;
			interactionAncestors.insert(CanonicalElement(node.id));
			if (!modals && (node.control || node.mask)) {
				result.element=CanonicalElement(node.id);
				if (node.control) elementIndex.emplace(node.id,result.element);
			}
		}
		return result;
	}
	Rml::Element* IndexedElement(const std::string& id) const {
		const auto found = elementIndex.find(id);
		return found == elementIndex.end() ? nullptr : found->second;
	}
	// Canonical node elements by ID, resolved once per load. A canonical
	// document keeps every node's element until it closes (text nodes own no
	// canonical children; a choice popup moves under the document but lives
	// on), so frames resolve IDs here instead of searching the document,
	// which RmlUi does breadth-first over every element.
	std::unordered_map<std::string,Rml::Element*> canonicalElements;
	Rml::Element* CanonicalElement(const std::string& id) const {
		const auto found = canonicalElements.find(id);
		return found != canonicalElements.end() ? found->second : document ? document->GetElementById(id) : nullptr;
	}
	// Builds the document a canonical load presents: the derived markup's
	// style sheet, then every node's element with its id and the typed
	// properties its inline style would carry, each appended to its parent
	// before its children, as the markup parser appends them. `elements`
	// maps each node ID to its element. The caller shows the document, which
	// lays it out once (a markup load laid it out hidden first, then again on
	// show; nothing here observes the load or show events). A document that
	// cannot be built is closed and none is returned.
	Rml::ElementDocument* BuildCanonicalDocument(const DocumentModel& model, const std::string& sourcePath,
		std::unordered_map<std::string,Rml::Element*>& elements) {
		auto* document = context->CreateDocument();
		if (!document) return nullptr;
		{
			// Native unstyled scrollbars default to the containing width and can
			// consume the entire client area. Scrolling stays semantic until
			// authored scrollbar parts provide the editable vector control and
			// its pointer interactions.
			static constexpr std::string_view sheet = "body{margin:0;width:100%;height:100%;font-family:marine;font-size:16dp;color:#fff;}"
				"div,q4-node,q4-vector{display:block;}scrollbarvertical{width:0;}scrollbarhorizontal{height:0;}";
			auto container = Rml::MakeShared<Rml::StyleSheetContainer>();
			Rml::StreamMemory stream(reinterpret_cast<const Rml::byte*>(sheet.data()),sheet.size());
			stream.SetSourceURL(Rml::URL(Rml::URL(sourcePath).GetURL()));
			if (container->LoadStyleSheetContainer(&stream,1)) document->SetStyleSheetContainer(std::move(container));
		}
		if (!AppendCanonical(*document,model.root,elements)) { document->Close(); return nullptr; }
		return document;
	}
	bool AppendCanonical(Rml::Element& parent, const Node& node, std::unordered_map<std::string,Rml::Element*>& elements) {
		auto& templates = services->inlineProperties;
		static const Rml::String nodeTag = "q4-node", vectorTag = "q4-vector";
		static const Value one = [] { Value value; value.type = ValueType::Number; value.data[0] = 1; return value; }();
		static const Value relative = [] { Value value; value.type = ValueType::Keyword; value.text = "relative"; return value; }();
		const Rml::String& tag = node.type == "vector" ? vectorTag : nodeTag;
		// The properties in the order the node's inline style would declare
		// them, so the element's inline properties keep that order.
		Rml::PropertyDictionary properties;
		// Canonical opacity isolates the complete node subtree. RmlUi's ordinary
		// opacity is an inherited primitive tint; its filter supplies the needed
		// stacking/render boundary without changing the editable source format.
		templates.Declare(properties,"opacity",one);
		// Button parts (focus rail, marker, label) use the button's local box even
		// when the button itself participates in normal document flow.
		if (node.control) templates.Declare(properties,"position",relative);
		if (node.mask) templates.DeclareEffect(properties,"mask-image","q4-mask(alpha)");
		const auto blur = node.properties.find("backdrop-blur"), saturate = node.properties.find("backdrop-saturate");
		const auto streak = node.properties.find("backdrop-streak");
		if (blur != node.properties.end() || saturate != node.properties.end() || streak != node.properties.end()) {
			const auto css = BackdropFilterCss(blur != node.properties.end() ? &blur->second : nullptr,
				saturate != node.properties.end() ? &saturate->second : nullptr, streak != node.properties.end() ? &streak->second : nullptr);
			if (css != "none") templates.DeclareEffect(properties,"backdrop-filter",css);
		}
		for (const auto& [name,value] : node.properties) {
			// The same cases as the derived markup's, told apart by their
			// first letter before any comparison.
			const char first = name.empty() ? 0 : name[0];
			if (first == 't' && name == "text") continue;
			if (first == 'o' && name == "opacity") { if (value.data[0] < 1) templates.DeclareEffect(properties,"filter","opacity("+value.Css()+")"); }
			else if (first == 'b' && name.starts_with("backdrop-") && (name == "backdrop-blur" || name == "backdrop-saturate" || name == "backdrop-streak")) continue; // Folded into backdrop-filter.
			else if (first == 'i' && name == "image") { if (!HasImageCoordinates(node)) templates.DeclareEffect(properties,"decorator",ImageDecorator(node,value.text)); }
			else if (first == 'i' && name.starts_with("image-") && (name == "image-offset-x" || name == "image-offset-y" || name == "image-scale-x" || name == "image-scale-y")) continue;
			else if (first == 'i' && name.starts_with("image-") && (name == "image-fit" || name == "image-align-x" || name == "image-align-y" || name == "image-blend")) continue; // Folded into the decorator.
			else templates.Declare(properties,name,value);
		}
		auto element = Rml::Factory::InstanceElement(&parent,tag,tag,Rml::XMLAttributes());
		if (!element) return false;
		element->SetId(node.id);
		for (const auto& [id,property] : properties.GetProperties()) element->SetProperty(id,property);
		auto* appended = parent.AppendChild(std::move(element));
		elements.emplace(node.id,appended);
		for (const auto& child : node.children) if (!AppendCanonical(*appended,child,elements)) return false;
		return true;
	}
	bool initialized = false;
	std::map<std::string,bool> inputAllowed;
	std::string modalError;
	StateValues completionWrites;
	// Completion programs run in completion order at the time that completed
	// them, each as its own transaction with no action budget, on the given
	// candidate state and motion. A failed program is logged and skipped, as a
	// frame has no caller to fail. The document's completion graph is acyclic;
	// the round cap only bounds a pathological chain of replays.
	// Returns whether any program committed.
	bool RunCompletions(State& candidateState, Motion& candidateMotion, double now, StateValues& writes, std::string* focus = nullptr) const {
		if (!canonical) { candidateMotion.TakeCompleted(); return false; }
		const auto& model = canonical->Model();
		bool committed = false;
		for (unsigned round = 0; round < 64; ++round) {
			const auto finished = candidateMotion.TakeCompleted();
			if (finished.empty()) return committed;
			for (const auto& id : finished) {
				const auto timeline = std::find_if(model.timelines.begin(),model.timelines.end(),[&](const Timeline& item) { return item.id == id; });
				if (timeline == model.timelines.end() || timeline->complete.empty()) continue;
				EventResult candidate; std::string error;
				if (!EvaluateEvent(model,candidateState,candidateMotion,timeline->complete,now,candidate,error,{},0) ||
					!ValidModals(candidate.state,candidate.motion,error)) {
					host.Log(true,"Completion program of timeline '"+id+"' failed: "+error); continue;
				}
				for (auto& [key,value] : candidate.stateChanges) writes[key] = std::move(value);
				if (focus && !candidate.focus.empty()) *focus = std::move(candidate.focus);
				candidateState = std::move(candidate.state); candidateMotion = std::move(candidate.motion);
				committed = true;
			}
		}
		host.Log(true,"Timeline completion programs did not settle within 64 rounds");
		return committed;
	}
	// Focus a program asked for: now, when the control can take it, else
	// after the next frame's layout.
	void RequestFocus(const std::string& id) {
		SetPointerNavigation(false); fling = {}; detailPin.clear();
		if (interaction.Focus(id)) { pendingFocus.clear(); RevealFocus(); }
		else pendingFocus = id;
	}
	bool PrepareNumberEdit(double seconds, std::string& error) {
		if (!canonical || !document || !std::isfinite(seconds) || seconds < 0) {
			error = "Number editing requires a canonical document and valid presentation time"; return false;
		}
		ReadStateSources();
		if (!stateError.empty()) { error = stateError; return false; }
		return UpdateInteraction(seconds,false,true);
	}
    bool PrepareChoicePopup(double seconds) {
        if (!canonical || !document || viewport.width <= 0 || viewport.height <= 0 ||
            !std::isfinite(seconds) || seconds < 0) return false;
        ReadStateSources();
        if (!stateError.empty()) return false;
        return UpdateInteraction(seconds,false,true);
    }
	bool RefreshNumberFocusLayout(std::string& error) {
		if (!context || viewport.width <= 0 || viewport.height <= 0) {
			error = "Number focus requires a usable layout viewport"; return false;
		}
		// A program can hide its modal and request draft focus in the same
		// dispatch. Resolve the resulting pending focus from newly projected
		// layout, before testing the requested field's actual eligibility.
		ContextClock clock(*services,time,strictPreparation?&preparationFailed:nullptr);
		ApplyMotion(); context->Update();
		context->GetRootElement()->UpdateGeometryForProjection();
		return UpdateInteraction(-1,true);
	}
	static std::optional<Value> Property(const State& state, const Motion& motion, const PropertyKey& key) {
		const auto bound = state.Properties().find(key);
		if (bound != state.Properties().end()) return bound->second;
		const auto animated = motion.Values().find(key);
		return animated == motion.Values().end() ? std::nullopt : std::optional<Value>(animated->second);
	}
	std::optional<Value> PresentedProperty(const PropertyKey& key) const { return Property(state,motion,key); }
	bool VisibleModals(const State& state, const Motion& motion, std::vector<std::string>& roots, std::string& error) const {
		roots.clear(); error.clear();
		if (!canonical || !interaction.HasAuthoredModals()) return true;
		bool valid = true;
		std::function<void(const InteractionBranch&,bool,const std::string&)> visit = [&](const InteractionBranch& branch, bool inherited, const std::string& parent) {
			if (!branch.node) return;
			const auto& node=*branch.node;
			const auto display = Property(state,motion,{node.id,"display"});
			const bool visible = inherited && (!display || display->text != "none");
			std::string scope = parent;
			if (visible && node.modal) {
				if (!roots.empty() && roots.back() != parent) valid = false;
				roots.push_back(node.id); scope = node.id;
			}
			for (const auto& child : branch.children) visit(child,visible,scope);
		};
		visit(modalBranches,true,{});
		if (!valid) error = "Visible authored modals must form one nested ancestry chain";
		return valid;
	}
	bool ValidModals(const State& state, const Motion& motion, std::string& error) const {
		std::vector<std::string> roots; return VisibleModals(state,motion,roots,error);
	}
	bool SyncModals() {
		std::vector<std::string> roots; std::string error;
		VisibleModals(state,motion,roots,error);
		const bool valid = interaction.SyncAuthoredModals(roots,error);
		if (!valid && error != modalError) host.Log(true,error);
		modalError = std::move(error);
		// An authored modal opens with its scroll viewports at their start,
		// so a list reopened later shows its first entries again.
		if (valid) {
			for (const auto& root : roots)
				if (std::find(openModals.begin(),openModals.end(),root) == openModals.end()) ResetModalScroll(root);
			openModals = roots;
		}
		return valid;
	}
	void ResetModalScroll(const std::string& root) {
		const Node* node = canonical ? canonical->Model().FindNode(root) : nullptr;
		std::vector<const Node*> pending; if (node) pending.push_back(node);
		while (!pending.empty()) {
			const Node* current = pending.back(); pending.pop_back();
			const auto overflow = current->properties.find("overflow");
			if (overflow != current->properties.end() && (overflow->second.text == "auto" || overflow->second.text == "scroll"))
				if (auto* element = CanonicalElement(current->id)) { element->SetScrollTop(0); element->SetScrollLeft(0); }
			for (const auto& child : current->children) pending.push_back(&child);
		}
	}
	std::vector<std::string> openModals;
	void CollectInputEligibility(const InteractionBranch& branch, bool inherited = true) {
		if (!branch.node) return;
		const auto& node=*branch.node;
		const auto display = PresentedProperty({node.id,"display"});
		const auto events = PresentedProperty({node.id,"pointer-events"});
		auto* element = branch.element;
		const bool maskArea = !node.mask || (element && static_cast<VectorElement*>(element)->HasMaskArea());
		const bool allowed = inherited && maskArea && (!display || display->text != "none") && (!events || events->text != "none");
		if (node.control) inputAllowed[node.id] = allowed;
		for (const auto& child : branch.children) CollectInputEligibility(child,allowed);
	}
	void ApplyControlBindings() {
		SyncModals(); // Capture the opener before enabled/readback changes clear it.
		if (appliedStateRevision == state.Revision()) return;
		interactionLayoutDirty=true;
		std::string error;
		if (!interaction.SetReadbacks(state.ControlValues(),error)) { host.Log(true,error); return; }
		for (const auto& [id,enabled] : state.Enabled()) interaction.SetEnabled(id,enabled);
		appliedStateRevision = state.Revision(); Feedback(time);
	}
	void ReadStateSources() {
		StateValues changes; std::string error;
		for (const auto& source : stateSources) {
			StateValue value;
			if (!source.view.empty()) {
				// The text size the runtime applies to font lengths, or the safe
				// area's shape or an inset to it in dp, which a view not yet laid
				// out keeps from the last.
				if (source.view == "text") value = static_cast<double>(viewport.TextRatio());
				else if (viewport.width <= 0 || viewport.height <= 0) continue;
				else if (source.view == "aspect") value = static_cast<double>(viewport.SafeWidth())/static_cast<double>(viewport.SafeHeight());
				else {
					const float inset = source.view == "safeLeft" ? viewport.safeLeft : source.view == "safeTop" ? viewport.safeTop :
						source.view == "safeRight" ? viewport.safeRight : viewport.safeBottom;
					const float extent = static_cast<float>(source.view == "safeLeft" || source.view == "safeRight" ? viewport.width : viewport.height);
					value = static_cast<double>(std::isfinite(inset) ? std::clamp(inset,0.f,extent) : 0.f)/viewport.DpRatio();
				}
			} else if (!host.ReadCVar(source.cvar,source.type,value)) {
				error = "Unavailable or invalid CVar source '"+source.cvar+"' for state '"+source.id+"'"; break;
			}
			const auto previous=state.Variables().find(source.id);
			if (previous==state.Variables().end() || previous->second!=value) changes.emplace(source.id,std::move(value));
		}
		if (error.empty()) {
			// A large document's state is costly to copy: copy it only when a
			// source changed. Unchanged sources leave the state as it was.
			if (!state.WouldChange(changes,true)) ValidModals(state,motion,error);
			else if (!interaction.HasAuthoredModals()) state.Set(changes,error,true);
			else {
				State candidate = state;
				if (candidate.Set(changes,error,true) && ValidModals(candidate,motion,error)) state = std::move(candidate);
			}
		}
		if (!error.empty() && error != stateError) host.Log(true,error);
		stateError = std::move(error); ApplyControlBindings();
	}
	void Feedback(double seconds) {
		if (std::isfinite(seconds)) time = std::max(time,seconds);
		for (const auto& change : interaction.TakeFeedback()) motion.Play(change.timeline,time);
	}
	// Writes the detail subject's detail to the detail state: the control the
	// pointer is over while the pointer moved last, otherwise the focused
	// control, or the definition's `none` without one that names a detail.
	// True when the state changed (its bindings are then applied).
	bool UpdateDetailSubject() {
		if (!canonical || !canonical->Model().detail) return false;
		const auto& spec = *canonical->Model().detail;
		// A long-pressed button keeps its detail, placed as for focus, while it
		// can still take input.
		if (!detailPin.empty() && !interaction.CanActivate(detailPin)) detailPin.clear();
		const bool byPointer = pointerNavigation && detailPin.empty();
		std::string subject = !detailPin.empty() ? detailPin : byPointer ? (pointerPresent ? interaction.Hovered() : std::string{}) : interaction.Focused();
		const auto* node = subject.empty() ? nullptr : canonical->Model().FindNode(subject);
		if (!node || !node->control || !node->control->detail) subject.clear();
		if (subject != detailSubject || byPointer != detailByPointer) detailPlaced = false;
		detailSubject = subject; detailByPointer = byPointer;
		const StateValues change{{spec.state, subject.empty() ? spec.none : *node->control->detail}};
		if (!state.WouldChange(change)) return false;
		State candidate = state; std::string error;
		if (!candidate.Set(change,error) || !ValidModals(candidate,motion,error)) { host.Log(true,error); return false; }
		state = std::move(candidate); ApplyControlBindings(); ApplyMotion(); return true;
	}
	// Places the detail card beside its subject by the card's measured box
	// (DetailSpec). A hidden card, or none, is not placed. True when it moved.
	bool PlaceDetailCard() {
		if (!canonical || !canonical->Model().detail || canonical->Model().detail->card.empty() || detailSubject.empty()) return false;
		auto* card = CanonicalElement(canonical->Model().detail->card);
		auto* subject = CanonicalElement(detailSubject);
		if (!card || !subject) return false;
		const auto size = card->GetBox().GetSize(Rml::BoxArea::Border);
		if (size.x <= 0 || size.y <= 0) return false;
		auto* parent = card->GetOffsetParent();
		const auto origin = parent ? parent->GetAbsoluteOffset(Rml::BoxArea::Padding) : Rml::Vector2f{};
		const float dp = viewport.DpRatio(), gap = 6*dp, edge = 16*dp;
		// The card keeps 16 dp inside the window's safe area.
		const auto inset = [](float value) { return std::isfinite(value) ? std::max(value,0.f) : 0.f; };
		const float leading = inset(viewport.safeLeft)+edge, top0 = inset(viewport.safeTop)+edge;
		const float trailing = viewport.width-inset(viewport.safeRight)-edge, foot = viewport.height-inset(viewport.safeBottom)-edge;
		float x = 0, y = 0;
		if (detailByPointer) {
			x = pointerX-origin.x > 555*dp ? pointerX+28*dp-size.x : pointerX+gap;
			y = pointerY+gap;
			if (y+size.y > foot) y = pointerY-gap-size.y;
		} else {
			const auto at = subject->GetAbsoluteOffset(Rml::BoxArea::Border);
			const auto box = subject->GetBox().GetSize(Rml::BoxArea::Border);
			x = at.x; y = at.y+box.y+gap;
			if (y+size.y > foot) y = at.y-gap-size.y;
		}
		x = std::clamp(x,leading,std::max(leading,trailing-size.x));
		y = std::clamp(y,top0,std::max(top0,foot-size.y));
		const float left = x-origin.x, top = y-origin.y;
		if (detailPlaced && std::abs(left-detailLeft) < .25f && std::abs(top-detailTop) < .25f) return false;
		detailLeft = left; detailTop = top; detailPlaced = true;
		card->SetProperty(Rml::PropertyId::Left,Rml::Property(left,Rml::Unit::PX));
		card->SetProperty(Rml::PropertyId::Top,Rml::Property(top,Rml::Unit::PX));
		return true;
	}
	Rml::Element* NumberViewport(Rml::Element* element) const {
		const auto* node = element && canonical ? canonical->Model().FindNode(element->GetId()) : nullptr;
		const auto* number = node && node->control ? std::get_if<NumberSpec>(&node->control->widget) : nullptr;
		return number ? CanonicalElement(number->viewport) : nullptr;
	}
	static bool ProjectedBounds(Rml::Element* element, Rml::Element* parent, Rml::Vector2f& minimum, Rml::Vector2f& maximum) {
		Rml::Array<Rml::Vector2f,4> quad;
		if (!element || !element->IsVisible(true) || !Rml::ElementUtilities::GetBorderBoxQuad(quad,element)) return false;
		const auto origin = parent->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(parent->GetClientLeft(),parent->GetClientTop());
		for (size_t i = 0; i < quad.size(); ++i) {
			auto point = quad[i];
			if (!parent->Project(point) || !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
			point -= origin;
			if (i == 0) minimum = maximum = point;
			else { minimum.x = std::min(minimum.x,point.x); minimum.y = std::min(minimum.y,point.y);
				maximum.x = std::max(maximum.x,point.x); maximum.y = std::max(maximum.y,point.y); }
		}
		return true;
	}
	bool FocusBounds(Rml::Element* element, Rml::Element* parent, Rml::Vector2f& minimum, Rml::Vector2f& maximum) const {
		if (!ProjectedBounds(element,parent,minimum,maximum)) return false;
		// A translated label or validation message can make a Number control
		// taller than its scroll body. Reveal the editable value on oversized
		// axes; retain the complete control, including its label, wherever it fits.
		const bool oversizedX = maximum.x-minimum.x > parent->GetClientWidth();
		const bool oversizedY = maximum.y-minimum.y > parent->GetClientHeight();
		if (auto* viewport = NumberViewport(element); viewport && (oversizedX || oversizedY)) {
			Rml::Vector2f low, high;
			if (!ProjectedBounds(viewport,parent,low,high)) return false;
			if (oversizedX) { minimum.x = low.x; maximum.x = high.x; }
			if (oversizedY) { minimum.y = low.y; maximum.y = high.y; }
		}
		return true;
	}
	// PageUp and PageDown move focus a visible page through the scrolling list
	// that holds the focused button or toggle, Home and End to its first and
	// last row (section 13.5: the triggers page lists). Rows in the focused
	// row's column are preferred. Any other focus, or a list with nowhere to
	// go, leaves the input to the interaction.
	bool PageList(MenuInput input) {
		if (input != MenuInput::PageUp && input != MenuInput::PageDown && input != MenuInput::Home && input != MenuInput::End) return false;
		const auto focused = interaction.Focused();
		const auto role = interaction.Role(focused);
		if (!document || !context || !role || (*role != ControlRole::Button && *role != ControlRole::Toggle)) return false;
		auto* element = CanonicalElement(focused);
		Rml::Element* list = nullptr;
		for (auto* parent = element ? element->GetParentNode() : nullptr; parent && parent != document; parent = parent->GetParentNode()) {
			const auto overflow = parent->GetComputedValues().overflow_y();
			if (overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll) { list = parent; break; }
		}
		Rml::Vector2f low, high;
		if (!list || !ProjectedBounds(element,list,low,high)) return false;
		struct Row { std::string id; float top, left, right; };
		std::vector<Row> rows;
		std::function<void(Rml::Element*)> collect = [&](Rml::Element* node) {
			for (int i = 0; i < node->GetNumChildren(); ++i) {
				auto* child = node->GetChild(i);
				const auto& id = child->GetId();
				const auto row = id.empty() ? std::nullopt : interaction.Role(id);
				Rml::Vector2f top, bottom;
				if (row && (*row == ControlRole::Button || *row == ControlRole::Toggle) && interaction.CanActivate(id) &&
					interaction.AllowsNode(id) && ProjectedBounds(child,list,top,bottom)) rows.push_back({id,top.y,top.x,bottom.x});
				collect(child);
			}
		};
		collect(list);
		const auto column = [&](const Row& row) { return row.left < high.x && row.right > low.x; };
		if (std::any_of(rows.begin(),rows.end(),column)) rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Row& row) { return !column(row); }),rows.end());
		// A page is the rows that fit with the focused one: the farthest row
		// within it, or the next row when even that one does not fit.
		const float page = std::max(0.f,list->GetClientHeight()-(high.y-low.y)), slack = .5f;
		const bool down = input == MenuInput::PageDown || input == MenuInput::End;
		const Row* farthest = nullptr; const Row* nearest = nullptr;
		for (const auto& row : rows) {
			const float distance = down ? row.top-low.y : low.y-row.top;
			if (distance <= slack) continue;
			const bool paged = input == MenuInput::Home || input == MenuInput::End || distance <= page+slack;
			if (paged && (!farthest || (down ? row.top > farthest->top : row.top < farthest->top))) farthest = &row;
			if (!nearest || (down ? row.top < nearest->top : row.top > nearest->top)) nearest = &row;
		}
		const Row* target = farthest ? farthest : nearest;
		return target && interaction.Focus(target->id);
	}
	// The detail area the right stick scrolls (section 13.5): the first
	// scrolling container, in document order, that shows, that input may reach,
	// that has content past its edge and that holds no control, such as the
	// pause's objectives. A list of controls scrolls with its focus instead.
	Rml::Element* DetailArea() {
		if (!document) return nullptr;
		std::function<bool(Rml::Element*)> holdsControl = [&](Rml::Element* node) {
			for (int i = 0; i < node->GetNumChildren(); ++i) {
				auto* child = node->GetChild(i);
				const auto& id = child->GetId();
				if ((!id.empty() && std::find(controls.begin(),controls.end(),id) != controls.end()) || holdsControl(child)) return true;
			}
			return false;
		};
		Rml::Element* found = nullptr;
		std::function<void(Rml::Element*)> visit = [&](Rml::Element* node) {
			for (int i = 0; i < node->GetNumChildren() && !found; ++i) {
				auto* child = node->GetChild(i);
				if (!child->IsVisible()) continue;
				const auto overflow = child->GetComputedValues().overflow_y();
				if ((overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll) && !child->GetId().empty() &&
					interaction.AllowsNode(child->GetId()) && child->GetScrollHeight() > child->GetClientHeight()+.5f && !holdsControl(child)) {
					found = child; return;
				}
				visit(child);
			}
		};
		visit(document);
		return found;
	}
	std::vector<Rml::Vector2f> FocusLayout() const {
		std::vector<Rml::Vector2f> result;
		if (!document || interaction.Focused().empty()) return result;
		auto* target = CanonicalElement(interaction.Focused());
		if (auto* viewport = NumberViewport(target)) target = viewport;
		for (auto* element = target; element && element != document; element = element->GetParentNode()) {
			// Relative layout and client extents exclude scroll offsets. A user
			// can deliberately scroll away without an unchanged focus undoing it.
			result.push_back(element->GetRelativeOffset(Rml::BoxArea::Border));
			result.push_back(element->GetBox().GetSize(Rml::BoxArea::Border));
			result.push_back({element->GetClientWidth(),element->GetClientHeight()});
		}
		return result;
	}
	bool FocusVisible() const {
		if (!document || interaction.Focused().empty()) return false;
		auto* element = CanonicalElement(interaction.Focused());
		Rml::Array<Rml::Vector2f,4> quad;
		if (!element || !element->IsVisible(true) || !Rml::ElementUtilities::GetBorderBoxQuad(quad,element)) return false;
		for (auto* parent = element->GetParentNode(); parent; parent = parent->GetParentNode()) {
			const auto& style = parent->GetComputedValues();
			using Rml::Style::Overflow;
			const bool x = style.overflow_x() != Overflow::Visible && style.overflow_x() != Overflow::Hidden;
			const bool y = style.overflow_y() != Overflow::Visible && style.overflow_y() != Overflow::Hidden;
			if (!x && !y) continue;
			Rml::Vector2f low, high;
			if (!FocusBounds(element,parent,low,high)) return false;
			if ((x && (low.x < -.5f || high.x > parent->GetClientWidth()+.5f)) ||
				(y && (low.y < -.5f || high.y > parent->GetClientHeight()+.5f))) return false;
		}
		return true;
	}
	void RememberFocusLayout() {
		focusLayoutId=interaction.Focused();focusLayout=FocusLayout();focusLayoutVisible=FocusVisible();
	}
	bool RevealFocus(bool preserveAuthoredAxes = false) {
		if (!document || !context || interaction.Focused().empty()) return false;
		auto* element = CanonicalElement(interaction.Focused());
		if (!element) return false;
		const float margin = 4.f * viewport.DpRatio();
		if (!std::isfinite(margin) || margin <= 0) return false;
		ContextClock clock(*services,time,strictPreparation?&preparationFailed:nullptr);
		context->Update();
		context->GetRootElement()->UpdateGeometryForProjection();
		// Measure exact corners in each ancestor's own scroll plane. A window
		// axis bounding box would overestimate rotated controls and scrollers.
		bool changed = false;
		for (auto* parent = element->GetParentNode(); parent; parent = parent->GetParentNode()) {
			const auto& style = parent->GetComputedValues();
			using Rml::Style::Overflow;
			const bool scrollX = !(preserveAuthoredAxes && scrollView.OwnsAxis(parent,false)) && style.overflow_x() != Overflow::Visible && style.overflow_x() != Overflow::Hidden &&
				parent->GetScrollWidth() > parent->GetClientWidth();
			const bool scrollY = !(preserveAuthoredAxes && scrollView.OwnsAxis(parent,true)) && style.overflow_y() != Overflow::Visible && style.overflow_y() != Overflow::Hidden &&
				parent->GetScrollHeight() > parent->GetClientHeight();
			if (!scrollX && !scrollY) continue;
			Rml::Vector2f minimum, maximum;
			if (!FocusBounds(element,parent,minimum,maximum)) break;
			const auto delta = [margin](float low, float high, float client) {
				if (!std::isfinite(client) || client <= 0) return 0.f;
				// Reduce the inset for a nearly full-size control. An oversized
				// control keeps nearest-edge behavior instead of oscillating.
				const float inset = std::min(margin,std::max(0.f,(client-(high-low))*.5f));
				const float start = low-inset, end = high-(client-inset);
				const float correction = start < 0 && end < 0 ? std::max(start,end) :
					start > 0 && end > 0 ? std::min(start,end) : 0.f;
				// Rml scroll offsets are integral layout pixels. Round outward so
				// the focus ink retains its inset after that pixel quantization.
				return correction > .01f ? std::ceil(correction) : correction < -.01f ? std::floor(correction) : 0.f;
			};
			const float oldX = parent->GetScrollLeft(), oldY = parent->GetScrollTop();
			if (scrollX) parent->SetScrollLeft(oldX + delta(minimum.x,maximum.x,parent->GetClientWidth()));
			if (scrollY) parent->SetScrollTop(oldY + delta(minimum.y,maximum.y,parent->GetClientHeight()));
			if (oldX != parent->GetScrollLeft() || oldY != parent->GetScrollTop()) {
				// Setters clamp to authored overflow; never manufacture scroll range.
				changed = true;
				context->Update();
				context->GetRootElement()->UpdateGeometryForProjection();
			}
		}
		RememberFocusLayout();
		return changed;
	}
	// Touch thresholds (section 13.6), in dp and seconds: the slop a tap
	// keeps, the travel that makes a swipe, the rest that makes a long-press
	// and the span the release velocity is measured over.
	static constexpr float TouchSlop = 8, SwipeTravel = 48;
	static constexpr double LongPressSeconds = .5, VelocityWindow = .1;
	// Inertia: a drag released faster than 50 dp a second keeps going, at
	// most 8000, slowing exponentially over 325 ms until under 20.
	static constexpr double FlingMinimum = 50, FlingMaximum = 8000, FlingStop = 20, FlingDecay = .325;
	// The rim light reaches 19 u (28.5 dp) in from the list's edge. A 64 dp
	// pull past the end, or a fling arriving at 2000 dp a second, lights it
	// fully; let go, it fades over 250 ms.
	static constexpr float RimReach = 28.5f, RimPull = 64, RimSpeed = 2000;
	static constexpr double RimFade = .25;
	// A touch press (section 13.6): where and when it landed, the scrolling
	// container whose content it may drag, whether it is on a tab strip and
	// the button whose detail a long-press opens, unless the press captured a
	// value control (a slider, a scrollbar) or a choice popup is open, whose
	// own gestures own the finger.
	void BeginTouch() {
		touch = {};
		if (!touchPointer || !interaction.CapturedPointerControl().empty()) return;
		for (const auto& id : controls) if (const auto widget = interaction.Widget(id); widget && widget->popupOpen) return;
		touch.pressed = true; touch.x = pointerX; touch.y = pointerY; touch.at = time;
		touch.samples.emplace_back(time,pointerY);
		const auto pressed = interaction.Hovered();
		const auto* node = pressed.empty() || !canonical ? nullptr : canonical->Model().FindNode(pressed);
		if (node && node->control && node->control->role == ControlRole::Button) {
			const auto& control = *node->control;
			if (control.detail && canonical->Model().detail) touch.detail = pressed;
			// A tab strip's tabs run its `tab_` programs, and its ends the
			// previous and next tab.
			const auto& events = canonical->Model().events;
			touch.strip = (control.event.starts_with("tab_") || control.event == "ontabprevious" || control.event == "ontabnext") &&
				(events.contains("ontabprevious") || events.contains("ontabnext"));
		}
		auto* hit = HitElement();
		auto* scroll = hit ? hit->GetClosestScrollableContainer() : nullptr;
		if (!scroll || scroll->GetId().empty() || !interaction.AllowsNode(scroll->GetId()) ||
			scroll->GetScrollHeight() <= scroll->GetClientHeight()) return;
		touch.offset = scroll->GetScrollTop(); touch.container = scroll->GetId();
	}
	// The finger's heights over the velocity span (at least the latest), and
	// its velocity over them in pixels a second.
	void SampleTouch() {
		auto& samples = touch.samples;
		samples.emplace_back(time,pointerY);
		while (samples.size() > 1 && samples.front().first < time-VelocityWindow) samples.erase(samples.begin());
	}
	double TouchVelocity() const {
		const auto& samples = touch.samples;
		const double span = samples.size() > 1 ? samples.back().first-samples.front().first : 0;
		return span > 1e-3 ? (samples.back().second-samples.front().second)/span : 0;
	}
	// A touch that moves past 8 dp is no longer a tap (INP-002): the arm is
	// cancelled, so the release activates nothing. Moved more up or down than
	// across in a scrolling container, it drags the content with the finger;
	// past either end the content stays and that end's rim light brightens
	// with the pull. Moved more across on a tab strip, it is a swipe, which
	// the release decides.
	void MoveTouch() {
		if (!touch.pressed) return;
		if (!pointerPresent) { EndTouch(); return; }
		SampleTouch();
		const float dp = viewport.DpRatio(), dx = pointerX-touch.x, dy = pointerY-touch.y;
		if (!touch.moved) {
			if (LongPress()) return;
			if (std::max(std::abs(dx),std::abs(dy)) <= TouchSlop*dp) return;
			interaction.Cancel(); touch.moved = true;
			touch.dragging = !touch.container.empty() && std::abs(dy) >= std::abs(dx);
			touch.swiping = touch.strip && std::abs(dx) > std::abs(dy);
			if (!touch.dragging && !touch.swiping) { touch = {}; return; }
		}
		if (!touch.dragging) return;
		auto* scroll = CanonicalElement(touch.container);
		if (!scroll || !interaction.AllowsNode(touch.container)) { EndTouch(); return; }
		const float wanted = touch.offset-dy, range = std::max(0.f,scroll->GetScrollHeight()-scroll->GetClientHeight());
		scroll->SetScrollTop(wanted);
		const float pull = wanted < 0 ? -wanted : wanted > range ? wanted-range : 0;
		if (pull > 0) rim = {touch.container,wanted > range,false,std::min(1.f,pull/(RimPull*dp)),0,0};
		else if (rim.container == touch.container && !rim.fading) rim = {};
		focusLayoutVisible = false; presentationDirty = true;
	}
	// A lifted finger: a resting one long-presses; a swipe 48 dp across a tab
	// strip asks for the previous or next tab (a finger moving toward the
	// leading edge brings the next one in); a drag keeps its release velocity
	// unless it holds its content against that end.
	void ReleaseTouch() {
		if (!touch.pressed) return;
		SampleTouch();
		if (!touch.moved && LongPress()) return;
		const float dp = viewport.DpRatio(), dx = pointerX-touch.x, dy = pointerY-touch.y;
		if (touch.swiping && pointerPresent && std::abs(dx) >= SwipeTravel*dp && std::abs(dx) > std::abs(dy) && gestureEvents.size() < 16 &&
			canonical->Model().events.contains(dx < 0 ? "ontabnext" : "ontabprevious")) gestureEvents.push_back(dx < 0 ? "onTabNext" : "onTabPrevious");
		auto* scroll = touch.dragging ? CanonicalElement(touch.container) : nullptr;
		if (scroll && interaction.AllowsNode(touch.container)) {
			const double velocity = std::clamp(-TouchVelocity(),-FlingMaximum*dp,FlingMaximum*dp);
			const double offset = scroll->GetScrollTop(), range = std::max(0.f,scroll->GetScrollHeight()-scroll->GetClientHeight());
			if (std::abs(velocity) >= FlingMinimum*dp && (velocity > 0 ? offset < range : offset > 0))
				fling = {touch.container,interaction.Focused(),interaction.Modal(),offset,velocity,time};
		}
		EndTouch();
	}
	void EndTouch() { touch = {}; FadeRim(); }
	// A finger resting 500 ms on a button that names a detail opens that
	// detail: the press is cancelled, so its release activates nothing, and
	// the card stays beside the button after the finger lifts.
	bool LongPress() {
		if (touch.detail.empty() || time-touch.at < LongPressSeconds || !interaction.CanActivate(touch.detail)) return false;
		detailPin = touch.detail; touch = {};
		interaction.Cancel(); presentationDirty = true;
		return true;
	}
	// Inertia, advanced each frame: the content keeps the release velocity
	// as it decays. A new press, a focus move or a modal stops it; reaching
	// an end stops it there and lights that end's rim by the speed it arrived
	// with.
	void AdvanceFling() {
		if (fling.container.empty()) return;
		auto* scroll = CanonicalElement(fling.container);
		if (!scroll || interaction.Focused() != fling.focus || interaction.Modal() != fling.modal || !interaction.AllowsNode(fling.container)) { fling = {}; return; }
		const double elapsed = time-fling.at;
		if (elapsed <= 0) return;
		const double decay = std::exp(-elapsed/FlingDecay), range = std::max(0.f,scroll->GetScrollHeight()-scroll->GetClientHeight());
		const double offset = fling.offset+fling.velocity*FlingDecay*(1-decay);
		fling.velocity *= decay; fling.at = time;
		scroll->SetScrollTop(static_cast<float>(std::clamp(offset,0.0,range)));
		focusLayoutVisible = false; presentationDirty = true;
		const float dp = viewport.DpRatio();
		if (offset < 0 || offset > range) {
			const float strength = static_cast<float>(std::min(1.0,std::abs(fling.velocity)/(RimSpeed*dp)));
			rim = {fling.container,offset > range,true,strength,strength,time};
			fling = {};
		} else if (std::abs(fling.velocity) < FlingStop*dp) fling = {};
		else fling.offset = offset;
	}
	void FadeRim() {
		if (!rim.container.empty() && !rim.fading) { rim.fading = true; rim.from = rim.strength; rim.fadeAt = time; }
	}
	// The rim light's artwork (marine.rim): #626934 falling linearly to
	// nothing across its height from the list's edge, at twice the framing
	// bands' 0.37 when fully lit.
	static Node RimNode(bool end) {
		Node node; node.id = "q4-touch-rim"; node.type = "vector";
		const auto corner = [](double x, double y) { PathPoint point; point.x.fraction = x; point.y.fraction = y; return point; };
		VectorPath path; path.id = "rim"; path.antialias = false;
		for (const auto& point : {corner(0,0),corner(1,0),corner(1,1),corner(0,1)}) {
			PathCommand command; command.op = path.commands.empty() ? PathOperation::Move : PathOperation::Line;
			command.points[0] = point; path.commands.push_back(command);
		}
		PathCommand close; close.op = PathOperation::Close; path.commands.push_back(close);
		const VectorColour lit{98/255.,105/255.,52/255.,.74}, dark{98/255.,105/255.,52/255.,0};
		path.fill.type = PaintType::Linear; path.fill.from = corner(0,0); path.fill.to = corner(0,1);
		path.fill.stops = {{0,end ? dark : lit},{1,end ? lit : dark}};
		node.paths.push_back(std::move(path));
		return node;
	}
	// Fades the rim light and lays its element over the list's end: the
	// list's client width, 19 u deep (at most half the list), at the rim's
	// strength. The element takes no input.
	struct RimPlacement { float left = 0, top = 0, width = 0, height = 0, opacity = 0; bool operator==(const RimPlacement&) const = default; } rimPlaced;
	void AdvanceRim() {
		if (rim.fading) rim.strength = rim.from*static_cast<float>(std::max(0.0,1-(time-rim.fadeAt)/RimFade));
		auto* scroll = rim.container.empty() ? nullptr : CanonicalElement(rim.container);
		if (rim.strength <= 0 || !scroll || !scroll->IsVisible(true) || !interaction.AllowsNode(rim.container)) rim = {};
		RimPlacement placement;
		if (!rim.container.empty()) {
			const auto origin = document->GetAbsoluteOffset(Rml::BoxArea::Padding), at = scroll->GetAbsoluteOffset(Rml::BoxArea::Padding);
			const float height = std::min(RimReach*viewport.DpRatio(),scroll->GetClientHeight()*.5f);
			placement = {at.x-origin.x,at.y-origin.y+(rim.end ? scroll->GetClientHeight()-height : 0),scroll->GetClientWidth(),height,rim.strength};
		}
		if (placement == rimPlaced && (rim.container.empty() || rimElementEnd == rim.end)) return;
		if (!rimElement) {
			auto element = Rml::Factory::InstanceElement(document,"q4-vector","q4-vector",Rml::XMLAttributes());
			if (!element) return;
			rimElement = document->AppendChild(std::move(element));
			rimElement->SetId("q4-touch-rim");
			rimElement->SetProperty("position","absolute"); rimElement->SetProperty("pointer-events","none");
			auto* vector = static_cast<VectorElement*>(rimElement);
			vector->Configure(RimNode(rim.end),host,Stats());
			if (backend) { vector->SetImageSamplingInterface(&backend->renderer); vector->SetLinearMaskInterface(&backend->renderer); }
			rimElementEnd = rim.end;
		} else if (!rim.container.empty() && rimElementEnd != rim.end) {
			static_cast<VectorElement*>(rimElement)->Configure(RimNode(rim.end),host,Stats());
			rimElementEnd = rim.end;
		}
		rimPlaced = placement;
		rimElement->SetProperty("display",placement.opacity > 0 ? "block" : "none");
		rimElement->SetProperty(Rml::PropertyId::Left,Rml::Property(placement.left,Rml::Unit::PX));
		rimElement->SetProperty(Rml::PropertyId::Top,Rml::Property(placement.top,Rml::Unit::PX));
		rimElement->SetProperty(Rml::PropertyId::Width,Rml::Property(placement.width,Rml::Unit::PX));
		rimElement->SetProperty(Rml::PropertyId::Height,Rml::Property(placement.height,Rml::Unit::PX));
		rimElement->SetProperty(Rml::PropertyId::Opacity,Rml::Property(placement.opacity,Rml::Unit::NUMBER));
		presentationDirty = true;
	}
	// Frame work of touch gestures: a resting finger's long-press, inertia
	// and the rim light.
	void AdvanceTouch() {
		if (!document || !canonical) return;
		if (touch.pressed && !touch.moved) LongPress();
		AdvanceFling(); AdvanceRim();
	}
	// Stops every touch transient: the gesture, inertia, a long-pressed
	// detail and the rim light, whose element the next frame hides.
	void ResetTouch() { touch = {}; fling = {}; rim = {}; detailPin.clear(); gestureEvents.clear(); }
	Rml::Element* HitElement() const {
		if (!pointerPresent || !context || !document || pointerX < 0 || pointerY < 0 || pointerX >= viewport.width || pointerY >= viewport.height) return nullptr;
		const Rml::Vector2f point(pointerX,pointerY);
		std::map<Rml::Element*,bool> checked;
		const auto allowed = [&](Rml::Element* element) {
			for (auto* ancestor = element; ancestor && ancestor != document; ancestor = ancestor->GetParentNode()) {
				if (ancestor->GetTagName() != "q4-node" && ancestor->GetTagName() != "q4-vector") continue;
				// Popup parts have live visibility owned by the widget view; their
				// canonical closed-state display value is not the current layout.
				// Owning controls still enforce semantic eligibility at dispatch.
				if (!ancestor->IsVisible() || ancestor->GetComputedValues().pointer_events() == Rml::Style::PointerEvents::None) return false;
				auto [entry,inserted] = checked.emplace(ancestor,false);
				if (inserted) entry->second = static_cast<VectorElement*>(ancestor)->AllowsMaskedPoint(point);
				if (!entry->second) return false;
			}
			return true;
		};
		return context->GetElementAtPoint(point,nullptr,document,allowed);
	}
	std::string HitControl(Rml::Element* element) const {
		if (!canonical) return {};
		for (; element && element != document; element = element->GetParentNode()) {
			const auto* node = canonical->Model().FindNode(element->GetId());
			if (node && node->control) {
				// Decorative/translated child ink never enlarges the button's
				// stable hit box, including during pressed-state movement.
				Rml::Vector2f local(pointerX,pointerY);
				const auto allowed = inputAllowed.find(node->id);
				return allowed != inputAllowed.end() && allowed->second && element->Project(local) && element->IsPointWithinElement(local) ? node->id : std::string{};
			}
		}
		return {};
	}
	void ApplyScrollCommands() {
        if(!context)return;
        ContextClock clock(*services,time,strictPreparation?&preparationFailed:nullptr);
        if(!scrollView.ApplyCommands(interaction))return;
        context->Update();context->GetRootElement()->UpdateGeometryForProjection();
        std::string error;scrollLayoutDirty|=scrollView.Sync(interaction,viewport.DpRatio(),false,error);
        if(!error.empty())host.Log(true,error);
    }
    bool UpdateInteraction(double seconds = -1, bool freshLayout = false, bool refreshPopup = false) {
		if (!document || viewport.width<=0 || viewport.height<=0) return false;
        if (!canonical) return true; // Valid legacy RML has no canonical interaction work.
		++Stats().interactionUpdates;
		if (!freshLayout) interactionLayoutDirty=true;
		if (std::isfinite(seconds) && seconds >= 0) time = std::max(time,seconds);
        const auto owner=ownerCanvas;
        const auto stamp=owner->identity;
        const bool refreshing=refreshPopup && !freshLayout && valueView.HasConstrainedPopup(interaction);
        if(refreshing && (owner->preparing || owner->retiring || !owner->alive))return false;
        struct Scope {std::shared_ptr<Canvas> owner;bool active;~Scope(){if(active)owner->preparing=false;}} scope{owner,refreshing};
        if(refreshing) {
            owner->preparing=true;
            try {
                ContextClock clock(*services,time,strictPreparation?&preparationFailed:nullptr);
                if(!ApplyMotion(&stamp))return false;
                document->UpdateDocument();
                if(!owner->alive || owner->retiring || owner->identity!=stamp)return false;
                context->GetRootElement()->UpdateGeometryForProjection();
            }catch(...){return false;}
            if(!owner->alive || owner->retiring || owner->identity!=stamp)return false;
        }
		SyncModals();
		// A presentation-only document (a HUD) has nothing to focus, scroll or hit.
		if (controls.empty() && !scrollView.HasScrollbars() && !interaction.HasAuthoredModals()) {
			inputAllowed.clear(); interaction.SetBounds({},freshLayout); Feedback(time);
			if (freshLayout) interactionLayoutDirty=false;
			return true;
		}
		inputAllowed.clear(); CollectInputEligibility(inputBranches);
        // Publish scrollbar metrics and projected bounds as one eligibility update.
        // Restored focus must never be tested against this instance's old bounds.
        std::string scrollError;scrollLayoutDirty|=scrollView.Sync(interaction,viewport.DpRatio(),freshLayout,scrollError,true);
        if(!scrollError.empty())host.Log(true,scrollError);
        if(refreshing && (!owner->alive || owner->retiring || owner->identity!=stamp))return false;
		std::map<std::string,ControlBounds> bounds;
		for (const auto& id : controls) {
			auto* element = IndexedElement(id); Rml::Rectanglef rect;
			if (element && inputAllowed.at(id) && element->IsVisible(true) && Rml::ElementUtilities::GetBoundingBox(rect,element,Rml::BoxArea::Border))
				bounds[id] = {rect.Left(),rect.Top(),rect.Width(),rect.Height(),true};
		}
		interaction.SetBounds(bounds,freshLayout);
        valueView.ValidatePlacement(interaction,viewport.width,viewport.height,viewport.DpRatio(),refreshPopup && !freshLayout);
		// A finger dragging content or swiping a tab strip hovers nothing.
		auto* element = pointerNavigation && !touch.dragging && !touch.swiping ? HitElement() : nullptr;
		const auto hit = HitControl(element);
		const auto scrollbar=scrollView.PointerPart(element,pointerX,pointerY,interaction);
        if(!scrollbar.control.empty()) interaction.PointerPart(scrollbar.control,scrollbar.invalidProjection?std::nullopt:scrollbar.fraction,{},scrollbar.thumb);
        else {
            const auto part = valueView.PointerPart(element,pointerX,pointerY,interaction);
            if (part.invalidProjection) interaction.Cancel();
            else if(!part.pendingLayout) interaction.PointerPart(part.control.empty() ? (pointerNavigation ? hit : std::string{}) : part.control,part.fraction,part.option,part.thumb,part.choiceTrack);
        }
        ApplyScrollCommands();
		Feedback(time);
        const bool current=!refreshing || (owner->alive && !owner->retiring && owner->identity==stamp);
		if (current && freshLayout) interactionLayoutDirty=false;
		return current;
	}
	bool ApplyMotion(const RuntimeCanvasIdentity* expected=nullptr) {
        const auto current=[&]{return !expected || (ownerCanvas->alive && !ownerCanvas->retiring && ownerCanvas->identity==*expected);};
        if (!document || !canonical || !current()) return false;
		const bool freshMarkup = inlineAuthored; inlineAuthored = false;
		const float textRatio=viewport.TextRatio();
		const bool flushDeferred = presentationDirty && !deferredInputs.empty();
		if (!flushDeferred && !documentFontCss.empty() && appliedInputTextRatio==textRatio && appliedInputRevision==state.Revision() &&
			appliedMotionRevision && appliedMotionRevision==motion.Revision() && appliedInputs.size()>=motion.Values().size()) return current();
		// The inherited document default participates once. Explicit absolute
		// typography below replaces it; em values use the resulting parent/font
		// metrics and must never receive this multiplier a second time.
		if (appliedInputTextRatio!=textRatio || documentFontCss.empty()) {
			Value defaultFont; defaultFont.type=ValueType::Length; defaultFont.unit="dp";
			defaultFont.data[0]=16*textRatio;
			const auto defaultCss=defaultFont.Css();
			if (documentFontCss!=defaultCss) {
				document->SetProperty("font-size",defaultCss); documentFontCss=defaultCss;
				presentationDirty=true;
			}
		}
		// Motion holds every authored property, nearly all static. An input
		// already applied under this text ratio formats to the CSS already
		// applied, so only a changed one is formatted again; a new state
		// revision can change only the properties bindings own. Text is
		// translated every time and a backdrop pairs two values.
		if (appliedInputTextRatio != textRatio) {
			appliedInputs.clear(); appliedInputTextRatio = textRatio;
		}
		const bool stateChanged = appliedInputRevision != state.Revision();
		appliedInputRevision = state.Revision();
		// A pass reads the motion values, the state bindings own, the text
		// ratio and the language, which a reload replaces with the document.
		// While none has changed since a complete pass, there is nothing to
		// apply: a settled screen walks none of its authored values.
		std::vector<PropertyKey> changedKeys;
		const bool partial = motion.TakeChanged(changedKeys);
		if (!flushDeferred && !stateChanged && appliedInputs.size() >= motion.Values().size() && appliedMotionRevision != 0 &&
			appliedMotionRevision == motion.Revision()) return current();
		const auto passRevision = motion.Revision();
		// The incremental pass serves frames. An input-time refresh (a
		// constrained popup is open) stays a complete pass, translating its
		// text again through the host it validates the popup against.
		const bool incremental = !expected && partial && appliedMotionRevision != 0 &&
			appliedInputs.size() >= motion.Values().size() && InputsIndexed();
		if (incremental && stateChanged && !state.TakeChangedProperties(changedKeys))
			for (const auto& [key,value] : state.Properties()) changedKeys.push_back(key);
		// A display transition flushes pending inline styles before RmlUi resolves
		// the newly visible tree. In between, invisible animation still advances
		// in Motion without repeatedly parsing filters/transforms nobody can see.
		const bool deferHidden=incremental && !presentationDirty && controls.empty() && !scrollView.HasScrollbars() &&
			!interaction.HasAuthoredModals() && !pointerPresent &&
			std::none_of(changedKeys.begin(),changedKeys.end(),[](const PropertyKey& key){return key.second=="display";});
		if (!deferHidden) {
			changedKeys.insert(changedKeys.end(),deferredInputs.begin(),deferredInputs.end());
			deferredInputs.clear();
		}
		appliedMotionRevision = 0; ++presentationPass;
		bool complete = true;
		const auto sameValue = [](const Value& a, const Value& b) {
			return a.type == b.type && std::memcmp(a.data.data(),b.data.data(),sizeof(a.data)) == 0 && a.unit == b.unit && a.text == b.text;
		};
		// One input: formats it only when it changed, then applies it.
		// `authored`: the value's authored one while the load's inline values
		// are fresh. `keyPointer`: the key in the authored map. `boundAt`: the
		// key's bound property, or end, when the caller read the state in step.
		const auto apply = [&](AppliedInput& input, const PropertyKey& key, const Value& animated, const Value* authored,
			const PropertyKey* keyPointer, std::optional<PropertyValues::const_iterator> boundAt) -> bool {
			const bool sameKey = input.key && *input.key == key;
			const bool backdrop = key.second.starts_with("backdrop-") &&
				(key.second == "backdrop-blur" || key.second == "backdrop-saturate" || key.second == "backdrop-streak");
			if (!backdrop && animated.type != ValueType::Text && sameKey && !(stateChanged && input.bound) && input.value.type == animated.type &&
				std::memcmp(input.value.data.data(),animated.data.data(),sizeof(animated.data)) == 0 &&
				input.value.unit == animated.unit && input.value.text == animated.text) return true;
			const auto bound = boundAt ? *boundAt : !stateChanged && sameKey && !input.bound ? state.Properties().end() : state.Properties().find(key);
			const bool owned = bound != state.Properties().end();
			const auto& value = owned ? bound->second : animated;
			if (deferHidden) {
				auto* element=input.element ? input.element : CanonicalElement(key.first);
				// A hidden animated property may never have needed an inline
				// write. Keep its resolved element even while deferring the value,
				// otherwise every sample searches the entire document again.
				input.element=element;
				for (auto* ancestor=element;ancestor;ancestor=ancestor->GetParentNode())
					if (ancestor->GetComputedValues().display()==Rml::Style::Display::None) {
						if (!input.deferred) deferredInputs.push_back(key);
						input.deferred=true;
						input.key=nullptr; // A deferred bound value has not reached its element yet.
						return current();
					}
			}
			const bool image=key.second.starts_with("image-");
			const bool coordinates=image && (key.second == "image-offset-x" || key.second == "image-offset-y" || key.second == "image-scale-x" || key.second == "image-scale-y");
			if (coordinates) {
				auto* element=input.element ? input.element : CanonicalElement(key.first);
				if (!element) { complete=false; return true; }
				static_cast<VectorElement*>(element)->SetImageCoordinate(key.second,value.data[0]);
				input.Set(keyPointer,animated,owned,element); return current();
			}
			const bool opacity = key.second == "opacity";
			// A decorative leaf beside canonical buttons can replay its isolated
			// fade. Owned widgets, interactive ancestry and compound groups keep
			// their ordinary style/layout update and filter preparation; widgets
			// in a closed dialog are not shown, as frame replay also finds.
			auto* opacityElement=opacity ? (input.element ? input.element : CanonicalElement(key.first)) : nullptr;
			const bool paintOpacity=opacityElement && incremental && !presentationDirty &&
				!scrollView.Affects(opacityElement) && ((controls.empty() && !interaction.HasAuthoredModals() && !pointerPresent) ||
				(!interactionAncestors.count(opacityElement) && static_cast<VectorElement*>(opacityElement)->ReplayOpacity() &&
				 std::none_of(layoutWidgets.begin(),layoutWidgets.end(),[](const auto* widget){return widget->IsVisible(true);})));
			if (opacity) {
				auto* element=opacityElement;
				if (!element) { complete=false; return true; }
				const bool reusable=static_cast<VectorElement*>(element)->UpdateReplayOpacity(float(value.data[0]));
				if (paintOpacity && reusable) {
					// The compiled leaf filter already reads the live value. Keep
					// 'applied' as the last CSS actually installed: formatting and
					// looking up a new CSS string cannot change this cached draw.
					// Zero/full-opacity crossings still install the new topology.
					if (input.cssAuthored) { input.css = input.AuthoredCss(); input.cssAuthored = false; }
					input.Set(keyPointer,animated,owned,element); return current();
				}
			}
			// Decorative transforms affect projection, not box layout. Resolve
			// the typed value directly; Render updates inherited matrices in
			// parent order. Controls, modals and scrolling areas retain full
			// layout; a scrollbar elsewhere in the document (a closed dialog's)
			// does not hold back decoration that cannot reach it.
			if (incremental && !presentationDirty && key.second=="transform" &&
				value.type==ValueType::Transform) {
				auto* element=input.element ? input.element : CanonicalElement(key.first);
				if (element && !interactionAncestors.count(element) && !scrollView.Affects(element)) {
					if (!static_cast<VectorElement*>(element)->SetPresentationTransform(value,viewport.DpRatio(),
						{float(viewport.width),float(viewport.height)})) {
						host.Log(true,"Canonical transform rejected: "+key.first); paintDirty=true;
					}
					// An ordinary pass must not mistake the old CSS for the
					// current property after a typed update or a timeline rewind.
					input.cssApplied=false; input.cssAuthored=false; input.Set(keyPointer,animated,owned,element);
					auto* projected=static_cast<VectorElement*>(element);
					if (std::find(changedProjections.begin(),changedProjections.end(),projected)==changedProjections.end())
						changedProjections.push_back(projected);
					return current();
				}
			}
			std::string string;
			// Whether the load's fresh inline value already is this string, and
			// whether its record then needs no formatting at all (cssAuthored).
			bool carried = false, lazy = false;
			if (value.type==ValueType::Length && (value.unit=="dp" || value.unit=="px") && (key.second[0]=='f' || key.second[0]=='l') &&
				(key.second=="font-size" || key.second=="line-height" || key.second=="letter-spacing")) {
				auto scaled=value; scaled.data[0]*=viewport.TextRatio(); string=scaled.Css();
			} else if (backdrop) {
				// One backdrop-filter carries both: compose the node's current pair.
				const auto present = [&](const char* property) -> std::optional<Value> {
					const PropertyKey other{key.first,property};
					if (const auto found = state.Properties().find(other); found != state.Properties().end()) return found->second;
					if (const auto found = motion.Values().find(other); found != motion.Values().end()) return found->second;
					return std::nullopt;
				};
				const auto blur = present("backdrop-blur"), saturate = present("backdrop-saturate"), streak = present("backdrop-streak");
				string = BackdropFilterCss(blur ? &*blur : nullptr,saturate ? &*saturate : nullptr,streak ? &*streak : nullptr);
			} else {
				// The load builds an authored value as the pass formats it here,
				// an opacity below one as its filter; text and images it leaves to
				// the pass. A value the load built is recorded as applied without
				// a second parse, and without formatting: its record formats on
				// demand.
				carried = freshMarkup && authored && value.type != ValueType::Text && value.type != ValueType::Image &&
					(!opacity || value.data[0] < 1) && sameValue(value,*authored);
				lazy = carried && !input.cssApplied && !inlineOverridden.contains(key.first);
				if (!lazy) string = opacity ? (value.data[0] < 1 ? "opacity("+value.Css()+")" : "none") :
					value.type == ValueType::Text ? host.Translate(value.text) : value.Css();
			}
			if(!current())return false;
			// Fit and alignment are authored once and folded into the decorator.
			if (image && (key.second == "image-fit" || key.second == "image-align-x" || key.second == "image-align-y" || key.second == "image-blend")) {
				if (lazy) { input.Set(keyPointer,animated,owned,input.element); input.css.clear(); input.cssApplied = true; input.cssAuthored = true; return true; }
				input.css = std::move(string); input.cssApplied = true; input.cssAuthored = false; input.Set(keyPointer,animated,owned,input.element); return true;
			}
			// Canonical elements survive property/text edits and owned-widget
			// reparenting. These handles reset with the document or its snapshot.
			auto* element = input.element ? input.element : CanonicalElement(key.first);
			if (!element) { complete = false; return true; }
			if (opacity && lazy && static_cast<VectorElement*>(element)->ReplayOpacity()) { lazy = false; string = "opacity("+value.Css()+")"; }
			if (opacity && !lazy && string!="none" && static_cast<VectorElement*>(element)->ReplayOpacity()) string.insert(0,"q4-");
			if (input.cssApplied && input.InstalledCss(string)) { input.Set(keyPointer,animated,owned,element); return true; }
			// openQ4: the load built this very value; record it as applied
			// instead of parsing it a second time.
			if (lazy) { input.Set(keyPointer,animated,owned,element); input.css.clear(); input.cssApplied = true; input.cssAuthored = true; return true; }
			{
				// A hidden HUD branch can keep advancing its authored timelines
				// without relaying out the visible document. Keep its inline values
				// current; a later display change resolves all pending styles.
				bool hidden=false;
				for (auto* ancestor=element; ancestor; ancestor=ancestor->GetParentNode())
					if (ancestor->GetComputedValues().display()==Rml::Style::Display::None) { hidden=true; break; }
				if ((!hidden || key.second=="display") && !paintOpacity) presentationDirty=true;
			}
			if (paintOpacity) {
				if (!static_cast<VectorElement*>(element)->SetPresentationOpacity(string,viewport.DpRatio(),
					{float(viewport.width),float(viewport.height)})) host.Log(true,"Canonical opacity rejected: "+key.first);
				paintDirty=true;
			} else if (value.type == ValueType::Text) static_cast<VectorElement*>(element)->SetCanonicalText(string);
			else if (value.type == ValueType::Image) {
				const auto* node = canonical->Model().FindNode(key.first);
				if (!static_cast<VectorElement*>(element)->SetImageSource(value.text) &&
					(!node || !element->SetProperty("decorator",ImageDecorator(*node,value.text)))) host.Log(true,"Canonical image rejected: "+key.first);
			}
			else if (!element->SetProperty(opacity ? "filter" : backdrop ? "backdrop-filter" : key.second,string)) host.Log(true,"Canonical property rejected: "+key.first+"."+key.second);
			if(!current())return false;
			input.css = std::move(string); input.cssApplied = true; input.cssAuthored = false; input.Set(keyPointer,animated,owned,element);
			return true;
		};
		if (incremental) {
			// openQ4: after a complete pass, a frame applies only the values
			// motion wrote since and, when the state changed, the properties
			// bindings own: a HUD's running trace or a changed readout walks
			// those, not every authored value.
			// openQ4: a state change reaches only the bound properties whose
			// values changed (all of them after a full evaluation).
			std::sort(changedKeys.begin(),changedKeys.end());
			changedKeys.erase(std::unique(changedKeys.begin(),changedKeys.end()),changedKeys.end());
			for (const auto& key : changedKeys) {
				const size_t index = InputIndex(key);
				if (index == inputKeys.size()) continue;
				auto& input = appliedInputs[index];
				// A deferred property's branch cannot become visible while the
				// presentation and its display bindings remain unchanged. Its
				// timeline still samples in Motion; read the latest value when a
				// later display/layout change flushes the deferred inputs.
				if (deferHidden && !presentationDirty && input.deferred) continue;
				++Stats().motionInputsScanned;
				const auto animated = motion.Values().find(key);
				if (animated == motion.Values().end()) continue;
				if (!apply(input,key,animated->second,nullptr,inputKeys[index],std::nullopt)) return false;
			}
			if (complete) appliedMotionRevision = passRevision;
			return current();
		}
		Stats().motionInputsScanned += motion.Values().size();
		// A complete pass applies every bound value: the change log restarts.
		{ std::vector<PropertyKey> consumed; state.TakeChangedProperties(consumed); }
		if (appliedInputs.size() < motion.Values().size()) appliedInputs.resize(motion.Values().size());
		const bool indexing = !InputsIndexed();
		if (indexing) { inputKeys.clear(); inputKeys.reserve(motion.Values().size()); indexedKeys = &motion.Base(); }
		size_t inputIndex = 0;
		// The authored values keep the live values' key order, so the pass
		// reads each value's authored key (and, on the load's first pass, its
		// authored value) in step, and the bound properties in step too.
		const auto& authoredValues = motion.Base();
		auto authored = authoredValues.begin();
		const auto& boundProperties = state.Properties();
		auto bound = boundProperties.begin();
		for (const auto& [key,animated] : motion.Values()) {
			const PropertyKey* keyPointer = nullptr; const Value* carried = nullptr;
			if (authored != authoredValues.end() && authored->first == key) {
				keyPointer = &authored->first;
				if (freshMarkup) carried = &authored->second;
				++authored;
			}
			// A live key the authored map lacks leaves the inputs unindexed:
			// every pass then stays complete.
			if (indexing) { if (keyPointer) inputKeys.push_back(keyPointer); else indexedKeys = nullptr; }
			while (bound != boundProperties.end() && bound->first < key) ++bound;
			const auto boundAt = bound != boundProperties.end() && bound->first == key ? bound : boundProperties.end();
			if (!apply(appliedInputs[inputIndex++],key,animated,carried,keyPointer,boundAt)) return false;
		}
		if (!indexedKeys) inputKeys.clear();
		inlineOverridden.clear();
		if (complete) appliedMotionRevision = passRevision;
        return current();
	}
};


struct Runtime::PreparedDocument::Data {
    std::weak_ptr<Canvas> owner;
    RuntimeCanvasIdentity expected;
    std::shared_ptr<void> source;
    std::unique_ptr<Runtime> candidate;
    bool ready=true,noOp=false;
};
Runtime::PreparedDocument::PreparedDocument(std::shared_ptr<Data> value):data(std::move(value)){}
Runtime::PreparedDocument::~PreparedDocument(){data->ready=false;}
Runtime::Runtime(Host& host) : canvas(std::make_shared<Canvas>()),impl(std::make_unique<Impl>(host,canvas)) {
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    while(value!=(std::numeric_limits<std::uint64_t>::max)())
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)){canvas->identity={value,1};break;}
}
bool Runtime::Mutate(bool cleanup) noexcept {
    if(canvas->thread!=std::this_thread::get_id() || !canvas->alive)return false;
    auto& revision=canvas->identity.revision;
    if(revision)revision=revision==(std::numeric_limits<std::uint64_t>::max)()?0:revision+1;
    return !canvas->preparing && (cleanup || (!canvas->retiring && canvas->identity.lifetime && revision));
}
RuntimeCanvasIdentity Runtime::CanvasIdentity() const noexcept {
    return canvas->alive && !canvas->retiring && canvas->thread==std::this_thread::get_id()?canvas->identity:RuntimeCanvasIdentity{};
}
RuntimeNativeVacancy Runtime::NativeVacancy() const noexcept {
    if(!canvas->alive || canvas->retiring || canvas->preparing || canvas->thread!=std::this_thread::get_id())return RuntimeNativeVacancy::BusyOrUnknown;
    return impl->interaction.NativeDocumentVacancy();
}
bool Runtime::HasActiveCanvasCallback() const noexcept {
    return canvas->thread!=std::this_thread::get_id() || canvas->preparing;
}
void Runtime::RetireCanvasOwner() noexcept {
    if(canvas->thread!=std::this_thread::get_id())return;
    canvas->retiring=true;Mutate(true);
}
bool Runtime::ObserveNumberNativeCandidate(NumberNativeCandidate& out) const noexcept {
    if(!CanvasIdentity().revision || canvas->preparing || !impl->canonical || !impl->document)return false;
    return impl->interaction.ObserveNumberNativeCandidate(out);
}
bool Runtime::SourceMatches(const std::string& source,const std::string& path) const noexcept {
    return canvas->alive && !canvas->preparing && canvas->thread==std::this_thread::get_id() &&
        impl->canonical && impl->canonical->Source()==source && impl->sourcePath==path;
}
bool Runtime::AbortPreparedDocument() noexcept {
    if(!Mutate(true))return false;
    if(auto pending=canvas->pending.lock()){pending->ready=false;pending->candidate.reset();}
    return true;
}
std::unique_ptr<Runtime::PreparedDocument> Runtime::PrepareDocument(RuntimeCanvasIdentity expected,
    const DocumentEdit::Prepared& source,RuntimeDocumentOptions options,std::vector<Diagnostic>& diagnostics) noexcept {
    const bool matched=expected==CanvasIdentity();
    if(!Mutate() || !matched || !source.OwnerCurrent() || !canvas->pending.expired() || NativeVacancy()!=RuntimeNativeVacancy::Vacant ||
        !impl->canonical || impl->canonical->Source()!=source.Origin().Source())return {};
    const auto origin=CanvasIdentity();canvas->preparing=true;
    struct Guard {Canvas& owner;~Guard(){owner.preparing=false;}} guard{*canvas};
    try {
        if(!std::isfinite(options.seconds) || options.seconds<0 || options.viewport.width<=0 || options.viewport.height<=0 ||
            !std::isfinite(options.viewport.displayScale) || options.viewport.displayScale<=0 ||
            !std::isfinite(options.viewport.userScale) || options.viewport.userScale<=0 ||
            !std::isfinite(options.viewport.textScale) || options.viewport.textScale<=0 ||
            !std::isfinite(options.viewport.fitScale) || options.viewport.fitScale<=0 ||
            !std::isfinite(options.viewport.canvasHeight) || options.viewport.canvasHeight<0 ||
            !std::isfinite(options.viewport.canvasMinWidth) || options.viewport.canvasMinWidth<0 ||
            !std::isfinite(options.viewport.safeLeft) || options.viewport.safeLeft<0 ||
            !std::isfinite(options.viewport.safeTop) || options.viewport.safeTop<0 ||
            !std::isfinite(options.viewport.safeRight) || options.viewport.safeRight<0 ||
            !std::isfinite(options.viewport.safeBottom) || options.viewport.safeBottom<0 ||
            !std::isfinite(options.viewport.pixelDensityX) || options.viewport.pixelDensityX<=0 ||
            !std::isfinite(options.viewport.pixelDensityY) || options.viewport.pixelDensityY<=0 ||
            !std::isfinite(options.viewport.originX) || !std::isfinite(options.viewport.originY))return {};
        auto data=std::make_shared<PreparedDocument::Data>();data->owner=canvas;data->expected=origin;data->source=source.data;
        data->noOp=!source.Receipt().sourceChanged;
        if(!data->noOp) {
            data->candidate=std::make_unique<Runtime>(impl->host);
            auto& candidate=*data->candidate;candidate.impl->strictPreparation=true;
            if(!candidate.LoadDocument(source.Target().Source(),options.sourcePath,diagnostics))return {};
            std::string error;
            if(!candidate.impl->stateError.empty() || !candidate.SetState(options.application,error,options.seconds))return {};
            candidate.SetReducedMotion(options.reducedMotion,options.seconds);
            if(!candidate.Layout(options.viewport,options.seconds) || !candidate.impl->stateError.empty() ||
                candidate.NativeVacancy()!=RuntimeNativeVacancy::Vacant || candidate.impl->preparationFailed)return {};
            candidate.impl->strictPreparation=false;
        }
        if(CanvasIdentity()!=origin || !canvas->alive || impl->interaction.NativeDocumentVacancy()!=RuntimeNativeVacancy::Vacant)return {};
        auto result=std::unique_ptr<PreparedDocument>(new PreparedDocument(data));canvas->pending=data;return result;
    }catch(...){return {};}
}
bool Runtime::CanPublishDocument(const DocumentEdit& history,const DocumentEdit::Prepared& source,const PreparedDocument& prepared) const noexcept {
    const auto& data=*prepared.data;
    return CanvasIdentity().revision && CanvasIdentity().revision!=(std::numeric_limits<std::uint64_t>::max)() &&
        history.CanPublish(source) && data.ready && data.owner.lock()==canvas && canvas->pending.lock()==prepared.data &&
        CanvasIdentity()==data.expected && data.source.get()==source.data.get() &&
        NativeVacancy()==RuntimeNativeVacancy::Vacant && (data.noOp || (data.candidate &&
            data.candidate->NativeVacancy()==RuntimeNativeVacancy::Vacant &&
            data.candidate->impl->canonical->Source()==source.Target().Source()));
}
bool Runtime::PublishDocument(DocumentEdit& history,DocumentEdit::Prepared& source,PreparedDocument& prepared,DocumentEditReceipt& out) noexcept {
    if(!CanPublishDocument(history,source,prepared)){Mutate();return false;}
    auto& data=*prepared.data;
    // Every test and allocation precedes this interval. Old context/history
    // stay owned by the prepared holders until their later explicit cleanup.
    ++canvas->identity.revision;
    history.PublishPrepared(source,out);
    if(!data.noOp) {
        impl.swap(data.candidate->impl);
        impl->ownerCanvas=canvas;data.candidate->impl->ownerCanvas=data.candidate->canvas;
    }
    data.ready=false;return true;
}
Runtime::~Runtime() { Shutdown(); canvas->alive=false; }
bool Runtime::Initialize() {
    if(!Mutate())return false;

	if (impl->initialized) return true;
	auto services = activeServices.lock();
	if (services && &services->host != &impl->host) {
		impl->host.Log(true,"Live retained contexts must share the same host"); return false;
	}
	if (!services) {
		if (!impl->host.shared) impl->host.shared = std::make_unique<Host::Shared>();
		services = std::make_shared<Services>(impl->host,impl->host.shared->layers);
		if (!services->Initialize()) return false;
		activeServices = services;
	}
	impl->services = services;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	impl->backend = impl->services->AcquireBackend();
	impl->contextName = "openq4-retained-"+std::to_string(++nextContext);
	impl->context = Rml::CreateContext(impl->contextName, {1280,720}, &impl->backend->renderer);
	if (!impl->context) {
		impl->services->ReleaseBackend(*impl->backend); impl->backend = nullptr;
		impl->services.reset(); return false;
	}
	impl->initialized = true;
	return true;
}
void Runtime::Shutdown() {
    if(!Mutate(true))return;
    AbortPreparedDocument();

	if (!impl->initialized) return;
	impl->valueView.Reset();
	impl->scrollView.Reset();impl->scrollLayoutDirty=impl->preserveRestoredScroll=false;
	impl->focusLayoutId.clear();impl->focusLayout.clear();impl->focusLayoutVisible=false;
	impl->numberView.Reset();
	{
		ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
		Rml::RemoveContext(impl->contextName);
		impl->services->ReleaseBackend(*impl->backend);
		impl->statistics = impl->backend->statistics;
		impl->backend = nullptr;
	}
	impl->context = nullptr;
	impl->document = nullptr;
	impl->ReleaseModelScenes();
	impl->canonicalElements.clear();
	impl->boundsElements.clear();
	impl->inputBranches={}; impl->modalBranches={}; impl->interactionAncestors.clear(); impl->elementIndex.clear(); impl->layoutWidgets.clear();
	impl->hud = nullptr;
	impl->paintDirty=true;
	impl->changedProjections.clear();
	impl->canonical.reset(); impl->snapshotDocumentIdentity.clear(); impl->documentFontCss.clear(); impl->inlineAuthored=false; impl->inlineOverridden.clear();
	impl->appliedInputs.clear(); impl->deferredInputs.clear(); impl->inputKeys.clear(); impl->indexedKeys = nullptr; impl->motion.Reset(DocumentModel{});
	impl->state = {}; impl->stateSources.clear(); impl->appliedStateRevision = 0; impl->stateError.clear();
	impl->interaction.Reset({}); impl->controls.clear(); impl->pointerPresent = impl->pointerNavigation = false;
	impl->pendingFocus.clear();
	impl->ResetTouch(); impl->rimElement = nullptr; impl->rimPlaced = {};
	impl->initialized = false;
	impl->time = 0;
	impl->contextName.clear();
	impl->sourcePath.clear();
	impl->services.reset();
}
void Runtime::CloseDocument() {
    if(!Mutate(true))return;
    AbortPreparedDocument();

	impl->sourcePath.clear();
	impl->boundsElements.clear();
	impl->inputBranches={}; impl->modalBranches={}; impl->interactionAncestors.clear(); impl->elementIndex.clear(); impl->layoutWidgets.clear();
	impl->hud = nullptr;
	impl->valueView.Reset();
	impl->scrollView.Reset();impl->scrollLayoutDirty=impl->preserveRestoredScroll=false;
	impl->focusLayoutId.clear();impl->focusLayout.clear();impl->focusLayoutVisible=false;
	impl->numberView.Reset();
	if (impl->backend) impl->backend->renderer.InvalidateFrame();
	impl->paintDirty=true;
	impl->changedProjections.clear();
	impl->canonical.reset(); impl->snapshotDocumentIdentity.clear(); impl->documentFontCss.clear(); impl->inlineAuthored=false; impl->inlineOverridden.clear();
	impl->appliedInputs.clear(); impl->deferredInputs.clear(); impl->inputKeys.clear(); impl->indexedKeys = nullptr; impl->motion.Reset(DocumentModel{});
	impl->state = {}; impl->stateSources.clear(); impl->appliedStateRevision = 0; impl->stateError.clear();
	impl->interaction.Reset({}); impl->controls.clear(); impl->pointerPresent = impl->pointerNavigation = false;
	impl->pendingFocus.clear();
	// Closing the document destroys the rim light's element with it.
	impl->ResetTouch(); impl->rimElement = nullptr; impl->rimPlaced = {};
	if (!impl->document) return;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	impl->document->Close();
	impl->document = nullptr;
	impl->ReleaseModelScenes();
	impl->canonicalElements.clear();
	impl->context->Update();
}
bool Runtime::LoadMarkup(const std::string& markup, const std::string& sourcePath) {
    if(!Mutate())return false;

	if (!Initialize()) return false;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	// Keep the currently loaded document if parsing a replacement fails.
	auto* document = impl->context->LoadDocumentFromMemory(markup, sourcePath);
	if (!document) return false;
	CloseDocument();
	impl->document = document;
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	return true;
}
bool Runtime::LoadDocument(const std::string& source, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics) {
    if(!Mutate())return false;

	auto candidate = std::make_unique<Document>();
	if (!candidate->Load(source,diagnostics)) return false;
	return LoadCanonical(std::move(candidate),sourcePath,diagnostics);
}
bool Runtime::LoadDocument(const Document& document, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics) {
    if(!Mutate())return false;
	diagnostics.clear();
	if (document.Source().empty()) return false; // Never loaded.
	return LoadCanonical(std::make_unique<Document>(document),sourcePath,diagnostics);
}
bool Runtime::LoadCanonical(std::unique_ptr<Document> candidate, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics) {
	if (!Mutate() || !Initialize()) return false;
	Json::Value identity(Json::objectValue);
	identity["version"] = 1; identity["id"] = candidate->Model().id;
	identity["path"] = sourcePath; identity["source"] = candidate->Source();
	Json::StreamWriterBuilder identityWriter; identityWriter["indentation"] = ""; identityWriter["precision"] = 17;
	std::string snapshotIdentity = Json::writeString(identityWriter,identity);
	{
		ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
		// The document is built from the model, each node's element with its
		// typed properties (no markup is derived or parsed). Keep the currently
		// loaded document if building a replacement fails.
		std::unordered_map<std::string,Rml::Element*> elements;
		auto* document = impl->BuildCanonicalDocument(candidate->Model(),sourcePath,elements);
		if (!document) return false;
		CloseDocument();
		impl->document = document;
		impl->canonicalElements = std::move(elements);
		document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	}
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	// The loaded document outlives the motion, state and views that read its
	// model: they share it instead of copying its values and declarations.
	impl->motion.Reset(candidate->SharedModel());
	impl->interaction.Reset(candidate->Model());
	std::string stateError;
	impl->state = candidate->InitialState(); // Evaluated and validated by Document::Load, from the shared model.
	for (const auto& [id,declaration]:impl->state.Declarations()) if (declaration.HostSourced())
		impl->stateSources.push_back({id,declaration.cvar,declaration.view,declaration.initial.index()});
	impl->canonical = std::move(candidate);
	impl->sourcePath = sourcePath;
	impl->snapshotDocumentIdentity = std::move(snapshotIdentity);
	// The build mapped every node to its element, so the load resolves each
	// node without a search.
	std::vector<const Node*> nodes{&impl->canonical->Model().root};
	while (!nodes.empty()) {
		const auto* node = nodes.back(); nodes.pop_back();
		const auto built = impl->canonicalElements.find(node->id);
		auto* element = built == impl->canonicalElements.end() ? nullptr : built->second;
		if (node->control) {
			impl->controls.push_back(node->id);
			if (node->control->role!=ControlRole::Button && element) impl->layoutWidgets.push_back(element);
		}
		if (element) {
			// Canonical subtrees preserve paint order when opacity crosses 1 and
			// their temporary filter layer appears or disappears.
			element->SetProperty(Rml::PropertyId::ZIndex,Rml::Property(0.f,Rml::Unit::NUMBER));
			static_cast<VectorElement*>(element)->Configure(*node,impl->host,impl->Stats());
			static_cast<VectorElement*>(element)->SetImageSamplingInterface(&impl->backend->renderer);
			static_cast<VectorElement*>(element)->SetLinearMaskInterface(&impl->backend->renderer);
			if (node->model) impl->models.push_back({&*node->model,static_cast<VectorElement*>(element),0});
		}
		for (const auto& child : node->children) nodes.push_back(&child);
	}
	if (!impl->valueView.Initialize(impl->canonical->SharedModel(),*impl->document,
		[owner=impl.get()](const std::string& text) { return owner->host.Translate(text); },stateError,
		[owner=impl.get()](const std::string& id) { return owner->CanonicalElement(id); })) {
		diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
	}
	impl->valueView.SetSafeArea(impl->viewport.safeLeft,impl->viewport.safeTop,impl->viewport.safeRight,impl->viewport.safeBottom);
	if (!impl->scrollView.Initialize(impl->canonical->Model(),*impl->document,stateError)) {
        diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
    }
    if (!impl->numberView.Initialize(impl->canonical->SharedModel(),*impl->document,
		[owner=impl.get()](std::uintptr_t face,std::string_view text,float spacing) { return owner->services->fonts.QueryRun(face,text,spacing); },
		[owner=impl.get()](const std::string& text) { return owner->host.Translate(text); },{},stateError,
		[owner=impl.get()](const std::string& id) { return owner->CanonicalElement(id); })) {
		diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
	}
	impl->inputBranches=impl->BuildInteractionBranch(impl->canonical->Model().root,false);
	impl->modalBranches=impl->BuildInteractionBranch(impl->canonical->Model().root,true);
	impl->ReadStateSources(); impl->Feedback(impl->time);
	// The built inline values are current for the first pass, except on the
	// choice popups and viewports the value-control view styled above.
	impl->inlineOverridden.clear();
	for (const auto& id : impl->controls)
		if (const auto* node = impl->canonical->Model().FindNode(id))
			if (const auto* choice = node->control ? std::get_if<ChoiceSpec>(&node->control->widget) : nullptr) {
				impl->inlineOverridden.insert(choice->popup); impl->inlineOverridden.insert(choice->viewport);
			}
	impl->inlineAuthored = true;
	impl->ApplyMotion();
	impl->presentationDirty = true; // A new document is a new presentation, as its first pass always found.
	return true;
}
bool Runtime::SetHudPresentation(const std::vector<HudPrimitive>& items,const Viewport& viewport,std::string& error) {
	if (!Mutate() || !impl->canonical || !impl->document || impl->canonical->Model().id != "openq4.hud-artwork") {
		error="HUD presentation requires its canonical artwork catalog"; return false;
	}
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	if (!impl->hud) {
		auto child=Rml::Factory::InstanceElement(impl->document,"q4-hud","q4-hud",{});
		if (!child) { error="Could not create retained HUD presentation"; return false; }
		impl->hud=static_cast<HudElement*>(child.get());
		impl->hud->SetProperty("position","absolute"); impl->hud->SetProperty("left","0px"); impl->hud->SetProperty("top","0px");
		impl->hud->SetProperty("width","100%"); impl->hud->SetProperty("height","100%");
		impl->document->AppendChild(std::move(child));
	}
	bool changed=false;
	const bool prepared=impl->hud->Prepare(items,impl->canonical->Model().root,viewport.width,viewport.height,impl->Stats(),error,changed);
	if (changed || !prepared) impl->paintDirty=true;
	return prepared;
}
bool Runtime::PlayTimeline(const std::string& id, double seconds) {
    if(!Mutate())return false;
 return impl->canonical && impl->motion.Play(id,seconds); }
bool Runtime::SetState(const StateValues& changes, std::string& error, double seconds) {
    if(!Mutate())return false;

	if (!impl->canonical) { error = "State updates require a canonical document"; return false; }
	if (!impl->interaction.HasAuthoredModals()) {
		// openQ4: Set is transactional on its own, and without authored modals
		// there is nothing else to validate: a HUD's frequent updates no longer
		// copy the whole state.
		if (!impl->state.Set(changes,error)) return false;
	} else {
		State candidate = impl->state;
		if (!candidate.Set(changes,error) || !impl->ValidModals(candidate,impl->motion,error)) return false;
		impl->state = std::move(candidate);
	}
	if (std::isfinite(seconds)) impl->time = std::max(impl->time,seconds);
	impl->ApplyControlBindings(); return true;
}
StateValues Runtime::GetState(bool includeHostSources) const {
	StateValues values;
	for (const auto& [id,value] : impl->state.Variables())
		if (includeHostSources || !impl->state.Declarations().at(id).HostSourced()) values[id] = value;
	return values;
}
std::uint64_t Runtime::StateRevision() const { return impl->state.Revision(); }
bool Runtime::GetStateValue(const std::string& id, StateValue& value) const {
	const auto found = impl->state.Variables().find(id);
	if (found == impl->state.Variables().end()) return false;
	value = found->second; return true;
}
bool Runtime::SaveSnapshot(std::string& snapshot, std::string& error, double seconds) const {
	error.clear();
	if (!impl->canonical || !impl->document) { error = "Snapshots require a loaded canonical document"; return false; }
	if (!std::isfinite(seconds) || seconds < 0) { error = "Invalid snapshot presentation time"; return false; }
	try {
		const double now = std::max(seconds,impl->time);
		// Save durable drafts before copying interaction state. This checked
		// boundary rejects excessive aggregate text/history without truncation.
		ValueWidgetSnapshot widgets;
		if (!impl->interaction.CaptureWidgets(widgets,error)) return false;
        for(const auto& [id,offset]:impl->scrollView.CaptureOffsets(impl->interaction)) {
            if(!std::isfinite(offset)||offset<0||offset>1e12) {error="Saved scroll offset is outside its bounded range";return false;}
            widgets.widgets.at(id).scrollOffsetDp=offset;
        }
		Interaction interaction = impl->interaction;
		Motion motion = impl->motion;
		// A completion due by the snapshot's time belongs in it: run it on these
		// copies, so a restore neither replays it nor loses it.
		State snapshotState = impl->state;
		motion.Advance(now);
		StateValues completed;
		impl->RunCompletions(snapshotState,motion,now,completed);
		const bool authoredModals = interaction.HasAuthoredModals();
		if (authoredModals) {
			std::vector<std::string> roots;
			if (!impl->VisibleModals(snapshotState,motion,roots,error) || !interaction.SyncAuthoredModals(roots,error)) return false;
		}
		interaction.Cancel();
		// A press/hover is transient. Preserve a continuous transition toward
		// its persistent focus/default feedback instead of reviving pressed ink.
		// The pointer's lead is too: a restore starts with focus shown.
		interaction.SetPointerPresentation(false);
		for (const auto& feedback : interaction.TakeFeedback()) motion.Play(feedback.timeline,now);
		const auto playback = motion.Capture(now);
		if (!motion.Restore(playback,now,error,true)) return false;
		const auto input = interaction.Capture();
		Json::Value root(Json::objectValue);
		root["format"] = "openq4-ui-instance"; root["version"] = authoredModals ? 4 : widgets.widgets.empty() ? 2 : 3;
		root["application"] = Json::Value(Json::objectValue);
		for (const auto& [id,value] : snapshotState.Variables()) if (!snapshotState.Declarations().at(id).HostSourced())
			std::visit([&](const auto& primitive) { root["application"][id] = primitive; },value);
		if (authoredModals) {
			root["hostSources"] = Json::Value(Json::objectValue);
			for (const auto& [id,declaration] : snapshotState.Declarations()) if (declaration.HostSourced())
				std::visit([&](const auto& primitive) { root["hostSources"][id] = primitive; },snapshotState.Variables().at(id));
		}
		auto& aliasState = root["presentationState"];
		aliasState["variables"] = Json::Value(Json::objectValue);
		for (const auto& [id,cell] : snapshotState.Presentation().variables) {
			auto& item = aliasState["variables"][id]; item["value"] = SnapshotPresentationValue(cell.value);
			item["expressionDisabled"] = cell.expressionDisabled; item["pending"] = cell.pending;
		}
		aliasState["properties"] = Json::Value(Json::arrayValue);
		for (const auto& [key,cell] : snapshotState.Presentation().properties) {
			Json::Value item(Json::objectValue);
			item["node"] = key.first; item["property"] = key.second; item["value"] = SnapshotValue(cell.value);
			item["expressionDisabled"] = cell.expressionDisabled;
			aliasState["properties"].append(std::move(item));
		}
		auto& presentation = root["presentation"];
		presentation["reducedMotion"] = playback.reducedMotion;
		presentation["values"] = Json::Value(Json::arrayValue);
		for (const auto& [key,value] : playback.values) {
			Json::Value item(Json::objectValue);
			item["node"] = key.first; item["property"] = key.second; item["value"] = SnapshotValue(value);
			presentation["values"].append(std::move(item));
		}
		presentation["playing"] = Json::Value(Json::arrayValue);
		for (const auto& saved : playback.playing) {
			Json::Value item(Json::objectValue);
			item["owner"] = saved.owner; item["node"] = saved.node; item["property"] = saved.property;
			item["from"] = SnapshotValue(saved.from); item["elapsedMs"] = saved.elapsedMs; item["durationMs"] = saved.durationMs;
			item["paused"] = saved.paused; item["reduced"] = saved.reduced;
			presentation["playing"].append(std::move(item));
		}
		auto& semantics = root["interaction"];
		semantics["focus"] = input.focus; semantics["modals"] = Json::Value(Json::arrayValue);
		if (authoredModals) { semantics["focusPending"] = input.focusPending; semantics["pendingFocus"] = input.pendingFocus; }
		for (const auto& scope : input.modals) {
			Json::Value item(Json::objectValue); item["root"] = scope.root; item["restore"] = scope.restore;
			if (authoredModals) item["authored"] = scope.authored;
			semantics["modals"].append(std::move(item));
		}
		semantics["enabled"] = Json::Value(Json::objectValue);
		for (const auto& [id,enabled] : input.enabled) if (!snapshotState.Enabled().contains(id)) semantics["enabled"][id] = enabled;
		semantics["presented"] = Json::Value(Json::objectValue);
		for (const auto& [id,state] : input.presented) semantics["presented"][id] = unsigned(state);
		root["widgets"] = Json::Value(Json::objectValue);
		if (authoredModals || !widgets.widgets.empty()) {
			root["widgets"]["version"] = widgets.version;
			root["widgets"]["controls"] = Json::Value(Json::objectValue);
			for (const auto& [id,widget] : widgets.widgets) {
				auto& entry = root["widgets"]["controls"][id];
				entry["role"] = unsigned(widget.role); entry["firstVisible"] = widget.firstVisible;
				if (widget.number) entry["number"] = SnapshotNumberEditor(*widget.number);
                if(widget.scrollOffsetDp) entry["scrollOffsetDp"]=*widget.scrollOffsetDp;
			}
		}
		Json::StreamWriterBuilder writer; writer["indentation"] = ""; writer["precision"] = 17;
		const std::string dynamic = Json::writeString(writer,root);
		const auto& identity = impl->snapshotDocumentIdentity;
		constexpr std::size_t prefixBytes = sizeof("{\"document\":")-1;
		if (identity.size() > MaxSnapshotBytes-prefixBytes || dynamic.size() > MaxSnapshotBytes-prefixBytes-identity.size()) {
			error = "Instance snapshot exceeds the 128 MiB limit"; return false;
		}
		// Compose two independently encoded JSON objects, preserving every
		// existing field and the exact source identity. Only member order changes.
		std::string candidate;
		candidate.reserve(prefixBytes+identity.size()+dynamic.size());
		candidate = "{\"document\":"; candidate += identity; candidate += ',';
		candidate.append(dynamic.data()+1,dynamic.size()-1);
		snapshot.swap(candidate); return true;
	} catch (const std::exception& problem) { error = std::string("Cannot save instance snapshot: ")+problem.what(); return false; }
}
// A saved document source matches whatever line endings each checkout gave the
// file: Windows checkouts write CRLF and the others LF, so a save made on one
// platform names the same document differently on another (roadmap Q4-06).
static bool SnapshotSourceMatches(const Json::Value& saved, const std::string& source) {
	if (!saved.isString()) return false;
	const std::string text = saved.asString();
	std::size_t a = 0, b = 0;
	for (;;) {
		if (a < text.size() && text[a] == '\r' && a+1 < text.size() && text[a+1] == '\n') ++a;
		if (b < source.size() && source[b] == '\r' && b+1 < source.size() && source[b+1] == '\n') ++b;
		if (a == text.size() || b == source.size()) return a == text.size() && b == source.size();
		if (text[a++] != source[b++]) return false;
	}
}
bool Runtime::RestoreSnapshot(const std::string& snapshot, std::string& error, double seconds) {
    if(!Mutate())return false;

	error.clear();
	auto reject = [&](const char* message) { error = message; return false; };
	if (!impl->canonical || !impl->document) return reject("Snapshots require a loaded canonical document");
	if (!std::isfinite(seconds) || seconds < 0) return reject("Invalid restored presentation time");
	if (snapshot.empty() || snapshot.size() > MaxSnapshotBytes) return reject("Invalid instance snapshot size");
	try {
		if (!SnapshotLexicalForms(snapshot)) return reject("Invalid instance snapshot JSON lexical form");
		Json::CharReaderBuilder builder;
		builder["collectComments"] = false; builder["allowComments"] = false; builder["allowTrailingCommas"] = false;
		builder["strictRoot"] = true; builder["failIfExtra"] = true; builder["rejectDupKeys"] = true;
		builder["allowSpecialFloats"] = false; builder["stackLimit"] = 32; builder["skipBom"] = false;
		std::unique_ptr<Json::CharReader> reader(builder.newCharReader()); Json::Value root;
		if (!reader->parse(snapshot.data(),snapshot.data()+snapshot.size(),&root,nullptr)) return reject("Invalid instance snapshot JSON");
		if (root["format"] != "openq4-ui-instance" || !root["version"].isUInt() || root["version"].asUInt() < 1 || root["version"].asUInt() > 4)
			return reject("Unsupported instance snapshot schema");
		const bool authoredModals = root["version"].asUInt() == 4;
		if (authoredModals != impl->interaction.HasAuthoredModals()) return reject("Instance snapshot modal schema does not match the document");
		const bool hasPresentation = root["version"].asUInt() >= 2;
		if (authoredModals ? !SnapshotFields(root,{"format","version","document","application","hostSources","presentation","interaction","widgets","presentationState"}) :
			hasPresentation ? !SnapshotFields(root,{"format","version","document","application","presentation","interaction","widgets","presentationState"}) :
			!SnapshotFields(root,{"format","version","document","application","presentation","interaction","widgets"})) return reject("Invalid instance snapshot fields");
		const auto& identity = root["document"];
		if (!SnapshotFields(identity,{"version","id","path","source"}) || !identity["version"].isUInt() || identity["version"].asUInt() != 1 ||
			identity["id"] != impl->canonical->Model().id || identity["path"] != impl->sourcePath ||
			!SnapshotSourceMatches(identity["source"],impl->canonical->Source()))
			return reject("Instance snapshot document/source identity mismatch");
		ValueWidgetSnapshot widgets;
		std::size_t numberEditors = 0, numberTextRemaining = ValueWidgetSnapshot::MaxNumberTextBytes;
		if (root["version"].asUInt() >= 3) {
			const auto& saved = root["widgets"];
			if (!SnapshotFields(saved,{"version","controls"}) || !saved["version"].isUInt() || saved["version"].asUInt() < 1 || saved["version"].asUInt() > 3 ||
				!saved["controls"].isObject() || saved["controls"].size() > impl->controls.size()) return reject("Invalid restored widget table");
			widgets.version = saved["version"].asUInt();
			for (const auto& id : saved["controls"].getMemberNames()) {
				const auto& entry = saved["controls"][id];
				const bool hasNumber = widgets.version >= 2 && entry.isMember("number");
                const bool hasScroll = widgets.version == 3 && entry.isMember("scrollOffsetDp");
				if (!(hasNumber ? SnapshotFields(entry,{"role","firstVisible","number"}) : hasScroll ? SnapshotFields(entry,{"role","firstVisible","scrollOffsetDp"}) : SnapshotFields(entry,{"role","firstVisible"})) ||
					!entry["role"].isUInt() || entry["role"].asUInt() < unsigned(ControlRole::Toggle) ||
					entry["role"].asUInt() > unsigned(widgets.version == 3 ? ControlRole::Scrollbar : widgets.version == 2 ? ControlRole::Number : ControlRole::Choice) ||
					!entry["firstVisible"].isUInt()) return reject("Invalid restored widget state");
				ValueWidgetSnapshot::Widget widget{ControlRole(entry["role"].asUInt()),entry["firstVisible"].asUInt()};
				if(hasScroll) {
                    if((widget.role!=ControlRole::Scrollbar && widget.role!=ControlRole::Choice) || !entry["scrollOffsetDp"].isNumeric() ||
                        !std::isfinite(entry["scrollOffsetDp"].asDouble()) || entry["scrollOffsetDp"].asDouble()<0 || entry["scrollOffsetDp"].asDouble()>1e12)
                        return reject("Invalid restored scrollbar offset");
                    widget.scrollOffsetDp=entry["scrollOffsetDp"].asDouble();
                }
                if (hasNumber) {
                    if (widget.role != ControlRole::Number || ++numberEditors > ValueWidgetSnapshot::MaxNumberEditors)
						return reject("Invalid restored number editor table");
					widget.number.emplace();
					if (!ReadSnapshotNumberEditor(entry["number"],*widget.number,numberTextRemaining)) return reject("Invalid restored number editor");
				}
				widgets.widgets.emplace(id,std::move(widget));
			}
		} else if (!root["widgets"].isObject() || !root["widgets"].empty()) return reject("Unsupported restored widget state");
		const auto& application = root["application"];
		if (!application.isObject()) return reject("Invalid restored application state");
		StateValues values, sources;
		for (const auto& id : application.getMemberNames()) {
			const auto& value = application[id];
			if (value.isBool()) values[id] = value.asBool();
			else if (value.isNumeric()) values[id] = value.asDouble();
			else if (value.isString()) values[id] = value.asString();
			else return reject("Invalid restored application value type");
		}
		for (const auto& [id,declaration] : impl->state.Declarations()) if (declaration.HostSourced()) {
			StateValue value = impl->state.Variables().at(id); // A view source keeps the view it last measured.
			if (declaration.view.empty() && !impl->host.ReadCVar(declaration.cvar,declaration.initial.index(),value)) {
				error = "Unavailable CVar source during restore: "+declaration.cvar; return false;
			}
			sources.emplace(id,std::move(value));
		}
		State state = impl->state;
		StatePresentationSnapshot restoredPresentation;
		if (hasPresentation) {
			const auto& aliasState = root["presentationState"];
			if (!SnapshotFields(aliasState,{"variables","properties"}) || !aliasState["variables"].isObject() ||
				!aliasState["properties"].isArray() || aliasState["properties"].size() > impl->canonical->Model().bindings.size() ||
				aliasState["variables"].size() != impl->canonical->Model().presentationVariables.size()) return reject("Invalid restored presentation tables");
			for (const auto& id : aliasState["variables"].getMemberNames()) {
				const auto& item = aliasState["variables"][id]; PresentationCell cell;
				if (!SnapshotFields(item,{"value","expressionDisabled","pending"}) || !item["expressionDisabled"].isBool() || !item["pending"].isBool() ||
					!ReadSnapshotPresentationValue(item["value"],cell.value)) return reject("Invalid restored presentation cell");
				cell.expressionDisabled = item["expressionDisabled"].asBool(); cell.pending = item["pending"].asBool();
				restoredPresentation.variables.emplace(id,std::move(cell));
			}
			for (const auto& item : aliasState["properties"]) {
				PresentationPropertyOverride cell;
				if (!SnapshotFields(item,{"node","property","value","expressionDisabled"}) || !item["node"].isString() || !item["property"].isString() ||
					!item["expressionDisabled"].isBool() || !ReadSnapshotValue(item["value"],cell.value)) return reject("Invalid restored presentation override");
				cell.expressionDisabled = item["expressionDisabled"].asBool();
				if (!restoredPresentation.properties.emplace(PropertyKey{item["node"].asString(),item["property"].asString()},std::move(cell)).second)
					return reject("Duplicate restored presentation override");
			}
		}
		if (!state.Restore(values,sources,error,hasPresentation ? &restoredPresentation : nullptr)) return false;
		const auto& presentation = root["presentation"];
		if (!SnapshotFields(presentation,{"reducedMotion","values","playing"}) || !presentation["reducedMotion"].isBool() ||
			!presentation["values"].isArray() || !presentation["playing"].isArray() ||
			presentation["values"].size() > impl->motion.Values().size() || presentation["playing"].size() > impl->motion.Values().size())
			return reject("Invalid restored presentation state");
		MotionSnapshot playback; playback.reducedMotion = presentation["reducedMotion"].asBool();
		for (const auto& item : presentation["values"]) {
			Value value;
			if (!SnapshotFields(item,{"node","property","value"}) || !item["node"].isString() || !item["property"].isString() || !ReadSnapshotValue(item["value"],value) ||
				!playback.values.emplace(PropertyKey{item["node"].asString(),item["property"].asString()},std::move(value)).second)
				return reject("Invalid or duplicate restored presentation property");
		}
		for (const auto& item : presentation["playing"]) {
			MotionPlayback saved;
			if (!SnapshotFields(item,{"owner","node","property","from","elapsedMs","durationMs","paused","reduced"}) ||
				!item["owner"].isString() || !item["node"].isString() || !item["property"].isString() || !ReadSnapshotValue(item["from"],saved.from) ||
				!item["elapsedMs"].isNumeric() || !item["durationMs"].isNumeric() || !item["paused"].isBool() || !item["reduced"].isBool())
				return reject("Invalid restored timeline playback");
			saved.owner = item["owner"].asString(); saved.node = item["node"].asString(); saved.property = item["property"].asString();
			saved.elapsedMs = item["elapsedMs"].asDouble(); saved.durationMs = item["durationMs"].asDouble();
			saved.paused = item["paused"].asBool(); saved.reduced = item["reduced"].asBool(); playback.playing.push_back(std::move(saved));
		}
		const double now = std::max(seconds,impl->time);
		Motion motion = impl->motion;
		if (!motion.Restore(playback,now,error,hasPresentation)) return false;
		const auto& semantics = root["interaction"];
		if (!(authoredModals ? SnapshotFields(semantics,{"focus","modals","enabled","presented","focusPending","pendingFocus"}) :
			SnapshotFields(semantics,{"focus","modals","enabled","presented"})) || !semantics["focus"].isString() || !semantics["modals"].isArray() ||
			semantics["modals"].size() > 64 || !semantics["enabled"].isObject() || !semantics["presented"].isObject()) return reject("Invalid restored interaction state");
		InteractionSnapshot input; input.focus = semantics["focus"].asString();
		if (authoredModals) {
			if (!semantics["focusPending"].isBool() || !semantics["pendingFocus"].isString()) return reject("Invalid restored pending modal focus");
			input.focusPending = semantics["focusPending"].asBool(); input.pendingFocus = semantics["pendingFocus"].asString();
		}
		for (const auto& scope : semantics["modals"]) {
			if (!(authoredModals ? SnapshotFields(scope,{"root","restore","authored"}) : SnapshotFields(scope,{"root","restore"})) ||
				!scope["root"].isString() || !scope["restore"].isString() || (authoredModals && !scope["authored"].isBool())) return reject("Invalid restored modal scope");
			input.modals.push_back({scope["root"].asString(),scope["restore"].asString(),authoredModals && scope["authored"].asBool()});
		}
		for (const auto& id : semantics["enabled"].getMemberNames()) {
			if (!semantics["enabled"][id].isBool() || state.Enabled().contains(id)) return reject("Invalid or binding-owned restored control override");
			input.enabled[id] = semantics["enabled"][id].asBool();
		}
		for (const auto& [id,enabled] : state.Enabled()) input.enabled[id] = enabled;
		for (const auto& id : semantics["presented"].getMemberNames()) {
			const auto& value = semantics["presented"][id];
			if (!value.isUInt() || value.asUInt() > unsigned(ControlState::Disabled)) return reject("Invalid restored control presentation");
			input.presented[id] = ControlState(value.asUInt());
		}
		Interaction interaction = impl->interaction;
		if (!interaction.SetReadbacks(state.ControlValues(),error) || !interaction.Restore(input,error)) return false;
		const auto restoredFeedback = interaction.TakeFeedback();
		if (!interaction.RestoreWidgets(widgets,error)) return false;
		std::vector<std::string> modalRoots;
		if (authoredModals) {
			// Validate the saved scope chain against its own source values before
			// reconciling fresh host state. Saved CVars never replace live CVars.
			if (!root["hostSources"].isObject()) return reject("Invalid saved modal host sources");
			StateValues historicalSources;
			for (const auto& id : root["hostSources"].getMemberNames()) {
				const auto& value = root["hostSources"][id];
				if (value.isBool()) historicalSources[id] = value.asBool();
				else if (value.isNumeric()) historicalSources[id] = value.asDouble();
				else if (value.isString()) historicalSources[id] = value.asString();
				else return reject("Invalid saved modal host value");
			}
			State historical = impl->state;
			if (!historical.Restore(values,historicalSources,error,&restoredPresentation) ||
				!impl->VisibleModals(historical,motion,modalRoots,error)) return false;
			std::vector<std::string> savedRoots;
			for (const auto& scope : input.modals) if (scope.authored) savedRoots.push_back(scope.root);
			if (savedRoots != modalRoots) return reject("Saved modal scopes do not match saved visibility");
		}
		if (!impl->VisibleModals(state,motion,modalRoots,error) || !interaction.SyncAuthoredModals(modalRoots,error)) return false;
		// Host bindings remain authoritative. If current CVars changed control
		// availability, transition from the saved ink to the new semantic state.
		for (const auto& feedback : restoredFeedback) motion.Play(feedback.timeline,now);
		for (const auto& feedback : interaction.TakeFeedback()) motion.Play(feedback.timeline,now);
		// No live state, geometry, clock or input queues change before validation
		// completes. Rendering applies the restored values on the next frame.
		// Native editor ownership may have changed while preparing the restore.
		// Publish its checked adoption before any state or presentation changes.
		if (!impl->interaction.Adopt(std::move(interaction),error)) return false;
		impl->state = std::move(state); impl->motion = std::move(motion);
		impl->time = now; impl->pointerPresent = impl->pointerNavigation = false; impl->documentFontCss.clear(); impl->inlineAuthored=false; impl->inlineOverridden.clear();
		impl->appliedInputs.clear(); impl->deferredInputs.clear(); impl->inputKeys.clear(); impl->indexedKeys = nullptr;
		impl->pendingFocus.clear(); impl->ResetTouch();
        impl->preserveRestoredScroll=widgets.version==3;
		// The restored modals are open already: their saved scroll offsets stand.
		impl->openModals = modalRoots;
		impl->appliedStateRevision = impl->state.Revision(); impl->stateError.clear();
		return true;
	} catch (const std::exception& problem) { error = std::string("Cannot restore instance snapshot: ")+problem.what(); return false; }
}
std::optional<Value> Runtime::PresentedValue(const std::string& node, const std::string& property) const {
	return impl->PresentedProperty({node,property});
}
bool Runtime::GetPresentationAlias(const std::string& name, std::string& value) const {
	if (!impl->canonical) return false;
	PresentationValue result;
	if (!ReadPresentationAlias(impl->canonical->Model(),impl->state,impl->motion,name,result)) return false;
	value = FormatPresentationValue(result); return true;
}
bool Runtime::SetPresentationAlias(const std::string& name, const std::string& text, bool overrideExpression, std::string& error) {
    if(!Mutate())return false;

	error.clear();
	if (!impl->canonical) { error = "Presentation writes require a canonical document"; return false; }
	PresentationValue current, value;
	if (!ReadPresentationAlias(impl->canonical->Model(),impl->state,impl->motion,name,current)) {
		error = "Unavailable presentation alias '"+name+"'"; return false;
	}
	if (!ParsePresentationValue(current.type,text,value,error)) return false;
	State state = impl->state; Motion motion = impl->motion;
	if (!WritePresentationAlias(impl->canonical->Model(),state,motion,name,value,overrideExpression,error) ||
		!impl->ValidModals(state,motion,error)) return false;
	impl->state = std::move(state); impl->motion = std::move(motion); impl->ApplyControlBindings(); return true;
}
bool Runtime::HasEvent(const std::string& name) const {
	return impl->canonical && impl->canonical->Model().events.contains(PresentationAliasKey(name));
}
bool Runtime::RunEvent(const std::string& name, double seconds, EventEffects& effects, std::string& error,
	const StateValues& application, const ActionValidator& validate, size_t maxActions) {
    if(!Mutate())return false;

	error.clear();
	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) { error = "Event requires a canonical document and valid presentation time"; return false; }
	const auto& model = impl->canonical->Model();
	StateValues sources;
	for (const auto& [id,declaration] : model.state) {
		if (declaration.cvar.empty()) {
			// A view source enters with the view the last layout measured.
			if (!declaration.view.empty()) sources[id] = impl->state.Variables().at(id);
			continue;
		}
		StateValue value;
		if (!impl->host.ReadCVar(declaration.cvar,declaration.initial.index(),value)) {
			error = "Unavailable CVar source '"+declaration.cvar+"' at event entry"; return false;
		}
		sources[id] = std::move(value);
	}
	State state = impl->state;
	if (!state.SetCombined(application,sources,error)) return false;
	const double now = std::max(impl->time,seconds);
	// Timelines that ended by this event's time complete before its program,
	// and any the program completes at once (reduced motion) after it. All of
	// it commits with the event, and their writes are published with its own.
	// openQ4: the live motion samples this time in place, undoably, so the
	// transactional copies below share its values until the program writes
	// one. A rejected event rolls the sample back once those copies are gone.
	struct Undo {
		Motion& motion; Motion::Checkpoint saved; bool armed = true;
		~Undo() { if (armed) motion.Rollback(std::move(saved)); }
	} undo{impl->motion,impl->motion.AdvanceUndoable(now)};
	Motion motion = impl->motion;
	StateValues before, after;
	std::string focus;
	impl->RunCompletions(state,motion,now,before,&focus);
	EventResult candidate;
	if (!EvaluateEvent(model,state,motion,name,now,candidate,error,validate,maxActions)) return false;
	if (!candidate.focus.empty()) focus = std::move(candidate.focus);
	impl->RunCompletions(candidate.state,candidate.motion,now,after,&focus);
	if (!impl->ValidModals(candidate.state,candidate.motion,error)) return false;
	EventEffects published{std::move(before),std::move(candidate.actions)};
	for (auto& [key,value] : candidate.stateChanges) published.stateChanges[key] = std::move(value);
	for (auto& [key,value] : after) published.stateChanges[key] = std::move(value);
	undo.armed = false;
	impl->state = std::move(candidate.state); impl->motion = std::move(candidate.motion); impl->time = now;
	impl->stateError.clear(); impl->ApplyControlBindings(); impl->UpdateInteraction(now);
	if (!focus.empty()) { impl->RequestFocus(focus); impl->Feedback(now); }
	effects = std::move(published); return true;
}
StateValues Runtime::TakeCompletionWrites() {
	StateValues result;
	result.swap(impl->completionWrites);
	return result;
}
bool Runtime::ResolveAction(const std::string& id, ActionInvocation& invocation, std::string& error, const StateValue* input) const {
	if (!impl->canonical) { error = "Action requires a canonical document"; return false; }
	return impl->canonical->Model().ResolveAction(id,impl->state.Variables(),invocation,error,
		MakePresentationLookup(impl->canonical->Model(),impl->state,impl->motion),input);
}
void Runtime::PauseTimeline(const std::string& id, double seconds) {
    if(!Mutate())return;
 impl->motion.Pause(id,seconds); }
void Runtime::ResumeTimeline(const std::string& id, double seconds) {
    if(!Mutate())return;
 impl->motion.Resume(id,seconds); }
void Runtime::CancelTimeline(const std::string& id, CancelPolicy policy, double seconds) {
    if(!Mutate())return;
 impl->motion.Cancel(id,policy,seconds); }
void Runtime::SetReducedMotion(bool enabled, double seconds) {
    if(!Mutate())return;
 impl->motion.SetReducedMotion(enabled,seconds); }
bool Runtime::Layout(const Viewport& viewport,double seconds) {
	if (!impl->context || !impl->document) return false;
	if (viewport.width <= 0 || viewport.height <= 0) {
        // Keep invalid dimensions visible to input; old Rml boxes must not be
        // republished before a later valid Frame measures layout again.
        impl->viewport.width=viewport.width;impl->viewport.height=viewport.height;
		impl->interaction.InvalidateLayout(); impl->interaction.Cancel(); impl->Feedback(seconds); return false;
	}
	const bool textScaleChanged = impl->viewport.TextRatio() != viewport.TextRatio();
	const bool viewportChanged = impl->viewport.width != viewport.width || impl->viewport.height != viewport.height ||
		impl->viewport.DpRatio() != viewport.DpRatio() || textScaleChanged ||
		impl->viewport.safeLeft != viewport.safeLeft || impl->viewport.safeTop != viewport.safeTop ||
		impl->viewport.safeRight != viewport.safeRight || impl->viewport.safeBottom != viewport.safeBottom;
	impl->scrollView.PreserveDensity(viewport.DpRatio());
	impl->viewport = viewport;
	impl->valueView.SetSafeArea(viewport.safeLeft,viewport.safeTop,viewport.safeRight,viewport.safeBottom);
	if (impl->pointerPresent) viewport.WindowToDocument(impl->windowPointerX,impl->windowPointerY,impl->pointerX,impl->pointerY);
	if (std::isfinite(seconds)) impl->time = std::max(impl->time, seconds);
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	impl->AdvanceTouch();
	const auto presentationStart=impl->Stats().Now();
	impl->ReadStateSources();
	const auto sourcesEnd=impl->Stats().Now();
	impl->motion.Advance(impl->time);
	std::string completionFocus;
	if (impl->RunCompletions(impl->state,impl->motion,impl->time,impl->completionWrites,&completionFocus)) impl->ApplyControlBindings();
	if (!completionFocus.empty()) impl->pendingFocus = std::move(completionFocus);
	const auto motionEnd=impl->Stats().Now();
	impl->ApplyMotion();
	const auto presentationEnd=impl->Stats().Now();
	impl->Stats().sourceMilliseconds+=std::chrono::duration<double,std::milli>(sourcesEnd-presentationStart).count();
	impl->Stats().motionMilliseconds+=std::chrono::duration<double,std::milli>(motionEnd-sourcesEnd).count();
	impl->Stats().applyMilliseconds+=std::chrono::duration<double,std::milli>(presentationEnd-motionEnd).count();
	impl->Stats().presentationMilliseconds+=std::chrono::duration<double,std::milli>(presentationEnd-presentationStart).count();
	impl->context->SetDimensions({viewport.width, viewport.height});
	impl->context->SetDensityIndependentPixelRatio(viewport.DpRatio());
	const bool presentationOnly=impl->canonical && impl->controls.empty() && !impl->scrollView.HasScrollbars() &&
		!impl->interaction.HasAuthoredModals() && !impl->pointerPresent;
	// Canonical buttons receive feedback through explicit presentation values,
	// just like HUD artwork. A resting button menu has no native CSS animation
	// or editor caret that requires a style/layout walk on every frame. Keep
	// interaction and widget settling below independent of this decision.
	const bool explicitLayout=impl->canonical && std::none_of(impl->layoutWidgets.begin(),impl->layoutWidgets.end(),
		[](const auto* element){return element->IsVisible(true);});
	const bool layoutUpdated=!explicitLayout || viewportChanged || impl->presentationDirty;
	if (layoutUpdated) {
		++impl->Stats().layoutUpdates;
		impl->context->Update(explicitLayout); impl->presentationDirty=false;
		impl->paintDirty=true;
	}
	// Owned widget wrappers remain derived layout. Bounded settling lets a
	// newly opened popup measure its authored rows before its first rendering,
	// including rows it wraps to fit a narrower safe area: showing, narrowing,
	// re-measuring the wrapped rows and settling take one pass each. A frame
	// stops at the first pass that changes nothing.
	const bool settleInteraction=!presentationOnly && (!explicitLayout || layoutUpdated || impl->interactionLayoutDirty ||
		impl->preserveRestoredScroll || impl->scrollLayoutDirty || !impl->pendingFocus.empty() ||
		impl->focusLayoutId!=impl->interaction.Focused());
	for (unsigned pass = 0; settleInteraction && pass < 4; ++pass) {
		impl->context->GetRootElement()->UpdateGeometryForProjection();
		const auto before = impl->interaction.Focused();
		if(!impl->UpdateInteraction(-1,true))return false;
		const bool focusChanged = before != impl->interaction.Focused();
		// Preserve restored/reflowed offsets only on axes owned by authored bars.
		// Other axes and legacy ancestors still reveal the current control.
		const bool preserveAuthoredAxes = !textScaleChanged && (impl->preserveRestoredScroll || (viewportChanged && !focusChanged));
		const bool focusReflow = impl->focusLayoutVisible && !viewportChanged && !impl->preserveRestoredScroll &&
			impl->focusLayoutId == impl->interaction.Focused() && impl->focusLayout != impl->FocusLayout();
		const bool revealed = (focusChanged || focusReflow || (pass == 0 && (viewportChanged || impl->preserveRestoredScroll))) && impl->RevealFocus(preserveAuthoredAxes);
		if (focusChanged) impl->ApplyMotion();
		const auto* focusedNode = impl->canonical ? impl->canonical->Model().FindNode(impl->interaction.Focused()) : nullptr;
		auto* focusedNumber = focusedNode && focusedNode->control && focusedNode->control->role == ControlRole::Number ?
			impl->CanonicalElement(focusedNode->id) : nullptr;
		const auto numberSize = focusedNumber ? focusedNumber->GetBox().GetSize(Rml::BoxArea::Border) : Rml::Vector2f{};
		bool painted = impl->valueView.Paint(impl->interaction,impl->state.ControlValues(),viewport.width,viewport.height,viewport.DpRatio(),
			[&](const std::string& id) { const auto value = impl->PresentedProperty({id,"opacity"}); return value ? value->data[0] : 1.0; },
			impl->presentationPass);
		painted |= std::exchange(impl->scrollLayoutDirty,false);
		painted |= impl->numberView.Paint(impl->interaction,viewport.DpRatio(),impl->time,impl->motion.ReducedMotion());
		if (!painted && !focusChanged && !revealed) break;
		impl->paintDirty=true;
		impl->context->Update();
		if (focusedNumber && focusedNumber->GetBox().GetSize(Rml::BoxArea::Border) != numberSize) {
			// Validation can grow the focused field without changing focus.
			// Reveal its new extent once; unchanged frames preserve user scroll.
			impl->RevealFocus(preserveAuthoredAxes); impl->context->Update();
		}
	}
	impl->preserveRestoredScroll=false;
	// The detail card follows the subject this layout settled on: its detail
	// shows the card and its content, then the card's measured box places it.
	bool detailChanged = false;
	if (impl->UpdateDetailSubject()) { impl->context->Update(); detailChanged = true; }
	if (impl->PlaceDetailCard()) { impl->context->Update(); detailChanged = true; }
	if (detailChanged) { impl->presentationDirty=false; impl->paintDirty=true; }
	if (layoutUpdated || detailChanged) impl->context->GetRootElement()->UpdateGeometryForProjection();
	if (layoutUpdated || settleInteraction || detailChanged) impl->RememberFocusLayout();
	impl->Stats().layoutMilliseconds+=std::chrono::duration<double,std::milli>(impl->Stats().Now()-presentationEnd).count();
	return true;
}
bool Runtime::Frame(const Viewport& viewport, double seconds) {
    if(!Mutate())return false;

	auto& statistics = impl->Stats();
	const auto residentCount = statistics.residentGeometryCount, residentBytes = statistics.residentGeometryBytes;
	statistics = {};
	statistics.timingsEnabled=impl->host.CollectTimings();
	statistics.residentGeometryCount = residentCount; statistics.residentGeometryBytes = residentBytes;
	const auto start = statistics.Now();
	if(!Layout(viewport,seconds))return false;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	bool modelPixelsChanged=false;
	for (auto& model:impl->models) {
		if (!model.element->IsVisible(true)) continue;
		const auto size=model.element->GetBox().GetSize(Rml::BoxArea::Border);
		if (size.x<=0 || size.y<=0) continue;
		Rml::Rectanglef projected;
		if (!Rml::ElementUtilities::GetBoundingBox(projected,model.element,Rml::BoxArea::Border)) continue;
		const double scale=std::max({1.0,double(projected.Width()/size.x),double(projected.Height()/size.y)});
		const double width=std::ceil(size.x*scale),height=std::ceil(size.y*scale);
		if (!std::isfinite(width) || !std::isfinite(height) || width>8192 || height>8192 || width*height>16777216) {
			impl->host.Log(true,"Projected model scene exceeds its render-target budget"); return false;
		}
		ModelScene scene; std::string image,error;
		if (!ResolveModelScene(*model.spec,impl->state.Variables(),scene,error)) { impl->host.Log(true,error); return false; }
		if (!model.lease) model.lease=NewModelSceneLease();
		if (!model.lease || !impl->host.RenderModelScene(model.lease,scene,int(width),int(height),
			impl->motion.ReducedMotion()?0:impl->time,image,error) || !ValidImageSource(image)) {
			impl->host.Log(true,error.empty()?"Model scene preparation failed":error); return false;
		}
		if (!model.element->SetImageSource(image)) { impl->host.Log(true,"Model scene has no retained image projection"); return false; }
		modelPixelsChanged=true;
	}
	if (modelPixelsChanged) {
		impl->paintDirty=true;
		impl->backend->renderer.ExternalPixelsChanged();
	}
	const auto updated = statistics.Now();
	impl->backend->renderer.BeginFrame(viewport.width,viewport.height);
	const bool replayCandidate=impl->host.CanReplayFrames() && impl->canonical && !impl->paintDirty &&
		std::none_of(impl->layoutWidgets.begin(),impl->layoutWidgets.end(),[](const auto* element){return element->IsVisible(true);});
	bool projectionMatches=true;
	if (replayCandidate && !impl->changedProjections.empty()) {
		impl->context->GetRootElement()->UpdateGeometryForProjection();
		for (auto* element:impl->changedProjections) if (!element->UpdateReplayProjection()) { projectionMatches=false; break; }
	}
	const bool replayed=replayCandidate && projectionMatches && impl->backend->renderer.ReplayFrame();
	if (!replayed) {
		impl->backend->renderer.RecordFrame(replayCandidate);
		impl->context->Render();
	}
	impl->backend->renderer.EndFrame();
	if (!replayed) impl->backend->renderer.FinishRecording();
	impl->paintDirty=false;
	impl->changedProjections.clear();
	// RmlUi resolves transform state while rendering. Hit/navigation bounds
	// therefore follow the just-presented frame, not stale transform matrices.
	// Decorative projection outside the control/modal ancestry cannot change
	// hit bounds. Painting it again is independent of refreshing interaction.
	if ((!replayed && (!impl->canonical || statistics.layoutUpdates)) || impl->interactionLayoutDirty)
		impl->UpdateInteraction(-1,true);
	if (!impl->pendingFocus.empty()) {
		const auto focus = std::move(impl->pendingFocus); impl->pendingFocus.clear();
		impl->SetPointerNavigation(false);
		if (impl->interaction.Focus(focus)) { impl->RevealFocus(); impl->Feedback(impl->time); }
	}
	const auto end = statistics.Now();
	statistics.updateMilliseconds = std::chrono::duration<double,std::milli>(updated-start).count();
	statistics.renderMilliseconds = std::chrono::duration<double,std::milli>(end-updated).count();
	statistics.frameMilliseconds = std::chrono::duration<double,std::milli>(end-start).count();
	return impl->backend->renderer.Succeeded();
}
bool Runtime::GetBounds(const std::string& id, Bounds& bounds) const {
    if(impl->viewport.width<=0 || impl->viewport.height<=0)return false;
	Rml::Element* element = nullptr;
	if (impl->canonical) {
		const auto found = impl->boundsElements.find(id);
		if (found != impl->boundsElements.end()) element = found->second;
	}
	if (!element && impl->document) {
		element = impl->CanonicalElement(id);
		if (element && impl->canonical && impl->canonical->Model().FindNode(id)) impl->boundsElements.emplace(id,element);
	}
	if (!element) return false;
	const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
	bounds = {position.x,position.y,size.x,size.y};
	return true;
}
bool Runtime::QueryTextFit(const std::string& id, std::string_view text, TextFit& result) const {
	if (!impl->document || !impl->services || !impl->canonical || text.size() > TextInputMaxBytes) return false;
	const auto* node = impl->canonical->Model().FindNode(id);
	if (!node || node->type != "text" || node->legacyText) return false;
	auto* element = impl->CanonicalElement(id);
	if (!element || !element->IsVisible(true)) return false;
	const float width = element->GetBox().GetSize(Rml::BoxArea::Content).x;
	if (!std::isfinite(width) || width <= 0) return false;
	const auto face = element->GetFontFaceHandle();
	if (!face) return false;
	// Retained canonical text is plain UTF-8. Case transforms in RmlUi are
	// byte-preserving; leave all source offsets in the caller's string.
	std::string displayed(text);
	const auto& style = element->GetComputedValues();
	if (style.text_transform() == Rml::Style::TextTransform::Uppercase) displayed = Rml::StringUtilities::ToUpper(displayed);
	else if (style.text_transform() == Rml::Style::TextTransform::Lowercase) displayed = Rml::StringUtilities::ToLower(displayed);
	if (displayed.size() != text.size()) return false;
	const auto run = impl->services->fonts.QueryRun(face,displayed,style.letter_spacing());
	if (!run || !run->monotonicLtr) return false;
	TextFit candidate;
	for (std::size_t i = 0; i < run->glyphs.size(); ++i) {
		const auto& glyph = run->glyphs[i];
		// Font submission rounds once at the end of each run, never per glyph.
		if (std::round(run->carets[i+1].x) > width) {
			candidate.overflow = true;
			candidate.maxIndex = static_cast<int>(glyph.byteStart ? glyph.byteStart : glyph.byteEnd);
			break;
		}
		if (glyph.codepoint == ' ') candidate.lastWhitespace = static_cast<int>(glyph.byteStart);
	}
	result = candidate;
	return true;
}
bool Runtime::SetProperty(const std::string& id, const std::string& property, const std::string& value) {
    if(!Mutate())return false;

	if (impl->canonical) return false; // Edit the canonical source transactionally.
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	return element->SetProperty(property,value);
}
bool Runtime::SetText(const std::string& id, const std::string& text) {
    if(!Mutate())return false;

	if (impl->canonical) return false;
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
	return true;
}
bool Runtime::IsLoaded() const { return impl->document != nullptr; }
RuntimeStatistics Runtime::Statistics() const {
	auto statistics = impl->Stats();
	if (impl->services) {
		statistics.residentBackends = impl->services->backends.size();
		statistics.activeContexts = std::count_if(impl->services->backends.begin(),impl->services->backends.end(),
			[](const auto& backend) { return backend->leased; });
	}
	return statistics;
}
void Runtime::PointerMove(float x, float y, double seconds) {
    if(!Mutate())return;

	impl->SetPointerNavigation(true);
	impl->pointerPresent = std::isfinite(x) && std::isfinite(y);
	impl->windowPointerX = x; impl->windowPointerY = y;
	impl->viewport.WindowToDocument(x,y,impl->pointerX,impl->pointerY);
	// A mouse that moves ends a long-pressed detail: hover leads again.
	if (!impl->touchPointer && impl->pointerPresent) impl->detailPin.clear();
	if (std::isfinite(seconds) && seconds >= 0) impl->time = std::max(impl->time,seconds);
	impl->MoveTouch();
	if(!impl->UpdateInteraction(seconds,false,true))return; impl->Feedback(seconds);
}
void Runtime::PointerButton(bool down, double seconds) {
    if(!Mutate())return;
 impl->SetPointerNavigation(true); if(!impl->UpdateInteraction(seconds,false,true))return;
 // A press stops inertia and closes a long-pressed detail; a lifted finger
 // settles its gesture before the release can activate anything.
 if(down) { impl->fling = {}; impl->detailPin.clear(); } else impl->ReleaseTouch();
 impl->interaction.Pointer(down); impl->ApplyScrollCommands();
 if(down) { impl->RememberFocusLayout(); impl->BeginTouch(); }
 impl->Feedback(seconds); }
void Runtime::SetTouchPointer(bool touch) { impl->touchPointer = touch; if (!touch && impl->touch.pressed) impl->EndTouch(); }
std::vector<std::string> Runtime::TakeGestureEvents() { std::vector<std::string> result; result.swap(impl->gestureEvents); return result; }
void Runtime::PointerWheel(int rows, double seconds) {
    if(!Mutate())return;

	impl->fling = {};
	if(!impl->UpdateInteraction(seconds,false,true))return;
	if (!rows) return;
	for (const auto& id : impl->controls) {
		const auto widget = impl->interaction.Widget(id);
		if (!widget || !widget->popupOpen) continue;
		// One bounded row step per supplied wheel event; platform adapters do not
		// turn accumulated input or a stalled frame into a navigation burst.
		// Keep this selection through layout and subsequent wheel events. The
		// stationary pointer must not reselect its old row on the next frame;
		// an actual pointer move or button event restores pointer navigation.
		impl->SetPointerNavigation(false);
        const auto* node=impl->canonical->Model().FindNode(id);
        const auto* choice=node&&node->control?std::get_if<ChoiceSpec>(&node->control->widget):nullptr;
        if(choice&&choice->scrollbar) {
            if(impl->valueView.ChoiceScrollFresh(id,impl->interaction))impl->interaction.ChoiceScrollPulse(id,rows>0?ScrollStep::LineForward:ScrollStep::LineBackward);
        } else impl->interaction.NavigationPulse(rows > 0 ? MenuInput::Down : MenuInput::Up);
		impl->Feedback(seconds); return;
	}
	if (!impl->pointerPresent || !impl->context || !impl->document || impl->pointerX < 0 || impl->pointerY < 0 ||
		impl->pointerX >= impl->viewport.width || impl->pointerY >= impl->viewport.height) return;
	if (auto* hit = impl->HitElement()) {
        const auto target=impl->scrollView.WheelTarget(hit,impl->interaction);
        if(!target.empty()) {
            impl->focusLayoutVisible=false;
            impl->interaction.ScrollPulse(target,rows>0?ScrollStep::LineForward:ScrollStep::LineBackward);
            impl->ApplyScrollCommands();impl->Feedback(seconds);return;
        }
        if (auto* scroll = hit->GetClosestScrollableContainer(); scroll && impl->interaction.AllowsNode(scroll->GetId())) {
            impl->focusLayoutVisible=false;
            scroll->SetScrollTop(scroll->GetScrollTop()+(rows > 0 ? 36.f : -36.f)*impl->viewport.DpRatio());
			impl->presentationDirty=true;
        }
    }
}
bool Runtime::ScrollDetail(float dp, double seconds) {
    if(!Mutate())return false;
	if (!std::isfinite(dp) || dp == 0) return false;
	if (!impl->UpdateInteraction(seconds,false,true)) return false;
	// An open choice list holds the input, as it does the wheel's.
	for (const auto& id : impl->controls) if (const auto widget = impl->interaction.Widget(id); widget && widget->popupOpen) return false;
	auto* area = impl->DetailArea();
	if (!area) return false;
	const float before = area->GetScrollTop();
	area->SetScrollTop(before+dp*impl->viewport.DpRatio());
	if (area->GetScrollTop() == before) return false;
	impl->presentationDirty = true;
	return true;
}
void Runtime::MenuAction(MenuInput input, bool down, double seconds) {
    if(!Mutate())return;

    if(impl->viewport.width<=0 || impl->viewport.height<=0) {impl->interaction.QuarantineInput(input,down);return;}
	if (down && input != MenuInput::Back && !impl->interaction.CapturedPointerControl().empty()) impl->interaction.Cancel();
	if (down) { impl->pendingFocus.clear(); impl->fling = {}; impl->detailPin.clear(); }
	const auto before = impl->interaction.Focused();
	impl->SetPointerNavigation(false); if(!impl->UpdateInteraction(seconds,false,true))return;
	if (!down || !impl->PageList(input)) impl->interaction.Input(input,down);
	impl->ApplyScrollCommands(); impl->Feedback(seconds);
	if (impl->interaction.Focused() != before) impl->RevealFocus();
}
void Runtime::CancelInput(double seconds) {
    if(!Mutate())return;
 impl->EndTouch(); impl->fling = {}; impl->detailPin.clear();
 impl->pointerPresent = false; impl->SetPointerNavigation(false); impl->interaction.Cancel(); impl->Feedback(seconds); }
void Runtime::ReleaseInputSources() {
    if(!Mutate())return;
 impl->interaction.ReleaseInputSources(); }
bool Runtime::FocusControl(const std::string& id, double seconds) {
    if(!Mutate())return false;

	impl->SetPointerNavigation(false); impl->pendingFocus.clear(); impl->fling = {}; impl->detailPin.clear();
	if(!impl->UpdateInteraction(seconds,false,true))return false; const bool result = impl->interaction.Focus(id);
	if (result) impl->RevealFocus();
	impl->Feedback(seconds); return result;
}
bool Runtime::SetControlEnabled(const std::string& id, bool enabled, double seconds) {
    if(!Mutate())return false;

	if (impl->state.Enabled().contains(id)) return false; // The binding owns this control's availability.
	impl->interactionLayoutDirty=true;
	const bool result = impl->interaction.SetEnabled(id,enabled); impl->Feedback(seconds); return result;
}
bool Runtime::PushModal(const std::string& id, double seconds) {
    if(!Mutate())return false;
 if(!impl->UpdateInteraction(seconds,false,true))return false; const bool result = impl->interaction.PushModal(id); impl->Feedback(seconds); return result; }
bool Runtime::ModalOpen() const { return !impl->interaction.Modal().empty(); }
bool Runtime::PopModal(double seconds) {
    if(!Mutate())return false;
 if(!impl->UpdateInteraction(seconds,false,true))return false; const bool result = impl->interaction.PopModal(); impl->Feedback(seconds); return result; }
bool Runtime::CanDispatchModalBack(const ControlAction& action, double seconds) {
    if(!Mutate())return false;

	if(!impl->UpdateInteraction(seconds,false,true))return false; return impl->interaction.CanDispatchModalBack(action);
}
bool Runtime::CanDispatchControlAction(const ControlAction& action, double seconds) {
    if(!Mutate())return false;

	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) return false;
	// A host source may change after a proposal is queued, including between
	// commands in one application pump. Retire stale value proposals before
	// their side effect, not only when acknowledging the resulting readback.
	if (action.proposal) {
		impl->ReadStateSources();
		if (!impl->stateError.empty()) return false;
	}
	if(!impl->UpdateInteraction(seconds,false,true))return false; return impl->interaction.CanDispatchControlAction(action);
}
std::string Runtime::FocusedControl() const { return impl->interaction.Focused(); }
std::string Runtime::HoveredControl() const { return impl->interaction.Hovered(); }
std::string Runtime::AcceptPrompt(const std::string& fallback, double seconds) {
	const auto id = FocusedControl();
	if (id.empty() || !CanActivateControl(id,seconds)) return {};
	const auto* node = impl->canonical->Model().FindNode(id);
	if (!node || !node->control || !node->control->prompt) return fallback;
	// A verb the state cannot settle keeps the bar's own.
	StateValue value; std::string error;
	if (!EvaluateStateExpression(*node->control->prompt,impl->state.Variables(),value,error)) return fallback;
	const auto* verb = std::get_if<std::string>(&value);
	return verb && verb->starts_with("#str_") ? *verb : fallback;
}
std::optional<ControlState> Runtime::GetControlState(const std::string& id) const { return impl->interaction.State(id); }
std::optional<WidgetViewState> Runtime::GetWidgetState(const std::string& id) const {
    auto value=impl->interaction.Widget(id);
    if(value && value->scroll && (impl->viewport.width<=0 || impl->viewport.height<=0))value->scroll->available=false;
    return value;
}
bool Runtime::OpenChoicePopup(const std::string& id,double seconds) {
    if(!Mutate())return false;

    if (!impl->PrepareChoicePopup(seconds)) return false;
    const bool result = impl->interaction.OpenChoicePopup(id); impl->Feedback(seconds); return result;
}
bool Runtime::CloseChoicePopup(const std::string& id,std::uint64_t expected,double seconds) {
    if(!Mutate())return false;

    if (!impl->PrepareChoicePopup(seconds)) return false;
    const bool result = impl->interaction.CloseChoicePopup(id,expected); impl->Feedback(seconds); return result;
}
bool Runtime::ScrollChoicePopup(const std::string& id,std::uint64_t expected,ScrollStep step,double seconds) {
    if(!Mutate())return false;

    if (!impl->PrepareChoicePopup(seconds)) return false;
    if (!impl->valueView.ChoiceScrollFresh(id,impl->interaction)) return false;
    const auto view = impl->interaction.Widget(id);
    if (!view || !view->scroll) return false;
    // A semantic line/page/end intent has no stored pointer coordinates. Bind
    // it to fresh painted geometry and the caller's exact original opening.
    const bool result = impl->interaction.ScrollChoicePopup(id,expected,view->scroll->geometryToken,step);
    impl->Feedback(seconds); return result;
}
bool Runtime::AcknowledgeControlProposal(const std::string& id, std::uint64_t token, bool accepted) {
    if(!Mutate())return false;

	// Host-backed actions can complete between frames. Observe their actual
	// readback before acknowledging a local editor, including normalization or
	// an intervening external change; success alone cannot set accepted data.
	if (accepted && impl->canonical) {
		impl->ReadStateSources();
		if (!impl->stateError.empty()) accepted = false;
	}
	return impl->interaction.AcknowledgeProposal(id,token,accepted);
}
bool Runtime::BeginNumberEdit(const std::string& id,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.BeginNumberEdit(id,error); impl->Feedback(seconds); return result;
}
bool Runtime::SetNumberSelection(const std::string& id,NumberEditIdentity expected,std::size_t anchor,std::size_t caret,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.SetNumberSelection(id,expected,anchor,caret,error); impl->Feedback(seconds); return result;
}
bool Runtime::ApplyNumberInput(const std::string& id,NumberEditIdentity expected,const TextInputEvent& event,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ApplyNumberInput(id,expected,event,error); impl->Feedback(seconds); return result;
}
bool Runtime::SetNumberNotice(const std::string& id,NumberEditIdentity expected,NumberEditNotice notice,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	return impl->interaction.SetNumberNotice(id,expected,notice,error);
}
bool Runtime::ReplaceNumberSelection(const std::string& id,NumberEditIdentity expected,std::string_view text,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ReplaceNumberSelection(id,expected,text,error); impl->Feedback(seconds); return result;
}
bool Runtime::UndoNumberEdit(const std::string& id,NumberEditIdentity expected,bool redo,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.UndoNumberEdit(id,expected,redo,error); impl->Feedback(seconds); return result;
}
bool Runtime::NumberCommand(const std::string& id,NumberEditIdentity expected,TextEditCommand command,
	bool extendSelection,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	// Update canonical styling and resolve current inherited fonts without Paint
	// or Render. The edit buffer may have changed repeatedly since the last frame.
	ContextClock clock(*impl->services,impl->time,impl->strictPreparation?&impl->preparationFailed:nullptr);
	impl->ApplyMotion(); impl->context->Update();
	impl->context->GetRootElement()->UpdateGeometryForProjection();
	if(!impl->UpdateInteraction(seconds,false,true))return false;
	const auto run=impl->numberView.CommandRun(id,expected,impl->interaction,error); if (!run) return false;
	const auto view=impl->interaction.Widget(id);
	TextEditBoundaryMap boundaries; boundaries.text=run->text;
	for (const auto& caret:run->carets) {
		boundaries.visualCarets.push_back(caret.byteOffset);
		boundaries.deletionStops.push_back(caret.byteOffset);
	}
	TextEditOperation operation;
	if (!EvaluateTextEditCommand(view->number->state,boundaries,command,extendSelection,operation,error)) return false;
	const bool result=impl->interaction.ApplyNumberOperation(id,expected,operation,error);
	impl->Feedback(seconds); return result;
}
bool Runtime::CommitNumberEdit(const std::string& id,NumberEditIdentity expected,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.CommitNumberEdit(id,expected,error); impl->Feedback(seconds); return result;
}
bool Runtime::ResolveNumberConflict(const std::string& id,NumberEditIdentity expected,bool keepDraft,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ResolveNumberConflict(id,expected,keepDraft,error); impl->Feedback(seconds); return result;
}
bool Runtime::CancelNumberEdit(const std::string& id,NumberEditIdentity expected,double seconds) {
    if(!Mutate())return false;

	std::string error; if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.CancelNumberEdit(id,expected); impl->Feedback(seconds); return result;
}
bool Runtime::QueryNumberDrafts(NumberDraftSummary& out,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	return impl->interaction.QueryNumberDrafts(out,error);
}
bool Runtime::DiscardNumberDrafts(const NumberDraftBarrier& expected,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.DiscardNumberDrafts(expected,error); impl->Feedback(seconds); return result;
}
bool Runtime::FocusNumberDraft(const NumberDraftBarrier& expected,const std::string& control,std::string& error,double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	if (!impl->RefreshNumberFocusLayout(error)) return false;
	const bool result=impl->interaction.FocusNumberDraft(expected,control,error);
	if (result) { impl->SetPointerNavigation(false); impl->RevealFocus(); }
	impl->Feedback(seconds); return result;
}
std::optional<NumberTextGeometry> Runtime::GetNumberGeometry(const std::string& id) const {
    if(impl->viewport.width<=0 || impl->viewport.height<=0)return std::nullopt;
	return impl->numberView.Geometry(id,impl->interaction);
}
std::optional<NumberEditorContext> Runtime::QueryNumberEditor(std::string& error, double seconds) {
    if(!Mutate())return {};

	error.clear();
	if (!impl->PrepareNumberEdit(seconds,error)) return std::nullopt;
	const auto id = impl->interaction.Focused();
	const auto view = impl->interaction.Widget(id);
	const auto modal = impl->interaction.ModalToken();
	if (!modal || !impl->interaction.CanActivate(id) || !view || view->role != ControlRole::Number ||
		view->pending || !view->number || !view->number->active || view->number->conflict ||
		view->number->nativePresentation || view->number->nativeUnsettled ||
		!view->number->identity.session || !view->number->identity.revision) return std::nullopt;
	return NumberEditorContext{id,*view->number,modal};
}
bool Runtime::CanActivateControl(const std::string& id, double seconds) {
    if(!Mutate())return false;

	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) return false;
	if(!impl->UpdateInteraction(seconds,false,true))return false;
	return impl->interaction.CanActivate(id);
}
bool Runtime::AttachNumberNative(const TextEditorIdentity& owner, NativeTextIdentity native,
	NativeTextEditorBarrier& out, std::string& error, double seconds) {
    if(!Mutate())return false;

	const auto current = QueryNumberEditor(error,seconds);
	if (!current || current->control != owner.control || current->modalToken != owner.modal ||
		current->editor.identity.session != owner.session || current->editor.identity.revision != owner.revision) return false;
	return impl->interaction.AttachNumberNative(owner.control,current->editor.identity,owner,native,out,error);
}
bool Runtime::RefreshNumberNative(const NativeTextEditorBarrier& expected,
	NativeTextEditorView& out, std::string& error, double seconds) {
    if(!Mutate())return false;

	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	NativeTextEditorView candidate;
	if (!impl->interaction.QueryNumberNative(expected.editor.control,expected.editor,candidate,error) ||
		candidate.barrier != expected) return false;
	out = std::move(candidate); return true;
}
bool Runtime::BeginNumberNativeCollection(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
	NativeTextEditorBarrier& out, std::string& error) {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.BeginNumberNativeCollection(expected,collection,out,error);
}
bool Runtime::IsNumberNativeCurrent(const NativeTextEditorBarrier& expected) const noexcept {
	return impl->canonical && impl->document && impl->interaction.IsNumberNativeCurrent(expected);
}
bool Runtime::ApplyNumberNative(const NativeTextEditorBarrier& expected, const NativeTextOffer& offer,
	NativeTextEditorReceipt& out, std::string& error) {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.ApplyNumberNative(expected,offer,out,error);
}
bool Runtime::CompleteNumberNativeCollection(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
	NativeTextEditorBarrier& out, std::string& error) {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.CompleteNumberNativeCollection(expected,collection,out,error);
}
bool Runtime::SettleNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.SettleNumberNative(expected,out,error);
}
std::unique_ptr<Interaction::NativeSettlement> Runtime::PrepareNumberNativeSettlement(const NativeTextEditorBarrier& expected, std::string& error) {
    if(!Mutate())return {};

	if (!impl->canonical || !impl->document) return {};
	return impl->interaction.PrepareNumberNativeSettlement(expected,error);
}
bool Runtime::PublishNumberNativeSettlement(Interaction::NativeSettlement& prepared, NativeTextEditorReceipt& out) noexcept {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.PublishNumberNativeSettlement(prepared,out);
}
bool Runtime::RetireNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.RetireNumberNative(expected,out,error);
}
NativeTextPresence Runtime::QueryNumberNativePresence(NativeTextIdentity native, const TextEditorIdentity& owner) const noexcept {
    return impl?impl->interaction.QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;
}
bool Runtime::RetireNumberNativeExact(NativeTextIdentity native, const TextEditorIdentity& owner) noexcept {
    if(!Mutate())return false;

	return impl->canonical && impl->document && impl->interaction.RetireNumberNativeExact(native,owner);
}
std::vector<ControlAction> Runtime::TakeActions() {
    if(!Mutate())return {};

	if (impl->interaction.Overflowed()) impl->host.Log(true,"Retained control action queue overflow");
	return impl->interaction.TakeActions();
}

} // namespace openq4::ui
