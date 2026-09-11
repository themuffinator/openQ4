// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Runtime.h"
#include "State.h"
#include "VectorElement.h"
#include "ValueControlView.h"
#include "ScrollbarView.h"
#include "TextRun.h"
#include "NumberControlView.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string_view>
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
	if (!SnapshotFields(source,{"type","unit","text","data"}) || !source["type"].isUInt() || source["type"].asUInt() > unsigned(ValueType::Transform) ||
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

void Clip(std::vector<Vertex>& polygon, int axis, float edge, bool greater) {
	if (polygon.empty()) return;
	std::vector<Vertex> output;
	output.reserve(polygon.size() + 1);
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

struct Geometry {
	std::vector<Rml::Vertex> vertices;
	std::vector<int> indices;
};
struct Filter {
	float opacity = 1;
	std::uint32_t mask = 0;
	int width = 0, height = 0;
	std::uint64_t generation = 0;
};

// The host's composition target IDs are process-wide. A mask may retain its
// lease after the stack layer that produced it has been popped.
struct LayerPool {
	struct Slot { const void* owner = nullptr; int width = 0, height = 0; std::uint64_t frame = 0; };
	std::array<Slot,49> slots{};
};

class Renderer final : public Rml::RenderInterface {
public:
	Renderer(Host& h, RuntimeStatistics& statistics) : host(h), statistics(statistics) {}
	void Attach(LayerPool& value) { pool = &value; }
	void Detach() {
		EndFrame();
		if (pool) for (auto& slot : pool->slots) if (slot.owner == this) {
			host.Log(true,"Retained context leaked a composition lease"); slot.owner = nullptr;
		}
		pool = nullptr;
	}
	void BeginFrame(int width, int height) {
		if (width != viewportWidth || height != viewportHeight) ++viewportGeneration;
		viewportWidth = width; viewportHeight = height; failed = false;
	}
	void EndFrame() {
		if (!layers.empty()) {
			host.Log(true,"Unbalanced retained composition layers"); host.EndLayer(0);
			for (const auto slot : layers) ReleaseSlot(slot);
			layers.clear();
		}
	}
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
		if (vertices.empty() || vertices.size() > 262144 || indices.empty() || indices.size() % 3 != 0) return 0;
		for (const int i : indices) if (i < 0 || static_cast<size_t>(i) >= vertices.size()) return 0;
		auto geometry = std::make_unique<Geometry>();
		geometry->vertices.assign(vertices.begin(), vertices.end());
		geometry->indices.assign(indices.begin(), indices.end());
		++statistics.geometryCompiles; ++statistics.residentGeometryCount;
		statistics.residentGeometryBytes += Bytes(*geometry);
		return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
		if (!handle) return;
		auto* geometry = reinterpret_cast<Geometry*>(handle);
		--statistics.residentGeometryCount; statistics.residentGeometryBytes -= Bytes(*geometry);
		delete geometry;
	}
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
		if (failed || !handle || (scissorEnabled && (scissor.Width() <= 0 || scissor.Height() <= 0))) return;
		const Geometry& geometry = *reinterpret_cast<const Geometry*>(handle);
		std::vector<Vertex> vertices;
		vertices.reserve(geometry.vertices.size());
		for (const auto& source : geometry.vertices) {
			const Rml::Vector4f p(source.position.x + translation.x, source.position.y + translation.y, 0, 1);
			const auto transformed = hasTransform ? transform * p : p;
			if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y) || !std::isfinite(transformed.w) || transformed.w <= 0) return;
			vertices.push_back({transformed.x / transformed.w, transformed.y / transformed.w,
				source.tex_coord.x, source.tex_coord.y,
				source.colour.red / 255.f, source.colour.green / 255.f, source.colour.blue / 255.f, source.colour.alpha / 255.f});
		}
		if (!scissorEnabled) { Submit(vertices, geometry.indices, texture); return; }
		std::vector<Vertex> clipped;
		std::vector<int> indices;
		for (size_t i = 0; i < geometry.indices.size(); i += 3) {
			std::vector<Vertex> polygon = {vertices[geometry.indices[i]], vertices[geometry.indices[i+1]], vertices[geometry.indices[i+2]]};
			Clip(polygon, 0, static_cast<float>(scissor.Left()), true);
			Clip(polygon, 0, static_cast<float>(scissor.Right()), false);
			Clip(polygon, 1, static_cast<float>(scissor.Top()), true);
			Clip(polygon, 1, static_cast<float>(scissor.Bottom()), false);
			if (polygon.size() < 3) continue;
			const int base = static_cast<int>(clipped.size());
			clipped.insert(clipped.end(), polygon.begin(), polygon.end());
			for (int j = 2; j < static_cast<int>(polygon.size()); ++j) {
				indices.insert(indices.end(), {base, base + j - 1, base + j});
			}
		}
		if (!indices.empty()) Submit(clipped, indices, texture);
	}
	Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
		return host.LoadMaterial(source, dimensions.x, dimensions.y);
	}
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
		host.Log(true, "Generated textures are not implemented in the retained renderer spike");
		return 0;
	}
	void ReleaseTexture(Rml::TextureHandle) override {} // Host material manager owns them.
	void EnableScissorRegion(bool enable) override { scissorEnabled = enable; }
	void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }
	void SetTransform(const Rml::Matrix4f* value) override {
		hasTransform = value != nullptr;
		if (value) transform = *value;
	}
	Rml::LayerHandle PushLayer() override {
		const auto slot = AllocateSlot();
		// Keep a nonzero logical handle even when allocation failed, so RmlUi
		// can unwind its stack while this renderer suppresses the failed frame.
		layers.push_back(slot ? slot : static_cast<std::uint32_t>(49+layers.size()));
		++statistics.layerPushes; statistics.peakLayerDepth = std::max<std::uint64_t>(statistics.peakLayerDepth,layers.size());
		return layers.back();
	}
	void PopLayer() override {
		if (layers.empty()) { host.Log(true,"Retained composition layer underflow"); failed = true; return; }
		ReleaseSlot(layers.back()); layers.pop_back();
		// Restore the base even after an allocation failure. No further draws
		// are accepted in a failed frame, preventing paint on the wrong target.
		if (!failed || layers.empty()) host.EndLayer(TopLayer());
	}
	Rml::CompiledFilterHandle CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) override {
		const auto value = parameters.find("value");
		if (name == "opacity" && value != parameters.end()) {
			const float opacity = value->second.Get<float>();
			if (std::isfinite(opacity)) return reinterpret_cast<Rml::CompiledFilterHandle>(new Filter{std::clamp(opacity,0.f,1.f)});
		}
		host.Log(true,"Unsupported retained filter: "+name); return 0;
	}
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
		if (failed || layers.empty()) return 0;
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
		if (!source || source == destination || !ActiveLayer(source) || !ActiveLayer(destination) || mode != Rml::BlendMode::Blend) {
			host.Log(true,"Unsupported retained layer composition"); failed = true; return;
		}
		float opacity = 1;
		std::vector<std::uint32_t> masks;
		for (auto handle : filters) {
			const auto& filter = *reinterpret_cast<const Filter*>(handle);
			opacity *= filter.opacity;
			if (filter.mask) {
				if (filter.generation != viewportGeneration) {
					host.Log(true,"Retained mask snapshot belongs to a different viewport"); failed = true; return;
				}
				masks.push_back(filter.mask);
			}
		}
		const Bounds clip = ClipBounds();
		if (clip.width > 0 && clip.height > 0) {
			std::uint32_t scratch = 0;
			if (!masks.empty()) {
				scratch = AllocateSlot();
				if (!scratch) return;
				// Filters cannot mutate the source: it may be composited again.
				host.CompositeLayer(static_cast<std::uint32_t>(source),scratch,1,clip);
				for (const auto mask : masks) { host.MaskLayer(mask,scratch,clip); ++statistics.maskApplications; }
			}
			host.CompositeLayer(scratch ? scratch : static_cast<std::uint32_t>(source),static_cast<std::uint32_t>(destination),opacity,clip);
			ReleaseSlot(scratch);
			++statistics.layerComposites;
		}
	}
