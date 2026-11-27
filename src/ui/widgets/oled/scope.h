#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>

/// @file
/// @brief The master output as a picture: a triggered trace, and L against R as a figure. The caller triggers,
///        decimates and hands over the samples; these only draw them.

namespace deluge::ui::widgets::oled {

/// @brief A trace across @p r, one sample per column from its left edge.
///
/// The centre row is `y + h / 2` of @p r and a sample s is drawn `s * (h / 2) / 128` rows above it, clamped to @p r;
/// consecutive columns are joined. A dotted centre line (every 2nd column from the left edge) is drawn first. Samples
/// past the rect's width are not drawn.
void scope(OledFrame& f, gfx::oled::Rect r, std::span<const int8_t> samples);

/// @brief L on x against R on y, in a box as tall as @p r and twice as wide (no wider than @p r), centred in it.
///
/// Dotted axes (every 2nd pixel) cross at the box's centre; a sample pair (l, r) is the point `l * (w / 2) / 128`
/// right of the centre and `r * (h / 2) / 128` above it, clamped to the box, and consecutive points are joined. Uses
/// as many points as the shorter of @p left and @p right.
void scope_xy(OledFrame& f, gfx::oled::Rect r, std::span<const int8_t> left, std::span<const int8_t> right);

} // namespace deluge::ui::widgets::oled
