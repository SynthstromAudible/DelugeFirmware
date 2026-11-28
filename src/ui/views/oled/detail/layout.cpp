#include "ui/views/oled/detail/layout.h"

#include "ui/gfx/oled/font.h"
#include "ui/widgets/core/state.h"
#include "ui/widgets/oled/text.h"

#include <algorithm>

namespace deluge::ui::views::oled::detail {

using gfx::oled::kTextSpacingX;
using gfx::oled::kTextSpacingY;

int32_t row_y(const widgets::oled::OledFrame& f, int32_t row) {
	return f.style.title_height + 3 + row * f.style.row_height;
}

int32_t last_row_y(const widgets::oled::OledFrame& f) {
	return f.bounds().h - (kTextSpacingY - 1);
}

gfx::oled::Rect body_rect(const widgets::oled::OledFrame& f, int32_t from_row) {
	const int32_t y = row_y(f, from_row);
	return {0, y, f.bounds().w, std::max(0, f.bounds().h - y)};
}

void text_pair(widgets::oled::OledFrame& f, int32_t y, RowText row) {
	const int32_t w = f.bounds().w;
	const int32_t right_x = w - static_cast<int32_t>(row.right.size()) * kTextSpacingX;
	if (!row.left.empty()) {
		const int32_t end = row.right.empty() ? w : right_x - 4;
		if (end > 0) {
			f.region({0, y, end, kTextSpacingY}, false, [&](widgets::oled::OledFrame& band) {
				band.glyphs(row.left, 0, 0, kTextSpacingX, kTextSpacingY);
			});
		}
	}
	if (!row.right.empty()) {
		f.glyphs(row.right, right_x, y, kTextSpacingX, kTextSpacingY);
	}
}

void text_centred(widgets::oled::OledFrame& f, int32_t y, std::string_view s) {
	if (!s.empty()) {
		const auto w = static_cast<int32_t>(s.size()) * kTextSpacingX;
		f.glyphs(s, (f.bounds().w - w) / 2, y, kTextSpacingX, kTextSpacingY);
	}
}

void text_rows(widgets::oled::OledFrame& f, std::span<const RowText> rows, int32_t first_row) {
	for (size_t i = 0; i < rows.size(); i++) {
		text_pair(f, row_y(f, first_row + static_cast<int32_t>(i)), rows[i]);
	}
}

void title_short_of(widgets::oled::OledFrame& f, std::string_view s, int32_t right_x) {
	f.region({0, 0, std::max(0, right_x - 4), f.style.title_height}, false,
	         [&](widgets::oled::OledFrame& band) { widgets::oled::title(band, s, false); });
	f.hline(f.style.title_height, 0, f.bounds().w - 1);
}

void big_text(widgets::oled::OledFrame& f, std::string_view s) {
	widgets::oled::text(f, {0, row_y(f, 0) - 1, f.bounds().w, 20}, s,
	                    {.font = gfx::oled::FontId::MetricBold20, .align = widgets::oled::Align::Centre});
}

void name_centred(widgets::oled::OledFrame& f, int32_t y, std::string_view s) {
	const int32_t w = widgets::oled::measure(s, gfx::oled::FontId::MetricBold9).w;
	f.glyphs(s, (f.bounds().w - w) / 2, y, gfx::oled::kTextTitleSpacingX, gfx::oled::kTextTitleSizeY);
}

void list_rows(widgets::oled::OledFrame& f, std::span<const widgets::oled::ListRow> rows, int16_t current) {
	f.region(f.bounds(), false, [&](widgets::oled::OledFrame& g) {
		widgets::core::ListState state{.cursor = current};
		widgets::core::ScrollText scroll{};
		widgets::oled::list(g, widgets::oled::menu_rect(g), state, rows, scroll, false);
	});
}

} // namespace deluge::ui::views::oled::detail
