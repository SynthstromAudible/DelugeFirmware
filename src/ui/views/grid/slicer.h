#pragma once
#include "ui/widgets/grid/frame.h"

#include <array>
#include <cstdint>

/// @file
/// @brief The slicer's slice marks and slot grid over its waveform on the main pads.

namespace deluge::ui::views::grid {

/// @brief What the slicer's pads show over the waveform.
struct SliceMarks {
	bool manual;                  ///< Manual mode: the waveform is squashed and the marks and slots drawn. Else none.
	int32_t count;                ///< How many slices are placed, `0 .. cols.size()`.
	int32_t current;              ///< The selected slice's index.
	std::array<int32_t, 64> cols; ///< Each placed slice's column; one past the last main column is clipped.
};

/// @brief `Slicer::renderMainPads`' MANUAL arm after its waveform: the full-height waveform averaged in row pairs into
///        the lower half, a mark on that half's first row for each slice, and the 64 slots in four pages of 4x4 above
///        it. Nothing when `d.manual` is false.
/// @param main A region over the 16x8 main pads, its waveform painted full height already.
/// @param d    What to show.
void slice_marks(widgets::grid::GridFrame& main, const SliceMarks& d);

} // namespace deluge::ui::views::grid
