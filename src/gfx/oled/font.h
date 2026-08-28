#pragma once
#include "gfx/oled/fonts/fonts.h"
#include <cstdint>
#include <string_view>

namespace deluge::gfx::oled {

// Text-layout metric constants used by the UI toolkit.
constexpr int32_t kTextSpacingX = 6; // 5px glyph + 1px gap
constexpr int32_t kTextSpacingY = 9; // 7px glyph + 1px above and below
constexpr int32_t kTextSizeYUpdated = 7;
constexpr int32_t kTextTitleSpacingX = 9;
constexpr int32_t kTextTitleSizeY = 10;
constexpr int32_t kTextBigSizeY = 13;
constexpr int32_t kTextHugeSpacingX = 18;
constexpr int32_t kTextHugeSizeY = 20;

/// @brief Which glyph bitmap/table pair a Font uses.
enum class FontId : uint8_t { Px5, Apple7, MetricBold9, MetricBold13, MetricBold20 };

/// @brief One selectable OLED text style: a glyph bitmap/table pair plus the render metrics
/// drawChar and the metric helpers below need to lay it out.
struct Font {
	FontId id;
	const uint8_t* bitmap;
	const lv_font_glyph_dsc_t* glyphs;
	uint8_t native_height; ///< Rows in the bitmap.
	/// Rows actually drawn.
	///
	/// @note Sentinel 0: render at whatever height the caller asked for, unchanged. An unlisted
	///       requested height (e.g. 15, on a taller-than-48px panel) then draws the 20px font
	///       clipped/stretched to the caller's own height instead of being forced to 20.
	uint8_t render_height;
	int8_t y_adjust; ///< Added to pixelY before drawing (render_height 9 -> +1, for the 9-row Apple face).
	bool monospaced; ///< Apple ][: every glyph advances kTextSpacingX regardless of its own width.
};

/// @brief Look up the Font that renders at a given text height.
/// @param textHeight Requested pixel height (5/6/7/8/9/10/13/20). Any other height falls back
///        to the 20px face.
/// @return The matching Font.
const Font& font_for_text_height(int32_t textHeight);

/// @brief Glyph table index for a character.
///
/// Lowercase folds to uppercase because lowercase glyphs were snipped from the tables.
/// @param c Character to look up.
/// @return Index into a font's glyph table; <= 0 means "not drawable".
int32_t glyph_index(uint8_t c);

/// @{
/// @name Glyph metrics
///
/// Pure functions over the font tables -- no framebuffer needed. `Canvas` forwards to these
/// for callers that already have one; a caller that only wants to measure a string (e.g. the
/// UI toolkit's `measure()`) does not need to conjure a canvas just to do it.

/// @brief Width in pixels of one character.
/// @param the_char Character to measure.
/// @param text_height Font height to measure at.
/// @return Width in pixels; 0 when the character has no glyph.
int32_t char_width_px(uint8_t the_char, int32_t text_height);

/// @brief Gap in pixels placed after a character when it is followed by another character.
/// @param the_char Character being spaced.
/// @param text_height Font height in use.
/// @param is_last_char True when no character follows, so no gap is added.
/// @return Gap width in pixels.
int32_t char_spacing_px(uint8_t the_char, int32_t text_height, bool is_last_char);

/// @brief Width in pixels of a whole string.
/// @param str String to measure.
/// @param text_height Font height to measure at.
/// @return Width in pixels.
int32_t string_width_px(std::string_view str, int32_t text_height);
/// @}

} // namespace deluge::gfx::oled
