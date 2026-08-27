#pragma once
#include "gfx/oled/fonts/fonts.h"
#include <cstdint>

namespace deluge::gfx::oled {

// Text metric constants (were in definitions_cxx.hpp).
constexpr int32_t kTextSpacingX = 6; // 5px glyph + 1px gap
constexpr int32_t kTextSpacingY = 9; // 7px glyph + 1px above and below
constexpr int32_t kTextSizeYUpdated = 7;
constexpr int32_t kTextTitleSpacingX = 9;
constexpr int32_t kTextTitleSizeY = 10;
constexpr int32_t kTextBigSizeY = 13;
constexpr int32_t kTextHugeSpacingX = 18;
constexpr int32_t kTextHugeSizeY = 20;

enum class FontId : uint8_t { Px5, Apple7, MetricBold9, MetricBold13, MetricBold20 };

struct Font {
	FontId id;
	const uint8_t* bitmap;
	const lv_font_glyph_dsc_t* glyphs;
	uint8_t native_height; // rows in the bitmap
	uint8_t render_height; // rows drawn (the old textHeight after normalisation)
	int8_t y_adjust;       // added to pixelY before drawing (textHeight 9 → +1)
	bool monospaced;       // Apple ][: every glyph advances kTextSpacingX
};

/// Select the font a text height selects. Same table as the original Canvas::drawChar.
const Font& font_for_text_height(int32_t textHeight);

/// Glyph table index for a character; <= 0 means "not drawable". Lowercase folds to
/// uppercase because lowercase glyphs were snipped from the tables.
int32_t glyph_index(uint8_t c);

} // namespace deluge::gfx::oled
