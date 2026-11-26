#include "ui/widgets/oled/column_drawings.h"

#include "ui/gfx/oled/icons.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/core/style.h"
#include "ui/widgets/oled/meters.h"
#include "ui/widgets/oled/page_drawing.h"
#include "ui/widgets/oled/waveform.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace deluge::ui::widgets::oled {

namespace {

/// @brief `sigmoidLikeCurve` (`util/functions.cpp`).
float sigmoid_like_curve(float x, float x_max, float softening) {
	const float raw = x / (x + softening);
	const float max_val = x_max / (x_max + softening);
	return raw / max_val;
}

} // namespace

void filter_graph(OledFrame& f, gfx::oled::Rect slot, const FilterGraph& g) {
	// Canvas coordinates throughout, as legacy draws them: its `uint8_t`/`int8_t` truncations are part of the pixels.
	const int32_t slot_x = f.origin.x + slot.x;
	const int32_t slot_y = f.origin.y + slot.y;

	const float freq_value = static_cast<float>(g.freq_raw) / 50.f;
	const float reso_value = sigmoid_like_curve(static_cast<float>(g.reso_raw), 50.f, 30.f);
	float morph_value = g.morphable ? static_cast<float>(g.morph_raw) / 50.f : 0.f;
	if (g.hpf) {
		morph_value = 1.0f - morph_value;
	}

	constexpr uint8_t reso_segment_width = 6;
	constexpr uint8_t freq_slope_width = 5;
	constexpr uint8_t padding_x = 5;
	const auto total_width = static_cast<uint8_t>(slot.w - 2 - padding_x * 2);
	const auto base_width = static_cast<uint8_t>(total_width - freq_slope_width - reso_segment_width);

	const auto min_x = static_cast<uint8_t>(slot_x + padding_x);
	const auto max_x = static_cast<uint8_t>(min_x + total_width);
	auto reso_x0 = static_cast<uint8_t>(min_x - reso_segment_width + base_width * freq_value);
	auto reso_x1 = static_cast<uint8_t>(reso_x0 + reso_segment_width);
	auto reso_x2 = static_cast<uint8_t>(reso_x1 + reso_segment_width);
	auto left_slope_x0 = static_cast<int8_t>(reso_x0 - base_width - freq_slope_width);
	auto left_slope_x1 = static_cast<int8_t>(left_slope_x0 + freq_slope_width);
	auto right_slope_x0 = reso_x2;
	auto right_slope_x1 = static_cast<uint8_t>(right_slope_x0 + freq_slope_width);

	if (morph_value > 0) {
		const auto slope_shift = static_cast<uint8_t>(std::lerp(0, total_width, morph_value));
		const auto reso_shift = static_cast<uint8_t>(std::lerp(0, freq_slope_width + reso_segment_width, morph_value));
		left_slope_x0 = static_cast<int8_t>(left_slope_x0 + slope_shift);
		left_slope_x1 = static_cast<int8_t>(left_slope_x1 + slope_shift);
		right_slope_x0 = static_cast<uint8_t>(right_slope_x0 + slope_shift);
		right_slope_x1 = static_cast<uint8_t>(right_slope_x1 + slope_shift);
		reso_x0 = static_cast<uint8_t>(reso_x0 + reso_shift);
		reso_x1 = static_cast<uint8_t>(reso_x1 + reso_shift);
		reso_x2 = static_cast<uint8_t>(reso_x2 + reso_shift);
	}

	constexpr uint8_t container_height = 23;
	const auto start_y = static_cast<uint8_t>(slot_y + 1);
	const auto end_y = static_cast<uint8_t>(start_y + container_height - 1);
	const auto center_y = static_cast<uint8_t>(start_y + (end_y - start_y) / 2);
	const auto reso_y = static_cast<uint8_t>(std::lerp(center_y, start_y, reso_value));

	// The outline, through the frame so the style's rounding applies.
	f.rect_rounded(gfx::oled::Rect{min_x - 1 - f.origin.x, start_y - 1 - f.origin.y, (max_x + 1) - (min_x - 1) + 1,
	                               (end_y + 1) - (start_y - 1) + 1});

	// The segments are in canvas coordinates, as legacy computes them; the fill's dot grid is the panel's.
	const auto local = [&](int32_t x, int32_t y) { return gfx::oled::Point{x - f.origin.x, y - f.origin.y}; };
	const auto segment = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
		fill_line(f, local(x0, y0), local(x1, y1), end_y - f.origin.y, min_x - f.origin.x, max_x - f.origin.x);
	};

	segment(left_slope_x0, end_y, left_slope_x1, center_y); // left slope
	segment(left_slope_x1, center_y, reso_x0, center_y);    // body
	segment(reso_x0, center_y, reso_x1, reso_y);            // resonance
	segment(reso_x1, reso_y, reso_x2, center_y);
	segment(reso_x2, center_y, right_slope_x0, center_y);
	segment(right_slope_x0, center_y, right_slope_x1, end_y); // right slope
}

