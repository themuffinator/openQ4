// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Document.h"
#include "Motion.h"

namespace openq4::ui {

// The retained library owns layout in physical pixels. Engine conversion to
// its 640x480 submission space happens only at the final drawing boundary.
struct Viewport {
	int width = 1280, height = 720;
	float displayScale = 1, userScale = 1;
	float pixelDensityX = 1, pixelDensityY = 1;
	float originX = 0, originY = 0;
	float DpRatio() const;
	void WindowToDocument(float x, float y, float& outX, float& outY) const;
};

struct Vertex {
	float x = 0, y = 0, u = 0, v = 0;
	// Premultiplied sRGB, kept floating point while clipping.
	float r = 1, g = 1, b = 1, a = 1;
};

struct Bounds { float x = 0, y = 0, width = 0, height = 0; };
struct FontMetrics { float ascent = 0, descent = 0, lineSpacing = 0, xHeight = 0; };
struct Glyph {
	float advance = 0, left = 0, top = 0, width = 0, height = 0;
	float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	std::string material;
};
// CPU submission measurements, not GPU timings. Counts reset for each Frame;
// resident geometry counts track resource lifetime, including hidden elements.
struct RuntimeStatistics {
	double frameMilliseconds = 0, updateMilliseconds = 0, renderMilliseconds = 0;
	double vectorCompileMilliseconds = 0, vectorUploadMilliseconds = 0;
	std::uint64_t vectorElements = 0, vectorPathsCompiled = 0, vectorCacheHits = 0, vectorUploads = 0;
	std::uint64_t geometryCompiles = 0, drawCalls = 0, submittedVertices = 0, submittedIndices = 0;
	std::uint64_t residentGeometryCount = 0, residentGeometryBytes = 0;
	std::uint64_t visibleVectorCacheBytes = 0;
	std::uint64_t layerPushes = 0, layerComposites = 0, peakLayerDepth = 0;
	std::uint64_t maskSnapshots = 0, maskApplications = 0, peakLayerTargets = 0;
};

class Host {
public:
	virtual ~Host() = default;
	virtual bool ReadFile(const std::string& path, std::string& contents) = 0;
	virtual std::string Translate(const std::string& text) = 0;
	virtual void Log(bool error, const std::string& message) = 0;
	virtual std::uintptr_t LoadMaterial(const std::string& name, int& width, int& height) = 0;
	virtual void Draw(const std::vector<Vertex>& vertices, const std::vector<int>& indices, std::uintptr_t material) = 0;
	// Physical-pixel, transparent, premultiplied targets. The runtime owns slot
	// leases (including immutable mask snapshots); zero is the caller's output.
	// BeginLayer clears a reused slot. EndLayer only changes the active target.
	virtual bool BeginLayer(std::uint32_t id, int width, int height) = 0;
	virtual void CompositeLayer(std::uint32_t source, std::uint32_t destination, float opacity, const Bounds& clip) = 0;
	// Multiply destination RGBA by the mask alpha, including zero outside its
	// paths. Source and destination are distinct; neither is the base surface.
	virtual void MaskLayer(std::uint32_t mask, std::uint32_t destination, const Bounds& clip) = 0;
	virtual void EndLayer(std::uint32_t restore) = 0;
	virtual FontMetrics GetFontMetrics(const std::string& family, int pixelSize) = 0;
	virtual Glyph GetGlyph(const std::string& family, int pixelSize, std::uint32_t codepoint) = 0;
};

// One owner of RmlUi's process-wide services; many documents/contexts will
// share this owner. Runtime contains no platform window, GL or input capture.
class Runtime {
public:
	explicit Runtime(Host& host);
	~Runtime();
	Runtime(const Runtime&) = delete;
	Runtime& operator=(const Runtime&) = delete;
	bool Initialize();
	void Shutdown();
	// Integration-spike entry point, not the canonical editor serialization.
	bool LoadMarkup(const std::string& markup, const std::string& sourcePath);
	bool LoadDocument(const std::string& source, const std::string& sourcePath, std::vector<Diagnostic>& diagnostics);
	bool PlayTimeline(const std::string& id, double monotonicSeconds);
	void PauseTimeline(const std::string& id, double monotonicSeconds);
	void ResumeTimeline(const std::string& id, double monotonicSeconds);
	void CancelTimeline(const std::string& id, CancelPolicy policy, double monotonicSeconds);
	void SetReducedMotion(bool enabled, double monotonicSeconds);
	void CloseDocument();
	void Frame(const Viewport& viewport, double monotonicSeconds);
	bool GetBounds(const std::string& id, Bounds& bounds) const;
	bool SetProperty(const std::string& id, const std::string& property, const std::string& value);
	bool SetText(const std::string& id, const std::string& text);
	bool IsLoaded() const;
	RuntimeStatistics Statistics() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
