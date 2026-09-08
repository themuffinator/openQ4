// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#include "Runtime.h"
#include "VectorElement.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace openq4::ui {
namespace {
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

class Renderer final : public Rml::RenderInterface {
public:
	explicit Renderer(Host& h) : host(h) {}
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
		if (vertices.empty() || vertices.size() > 262144 || indices.empty() || indices.size() % 3 != 0) return 0;
		for (const int i : indices) if (i < 0 || static_cast<size_t>(i) >= vertices.size()) return 0;
		auto geometry = std::make_unique<Geometry>();
		geometry->vertices.assign(vertices.begin(), vertices.end());
		geometry->indices.assign(indices.begin(), indices.end());
		return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
	}
	void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<Geometry*>(handle); }
	void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
		if (!handle || (scissorEnabled && (scissor.Width() <= 0 || scissor.Height() <= 0))) return;
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
		if (!scissorEnabled) { host.Draw(vertices, geometry.indices, texture); return; }
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
		if (!indices.empty()) host.Draw(clipped, indices, texture);
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
private:
	Host& host;
	bool scissorEnabled = false, hasTransform = false;
	Rml::Rectanglei scissor;
	Rml::Matrix4f transform;
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
	int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext& shaping, Rml::Character) override {
		const auto& face = *reinterpret_cast<Face*>(handle);
		float width = 0;
		for (Rml::StringIteratorU8 it(string); it; ++it) width += host.GetGlyph(face.family, face.size, static_cast<std::uint32_t>(*it)).advance + shaping.letter_spacing;
		return static_cast<int>(std::lround(width));
	}
	int GenerateString(Rml::RenderManager& manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle,
		Rml::StringView string, Rml::Vector2f position, Rml::ColourbPremultiplied colour, float,
		const Rml::TextShapingContext& shaping, Rml::TexturedMeshList& meshes) override {
		const auto& face = *reinterpret_cast<Face*>(handle);
		float width = 0;
		std::string lastMaterial;
		for (Rml::StringIteratorU8 it(string); it; ++it) {
			const auto glyph = host.GetGlyph(face.family, face.size, static_cast<std::uint32_t>(*it));
			if (!glyph.material.empty() && glyph.width > 0 && glyph.height > 0) {
				if (meshes.empty() || lastMaterial != glyph.material) {
					meshes.push_back({{}, manager.LoadTexture("material:" + glyph.material)});
					lastMaterial = glyph.material;
				}
				auto& mesh = meshes.back().mesh;
				const int base = static_cast<int>(mesh.vertices.size());
				const float x = position.x + width + glyph.left, y = position.y + glyph.top;
				mesh.vertices.insert(mesh.vertices.end(), {
					{{x,y},colour,{glyph.u0,glyph.v0}}, {{x+glyph.width,y},colour,{glyph.u1,glyph.v0}},
					{{x+glyph.width,y+glyph.height},colour,{glyph.u1,glyph.v1}}, {{x,y+glyph.height},colour,{glyph.u0,glyph.v1}}});
				mesh.indices.insert(mesh.indices.end(), {base,base+3,base+1,base+1,base+3,base+2});
			}
			width += glyph.advance + shaping.letter_spacing;
		}
		return static_cast<int>(std::lround(width));
	}
	void ReleaseFontResources() override { faces.clear(); }
	void Shutdown() override { faces.clear(); }
private:
	Host& host;
	std::map<std::string, std::unique_ptr<Face>> faces;
};

Runtime* activeRuntime = nullptr;
} // namespace

float Viewport::DpRatio() const { return Positive(displayScale) * std::clamp(Positive(userScale), .75f, 2.f); }
void Viewport::WindowToDocument(float x, float y, float& outX, float& outY) const {
	outX = x * Positive(pixelDensityX) - originX;
	outY = y * Positive(pixelDensityY) - originY;
}

struct Runtime::Impl {
	explicit Impl(Host& host) : host(host), renderer(host), files(host), system(host), fonts(host) {}
	Host& host;
	Renderer renderer;
	Files files;
	System system;
	Fonts fonts;
	Rml::ElementInstancerGeneric<VectorElement> vectorInstancer;
	Rml::Context* context = nullptr;
	Rml::ElementDocument* document = nullptr;
	std::unique_ptr<Document> canonical;
	Motion motion;
	std::map<PropertyKey,std::string> applied;
	bool initialized = false;
	void ApplyMotion() {
		if (!document || !canonical) return;
		for (const auto& [key,value] : motion.Values()) {
			if (key.second == "opacity") continue; // Resolve ancestry below.
			const auto string = value.type == ValueType::Text ? host.Translate(value.text) : value.Css();
			auto previous = applied.find(key);
			if (previous != applied.end() && previous->second == string) continue;
			auto* element = document->GetElementById(key.first);
			if (!element) continue;
			if (value.type == ValueType::Text) element->SetInnerRML(Rml::StringUtilities::EncodeRml(string));
			else if (!element->SetProperty(key.second,string)) host.Log(true,"Canonical property rejected: "+key.first+"."+key.second);
			applied[key] = string;
		}
		// RmlUi's opacity is inherited as a value, rather than multiplied with
		// an explicitly authored child's opacity. Canonical opacity multiplies
		// ancestry for all paint/text primitives, including custom vector nodes.
		std::vector<std::pair<const Node*,double>> stack{{&canonical->Model().root,1}};
		while (!stack.empty()) {
			const auto [node,parentOpacity] = stack.back(); stack.pop_back();
			const PropertyKey key{node->id,"opacity"};
			const auto authored = motion.Values().find(key);
			const double opacity = parentOpacity*(authored == motion.Values().end() ? 1 : authored->second.data[0]);
			Value effective; effective.data[0] = opacity;
			const auto string = effective.Css();
			const auto previous = applied.find(key);
			if (previous == applied.end() || previous->second != string) {
				if (auto* element = document->GetElementById(node->id)) element->SetProperty("opacity",string);
				applied[key] = string;
			}
			for (const auto& child : node->children) stack.push_back({&child,opacity});
		}
	}
};

