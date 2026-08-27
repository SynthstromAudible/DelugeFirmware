#pragma once
#include <cstdint>
#include <limits>
#include <utility>

namespace deluge::gfx::oled {

/// Panel geometry. `visible_top` rows are hidden by the faceplate; content is laid out
/// below them. This is NOT the controller's page offset (that stays in the BSP).
struct Geometry {
	uint16_t width;
	uint16_t height; // multiple of 8
	uint8_t visible_top;
	constexpr uint32_t pages() const { return height >> 3; }
	constexpr uint32_t bytes() const { return pages() * width; }
	constexpr int32_t visible_height() const { return height - visible_top; }
};

constexpr Geometry kDelugeOled{128, 48, 5};

/// Sentinels for "the canvas edge" / "the canvas centre" in x parameters, so call sites
/// written against a fixed 128-wide panel keep their defaults on any geometry.
///
/// Deliberately the two most negative int32_t values rather than -1: a caller can plausibly
/// *compute* -1 as an x coordinate (`width - render_length` on a panel narrower than the
/// content), and that arithmetic result would then be silently reinterpreted as "use the
/// whole width". Nothing computes INT32_MIN, so these can only be passed on purpose.
constexpr int32_t kEdge = std::numeric_limits<int32_t>::min();
constexpr int32_t kCentre = std::numeric_limits<int32_t>::min() + 1;

struct Point {
	int32_t x, y;
	constexpr bool operator==(const Point&) const = default;
};
struct Size {
	int32_t w, h;
};
struct Insets {
	int32_t top, right, bottom, left;
};

struct Rect {
	int32_t x, y, w, h;
	constexpr bool operator==(const Rect&) const = default;
	constexpr int32_t right() const { return x + w; }
	constexpr int32_t bottom() const { return y + h; }
	constexpr bool contains(Point p) const { return p.x >= x && p.x < right() && p.y >= y && p.y < bottom(); }
	constexpr Point centre() const { return {x + w / 2, y + h / 2}; }
	constexpr Rect inset(Insets i) const { return {x + i.left, y + i.top, w - i.left - i.right, h - i.top - i.bottom}; }
	constexpr std::pair<Rect, Rect> split_rows(int32_t at) const { return {{x, y, w, at}, {x, y + at, w, h - at}}; }
	constexpr std::pair<Rect, Rect> split_cols(int32_t at) const { return {{x, y, at, h}, {x + at, y, w - at, h}}; }
	constexpr Rect intersect(Rect o) const {
		int32_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
		int32_t nr = right() < o.right() ? right() : o.right();
		int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
		return {nx, ny, nr > nx ? nr - nx : 0, nb > ny ? nb - ny : 0};
	}
};

} // namespace deluge::gfx::oled
