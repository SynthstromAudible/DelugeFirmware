#include "gfx/oled/font.h"

namespace deluge::gfx::oled {

namespace {
constexpr Font kPx5{FontId::Px5, font_5px, font_5px_desc, 5, 5, 0, false};
constexpr Font kApple7{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 0, true};
constexpr Font kApple7Up{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 1, true};
constexpr Font kMetric9{FontId::MetricBold9, font_metric_bold_9px, font_metric_bold_9px_desc, 9, 9, 0, false};
constexpr Font kMetric13{FontId::MetricBold13, font_metric_bold_13px, font_metric_bold_13px_desc, 13, 13, 0, false};
constexpr Font kMetric20{FontId::MetricBold20, font_metric_bold_20px, font_metric_bold_20px_desc, 20, 20, 0, false};
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

} // namespace deluge::gfx::oled
