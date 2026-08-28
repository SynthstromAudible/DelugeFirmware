#include "ui/grid/waveform.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

/// `WaveformRenderer::drawColBar` (waveform_renderer.cpp:731-762), transcribed.
///
/// \p half is legacy's `kDisplayHeight >> 1`, taken from the frame rather than a global. The
/// shifts are legacy's own: `>> 24` floors toward minus infinity for the negative half of the
/// bar, and `y << 24` reconstructs that row's floor so the partial-coverage terms measure how far
/// into the row the bar reaches.
void draw_col_bar(GridFrame& f, int32_t x, int32_t min24, int32_t max24, int32_t brightness, std::optional<RGB> rgb,
                  int32_t half) {
	const int32_t yStart = std::max<int32_t>(min24 >> 24, -half);     // :733
	const int32_t yStop = std::min<int32_t>((max24 >> 24) + 1, half); // :734

	for (int32_t y = yStart; y < yStop; y++) {
		int32_t colourAmount; // Out of 256 (:738)

		if (y == (min24 >> 24)) {                                        // :740 -- the bottom row, partially covered
			const int32_t howMuchThisSquare = (min24 - (y << 24)) >> 16; // 8-bit (:741)
			colourAmount = brightness - ((howMuchThisSquare * brightness) >> 8); // :742
		}
		else if (y < (max24 >> 24)) {  // :745 -- fully covered
			colourAmount = brightness; // :746
		}
		else {                                                           // :749 -- the top row, partially covered
			const int32_t howMuchThisSquare = (max24 - (y << 24)) >> 16; // :750
			colourAmount = ((howMuchThisSquare * brightness) >> 8);      // :751
		}

		const int32_t valueHere = (colourAmount * colourAmount) >> 8; // :754
		// :755-757. `RGB::monochrome` takes a uint8_t and legacy relies on the mono path only ever
		// being reached with brightness 128 (`renderOneCol`, :766), where valueHere peaks at 64.
		const RGB color = rgb.has_value()
		                      ? rgb.value().transform([valueHere](auto channel) { return (valueHere * channel) >> 8; })
		                      : RGB::monochrome(valueHere);

		f.pad(x, y + half, color); // :759
	}
}

} // namespace

RGB waveform_col_colour(int32_t brightness, RGB base) {
	const int32_t colourValue = brightness * brightness;         // :137
	return base.transform([colourValue](auto channel) {          // :140
		const int32_t valueHere = (colourValue * channel) >> 16; // :141
		// :145 -- the deliberate bit-depth limiter, not rounding. See the header.
		return std::clamp<int32_t>((valueHere + 6) & ~15, 0, RGB::channel_max);
	});
}

void waveform_row(GridFrame& f, int32_t y, std::span<const int32_t> brightness, RGB base, bool reversed) {
	const int32_t w = f.bounds().w;

	for (int32_t x = 0; x < w; x++) {
		// :131-134 -- the output column reads the mirrored source column when reversed.
		const int32_t source = reversed ? (w - 1 - x) : x;

		if (source < 0 || source >= static_cast<int32_t>(brightness.size())) {
			f.pad(x, y, gui::colours::black); // :127-130, "If no data here"
			continue;
		}
		f.pad(x, y, waveform_col_colour(brightness[source], base));
	}
}

void waveform_full(GridFrame& f, std::span<const PeakCol> cols, int32_t brightness, std::optional<RGB> rgb) {
	const int32_t w = f.bounds().w;
	const int32_t rows = f.bounds().h;
	const int32_t half = rows >> 1; // legacy's kDisplayHeight >> 1 (:733)
	const int32_t n = std::min<int32_t>(w, static_cast<int32_t>(cols.size()));

	// "Clear display" (renderFullScreen, :92-95). Only the columns this call covers, so a frame
	// wider than the waveform -- or a sidebar painted by someone else -- is left alone.
	for (int32_t x = 0; x < n; x++) {
		for (int32_t y = 0; y < rows; y++) {
			f.pad(x, y, gui::colours::black);
		}
	}

	for (int32_t x = 0; x < n; x++) {
		if (!cols[x].valid) { // renderOneCol, :771
			continue;
		}
		draw_col_bar(f, x, cols[x].min24, cols[x].max24, brightness, rgb, half); // :776
	}
}

} // namespace deluge::ui::grid
