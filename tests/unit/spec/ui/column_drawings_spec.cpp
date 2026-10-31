#include "gfx/oled/canvas.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/oled/column_drawings.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// Each drawing is drawn into a value region the size `columns` gives a labelled one-slot column (`render_slot`: the
// box less one pixel wide, the box less the label tall), and compared with legacy's own `renderInHorizontalMenu`
// replayed on the reference canvas with the same `SlotPosition`.

namespace {

/// The slot: the second column of the 48-row page, `start_y` `kHorizontalMenuBaseY` + `visible_top`.
constexpr int32_t kStartX = 32;
constexpr int32_t kStartY = 14 + 5;
constexpr int32_t kWidth = 31;
constexpr int32_t kHeight = 16;
constexpr int32_t kSlotYOffset = 2; // `kHorizontalMenuSlotYOffset`

void draw(uitest::Rig48& r, const DrawColumn& data) {
	r.frame.region(Rect{kStartX, kStartY - 5, kWidth, kHeight}, false,
	               [&](OledFrame& slot) { draw_column(slot, false, const_cast<DrawColumn*>(&data)); });
}

/// `voice::Portamento::renderInHorizontalMenu` (`voice/portamento.h:34-66`), its `uint8_t`s kept.
void legacy_portamento(Canvas& image, float norm) {
	constexpr uint8_t porta_graphics_width = 25;
	const uint8_t center_x = kStartX + kWidth / 2;
	const uint8_t porta_start_x = kStartX + 2;
	const uint8_t porta_end_x = porta_start_x + porta_graphics_width - 1;
	constexpr uint8_t porta_graphics_height = 11;
	const uint8_t porta_start_y = kStartY + kSlotYOffset - 1;
	const uint8_t porta_end_y = porta_start_y + porta_graphics_height - 1;
	constexpr uint8_t porta_line_width = 9;
	const uint8_t porta_x0 = std::lerp(center_x, center_x - porta_line_width, norm);
	const uint8_t porta_x1 = std::lerp(center_x, center_x + porta_line_width, norm);
	if (porta_x0 == porta_x1) {
		for (uint8_t y = porta_start_y; y <= porta_end_y; y += 2) {
			image.draw_pixel(porta_x0, y);
		}
	}
	else {
		image.draw_line(porta_x0, porta_end_y, porta_x1, porta_start_y);
	}
	constexpr uint8_t max_note_width = 6;
	constexpr uint8_t note_offset = 2;
	const uint8_t lower_note_start_x = std::max<uint8_t>(porta_start_x, porta_x0 - note_offset - max_note_width);
	const uint8_t higher_note_end_x = std::min<uint8_t>(porta_end_x, porta_x1 + note_offset + max_note_width);
	image.draw_horizontal_line(porta_end_y, lower_note_start_x, porta_x0 - note_offset);
	image.draw_horizontal_line(porta_end_y - 1, lower_note_start_x, porta_x0 - note_offset);
	image.draw_horizontal_line(porta_start_y, porta_x1 + note_offset, higher_note_end_x);
	image.draw_horizontal_line(porta_start_y + 1, porta_x1 + note_offset, higher_note_end_x);
}

/// `unison::Detune::renderInHorizontalMenu` (`unison/detune.h:57-98`).
void legacy_detune(Canvas& image, float norm) {
	for (int i = 0; i < 3; ++i) {
		constexpr int32_t line_spacing = 5;
		constexpr int32_t max_y_offset = 4;
		const int32_t y = kStartY + kSlotYOffset + i * line_spacing;
		const int32_t x0 = kStartX + 6;
		const int32_t x1 = kStartX + kWidth - 7;
		if (i == 0) {
			const auto offset = static_cast<int32_t>(max_y_offset * norm * 0.30f);
			image.draw_line(x0, y, x1, y + offset);
		}
		else if (i == 1) {
			const auto offset = static_cast<int32_t>(max_y_offset * norm * 0.5f);
			image.draw_line(x0, y - offset, x1, y + offset);
			if (norm > 0.7 && norm < 1) {
				image.clear_pixel(x0, y - offset);
				image.clear_pixel(x1, y + offset);
				image.draw_pixel(x0, y - offset - 1);
				image.draw_pixel(x1, y + offset + 1);
			}
		}
		else {
			int32_t offset = y - static_cast<int32_t>(max_y_offset * norm * 0.8f);
			if (norm > 0 && offset == y) {
				offset -= 1;
			}
			image.draw_line(x0, offset, x1 - 8, y);
			image.draw_horizontal_line(y, x1 - 8, x1);
		}
	}
}

/// `osc::PulseWidth::renderInHorizontalMenu` (`osc/pulse_width.h:54-77`).
void legacy_pulse_width(Canvas& image, int32_t value) {
	const float norm = value / 50.0f;
	constexpr int32_t x_padding = 4;
	const uint8_t width = kWidth - x_padding * 2;
	const int32_t start_x = kStartX + x_padding;
	const int32_t end_x = start_x + width - 1;
	const int32_t start_y = kStartY + kSlotYOffset;
	const int32_t end_y = kStartY + kHeight - 6;
	const int32_t pw_min_x = start_x + 2;
	const int32_t pw_max_x = start_x + width / 2;
	const int32_t pw_width = pw_max_x - pw_min_x;
	const auto pw_x = static_cast<int32_t>(pw_max_x - pw_width * norm);
	image.draw_vertical_line(start_x, start_y, end_y);
	image.draw_horizontal_line(start_y, start_x, pw_x);
	image.draw_vertical_line(pw_x, start_y, end_y);
	image.draw_horizontal_line(end_y, pw_x, end_x);
}

/// `delay::Amount::renderInHorizontalMenu` (`delay/amount.h:35-58`): `Number::drawBar` (`number.cpp:156-173`) over the
/// value clamped to 0..24, and the inverted "!" above 24.
void legacy_delay_amount(Canvas& image, int32_t value) {
	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = kStartX + 5;
	const uint8_t bar_start_y = kStartY + kSlotYOffset + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;
	image.draw_rectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                     bar_end_y + outline_padding);
	const float norm = std::clamp<int32_t>(value, 0, 24) / 24.0f;
	const uint8_t fill_width = norm * bar_width;
	image.invert_area(bar_start_x, fill_width, bar_start_y, bar_end_y);
	if (value > 24) {
		constexpr uint8_t excl_mark_width = 3;
		constexpr uint8_t excl_mark_height = 11;
		const uint8_t center_x = kStartX + kWidth / 2;
		const uint8_t excl_mark_start_y = kStartY + kSlotYOffset - 1;
		const uint8_t excl_mark_end_y = excl_mark_start_y + excl_mark_height - 1;
		const uint8_t excl_mark_start_x = center_x - 1;
		for (uint8_t x = center_x - 2; x <= center_x + 2; x++) {
			for (uint8_t y = excl_mark_start_y - 1; y <= excl_mark_end_y + 1; y++) {
				image.draw_pixel(x, y);
			}
		}
		image.invert_area(excl_mark_start_x, excl_mark_width, excl_mark_start_y, excl_mark_start_y + 6);
		image.invert_area(excl_mark_start_x, excl_mark_width, excl_mark_start_y + 8, excl_mark_start_y + 10);
	}
}

} // namespace

