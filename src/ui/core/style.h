#pragma once
#include "gfx/oled/font.h"
#include "ui/core/metrics.h"
#include <cstdint>

namespace deluge::ui {

/// Passed by const& into every frame. Chosen per board profile; "menu line count", "menu base
/// row" and "rounded corners" are values here, not globals on the rasteriser.
///
/// Every field is read by at least one widget -- the branch review's rule, so a board profile
/// cannot fill in a number that quietly does nothing. Where a field's value coincides with a
/// legacy metric from `core/metrics.h`, the widget that reads it carries a `static_assert` tying
/// the two together, so a profile edit that would move pixels fails to compile rather than
/// failing a golden.
///
/// A profile is really a `(Style, Geometry)` *pair*: the geometry decides how many rows exist and
/// the style decides where the menu starts inside them. `menu_base_y` is what keeps the two in
/// step -- it is the only panel-height-dependent number the OLED widgets need, and it now lives
/// here rather than being re-derived from `Canvas::geometry()` inside `list` and `output_name`.
struct Style {
	/// The title band's height: the row `title()` draws its separator rule on, and the first row
	/// below the title a caller may use. `= kTitleSeparatorY` on both shipped profiles.
	int32_t title_height;
	/// The vertical pitch of one `list` row, in pixels. `= kTextSpacingY` on both shipped
	/// profiles, which is the legacy menu-item pitch (`menu_item.cpp:56`).
	int32_t row_height;
	/// The x inset of a `list` row's label from the rect's left edge. `= kTextSpacingX` on both
	/// shipped profiles (`menu_item.cpp:58`).
	int32_t inset_x;
	/// The width `scrollbar()` draws its thumb, clamped to the rect it is given. No legacy
	/// counterpart -- the legacy menus scroll with no indicator at all -- so this is the toolkit's
	/// own number rather than a ported metric.
	int32_t scrollbar_w;
	/// How many rows `list` will show at once: legacy's `OLED_HEIGHT_CHARS - 1`.
	int32_t menu_rows;
	/// The `list`/`menu_rect` base row, `MenuItem::drawItemsForOled`'s `(OLED_MAIN_HEIGHT_PIXELS
	/// == 64) ? 15 : 14` (`menu_item.cpp:50`) as a profile value. Deliberately *not*
	/// `kHorizontalMenuBaseY`, which `columns` uses and which is 14 on every panel -- see the note
	/// on that constant in `core/metrics.h`.
	int32_t menu_base_y;
	bool rounded, invert_highlight;
	gfx::oled::FontId title_font, row_font, value_font, big_font;
};

inline constexpr Style kDelugeStyle48{.title_height = kTitleSeparatorY,
                                      .row_height = kTextSpacingY,
                                      .inset_x = kTextSpacingX,
                                      .scrollbar_w = 4,
                                      .menu_rows = 3,
                                      .menu_base_y = kMenuBaseY48,
                                      .rounded = true,
                                      .invert_highlight = true,
                                      .title_font = gfx::oled::FontId::MetricBold9,
                                      .row_font = gfx::oled::FontId::Apple7,
                                      .value_font = gfx::oled::FontId::MetricBold20,
                                      .big_font = gfx::oled::FontId::MetricBold20};

inline constexpr Style kDelugeStyle64{.title_height = kTitleSeparatorY,
                                      .row_height = kTextSpacingY,
                                      .inset_x = kTextSpacingX,
                                      .scrollbar_w = 4,
                                      .menu_rows = 5,
                                      .menu_base_y = kMenuBaseY64,
                                      .rounded = true,
                                      .invert_highlight = true,
                                      .title_font = gfx::oled::FontId::MetricBold9,
                                      .row_font = gfx::oled::FontId::Apple7,
                                      .value_font = gfx::oled::FontId::MetricBold20,
                                      .big_font = gfx::oled::FontId::MetricBold20};

// The shipped profiles' values, pinned against the legacy metrics they reproduce. Every widget
// that reads one of these fields reads it *instead of* the constant on the right; if a profile
// ever moves one, these fail to compile rather than moving pixels under a golden.
static_assert(kDelugeStyle48.title_height == kTitleSeparatorY && kDelugeStyle64.title_height == kTitleSeparatorY);
static_assert(kDelugeStyle48.row_height == kTextSpacingY && kDelugeStyle64.row_height == kTextSpacingY);
static_assert(kDelugeStyle48.inset_x == kTextSpacingX && kDelugeStyle64.inset_x == kTextSpacingX);
static_assert(kDelugeStyle48.menu_base_y == kMenuBaseY48 && kDelugeStyle64.menu_base_y == kMenuBaseY64);

} // namespace deluge::ui