namespace {

/// @brief `EnvelopeCurve::rising`: 0 at @p x = 0, 1 at 1, bent by @p k octaves; 0 is a straight line.
float rising(float k, float x) {
	if (k == 0.f) {
		return x;
	}
	const float floor = std::exp2(-k);
	return 1.f - ((std::exp2(-k * x) - floor) / (1.f - floor));
}

/// @brief One stage of the envelope: from one level to another, bent by `k` octaves.
struct Stage {
	float from;
	float to;
	float k;
};

float stage_level(float t, const void* ctx) {
	const auto& s = *static_cast<const Stage*>(ctx);
	return s.from + (s.to - s.from) * rising(s.k, t);
}

} // namespace

void envelope_graph(OledFrame& f, const EnvelopeGraph& envelope, int32_t selected) {
	const PageBody b = page_body(f);
	const auto start_x = static_cast<float>(b.left);
	const auto end_x = static_cast<float>(b.right);
	const auto max_segment_width = static_cast<float>((b.right - b.left) / 4);

	const auto ix = [](float v) { return static_cast<int32_t>(v); };
	const auto fraction = [](int32_t value) { return static_cast<float>(value) / 50.0f; };
	const auto stage = [&](float x0, float from, float x1, float to, float k) {
		const Stage s{.from = from, .to = to, .k = k};
		curve(f, b, x0, x1, stage_level, &s);
	};
	// Lines close to each other look messy: a caller leaves the nearer out
	const auto dotted = [&](float x, bool draw, int32_t top_y) {
		if (draw) {
			guide_v(f, b, ix(x), top_y);
		}
	};
	Handles marks{f};
	const auto mark = [&](float x, int32_t y, int32_t segment) { marks.draw(ix(x), y, selected == segment); };

	using Mode = EnvelopeGraph::Mode;
	const float attack_x = std::round(start_x + fraction(envelope.attack) * max_segment_width);
	const float decay_width = sigmoid_like_curve(static_cast<float>(envelope.decay), 50.0f, 8.0f) * max_segment_width;
	const float sustain_level = envelope.mode == Mode::AHD ? 0.f : fraction(envelope.sustain);
	const int32_t sustain_y = b.y_at(sustain_level);
	const float sustain_x = start_x + max_segment_width * 3;
	const float release_x = std::round(sustain_x + fraction(envelope.release) * (end_x - sustain_x));

	// AR rises to its level; every other mode to full
	const float attack_level = envelope.mode == Mode::AR ? sustain_level : 1.f;
	stage(start_x, 0.f, attack_x, attack_level, envelope.attack_k);

	switch (envelope.mode) {
	case Mode::AHD: {
		const float hold_x = std::round(attack_x + decay_width);
		// The decay gets the room the release would have had, so the line ends where the release knob says
		const float decay_end_x = std::round(hold_x + fraction(envelope.release) * (end_x - hold_x));
		f.line(ix(attack_x), b.top, ix(hold_x), b.top);
		stage(hold_x, 1.f, decay_end_x, 0.f, envelope.decay_k);
		f.line(ix(decay_end_x), b.bottom, ix(end_x), b.bottom);
		dotted(attack_x, attack_x > start_x + 3, b.top);
		dotted(hold_x, hold_x - attack_x > 4, b.top);
		mark(attack_x, b.top, 0);
		mark(hold_x, b.top, 1);
		mark(decay_end_x, b.bottom, 3);
		break;
	}
	case Mode::AR: // up to the level while held, down on release
		f.line(ix(attack_x), sustain_y, ix(sustain_x), sustain_y);
		stage(sustain_x, sustain_level, release_x, 0.f, envelope.release_k);
		f.line(ix(release_x), b.bottom, ix(end_x), b.bottom);
		dotted(sustain_x, true, sustain_y);
		mark(attack_x, b.y_at(attack_level), 0);
		mark(attack_x + (sustain_x - attack_x) / 2, sustain_y, 2);
		mark(release_x, b.bottom, 3);
		break;
	case Mode::MPE: // up to full with the input fully on, resting at the level with it off, down on release
		f.line(ix(attack_x), b.top, ix(sustain_x), b.top);
		stage(sustain_x, 1.f, release_x, 0.f, envelope.release_k);
		f.line(ix(release_x), b.bottom, ix(end_x), b.bottom);
		guide_h(f, sustain_y, b.left, ix(sustain_x));
		dotted(sustain_x, true, b.top);
		mark(attack_x, b.top, 0);
		mark(start_x + (sustain_x - start_x) / 2, sustain_y, 2);
		mark(release_x, b.bottom, 3);
		break;
	case Mode::ADSR: {
		const float decay_x = std::round(attack_x + decay_width);
		stage(attack_x, 1.f, decay_x, sustain_level, envelope.decay_k);
		f.line(ix(decay_x), sustain_y, ix(sustain_x), sustain_y);
		stage(sustain_x, sustain_level, release_x, 0.f, envelope.release_k);
		f.line(ix(release_x), b.bottom, ix(end_x), b.bottom);
		dotted(attack_x, attack_x > start_x + 3, b.top);
		dotted(decay_x, decay_x - attack_x > 4, b.top);
		dotted(sustain_x, true, sustain_y > b.top ? b.top : sustain_y);
		mark(attack_x, b.top, 0);
		mark(decay_x, sustain_y, 1);
		mark(decay_x + (sustain_x - decay_x) / 2, sustain_y, 2);
		mark(release_x, b.bottom, 3);
		break;
	}
	}
}

