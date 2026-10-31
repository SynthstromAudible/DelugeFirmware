#include "ui/oled/column_drawings.h"

#include "ui/oled/meters.h"

#include <cmath>

namespace deluge::ui::oled {

namespace {

/// @brief `sigmoidLikeCurve` (`util/functions.cpp:1170-1174`).
float sigmoid_like_curve(float x, float x_max, float softening) {
	const float raw = x / (x + softening);
	const float max_val = x_max / (x_max + softening);
	return raw / max_val;
}

/// @brief What the fill-pattern callback needs: the canvas and the curve's bottom row, both canvas-space.
struct FillPattern {
	gfx::oled::Canvas* canvas;
	int32_t end_y;
	gfx::oled::Point last{-1, -1};
};

/// @brief `draw_fill_pattern` (`filter_container.h:91-100`): below each third column of the curve, every third row.
void fill_pattern(gfx::oled::Point point, void* ctx) {
	auto* fill = static_cast<FillPattern*>(ctx);
	if (fill->last.x != point.x && point.x % 3 == 0) {
		for (int32_t y = point.y; y <= fill->end_y; y++) {
			if (y % 3 == 1) {
				fill->canvas->draw_pixel(point.x, y);
			}
		}
	}
	fill->last = point;
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

	ClipScope scope{f.canvas, f.clip};
	FillPattern fill{.canvas = &f.canvas, .end_y = end_y};
	const auto segment = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
		fill.last = {-1, -1};
		f.canvas.draw_line(x0, y0, x1, y1,
		                   {.min_x = min_x, .max_x = max_x, .point_callback = fill_pattern, .callback_ctx = &fill});
	};

	segment(left_slope_x0, end_y, left_slope_x1, center_y); // left slope
	segment(left_slope_x1, center_y, reso_x0, center_y);    // body
	segment(reso_x0, center_y, reso_x1, reso_y);            // resonance
	segment(reso_x1, reso_y, reso_x2, center_y);
	segment(reso_x2, center_y, right_slope_x0, center_y);
	segment(right_slope_x0, center_y, right_slope_x1, end_y); // right slope
}

namespace {

/// @brief `drawTransitionIndicator`/`drawControlIndicator` (`envelope_menu.h:96-125`, `eq_menu.h:162-191`).
struct Indicators {
	gfx::oled::Canvas& canvas;
	int32_t selected_x = -1;
	int32_t selected_y = -1;

