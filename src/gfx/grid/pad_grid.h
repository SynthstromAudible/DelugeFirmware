#pragma once
#include "gfx/contract.h"
#include "gfx/grid/rgb.h"
#include <cstdint>

namespace deluge::gfx::grid {

struct GridDimensions {
	uint8_t main_cols;
	uint8_t sidebar_cols;
	uint8_t rows;
	/// uint16_t, not uint8_t: both operands are uint8_t but their sum is up to 510, so the
	/// narrower return type would silently wrap (e.g. {200, 100, 8} would report 44 columns).
	constexpr uint16_t cols() const { return static_cast<uint16_t>(main_cols + sidebar_cols); }
	/// So the tweens can state their "out shares dims() with the inputs" precondition as a check.
	constexpr bool operator==(const GridDimensions&) const = default;
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
	/// Precondition: `d.rows <= kMaxGridRows`, `d.cols() <= kMaxGridCols` (the fixed `cells_`
	/// capacity -- a larger grid would index off the end of it) and `d.rows <= 32` (the width
	/// of the `dirty_` row mask). Checked in the constructor; nothing here clamps, because a
	/// silently-shrunk grid would drop pads the caller believes it drew.
	explicit PadGrid(GridDimensions d);

	GridDimensions dims() const { return dims_; }
	bool in_range(uint8_t x, uint8_t y) const { return x < dims_.cols() && y < dims_.rows; }

	/// Accessors, unlike set()/clear_row()/fill_rect(), do NOT clamp: they hand back a reference
	/// the caller writes through, so there is no in-range answer to return for an out-of-range
	/// request. `in_range(x, y)` (rows for row()) is the caller's precondition; violating it is a
	/// programming error and traps. See gfx/contract.h for why this survives -DNDEBUG.
	RGB& at(uint8_t x, uint8_t y) {
		DELUGE_GFX_CONTRACT(in_range(x, y));
		return cells_[y][x];
	}
	const RGB& at(uint8_t x, uint8_t y) const {
		DELUGE_GFX_CONTRACT(in_range(x, y));
		return cells_[y][x];
	}
	RGB* row(uint8_t y) {
		DELUGE_GFX_CONTRACT(y < dims_.rows);
		return cells_[y];
	}
	const RGB* row(uint8_t y) const {
		DELUGE_GFX_CONTRACT(y < dims_.rows);
		return cells_[y];
	}

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
	void mark_all_dirty() {
		// `1u << 32` is undefined, so the full-width case is spelled out rather than computed.
		// The constructor forbids rows > 32; this keeps rows == 32 well defined.
		dirty_ = dims_.rows >= 32 ? 0xFFFFFFFFu : (1u << dims_.rows) - 1;
	}
	void clear_dirty() { dirty_ = 0; }

private:
	GridDimensions dims_;
	uint32_t dirty_ = 0;
	RGB cells_[kMaxGridRows][kMaxGridCols];
};

} // namespace deluge::gfx::grid
