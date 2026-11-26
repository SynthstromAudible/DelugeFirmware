#include "support/frames.h"
#include "ui/gfx/oled/canvas.h"
#include "ui/gfx/oled/icons.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/core/style.h"
#include "ui/widgets/oled/column_drawings.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Canvas;
using deluge::ui::gfx::oled::Rect;
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

/// `voice::Portamento::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`), its
/// `uint8_t`s kept.
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

/// `unison::Detune::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/unison/detune.h`).
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

/// `osc::PulseWidth::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`).
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

/// `delay::Amount::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/delay/amount.h`): `Number::drawBar`
/// (`origin/main:src/deluge/gui/menu_item/number.cpp`) over the value clamped to 0..24, and the inverted "!" above 24.
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

/// `osc::AudioRecorder::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) over a
/// two-slot region.
void legacy_recorder(Canvas& image, int32_t width, int32_t source, bool current, bool full_inversion) {
	const uint8_t start_x = kStartX + 8;
	const uint8_t start_y = kStartY + kSlotYOffset + 6;
	constexpr uint8_t circle_radius = 3;
	image.draw_circle(start_x + circle_radius + 1, start_y + circle_radius + 1, circle_radius, true);
	image.draw_string("rec", start_x + circle_radius * 2 + 5, start_y, 6, 9);
	const char digit[] = {static_cast<char>('1' + source), 0};
	const uint8_t source_x = kStartX + width - 11 - 7;
	const uint8_t source_y = kStartY + kSlotYOffset + 4;
	if (full_inversion || current) {
		image.draw_string(digit, source_x, source_y, 11, 13);
	}
	else {
		image.draw_string(digit, source_x - 1, source_y + 2, 6, 9);
	}
	if (!full_inversion && source == 0) {
		const uint8_t y0 = kStartY + kSlotYOffset + 1;
		const uint8_t y1 = y0 + 18;
		for (uint8_t y = y0; y <= y1; y += 2) {
			image.draw_pixel(kStartX + width - 1, y);
		}
	}
}

/// `sample::LoopPoint::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`).
void legacy_loop_point(Canvas& image, bool start) {
	const int32_t line_x = start ? kStartX + 8 : kStartX + kWidth - 12;
	for (int32_t y = kStartY + kSlotYOffset - 1; y <= kStartY + kHeight - 5; y += 2) {
		image.draw_pixel(line_x, y);
	}
	const int32_t icon_x = start ? line_x + 4 : kStartX - 2;
	image.draw_icon(deluge::ui::gfx::oled::icons::loopPoint, icon_x, kStartY + kSlotYOffset, !start);
}

/// `audio_clip::SampleMarkerEditor::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`).
void legacy_clip_end(Canvas& image) {
	const int32_t line_x = kStartX + kWidth - 6;
	for (int32_t y = kStartY + 1; y <= kStartY + kHeight - 5; y += 2) {
		image.draw_pixel(line_x, y);
	}
	image.draw_icon(deluge::ui::gfx::oled::icons::loopPoint, kStartX + 5, kStartY + kSlotYOffset, true);
}

/// `EnvelopeMenu::renderMenuItems` with its helpers (`origin/main:src/deluge/gui/menu_item/envelope/envelope_menu.h`)
/// and `EnvelopeCurve::rising`, on the reference canvas.
struct LegacyEnvelope {
	Canvas& image;
	EnvelopeGraph g;
	int32_t selected_pos;
	int32_t selected_x = -1;
	int32_t selected_y = -1;
	static constexpr int32_t kTopY = 5 + deluge::ui::gfx::oled::kTextTitleSizeY + 7; // OLED_MAIN_TOPMOST_PIXEL is 5
	static constexpr int32_t kBaseY = 48 - 6;