void eq_graph(OledFrame& f, float bass, float treble, float bass_freq, float treble_freq, int32_t selected) {
	const PageBody b = page_body(f);
	const int32_t center_y = b.center_y();
	const auto start_x = static_cast<uint8_t>(b.left);
	const auto end_x = static_cast<uint8_t>(b.right);
	constexpr uint8_t slope_width = 12;
	const auto bass_band_travel_width = static_cast<uint8_t>((end_x - start_x) / 2 - slope_width);
	const auto treble_band_travel_width = static_cast<uint8_t>((end_x - start_x) * 0.75f);

	const uint8_t bass_x0 = start_x;
	auto bass_x1 = static_cast<uint8_t>(std::lerp(bass_x0, bass_x0 + bass_band_travel_width, bass_freq));
	auto bass_x2 = static_cast<uint8_t>(bass_x1 + slope_width);
	const int32_t bass_y1 = b.y_at(bass);
	int32_t bass_y2 = center_y;

	const uint8_t treble_x0 = end_x;
	const auto treble_x1 = static_cast<uint8_t>(std::lerp(end_x - treble_band_travel_width, end_x, treble_freq));
	const auto treble_x2 = static_cast<uint8_t>(treble_x1 - slope_width);
	const int32_t treble_y1 = b.y_at(treble);
	int32_t treble_y2 = center_y;

	// The treble band may push into the bass's territory (`EqMenu::renderMenuItems` in
	// `origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`).
	if (bass_x2 > treble_x2) {
		const auto diff = static_cast<uint8_t>(bass_x2 - treble_x2);
		bass_x2 = static_cast<uint8_t>(bass_x2 - diff);
		bass_x1 = static_cast<uint8_t>(bass_x1 - diff);
	}

	const auto center_between = [](int32_t p, int32_t q) { return std::min(p, q) + std::abs(q - p) / 2; };
	const float morph = 1.0f - static_cast<float>(treble_x2 - bass_x2) / 14.f;
	if (morph > 0.f) {
		const int32_t target_y = center_between(bass_y1, treble_y1);
		bass_y2 = static_cast<int32_t>(std::lerp(bass_y2, target_y, morph));
		treble_y2 = static_cast<int32_t>(std::lerp(treble_y2, target_y, morph));
	}

	const std::array<gfx::oled::Point, 6> shape{{{bass_x0, bass_y1},
	                                             {bass_x1, bass_y1},
	                                             {bass_x2, bass_y2},
	                                             {treble_x2, treble_y2},
	                                             {treble_x1, treble_y1},
	                                             {treble_x0, treble_y1}}};
	polyline(f, shape);

	const std::array<int32_t, 2> band_edges{bass_x2, treble_x2};
	if (std::abs(center_y - bass_y1) > 1) {
		guide_h(f, center_y, 0, bass_x2, band_edges);
	}
	if (std::abs(center_y - treble_y1) > 1) {
		guide_h(f, center_y, 0, end_x, band_edges);
	}
	guide_v(f, b, bass_x2, b.top);
	guide_v(f, b, treble_x2, b.top);

	Handles marks{f};
	marks.draw(center_between(bass_x0, bass_x1), bass_y1, selected == 0);
	marks.draw(bass_x2, bass_y2, selected == 1);
	marks.draw(treble_x2, treble_y2, selected == 2);
	marks.draw(center_between(treble_x1, treble_x0), treble_y1, selected == 3);
}

