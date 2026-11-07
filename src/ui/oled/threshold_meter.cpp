/// @file
/// @brief The audio recorder's threshold-take screen. See `threshold_meter.h`.

#include "ui/oled/threshold_meter.h"

#include "ui/oled/text.h"

#include <algorithm>

namespace deluge::ui::oled {

namespace {

/// The meter's left end, x.
constexpr int32_t kMeterStartX = 12;
/// How far short of the panel's right edge the meter ends.
constexpr int32_t kMeterEndInset = 14;
/// The dotted caution line's x; a level past it shows the warning mark.
constexpr int32_t kCautionX = 92;
/// The level the meter starts from.
constexpr float kFloorLevel = 8.0f;
/// Full scale: ln(2^31), a level being the natural log of the mean absolute q31 sample.
constexpr float kFullScaleLevel = 21.48756f;
/// Where the caution zone starts, about 19 dB below full scale.
constexpr float kCautionLevel = kFullScaleLevel - 8.8f / 4.0f;
/// The status line's height above the meters' block, which `top` centres on the panel.
constexpr int32_t kBlockHeight = 40;
/// The record dot's rows, each inset one pixel on its first and last.
constexpr int32_t kDotRows = 5;
/// Each bar's rows.
constexpr int32_t kBarRows = 5;

} // namespace

int32_t threshold_meter_x(float level, int32_t width) {
	if (level < kCautionLevel) {
		return kMeterStartX
		       + static_cast<int32_t>(std::clamp((level - kFloorLevel) / (kCautionLevel - kFloorLevel), 0.0f, 1.0f)
		                              * (kCautionX - kMeterStartX));
	}
	const int32_t end = width - kMeterEndInset;
	return kCautionX
	       + static_cast<int32_t>(std::clamp((level - kCautionLevel) / (kFullScaleLevel - kCautionLevel), 0.0f, 1.0f)
	                              * (end - kCautionX));
}

void threshold_meter(OledFrame& f, const ThresholdMeter& m) {
	const int32_t panel_h = f.origin.y + f.bounds().h;
	const int32_t top = (panel_h - kBlockHeight) / 2 + 2 - f.origin.y;
	const auto x = [&f](int32_t canvas_x) { return canvas_x - f.origin.x; };
	const int32_t big_w = spacing_x_for(gfx::oled::FontId::MetricBold13);
	const int32_t big_h = text_height_for(gfx::oled::FontId::MetricBold13);
	const int32_t small_w = spacing_x_for(gfx::oled::FontId::Px5);
	const int32_t small_h = text_height_for(gfx::oled::FontId::Px5);

	// The status; while recording, a record dot blinks left of it.
	if (m.recording) {
		f.glyphs("RECORDING", x(12), top, big_w, big_h);
		if (m.blink_on) {
			for (int32_t row = 0; row < kDotRows; ++row) {
				const int32_t inset = (row == 0 || row == kDotRows - 1) ? 1 : 0;
				f.hline(top + 4 + row, x(5 + inset), x(9 - inset));
			}
		}
	}
	else {
		text(f, gfx::oled::Rect{0, top, f.bounds().w, big_h}, "WAITING...",
		     {.font = gfx::oled::FontId::MetricBold13, .align = Align::Centre});
	}

	// The channel labels.
	f.glyphs("L", x(6), top + 19, small_w, small_h);
	f.glyphs("R", x(6), top + 29, small_w, small_h);

	// The dotted caution line, and while waiting the threshold's, blinking.
	for (int32_t row = top + 18; row <= top + 34; row += 2) {
		f.pixel(x(kCautionX), row);
		if (!m.recording && m.blink_on) {
			f.pixel(x(m.threshold_x), row);
		}
	}

	// Either channel past the caution line shows the warning mark; the 13 px glyph leaves the foot of the stem's right
	// column empty, which the pixel fills.
	if (std::max(m.level_end_x[0], m.level_end_x[1]) > kCautionX) {
		f.glyph('!', x(119), top + 20, 3, big_h);
		f.pixel(x(121), top + 28);
	}

	// Each channel's bar: solid up to the caution line while recording, otherwise checkered.
	for (int32_t channel = 0; channel < 2; ++channel) {
		for (int32_t column = kMeterStartX + 1; column <= m.level_end_x[channel]; ++column) {
			for (int32_t row = 0; row < kBarRows; ++row) {
				if ((m.recording && column < kCautionX) || (column + row) % 2 == (m.recording ? 0 : 1)) {
					f.pixel(x(column), top + 19 + channel * 10 + row);
				}
			}
		}
	}
}

} // namespace deluge::ui::oled
