#include "ui/oled/meters.h"

#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "ui/core/metrics.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/text.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <span>
#include <string_view>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

/// `Canvas::drawRectangle(min_x, min_y, max_x, max_y)`, whose bounds are *inclusive*, as the
/// half-open `Rect` that `OledFrame::rect` takes. Lets the ported bodies keep legacy's own
/// four-corner spelling.
constexpr Rect inclusive(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
	return Rect{min_x, min_y, max_x - min_x + 1, max_y - min_y + 1};
}

/// `Canvas::invertArea(x_min, width, start_y, end_y)` -- width exclusive, `end_y` inclusive --
/// as the `Rect` that `OledFrame::invert` takes.
constexpr Rect inverted(int32_t x_min, int32_t width, int32_t start_y, int32_t end_y) {
	return Rect{x_min, start_y, width, end_y - start_y + 1};
}

/// Ports legacy `deluge::string::appendInt` via the shared `int_to_string` helper in
/// ui/oled/detail/int_to_string.h.
std::string_view int_to_string(int32_t number, std::span<char> buf) {
	return detail::int_to_string(number, 1, buf);
}

} // namespace

void percent(OledFrame& f, Rect slot, int32_t value) {
	// Ports legacy `Number::drawPercent`. The face is the style's rather than a hard-coded pair;
	// both shipped profiles set Apple7, which has the same metrics legacy hard-coded.
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t text_spacing_x = spacing_x_for(font);
	const int32_t text_spacing_y = text_height_for(font);

	std::array<char, 12> buf{}; // matches legacy's buffer size
	const std::string_view value_string = int_to_string(value, std::span{buf});

	constexpr char percent_char = '%';
	const int32_t value_width = gfx::oled::string_width_px(value_string, text_spacing_y);
	const int32_t percent_char_width = gfx::oled::char_width_px(percent_char, text_spacing_y);
	constexpr int32_t padding_between = 2;
	const int32_t total_width = value_width + percent_char_width + padding_between;

	int32_t x = slot.x + (slot.w - total_width) / 2 + 1;
	int32_t y = slot.y + kHorizontalSlotYOffset;
	f.glyphs(value_string, x, y, text_spacing_x, text_spacing_y);

	x += value_width + padding_between;
	f.glyph(percent_char, x, y, text_spacing_x, text_spacing_y);
}

void knob_arc(OledFrame& f, Rect slot, float norm) {
	// Ports legacy `Number::drawKnob`.
	// Background arc: a bitmap, easier to keep pixel-perfect than a drawn arc.
	f.icon_centered(gfx::oled::icons::knobArc, slot.x, slot.w, slot.y - 1);

	// Calculate current value angle
	constexpr int32_t knob_radius = 10;
	// int32_t, not uint8_t: `slot` is a Rect of int32_t, so squeezing slot-derived geometry back
	// into a uint8_t both loses the -Wconversion argument and wraps for slots at x >= 251.
	// Nothing below depends on the narrower type -- these only ever feed float arithmetic and
	// int32_t drawing calls. (Applies to every `slot.` derivation in this file.)
	const int32_t center_x = slot.x + slot.w / 2;
	const int32_t center_y = slot.y + knob_radius;
	constexpr int32_t arc_range_angle = 210;
	constexpr int32_t beginning_angle = 165;
	const float current_angle = beginning_angle + (arc_range_angle * norm); // legacy's value_percent

	// Draw the leading line
	// M_PI, i.e. double: the angle-to-radians conversion evaluates in double and only then
	// narrows to float, and the rounded endpoints below are sensitive to that -- spelled out
	// here so no feature-test macro decides whether M_PI exists.
	constexpr double pi = 3.14159265358979323846;
	const float radians = current_angle * pi / 180.0f;
	constexpr float outer_radius = static_cast<float>(knob_radius) - 1.0f;
	constexpr float inner_radius = 3.5f;
	const float cos_a = std::cos(radians);
	const float sin_a = std::sin(radians);
	const float line_start_x = static_cast<float>(center_x) + inner_radius * cos_a;
	const float line_start_y = static_cast<float>(center_y) + inner_radius * sin_a;
	const float line_end_x = static_cast<float>(center_x) + outer_radius * cos_a;
	const float line_end_y = static_cast<float>(center_y) + outer_radius * sin_a;
	f.line(static_cast<int32_t>(std::round(line_start_x)), static_cast<int32_t>(std::round(line_start_y)),
	       static_cast<int32_t>(std::round(line_end_x)), static_cast<int32_t>(std::round(line_end_y)), true);
}