namespace {

/// `voice::Portamento::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`), in canvas
/// coordinates with its `uint8_t`s.
void glide(OledFrame& f, gfx::oled::Rect slot, float norm) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	const int32_t start_x = f.origin.x + slot.x;
	const int32_t start_y = f.origin.y + slot.y;
	constexpr uint8_t porta_graphics_width = 25;
	const auto center_x = static_cast<uint8_t>(start_x + slot.w / 2);
	const auto porta_start_x = static_cast<uint8_t>(start_x + 2);
	const auto porta_end_x = static_cast<uint8_t>(porta_start_x + porta_graphics_width - 1);
	constexpr uint8_t porta_graphics_height = 11;
	const auto porta_start_y = static_cast<uint8_t>(start_y + core::kHorizontalSlotYOffset - 1);
	const auto porta_end_y = static_cast<uint8_t>(porta_start_y + porta_graphics_height - 1);
	constexpr uint8_t porta_line_width = 9;
	const auto porta_x0 = static_cast<uint8_t>(std::lerp(center_x, center_x - porta_line_width, norm));
	const auto porta_x1 = static_cast<uint8_t>(std::lerp(center_x, center_x + porta_line_width, norm));
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

/// `unison::Detune::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/unison/detune.h`): three lines
/// splaying as the detune grows.
void detune(OledFrame& f, gfx::oled::Rect slot, float norm) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	const int32_t start_x = f.origin.x + slot.x;
	const int32_t start_y = f.origin.y + slot.y;
	constexpr int32_t line_spacing = 5;
	constexpr int32_t max_y_offset = 4;
	const int32_t x0 = start_x + 6;
	const int32_t x1 = start_x + slot.w - 7;
	const int32_t top = start_y + core::kHorizontalSlotYOffset;
	image.draw_line(x0, top, x1, top + static_cast<int32_t>(max_y_offset * norm * 0.30f));

	const int32_t middle = top + line_spacing;
	const auto spread = static_cast<int32_t>(max_y_offset * norm * 0.5f);
	image.draw_line(x0, middle - spread, x1, middle + spread);
	if (norm > 0.7 && norm < 1) {
		image.clear_pixel(x0, middle - spread);
		image.clear_pixel(x1, middle + spread);
		image.draw_pixel(x0, middle - spread - 1);
		image.draw_pixel(x1, middle + spread + 1);
	}

	const int32_t bottom = top + 2 * line_spacing;
	int32_t rise = bottom - static_cast<int32_t>(max_y_offset * norm * 0.8f);
	if (norm > 0 && rise == bottom) {
		rise -= 1;
	}
	image.draw_line(x0, rise, x1 - 8, bottom);
	image.draw_horizontal_line(bottom, x1 - 8, x1);
}

