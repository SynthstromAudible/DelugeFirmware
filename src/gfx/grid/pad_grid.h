#pragma once
#include "gfx/grid/rgb.h"
#include <cstdint>

namespace deluge::gfx::grid {

struct GridDimensions {
	uint8_t main_cols;
	uint8_t sidebar_cols;
	uint8_t rows;
	constexpr uint8_t cols() const { return main_cols + sidebar_cols; }
};
constexpr GridDimensions kDelugeGrid{16, 2, 8};

constexpr uint8_t kMaxGridCols = 32;
constexpr uint8_t kMaxGridRows = 16;

/// RGB pad framebuffer with runtime dimensions and a per-row dirty mask (the protocol the
/// existing renderMainPads(whichRows) already uses). Storage is `cells_[kMaxGridRows][kMaxGridCols]`
/// row-major with a compile-time capacity chosen to cover the largest grid this layer supports;
/// it is NOT laid out the same as legacy `PadLEDs::image[kDisplayHeight][kDisplayWidth + kSideBarWidth]`
/// (18 columns) because kMaxGridCols is 32, not 18. `row(y)` is therefore the aliasing seam later
/// phases use to copy a single row to/from that legacy buffer -- never reinterpret the whole
/// `cells_` array as one contiguous legacy image.
class PadGrid {
public:
	explicit PadGrid(GridDimensions d);

	GridDimensions dims() const { return dims_; }
	bool in_range(uint8_t x, uint8_t y) const { return x < dims_.cols() && y < dims_.rows; }

	RGB& at(uint8_t x, uint8_t y) { return cells_[y][x]; }
	const RGB& at(uint8_t x, uint8_t y) const { return cells_[y][x]; }
	RGB* row(uint8_t y) { return cells_[y]; }
	const RGB* row(uint8_t y) const { return cells_[y]; }

	void set(uint8_t x, uint8_t y, RGB c);
	void clear();
	void clear_row(uint8_t y);
	void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c);

	uint32_t dirty_rows() const { return dirty_; }
	void mark_row_dirty(uint8_t y) {
		if (y < dims_.rows) {
			dirty_ |= 1u << y;
		}
	}
	void mark_all_dirty() { dirty_ = (1u << dims_.rows) - 1; }
	void clear_dirty() { dirty_ = 0; }

private:
	GridDimensions dims_;
	uint32_t dirty_ = 0;
	RGB cells_[kMaxGridRows][kMaxGridCols];
};

} // namespace deluge::gfx::grid
