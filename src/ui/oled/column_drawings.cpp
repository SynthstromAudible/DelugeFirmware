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
