#pragma once
#include "gfx/contract.h"
#include <cstdint>

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

	/// Slice `i` of `n` equal-ish row-bands, full width, stacked top to bottom. `h` divides as
	/// evenly as it can: `h / n` rows per slice, with the first `h % n` slices one row taller so
	/// the remainder lands at the top rather than vanishing off the bottom edge. E.g. 8 rows
	/// split 3 ways is 3, 3, 2 -- not the SDK's shape (`imode.rs` `Frame::split_rows` at line 367
	/// takes a single split point `at` and returns a `(top, bottom)` pair, not an n-way index);
	/// this is the toolkit-fixes task-6 ruling's own signature, generalising that two-way split
	/// to n slices with the same "front absorbs the remainder" rule spelled out in the ruling's
	/// worked example.
	///
	/// `n > 0` and `0 <= i < n` is a caller precondition (an out-of-range slice index is a
	/// programming error, not a value that arrives at runtime), so it traps rather than clamping.
	constexpr Rect split_rows(int32_t n, int32_t i) const {
		DELUGE_GFX_CONTRACT(n > 0 && i >= 0 && i < n);
		const int32_t base = h / n;
		const int32_t rem = h % n;
		const int32_t size = base + (i < rem ? 1 : 0);
		const int32_t offset = i * base + (i < rem ? i : rem);
		return {x, y + offset, w, size};
	}

	/// Slice `i` of `n` equal-ish column-bands, full height, left to right. Same remainder rule
	/// as `split_rows`, transposed: `w / n` columns per slice, first `w % n` slices one column
	/// wider.
	constexpr Rect split_cols(int32_t n, int32_t i) const {
		DELUGE_GFX_CONTRACT(n > 0 && i >= 0 && i < n);
		const int32_t base = w / n;
		const int32_t rem = w % n;
		const int32_t size = base + (i < rem ? 1 : 0);
		const int32_t offset = i * base + (i < rem ? i : rem);
		return {x + offset, y, size, h};
	}

	/// Shrink by `d` on every side (negative `d` grows it), mirroring `gfx::oled::Rect::inset`'s
	/// naming and shape but with a single uniform amount rather than per-side `Insets` -- nothing
	/// in the grid toolkit needs asymmetric insets yet, and the SDK has no `inset` on its `Rect`
	/// at all (`imode.rs:127-175`), so this follows the OLED sibling instead.
	constexpr Rect inset(int32_t d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};
} // namespace deluge::gfx::grid
