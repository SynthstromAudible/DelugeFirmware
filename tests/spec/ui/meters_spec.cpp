#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/oled/meters.h"

#include "cppspec.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Rect;
namespace icons = deluge::gfx::oled::icons;
namespace uitest = deluge::ui::test;

// Every meter below is pinned three ways at once, the same way values_spec.cpp pins the value
// widgets. First as a byte-for-byte comparison against the legacy routine itself: `namespace
// legacy` re-transcribes `Number::drawPercent` / `drawKnob` / `drawBar` / `drawSlider` /
// `drawLengthSlider` / `drawPan` / `drawHpf` / `drawLpf` / `drawAttack` / `drawRelease` /
// `drawSidechainDucking` (gui/menu_item/number.cpp:107-427) against the raw gfx `Canvas` in
// canvas-absolute coordinates, with every magic number spelled as a literal (2 for
// kHorizontalMenuSlotYOffset, 6/9 for the Apple ][ font, 9/10 for the title font) rather than
// borrowed from the constants the port lays out with, and every scene is drawn both ways and
// compared. Second as individual coordinates read off the legacy source, for the three checks
// the task brief names. Third as golden PBMs. Both shipped geometries are covered for every
// meter, since the legacy `SlotPosition::start_y` was panel-relative and the frame's origin now
// carries the bezel term.
//
// Deviations from the task brief, all resolved in the legacy code's favour (ruling 1):
//
//  D1. The brief says `unipolar_bar(0.5)` "inverts exactly columns bar_start_x .. bar_start_x +
//      10", i.e. eleven columns. `drawBar` (number.cpp:170-172) computes `fill_width = norm *
//      21` through a `uint8_t` -- 10, not 10.5 and not 11 -- and `invertArea`'s `width` is
//      exclusive (canvas.h:265-271), so exactly ten columns invert: bar_start_x .. bar_start_x +
//      9. Asserted below, including that bar_start_x + 10 stays dark.
//  D2. The brief's `meter(f, slot, Hint, float norm, int32_t raw)` has no room for
//      `getMaxValue()`, which `drawSidechainDucking` reads (number.cpp:402) to pick between its
//      two value scales. A defaulted sixth parameter `raw_max` carries it; the brief's five-
//      argument call shape still compiles and selects the `/ 50.0f` arm.
//  D3. The brief lists `percent(f, slot, int32_t value)` and, separately, "getValue()*2 for
//      percent -> the value parameter". Taken literally: the widget's parameter is the number
//      that gets *printed*, and `meter` does legacy's doubling (number.cpp:111). Pinned by the
//      dispatch test at the bottom.
//  D4. Legacy's `drawSlider` takes an optional value override and `drawLengthSlider` a
//      `min_slider_pos` flag; the brief's `slider(f, slot, float norm)` drops the former (it is
//      just "whichever number the caller wants normalised", number.cpp:185) and keeps the latter
//      as `min_pos`. Both arms of `min_pos` are covered.

