#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::widgets::oled {

/// @brief A sample's waveform, one column per entry of @p cols from @p r's left edge.
///
/// A valid column is a solid vertical from its max row to its min row about @p r's centre row, `1 << 24` being half
/// @p r's height, clamped to @p r; silence is one pixel on the centre row. An uninvestigated column draws the centre
/// row on the even columns of @p r only (counted from its left edge), so a part of the sample not yet scanned reads as
/// a dotted line. Columns past @p r's width are not drawn. The scene chooses the columns (scroll, zoom) and scales the
/// peaks.
void waveform(OledFrame& f, gfx::oled::Rect r, std::span<const core::PeakCol> cols);

/// @brief The loop-point marker (`sample::LoopPoint::renderInHorizontalMenu`,
///        `origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`): a line dotted every 2 rows from @p y0 to
///        @p y1 at @p line_x, and the loop icon at @p icon_y, 4 columns clear of the line: to its right for a start,
///        mirrored to its left for an end.
void loop_marker(OledFrame& f, int32_t line_x, int32_t y0, int32_t y1, int32_t icon_y, bool start);

/// @brief `waveform` under an 8-row icon band, with a loop marker at columns @p start_col and @p end_col of @p r.
void looped_waveform(OledFrame& f, gfx::oled::Rect r, std::span<const core::PeakCol> cols, int32_t start_col,
                     int32_t end_col);

} // namespace deluge::ui::widgets::oled
