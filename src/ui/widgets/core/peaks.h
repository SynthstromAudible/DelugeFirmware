#pragma once
#include <cstdint>

namespace deluge::ui::widgets::core {

/// @brief One column's vertical extent, in 8.24 fixed point, as a waveform widget draws it.
///
/// The column's minimum and maximum sample values, re-centred on the sample's found value centre point and scaled for
/// the surface that draws them: on the grid (`grid::waveform_full`) `1 << 24` is one pad row; on the OLED
/// (`oled::waveform`) it is half the waveform rect's height. `valid` marks whether this column has been investigated
/// by the scan; an uninvestigated column draws as such (black on the grid, a dotted centre on the OLED).
///
/// @note The "at least 1 pixel's width" widening of a bar is part of the scan that produces `min24`/`max24`, not the
///       render, and lives with the rest of that scan.
struct PeakCol {
	int32_t min24 = 0;
	int32_t max24 = 0;
	bool valid = false;
};

} // namespace deluge::ui::widgets::core
