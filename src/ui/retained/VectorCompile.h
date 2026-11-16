// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include "Vector.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openq4::ui {
class VectorGeometry;
// The compilations a page's first presented frame would make, gathered by a
// frame drawn out of sight (DeferVectorCompilations) instead of tessellated
// there. Tessellation is pure, so Run may compile each request on any thread;
// Install then hands every element that has still never drawn its meshes,
// back on the thread that drew, once every Run has finished. An element that
// changed or went in between takes nothing.
class VectorCompileBatch {
public:
	size_t Size() const { return requests.size(); }
	void Run(size_t index);
	size_t Install();
	void Clear() { requests.clear(); }
	size_t Paths() const;
private:
	friend class VectorGeometry;
	struct Request {
		std::weak_ptr<VectorGeometry*> owner;
		std::uint64_t generation = 0;
		std::shared_ptr<const std::vector<VectorPath>> paths;
		VectorOptions options;
		std::array<double,12> signature{};
		std::string element;
		std::vector<VectorMesh> meshes;
		std::vector<PathBlend> blends;
		std::vector<std::string> errors;
		bool compiled = false;
	};
	std::vector<Request> requests;
};
// While a batch is set, VectorGeometry::Render of an element that has never
// drawn queues its compilation there and submits nothing; nullptr ends it.
// Only for the thread that renders.
void DeferVectorCompilations(VectorCompileBatch* batch);
}
