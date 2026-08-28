#pragma once
#include "gfx/contract.h"
#include <cstdint>
#include <utility>

namespace deluge::gfx::grid {
/// Rectangle in pad units (columns, rows). Same shape as gfx::oled::Rect; kept separate so
/// the two gfx libraries stay independent.
struct Rect {
	int32_t x, y, w, h;
	constexpr bool operator==(const Rect&) const = default;
	constexpr int32_t right() const { return x + w; }
	constexpr int32_t bottom() const { return y + h; }
	constexpr bool contains(int32_t px, int32_t py) const {
		return px >= x && px < right() && py >= y && py < bottom();
	}
	constexpr Rect intersect(Rect o) const {
		int32_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
		int32_t nr = right() < o.right() ? right() : o.right();
		int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
		return {nx, ny, nr > nx ? nr - nx : 0, nb > ny ? nb - ny : 0};
	}

	/// Split into `(top, bottom)` at local row `at` -- `top` gets rows `[0, at)`, `bottom` gets
	/// the rest. Same two-way-at-a-position shape as `gfx::oled::Rect::split_rows`
	/// (`gfx/oled/geometry.h:50`, used at `src/ui/oled/misc.cpp:126` as `rect.split_rows(rect.h /
	/// 2)`) and the SDK's `Frame::split_rows` (`imode.rs:367-382`) -- task-6 fix round 1 replaced
	/// an earlier `(n, i)` n-way form here that matched neither sibling.
	constexpr std::pair<Rect, Rect> split_rows(int32_t at) const { return {{x, y, w, at}, {x, y + at, w, h - at}}; }

	/// Split into `(left, right)` at local column `at`, transposed from `split_rows`. Matches
	/// `gfx::oled::Rect::split_cols` (`gfx/oled/geometry.h:51`) and the SDK's `Frame::split_cols`
	/// (`imode.rs:384-398`).
	constexpr std::pair<Rect, Rect> split_cols(int32_t at) const { return {{x, y, at, h}, {x + at, y, w - at, h}}; }

	/// Shrink by `d` on every side (negative `d` grows it), mirroring `gfx::oled::Rect::inset`'s
	/// naming and shape but with a single uniform amount rather than per-side `Insets` -- nothing
	/// in the grid toolkit needs asymmetric insets yet, and the SDK has no `inset` on its `Rect`
	/// at all (`imode.rs:127-175`), so this follows the OLED sibling instead.
	constexpr Rect inset(int32_t d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};
} // namespace deluge::gfx::grid
