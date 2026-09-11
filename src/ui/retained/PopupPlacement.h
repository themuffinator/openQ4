// Copyright (C) 2026 DarkMatter Productions. GPL-3.0-or-later.
#pragma once
#include <array>
#include <cstddef>

namespace openq4::ui {
struct PopupPoint { double x = 0, y = 0; };
struct PopupRect { double x = 0, y = 0, width = 0, height = 0; };
struct PopupHalfPlane { double x = 0, y = 0, limit = 0; };
struct PopupRegion {
    std::array<PopupHalfPlane, 8> planes{};
    std::size_t count = 0;
    PopupRect viewport;
};
struct PopupPlacement {
    PopupRect rectangle;
    bool above = false, overlapsAnchor = false;
};

// Physical layout coordinates, already projected once. The four source corners
// must describe a finite convex quad; mirrored winding is accepted. Insets are
// distances from the actual edges, not from a loose axis-aligned bounding box.
// All helpers are allocation/callback free and preserve outputs on refusal.
bool MeasurePopupRegion(const std::array<PopupPoint, 4>& bounds, const PopupRect& viewport,
    double inset, PopupRegion& out) noexcept;
bool PopupRegionContains(const PopupRegion&, const PopupRect&, double tolerance = 0) noexcept;
// Preserve row/font size: only the viewport rectangle is reduced. A full
// minHeight (row including margins plus frame chrome) must fit. Prefer below,
// then above; overlap the anchor only when neither side has one readable row.
bool PlacePopup(const PopupRegion&, const PopupRect& anchor, double preferredWidth,
    double minWidth, double desiredHeight, double minHeight, PopupPlacement& out) noexcept;
} // namespace openq4::ui
