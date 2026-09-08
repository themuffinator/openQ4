// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "Vector.h"

namespace openq4::ui {

enum class ValueType { Number, Length, Colour, Keyword, Font, Text, Transform };
// Colours are straight sRGB RGBA, 0..1. Transform is translation x/y,
// scale x/y and rotation in degrees, composed in that order by the renderer.
struct Value {
	ValueType type = ValueType::Number;
	std::array<double, 5> data{};
	std::string unit, text;
	bool CanInterpolate(const Value& other) const;
	Value Interpolate(const Value& other, double fraction) const;
	std::string Css() const;
};
struct Node {
	std::string id, type;
	std::map<std::string, Value> properties;
	std::vector<Node> children;
	std::vector<VectorPath> paths;
};
struct Easing {
	double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
	double Evaluate(double fraction) const;
};
struct Keyframe {
	double atMs = 0;
	Value value;
	Easing easing; // Easing of the interval beginning at this key.
};
struct Track {
	std::string node, property;
	std::vector<Keyframe> keys;
};
struct Timeline {
	std::string id;
	double durationMs = 0;
	unsigned iterations = 1; // Zero repeats until cancelled.
	bool essential = false;
	std::vector<Track> tracks;
};
struct DocumentModel {
	std::string id;
	Node root;
	std::map<std::string, Value> tokens;
	std::vector<Timeline> timelines;
	const Node* FindNode(const std::string& id) const;
};
struct Diagnostic {
	std::string pointer, message;
	size_t byte = 0, line = 1, column = 1;
};

// Canonical JSONC source is retained verbatim. DOM source offsets make edits
// transactional and preserve comments/extension fields outside the edited span.
// JsonCpp and its DOM do not cross this library boundary.
class Document {
public:
	Document();
	~Document();
	Document(Document&&) noexcept;
	Document& operator=(Document&&) noexcept;
	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;
	bool Load(const std::string& source, std::vector<Diagnostic>& diagnostics);
	bool ReplaceValue(const std::string& jsonPointer, const std::string& jsonValue,
		std::vector<Diagnostic>& diagnostics);
	const std::string& Source() const;
	const DocumentModel& Model() const;
	// Derived layout only. Text is inserted via Runtime's escaped/localized
	// text API; neither translation strings nor extensions become markup.
	std::string BuildMarkup() const;
private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace openq4::ui
