// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include "Hud.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Texture.h>

namespace openq4::ui {
class Host;
struct Node;
struct Value;
struct RuntimeStatistics;
// Multiply-blended paths submit through this reserved texture source. The
// renderer resolves it to the host's multiply material, never to an image.
inline constexpr const char* MultiplySource = "q4-blend-multiply";
inline constexpr Rml::TextureHandle MultiplyTexture = ~static_cast<Rml::TextureHandle>(0);
// One fill-only analytic mask is a partition of non-overlapping coverage
// cells. Mark that guarantee without allocating a host texture.
inline constexpr const char* CoverageSource = "q4-mask-coverage";
inline constexpr Rml::TextureHandle CoverageTexture = MultiplyTexture-1;
// A presentation recording may keep image sampling live while its layout,
// geometry and mask remain stationary. The coordinates outlive recordings.
using ImageCoordinates = std::array<double,4>;
class ImageSamplingInterface {
public:
	virtual ~ImageSamplingInterface() = default;
	virtual void SetImageSampling(std::shared_ptr<const ImageCoordinates> coordinates) = 0;
};
class LinearMaskInterface {
public:
	virtual ~LinearMaskInterface() = default;
	virtual bool SubmitLinearMask(const LinearMask& mask, std::shared_ptr<const LinearMask> live) = 0;
};
class VectorGeometry {
public:
	void SetLinearMaskInterface(LinearMaskInterface* value) { linearMasks=value; }
	void Configure(const std::vector<VectorPath>& paths, Host& host, RuntimeStatistics& statistics);
	// Copy authored paths and shared host ownership only; compiled geometry is
	// always invalidated, including when replacing an already rendered copy.
	void CopyArtworkFrom(const VectorGeometry& source);
	void Render(Rml::Element& element, bool inheritOpacity);
	// Validate a recorded paint against a freshly resolved projection. Only
	// an analytic mask may change; ordinary vector coverage must still match.
	bool UpdateReplayProjection(Rml::Element& element, const Rml::Matrix4f& transform);
	bool HitTest(Rml::Element& element, Rml::Vector2f outputPoint);
	bool HasHitArea(Rml::Element& element);
private:
	bool PrepareHitGeometry(Rml::Element& element, Rml::Vector2f& origin);
	std::vector<VectorMesh> hitGeometry;
	std::array<double,6> hitSignature{};
	bool hitPrepared = false, hitValid = false, hitArea = false;
	std::vector<VectorPath> paths;
	// One compiled form of the paths: meshes in output pixels for a
	// signature (size, transform, sub-pixel phase, cache region), and their
	// uploaded geometry at one opacity.
	struct Compilation {
		std::array<double,12> signature{};
		std::vector<VectorMesh> meshes;
		std::vector<PathBlend> blends; // Parallel to meshes.
		std::vector<Rml::Geometry> geometry;
		double opacity = -1;
	};
	// Most recent first. A resting element keeps one exact compilation; an
	// element moving by translation alone keeps a few quarter-pixel phases.
	std::vector<Compilation> cache;
	Rml::Texture multiply, coverage;
	Host* host = nullptr;
	LinearMaskInterface* linearMasks = nullptr;
	RuntimeStatistics* statistics = nullptr;
	std::array<double,12> previous{}; // The exact signature of the last render.
	double previousX = 0, previousY = 0; // Its whole output translation.
	double localScale = 0; // A local mesh's output scale at its last render.
	bool valid = false;
	std::array<float,6> renderInputs{};
	Rml::Matrix4f renderTransform=Rml::Matrix4f::Identity(), cachedTransform=Rml::Matrix4f::Identity();
	Rml::Rectanglei renderScissor;
	Rml::Vector2i renderViewport;
	Rml::Vector2f cachedTranslation;
	bool renderReady=false, renderInheritOpacity=false, cachedLocalMesh=false, cachedPartition=false;
	std::shared_ptr<LinearMask> replayMask;
};
class VectorElement final : public Rml::Element {
public:
	explicit VectorElement(const Rml::String& tag) : Rml::Element(tag) {}
	void Configure(const Node& node, Host& host, RuntimeStatistics& statistics);
	void CopyArtworkFrom(const VectorElement& source);
	void SetCanonicalText(const std::string& text);
	bool SetImageSource(const std::string& source);
	void SetImageCoordinate(const std::string& property, double value);
	// Isolated opacity changes paint only. Canonical presentation callers can
	// resolve this element's non-inherited filter without updating the tree.
	bool SetPresentationOpacity(const std::string& filter, float density, Rml::Vector2f dimensions);
	bool SetPresentationTransform(const Value& transform, float density, Rml::Vector2f dimensions);
	bool UpdateReplayProjection();
	// Only isolated leaf images/text can update a recorded opacity. Crossing
	// zero or one changes the rendered layer topology and requires repaint.
	bool UpdateReplayOpacity(float opacity);
	std::shared_ptr<const float> ReplayOpacity() const { return replayOpacity; }
	bool ReplayOpacityIsText() const { return replayText; }
	void SetImageSamplingInterface(ImageSamplingInterface* value) { imageSampling=value; }
	void SetLinearMaskInterface(LinearMaskInterface* value) { mask.SetLinearMaskInterface(value); }
	void RenderMask() { mask.Render(*this,false); }
	bool AllowsMaskedPoint(Rml::Vector2f point) { return !hasMask || mask.HitTest(*this,point); }
	bool HasMaskArea() { return !hasMask || mask.HasHitArea(*this); }
protected:
	void OnRender() override;
	bool IsRenderSuppressed() override;
private:
	RuntimeStatistics* measurements=nullptr;
	VectorGeometry paint, mask;
	bool legacyText=false, legacyTextClip=false;
	Host* textHost=nullptr;
	std::string text;
	std::vector<HudPrimitive> textItems;
	std::vector<Rml::Geometry> textGeometry;
	std::vector<Rml::Texture> textTextures;
	std::array<float,12> textSignature{};
	std::string layoutText, layoutFamily;
	std::array<float,5> textLayoutSignature{};
	bool textLayoutValid=false;
	void RenderLegacyText();
	void RenderImage();
	bool coordinatesImage = false, additiveImage = false;
	std::string imageSource;
	std::shared_ptr<ImageCoordinates> imageCoordinates=std::make_shared<ImageCoordinates>(ImageCoordinates{0,0,1,1});
	ImageSamplingInterface* imageSampling=nullptr;
	std::shared_ptr<float> replayOpacity;
	bool replayText=false;
	std::array<double,11> imageSignature{};
	Rml::Texture imageTexture;
	Rml::Geometry imageGeometry;
	bool hasMask = false;
	bool replayProjection = false;
};
class VectorMaskInstancer final : public Rml::DecoratorInstancer {
public:
	VectorMaskInstancer();
	Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary&,
		const Rml::DecoratorInstancerInterface&) override;
};
} // namespace openq4::ui
