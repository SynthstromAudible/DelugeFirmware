#pragma once
#include "gfx/contract.h"
#include "gfx/grid/rgb.h"
#include <cstdint>

namespace deluge::gfx::grid {

/// @brief Column/row extents of a pad grid: main pad columns, sidebar columns, and rows.
struct GridDimensions {
	uint8_t main_cols;
	uint8_t sidebar_cols;
	uint8_t rows;

	/// @brief Total column count (main + sidebar).
	///
	/// Returns uint16_t, not uint8_t: both operands are uint8_t but their sum can be up to 510,
	/// so the narrower return type would silently wrap (e.g. {200, 100, 8} would report 44
	/// columns).
	/// @return main_cols + sidebar_cols.
	constexpr uint16_t cols() const { return static_cast<uint16_t>(main_cols + sidebar_cols); }

	/// @brief Equality comparison, so callers (e.g. the gfx/grid tween functions) can check that
	///        an output grid shares dims() with its inputs as a precondition.
	constexpr bool operator==(const GridDimensions&) const = default;
};
/// @brief The real Deluge hardware's grid: 16 main columns, 2 sidebar columns, 8 rows.
constexpr GridDimensions kDelugeGrid{16, 2, 8};

/// @brief Fixed column capacity of PadGrid's backing storage (see PadGrid for why it isn't 18).
constexpr uint8_t kMaxGridCols = 32;
/// @brief Fixed row capacity of PadGrid's backing storage, and the width of its dirty-row mask.
constexpr uint8_t kMaxGridRows = 16;

/// @brief RGB pad framebuffer with runtime dimensions and a per-row dirty mask.
///
/// The dirty-mask protocol matches what the existing renderMainPads(whichRows) already consumes.
/// Storage is `cells_[kMaxGridRows][kMaxGridCols]`, row-major, sized to the largest grid this
/// layer supports. This layout is NOT the same as legacy `PadLEDs::image[kDisplayHeight][
/// kDisplayWidth + kSideBarWidth]` (18 columns) because kMaxGridCols is 32, not 18.
/// @warning `row(y)` is the aliasing seam for copying a single row to/from that legacy buffer --
///          never reinterpret the whole `cells_` array as one contiguous legacy image.
class PadGrid {
public:
	/// @brief Construct a pad grid with the given dimensions. All cells start black and clean.
	///
	/// @pre `d.rows <= kMaxGridRows` and `d.cols() <= kMaxGridCols` (the fixed `cells_` capacity
	///      -- a larger grid would index off the end of it), and `d.rows <= 32` (the width of the
	///      `dirty_` row mask). Checked in the constructor; nothing here clamps, because a
	///      silently-shrunk grid would drop pads the caller believes it drew.
	/// @param d Requested main/sidebar column counts and row count.
	explicit PadGrid(GridDimensions d);

	/// @brief Current grid dimensions.
	/// @return The grid's dimensions.
	GridDimensions dims() const { return dims_; }
	/// @brief Whether (x, y) addresses a valid cell in this grid.
	/// @param x Column index.
	/// @param y Row index.
	/// @return true if (x, y) is within the grid's dimensions.
	bool in_range(uint8_t x, uint8_t y) const { return x < dims_.cols() && y < dims_.rows; }

	/// @brief Access the cell at (x, y).
	///
	/// @warning Does NOT clamp, unlike set()/clear_row()/fill_rect(): this hands back a reference
	///          the caller writes through, so there is no in-range answer to return for an
	///          out-of-range request. `in_range(x, y)` is the caller's precondition; violating it
	///          is a programming error and traps rather than silently clamping (see
	///          gfx/contract.h for why this survives -DNDEBUG).
	/// @param x Column index; must satisfy in_range(x, y).
	/// @param y Row index; must satisfy in_range(x, y).
	/// @return Reference to the cell.
	RGB& at(uint8_t x, uint8_t y) {
		DELUGE_GFX_CONTRACT(in_range(x, y));
		return cells_[y][x];
	}
	/// @copydoc at(uint8_t, uint8_t)
	const RGB& at(uint8_t x, uint8_t y) const {
		DELUGE_GFX_CONTRACT(in_range(x, y));
		return cells_[y][x];
	}
	/// @brief Pointer to the start of row @p y (dims().cols() cells wide).
	///
	/// @warning Does NOT clamp: violating `y < dims().rows` is a programming error and traps
	///          rather than silently clamping, for the same reason as at() above.
	/// @param y Row index; must satisfy y < dims().rows.
	/// @return Pointer to the first cell of row y.
	RGB* row(uint8_t y) {
		DELUGE_GFX_CONTRACT(y < dims_.rows);
		return cells_[y];
	}
	/// @copydoc row(uint8_t)
	const RGB* row(uint8_t y) const {
		DELUGE_GFX_CONTRACT(y < dims_.rows);
		return cells_[y];
	}

	/// @brief Set the cell at (x, y) to @p c and mark its row dirty.
	///
	/// Unlike at(), out-of-range (x, y) is silently ignored rather than trapping.
	/// @param x Column index.
	/// @param y Row index.
	/// @param c Colour to write.
	void set(uint8_t x, uint8_t y, RGB c);
	/// @brief Clear every cell to black and mark every row dirty.
	void clear();
	/// @brief Clear row @p y to black and mark it dirty. Out-of-range @p y is silently ignored.
	/// @param y Row index.
	void clear_row(uint8_t y);
	/// @brief Fill the rectangle at (x, y) sized w x h with @p c, clamped to the grid, marking
	///        every touched row dirty.
	/// @param x Rectangle left column.
	/// @param y Rectangle top row.
	/// @param w Rectangle width in columns.
	/// @param h Rectangle height in rows.
	/// @param c Colour to fill with.
	void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c);

	/// @brief Bitmask of rows changed since the last clear_dirty(); bit y set means row y is dirty.
	/// @return The dirty-row bitmask.
	uint32_t dirty_rows() const { return dirty_; }
	/// @brief Mark row @p y dirty without changing its contents.
	/// @param y Row index; out-of-range values are silently ignored.
	void mark_row_dirty(uint8_t y) {
		if (y < dims_.rows) {
			dirty_ |= 1u << y;
		}
	}
	/// @brief Mark every row in the grid dirty.
	void mark_all_dirty() {
		// `1u << 32` is undefined, so the full-width case is spelled out rather than computed.
		// The constructor forbids rows > 32; this keeps rows == 32 well defined.
		dirty_ = dims_.rows >= 32 ? 0xFFFFFFFFu : (1u << dims_.rows) - 1;
	}
	/// @brief Clear the dirty mask (mark every row clean).
	void clear_dirty() { dirty_ = 0; }

private:
	GridDimensions dims_;
	uint32_t dirty_ = 0;
	RGB cells_[kMaxGridRows][kMaxGridCols];
};

} // namespace deluge::gfx::grid
