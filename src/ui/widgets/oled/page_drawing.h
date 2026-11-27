#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cmath>
#include <cstdint>
#include <span>

namespace deluge::ui::widgets::oled {

/// @brief The area a page drawn whole (envelope, EQ) draws its shape in, in frame-local coordinates.
///
/// The EQ page's geometry (`EqMenu::renderMenuItems`, `origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`), which
/// every page drawing shares: one column in from the envelope's 4-px padding on the left, 4 px on the right, 3 rows
/// under the title rule, 6 rows above the frame's bottom. All four edges are inclusive.
struct PageBody {
	int32_t left;
	int32_t right;
	int32_t top;
	int32_t bottom;

	/// @return The rows from top to bottom, exclusive of one end: `bottom - top`.
	[[nodiscard]] int32_t height() const { return bottom - top; }
	/// @return The row half way down, rounded up: the EQ's centre line.
	[[nodiscard]] int32_t center_y() const { return top + (bottom - top) / 2; }
	/// @brief The row of a level.
	///
	/// In `double` and truncated, as the EQ's `uint8_t y = std::lerp(end_y, end_y - height, level)` is: integer
	/// arguments to `std::lerp` select its `double` overload, and that product is exact, so the row is the same in
	/// local and canvas coordinates.
	/// @param level 0 at the bottom, 1 at the top.
	[[nodiscard]] int32_t y_at(float level) const {
		return static_cast<int32_t>(
		    std::lerp(static_cast<double>(bottom), static_cast<double>(top), static_cast<double>(level)));
	}
};

/// @brief The page body of @p f, which must be the frame the menu hands a page drawing (the visible panel).
PageBody page_body(const OledFrame& f);

/// @brief A level at @p t in [0, 1]: 0 the body's bottom, 1 its top. @p ctx is what the caller passed to `curve`.
using LevelFn = float (*)(float t, const void* ctx);

/// @brief A 1-px curve from column @p x0 to @p x1: @p steps straight segments, each end's x rounded and its row
///        `b.y_at(level(t))`.
void curve(OledFrame& f, const PageBody& b, float x0, float x1, LevelFn level, const void* ctx, int32_t steps = 12);

/// @brief Straight 1-px segments joining @p pts in order.
void polyline(OledFrame& f, std::span<const gfx::oled::Point> pts);

/// @brief A dotted vertical guide at column @p x.
///
/// Dots on the rows `b.top - 1 + 4k` only, so every guide on a page shares one dot grid; from the first such row at
/// or below `from_y - 1` down to `b.bottom + 1`. A full-height guide passes `from_y = b.top`.
void guide_v(OledFrame& f, const PageBody& b, int32_t x, int32_t from_y);

/// @brief A dotted horizontal guide on row @p y: a dot where `x % 6 == 3`, for @p x_from <= x <= @p x_to, none
///        within one column of any of @p skip_x (the EQ keeps its centre line clear of its band edges).
void guide_h(OledFrame& f, int32_t y, int32_t x_from, int32_t x_to, std::span<const int32_t> skip_x = {});

/// @brief A 1-px line from @p a to @p b that also fills below itself down to row @p fill_bottom.
///
/// `FilterContainer::render`'s `draw_fill_pattern` (`origin/main:src/deluge/gui/menu_item/filter/filter_container.h`):
/// below each point of the line in a column where `x % 3 == 0`, a dot on each row where `y % 3 == 1`, both in
/// *canvas* coordinates so the dot grid is the panel's wherever the frame sits. Only columns @p min_x..@p max_x are
/// drawn, the line included. All arguments are frame-local.
void fill_line(OledFrame& f, gfx::oled::Point a, gfx::oled::Point b, int32_t fill_bottom, int32_t min_x, int32_t max_x);

/// @brief The square handles on a page drawing's points.
///
/// `drawControlIndicator` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`): a 5x5 outline with its 3x3 interior
/// cleared, the focused one's interior inverted. An unfocused handle drawn where the focused one already is, is
/// skipped, so the focused stays on top; draw the handles after every other mark.
struct Handles {
	OledFrame& f;
	int32_t focused_x = -1;
	int32_t focused_y = -1;

	void draw(int32_t x, int32_t y, bool focused);
};

} // namespace deluge::ui::widgets::oled