	static float curve_rising(float k, float x) {
		return k == 0.f ? x : 1.f - ((std::exp2(-k * x) - std::exp2(-k)) / (1.f - std::exp2(-k)));
	}
	static int32_t y_of(float level) { return static_cast<int32_t>(std::round(kBaseY - level * (kBaseY - kTopY))); }
	void segment(float x0, float from, float x1, float to, float k) {
		auto last_x = static_cast<int32_t>(x0);
		int32_t last_y = y_of(from);
		for (int32_t i = 1; i <= 12; i++) {
			const float t = static_cast<float>(i) / 12;
			const auto this_x = static_cast<int32_t>(std::round(x0 + (x1 - x0) * t));
			const int32_t this_y = y_of(from + (to - from) * curve_rising(k, t));
			image.draw_line(last_x, last_y, this_x, this_y);
			last_x = this_x;
			last_y = this_y;
		}
	}
	void dotted(float x, bool draw, int32_t top_y) {
		if (!draw) {
			return;
		}
		for (int32_t y = 43 + 1; y >= top_y - 2; y -= 4) { // OLED_MAIN_VISIBLE_HEIGHT is 43
			image.draw_pixel(static_cast<int32_t>(x), y);
		}
	}
	void indicator(float center_x, float center_y, bool is_selected) {
		const auto ix = static_cast<int32_t>(center_x);
		const auto iy = static_cast<int32_t>(center_y);
		if (!is_selected && ix == selected_x && iy == selected_y) {
			return;
		}
		for (int32_t x = ix - 1; x <= ix + 1; x++) {
			for (int32_t y = iy - 1; y <= iy + 1; y++) {
				image.clear_pixel(x, y);
			}
		}
		if (is_selected) {
			selected_x = ix;
			selected_y = iy;
			image.invert_area(ix - 1, 3, iy - 1, iy + 1);
		}
		image.draw_rectangle(ix - 2, iy - 2, ix + 2, iy + 2);
	}
	void line(float x0, int32_t y0, float x1, int32_t y1) {
		image.draw_line(static_cast<int32_t>(x0), y0, static_cast<int32_t>(x1), y1);
	}

	void draw() {
		constexpr float start_x = 4;
		constexpr float end_x = 4 + 120;
		constexpr float max_segment_width = 30;
		const float attack_x = std::round(start_x + g.attack / 50.0f * max_segment_width);
		// `sigmoidLikeCurve(decay, 50, 8)` (`origin/main:src/deluge/util/functions.cpp`)
		const auto decay = static_cast<float>(g.decay);
		const float decay_width = (decay / (decay + 8.0f)) / (50.0f / (50.0f + 8.0f)) * max_segment_width;
		const float sustain_level = g.mode == EnvelopeGraph::Mode::AHD ? 0.f : g.sustain / 50.0f;
		const int32_t sustain_y = y_of(sustain_level);
		const float attack_level = g.mode == EnvelopeGraph::Mode::AR ? sustain_level : 1.f;
		constexpr float sustain_x = start_x + max_segment_width * 3;
		const float release_x = std::round(sustain_x + g.release / 50.0f * (end_x - sustain_x));
		segment(start_x, 0.f, attack_x, attack_level, g.attack_k);
		switch (g.mode) {
		case EnvelopeGraph::Mode::AHD: {
			const float hold_x = std::round(attack_x + decay_width);
			const float decay_end_x = std::round(hold_x + g.release / 50.0f * (end_x - hold_x));
			line(attack_x, kTopY, hold_x, kTopY);
			segment(hold_x, 1.f, decay_end_x, 0.f, g.decay_k);
			line(decay_end_x, kBaseY, end_x, kBaseY);
			dotted(attack_x, attack_x > start_x + 3, kTopY);
			dotted(hold_x, hold_x - attack_x > 4, kTopY);
			indicator(attack_x, kTopY, selected_pos == 0);
			indicator(hold_x, kTopY, selected_pos == 1);
			indicator(decay_end_x, kBaseY, selected_pos == 3);
			break;
		}
		case EnvelopeGraph::Mode::AR:
			line(attack_x, sustain_y, sustain_x, sustain_y);
			segment(sustain_x, sustain_level, release_x, 0.f, g.release_k);
			line(release_x, kBaseY, end_x, kBaseY);
			dotted(sustain_x, true, sustain_y);
			indicator(attack_x, y_of(attack_level), selected_pos == 0);
			indicator(attack_x + (sustain_x - attack_x) / 2, sustain_y, selected_pos == 2);
			indicator(release_x, kBaseY, selected_pos == 3);
			break;
		case EnvelopeGraph::Mode::MPE:
			line(attack_x, kTopY, sustain_x, kTopY);
			segment(sustain_x, 1.f, release_x, 0.f, g.release_k);
			line(release_x, kBaseY, end_x, kBaseY);
			for (int32_t x = 4; x <= sustain_x; x += 3) {
				image.draw_pixel(x, sustain_y);
			}
			dotted(sustain_x, true, kTopY);
			indicator(attack_x, kTopY, selected_pos == 0);
			indicator(start_x + (sustain_x - start_x) / 2, sustain_y, selected_pos == 2);
			indicator(release_x, kBaseY, selected_pos == 3);
			break;
		default: {
			const float decay_x = std::round(attack_x + decay_width);
			segment(attack_x, 1.f, decay_x, sustain_level, g.decay_k);
			line(decay_x, sustain_y, sustain_x, sustain_y);
			segment(sustain_x, sustain_level, release_x, 0.f, g.release_k);
			line(release_x, kBaseY, end_x, kBaseY);
			dotted(attack_x, attack_x > start_x + 3, kTopY);
			dotted(decay_x, decay_x - attack_x > 4, kTopY);
			dotted(sustain_x, true, sustain_y > kTopY ? kTopY : sustain_y);
			indicator(attack_x, kTopY, selected_pos == 0);
			indicator(decay_x, sustain_y, selected_pos == 1);
			indicator(decay_x + (sustain_x - decay_x) / 2, sustain_y, selected_pos == 2);
			indicator(release_x, kBaseY, selected_pos == 3);
			break;
		}
		}
	}
};

