#include "ui/oled/text.h"

#include "ui/core/metrics.h"

namespace deluge::ui::oled {

using gfx::oled::FontId;
using gfx::oled::Rect;
using gfx::oled::Size;
using gfx::oled::string_width_px;

int32_t text_height_for(FontId font) {
	switch (font) {
	case FontId::Px5:
		return 5;
	case FontId::Apple7:
		return 7;
	case FontId::MetricBold9:
		return kTextTitleSizeY; // 10
	case FontId::MetricBold13:
		return kTextBigSizeY; // 13
	case FontId::MetricBold20:
	default:
		return kTextHugeSizeY; // 20
	}
}

int32_t spacing_x_for(FontId font) {
	switch (font) {
	case FontId::Px5:
		return 6;
	case FontId::Apple7:
		return kTextSpacingX; // 6
	case FontId::MetricBold9:
	case FontId::MetricBold13:
		return kTextTitleSpacingX; // 9
	case FontId::MetricBold20:
	default:
		return kTextHugeSpacingX; // 18
	}
}

Size measure(std::string_view s, FontId font) {
	const int32_t h = text_height_for(font);
	return Size{string_width_px(s, h), h};
}

namespace {

/// Port of `Canvas::drawStringCentredShrinkIfNecessary`, with the rect's width standing in
/// for the panel width the legacy routine hard-coded.
///
/// The unsigned arithmetic is the legacy arithmetic, with `s.length()` narrowed to uint32_t
/// so the host (64-bit size_t) and the device (32-bit) agree bit for bit.
void centred_shrink(OledFrame& f, Rect rect, std::string_view s, int32_t text_width, int32_t text_height) {
	const uint32_t length = static_cast<uint32_t>(s.length());
	if (length == 0 || rect.w <= 0) {
		return; // the legacy routine divided by length without checking
	}

	bool shrink = false;
	const int32_t max_text_width = static_cast<uint32_t>(rect.w) / length;
	if (text_width > max_text_width) {
		// Step down the ladder of fonts that actually exist, then re-centre vertically in the
		// space the original height would have taken.
		int32_t new_height = static_cast<uint32_t>(text_height * max_text_width) / static_cast<uint32_t>(text_width);
		if (new_height >= 20) {
			new_height = 20;
		}
		else if (new_height >= 13) {
			new_height = 13;
		}
		else if (new_height >= 10) {
			new_height = 10;
		}
		else if (new_height >= 7) {
			new_height = 7;
		}
		else {
			new_height = 5;
		}

		text_width = max_text_width;

		const int32_t height_diff = text_height - new_height;
		rect.y += height_diff >> 1;
		text_height = new_height;

		shrink = true;
	}
	const int32_t pixel_x = rect.x + static_cast<int32_t>((static_cast<uint32_t>(rect.w) - text_width * length) >> 1);
	f.glyphs(s, pixel_x, rect.y, text_width, text_height, 0, rect.right(), shrink);
}

} // namespace

void text(OledFrame& f, Rect rect, std::string_view s, TextStyle ts) {
	const int32_t height = text_height_for(ts.font);
	const int32_t spacing = spacing_x_for(ts.font);

	if (ts.shrink) {
		centred_shrink(f, rect, s, spacing, height);
		return;
	}

	int32_t x = rect.x;
	switch (ts.align) {
	case Align::Left:
		break;
	case Align::Centre:
		// drawStringCentred: `centrePos - (stringWidth >> 1)`, about the rect's centre. NOT
		// `(rect.w - width) / 2` -- for an odd string width those differ by a pixel, and the
		// legacy routines both land the extra pixel on the left (i.e. text 1px right).
		x = rect.x + rect.w / 2 - (measure(s, ts.font).w >> 1);
		break;
	case Align::Right:
		// drawStringAlignRight: `rightPos - stringWidth`, rightPos exclusive.
		x = rect.right() - measure(s, ts.font).w;
		break;
	}
	f.glyphs(s, x, rect.y, spacing, height, 0, rect.right());
}

void title(OledFrame& f, std::string_view s, bool underline) {
	f.glyphs(s, 0, kTitleY, kTextTitleSpacingX, kTextTitleSizeY);
	if (underline) {
		f.hline(kTitleSeparatorY, 0, f.bounds().w - 1);
	}
}

} // namespace deluge::ui::oled