namespace {

// The slot geometry the task brief fixes: three legacy SlotPositions side by side.
constexpr int32_t kSlotY = 14;
constexpr int32_t kSlotW = 31;
constexpr int32_t kSlotH = 25;
constexpr int32_t kSlotX[3] = {0, 32, 64};
constexpr float kNorms[3] = {0.0f, 0.5f, 1.0f};

Rect slot_at(int32_t i) {
	return Rect{kSlotX[i], kSlotY, kSlotW, kSlotH};
}

/// Verbatim transcriptions of the eleven legacy renderers, drawing through the raw gfx Canvas.
/// `slot_y` is canvas-absolute -- the legacy `SlotPosition::start_y` plus the panel's topmost
/// pixel, which is what the horizontal menu handed them -- so these can be replayed onto a
/// reference canvas of either geometry. `image.` and the parameter names are legacy's own.
namespace legacy {

void drawPercent(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, int32_t value) {
	const std::string valueString = std::to_string(value); // etl::string<12> + appendInt

	constexpr char percent_char = '%';
	const int32_t value_width = image.get_string_width_in_pixels(valueString.c_str(), 9);
	const int32_t percent_char_width = image.get_char_width_in_pixels(percent_char, 9);
	constexpr int32_t padding_between = 2;
	const int32_t total_width = value_width + percent_char_width + padding_between;

	int32_t x = slot_x + (slot_w - total_width) / 2 + 1;
	int32_t y = slot_y + 2;
	image.draw_string(valueString, x, y, 6, 9);

	x += value_width + padding_between;
	image.draw_char(percent_char, x, y, 6, 9);
}

void drawKnob(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, float norm) {
	image.draw_icon_centered(icons::knobArc, slot_x, slot_w, slot_y - 1);

	constexpr uint8_t knob_radius = 10;
	const uint8_t center_x = slot_x + slot_w / 2;
	const uint8_t center_y = slot_y + knob_radius;
	constexpr uint8_t arc_range_angle = 210;
	constexpr uint8_t beginning_angle = 165;
	const float current_angle = beginning_angle + (arc_range_angle * norm);

	const float radians = current_angle * 3.14159265358979323846 / 180.0f;
	constexpr float outer_radius = knob_radius - 1.0f;
	constexpr float inner_radius = 3.5f;
	const float cos_a = std::cos(radians);
	const float sin_a = std::sin(radians);
	const float line_start_x = center_x + inner_radius * cos_a;
	const float line_start_y = center_y + inner_radius * sin_a;
	const float line_end_x = center_x + outer_radius * cos_a;
	const float line_end_y = center_y + outer_radius * sin_a;
	image.draw_line(std::round(line_start_x), std::round(line_start_y), std::round(line_end_x), std::round(line_end_y),
	                {.thick = true});
}

void drawBar(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = slot_x + 5;
	const uint8_t bar_start_y = slot_y + 2 + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;

	image.draw_rectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                     bar_end_y + outline_padding);

