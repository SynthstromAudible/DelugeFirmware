#pragma once
#include "gfx/oled/font.h"
#include <cstdint>

namespace deluge::ui {

/// Passed by const& into every frame. Chosen per board profile; "menu line count" and
/// "rounded corners" are values here, not globals on the rasteriser.
struct Style {
	int32_t title_height, row_height, inset_x, scrollbar_w, menu_rows;
	bool rounded, invert_highlight;
	gfx::oled::FontId title_font, row_font, value_font, big_font;
};

constexpr Style kDelugeStyle48{.title_height = 12,
                               .row_height = 9,
                               .inset_x = 6,
                               .scrollbar_w = 4,
                               .menu_rows = 3,
                               .rounded = true,
                               .invert_highlight = true,
                               .title_font = gfx::oled::FontId::MetricBold9,
                               .row_font = gfx::oled::FontId::Apple7,
                               .value_font = gfx::oled::FontId::MetricBold20,
                               .big_font = gfx::oled::FontId::MetricBold20};

constexpr Style kDelugeStyle64{.title_height = 12,
                               .row_height = 9,
                               .inset_x = 6,
                               .scrollbar_w = 4,
                               .menu_rows = 5,
                               .rounded = true,
                               .invert_highlight = true,
                               .title_font = gfx::oled::FontId::MetricBold9,
                               .row_font = gfx::oled::FontId::Apple7,
                               .value_font = gfx::oled::FontId::MetricBold20,
                               .big_font = gfx::oled::FontId::MetricBold20};

} // namespace deluge::ui
