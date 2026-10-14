#include "gfx/grid/pad_grid.h"

namespace deluge::gfx::grid {

// Zeroes storage without touching dirty_: a freshly-constructed grid starts clean (dirty_rows()
// == 0), whereas the public clear() below represents an actual content change and marks every row
// dirty so it gets redrawn.
PadGrid::PadGrid(GridDimensions d) : dims_(d) {
	// The storage is a fixed kMaxGridRows x kMaxGridCols array and the dirty mask is 32 bits
	// wide, so anything larger is a programming error rather than something to clamp away.
	// DELUGE_GFX_CONTRACT, not assert: these have to survive -DNDEBUG (gfx/contract.h).
	// `rows <= 32` is redundant while kMaxGridRows is 16, and is stated anyway so raising that
	// capacity past the mask width trips here rather than silently corrupting dirty_.
	DELUGE_GFX_CONTRACT(d.rows <= kMaxGridRows);
	DELUGE_GFX_CONTRACT(d.cols() <= kMaxGridCols);
	DELUGE_GFX_CONTRACT(d.rows <= 32);
	for (uint8_t y = 0; y < kMaxGridRows; y++) {
		for (uint8_t x = 0; x < kMaxGridCols; x++) {
			cells_[y][x] = RGB{0, 0, 0};
		}
	}
}

void PadGrid::set(uint8_t x, uint8_t y, RGB c) {
	if (!in_range(x, y)) {
		return;
	}
	cells_[y][x] = c;
	dirty_ |= 1u << y;
}

void PadGrid::clear() {
	for (uint8_t y = 0; y < kMaxGridRows; y++) {
		for (uint8_t x = 0; x < kMaxGridCols; x++) {
			cells_[y][x] = RGB{0, 0, 0};
		}
	}
	mark_all_dirty();
}

void PadGrid::clear_row(uint8_t y) {
	if (y >= dims_.rows) {
		return;
	}
	for (uint8_t x = 0; x < dims_.cols(); x++) {
		cells_[y][x] = RGB{0, 0, 0};
	}
	dirty_ |= 1u << y;
}

void PadGrid::fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c) {
	// x + w / y + h are computed in int32_t: x, w (and y, h) are each up to 255, so their sum can
	// exceed uint8_t range before the clamp below has a chance to bring it back into range.
	const int32_t x1_wide = static_cast<int32_t>(x) + w;
	const int32_t y1_wide = static_cast<int32_t>(y) + h;
	const uint8_t x1 = (x1_wide < dims_.cols()) ? static_cast<uint8_t>(x1_wide) : static_cast<uint8_t>(dims_.cols());
	const uint8_t y1 = (y1_wide < dims_.rows) ? static_cast<uint8_t>(y1_wide) : dims_.rows;
	for (uint8_t yy = y; yy < y1; yy++) {
		for (uint8_t xx = x; xx < x1; xx++) {
			cells_[yy][xx] = c;
		}
		dirty_ |= 1u << yy;
	}
}

} // namespace deluge::gfx::grid
