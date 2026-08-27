#pragma once
#include "gfx/oled/fonts/fonts.h"
#include <cstdint>
#include <string_view>

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
	uint8_t render_height; // rows drawn (the old textHeight after normalisation).
	                       // Sentinel 0: render at whatever height the caller asked for,
	                       // unchanged (the legacy switch's `case 20: default:` branch set
	                       // fontNativeHeight but left textHeight alone, so an unlisted
	                       // height — e.g. 15, on a taller-than-48px panel — drew the 20px
	                       // font clipped/stretched to the caller's own height instead of
	                       // being forced to 20).
	int8_t y_adjust;       // added to pixelY before drawing (textHeight 9 → +1)
	bool monospaced;       // Apple ][: every glyph advances kTextSpacingX
};

/// Select the font a text height selects. Same table as the original Canvas::drawChar.
const Font& font_for_text_height(int32_t textHeight);

/// Glyph table index for a character; <= 0 means "not drawable". Lowercase folds to
/// uppercase because lowercase glyphs were snipped from the tables.
int32_t glyph_index(uint8_t c);

/// @{
/// @name Glyph metrics
///
/// These were `Canvas` members (`get_char_width_in_pixels` and friends) even though they
/// only ever read the font tables — nothing about them needs a framebuffer. The bodies live
/// here now; the `Canvas` members forward. A caller that only wants to measure a string (the
/// UI toolkit's `measure()`) no longer has to conjure a canvas to do it.

/// Width in pixels of one character at \p text_height. 0 when the character has no glyph.
int32_t char_width_px(uint8_t the_char, int32_t text_height);

/// Gap in pixels placed *after* \p the_char when it is followed by another character.
///
/// @param is_last_char no character follows, so no gap is added.
int32_t char_spacing_px(uint8_t the_char, int32_t text_height, bool is_last_char);

/// Width in pixels of a whole string at \p text_height.
int32_t string_width_px(std::string_view str, int32_t text_height);
/// @}

} // namespace deluge::gfx::oled