	void draw(float center_x, float center_y, bool is_selected) {
		const auto ix = static_cast<int32_t>(center_x);
		const auto iy = static_cast<int32_t>(center_y);
		if (!is_selected && ix == selected_x && iy == selected_y) {
			return; // overlap: the selected one stays on top
		}
		constexpr int32_t square_size = 2;
		constexpr int32_t inner = square_size - 1;
		for (int32_t x = ix - inner; x <= ix + inner; x++) {
			for (int32_t y = iy - inner; y <= iy + inner; y++) {
				canvas.clear_pixel(x, y);
			}
		}
		if (is_selected) {
			selected_x = ix;
			selected_y = iy;
			canvas.invert_area(ix - inner, square_size * 2 - 1, iy - inner, iy + inner);
		}
		canvas.draw_rectangle(ix - square_size, iy - square_size, ix + square_size, iy + square_size);
	}
};

} // namespace

void envelope_graph(OledFrame& panel, int32_t attack, int32_t decay, int32_t sustain, int32_t release,
                    int32_t selected) {
	const gfx::oled::Geometry g = panel.canvas.geometry();
	ClipScope scope{panel.canvas, panel.clip};
	gfx::oled::Canvas& image = panel.canvas;

	constexpr int32_t padding_x = 4;
	constexpr int32_t start_x = padding_x;
	const auto start_y = static_cast<uint8_t>(g.visible_top + gfx::oled::kTextTitleSizeY + 7);
	const auto end_y = static_cast<uint8_t>(g.height - 6);
	const int32_t draw_width = g.width - 2 * padding_x;
	const auto draw_height = static_cast<uint8_t>(end_y - start_y);
	const float max_segment_width = static_cast<float>(draw_width / 4);

	const float attack_width = static_cast<float>(attack) / 50.0f * max_segment_width;
	const float decay_width = sigmoid_like_curve(static_cast<float>(decay), 50.0f, 8.0f) * max_segment_width;

	const float attack_x = std::round(start_x + attack_width);
	const float decay_x = std::round(attack_x + decay_width);
	const float sustain_x = start_x + max_segment_width * 3;
	const float release_x = std::round(
	    sustain_x + static_cast<float>(release) / 50.0f * (static_cast<float>(start_x + draw_width) - sustain_x));

	const int32_t base_y = start_y + draw_height;
	const auto sustain_y = static_cast<int32_t>(base_y - std::round(static_cast<float>(sustain) / 50.0f * draw_height));

	const auto ix = [](float v) { return static_cast<int32_t>(v); };
	image.draw_line(start_x, base_y, ix(attack_x), start_y);
	image.draw_line(ix(attack_x), start_y, ix(decay_x), sustain_y);
	image.draw_line(ix(decay_x), sustain_y, ix(sustain_x), sustain_y);
	image.draw_line(ix(sustain_x), sustain_y, ix(release_x), base_y);
	image.draw_line(ix(release_x), base_y, start_x + draw_width, base_y);

	const int32_t visible_height = g.height - g.visible_top;
	for (int32_t y = visible_height + 1; y >= start_y - 2; y -= 4) {
		if (attack_x > start_x + 3) {
			image.draw_pixel(ix(attack_x), y);
		}
		if (decay_x - attack_x > 4) {
			image.draw_pixel(ix(decay_x), y);
		}
		if (sustain_y > start_y || y > sustain_y) {
			image.draw_pixel(ix(sustain_x), y);
		}
	}

	Indicators marks{image};
	marks.draw(attack_x, start_y, selected == 0);
	marks.draw(decay_x, static_cast<float>(sustain_y), selected == 1);
	marks.draw(decay_x + (sustain_x - decay_x) / 2, static_cast<float>(sustain_y), selected == 2);
	marks.draw(release_x, static_cast<float>(base_y), selected == 3);
}

void eq_graph(OledFrame& panel, float bass, float treble, float bass_freq, float treble_freq, int32_t selected) {
	const gfx::oled::Geometry g = panel.canvas.geometry();
	ClipScope scope{panel.canvas, panel.clip};
	gfx::oled::Canvas& image = panel.canvas;

	const auto start_y = static_cast<uint8_t>(g.visible_top + gfx::oled::kTextTitleSizeY + 5);
	const auto end_y = static_cast<uint8_t>(g.height - 6);
	const auto center_y = static_cast<uint8_t>(start_y + (end_y - start_y) / 2);
	const auto height = static_cast<uint8_t>(end_y - start_y);

	constexpr uint8_t padding_x = 4;
	constexpr uint8_t start_x = padding_x - 1;
	const auto end_x = static_cast<uint8_t>(g.width - padding_x);
	constexpr uint8_t slope_width = 12;
	const auto bass_band_travel_width = static_cast<uint8_t>((end_x - start_x) / 2 - slope_width);
	const auto treble_band_travel_width = static_cast<uint8_t>((end_x - start_x) * 0.75f);

	constexpr uint8_t bass_x0 = start_x;
	auto bass_x1 = static_cast<uint8_t>(std::lerp(bass_x0, bass_x0 + bass_band_travel_width, bass_freq));
	auto bass_x2 = static_cast<uint8_t>(bass_x1 + slope_width);
	const auto bass_y1 = static_cast<uint8_t>(std::lerp(end_y, end_y - height, bass));
	auto bass_y2 = center_y;

	const uint8_t treble_x0 = end_x;
	const auto treble_x1 = static_cast<uint8_t>(std::lerp(end_x - treble_band_travel_width, end_x, treble_freq));
	const auto treble_x2 = static_cast<uint8_t>(treble_x1 - slope_width);
	const auto treble_y1 = static_cast<uint8_t>(std::lerp(end_y, end_y - height, treble));
	auto treble_y2 = center_y;

	// The treble band may push into the bass's territory (`eq_menu.h:62-68`).
	if (bass_x2 > treble_x2) {
		const auto diff = static_cast<uint8_t>(bass_x2 - treble_x2);
		bass_x2 = static_cast<uint8_t>(bass_x2 - diff);
		bass_x1 = static_cast<uint8_t>(bass_x1 - diff);
	}

	const auto center_between = [](uint8_t a, uint8_t b) {
		return static_cast<uint8_t>(std::min(a, b) + std::abs(b - a) / 2);
	};
	const float morph = 1.0f - static_cast<float>(treble_x2 - bass_x2) / 14.f;
	if (morph > 0.f) {
		const uint8_t target_y = center_between(bass_y1, treble_y1);
		bass_y2 = static_cast<uint8_t>(std::lerp(bass_y2, target_y, morph));
		treble_y2 = static_cast<uint8_t>(std::lerp(treble_y2, target_y, morph));
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
	for (auto y = static_cast<uint8_t>(start_y - 1); y <= end_y + 1; y += 4) {
		image.draw_pixel(bass_x2, y);
		image.draw_pixel(treble_x2, y);
	}

	Indicators marks{image};
	marks.draw(center_between(bass_x0, bass_x1), bass_y1, selected == 0);
	marks.draw(bass_x2, bass_y2, selected == 1);
	marks.draw(treble_x2, treble_y2, selected == 2);
	marks.draw(center_between(treble_x1, treble_x0), treble_y1, selected == 3);
}

void draw_column(OledFrame& slot_frame, bool /*focused*/, void* ctx) {
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
	}
}

} // namespace deluge::ui::oled
