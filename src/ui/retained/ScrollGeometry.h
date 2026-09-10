// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once

namespace openq4::ui {

// All values use the same local layout units. The adapter measures the actual
// scroll viewport and projects incoming pointer coordinates into the track's
// local plane; density and transforms never enter this arithmetic a second time.
struct ScrollMetrics {
    double viewport = 0, content = 0, offset = 0;
    double track = 0, minimumThumb = 36;
};
struct ScrollGeometry {
    double viewport = 0, range = 0, offset = 0;
    double track = 0, thumb = 0, position = 0, travel = 0;
    bool usable = false;
};
enum class ScrollStep { LineBackward, LineForward, PageBackward, PageForward, Start, End };

// Empty, fitting and temporarily zero-size regions have a valid inert geometry.
// Nonfinite/negative extents refuse. Finite offsets are clamped to current range.
// Outputs remain unchanged on false. No state, allocation, input or host effects.
bool MeasureScroll(const ScrollMetrics&, ScrollGeometry& out) noexcept;
// A grab fraction within the thumb, retained through resize, prevents the thumb
// jumping to its center when pressed. Out-of-track drag coordinates clamp.
bool ScrollDragOffset(const ScrollGeometry&, double pointer, double grabFraction,
    double& out) noexcept;
// Page motion keeps one line of overlap where the viewport permits it. A supplied
// step is one semantic pulse; repeat timing and source ownership stay in Input.
bool ScrollStepOffset(const ScrollGeometry&, ScrollStep, double line, double& out) noexcept;

} // namespace openq4::ui
