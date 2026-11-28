#pragma once
#include "ui/gfx/grid/rgb.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/waveform.h"

#include <cstdint>
#include <optional>
#include <span>

/// @file
/// @brief A sample's full-height waveform on the main pads, one bar per column.

namespace deluge::ui::views::grid {

/// @brief `waveform_full(f, cols, brightness, colour)` over @p f's main columns: its first `cols.size()` columns, so a
///        root frame keeps its sidebar and a region of the main pads is painted whole.
/// @param f          The frame: the root frame, or a region over the main pads.
/// @param cols       One peak per main column; an invalid one paints its column black.
/// @param brightness `kWaveformBarBrightnessMono` with no @p colour, `kWaveformBarBrightnessColoured` with one
///                   (`waveform_full`'s precondition).
/// @param colour     The bars' colour, or none for monochrome bars.
void waveform(widgets::grid::GridFrame& f, std::span<const widgets::grid::PeakCol> cols, int32_t brightness,
              std::optional<RGB> colour);

} // namespace deluge::ui::views::grid