/// `EqMenu::renderMenuItems` with `drawControlIndicator` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`), on the
/// reference canvas of the 48-row panel; its `uint8_t`s and integer `std::lerp`s kept.
struct LegacyEq {
	Canvas& image;
	int32_t selected_x = -1;
	int32_t selected_y = -1;

	void indicator(int32_t ix, int32_t iy, bool is_selected) {
		if (!is_selected && ix == selected_x && iy == selected_y) {
			return;
		}
		for (int32_t x = ix - 1; x <= ix + 1; x++) {
			for (int32_t y = iy - 1; y <= iy + 1; y++) {
				image.clear_pixel(x, y);
			}
		}
		if (is_selected) {
			selected_x = ix;
			selected_y = iy;
			image.invert_area(ix - 1, 3, iy - 1, iy + 1);
		}
		image.draw_rectangle(ix - 2, iy - 2, ix + 2, iy + 2);
	}

	void draw(float bass, float treble, float bass_freq, float treble_freq, int32_t selected) {
		constexpr uint8_t start_y = 5 + deluge::ui::gfx::oled::kTextTitleSizeY + 5; // OLED_MAIN_TOPMOST_PIXEL is 5
		constexpr uint8_t end_y = 48 - 6;
		constexpr uint8_t center_y = start_y + (end_y - start_y) / 2;
		constexpr uint8_t height = end_y - start_y;
		constexpr uint8_t padding_x = 4;
		constexpr uint8_t start_x = padding_x - 1;
		constexpr uint8_t end_x = 128 - padding_x;
		constexpr uint8_t slope_width = 12;
		constexpr uint8_t bass_band_travel_width = (end_x - start_x) / 2 - slope_width;
		constexpr uint8_t treble_band_travel_width = (end_x - start_x) * 0.75f;

		constexpr uint8_t bass_x0 = start_x;
		uint8_t bass_x1 = std::lerp(bass_x0, bass_x0 + bass_band_travel_width, bass_freq);
		uint8_t bass_x2 = bass_x1 + slope_width;
		const uint8_t bass_y1 = std::lerp(end_y, end_y - height, bass);
		uint8_t bass_y2 = center_y;

		constexpr uint8_t treble_x0 = end_x;
		const uint8_t treble_x1 = std::lerp(end_x - treble_band_travel_width, end_x, treble_freq);
		const uint8_t treble_x2 = treble_x1 - slope_width;
		const uint8_t treble_y1 = std::lerp(end_y, end_y - height, treble);
		uint8_t treble_y2 = center_y;

		if (bass_x2 > treble_x2) {
			const uint8_t diff = bass_x2 - treble_x2;
			bass_x2 -= diff;
			bass_x1 -= diff;
		}
		const auto center_between = [](uint8_t a, uint8_t b) { return std::min(a, b) + std::abs(b - a) / 2; };
		const float morph = 1.0f - (treble_x2 - bass_x2) / 14.f;
		if (morph > 0.f) {
			const uint8_t target_y = center_between(bass_y1, treble_y1);
			bass_y2 = std::lerp(bass_y2, target_y, morph);
			treble_y2 = std::lerp(treble_y2, target_y, morph);
		}

		image.draw_line(bass_x0, bass_y1, bass_x1, bass_y1);
		image.draw_line(bass_x1, bass_y1, bass_x2, bass_y2);
		image.draw_line(bass_x2, bass_y2, treble_x2, treble_y2);
		image.draw_line(treble_x2, treble_y2, treble_x1, treble_y1);
		image.draw_line(treble_x1, treble_y1, treble_x0, treble_y1);

		if (std::abs(center_y - bass_y1) > 1) {
			for (uint8_t x = 0; x <= bass_x2; x++) {
				if (x % 6 == 3 && std::abs(x - bass_x2) > 1 && std::abs(x - treble_x2) > 1) {
					image.draw_pixel(x, center_y);
				}
			}
		}
		if (std::abs(center_y - treble_y1) > 1) {
			for (uint8_t x = 0; x <= end_x; x++) {
				if (x % 6 == 3 && std::abs(x - bass_x2) > 1 && std::abs(x - treble_x2) > 1) {
					image.draw_pixel(x, center_y);
				}
			}
		}
		for (uint8_t y = start_y - 1; y <= end_y + 1; y += 4) {
			image.draw_pixel(bass_x2, y);
			image.draw_pixel(treble_x2, y);
		}

		indicator(center_between(bass_x0, bass_x1), bass_y1, selected == 0);
		indicator(bass_x2, bass_y2, selected == 1);
		indicator(treble_x2, treble_y2, selected == 2);
		indicator(center_between(treble_x1, treble_x0), treble_y1, selected == 3);
	}
};

} // namespace