	const uint8_t fill_width = norm * bar_width;
	image.invert_area(bar_start_x, fill_width, bar_start_y, bar_end_y);
}

void drawSlider(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const int32_t value_line_min_x = min_x;
	const int32_t value_line_width = static_cast<int32_t>(norm * (max_x - 1 - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t x = min_x; x <= max_x; x += 3) {
		if (x != value_line_x - 1 && x != value_line_x + 2) {
			image.draw_pixel(x, center_y - 1);
			image.draw_pixel(x, center_y + 1);
		}
	}

	image.draw_vertical_line(value_line_x, min_y, max_y);
	image.draw_vertical_line(value_line_x + 1, min_y, max_y);
}

void drawLengthSlider(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm,
                      bool min_slider_pos) {
	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const int32_t value_line_min_x = min_x + min_slider_pos;
	const int32_t value_line_width = static_cast<int32_t>(norm * (max_x - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t y = center_y - 1; y <= center_y + 1; y++) {
		image.draw_horizontal_line(y, min_x, value_line_x);
	}
	image.draw_vertical_line(value_line_x, min_y, max_y);

	for (int32_t x = max_x; x >= value_line_min_x; x -= 3) {
		image.draw_pixel(x, center_y - 1);
		image.draw_pixel(x, center_y + 1);
	}
}

void drawPan(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, int32_t value) {
	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = slot_x + 5;
	const uint8_t bar_start_y = slot_y + 2 + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;
	const uint8_t bar_center_x = bar_start_x + bar_width / 2;

	image.draw_rectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                     bar_end_y + outline_padding);

	for (const auto offset : {1, 2}) {
		image.draw_pixel(bar_center_x, bar_end_y + outline_padding + offset);
		image.draw_pixel(bar_center_x, bar_start_y - outline_padding - offset);
	}

	image.draw_vertical_line(bar_center_x, bar_start_y, bar_end_y);

	const int8_t direction = (value > 0) - (value < 0);

	uint8_t abs_value = std::abs(value);
	if (abs_value > 0 && abs_value < 3) {
		abs_value = 3;
	}

	const uint8_t fill_width = abs_value / 25.f * (bar_width / 2);
	if (fill_width > 0) {
		const uint8_t fill_start_x = direction >= 0 ? bar_center_x + 1 : bar_center_x - fill_width - 1;
		image.invert_area(fill_start_x, fill_width + 1, bar_start_y, bar_end_y);
	}
}

void drawHpf(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t hpf_start_x = slot_x + 5;
	const uint8_t hpf_end_x = hpf_start_x + width - 1;
	const uint8_t hpf_start_y = slot_y + 1;
	const uint8_t hpf_end_y = hpf_start_y + height - 1;

	const uint8_t slope_start_x = std::lerp(hpf_start_x, hpf_end_x - slope_width - 4, norm);
	const uint8_t slope_end_x = slope_start_x + slope_width;

	image.draw_line(slope_start_x, hpf_end_y, slope_end_x, hpf_start_y, {.thick = true});
	image.draw_horizontal_line(hpf_start_y, slope_end_x, hpf_end_x);
	image.draw_horizontal_line(hpf_start_y + 1, slope_end_x, hpf_end_x);

	for (uint8_t x = hpf_start_x; x < slope_start_x; x += 3) {
		image.draw_pixel(x, hpf_start_y);
	}
	if (slope_start_x != hpf_start_x) {
		for (uint8_t y = hpf_start_y; y < hpf_end_y; y += 3) {
			image.draw_pixel(hpf_start_x, y);
		}
	}
}

void drawLpf(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t lpf_start_x = slot_x + 5;
	const uint8_t lpf_end_x = lpf_start_x + width - 1;
	const uint8_t lpf_start_y = slot_y + 1;
	const uint8_t lpf_end_y = lpf_start_y + height - 1;

	const uint8_t slope_start_x = std::lerp(lpf_start_x + 3, lpf_end_x - slope_width, norm);
	const uint8_t slope_end_x = slope_start_x + slope_width;

	image.draw_line(slope_start_x, lpf_start_y, slope_end_x, lpf_end_y, {.thick = true});
	image.draw_horizontal_line(lpf_start_y, lpf_start_x, slope_start_x);
	image.draw_horizontal_line(lpf_start_y + 1, lpf_start_x, slope_start_x);

	for (uint8_t x = lpf_end_x; x > slope_end_x; x -= 3) {
		image.draw_pixel(x, lpf_start_y);
	}
	if (slope_end_x != lpf_end_x) {
		for (uint8_t y = lpf_start_y; y < lpf_end_y; y += 3) {
			image.draw_pixel(lpf_end_x, y);
		}
	}
}

void drawRelease(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr uint8_t width = 19;
	constexpr uint8_t height = 11;

	const uint8_t rel_start_x = slot_x + 5;
	const uint8_t rel_end_x = rel_start_x + width - 1;
	const uint8_t rel_start_y = slot_y + 2 - 1;
	const uint8_t rel_end_y = rel_start_y + height - 1;

	const uint8_t rel_stage_start_x = rel_start_x + 4;
	const uint8_t rel_effective_x = std::lerp(rel_stage_start_x, rel_end_x, norm);

	image.draw_horizontal_line(rel_start_y, rel_start_x, rel_stage_start_x);
	image.draw_line(rel_stage_start_x, rel_start_y, rel_effective_x, rel_end_y);

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = rel_effective_x - indicator_offset; x <= rel_effective_x + indicator_offset; x++) {
		for (uint8_t y = rel_end_y - indicator_offset; y <= rel_end_y + indicator_offset - 1; y++) {
			image.draw_pixel(x, y);
		}
	}

	for (uint8_t x = rel_end_x; x > rel_effective_x + 1; x -= 2) {
		image.draw_pixel(x, rel_end_y);
	}
}

void drawAttack(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t /*slot_w*/, float norm) {
	constexpr uint8_t width = 19;
	constexpr uint8_t height = 11;

	const uint8_t atk_start_x = slot_x + 7;
	const uint8_t atk_end_x = atk_start_x + width - 1;
	const uint8_t atk_start_y = slot_y + 2;
	const uint8_t atk_end_y = atk_start_y + height - 1;

	const uint8_t atk_effective_x = std::lerp(atk_start_x, atk_end_x - 2, norm);

	image.draw_line(atk_start_x, atk_end_y, atk_effective_x, atk_start_y, {.thick = false});

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = atk_effective_x - indicator_offset; x <= atk_effective_x + indicator_offset; x++) {
		for (uint8_t y = atk_start_y - indicator_offset + 1; y <= atk_start_y + indicator_offset; y++) {
			image.draw_pixel(x, y);
		}
	}

	for (uint8_t x = atk_end_x; x > atk_effective_x + 1; x -= 2) {
		image.draw_pixel(x, atk_start_y);
	}
}

