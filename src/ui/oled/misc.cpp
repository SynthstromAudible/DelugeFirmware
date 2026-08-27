#include "ui/oled/misc.h"

#include "ui/core/metrics.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace deluge::ui::oled {

using gfx::oled::Point;
using gfx::oled::Rect;
using gfx::oled::string_width_px;

namespace {

/// `slotToString(slot, sub_slot, buffer, 1)` (foundation/string_format.c:205-213): the slot in
/// decimal, then `'A' + sub_slot` unless `sub_slot` is -1.
std::string_view slot_text(int32_t slot, int32_t sub_slot, std::span<char> buf) {
	std::string_view digits = detail::int_to_string(slot, 1, buf);
	if (sub_slot != -1 && digits.size() < buf.size()) {
		buf[digits.size()] = static_cast<char>(sub_slot + 'A');
		return std::string_view{buf.data(), digits.size() + 1};
	}
	return digits;
}

/// One channel of `vu`, as a run of lit segments across \p band with a one-row gap under it.
void vu_channel(OledFrame& f, Rect band, uint8_t level) {
	// (level + 1) * 8 / 256: 0 lights nothing, 255 lights all eight -- the eight steps
	// getMaxYDisplayForVUMeter quantises to (view.cpp:1813-1846).
	const int32_t lit = (static_cast<int32_t>(level) + 1) * 8 / 256;
	const int32_t h = std::max<int32_t>(1, band.h - 1);
	for (int32_t i = 0; i < lit; i++) {
		const int32_t x0 = band.x + i * band.w / 8;
		// `- 2`, not `- 1`: the segment's last column is *inclusive*, so stopping one short of the
		// next segment's first column still leaves them touching. This leaves one dark column
		// between segments, which is the whole point of drawing eight of them rather than a bar.
		const int32_t x1 = band.x + (i + 1) * band.w / 8 - 2;
		if (x1 >= x0) {
			f.invert(Rect{x0, band.y, x1 - x0 + 1, h});
		}
	}
}

} // namespace

void output_name(OledFrame& f, Rect rect, const OutputNameData& data) {
	// `#if OLED_MAIN_HEIGHT_PIXELS == 64` (view.cpp:2093-2097, :2115-2119), as a value.
	const bool tall = f.canvas.geometry().height == 64;
	const int32_t type_y = rect.y + (tall ? kOutputTypeY64 : kOutputTypeY48);
	const int32_t name_y = rect.y + (tall ? kOutputNameY64 : kOutputNameY48);

	// drawStringCentred(outputTypeText, yPos, kTextSpacingX, kTextSpacingY) -- view.cpp:2098.
	// f.style.row_font is Apple7 on both profiles, i.e. that exact pair.
	const int32_t type_h = text_height_for(f.style.row_font);
	text(f, Rect{rect.x, type_y, rect.w, type_h}, data.type_text,
	     TextStyle{.font = f.style.row_font, .align = Align::Centre});

	std::array<char, 13> buf{}; // char buffer[12] (view.cpp:2101), plus the suffix
	const std::string_view to_draw = data.name_empty ? slot_text(data.slot, data.sub_slot, std::span{buf}) : data.name;

	// drawStringCentred / drawString at kTextTitleSpacingX, kTextTitleSizeY (view.cpp:2126-2129);
	// f.style.title_font is MetricBold9 on both profiles, i.e. that pair.
	const gfx::oled::FontId font = f.style.title_font;
	const int32_t name_h = text_height_for(font);
	// `stringLengthPixels <= OLED_MAIN_WIDTH_PIXELS` (view.cpp:2123-2125), against the rect.
	const bool fits = string_width_px(to_draw, name_h) <= rect.w;
	text(f, Rect{rect.x, name_y, rect.w, name_h}, to_draw,
	     TextStyle{.font = font, .align = fits ? Align::Centre : Align::Left});
}