/// `osc::PulseWidth::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`): one cycle of
/// the wave, its high part narrowing as the value grows.
void pulse_width(OledFrame& f, gfx::oled::Rect slot, int32_t value) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	const float norm = static_cast<float>(value) / 50.0f;
	constexpr int32_t x_padding = 4;
	const auto width = static_cast<uint8_t>(slot.w - x_padding * 2);
	const int32_t start_x = f.origin.x + slot.x + x_padding;
	const int32_t end_x = start_x + width - 1;
	const int32_t start_y = f.origin.y + slot.y + core::kHorizontalSlotYOffset;
	const int32_t end_y = f.origin.y + slot.y + slot.h - 6;
	const int32_t pw_min_x = start_x + 2;
	const int32_t pw_max_x = start_x + width / 2;
	const int32_t pw_width = pw_max_x - pw_min_x;
	const auto pw_x = static_cast<int32_t>(static_cast<float>(pw_max_x) - static_cast<float>(pw_width) * norm);
	image.draw_vertical_line(start_x, start_y, end_y);
	image.draw_horizontal_line(start_y, start_x, pw_x);
	image.draw_vertical_line(pw_x, start_y, end_y);
	image.draw_horizontal_line(end_y, pw_x, end_x);
}

/// `delay::Amount::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/delay/amount.h`): `Number::drawBar`
/// (`origin/main:src/deluge/gui/menu_item/number.cpp`) over the value clamped to 0..24, and above 24 a filled box with
/// an inverted "!" across the bar.
void delay_amount(OledFrame& f, gfx::oled::Rect slot, int32_t value) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	constexpr int32_t kMaxShown = 24;
	const int32_t start_x = f.origin.x + slot.x;
	const int32_t start_y = f.origin.y + slot.y;
	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const auto bar_start_x = static_cast<uint8_t>(start_x + 5);
	const auto bar_start_y = static_cast<uint8_t>(start_y + core::kHorizontalSlotYOffset + 2);
	const auto bar_end_x = static_cast<uint8_t>(bar_start_x + bar_width - 1);
	const auto bar_end_y = static_cast<uint8_t>(bar_start_y + bar_height - 1);
	image.draw_rectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                     bar_end_y + outline_padding);
	const float norm = static_cast<float>(std::clamp<int32_t>(value, 0, kMaxShown)) / static_cast<float>(kMaxShown);
	const auto fill_width = static_cast<uint8_t>(norm * bar_width);
	image.invert_area(bar_start_x, fill_width, bar_start_y, bar_end_y);
	if (value <= kMaxShown) {
		return;
	}
	constexpr uint8_t excl_mark_width = 3;
	constexpr uint8_t excl_mark_height = 11;
	const auto center_x = static_cast<uint8_t>(start_x + slot.w / 2);
	const auto excl_mark_start_y = static_cast<uint8_t>(start_y + core::kHorizontalSlotYOffset - 1);
	const auto excl_mark_end_y = static_cast<uint8_t>(excl_mark_start_y + excl_mark_height - 1);
	const auto excl_mark_start_x = static_cast<uint8_t>(center_x - 1);
	for (auto x = static_cast<uint8_t>(center_x - 2); x <= center_x + 2; x++) {
		for (auto y = static_cast<uint8_t>(excl_mark_start_y - 1); y <= excl_mark_end_y + 1; y++) {
			image.draw_pixel(x, y);
		}
	}
	image.invert_area(excl_mark_start_x, excl_mark_width, excl_mark_start_y, excl_mark_start_y + 6);
	image.invert_area(excl_mark_start_x, excl_mark_width, excl_mark_start_y + 8, excl_mark_start_y + 10);
}

