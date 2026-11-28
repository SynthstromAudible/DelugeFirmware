#include "ui/views/grid/slicer.h"

#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/palette.h"
#include "ui/gfx/grid/rgb.h"

#include <algorithm>

namespace deluge::ui::views::grid {

namespace {

constexpr int32_t kMainCols = gfx::grid::kDelugeGrid.main_cols;
constexpr int32_t kRows = gfx::grid::kDelugeGrid.rows;
/// The squashed waveform and the marks start on the lower half's first row.
constexpr int32_t kWaveformRowBase = kRows / 2;

} // namespace

void slice_marks(widgets::grid::GridFrame& main, const SliceMarks& d) {
	if (!d.manual) {
		return;
	}

	// The waveform was drawn full height. Each lower row is the average of a pair of its rows, and row 6 reads rows 4
	// and 5, which rows 0 and 1 would already have overwritten if done in place: snapshot it first.
	RGB src[kRows][kMainCols]{};
	for (int32_t y = 0; y < kRows; y++) {
		for (int32_t x = 0; x < kMainCols; x++) {
			src[y][x] = main.peek(x, y);
		}
	}
	for (int32_t x = 0; x < kMainCols; x++) {
		for (int32_t yy = 0; yy < kRows / 2; yy++) {
			(void)main.pad(x, yy + kWaveformRowBase, RGB::average(src[yy * 2][x], src[yy * 2 + 1][x]));
		}
	}

	// A slice at the very end of the sample lands one past the last main column, which the region clips.
	const int32_t count = std::min<int32_t>(d.count, static_cast<int32_t>(d.cols.size()));
	for (int32_t i = 0; i < count; i++) {
		(void)main.pad(d.cols[static_cast<size_t>(i)], kWaveformRowBase,
		               RGB(1, static_cast<uint8_t>((i == d.current) ? 200 : 16), 1));
	}

	// The lower screen: the slots (one per column entry, 64) in four pages of 4x4, left to right by page.
	for (int32_t i = 0; i < static_cast<int32_t>(d.cols.size()); i++) {
		const int32_t xx = (i % 4) + (i / 16) * 4;
		const int32_t yy = (i / 4) % 4;
		const int32_t page = i / 16;

		const auto dim_level = static_cast<uint8_t>((i < d.count) ? 2 : 6);
		RGB colour = (page % 2 == 0) ? gui::colours::green.dim(dim_level) : gui::colours::darkblue.dim(dim_level);
		if (i == d.current) {
			colour = gui::colours::green.dim();
		}
		(void)main.pad(xx, yy, colour);
	}
}

} // namespace deluge::ui::views::grid
