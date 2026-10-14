#include "gfx/oled/font.h"

#include <algorithm>
#include <array>

namespace deluge::gfx::oled {

namespace {
// Px5 also serves heights 5 and 6 -- there's no separate 6px table. This unifies drawChar's
// and getCharWidthInPixels' previously-disagreeing height-6 behaviour (draw used Px5; width
// queries fell through to MetricBold20). No live caller passes textHeight 6, so the
// difference is unobservable either way.
constexpr Font kPx5{FontId::Px5, font_5px, font_5px_desc, 5, 5, 0, false};
constexpr Font kApple7{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 0, true};
constexpr Font kApple7Up{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 1, true};
constexpr Font kMetric9{FontId::MetricBold9, font_metric_bold_9px, font_metric_bold_9px_desc, 9, 9, 0, false};
constexpr Font kMetric13{FontId::MetricBold13, font_metric_bold_13px, font_metric_bold_13px_desc, 13, 13, 0, false};
// render_height 0: see Font::render_height for the sentinel's meaning.
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

namespace {

struct KerningRule {
	uint8_t text_height;
	uint8_t previous_char;
	uint8_t current_char;
	int8_t adjustment;
};

/// Letters kern case-insensitively: the rules are written in uppercase.
constexpr uint8_t normalise_kerning_char(uint8_t the_char) {
	if (the_char >= 'a' && the_char <= 'z') {
		return static_cast<uint8_t>(the_char - ('a' - 'A'));
	}
	return the_char;
}

/// Kerning rules for character pairs at each text height.
constexpr auto kKerningRules = std::to_array<KerningRule>({
    // Title/menu font (text height 10).
    {.text_height = 10, .previous_char = 'V', .current_char = 'A', .adjustment = -1},
    {.text_height = 10, .previous_char = 'W', .current_char = 'A', .adjustment = -2},
    {.text_height = 10, .previous_char = 'W', .current_char = 'J', .adjustment = -1},
    {.text_height = 10, .previous_char = '7', .current_char = 'J', .adjustment = -1},
    {.text_height = 10, .previous_char = 'W', .current_char = 'O', .adjustment = -1},
    {.text_height = 10, .previous_char = 'Y', .current_char = 'O', .adjustment = -1},
    {.text_height = 10, .previous_char = 'N', .current_char = 'R', .adjustment = 1},
    {.text_height = 10, .previous_char = 'Y', .current_char = 'S', .adjustment = -1},
    {.text_height = 10, .previous_char = 'L', .current_char = 'T', .adjustment = -1},
    {.text_height = 10, .previous_char = '"', .current_char = 'T', .adjustment = -1},
    {.text_height = 10, .previous_char = '4', .current_char = 'T', .adjustment = -1},
    {.text_height = 10, .previous_char = '6', .current_char = 'T', .adjustment = -1},
    {.text_height = 10, .previous_char = 'L', .current_char = 'V', .adjustment = -1},
    {.text_height = 10, .previous_char = 'L', .current_char = 'Y', .adjustment = -1},
    {.text_height = 10, .previous_char = 'O', .current_char = 'Y', .adjustment = -1},
    {.text_height = 10, .previous_char = 'W', .current_char = '.', .adjustment = -2},
    {.text_height = 10, .previous_char = 'F', .current_char = '.', .adjustment = -2},
    {.text_height = 10, .previous_char = 'T', .current_char = '.', .adjustment = -1},
    {.text_height = 10, .previous_char = 'V', .current_char = '.', .adjustment = -1},
    {.text_height = 10, .previous_char = '7', .current_char = '.', .adjustment = -1},
    {.text_height = 10, .previous_char = 'T', .current_char = ',', .adjustment = -1},
    {.text_height = 10, .previous_char = 'V', .current_char = ',', .adjustment = -1},
    {.text_height = 10, .previous_char = '7', .current_char = ',', .adjustment = -1},
    {.text_height = 10, .previous_char = 'L', .current_char = '"', .adjustment = -2},
    {.text_height = 10, .previous_char = 'L', .current_char = '\'', .adjustment = -1},
    {.text_height = 10, .previous_char = 'V', .current_char = '/', .adjustment = -1},
    {.text_height = 10, .previous_char = '7', .current_char = '4', .adjustment = -1},

    // 13px font.
    {.text_height = 13, .previous_char = 'W', .current_char = 'A', .adjustment = -4},
    {.text_height = 13, .previous_char = '-', .current_char = 'N', .adjustment = 2},
    {.text_height = 13, .previous_char = 'E', .current_char = 'S', .adjustment = 1},
    {.text_height = 13, .previous_char = 'L', .current_char = 'T', .adjustment = -1},
    {.text_height = 13, .previous_char = '4', .current_char = 'T', .adjustment = -1},
    {.text_height = 13, .previous_char = '6', .current_char = 'T', .adjustment = -1},
    {.text_height = 13, .previous_char = '1', .current_char = '4', .adjustment = -1},
    {.text_height = 13, .previous_char = '6', .current_char = '4', .adjustment = -1},
    {.text_height = 13, .previous_char = '7', .current_char = '4', .adjustment = -2},
    {.text_height = 13, .previous_char = '7', .current_char = '6', .adjustment = -1},
    {.text_height = 13, .previous_char = '7', .current_char = '8', .adjustment = -1},
    {.text_height = 13, .previous_char = '2', .current_char = '9', .adjustment = -1},
    {.text_height = 13, .previous_char = '7', .current_char = '9', .adjustment = -1},

    // 20px font.
    {.text_height = 20, .previous_char = '2', .current_char = '1', .adjustment = -1},
    {.text_height = 20, .previous_char = '2', .current_char = '7', .adjustment = -1},
});

} // namespace

int32_t kerning_px(uint8_t previous_char, uint8_t current_char, int32_t text_height) {
	if (previous_char == ' ' || current_char == ' ') {
		return 0;
	}

	// The 10px font sets a hyphen 1px further from its neighbours.
	if (text_height == 10
	    && ((previous_char == '-' && current_char != ' ') || (previous_char != ' ' && current_char == '-'))) {
		return 1;
	}

	previous_char = normalise_kerning_char(previous_char);
	current_char = normalise_kerning_char(current_char);

	constexpr int32_t kMaxTextHeight = 255;
	if (text_height < 0 || text_height > kMaxTextHeight) {
		return 0;
	}
	const auto rule_text_height = static_cast<uint8_t>(text_height);

	for (const KerningRule& rule : kKerningRules) {
		if (rule.text_height == rule_text_height && rule.previous_char == previous_char
		    && rule.current_char == current_char) {
			return rule.adjustment;
		}
	}
	return 0;
}

int32_t string_width_px(std::string_view str, int32_t text_height) {
	int32_t advance_x = 0;
	int32_t max_glyph_end_x = 0;

	auto chars_remaining = str.size();
	bool has_previous_char = false;
	uint8_t previous_char = 0;
	for (char const c : str) {
		const auto current_char = static_cast<uint8_t>(c);
		--chars_remaining;
		const int32_t char_width = char_width_px(current_char, text_height);
		// The last character's spacing is not advanced over, but its draw box is the one draw_string
		// centres the glyph in, which keeps its spacing.
		const int32_t advance_spacing = char_spacing_px(current_char, text_height, chars_remaining == 0);
		const int32_t draw_spacing = char_spacing_px(current_char, text_height, false);
		const int32_t draw_width = char_width + draw_spacing;
		const int32_t advance_width = char_width + advance_spacing;

		if (has_previous_char) {
			advance_x += kerning_px(previous_char, current_char, text_height);
		}

		if (char_width > 0) {
			const int32_t glyph_start_x = advance_x + ((draw_width - char_width) >> 1);
			max_glyph_end_x = std::max(max_glyph_end_x, glyph_start_x + char_width);
		}

		advance_x += advance_width;
		previous_char = current_char;
		has_previous_char = true;
	}
	return std::max(advance_x, max_glyph_end_x);
}

} // namespace deluge::gfx::oled