void drawSidechainDucking(Canvas& image, int32_t slot_x, int32_t slot_y, int32_t slot_w, int32_t value, int32_t max) {
	constexpr int32_t width = 23;
	constexpr int32_t height = 11;

	const int32_t left_padding = (slot_w - width) / 2;
	const int32_t min_x = slot_x + left_padding;
	const int32_t max_x = min_x + width;
	const int32_t min_y = slot_y + 2 - 1;
	const int32_t max_y = min_y + height - 1;

	const float norm = max > 50 ? std::abs(value) / 5000.0f : value / 50.0f;
	const int32_t fill_height = norm * height;
	const int32_t y_offset = (height - fill_height) / 2;

	int32_t ducking_start_y, ducking_end_y, y0, y1;
	if (value >= 0) {
		ducking_start_y = min_y + y_offset;
		ducking_end_y = ducking_start_y + fill_height;
		y0 = ducking_end_y;
		y1 = ducking_start_y;
	}
	else {
		ducking_end_y = max_y - y_offset;
		ducking_start_y = ducking_end_y - fill_height;
		y0 = ducking_start_y;
		y1 = ducking_end_y;
	}

	constexpr int32_t offset_right = 10;
	image.draw_line(min_x, y0, max_x - offset_right, y1);
	image.draw_line(min_x, y0, min_x, y1);
	image.draw_horizontal_line(y1, max_x - offset_right, max_x);
}

} // namespace legacy

/// Draw the three-slot scene into \p r: the widget through the frame, the legacy routine onto
/// the rig's reference canvas at the same slots shifted down by the panel's hidden rows (which
/// is exactly what the frame's origin adds).
template <class Rig, class Widget, class Legacy>
void three_slots(Rig& r, Widget&& widget, Legacy&& legacy_draw) {
	for (int32_t i = 0; i < 3; i++) {
		widget(r.frame, slot_at(i), i);
		legacy_draw(r.reference, kSlotX[i], kSlotY + r.frame.origin.y, kSlotW, i);
	}
}

/// The whole check for one meter: the three-slot scene at both shipped geometries, each compared
/// against the legacy replay and recorded as a golden.
template <class Self, class Widget, class Legacy>
void both_geometries(Self& self, const std::string& name, Widget&& widget, Legacy&& legacy_draw) {
	uitest::Rig48 r;
	three_slots(r, widget, legacy_draw);
	expect(r.pbm()).to_equal(r.reference_pbm());
	expect_golden(self, "../ui/widgets/" + name + "_48.pbm", r.pbm());

	uitest::Rig64 r64;
	three_slots(r64, widget, legacy_draw);
	expect(r64.pbm()).to_equal(r64.reference_pbm());
	expect_golden(self, "../ui/widgets/" + name + "_64.pbm", r64.pbm());
}

} // namespace

