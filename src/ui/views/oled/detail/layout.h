#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The row layout the OLED views share: text rows under the title band, the body below a row, a big value and
///        a centred name.

namespace deluge::ui::views::oled::detail {

/// @brief One row of text: a label on the left and a value ending at the right edge. Either may be empty.
struct RowText {
	std::string_view left;
	std::string_view right;
};

/// @brief The y of text row @p row: the first row starts 3 px under the title band's rule
///        (`style.title_height + 3`), the rest `style.row_height` apart.
int32_t row_y(const widgets::oled::OledFrame& f, int32_t row);

/// @brief The full-width rect from text row @p from_row's y to the frame's bottom.
gfx::oled::Rect body_rect(const widgets::oled::OledFrame& f, int32_t from_row);

/// @brief One row of Apple7 text at @p y (`glyphs` at `kTextSpacingX` by `kTextSpacingY`): @p row's left text at
///        x = 0, its right text ending at the frame's right edge, the left clipped to stop 4 px before the right.
///        An empty string draws nothing.
void text_pair(widgets::oled::OledFrame& f, int32_t y, RowText row);

/// @brief `text_pair` for each of @p rows on text rows `first_row`, `first_row + 1`, ...
void text_rows(widgets::oled::OledFrame& f, std::span<const RowText> rows, int32_t first_row = 0);

/// @brief A big value in MetricBold20, centred across the frame at `row_y(f, 0) - 1`.
void big_text(widgets::oled::OledFrame& f, std::string_view s);

/// @brief A name in the title face (MetricBold9), centred across the frame at @p y.
void name_centred(widgets::oled::OledFrame& f, int32_t y, std::string_view s);

} // namespace deluge::ui::views::oled::detail
