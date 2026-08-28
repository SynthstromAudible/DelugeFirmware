#pragma once
#include "gfx/contract.h"
#include <cstdint>
#include <utility>

namespace deluge::gfx::grid {
/// @brief Rectangle in pad units (columns, rows).
///
/// Same shape as gfx::oled::Rect; kept separate so the two gfx libraries stay independent.
struct Rect {
	int32_t x, y, w, h;
	/// @brief Equality comparison of position and size.
	constexpr bool operator==(const Rect&) const = default;
	/// @brief Right edge (x + w).
	/// @return The right edge, exclusive.
	constexpr int32_t right() const { return x + w; }
	/// @brief Bottom edge (y + h).
	/// @return The bottom edge, exclusive.
	constexpr int32_t bottom() const { return y + h; }
	/// @brief Whether point (px, py) lies within this rect (the right/bottom edges are exclusive).
	/// @param px Point x coordinate.
	/// @param py Point y coordinate.
	/// @return true if the point is inside this rect.
	constexpr bool contains(int32_t px, int32_t py) const {
		return px >= x && px < right() && py >= y && py < bottom();
	}
	/// @brief Intersection of this rect with @p o; zero width/height if they don't overlap.
	/// @param o Rect to intersect with.
	/// @return The overlapping rect, or a zero-size rect if there is none.
	constexpr Rect intersect(Rect o) const {
		int32_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
		int32_t nr = right() < o.right() ? right() : o.right();
		int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
		return {nx, ny, nr > nx ? nr - nx : 0, nb > ny ? nb - ny : 0};
	}

	/// @brief Split into `(top, bottom)` at local row @p at.
	///
	/// `top` gets rows `[0, at)`, `bottom` gets the rest. Same two-way-split-at-a-position shape
	/// as `gfx::oled::Rect::split_rows` and the SDK's `Frame::split_rows`.
	/// @param at Row offset to split at, relative to this rect's origin.
	/// @return The `(top, bottom)` rect pair.
	constexpr std::pair<Rect, Rect> split_rows(int32_t at) const { return {{x, y, w, at}, {x, y + at, w, h - at}}; }

	/// @brief Split into `(left, right)` at local column @p at, transposed from split_rows().
	///
	/// Matches `gfx::oled::Rect::split_cols` and the SDK's `Frame::split_cols`.
	/// @param at Column offset to split at, relative to this rect's origin.
	/// @return The `(left, right)` rect pair.
	constexpr std::pair<Rect, Rect> split_cols(int32_t at) const { return {{x, y, at, h}, {x + at, y, w - at, h}}; }

	/// @brief Shrink by @p d on every side (a negative @p d grows it instead).
	///
	/// Mirrors `gfx::oled::Rect::inset`'s naming and shape but with a single uniform amount
	/// rather than per-side `Insets` -- nothing in the grid toolkit needs asymmetric insets yet,
	/// and the SDK has no `inset` on its `Rect` at all, so this follows the OLED sibling instead.
	/// @param d Amount to shrink each side by.
	/// @return The inset rect.
	constexpr Rect inset(int32_t d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};
} // namespace deluge::gfx::grid