// clang-format off
describe meters_w("ui::oled::meters", $ {

	// -------------------------------------------------------------------- percent

	it("draws the legacy percent readout at both geometries", _ {
		// number.cpp:107-125. The three slots carry the numbers that get printed, i.e. legacy's
		// already-doubled getValue() * 2 (D3).
		constexpr int32_t values[3] = {0, 50, 100};
		both_geometries(self, "meter_percent",
			[&](OledFrame& f, Rect s, int32_t i) { percent(f, s, values[i]); },
			[&](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawPercent(c, x, y, w, values[i]); });

		// The middle slot spelled out in coordinates: "50" is 12px wide at height 9 and '%' 6px,
		// so total_width is 20 and the pair starts at 32 + (31 - 20) / 2 + 1 = 38, with the '%'
		// at 38 + 12 + 2 = 52. y is slot.y + kHorizontalMenuSlotYOffset = 16, canvas row 21.
		uitest::Rig48 r;
		percent(r.frame, slot_at(1), 50);
		r.reference.draw_string("50", 38, 21, 6, 9);
		r.reference.draw_char('%', 52, 21, 6, 9);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	// ------------------------------------------------------------------- knob_arc

	it("draws the legacy knob arc and leading line at both geometries", _ {
		both_geometries(self, "meter_knob",
			[](OledFrame& f, Rect s, int32_t i) { knob_arc(f, s, kNorms[i]); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawKnob(c, x, y, w, kNorms[i]); });

		// The brief's explicit check, with the trig spelled out here rather than borrowed from
		// the port: at norm 1 the line ends at 165 + 210 = 375 degrees, radius knob_radius - 1
		// (number.cpp:141-153), about a centre at (slot.x + slot.w / 2, slot.y + 10).
		uitest::Rig48 r;
		knob_arc(r.frame, slot_at(2), 1.0f);
		const double radians = (165.0 + 210.0) * 3.14159265358979323846 / 180.0;
		const int32_t center_x = kSlotX[2] + kSlotW / 2;
		const int32_t center_y = kSlotY + 10 + 5;                    // + the 48-row panel's hidden rows
		const int32_t end_x = std::round(center_x + 9.0f * std::cos(static_cast<float>(radians)));
		const int32_t end_y = std::round(center_y + 9.0f * std::sin(static_cast<float>(radians)));
		expect(end_x).to_equal(88);                                  // 79 + 9 * cos 15deg = 87.69
		expect(end_y).to_equal(31);                                  // 29 + 9 * sin 15deg = 31.33
		expect(r.lit(end_x, end_y)).to_be_true();
		// The arc bitmap does not reach that pixel -- its last two columns ink canvas rows 24/25
		// and 26..31 respectively -- so only the leading line can have lit it. Its inner end is
		// at radius 3.5, which rounds to (82, 30).
		expect(r.lit(82, 30)).to_be_true();
		expect(r.lit(80, 29)).to_be_false();                         // inside the arc, past the line's start

		// At norm 0 the line points the other way (165 degrees, i.e. up-left of centre).
		uitest::Rig48 zero;
		knob_arc(zero.frame, slot_at(2), 0.0f);
		const double zero_radians = 165.0 * 3.14159265358979323846 / 180.0;
		expect(zero.lit(std::round(center_x + 9.0f * std::cos(static_cast<float>(zero_radians))),
		                std::round(center_y + 9.0f * std::sin(static_cast<float>(zero_radians))))).to_be_true();
	});

	// --------------------------------------------------------------- unipolar_bar

	it("draws the legacy bar at both geometries", _ {
		both_geometries(self, "meter_bar",
			[](OledFrame& f, Rect s, int32_t i) { unipolar_bar(f, s, kNorms[i]); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawBar(c, x, y, w, kNorms[i]); });
	});

	it("inverts exactly ten columns at half (D1)", _ {
		// number.cpp:170-172: fill_width = norm * 21 through a uint8_t is 10, and invertArea's
		// width is exclusive -- so bar_start_x .. bar_start_x + 9, not + 10 as the brief said.
		uitest::Rig48 r;
		unipolar_bar(r.frame, slot_at(1), 0.5f);
		const int32_t bar_start_x = kSlotX[1] + 5;                   // 37
		const int32_t bar_start_y = kSlotY + 2 + 2 + 5;              // canvas row 23
		const int32_t bar_end_y = bar_start_y + 5 - 1;               // canvas row 27

		bool filled = true, empty = true;
		for (int32_t y = bar_start_y; y <= bar_end_y; y++) {
			for (int32_t x = bar_start_x; x <= bar_start_x + 9; x++) {
				filled = filled && r.lit(x, y);
			}
			for (int32_t x = bar_start_x + 10; x <= bar_start_x + 20; x++) {
				empty = empty && !r.lit(x, y);
			}
			empty = empty && !r.lit(bar_start_x - 1, y);             // the gap inside the outline
		}
		expect(filled).to_be_true();
		expect(empty).to_be_true();

		// The outline is 2px outside the bar on every side (number.cpp:161-168): x 35..59 and
		// canvas rows 21..29.
		expect(r.lit(35, 21)).to_be_true();
		expect(r.lit(59, 29)).to_be_true();
		expect(r.lit(34, 21)).to_be_false();
		expect(r.lit(60, 29)).to_be_false();

		// Nothing at all at norm 0 beyond the outline (fill_width 0 -> invertArea of width 0).
		uitest::Rig48 zero;
		unipolar_bar(zero.frame, slot_at(1), 0.0f);
		bool all_dark = true;
		for (int32_t y = bar_start_y; y <= bar_end_y; y++) {
			for (int32_t x = bar_start_x; x <= bar_start_x + 20; x++) {
				all_dark = all_dark && !zero.lit(x, y);
			}
		}
		expect(all_dark).to_be_true();
	});

	it("shifts by exactly visible_top between the two geometries", _ {
		// The slot is frame-local and the frame's origin carries the bezel, so the 64-row panel
		// draws the identical picture five rows higher and nothing else changes.
		uitest::Rig48 r;
		uitest::Rig64 r64;
		for (int32_t i = 0; i < 3; i++) {
			unipolar_bar(r.frame, slot_at(i), kNorms[i]);
			unipolar_bar(r64.frame, slot_at(i), kNorms[i]);
		}
		bool shifted = true;
		for (int32_t y = 0; y < 48; y++) {
			for (int32_t x = 0; x < 128; x++) {
				if (y < 5) {
					shifted = shifted && !r.lit(x, y);               // the hidden rows stay dark
				}
				else {
					shifted = shifted && (r.lit(x, y) == r64.lit(x, y - 5));
				}
			}
		}
		expect(shifted).to_be_true();
		bool tail_dark = true;
		for (int32_t y = 43; y < 64; y++) {
			for (int32_t x = 0; x < 128; x++) {
				tail_dark = tail_dark && !r64.lit(x, y);
			}
		}
		expect(tail_dark).to_be_true();
	});

	// ------------------------------------------------------------------------ pan

	it("draws the legacy pan meter at both geometries", _ {
		constexpr int32_t values[3] = {-25, 0, 25};
		both_geometries(self, "meter_pan",
			[&](OledFrame& f, Rect s, int32_t i) { pan(f, s, values[i]); },
			[&](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawPan(c, x, y, w, values[i]); });
	});

	it("draws only the midline at pan zero", _ {
		uitest::Rig48 r;
		pan(r.frame, slot_at(1), 0);
		const int32_t bar_start_x = kSlotX[1] + 5;                   // 37
		const int32_t bar_center_x = bar_start_x + 21 / 2;           // 47
		const int32_t bar_start_y = kSlotY + 2 + 2 + 5;              // canvas row 23
		const int32_t bar_end_y = bar_start_y + 5 - 1;               // canvas row 27

		// Inside the bar, the centre column and nothing else: value 0 gives direction 0 and
		// abs_value 0, so fill_width is 0 and the invertArea at number.cpp:267 never runs.
		bool only_midline = true;
		for (int32_t y = bar_start_y; y <= bar_end_y; y++) {
			for (int32_t x = bar_start_x; x <= bar_start_x + 20; x++) {
				const bool want = (x == bar_center_x);
				only_midline = only_midline && (r.lit(x, y) == want);
			}
		}
		expect(only_midline).to_be_true();

		// The two notches above and below the outline (number.cpp:246-249) are drawn regardless.
		expect(r.lit(bar_center_x, bar_end_y + 2 + 1)).to_be_true();
		expect(r.lit(bar_center_x, bar_end_y + 2 + 2)).to_be_true();
		expect(r.lit(bar_center_x, bar_start_y - 2 - 1)).to_be_true();
		expect(r.lit(bar_center_x, bar_start_y - 2 - 2)).to_be_true();

		// A magnitude of 1 is rounded up to 3 so that "off centre" always shows
		// (number.cpp:258-261): fill_width = 3 / 25 * 10 = 1, inverting two columns right of
		// the midline.
		uitest::Rig48 nudged;
		pan(nudged.frame, slot_at(1), 1);
		expect(nudged.lit(bar_center_x + 1, bar_start_y)).to_be_true();
		expect(nudged.lit(bar_center_x + 2, bar_start_y)).to_be_true();
		expect(nudged.lit(bar_center_x + 3, bar_start_y)).to_be_false();
		nudged.reference.clear();
		legacy::drawPan(nudged.reference, kSlotX[1], kSlotY + 5, kSlotW, 1);
		expect(nudged.pbm()).to_equal(nudged.reference_pbm());
	});

	// --------------------------------------------------------------------- slider

	it("draws the legacy slider at both geometries", _ {
		both_geometries(self, "meter_slider",
			[](OledFrame& f, Rect s, int32_t i) { slider(f, s, kNorms[i]); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawSlider(c, x, y, w, kNorms[i]); });

		// number.cpp:186-199: the thumb is two columns wide and spans the full 11-row track, and
		// at norm 0 it sits on the track's left edge (slot.x + 4). Track rows are slot.y + 1 ..
		// + 11, i.e. canvas 20..30 for the 48-row panel.
		uitest::Rig48 r;
		slider(r.frame, slot_at(0), 0.0f);
		expect(r.lit(4, 20)).to_be_true();
		expect(r.lit(5, 30)).to_be_true();
		expect(r.lit(6, 30)).to_be_false();
	});

	// -------------------------------------------------------------- length_slider

	it("draws the legacy length slider at both geometries", _ {
		both_geometries(self, "meter_length_slider",
			[](OledFrame& f, Rect s, int32_t i) { length_slider(f, s, kNorms[i], false); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) {
				legacy::drawLengthSlider(c, x, y, w, kNorms[i], false);
			});

		// min_pos shifts the thumb's zero one pixel right (number.cpp:213), which at norm 0
		// moves the whole filled bar's end with it.
		uitest::Rig48 r;
		length_slider(r.frame, slot_at(1), 0.0f, true);
		r.reference.clear();
		legacy::drawLengthSlider(r.reference, kSlotX[1], kSlotY + 5, kSlotW, 0.0f, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect(r.lit(kSlotX[1] + 5, 20)).to_be_true();               // thumb at min_x + 1
		expect(r.lit(kSlotX[1] + 4, 20)).to_be_false();
	});

	// --------------------------------------------------------------- filter_curve

	it("draws the legacy low-pass curve at both geometries", _ {
		both_geometries(self, "meter_lpf",
			[](OledFrame& f, Rect s, int32_t i) { filter_curve(f, s, kNorms[i], FilterKind::Lpf); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawLpf(c, x, y, w, kNorms[i]); });
	});

	it("draws the legacy high-pass curve at both geometries", _ {
		both_geometries(self, "meter_hpf",
			[](OledFrame& f, Rect s, int32_t i) { filter_curve(f, s, kNorms[i], FilterKind::Hpf); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawHpf(c, x, y, w, kNorms[i]); });

		// The two are not mirror images, and the dotted stopband is conditional at both ends.
		// The low-pass corner travels lerp(start + 3, end - 5, norm) (number.cpp:314), so at
		// norm 1 slope_end_x lands on lpf_end_x and both the dotted top-right run and the dotted
		// right edge vanish (number.cpp:321-328); at norm 0 the run is there. The corner's own
		// pixel at (lpf_end_x, lpf_start_y) is canvas (25, 20) for a slot at x 0.
		uitest::Rig48 lpf_open;
		filter_curve(lpf_open.frame, slot_at(0), 0.0f, FilterKind::Lpf);
		expect(lpf_open.lit(25, 20)).to_be_true();
		uitest::Rig48 lpf_shut;
		filter_curve(lpf_shut.frame, slot_at(0), 1.0f, FilterKind::Lpf);
		expect(lpf_shut.lit(25, 20)).to_be_false();

		// The high-pass corner travels lerp(start, end - 5 - 4, norm) (number.cpp:284) -- flush
		// against the left edge at norm 0, where number.cpp:291-298 leaves the near edge bare.
		uitest::Rig48 hpf_open;
		filter_curve(hpf_open.frame, slot_at(0), 0.0f, FilterKind::Hpf);
		expect(hpf_open.lit(5, 20)).to_be_false();
		uitest::Rig48 hpf_shut;
		filter_curve(hpf_shut.frame, slot_at(0), 1.0f, FilterKind::Hpf);
		expect(hpf_shut.lit(5, 20)).to_be_true();
	});

	// ------------------------------------------------------------- envelope_stage

	it("draws the legacy attack ramp at both geometries", _ {
		both_geometries(self, "meter_attack",
			[](OledFrame& f, Rect s, int32_t i) { envelope_stage(f, s, kNorms[i], StageKind::Attack); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawAttack(c, x, y, w, kNorms[i]); });
	});

	it("draws the legacy release ramp at both geometries", _ {
		both_geometries(self, "meter_release",
			[](OledFrame& f, Rect s, int32_t i) { envelope_stage(f, s, kNorms[i], StageKind::Release); },
			[](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) { legacy::drawRelease(c, x, y, w, kNorms[i]); });
	});

	// ---------------------------------------------------------- sidechain_ducking

	it("draws the legacy ducking wedge at both geometries", _ {
		// max 50 selects legacy's `getValue() / 50.0f` arm (number.cpp:402), so these three are
		// norms 0, 0.5 and 1.
		constexpr int32_t values[3] = {0, 25, 50};
		both_geometries(self, "meter_ducking",
			[&](OledFrame& f, Rect s, int32_t i) { sidechain_ducking(f, s, values[i], 50); },
			[&](Canvas& c, int32_t x, int32_t y, int32_t w, int32_t i) {
				legacy::drawSidechainDucking(c, x, y, w, values[i], 50);
			});

		// The negative branch draws bottom-up (number.cpp:414-419) ...
		uitest::Rig48 down;
		sidechain_ducking(down.frame, slot_at(1), -25, 50);
		legacy::drawSidechainDucking(down.reference, kSlotX[1], kSlotY + 5, kSlotW, -25, 50);
		expect(down.pbm()).to_equal(down.reference_pbm());

		// ... and a max above 50 switches to the /5000 scale on the magnitude (number.cpp:402),
		// so -2500 there is the same wedge shape as +2500.
		uitest::Rig48 big;
		sidechain_ducking(big.frame, slot_at(1), -2500, 1000);
		legacy::drawSidechainDucking(big.reference, kSlotX[1], kSlotY + 5, kSlotW, -2500, 1000);
		expect(big.pbm()).to_equal(big.reference_pbm());
	});

	// ---------------------------------------------------------------------- meter

	it("dispatches like Number::renderInHorizontalMenu", _ {
		// number.cpp:75-105. Each hint must produce exactly what the widget it names produces.
		const auto same = [](auto&& via_meter, auto&& via_widget) {
			uitest::Rig48 a;
			uitest::Rig48 b;
			via_meter(a.frame);
			via_widget(b.frame);
			return a.pbm() == b.pbm();
		};
		const Rect s = slot_at(1);

		expect(same([&](OledFrame& f) { meter(f, s, Hint::Knob, 0.5f, 7); },
		            [&](OledFrame& f) { knob_arc(f, s, 0.5f); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Bar, 0.5f, 7); },
		            [&](OledFrame& f) { unipolar_bar(f, s, 0.5f); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Slider, 0.25f, 7); },
		            [&](OledFrame& f) { slider(f, s, 0.25f); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::LengthSlider, 0.25f, 7); },
		            [&](OledFrame& f) { length_slider(f, s, 0.25f, false); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Lpf, 0.75f, 7); },
		            [&](OledFrame& f) { filter_curve(f, s, 0.75f, FilterKind::Lpf); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Hpf, 0.75f, 7); },
		            [&](OledFrame& f) { filter_curve(f, s, 0.75f, FilterKind::Hpf); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Attack, 0.75f, 7); },
		            [&](OledFrame& f) { envelope_stage(f, s, 0.75f, StageKind::Attack); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Release, 0.75f, 7); },
		            [&](OledFrame& f) { envelope_stage(f, s, 0.75f, StageKind::Release); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Pan, 0.9f, -25); },
		            [&](OledFrame& f) { pan(f, s, -25); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Ducking, 0.9f, 25, 50); },
		            [&](OledFrame& f) { sidechain_ducking(f, s, 25, 50); })).to_be_true();

		// PERCENT doubles the raw value on the way through (number.cpp:111) -- D3.
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Percent, 0.5f, 25); },
		            [&](OledFrame& f) { percent(f, s, 50); })).to_be_true();
		expect(same([&](OledFrame& f) { meter(f, s, Hint::Percent, 0.5f, 25); },
		            [&](OledFrame& f) { percent(f, s, 25); })).to_be_false();

		// The default arm is drawStringCentered(getValue(), slot.start_x, slot.start_y + 2,
		// kTextTitleSpacingX, kTextTitleSizeY, slot.width) -- number.cpp:99-104.
		for (const Hint hint : {Hint::None, Hint::Enum, Hint::Toggle}) {
			uitest::Rig48 r;
			meter(r.frame, s, hint, 0.5f, 42);
			r.reference.draw_string_centered("42", kSlotX[1], kSlotY + 2 + 5, 9, 10, kSlotW);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});
});
// clang-format on

// Not CPPSPEC_SPEC(meters_w): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_meters_spec`, which
// does not match the `meters_w` describe-block name the macro would assume. Spelled out by hand,
// as the other ui specs do, so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern`.
int ui_meters_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(meters_w).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