Runtime::Runtime(Host& host) : impl(std::make_unique<Impl>(host)) {}
Runtime::~Runtime() { Shutdown(); }
bool Runtime::Initialize() {
	if (impl->initialized) return true;
	if (activeRuntime) return false;
	Rml::SetRenderInterface(&impl->renderer);
	Rml::SetFileInterface(&impl->files);
	Rml::SetSystemInterface(&impl->system);
	Rml::SetFontEngineInterface(&impl->fonts);
	if (!Rml::Initialise()) return false;
	Rml::Factory::RegisterElementInstancer("q4-vector",&impl->vectorInstancer);
	impl->initialized = true;
	activeRuntime = this;
	impl->context = Rml::CreateContext("openq4-retained", {1280,720});
	if (!impl->context) { Shutdown(); return false; }
	return true;
}
void Runtime::Shutdown() {
	if (!impl->initialized) return;
	Rml::Shutdown();
	impl->context = nullptr;
	impl->document = nullptr;
	impl->canonical.reset(); impl->applied.clear(); impl->motion.Reset({});
	impl->initialized = false;
	impl->system.time = 0;
	activeRuntime = nullptr;
	Rml::SetRenderInterface(nullptr);
	Rml::SetFileInterface(nullptr);
	Rml::SetSystemInterface(nullptr);
	Rml::SetFontEngineInterface(nullptr);
}
void Runtime::CloseDocument() {
	impl->canonical.reset(); impl->applied.clear(); impl->motion.Reset({});
	if (!impl->document) return;
	impl->document->Close();
	impl->document = nullptr;
	impl->context->Update();
}
bool Runtime::LoadMarkup(const std::string& markup, const std::string& sourcePath) {
	if (!Initialize()) return false;
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
	impl->motion.Reset(candidate->Model());
	impl->canonical = std::move(candidate);
	std::vector<const Node*> nodes{&impl->canonical->Model().root};
	while (!nodes.empty()) {
		const auto* node = nodes.back(); nodes.pop_back();
		if (node->type == "vector") {
			auto* element = impl->document->GetElementById(node->id);
			if (element) static_cast<VectorElement*>(element)->Configure(node->paths,impl->host);
		}
		for (const auto& child : node->children) nodes.push_back(&child);
	}
	impl->ApplyMotion();
	return true;
}
bool Runtime::PlayTimeline(const std::string& id, double seconds) { return impl->canonical && impl->motion.Play(id,seconds); }
void Runtime::PauseTimeline(const std::string& id, double seconds) { impl->motion.Pause(id,seconds); }
void Runtime::ResumeTimeline(const std::string& id, double seconds) { impl->motion.Resume(id,seconds); }
void Runtime::CancelTimeline(const std::string& id, CancelPolicy policy, double seconds) { impl->motion.Cancel(id,policy,seconds); }
void Runtime::SetReducedMotion(bool enabled, double seconds) { impl->motion.SetReducedMotion(enabled,seconds); }
void Runtime::Frame(const Viewport& viewport, double seconds) {
	if (!impl->context || !impl->document || viewport.width <= 0 || viewport.height <= 0) return;
	if (std::isfinite(seconds)) impl->system.time = std::max(impl->system.time, seconds);
	impl->motion.Advance(impl->system.time);
	impl->ApplyMotion();
	impl->context->SetDimensions({viewport.width, viewport.height});
	impl->context->SetDensityIndependentPixelRatio(viewport.DpRatio());
	impl->context->Update();
	impl->context->Render();
}
bool Runtime::GetBounds(const std::string& id, Bounds& bounds) const {
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
	return element && element->SetProperty(property,value);
}
bool Runtime::SetText(const std::string& id, const std::string& text) {
	if (impl->canonical) return false;
	auto* element = impl->document ? impl->document->GetElementById(id) : nullptr;
	if (!element) return false;
	element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
	return true;
}
bool Runtime::IsLoaded() const { return impl->document != nullptr; }

} // namespace openq4::ui
