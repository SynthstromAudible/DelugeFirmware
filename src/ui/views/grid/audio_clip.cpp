#include "ui/views/grid/audio_clip.h"

#include "ui/gfx/grid/palette.h"

#include <algorithm>

namespace deluge::ui::views::grid {

void clip_markers(widgets::grid::GridFrame& main, const ClipMarkers& d) {
	namespace colours = deluge::gui::colours;
	const int32_t cols = std::min<int32_t>(main.bounds().w, gfx::grid::kDelugeGrid.main_cols);
	for (int32_t y = 0; y < main.bounds().h; y++) {
		// The end marker, and the grey past it.
		if (d.end_square < cols) {
			if (d.end_square >= 0 && d.end_visible) {
				(void)main.pad(d.end_square, y, d.blink_on ? colours::red : colours::red_dull);
			}
			const int32_t from = std::max<int32_t>(d.end_square + 1, 0);
			for (int32_t g = from; g < cols; g++) {
				(void)main.pad(g, y, colours::grey);
			}
		}
		// The start marker when it is on screen, and a whole-width grey fill when it has scrolled off the right.
		// Legacy's own commented-out grey fill before the start is left out, as legacy leaves it.
		if (d.start_square >= 0) {
			if (d.start_square < cols) {
				if (d.start_visible) {
					(void)main.pad(d.start_square, y, d.blink_on ? colours::green : colours::green.dim());
				}
			}
			else {
				for (int32_t g = 0; g < cols; g++) {
					(void)main.pad(g, y, colours::grey);
				}
			}
		}
	}
}

} // namespace deluge::ui::views::grid