void unipolar_bar(OledFrame& f, Rect slot, float norm) {
	// Ports legacy `Number::drawBar`.
	constexpr int32_t bar_width = 21;
	constexpr int32_t bar_height = 5;
	constexpr int32_t outline_padding = 2;
	const int32_t bar_start_x = slot.x + 5;
	const int32_t bar_start_y = slot.y + kHorizontalSlotYOffset + 2;
	const int32_t bar_end_x = bar_start_x + bar_width - 1;
	const int32_t bar_end_y = bar_start_y + bar_height - 1;

	f.rect(inclusive(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                 bar_end_y + outline_padding));

	// The float->integer truncation matches legacy; only the destination width changes.
	const int32_t fill_width = static_cast<int32_t>(norm * static_cast<float>(bar_width));
	f.invert(inverted(bar_start_x, fill_width, bar_start_y, bar_end_y));
}

void pan(OledFrame& f, Rect slot, int32_t value) {
	// Ports legacy `Number::drawPan`.
	constexpr int32_t bar_width = 21;
	constexpr int32_t bar_height = 5;
	constexpr int32_t outline_padding = 2;
	const int32_t bar_start_x = slot.x + 5;
	const int32_t bar_start_y = slot.y + kHorizontalSlotYOffset + 2;
	const int32_t bar_end_x = bar_start_x + bar_width - 1;
	const int32_t bar_end_y = bar_start_y + bar_height - 1;
	const int32_t bar_center_x = bar_start_x + bar_width / 2;

	// Outline
	f.rect(inclusive(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                 bar_end_y + outline_padding));

	// The top and the bottom notches
	for (const auto offset : {1, 2}) {
		f.pixel(bar_center_x, bar_end_y + outline_padding + offset);
		f.pixel(bar_center_x, bar_start_y - outline_padding - offset);
	}

	// Midpoint
	f.vline(bar_center_x, bar_start_y, bar_end_y);

	const int32_t direction = (value > 0) - (value < 0);

	// Adjust the value a bit to make a perfect zero indication.
	// static_cast<uint8_t>: `value` here is an unconstrained int32_t parameter -- pan positions
	// only ever run +/-50, but legacy's narrower truncation is preserved rather than quietly
	// widened, so a caller that passes something larger gets legacy's pixels, not new ones.
	int32_t abs_value = static_cast<uint8_t>(std::abs(value));
	if (abs_value > 0 && abs_value < 3) {
		abs_value = 3;
	}

	// Fill
	const int32_t fill_width = static_cast<int32_t>(static_cast<float>(abs_value) / 25.f * (bar_width / 2));
	if (fill_width > 0) {
		const int32_t fill_start_x = direction >= 0 ? bar_center_x + 1 : bar_center_x - fill_width - 1;
		f.invert(inverted(fill_start_x, fill_width + 1, bar_start_y, bar_end_y));
	}
}

