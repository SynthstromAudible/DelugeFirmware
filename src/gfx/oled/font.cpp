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

} // namespace deluge::gfx::oled
