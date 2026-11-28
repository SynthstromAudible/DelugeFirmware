#include "ui/views/grid/pad_image.h"

#include <algorithm>

namespace deluge::ui::views::grid {

void pads(widgets::grid::GridFrame& f, const PadImage& d) {
	const int32_t rows = std::min<int32_t>(f.bounds().h, gfx::grid::kDelugeGrid.rows);
	const int32_t cols = std::min<int32_t>(f.bounds().w, gfx::grid::kDelugeGrid.main_cols);
	for (int32_t y = 0; y < rows; y++) {
		for (int32_t x = 0; x < cols; x++) {
			(void)f.pad(x, y, d.pads[y][x]);
		}
	}
}

} // namespace deluge::ui::views::grid
