#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>

/// @file
/// @brief Small amount and count marks for the OLED, in the house's solid-then-dotted idiom: an amount line, the
///        master level as two of them, a row of beat boxes, and a position bar.

namespace deluge::ui::widgets::oled {

/// @brief One 1-px line on row @p y: solid from @p x0 to `x0 + round(v * (x1 - x0))`, then dotted (every 2nd column)
///        from two columns past that to @p x1. @p v is clamped to 0..1; at 0 the line is dotted only.
void amount_line(OledFrame& f, int32_t y, int32_t x0, int32_t x1, float v);

/// @brief The master level as two `amount_line`s across @p r: L on its first row, R on its second (left out of a
///        one-row rect). Levels 0..1, clamped.
void level_lines(OledFrame& f, gfx::oled::Rect r, float left, float right);

/// @brief A row of @p n boxes, @p box_w wide, as tall as @p r and @p gap apart, centred in @p r; the first @p filled
///        solid, the rest outlined. @p filled is clamped to 0..n; @p n <= 0 or @p box_w <= 0 draws nothing. Too wide
///        for @p r, the gaps narrow (down to 1), then the boxes (down to 1); more boxes than columns draws nothing.
///        Count-in and tap tempo use the defaults; stutter's sixteen repeats use 6 and 2.
void beat_boxes(OledFrame& f, gfx::oled::Rect r, int32_t n, int32_t filled, int32_t box_w = 8, int32_t gap = 5);

/// @brief Where a window sits in a whole: a dotted track (every 2nd column) on the middle row of @p r, and the window
///        `[start, start + len)` of @p total as a solid bar as tall as @p r. Unit u is column `x + u * w / total` of
///        @p r; a window covering the whole fills the track.
void position_bar(OledFrame& f, gfx::oled::Rect r, int32_t total, int32_t start, int32_t len);

} // namespace deluge::ui::widgets::oled