// clang-format off
describe column_drawings_w("ui::widgets::oled::column_drawings", $ {

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

	it("draws the recorder's two-slot column as main's: big digit when focused or fully inverted, rule after source 1", _ {
		constexpr int32_t kTwoSlots = 63;
		for (const int32_t source : {0, 1}) {
			for (const bool focused : {false, true}) {
				for (const widgets::core::Highlighting h : {widgets::core::Highlighting::PartialInversion, widgets::core::Highlighting::FullInversion}) {
					uitest::Rig48 r;
					widgets::core::Style style = r.style;
					style.highlighting = h;
					OledFrame frame = root_frame(r.canvas, style, r.ctx);
					DrawColumn data{.shape = DrawColumn::Shape::Recorder, .raw = source};
					// No label (`showColumnLabel()` false), so the whole 25-row box.
					frame.region(Rect{kStartX, kStartY - 5, kTwoSlots, 25}, false,
					             [&](OledFrame& slot) { draw_column(slot, focused, &data); });
					legacy_recorder(r.reference, kTwoSlots, source, focused, h == widgets::core::Highlighting::FullInversion);
					expect(r.pbm()).to_equal(r.reference_pbm());
				}
			}
		}
	});

	it("draws the loop points' and the clip end's dotted line and icon as main's", _ {
		for (const auto shape : {DrawColumn::Shape::LoopStart, DrawColumn::Shape::LoopEnd, DrawColumn::Shape::ClipEnd}) {
			uitest::Rig48 r;
			draw(r, DrawColumn{.shape = shape});
			if (shape == DrawColumn::Shape::ClipEnd) {
				legacy_clip_end(r.reference);
			}
			else {
				legacy_loop_point(r.reference, shape == DrawColumn::Shape::LoopStart);
			}
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("draws the envelope as legacy's page does, in every mode, curve and focus", _ {
		using Mode = EnvelopeGraph::Mode;
		for (const Mode mode : {Mode::ADSR, Mode::AHD, Mode::AR, Mode::MPE}) {
			for (const float curve : {-12.5f, 0.f, 8.f, 12.5f}) {
				for (const int32_t value : {0, 7, 25, 50}) {
					for (const int32_t focus : {0, 1, 2, 3}) {
						const EnvelopeGraph g{.attack = value, .decay = 50 - value, .sustain = value, .release = 50 - value,
						                      .mode = mode, .attack_k = curve / 2, .decay_k = curve, .release_k = curve};
						uitest::Rig48 r;
						OledFrame panel = panel_frame(r.frame);
						envelope_graph(panel, g, focus);
						LegacyEnvelope{.image = r.reference, .g = g, .selected_pos = focus}.draw();
						expect(r.pbm()).to_equal(r.reference_pbm());
					}
				}
			}
		}
	});

	it("draws the EQ as main's page does, over every value of each knob", _ {
		const auto at = [](int32_t knob, int32_t v) {
			return knob == 3 ? static_cast<float>(v) / 32.f : static_cast<float>(v) / 50.f;
		};
		const auto top = [](int32_t knob) { return knob == 3 ? 32 : 50; };
		for (int32_t swept = 0; swept < 4; swept++) {
			for (int32_t v = 0; v <= top(swept); v++) {
				for (int32_t others = 0; others < 27; others++) {
					std::array<float, 4> knobs{};
					int32_t digits = others;
					for (int32_t k = 0; k < 4; k++) {
						if (k == swept) {
							knobs[k] = at(k, v);
							continue;
						}
						const int32_t level = digits % 3;
						digits /= 3;
						knobs[k] = at(k, level == 0 ? 0 : level == 1 ? top(k) / 2 : top(k));
					}
					for (const int32_t focus : {0, 1, 2, 3}) {
						uitest::Rig48 r;
						OledFrame panel = panel_frame(r.frame);
						eq_graph(panel, knobs[0], knobs[1], knobs[2], knobs[3], focus);
						LegacyEq{.image = r.reference}.draw(knobs[0], knobs[1], knobs[2], knobs[3], focus);
						expect(r.pbm()).to_equal(r.reference_pbm());
					}
				}
			}
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

DELUGE_SPEC(column_drawings_spec, column_drawings_w)