void slider(OledFrame& f, Rect slot, float norm) {
	// Ports legacy `Number::drawSlider`.
	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot.x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot.y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const int32_t value_line_min_x = min_x;
	const int32_t value_line_width = static_cast<int32_t>(norm * static_cast<float>(max_x - 1 - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t x = min_x; x <= max_x; x += 3) {
		if (x != value_line_x - 1 && x != value_line_x + 2) {
			f.pixel(x, center_y - 1);
			f.pixel(x, center_y + 1);
		}
	}

	f.vline(value_line_x, min_y, max_y);
	f.vline(value_line_x + 1, min_y, max_y);
}

void length_slider(OledFrame& f, Rect slot, float norm, bool min_pos) {
	// Ports legacy `Number::drawLengthSlider`.
	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot.x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot.y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const int32_t value_line_min_x = min_x + min_pos;
	const int32_t value_line_width = static_cast<int32_t>(norm * static_cast<float>(max_x - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t y = center_y - 1; y <= center_y + 1; y++) {
		f.hline(y, min_x, value_line_x);
	}
	f.vline(value_line_x, min_y, max_y);

	for (int32_t x = max_x; x >= value_line_min_x; x -= 3) {
		f.pixel(x, center_y - 1);
		f.pixel(x, center_y + 1);
	}
}

namespace {

/// Ports legacy `Number::drawHpf`.
void draw_hpf(OledFrame& f, Rect slot, float norm) {
	constexpr int32_t slope_width = 5;
	constexpr int32_t width = 21;
	constexpr int32_t height = 11;

	const int32_t hpf_start_x = slot.x + 5;
	const int32_t hpf_end_x = hpf_start_x + width - 1;
	const int32_t hpf_start_y = slot.y + 1;
	const int32_t hpf_end_y = hpf_start_y + height - 1;

	// std::lerp with any integral argument computes in double either way, so widening the
	// endpoints does not move the result; only the truncation's destination changes.
	const int32_t slope_start_x = static_cast<int32_t>(std::lerp(hpf_start_x, hpf_end_x - slope_width - 4, norm));
	const int32_t slope_end_x = slope_start_x + slope_width;

	f.line(slope_start_x, hpf_end_y, slope_end_x, hpf_start_y, true);
	f.hline(hpf_start_y, slope_end_x, hpf_end_x);
	f.hline(hpf_start_y + 1, slope_end_x, hpf_end_x);

	for (int32_t x = hpf_start_x; x < slope_start_x; x += 3) {
		f.pixel(x, hpf_start_y);
	}
	if (slope_start_x != hpf_start_x) {
		for (int32_t y = hpf_start_y; y < hpf_end_y; y += 3) {
			f.pixel(hpf_start_x, y);
		}
	}
}

/// Ports legacy `Number::drawLpf`. Not `draw_hpf` mirrored: the corner's travel and the two
/// conditional dotted runs differ, which is why these are two routines and not one.
void draw_lpf(OledFrame& f, Rect slot, float norm) {
	constexpr int32_t slope_width = 5;
	constexpr int32_t width = 21;
	constexpr int32_t height = 11;

	const int32_t lpf_start_x = slot.x + 5;
	const int32_t lpf_end_x = lpf_start_x + width - 1;
	const int32_t lpf_start_y = slot.y + 1;
	const int32_t lpf_end_y = lpf_start_y + height - 1;

	const int32_t slope_start_x = static_cast<int32_t>(std::lerp(lpf_start_x + 3, lpf_end_x - slope_width, norm));
	const int32_t slope_end_x = slope_start_x + slope_width;

	f.line(slope_start_x, lpf_start_y, slope_end_x, lpf_end_y, true);
	f.hline(lpf_start_y, lpf_start_x, slope_start_x);
	f.hline(lpf_start_y + 1, lpf_start_x, slope_start_x);

	for (int32_t x = lpf_end_x; x > slope_end_x; x -= 3) {
		f.pixel(x, lpf_start_y);
	}
	if (slope_end_x != lpf_end_x) {
		for (int32_t y = lpf_start_y; y < lpf_end_y; y += 3) {
			f.pixel(lpf_end_x, y);
		}
	}
}

/// Ports legacy `Number::drawRelease`.
void draw_release(OledFrame& f, Rect slot, float norm) {
	constexpr int32_t width = 19;
	constexpr int32_t height = 11;

	const int32_t rel_start_x = slot.x + 5;
	const int32_t rel_end_x = rel_start_x + width - 1;
	const int32_t rel_start_y = slot.y + kHorizontalSlotYOffset - 1;
	const int32_t rel_end_y = rel_start_y + height - 1;

	const int32_t rel_stage_start_x = rel_start_x + 4;
	const int32_t rel_effective_x = static_cast<int32_t>(std::lerp(rel_stage_start_x, rel_end_x, norm));

	f.hline(rel_start_y, rel_start_x, rel_stage_start_x);
	f.line(rel_stage_start_x, rel_start_y, rel_effective_x, rel_end_y);

	constexpr int32_t indicator_offset = 2;
	for (int32_t x = rel_effective_x - indicator_offset; x <= rel_effective_x + indicator_offset; x++) {
		for (int32_t y = rel_end_y - indicator_offset; y <= rel_end_y + indicator_offset - 1; y++) {
			f.pixel(x, y);
		}
	}

	for (int32_t x = rel_end_x; x > rel_effective_x + 1; x -= 2) {
		f.pixel(x, rel_end_y);
	}
}

/// Ports legacy `Number::drawAttack`. Not `draw_release` reversed: the origin, the y offset, the
/// travel and the indicator's rows all differ.
void draw_attack(OledFrame& f, Rect slot, float norm) {
	constexpr int32_t width = 19;
	constexpr int32_t height = 11;

	const int32_t atk_start_x = slot.x + 7;
	const int32_t atk_end_x = atk_start_x + width - 1;
	const int32_t atk_start_y = slot.y + kHorizontalSlotYOffset;
	const int32_t atk_end_y = atk_start_y + height - 1;

	const int32_t atk_effective_x = static_cast<int32_t>(std::lerp(atk_start_x, atk_end_x - 2, norm));

	f.line(atk_start_x, atk_end_y, atk_effective_x, atk_start_y, false);

	constexpr int32_t indicator_offset = 2;
	for (int32_t x = atk_effective_x - indicator_offset; x <= atk_effective_x + indicator_offset; x++) {
		for (int32_t y = atk_start_y - indicator_offset + 1; y <= atk_start_y + indicator_offset; y++) {
			f.pixel(x, y);
		}
	}

	for (int32_t x = atk_end_x; x > atk_effective_x + 1; x -= 2) {
		f.pixel(x, atk_start_y);
	}
}

} // namespace

void filter_curve(OledFrame& f, Rect slot, float norm, FilterKind kind) {
	return kind == FilterKind::Hpf ? draw_hpf(f, slot, norm) : draw_lpf(f, slot, norm);
}

void envelope_stage(OledFrame& f, Rect slot, float norm, StageKind kind) {
	return kind == StageKind::Release ? draw_release(f, slot, norm) : draw_attack(f, slot, norm);
}

void sidechain_ducking(OledFrame& f, Rect slot, int32_t value, int32_t max) {
	// Ports legacy `Number::drawSidechainDucking`.
	constexpr int32_t width = 23;
	constexpr int32_t height = 11;

	const int32_t left_padding = (slot.w - width) / 2;
	const int32_t min_x = slot.x + left_padding;
	const int32_t max_x = min_x + width;
	const int32_t min_y = slot.y + kHorizontalSlotYOffset - 1;
	const int32_t max_y = min_y + height - 1;

	// Calculate shape height based on value
	const float norm = max > 50 ? std::abs(value) / 5000.0f : value / 50.0f;
	const int32_t fill_height = static_cast<int32_t>(norm * static_cast<float>(height));
	const int32_t y_offset = (height - fill_height) / 2;

	int32_t ducking_start_y, ducking_end_y, y0, y1;
	if (value >= 0) {
		// For positive values, draw from top down
		ducking_start_y = min_y + y_offset;
		ducking_end_y = ducking_start_y + fill_height;
		y0 = ducking_end_y;
		y1 = ducking_start_y;
	}
	else {
		// For negative values, draw from bottom up
		ducking_end_y = max_y - y_offset;
		ducking_start_y = ducking_end_y - fill_height;
		y0 = ducking_start_y;
		y1 = ducking_end_y;
	}

	// Draw a sidechain level shape
	constexpr int32_t offset_right = 10;
	f.line(min_x, y0, max_x - offset_right, y1);
	f.line(min_x, y0, min_x, y1);
	f.hline(y1, max_x - offset_right, max_x);
}

void meter(OledFrame& f, Rect slot, Hint hint, float norm, int32_t raw, int32_t raw_max) {
	// Ports legacy `Number::renderInHorizontalMenu`.
	switch (hint) {
	case Hint::Percent:
		return percent(f, slot, raw * 2); // doubled for the percent style, matching legacy
	case Hint::Knob:
		return knob_arc(f, slot, norm);
	case Hint::Bar:
		return unipolar_bar(f, slot, norm);
	case Hint::Slider:
		return slider(f, slot, norm);
	case Hint::LengthSlider:
		// min_pos defaults to true, matching legacy's only call site (see the note on
		// length_slider in meters.h).
		return length_slider(f, slot, norm);
	case Hint::Pan:
		return pan(f, slot, raw);
	case Hint::Hpf:
		return filter_curve(f, slot, norm, FilterKind::Hpf);
	case Hint::Lpf:
		return filter_curve(f, slot, norm, FilterKind::Lpf);
	case Hint::Attack:
		return envelope_stage(f, slot, norm, StageKind::Attack);
	case Hint::Release:
		return envelope_stage(f, slot, norm, StageKind::Release);
	case Hint::Ducking:
		return sidechain_ducking(f, slot, raw, raw_max);
	case Hint::None:
	case Hint::Enum:
	case Hint::Toggle:
	default: {
		// Ports legacy's default `Number` rendering: the raw value centred (with truncation) in
		// the title face. Both shipped profiles' title_font is MetricBold9, matching legacy's
		// hard-coded spacing/size.
		std::array<char, 12> buf{};
		const std::string_view value_string = int_to_string(raw, std::span{buf});
		const gfx::oled::FontId font = f.style.title_font;
		const Rect band{slot.x, slot.y + kHorizontalSlotYOffset, slot.w, text_height_for(font)};
		text(f, band, value_string, TextStyle{.font = font, .truncate = true});
		return;
	}
	}
}

} // namespace deluge::ui::oled
