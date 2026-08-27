#include "gfx/oled/font.h"

namespace deluge::gfx::oled {

namespace {
// Px5 also serves heights 5 AND 6 (no separate 6px table exists); as a side effect,
// width/spacing lookups for height 6 now select Px5 too. The legacy getCharWidthInPixels
// switch had no `case 6` and fell through to `default` (MetricBold20) for width queries at
// that height — a pre-existing inconsistency with drawChar's own height-6 handling (which
// already used Px5). No live caller passes textHeight 6, so this is unobservable either way.
constexpr Font kPx5{FontId::Px5, font_5px, font_5px_desc, 5, 5, 0, false};
constexpr Font kApple7{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 0, true};
constexpr Font kApple7Up{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 1, true};
constexpr Font kMetric9{FontId::MetricBold9, font_metric_bold_9px, font_metric_bold_9px_desc, 9, 9, 0, false};
constexpr Font kMetric13{FontId::MetricBold13, font_metric_bold_13px, font_metric_bold_13px_desc, 13, 13, 0, false};
// render_height 0: the legacy `case 20: default:` branch left textHeight as the caller's
// own value (only fontNativeHeight was set to 20) — see the Font::render_height comment.
constexpr Font kMetric20{FontId::MetricBold20, font_metric_bold_20px, font_metric_bold_20px_desc, 20, 0, 0, false};
} // namespace

const Font& font_for_text_height(int32_t textHeight) {
	switch (textHeight) {
	case 5:
	case 6:
		return kPx5;
	case 7:
	case 8:
		return kApple7;
	case 9:
		return kApple7Up;
	case 10:
		return kMetric9;
	case 13:
		return kMetric13;
	case 20:
	default:
		return kMetric20;
	}
}

int32_t glyph_index(uint8_t c) {
	// Copied verbatim from Canvas::getCharIndex (canvas.cpp).
	// 129 represents flat glyph
	if (c == 129) {
		c = '~' + 1;
	}
	else if (c > '~') {
		return 0;
	}

	if (c >= 'a') {
		if (c <= 'z') {
			c -= 32;
		}
		else {
			c -= 26; // Lowercase chars have been snipped out of the tables.
		}
	}

	int32_t charIndex = c - 0x20;
	return charIndex;
}

int32_t char_width_px(uint8_t the_char, int32_t text_height) {
	int32_t char_index = glyph_index(the_char);
	if (char_index <= 0) {
		return 0;
	}
	else if (text_height >= 7 && text_height <= 9) {
		// the smaller apple ][ is monospaced, so return standard width of each character
		return kTextSpacingX;
	}

	lv_font_glyph_dsc_t const* descriptor = font_for_text_height(text_height).glyphs;

	descriptor += char_index;
	return descriptor->w_px;
}

int32_t char_spacing_px(uint8_t the_char, int32_t text_height, bool is_last_char) {
	bool monospaced_font = (text_height >= 7 and text_height <= 9);
	// don't add space to the last character
	if (is_last_char) {
		return 0;
	}
	else if (the_char == ' ') {
		// smaller apple ][ font is monospaced, so spacing is different
		if (monospaced_font) {
			return kTextSpacingX;
		}
		// small font is spaced 2px
		else if (text_height <= 6) {
			return 2;
		}
		// if character is a space, make spacing 6px instead
		// (just need to add 5 since previous character added 1 after it)
		else {
			return 5;
		}
	}
	else {
		// smaller apple ][ font is monospaced, so no extra spacing needs to be added
		// as it's handled by the standard char width
		if (monospaced_font) {
			return 0;
		}
		// small font
		else if (text_height <= 6) {
			return 1;
		}
		// default spacing is 2 pixels for bold fonts
		else {
			return 2;
		}
	}
}

int32_t string_width_px(std::string_view str, int32_t text_height) {
	int32_t string_length = str.length();
	int32_t string_width = 0;
	int32_t char_idx = 0;
	for (char const c : str) {
		// `string_length`, not `string_length - 1`: char_idx never reaches it, so the last
		// character DOES get trailing spacing counted here even though draw_string does not
		// advance by it. Kept verbatim -- every legacy centring/right-aligning call site was
		// laid out against this width, so "fixing" it would shift text by a pixel or two.
		int32_t char_spacing = char_spacing_px(c, text_height, char_idx == string_length);
		int32_t char_width = char_width_px(c, text_height) + char_spacing;
		string_width += char_width;
		char_idx++;
	}
	return string_width;
}

} // namespace deluge::gfx::oled
