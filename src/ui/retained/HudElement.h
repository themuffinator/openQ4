// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Hud.h"
#include "Vector.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Texture.h>
#include <map>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

namespace openq4::ui {
struct Node;
struct RuntimeStatistics;
// Presentation-only child of the editable artwork catalog. It owns neither
// scripts nor input. All paths compile before publication, so a failed frame
// can fall back without leaving a partly drawn HUD on screen.
class HudElement final : public Rml::Element {
public:
	explicit HudElement(const Rml::String& tag) : Rml::Element(tag) {}
	bool Prepare(const std::vector<HudPrimitive>&, const Node&, int width, int height,
		RuntimeStatistics&, std::string& error, bool& changed);
protected:
	void OnRender() override;
private:
	// An item's compiled coverage, one mesh per path, in output pixels less
	// the entry's offset.
	struct Compiled {
		std::vector<VectorMesh> meshes;
		std::vector<PathBlend> blends;
	};
	struct Entry {
		HudPrimitive previous;
		int width = 0, height = 0;
		std::shared_ptr<const Compiled> compiled;
		VectorPoint offset;
		std::vector<Rml::Geometry> geometry;
		Rml::Texture texture;
		bool valid = false;
		unsigned settled = 0;	// passes in a row it came through unchanged
		bool dirty = true;		// changed since the batch holding it was built
	};
	std::vector<Entry> entries;
	// The game's art windows by shape and by where they fall within a pixel,
	// to a quarter pixel: a window that only moved draws its compiled coverage
	// again at a whole-pixel offset. The least recently drawn go first.
	struct WindowKey {
		std::string artwork;
		std::array<float,4> uv{};
		std::array<long,8> shape{};
		int width = 0, height = 0;
		bool operator<(const WindowKey& other) const {
			return std::tie(artwork,uv,shape,width,height)<std::tie(other.artwork,other.uv,other.shape,other.width,other.height);
		}
	};
	struct CachedWindow { std::shared_ptr<const Compiled> compiled; std::uint64_t used = 0; };
	std::map<WindowKey,CachedWindow> windows;
	std::uint64_t windowClock = 0;
	bool CompileWindow(const HudPrimitive&, const Node&, const std::array<VectorPoint,4>& quad,
		const VectorPixelBounds&, Compiled&, std::string& error);
	// What OnRender draws, in order: a game-drawn run of vector pieces, entries
	// first to last, as one geometry (one more for each switch to or from a
	// multiply path), or one entry drawn on its own. Settled pieces and moving
	// ones go in separate runs, so a moving piece rebuilds only its own run.
	struct BatchedDraw {
		static constexpr size_t Combined = static_cast<size_t>(-1);
		size_t entry = Combined, first = 0, last = 0;
		std::vector<Rml::Geometry> geometry;
		std::vector<bool> multiply;
	};
	std::vector<BatchedDraw> batches;
	bool batched = false;
	void Batch();
	std::map<std::string,const Node*> catalog;
	// Artwork that is one solid fill of its whole frame, and that fill's colour
	// (the "solid" box every game-drawn bar, stroke and dot names); the rest map
	// to nothing.
	std::map<std::string,std::optional<VectorColour>> solids;
	// How far each path of an artwork can paint, so a window onto part of a
	// design compiles only the paths that reach into it.
	struct PathExtent {
		VectorPoint low, high;	// dp in the artwork's 128 dp frame
		double pixels = 0;		// and output pixels beyond that
		bool known = false;		// false: assume it reaches everywhere
		static PathExtent Of(const VectorPath&);
		bool Touches(const VectorTransform&, const VectorPixelBounds&) const;
	};
	std::map<const Node*,std::vector<PathExtent>> extents;
	const std::vector<PathExtent>& Extents(const Node&);
	RuntimeStatistics* statistics = nullptr;
	Rml::Texture multiply;
	bool ready = false;
};
}