void segment_digits(OledFrame& f, Rect rect, std::span<const uint8_t, 4> segs) {
	// Legacy's rows are canvas-absolute (it drew straight onto `main` with no topmost-pixel
	// term), so they are converted the way decimal_value converts its 20 and 41.
	const auto row = [&](int32_t absolute) { return rect.y + absolute - f.origin.y; };

	for (int32_t i = 0; i < 4; i++) {
		const int32_t ix = rect.x + 33 * i + 1;
		constexpr int32_t dy = 17;
		const uint8_t seg = segs[static_cast<size_t>(i)];

		constexpr int32_t horz[] = {6, 0, 3};
		for (int32_t y = 0; y < 3; y++) {
			if ((seg & (1u << horz[y])) != 0u) {
				const int32_t ybase = row(7 + dy * y);
				f.invert(Rect{ix + 3, ybase + 0, 15, 1});
				f.invert(Rect{ix + 2, ybase + 1, 17, 1});
				f.invert(Rect{ix + 3, ybase + 2, 15, 1});
			}
		}

		constexpr int32_t vert[] = {1, 2, 5, 4};
		for (int32_t x = 0; x < 2; x++) {
			const int32_t xside = x * 2 - 1;
			for (int32_t y = 0; y < 2; y++) {
				if ((seg & (1u << vert[2 * x + y])) != 0u) {
					const int32_t xbase = ix + 18 * x + 1;
					const int32_t ybase = row(10 + dy * y);
					const int32_t yside = y * -2 + 1;
					f.invert(Rect{xbase + xside, ybase + yside, 1, 14});
					f.invert(Rect{xbase, ybase, 1, 14});
					f.invert(Rect{xbase - xside, ybase + 1, 1, 12});
				}
			}
		}

		if ((seg & (1u << 7)) != 0u) {
			f.invert(Rect{ix + 21, row(41), 3, 3}); // invertArea(ix + 21, 3, 41, 43)
		}
	}
}

void vu(OledFrame& f, Rect rect, uint8_t l, uint8_t r) {
	if (rect.w < 8 || rect.h < 2) {
		return;
	}
	const auto [top, bottom] = rect.split_rows(rect.h / 2);
	vu_channel(f, top, l);
	vu_channel(f, bottom, r);
}

void envelope(OledFrame& f, Rect rect, float a, float d, float s, float r) {
	if (rect.w < 4 || rect.h < 2) {
		return;
	}
	a = std::clamp(a, 0.0f, 1.0f);
	d = std::clamp(d, 0.0f, 1.0f);
	s = std::clamp(s, 0.0f, 1.0f);
	r = std::clamp(r, 0.0f, 1.0f);

	// Attack, decay and release each claim up to a quarter of the width; sustain holds the rest.
	const float quarter = static_cast<float>(rect.w) / 4.0f;
	const int32_t aw = static_cast<int32_t>(a * quarter);
	const int32_t dw = static_cast<int32_t>(d * quarter);
	const int32_t rw = static_cast<int32_t>(r * quarter);
	const int32_t sw = std::max<int32_t>(0, rect.w - 1 - aw - dw - rw);

	const int32_t top = rect.y;
	const int32_t bottom = rect.bottom() - 1;
	const int32_t sustain_y = bottom - static_cast<int32_t>(s * static_cast<float>(rect.h - 1));

	int32_t x = rect.x;
	f.line(x, bottom, x + aw, top);
	x += aw;
	f.line(x, top, x + dw, sustain_y);
	x += dw;
	f.line(x, sustain_y, x + sw, sustain_y);
	x += sw;
	f.line(x, sustain_y, x + rw, bottom);
}

void graphic(OledFrame& f, Rect rect, const uint8_t* data, int32_t w, int32_t h, int32_t bytes_tall, bool reversed) {
	f.graphic(data, rect.x, rect.y, w, h, bytes_tall, reversed);
}

void icon(OledFrame& f, Point at, const gfx::oled::Icon& i, bool reversed) {
	f.icon(i, at.x, at.y, reversed);
}

} // namespace deluge::ui::oled