private:
	std::uint32_t TopLayer() const { return layers.empty() ? 0 : layers.back(); }
	bool ActiveLayer(Rml::LayerHandle handle) const {
		return !handle || std::find(layers.begin(),layers.end(),handle) != layers.end();
	}
	std::uint32_t AllocateSlot() {
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
		host.Log(true,"Cannot allocate retained composition layer"); failed = true; return 0;
	}
	void ReleaseSlot(std::uint32_t id) {
		if (pool && id && id < pool->slots.size() && pool->slots[id].owner == this) pool->slots[id].owner = nullptr;
	}
	Bounds ClipBounds() const {
		const float left = scissorEnabled ? std::clamp(float(scissor.Left()),0.f,float(viewportWidth)) : 0;
		const float top = scissorEnabled ? std::clamp(float(scissor.Top()),0.f,float(viewportHeight)) : 0;
		const float right = scissorEnabled ? std::clamp(float(scissor.Right()),left,float(viewportWidth)) : float(viewportWidth);
		const float bottom = scissorEnabled ? std::clamp(float(scissor.Bottom()),top,float(viewportHeight)) : float(viewportHeight);
		return {left,top,right-left,bottom-top};
	}
	static size_t Bytes(const Geometry& geometry) {
		return sizeof(Geometry)+geometry.vertices.capacity()*sizeof(Rml::Vertex)+geometry.indices.capacity()*sizeof(int);
	}
	void Submit(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t texture) {
		++statistics.drawCalls; statistics.submittedVertices += vertices.size(); statistics.submittedIndices += indices.size();
		host.Draw(vertices,indices,texture);
	}
	Host& host;
	RuntimeStatistics& statistics;
	bool scissorEnabled = false, hasTransform = false;
	Rml::Rectanglei scissor;
	Rml::Matrix4f transform;
	int viewportWidth = 0, viewportHeight = 0;
	std::uint64_t viewportGeneration = 0;
	std::vector<std::uint32_t> layers;
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
	double GetElapsedTime() override { return time; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
		host.Log(type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT || type == Rml::Log::LT_WARNING, message);
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

// Transitional font backend using the engine's scalable Quake 4 font service.
// General shaping and arbitrary-size rasterisation are subsequent stage 2 work.
class Fonts final : public Rml::FontEngineInterface {
	struct Face { std::string family; int size; Rml::FontMetrics metrics; };
public:
	explicit Fonts(Host& h) : host(h) {}
	Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle, Rml::Style::FontWeight, int size) override {
		size = std::clamp(size, 1, 512);
		const auto key = family + ":" + std::to_string(size);
		auto& face = faces[key];
		if (!face) {
			const auto metrics = host.GetFontMetrics(family, size);
			if (metrics.lineSpacing <= 0) { faces.erase(key); return 0; }
			face = std::make_unique<Face>();
			face->family = family;
			face->size = size;
			face->metrics = {size, metrics.ascent, metrics.descent, metrics.lineSpacing, metrics.xHeight,
				metrics.descent * .5f, std::max(1.f, size / 16.f), false};
		}
		return reinterpret_cast<Rml::FontFaceHandle>(face.get());
	}
	const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override { return reinterpret_cast<Face*>(handle)->metrics; }
	// Number-field geometry queries this same immutable run as measurement and
	// font submission. Unknown/released handles and non-editable strings fail
	// without dereferencing a stale face or changing the legacy streaming path.
	std::shared_ptr<const TextRun> QueryRun(std::uintptr_t handle, std::string_view string, float letterSpacing) {
		const Face* face = nullptr;
		for (const auto& item : faces) if (reinterpret_cast<std::uintptr_t>(item.second.get()) == handle) { face = item.second.get(); break; }
		if (!face) return {};
		std::string error;
		return runs.Get(handle, string, letterSpacing,
			[&](std::uint32_t codepoint) { return host.GetGlyph(face->family, face->size, codepoint); }, error);
	}
	int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& shaping, Rml::Character) override {
		const std::string_view text(string.begin(), string.size());
		if (const auto run = QueryRun(handle, text, shaping.letter_spacing)) return run->roundedWidth;
		const auto& face = *reinterpret_cast<Face*>(handle);
		const float width = WalkTextRun(text, shaping.letter_spacing,
			[&](std::uint32_t codepoint) { return host.GetGlyph(face.family, face.size, codepoint); });
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
			[&](std::uint32_t codepoint) { return host.GetGlyph(face.family, face.size, codepoint); }, emit);
		return static_cast<int>(std::lround(width));
	}
	void ReleaseFontResources() override { runs.Clear(); faces.clear(); }
	void Shutdown() override { runs.Clear(); faces.clear(); }
private:
	Host& host;
	TextRunCache runs;
	std::map<std::string, std::unique_ptr<Face>> faces;
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
	Services(Host& host, LayerPool& layers) : host(host), files(host), system(host), fonts(host), layers(layers) {}
	Host& host;
	Files files;
	System system;
	Fonts fonts;
	LayerPool& layers;
	std::vector<std::unique_ptr<Backend>> backends;
	Rml::ElementInstancerGeneric<VectorElement> vectorInstancer;
	std::unique_ptr<VectorMaskInstancer> maskInstancer;
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
		Rml::Factory::RegisterElementInstancer("q4-vector",&vectorInstancer);
		Rml::Factory::RegisterElementInstancer("q4-node",&vectorInstancer);
		Rml::Factory::RegisterDecoratorInstancer("q4-mask",maskInstancer.get());
		return true;
	}
	~Services() {
		if (initialized) Rml::Shutdown();
		maskInstancer.reset();
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
	ContextClock(Services& services, double time) : system(services.system), previous(system.time) { system.time = time; }
	~ContextClock() { system.time = previous; }
};
} // namespace