/// `osc::AudioRecorder::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`): a filled
/// dot and "rec", the source's number in the Big face when focused or under full inversion, and a dotted rule after
/// source 1 otherwise.
void recorder(OledFrame& f, gfx::oled::Rect slot, int32_t source, bool focused) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	const int32_t slot_x = f.origin.x + slot.x;
	const int32_t slot_y = f.origin.y + slot.y;
	const auto start_x = static_cast<uint8_t>(slot_x + 8);
	const auto start_y = static_cast<uint8_t>(slot_y + core::kHorizontalSlotYOffset + 6);
	constexpr uint8_t circle_radius = 3;
	image.draw_circle(start_x + circle_radius + 1, start_y + circle_radius + 1, circle_radius, true);
	image.draw_string("rec", start_x + circle_radius * 2 + 5, start_y, core::kTextSpacingX, core::kTextSpacingY);
	const char digit[] = {static_cast<char>('1' + source), 0};
	const auto source_x = static_cast<uint8_t>(slot_x + slot.w - core::kTextBigSpacingX - 7);
	const auto source_y = static_cast<uint8_t>(slot_y + core::kHorizontalSlotYOffset + 4);
	const bool full_inversion = f.style.highlighting == core::Highlighting::FullInversion;
	if (full_inversion || focused) {
		image.draw_string(digit, source_x, source_y, core::kTextBigSpacingX, core::kTextBigSizeY);
	}
	else {
		image.draw_string(digit, source_x - 1, source_y + 2, core::kTextSpacingX, core::kTextSpacingY);
	}
	if (!full_inversion && source == 0) {
		const auto y0 = static_cast<uint8_t>(slot_y + core::kHorizontalSlotYOffset + 1);
		const auto y1 = static_cast<uint8_t>(y0 + 18);
		for (uint8_t y = y0; y <= y1; y += 2) {
			image.draw_pixel(slot_x + slot.w - 1, y);
		}
	}
}

/// `sample::LoopPoint::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`): a dotted
/// marker line, the loop icon beside it, mirrored for the end.
void loop_point(OledFrame& f, gfx::oled::Rect slot, bool start) {
	// Drawn through the panel's frame: the end marker's icon starts two pixels left of its region, as legacy draws it.
	OledFrame panel = panel_frame(f);
	const int32_t dx = f.origin.x - panel.origin.x;
	const int32_t dy = f.origin.y - panel.origin.y;
	const int32_t line_x = dx + (start ? slot.x + 8 : slot.x + slot.w - 12);
	const int32_t top = dy + slot.y + core::kHorizontalSlotYOffset;
	loop_marker(panel, line_x, top - 1, dy + slot.y + slot.h - 5, top, start);
}

/// `audio_clip::SampleMarkerEditor::renderInHorizontalMenu`
/// (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`).
void clip_end(OledFrame& f, gfx::oled::Rect slot) {
	ClipScope scope{f.canvas, f.clip};
	gfx::oled::Canvas& image = f.canvas;
	const int32_t slot_x = f.origin.x + slot.x;
	const int32_t slot_y = f.origin.y + slot.y;
	const int32_t line_x = slot_x + slot.w - 6;
	for (int32_t y = slot_y + 1; y <= slot_y + slot.h - 5; y += 2) {
		image.draw_pixel(line_x, y);
	}
	image.draw_icon(gfx::oled::icons::loopPoint, slot_x + 5, slot_y + core::kHorizontalSlotYOffset, true);
}

} // namespace

void draw_column(OledFrame& slot_frame, bool focused, void* ctx) {
	const auto* data = static_cast<const DrawColumn*>(ctx);
	if (data == nullptr) {
		return;
	}
	switch (data->shape) {
	case DrawColumn::Shape::FilterGraph:
		filter_graph(slot_frame, slot_frame.bounds(), data->filter);
		break;
	case DrawColumn::Shape::Slider:
		meter(slot_frame, slot_frame.bounds(), Hint::Slider, data->norm, 0);
		break;
	case DrawColumn::Shape::Glide:
		glide(slot_frame, slot_frame.bounds(), data->norm);
		break;
	case DrawColumn::Shape::Detune:
		detune(slot_frame, slot_frame.bounds(), data->norm);
		break;
	case DrawColumn::Shape::PulseWidth:
		pulse_width(slot_frame, slot_frame.bounds(), data->raw);
		break;
	case DrawColumn::Shape::DelayAmount:
		delay_amount(slot_frame, slot_frame.bounds(), data->raw);
		break;
	case DrawColumn::Shape::Recorder:
		recorder(slot_frame, slot_frame.bounds(), data->raw, focused);
		break;
	case DrawColumn::Shape::LoopStart:
	case DrawColumn::Shape::LoopEnd:
		loop_point(slot_frame, slot_frame.bounds(), data->shape == DrawColumn::Shape::LoopStart);
		break;
	case DrawColumn::Shape::ClipEnd:
		clip_end(slot_frame, slot_frame.bounds());
		break;
	}
}

} // namespace deluge::ui::widgets::oled
