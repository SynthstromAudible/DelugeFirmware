#include "ui/grid/waveform.h"

#include "gfx/grid/palette.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

// Draws one column's vertical bar between min24 and max24, in 8.24 fixed-point pad units.
//
// `half` is half the frame's height, so bars are centred on the frame rather than a fixed
// display height. `>> 24` floors toward minus infinity for the negative half of the bar, and
// `y << 24` reconstructs that row's floor so the partial-coverage terms measure how far into the
// row the bar reaches.
void draw_col_bar(GridFrame& f, int32_t x, int32_t min24, int32_t max24, int32_t brightness, std::optional<RGB> rgb,
                  int32_t half) {
	const int32_t y_start = std::max<int32_t>(min24 >> 24, -half);
	const int32_t y_stop = std::min<int32_t>((max24 >> 24) + 1, half);

	for (int32_t y = y_start; y < y_stop; y++) {
		int32_t colour_amount = 0; // Out of 256; every branch below assigns it

		if (y == (min24 >> 24)) {                                           // bottom row, partially covered
			const int32_t how_much_this_square = (min24 - (y << 24)) >> 16; // 8-bit
			colour_amount = brightness - ((how_much_this_square * brightness) >> 8);
		}
		else if (y < (max24 >> 24)) { // fully covered
			colour_amount = brightness;
		}
		else { // top row, partially covered
			const int32_t how_much_this_square = (max24 - (y << 24)) >> 16;
			colour_amount = ((how_much_this_square * brightness) >> 8);
		}

		const int32_t value_here = (colour_amount * colour_amount) >> 8;
		// `RGB::monochrome` takes a uint8_t and the mono path relies on brightness never exceeding
		// 128, where value_here peaks at 64. `waveform_full` states that as a precondition rather
		// than clamping it here -- see the header.
		const RGB color =
		    rgb.has_value() ? rgb.value().transform([value_here](auto channel) { return (value_here * channel) >> 8; })
		                    : RGB::monochrome(value_here);

		f.pad(x, y + half, color);
	}
}

} // namespace

RGB waveform_col_colour(int32_t brightness, RGB base) {
	const int32_t colour_value = brightness * brightness;
	return base.transform([colour_value](auto channel) {
		const int32_t value_here = (colour_value * channel) >> 16;
		// The deliberate bit-depth limiter, not rounding. See the header.
		return std::clamp<int32_t>((value_here + 6) & ~15, 0, RGB::channel_max);
	});
}

void waveform_row(GridFrame& f, int32_t y, std::span<const int32_t> brightness, RGB base, bool reversed) {
	const int32_t w = f.bounds().w;

	for (int32_t x = 0; x < w; x++) {
		// The output column reads the mirrored source column when reversed.
		const int32_t source = reversed ? (w - 1 - x) : x;

		if (source < 0 || source >= static_cast<int32_t>(brightness.size())) {
			f.pad(x, y, gui::colours::black); // If no data here
			continue;
		}
		f.pad(x, y, waveform_col_colour(brightness[source], base));
	}
}

void waveform_full(GridFrame& f, std::span<const PeakCol> cols, int32_t brightness, std::optional<RGB> rgb) {
	const int32_t w = f.bounds().w;
	const int32_t rows = f.bounds().h;
	const int32_t half = rows >> 1;
	const int32_t n = std::min<int32_t>(w, static_cast<int32_t>(cols.size()));

	// Clear every row of every column before any bar is drawn, so a main column past the end of
	// the waveform ends up black -- which is also what `waveform_row` does for a column with no
	// brightness entry. The frame is the waveform's area, so a caller that wants the sidebar
	// spared passes a region rather than the root frame.
	for (int32_t y = 0; y < rows; y++) {
		for (int32_t x = 0; x < w; x++) {
			f.pad(x, y, gui::colours::black);
		}
	}

	for (int32_t x = 0; x < n; x++) {
		if (!cols[x].valid) {
			continue;
		}
		draw_col_bar(f, x, cols[x].min24, cols[x].max24, brightness, rgb, half);
	}
}

} // namespace deluge::ui::grid