struct Host::Shared { LayerPool layers; };
Host::Host() = default;
Host::~Host() = default;

float Viewport::DpRatio() const { return Positive(displayScale) * std::clamp(Positive(userScale), .75f, 2.f); }
void Viewport::WindowToDocument(float x, float y, float& outX, float& outY) const {
	outX = x * Positive(pixelDensityX) - originX;
	outY = y * Positive(pixelDensityY) - originY;
}

struct Runtime::Impl {
	explicit Impl(Host& host) : host(host) {}
	Host& host;
	std::shared_ptr<Services> services;
	RuntimeStatistics statistics;
	Backend* backend = nullptr;
	RuntimeStatistics& Stats() { return backend ? backend->statistics : statistics; }
	double time = 0;
	std::string contextName, sourcePath;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::unique_ptr<Document> canonical;
	Motion motion;
	State state;
	std::string stateError;
	std::uint64_t appliedStateRevision = 0;
	Interaction interaction;
	ValueControlView valueView;
	ScrollbarView scrollView;
	bool scrollLayoutDirty=false, preserveRestoredScroll=false;
	NumberControlView numberView;
	Viewport viewport;
	std::vector<std::string> controls;
	float pointerX = 0, pointerY = 0;
	float windowPointerX = 0, windowPointerY = 0;
	bool pointerPresent = false;
	bool pointerNavigation = false;
	std::map<PropertyKey,std::string> applied;
	bool initialized = false;
	std::map<std::string,bool> inputAllowed;
	std::string modalError;
	bool PrepareNumberEdit(double seconds, std::string& error) {
		if (!canonical || !document || !std::isfinite(seconds) || seconds < 0) {
			error = "Number editing requires a canonical document and valid presentation time"; return false;
		}
		ReadStateSources();
		if (!stateError.empty()) { error = stateError; return false; }
		UpdateInteraction(seconds); return true;
	}
    bool PrepareChoicePopup(double seconds) {
        if (!canonical || !document || viewport.width <= 0 || viewport.height <= 0 ||
            !std::isfinite(seconds) || seconds < 0) return false;
        ReadStateSources();
        if (!stateError.empty()) return false;
        UpdateInteraction(seconds); return true;
    }
	bool RefreshNumberFocusLayout(std::string& error) {
		if (!context || viewport.width <= 0 || viewport.height <= 0) {
			error = "Number focus requires a usable layout viewport"; return false;
		}
		// A program can hide its modal and request draft focus in the same
		// dispatch. Resolve the resulting pending focus from newly projected
		// layout, before testing the requested field's actual eligibility.
		ContextClock clock(*services,time);
		ApplyMotion(); context->Update();
		context->GetRootElement()->UpdateGeometryForProjection();
		UpdateInteraction(-1,true); return true;
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
		std::function<void(const Node&,bool,const std::string&)> visit = [&](const Node& node, bool inherited, const std::string& parent) {
			const auto display = Property(state,motion,{node.id,"display"});
			const bool visible = inherited && (!display || display->text != "none");
			std::string scope = parent;
			if (visible && node.modal) {
				if (!roots.empty() && roots.back() != parent) valid = false;
				roots.push_back(node.id); scope = node.id;
			}
			for (const auto& child : node.children) visit(child,visible,scope);
		};
		visit(canonical->Model().root,true,{});
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
		modalError = std::move(error); return valid;
	}
	void CollectInputEligibility(const Node& node, bool inherited = true) {
		const auto display = PresentedProperty({node.id,"display"});
		const auto events = PresentedProperty({node.id,"pointer-events"});
		const bool allowed = inherited && (!display || display->text != "none") && (!events || events->text != "none");
		inputAllowed[node.id] = allowed;
		for (const auto& child : node.children) CollectInputEligibility(child,allowed);
	}
	void ApplyControlBindings() {
		SyncModals(); // Capture the opener before enabled/readback changes clear it.
		if (appliedStateRevision == state.Revision()) return;
		std::string error;
		if (!interaction.SetReadbacks(state.ControlValues(),error)) { host.Log(true,error); return; }
		for (const auto& [id,enabled] : state.Enabled()) interaction.SetEnabled(id,enabled);
		appliedStateRevision = state.Revision(); Feedback(time);
	}
	void ReadStateSources() {
		StateValues changes; std::string error;
		for (const auto& [id,declaration] : state.Declarations()) {
			if (declaration.cvar.empty()) continue;
			StateValue value;
			if (!host.ReadCVar(declaration.cvar,declaration.initial.index(),value)) {
				error = "Unavailable or invalid CVar source '"+declaration.cvar+"' for state '"+id+"'"; break;
			}
			changes[id] = std::move(value);
		}
		State candidate = state;
		if (error.empty() && candidate.Set(changes,error,true) && ValidModals(candidate,motion,error)) state = std::move(candidate);
		if (!error.empty() && error != stateError) host.Log(true,error);
		stateError = std::move(error); ApplyControlBindings();
	}
	void Feedback(double seconds) {
		if (std::isfinite(seconds)) time = std::max(time,seconds);
		for (const auto& change : interaction.TakeFeedback()) motion.Play(change.timeline,time);
	}
	bool RevealFocus(bool preserveAuthoredAxes = false) {
		if (!document || !context) return false;
		auto* element = document->GetElementById(interaction.Focused());
		if (!element) return false;
		const float margin = 4.f * viewport.DpRatio();
		if (!std::isfinite(margin) || margin <= 0) return false;
		ContextClock clock(*services,time);
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
			Rml::Array<Rml::Vector2f,4> quad;
			if (!Rml::ElementUtilities::GetBorderBoxQuad(quad,element)) break;
			const auto origin = parent->GetAbsoluteOffset(Rml::BoxArea::Border) +
				Rml::Vector2f(parent->GetClientLeft(),parent->GetClientTop());
			Rml::Vector2f minimum, maximum;
			bool valid = true;
			for (size_t i = 0; i < quad.size(); ++i) {
				auto point = quad[i];
				if (!parent->Project(point) || !std::isfinite(point.x) || !std::isfinite(point.y)) { valid = false; break; }
				point -= origin;
				if (i == 0) minimum = maximum = point;
				else { minimum.x = std::min(minimum.x,point.x); minimum.y = std::min(minimum.y,point.y);
					maximum.x = std::max(maximum.x,point.x); maximum.y = std::max(maximum.y,point.y); }
			}
			if (!valid) break;
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
		return changed;
	}
	std::string HitControl() const {
		if (!pointerPresent || !canonical || !document || pointerX < 0 || pointerY < 0 || pointerX >= viewport.width || pointerY >= viewport.height) return {};
		auto* element = context->GetElementAtPoint({pointerX,pointerY},nullptr,document);
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
        ContextClock clock(*services,time);
        if(!scrollView.ApplyCommands(interaction))return;
        context->Update();context->GetRootElement()->UpdateGeometryForProjection();
        std::string error;scrollLayoutDirty|=scrollView.Sync(interaction,viewport.DpRatio(),false,error);
        if(!error.empty())host.Log(true,error);
    }
    void UpdateInteraction(double seconds = -1, bool freshLayout = false) {
		if (!document || !canonical || viewport.width<=0 || viewport.height<=0) return;
		if (std::isfinite(seconds) && seconds >= 0) time = std::max(time,seconds);
		SyncModals();
		inputAllowed.clear(); CollectInputEligibility(canonical->Model().root);
        // Publish scrollbar metrics and projected bounds as one eligibility update.
        // Restored focus must never be tested against this instance's old bounds.
        std::string scrollError;scrollLayoutDirty|=scrollView.Sync(interaction,viewport.DpRatio(),freshLayout,scrollError,true);
        if(!scrollError.empty())host.Log(true,scrollError);
		std::map<std::string,ControlBounds> bounds;
		for (const auto& id : controls) {
			auto* element = document->GetElementById(id); Rml::Rectanglef rect;
			if (element && inputAllowed.at(id) && element->IsVisible(true) && Rml::ElementUtilities::GetBoundingBox(rect,element,Rml::BoxArea::Border))
				bounds[id] = {rect.Left(),rect.Top(),rect.Width(),rect.Height(),true};
		}
		interaction.SetBounds(bounds,freshLayout);
		const auto hit = HitControl();
		auto* element = pointerPresent && pointerNavigation && pointerX >= 0 && pointerY >= 0 && pointerX < viewport.width && pointerY < viewport.height ?
			context->GetElementAtPoint({pointerX,pointerY},nullptr,document) : nullptr;
		const auto scrollbar=scrollView.PointerPart(element,pointerX,pointerY,interaction);
        if(!scrollbar.control.empty()) interaction.PointerPart(scrollbar.control,scrollbar.invalidProjection?std::nullopt:scrollbar.fraction,{},scrollbar.thumb);
        else {
            const auto part = valueView.PointerPart(element,pointerX,pointerY,interaction);
            if (part.invalidProjection) interaction.Cancel();
            else if(!part.pendingLayout) interaction.PointerPart(part.control.empty() ? (pointerNavigation ? hit : std::string{}) : part.control,part.fraction,part.option,part.thumb,part.choiceTrack);
        }
        ApplyScrollCommands();
		Feedback(time);
	}
	void ApplyMotion() {
		if (!document || !canonical) return;
		for (const auto& [key,animated] : motion.Values()) {
			const auto bound = state.Properties().find(key);
			const auto& value = bound == state.Properties().end() ? animated : bound->second;
			const bool opacity = key.second == "opacity";
			const auto string = opacity ? (value.data[0] < 1 ? "opacity("+value.Css()+")" : "none") :
				value.type == ValueType::Text ? host.Translate(value.text) : value.Css();
			auto previous = applied.find(key);
			if (previous != applied.end() && previous->second == string) continue;
			auto* element = document->GetElementById(key.first);
			if (!element) continue;
			if (value.type == ValueType::Text) element->SetInnerRML(Rml::StringUtilities::EncodeRml(string));
			else if (!element->SetProperty(opacity ? "filter" : key.second,string)) host.Log(true,"Canonical property rejected: "+key.first+"."+key.second);
			applied[key] = string;
		}
	}
};

