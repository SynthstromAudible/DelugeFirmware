#include "support/frames.h"
#include "ui/gfx/oled/canvas.h"
#include "ui/gfx/oled/icons.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/core/style.h"
#include "ui/widgets/oled/column_drawings.h"
#include "ui/widgets/oled/page_drawing.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>

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

	it("draws each envelope mode on both panels", _ {
		using Mode = EnvelopeGraph::Mode;
		struct Case {
			const char* name;
			Mode mode;
			float k;
			int32_t focus;
		};
		const std::array<Case, 6> cases{{{"adsr", Mode::ADSR, 4.f, 1},
		                                 {"ahd", Mode::AHD, 4.f, 0},
		                                 {"ar", Mode::AR, 4.f, 2},
		                                 {"mpe", Mode::MPE, 4.f, 3},
		                                 {"adsr_steep", Mode::ADSR, 12.5f, 2},
		                                 {"adsr_concave", Mode::ADSR, -12.5f, 3}}};
		for (const Case& c : cases) {
			const EnvelopeGraph g{.attack = 7, .decay = 25, .sustain = 25, .release = 15, .mode = c.mode,
			                      .attack_k = c.k / 2, .decay_k = c.k, .release_k = c.k};
			uitest::Rig48 r48;
			envelope_graph(r48.frame, g, c.focus);
			expect_golden(self, std::string("ui/widgets/envelope_") + c.name + "_48.pbm", r48.pbm());
			uitest::Rig64 r64;
			envelope_graph(r64.frame, g, c.focus);
			expect_golden(self, std::string("ui/widgets/envelope_") + c.name + "_64.pbm", r64.pbm());
		}
	});

	it("keeps every envelope pixel within two of the page body, in every mode, curve, value and focus", _ {
		using Mode = EnvelopeGraph::Mode;
		for (const Mode mode : {Mode::ADSR, Mode::AHD, Mode::AR, Mode::MPE}) {
			for (const float curve : {-12.5f, 0.f, 8.f, 12.5f}) {
				for (const int32_t value : {0, 7, 25, 50}) {
					for (const int32_t focus : {0, 1, 2, 3}) {
						const EnvelopeGraph g{.attack = value, .decay = 50 - value, .sustain = value,
						                      .release = 50 - value, .mode = mode, .attack_k = curve / 2,
						                      .decay_k = curve, .release_k = curve};
						uitest::Rig48 r;
						envelope_graph(r.frame, g, focus);
						const PageBody b = page_body(r.frame);
						int32_t outside = 0;
						for (int32_t y = 0; y < 43; y++) {
							for (int32_t x = 0; x < 128; x++) {
								const bool in = x >= b.left - 2 && x <= b.right + 2 && y >= b.top - 2 && y <= b.bottom + 2;
								outside += (!in && r.lit(x, y + 5)) ? 1 : 0;
							}
						}
						expect(outside).to_equal(0);
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
						eq_graph(r.frame, knobs[0], knobs[1], knobs[2], knobs[3], focus);
						LegacyEq{.image = r.reference}.draw(knobs[0], knobs[1], knobs[2], knobs[3], focus);
						expect(r.pbm()).to_equal(r.reference_pbm());
					}
				}
			}
		}
	});

	it("draws the EQ on both panels", _ {
		struct Case {
			const char* name;
			std::array<float, 4> knobs;
			int32_t focus;
		};
		const std::array<Case, 3> cases{{{"flat", {0.5f, 0.5f, 0.5f, 0.5f}, 0},
		                                 {"boost", {1.f, 0.2f, 0.3f, 0.75f}, 1},
		                                 {"cross", {0.f, 1.f, 1.f, 0.f}, 2}}};
		for (const Case& c : cases) {
			uitest::Rig48 r48;
			eq_graph(r48.frame, c.knobs[0], c.knobs[1], c.knobs[2], c.knobs[3], c.focus);
			expect_golden(self, std::string("ui/widgets/eq_graph_") + c.name + "_48.pbm", r48.pbm());
			uitest::Rig64 r64;
			eq_graph(r64.frame, c.knobs[0], c.knobs[1], c.knobs[2], c.knobs[3], c.focus);
			expect_golden(self, std::string("ui/widgets/eq_graph_") + c.name + "_64.pbm", r64.pbm());
		}
	});

	it("draws the filter curve as it does today, low and high pass, with resonance and morph", _ {
		struct Case {
			const char* name;
			FilterGraph g;
		};
		const std::array<Case, 4> cases{{
		    {"lpf", {.freq_raw = 30, .reso_raw = 0, .morph_raw = 0, .morphable = false, .hpf = false}},
		    {"lpf_reso", {.freq_raw = 20, .reso_raw = 40, .morph_raw = 0, .morphable = false, .hpf = false}},
		    {"hpf_morph", {.freq_raw = 25, .reso_raw = 25, .morph_raw = 20, .morphable = true, .hpf = true}},
		    {"lpf_morph", {.freq_raw = 45, .reso_raw = 50, .morph_raw = 35, .morphable = true, .hpf = false}},
		}};
		for (const Case& c : cases) {
			uitest::Rig48 r;
			r.frame.region(Rect{0, 9, 64, 28}, false, [&](OledFrame& slot) { filter_graph(slot, slot.bounds(), c.g); });
			expect_golden(self, std::string("ui/widgets/filter_graph_") + c.name + "_48.pbm", r.pbm());
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
