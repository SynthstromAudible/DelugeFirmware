#pragma once
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
};
} // namespace deluge::gfx::grid