Runtime::Runtime(Host& host) : impl(std::make_unique<Impl>(host)) {}
Runtime::~Runtime() { Shutdown(); }
bool Runtime::Initialize() {
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
	ContextClock clock(*impl->services,impl->time);
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
	if (!impl->initialized) return;
	impl->valueView.Reset();
	impl->scrollView.Reset();impl->scrollLayoutDirty=impl->preserveRestoredScroll=false;
	impl->numberView.Reset();
	{
		ContextClock clock(*impl->services,impl->time);
		Rml::RemoveContext(impl->contextName);
		impl->services->ReleaseBackend(*impl->backend);
		impl->statistics = impl->backend->statistics;
		impl->backend = nullptr;
	}
	impl->context = nullptr;
	impl->document = nullptr;
	impl->canonical.reset(); impl->applied.clear(); impl->motion.Reset({});
	impl->state = {}; impl->appliedStateRevision = 0; impl->stateError.clear();
	impl->interaction.Reset({}); impl->controls.clear(); impl->pointerPresent = impl->pointerNavigation = false;
	impl->initialized = false;
	impl->time = 0;
	impl->contextName.clear();
	impl->sourcePath.clear();
	impl->services.reset();
}
void Runtime::CloseDocument() {
	impl->sourcePath.clear();
	impl->valueView.Reset();
	impl->scrollView.Reset();impl->scrollLayoutDirty=impl->preserveRestoredScroll=false;
	impl->numberView.Reset();
	impl->canonical.reset(); impl->applied.clear(); impl->motion.Reset({});
	impl->state = {}; impl->appliedStateRevision = 0; impl->stateError.clear();
	impl->interaction.Reset({}); impl->controls.clear(); impl->pointerPresent = impl->pointerNavigation = false;
	if (!impl->document) return;
	ContextClock clock(*impl->services,impl->time);
	impl->document->Close();
	impl->document = nullptr;
	impl->context->Update();
}
bool Runtime::LoadMarkup(const std::string& markup, const std::string& sourcePath) {
	if (!Initialize()) return false;
	ContextClock clock(*impl->services,impl->time);
	// Keep the currently loaded document if parsing a replacement fails.
	auto* document = impl->context->LoadDocumentFromMemory(markup, sourcePath);
	if (!document) return false;
	CloseDocument();
	impl->document = document;
	document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
	return true;
}
bool Runtime::LoadDocument(const std::string& source, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics) {
	auto candidate = std::make_unique<Document>();
	if (!candidate->Load(source,diagnostics)) return false;
	if (!LoadMarkup(candidate->BuildMarkup(),sourcePath)) return false;
	ContextClock clock(*impl->services,impl->time);
	impl->motion.Reset(candidate->Model());
	impl->interaction.Reset(candidate->Model());
	std::string stateError;
	impl->state.Reset(candidate->Model(),stateError); // Initial state was validated by Document::Load.
	impl->canonical = std::move(candidate);
	impl->sourcePath = sourcePath;
	std::vector<const Node*> nodes{&impl->canonical->Model().root};
	while (!nodes.empty()) {
		const auto* node = nodes.back(); nodes.pop_back();
		if (node->control) impl->controls.push_back(node->id);
		// Canonical subtrees preserve paint order when opacity crosses 1 and
		// their temporary filter layer appears or disappears.
		if (auto* element = impl->document->GetElementById(node->id)) element->SetProperty("z-index","0");
		if (auto* element = impl->document->GetElementById(node->id))
			static_cast<VectorElement*>(element)->Configure(*node,impl->host,impl->Stats());
		for (const auto& child : node->children) nodes.push_back(&child);
	}
	if (!impl->valueView.Initialize(impl->canonical->Model(),*impl->document,
		[&](const std::string& text) { return impl->host.Translate(text); },stateError)) {
		diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
	}
	if (!impl->scrollView.Initialize(impl->canonical->Model(),*impl->document,stateError)) {
        diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
    }
    if (!impl->numberView.Initialize(impl->canonical->Model(),*impl->document,
		[&](std::uintptr_t face,std::string_view text,float spacing) { return impl->services->fonts.QueryRun(face,text,spacing); },
		[&](const std::string& text) { return impl->host.Translate(text); },{},stateError)) {
		diagnostics.push_back({"/root",stateError}); CloseDocument(); return false;
	}
	impl->ReadStateSources(); impl->Feedback(impl->time);
	impl->ApplyMotion();
	return true;
}
bool Runtime::PlayTimeline(const std::string& id, double seconds) { return impl->canonical && impl->motion.Play(id,seconds); }
bool Runtime::SetState(const StateValues& changes, std::string& error, double seconds) {
	if (!impl->canonical) { error = "State updates require a canonical document"; return false; }
	State candidate = impl->state;
	if (!candidate.Set(changes,error) || !impl->ValidModals(candidate,impl->motion,error)) return false;
	impl->state = std::move(candidate);
	if (std::isfinite(seconds)) impl->time = std::max(impl->time,seconds);
	impl->ApplyControlBindings(); return true;
}
StateValues Runtime::GetState(bool includeHostSources) const {
	StateValues values;
	for (const auto& [id,value] : impl->state.Variables())
		if (includeHostSources || impl->state.Declarations().at(id).cvar.empty()) values[id] = value;
	return values;
}
std::uint64_t Runtime::StateRevision() const { return impl->state.Revision(); }
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
		const bool authoredModals = interaction.HasAuthoredModals();
		if (authoredModals) {
			motion.Advance(now); std::vector<std::string> roots;
			if (!impl->VisibleModals(impl->state,motion,roots,error) || !interaction.SyncAuthoredModals(roots,error)) return false;
		}
		interaction.Cancel();
		// A press/hover is transient. Preserve a continuous transition toward
		// its persistent focus/default feedback instead of reviving pressed ink.
		for (const auto& feedback : interaction.TakeFeedback()) motion.Play(feedback.timeline,now);
		const auto playback = motion.Capture(now);
		if (!motion.Restore(playback,now,error,true)) return false;
		const auto input = interaction.Capture();
		Json::Value root(Json::objectValue);
		root["format"] = "openq4-ui-instance"; root["version"] = authoredModals ? 4 : widgets.widgets.empty() ? 2 : 3;
		auto& identity = root["document"];
		identity["version"] = 1; identity["id"] = impl->canonical->Model().id;
		identity["path"] = impl->sourcePath; identity["source"] = impl->canonical->Source();
		root["application"] = Json::Value(Json::objectValue);
		for (const auto& [id,value] : GetState(false))
			std::visit([&](const auto& primitive) { root["application"][id] = primitive; },value);
		if (authoredModals) {
			root["hostSources"] = Json::Value(Json::objectValue);
			for (const auto& [id,declaration] : impl->state.Declarations()) if (!declaration.cvar.empty())
				std::visit([&](const auto& primitive) { root["hostSources"][id] = primitive; },impl->state.Variables().at(id));
		}
		auto& aliasState = root["presentationState"];
		aliasState["variables"] = Json::Value(Json::objectValue);
		for (const auto& [id,cell] : impl->state.Presentation().variables) {
			auto& item = aliasState["variables"][id]; item["value"] = SnapshotPresentationValue(cell.value);
			item["expressionDisabled"] = cell.expressionDisabled; item["pending"] = cell.pending;
		}
		aliasState["properties"] = Json::Value(Json::arrayValue);
		for (const auto& [key,cell] : impl->state.Presentation().properties) {
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
		for (const auto& [id,enabled] : input.enabled) if (!impl->state.Enabled().contains(id)) semantics["enabled"][id] = enabled;
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
		std::string candidate = Json::writeString(writer,root);
		if (candidate.size() > MaxSnapshotBytes) { error = "Instance snapshot exceeds the 128 MiB limit"; return false; }
		snapshot.swap(candidate); return true;
	} catch (const std::exception& problem) { error = std::string("Cannot save instance snapshot: ")+problem.what(); return false; }
}
bool Runtime::RestoreSnapshot(const std::string& snapshot, std::string& error, double seconds) {
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
			identity["id"] != impl->canonical->Model().id || identity["path"] != impl->sourcePath || identity["source"] != impl->canonical->Source())
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
		for (const auto& [id,declaration] : impl->state.Declarations()) if (!declaration.cvar.empty()) {
			StateValue value;
			if (!impl->host.ReadCVar(declaration.cvar,declaration.initial.index(),value)) { error = "Unavailable CVar source during restore: "+declaration.cvar; return false; }
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
		impl->time = now; impl->pointerPresent = impl->pointerNavigation = false; impl->applied.clear();
        impl->preserveRestoredScroll=widgets.version==3;
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
	error.clear();
	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) { error = "Event requires a canonical document and valid presentation time"; return false; }
	const auto& model = impl->canonical->Model();
	StateValues sources;
	for (const auto& [id,declaration] : model.state) {
		if (declaration.cvar.empty()) continue;
		StateValue value;
		if (!impl->host.ReadCVar(declaration.cvar,declaration.initial.index(),value)) {
			error = "Unavailable CVar source '"+declaration.cvar+"' at event entry"; return false;
		}
		sources[id] = std::move(value);
	}
	State state = impl->state;
	if (!state.SetCombined(application,sources,error)) return false;
	const double now = std::max(impl->time,seconds);
	EventResult candidate;
	if (!EvaluateEvent(model,state,impl->motion,name,now,candidate,error,validate,maxActions)) return false;
	if (!impl->ValidModals(candidate.state,candidate.motion,error)) return false;
	EventEffects published{std::move(candidate.stateChanges),std::move(candidate.actions)};
	impl->state = std::move(candidate.state); impl->motion = std::move(candidate.motion); impl->time = now;
	impl->stateError.clear(); impl->ApplyControlBindings(); impl->UpdateInteraction(now);
	effects = std::move(published); return true;
}
bool Runtime::ResolveAction(const std::string& id, ActionInvocation& invocation, std::string& error, const StateValue* input) const {
	if (!impl->canonical) { error = "Action requires a canonical document"; return false; }
	return impl->canonical->Model().ResolveAction(id,impl->state.Variables(),invocation,error,
		MakePresentationLookup(impl->canonical->Model(),impl->state,impl->motion),input);
}
void Runtime::PauseTimeline(const std::string& id, double seconds) { impl->motion.Pause(id,seconds); }
void Runtime::ResumeTimeline(const std::string& id, double seconds) { impl->motion.Resume(id,seconds); }
void Runtime::CancelTimeline(const std::string& id, CancelPolicy policy, double seconds) { impl->motion.Cancel(id,policy,seconds); }
void Runtime::SetReducedMotion(bool enabled, double seconds) { impl->motion.SetReducedMotion(enabled,seconds); }
void Runtime::Frame(const Viewport& viewport, double seconds) {
	auto& statistics = impl->Stats();
	const auto residentCount = statistics.residentGeometryCount, residentBytes = statistics.residentGeometryBytes;
	statistics = {};
	statistics.residentGeometryCount = residentCount; statistics.residentGeometryBytes = residentBytes;
	if (!impl->context || !impl->document) return;
	if (viewport.width <= 0 || viewport.height <= 0) {
        // Keep invalid dimensions visible to input; old Rml boxes must not be
        // republished before a later valid Frame measures layout again.
        impl->viewport.width=viewport.width;impl->viewport.height=viewport.height;
		impl->interaction.InvalidateLayout(); impl->interaction.Cancel(); impl->Feedback(seconds); return;
	}
	const bool viewportChanged = impl->viewport.width != viewport.width || impl->viewport.height != viewport.height ||
		impl->viewport.DpRatio() != viewport.DpRatio();
	impl->scrollView.PreserveDensity(viewport.DpRatio());
	impl->viewport = viewport;
	if (impl->pointerPresent) viewport.WindowToDocument(impl->windowPointerX,impl->windowPointerY,impl->pointerX,impl->pointerY);
	const auto start = std::chrono::steady_clock::now();
	if (std::isfinite(seconds)) impl->time = std::max(impl->time, seconds);
	ContextClock clock(*impl->services,impl->time);
	impl->ReadStateSources();
	impl->motion.Advance(impl->time);
	impl->ApplyMotion();
	impl->context->SetDimensions({viewport.width, viewport.height});
	impl->context->SetDensityIndependentPixelRatio(viewport.DpRatio());
	impl->context->Update();
	// Owned widget wrappers remain derived layout. Bounded settling lets a
	// newly opened popup measure its authored rows before its first rendering.
	for (unsigned pass = 0; pass < 3; ++pass) {
		impl->context->GetRootElement()->UpdateGeometryForProjection();
		const auto before = impl->interaction.Focused();
		impl->UpdateInteraction(-1,true);
		const bool focusChanged = before != impl->interaction.Focused();
		// Preserve restored/reflowed offsets only on axes owned by authored bars.
		// Other axes and legacy ancestors still reveal the current control.
		const bool preserveAuthoredAxes = impl->preserveRestoredScroll || (viewportChanged && !focusChanged);
		const bool revealed = (focusChanged || (pass == 0 && (viewportChanged || impl->preserveRestoredScroll))) && impl->RevealFocus(preserveAuthoredAxes);
		if (focusChanged) impl->ApplyMotion();
		const auto* focusedNode = impl->canonical ? impl->canonical->Model().FindNode(impl->interaction.Focused()) : nullptr;
		auto* focusedNumber = focusedNode && focusedNode->control && focusedNode->control->role == ControlRole::Number ?
			impl->document->GetElementById(focusedNode->id) : nullptr;
		const auto numberSize = focusedNumber ? focusedNumber->GetBox().GetSize(Rml::BoxArea::Border) : Rml::Vector2f{};
		bool painted = impl->valueView.Paint(impl->interaction,impl->state.ControlValues(),viewport.width,viewport.height,viewport.DpRatio(),
			[&](const std::string& id) { const auto value = impl->PresentedProperty({id,"opacity"}); return value ? value->data[0] : 1.0; });
		painted |= std::exchange(impl->scrollLayoutDirty,false);
		painted |= impl->numberView.Paint(impl->interaction,viewport.DpRatio(),impl->time,impl->motion.ReducedMotion());
		if (!painted && !focusChanged && !revealed) break;
		impl->context->Update();
		if (focusedNumber && focusedNumber->GetBox().GetSize(Rml::BoxArea::Border) != numberSize) {
			// Validation can grow the focused field without changing focus.
			// Reveal its new extent once; unchanged frames preserve user scroll.
			impl->RevealFocus(preserveAuthoredAxes); impl->context->Update();
		}
	}
	impl->preserveRestoredScroll=false;
	const auto updated = std::chrono::steady_clock::now();
	impl->backend->renderer.BeginFrame(viewport.width,viewport.height);
	impl->context->Render();
	impl->backend->renderer.EndFrame();
	// RmlUi resolves transform state while rendering. Hit/navigation bounds
	// therefore follow the just-presented frame, not stale transform matrices.
	impl->UpdateInteraction(-1,true);
	const auto end = std::chrono::steady_clock::now();
	statistics.updateMilliseconds = std::chrono::duration<double,std::milli>(updated-start).count();
	statistics.renderMilliseconds = std::chrono::duration<double,std::milli>(end-updated).count();
	statistics.frameMilliseconds = std::chrono::duration<double,std::milli>(end-start).count();
}
bool Runtime::GetBounds(const std::string& id, Bounds& bounds) const {
    if(impl->viewport.width<=0 || impl->viewport.height<=0)return false;
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	const auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
	const auto size = element->GetBox().GetSize(Rml::BoxArea::Border);
	bounds = {position.x,position.y,size.x,size.y};
	return true;
}
bool Runtime::SetProperty(const std::string& id, const std::string& property, const std::string& value) {
	if (impl->canonical) return false; // Edit the canonical source transactionally.
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	ContextClock clock(*impl->services,impl->time);
	return element->SetProperty(property,value);
}
bool Runtime::SetText(const std::string& id, const std::string& text) {
	if (impl->canonical) return false;
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	ContextClock clock(*impl->services,impl->time);
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
	impl->pointerNavigation = true;
	impl->pointerPresent = std::isfinite(x) && std::isfinite(y);
	impl->windowPointerX = x; impl->windowPointerY = y;
	impl->viewport.WindowToDocument(x,y,impl->pointerX,impl->pointerY);
	impl->UpdateInteraction(seconds); impl->Feedback(seconds);
}
void Runtime::PointerButton(bool down, double seconds) { impl->pointerNavigation = true; impl->UpdateInteraction(seconds); impl->interaction.Pointer(down); impl->ApplyScrollCommands(); impl->Feedback(seconds); }
void Runtime::PointerWheel(int rows, double seconds) {
	impl->UpdateInteraction(seconds);
	if (!rows) return;
	for (const auto& id : impl->controls) {
		const auto widget = impl->interaction.Widget(id);
		if (!widget || !widget->popupOpen) continue;
		// One bounded row step per supplied wheel event; platform adapters do not
		// turn accumulated input or a stalled frame into a navigation burst.
		// Keep this selection through layout and subsequent wheel events. The
		// stationary pointer must not reselect its old row on the next frame;
		// an actual pointer move or button event restores pointer navigation.
		impl->pointerNavigation = false;
        const auto* node=impl->canonical->Model().FindNode(id);
        const auto* choice=node&&node->control?std::get_if<ChoiceSpec>(&node->control->widget):nullptr;
        if(choice&&choice->scrollbar) {
            if(impl->valueView.ChoiceScrollFresh(id,impl->interaction))impl->interaction.ChoiceScrollPulse(id,rows>0?ScrollStep::LineForward:ScrollStep::LineBackward);
        } else impl->interaction.NavigationPulse(rows > 0 ? MenuInput::Down : MenuInput::Up);
		impl->Feedback(seconds); return;
	}
	if (!impl->pointerPresent || !impl->context || !impl->document || impl->pointerX < 0 || impl->pointerY < 0 ||
		impl->pointerX >= impl->viewport.width || impl->pointerY >= impl->viewport.height) return;
	if (auto* hit = impl->context->GetElementAtPoint({impl->pointerX,impl->pointerY},nullptr,impl->document)) {
        const auto target=impl->scrollView.WheelTarget(hit,impl->interaction);
        if(!target.empty()) {
            impl->interaction.ScrollPulse(target,rows>0?ScrollStep::LineForward:ScrollStep::LineBackward);
            impl->ApplyScrollCommands();impl->Feedback(seconds);return;
        }
        if (auto* scroll = hit->GetClosestScrollableContainer(); scroll && impl->interaction.AllowsNode(scroll->GetId()))
            scroll->SetScrollTop(scroll->GetScrollTop()+(rows > 0 ? 36.f : -36.f)*impl->viewport.DpRatio());
    }
}
void Runtime::MenuAction(MenuInput input, bool down, double seconds) {
    if(impl->viewport.width<=0 || impl->viewport.height<=0) {impl->interaction.QuarantineInput(input,down);return;}
	if (down && input != MenuInput::Back && !impl->interaction.CapturedPointerControl().empty()) impl->interaction.Cancel();
	const auto before = impl->interaction.Focused();
	impl->pointerNavigation = false; impl->UpdateInteraction(seconds); impl->interaction.Input(input,down); impl->ApplyScrollCommands(); impl->Feedback(seconds);
	if (impl->interaction.Focused() != before) impl->RevealFocus();
}
void Runtime::CancelInput(double seconds) { impl->pointerPresent = impl->pointerNavigation = false; impl->interaction.Cancel(); impl->Feedback(seconds); }
void Runtime::ReleaseInputSources() { impl->interaction.ReleaseInputSources(); }
bool Runtime::FocusControl(const std::string& id, double seconds) {
	impl->pointerNavigation = false;
	impl->UpdateInteraction(seconds); const bool result = impl->interaction.Focus(id);
	if (result) impl->RevealFocus();
	impl->Feedback(seconds); return result;
}
bool Runtime::SetControlEnabled(const std::string& id, bool enabled, double seconds) {
	if (impl->state.Enabled().contains(id)) return false; // The binding owns this control's availability.
	const bool result = impl->interaction.SetEnabled(id,enabled); impl->Feedback(seconds); return result;
}
bool Runtime::PushModal(const std::string& id, double seconds) { impl->UpdateInteraction(seconds); const bool result = impl->interaction.PushModal(id); impl->Feedback(seconds); return result; }
bool Runtime::PopModal(double seconds) { impl->UpdateInteraction(seconds); const bool result = impl->interaction.PopModal(); impl->Feedback(seconds); return result; }
bool Runtime::CanDispatchModalBack(const ControlAction& action, double seconds) {
	impl->UpdateInteraction(seconds); return impl->interaction.CanDispatchModalBack(action);
}
bool Runtime::CanDispatchControlAction(const ControlAction& action, double seconds) {
	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) return false;
	// A host source may change after a proposal is queued, including between
	// commands in one application pump. Retire stale numeric proposals before
	// their side effect, not only when acknowledging the resulting readback.
	if (action.editSession) {
		impl->ReadStateSources();
		if (!impl->stateError.empty()) return false;
	}
	impl->UpdateInteraction(seconds); return impl->interaction.CanDispatchControlAction(action);
}
std::string Runtime::FocusedControl() const { return impl->interaction.Focused(); }
std::optional<ControlState> Runtime::GetControlState(const std::string& id) const { return impl->interaction.State(id); }
std::optional<WidgetViewState> Runtime::GetWidgetState(const std::string& id) const {
    auto value=impl->interaction.Widget(id);
    if(value && value->scroll && (impl->viewport.width<=0 || impl->viewport.height<=0))value->scroll->available=false;
    return value;
}
bool Runtime::OpenChoicePopup(const std::string& id,double seconds) {
    if (!impl->PrepareChoicePopup(seconds)) return false;
    const bool result = impl->interaction.OpenChoicePopup(id); impl->Feedback(seconds); return result;
}
bool Runtime::CloseChoicePopup(const std::string& id,std::uint64_t expected,double seconds) {
    if (!impl->PrepareChoicePopup(seconds)) return false;
    const bool result = impl->interaction.CloseChoicePopup(id,expected); impl->Feedback(seconds); return result;
}
bool Runtime::ScrollChoicePopup(const std::string& id,std::uint64_t expected,ScrollStep step,double seconds) {
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
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.BeginNumberEdit(id,error); impl->Feedback(seconds); return result;
}
bool Runtime::SetNumberSelection(const std::string& id,NumberEditIdentity expected,std::size_t anchor,std::size_t caret,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.SetNumberSelection(id,expected,anchor,caret,error); impl->Feedback(seconds); return result;
}
bool Runtime::ApplyNumberInput(const std::string& id,NumberEditIdentity expected,const TextInputEvent& event,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ApplyNumberInput(id,expected,event,error); impl->Feedback(seconds); return result;
}
bool Runtime::SetNumberNotice(const std::string& id,NumberEditIdentity expected,NumberEditNotice notice,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	return impl->interaction.SetNumberNotice(id,expected,notice,error);
}
bool Runtime::ReplaceNumberSelection(const std::string& id,NumberEditIdentity expected,std::string_view text,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ReplaceNumberSelection(id,expected,text,error); impl->Feedback(seconds); return result;
}
bool Runtime::UndoNumberEdit(const std::string& id,NumberEditIdentity expected,bool redo,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.UndoNumberEdit(id,expected,redo,error); impl->Feedback(seconds); return result;
}
bool Runtime::NumberCommand(const std::string& id,NumberEditIdentity expected,TextEditCommand command,
	bool extendSelection,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	// Update canonical styling and resolve current inherited fonts without Paint
	// or Render. The edit buffer may have changed repeatedly since the last frame.
	ContextClock clock(*impl->services,impl->time);
	impl->ApplyMotion(); impl->context->Update();
	impl->context->GetRootElement()->UpdateGeometryForProjection();
	impl->UpdateInteraction(seconds);
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
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.CommitNumberEdit(id,expected,error); impl->Feedback(seconds); return result;
}
bool Runtime::ResolveNumberConflict(const std::string& id,NumberEditIdentity expected,bool keepDraft,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.ResolveNumberConflict(id,expected,keepDraft,error); impl->Feedback(seconds); return result;
}
bool Runtime::CancelNumberEdit(const std::string& id,NumberEditIdentity expected,double seconds) {
	std::string error; if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.CancelNumberEdit(id,expected); impl->Feedback(seconds); return result;
}
bool Runtime::QueryNumberDrafts(NumberDraftSummary& out,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	return impl->interaction.QueryNumberDrafts(out,error);
}
bool Runtime::DiscardNumberDrafts(const NumberDraftBarrier& expected,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	const bool result=impl->interaction.DiscardNumberDrafts(expected,error); impl->Feedback(seconds); return result;
}
bool Runtime::FocusNumberDraft(const NumberDraftBarrier& expected,const std::string& control,std::string& error,double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	if (!impl->RefreshNumberFocusLayout(error)) return false;
	const bool result=impl->interaction.FocusNumberDraft(expected,control,error);
	if (result) { impl->pointerNavigation = false; impl->RevealFocus(); }
	impl->Feedback(seconds); return result;
}
std::optional<NumberTextGeometry> Runtime::GetNumberGeometry(const std::string& id) const {
    if(impl->viewport.width<=0 || impl->viewport.height<=0)return std::nullopt;
	return impl->numberView.Geometry(id,impl->interaction);
}
std::optional<NumberEditorContext> Runtime::QueryNumberEditor(std::string& error, double seconds) {
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
	if (!impl->canonical || !std::isfinite(seconds) || seconds < 0) return false;
	impl->UpdateInteraction(seconds);
	return impl->interaction.CanActivate(id);
}
bool Runtime::AttachNumberNative(const TextEditorIdentity& owner, NativeTextIdentity native,
	NativeTextEditorBarrier& out, std::string& error, double seconds) {
	const auto current = QueryNumberEditor(error,seconds);
	if (!current || current->control != owner.control || current->modalToken != owner.modal ||
		current->editor.identity.session != owner.session || current->editor.identity.revision != owner.revision) return false;
	return impl->interaction.AttachNumberNative(owner.control,current->editor.identity,owner,native,out,error);
}
bool Runtime::RefreshNumberNative(const NativeTextEditorBarrier& expected,
	NativeTextEditorView& out, std::string& error, double seconds) {
	if (!impl->PrepareNumberEdit(seconds,error)) return false;
	NativeTextEditorView candidate;
	if (!impl->interaction.QueryNumberNative(expected.editor.control,expected.editor,candidate,error) ||
		candidate.barrier != expected) return false;
	out = std::move(candidate); return true;
}
bool Runtime::BeginNumberNativeCollection(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
	NativeTextEditorBarrier& out, std::string& error) {
	return impl->canonical && impl->document && impl->interaction.BeginNumberNativeCollection(expected,collection,out,error);
}
bool Runtime::IsNumberNativeCurrent(const NativeTextEditorBarrier& expected) const noexcept {
	return impl->canonical && impl->document && impl->interaction.IsNumberNativeCurrent(expected);
}
bool Runtime::ApplyNumberNative(const NativeTextEditorBarrier& expected, const NativeTextOffer& offer,
	NativeTextEditorReceipt& out, std::string& error) {
	return impl->canonical && impl->document && impl->interaction.ApplyNumberNative(expected,offer,out,error);
}
bool Runtime::CompleteNumberNativeCollection(const NativeTextEditorBarrier& expected, const NativeTextCollection& collection,
	NativeTextEditorBarrier& out, std::string& error) {
	return impl->canonical && impl->document && impl->interaction.CompleteNumberNativeCollection(expected,collection,out,error);
}
bool Runtime::SettleNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
	return impl->canonical && impl->document && impl->interaction.SettleNumberNative(expected,out,error);
}
std::unique_ptr<Interaction::NativeSettlement> Runtime::PrepareNumberNativeSettlement(const NativeTextEditorBarrier& expected, std::string& error) {
	if (!impl->canonical || !impl->document) return {};
	return impl->interaction.PrepareNumberNativeSettlement(expected,error);
}
bool Runtime::PublishNumberNativeSettlement(Interaction::NativeSettlement& prepared, NativeTextEditorReceipt& out) noexcept {
	return impl->canonical && impl->document && impl->interaction.PublishNumberNativeSettlement(prepared,out);
}
bool Runtime::RetireNumberNative(const NativeTextEditorBarrier& expected, NativeTextEditorReceipt& out, std::string& error) {
	return impl->canonical && impl->document && impl->interaction.RetireNumberNative(expected,out,error);
}
NativeTextPresence Runtime::QueryNumberNativePresence(NativeTextIdentity native, const TextEditorIdentity& owner) const noexcept {
    return impl?impl->interaction.QueryNumberNativePresence(native,owner):NativeTextPresence::BusyOrUnknown;
}
bool Runtime::RetireNumberNativeExact(NativeTextIdentity native, const TextEditorIdentity& owner) noexcept {
	return impl->canonical && impl->document && impl->interaction.RetireNumberNativeExact(native,owner);
}
std::vector<ControlAction> Runtime::TakeActions() {
	if (impl->interaction.Overflowed()) impl->host.Log(true,"Retained control action queue overflow");
	return impl->interaction.TakeActions();
}

} // namespace openq4::ui