// clang-format off
describe column_drawings_w("ui::oled::column_drawings", $ {

	it("draws the portamento glide as main's, at zero, mid and full", _ {
		for (const int32_t v : {0, 17, 50}) {
			uitest::Rig48 r;
			draw(r, DrawColumn{.shape = DrawColumn::Shape::Glide, .norm = v / 50.0f});
			legacy_portamento(r.reference, v / 50.0f);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("draws the unison detune's three lines as main's, at zero, past 0.7 and full", _ {
		for (const int32_t v : {0, 38, 50}) {
			uitest::Rig48 r;
			draw(r, DrawColumn{.shape = DrawColumn::Shape::Detune, .norm = v / 50.0f});
			legacy_detune(r.reference, v / 50.0f);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("draws the pulse width's wave as main's, at zero, mid and max", _ {
		for (const int32_t v : {0, 23, 50}) {
			uitest::Rig48 r;
			draw(r, DrawColumn{.shape = DrawColumn::Shape::PulseWidth, .raw = v});
			legacy_pulse_width(r.reference, v);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("the delay amount bar clamps at 24 and draws the mark above it", _ {
		for (const int32_t v : {0, 12, 24, 25, 50}) {
			uitest::Rig48 r;
			draw(r, DrawColumn{.shape = DrawColumn::Shape::DelayAmount, .raw = v});
			legacy_delay_amount(r.reference, v);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});
});
// clang-format on

DELUGE_SPEC(ui_column_drawings_spec, column_drawings_w)
