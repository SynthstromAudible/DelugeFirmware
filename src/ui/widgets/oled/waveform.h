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

} // namespace deluge::ui::widgets::oled
