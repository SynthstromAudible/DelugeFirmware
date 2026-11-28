#pragma once
#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/rgb.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/markers.h"

#include <array>
#include <cstdint>

/// @file
/// @brief A sample's four markers over its waveform on the main pads.

namespace deluge::ui::views::grid {

/// @brief Which markers land on each main column, and how they show on this pass.
struct MarkerOverlay {
	/// Per main column: bit `i` set when marker `i` lands there.
	std::array<uint32_t, gfx::grid::kDelugeGrid.main_cols> active;
	uint32_t suppress;                                   ///< Bit `i` set to skip marker `i` (the blink's off phase).
	int32_t selected;                                    ///< The selected marker's index.
	std::array<RGB, widgets::grid::kMaxMarkers> colours; ///< Each marker's colour, by index.
	int32_t flood; ///< The column the selected marker's colour fills whole (the blink's phase 1), or -1.
};

/// @brief `SampleMarkerEditor::timerCallback`'s columns: `marker_tint` down the whole `d.flood` column in the selected
///        marker's colour, and `marker_column` on every other column a marker lands on.
/// @param main A region over the main pads, its waveform painted already.
/// @param d    What to show.
void marker_overlay(widgets::grid::GridFrame& main, const MarkerOverlay& d);

} // namespace deluge::ui::views::grid
