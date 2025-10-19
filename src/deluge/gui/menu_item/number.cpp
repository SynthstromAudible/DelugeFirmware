/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "number.h"
#include "hid/display/oled.h"
#include "trigger/out/ppqn.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

using namespace hid::display;

float Number::getNormalizedValue() {
	const int32_t min_value = getMinValue();
	const int32_t max_value = getMaxValue();
	return static_cast<float>(getValue() - min_value) / static_cast<float>(max_value - min_value);
}

void Number::drawHorizontalBar(int32_t y_top, int32_t margin_l, int32_t margin_r, int32_t height) {
	oled_canvas::Canvas& canvas = OLED::main;
	if (margin_r == -1) {
		margin_r = margin_l;
	}
	const int32_t left_most = margin_l + 2;
	const int32_t right_most = OLED_MAIN_WIDTH_PIXELS - margin_r - 3;

	const int32_t value = getValue();
	const int32_t min_value = getMinValue();
	if (min_value == 0 && value == 0) {
		return canvas.drawRectangleRounded(left_most, y_top, right_most - 1, y_top + height);
	}

	const int32_t max_value = getMaxValue();
	const uint32_t range = max_value - min_value;
	const float pos_fractional = static_cast<float>(value - min_value) / range;
	const float zero_pos_fractional = static_cast<float>(-min_value) / range;

	const int32_t width = right_most - left_most;
	const int32_t pos_horizontal = static_cast<int32_t>(pos_fractional * width);

	if (const int32_t zero_pos_horizontal = static_cast<int32_t>(zero_pos_fractional * width);
	    pos_horizontal == zero_pos_horizontal) {
		// = 0, draw vertical line in the middle
		const int32_t xMin = left_most + zero_pos_horizontal;
		canvas.invertArea(xMin, 1, y_top + 1, y_top + height - 1);
	}
	else if (pos_horizontal < zero_pos_horizontal) {
		// < 0, draw the bar to the left
		const int32_t x_min = left_most + pos_horizontal;
		canvas.invertArea(x_min, zero_pos_horizontal - pos_horizontal, y_top + 1, y_top + height - 1);
	}
	else {
		// > 0, draw the bar to the right
		const int32_t x_min = left_most + zero_pos_horizontal;
		canvas.invertArea(x_min, pos_horizontal - zero_pos_horizontal, y_top + 1, y_top + height - 1);
	}

	canvas.drawRectangleRounded(left_most, y_top, right_most - 1, y_top + height);
}

void Number::renderInHorizontalMenu(int32_t start_x, int32_t width, int32_t start_y, int32_t height) {
	switch (getRenderingStyle()) {
	case PERCENT:
		return drawPercent(start_x, start_y, width, height);
	case KNOB:
		return drawKnob(start_x, start_y, width, height);
	case BAR:
		return drawBar(start_x, start_y, width, height);
	case SLIDER:
		return drawSlider(start_x, start_y, width, height);
	case LENGTH_SLIDER:
		return drawLengthSlider(start_x, start_y, width, height);
	case PAN:
		return drawPan(start_x, start_y, width, height);
	case HPF:
		return drawHpf(start_x, start_y, width, height);
	case LPF:
		return drawLpf(start_x, start_y, width, height);
	case ATTACK:
		return drawAttack(start_x, start_y, width, height);
	case RELEASE:
		return drawRelease(start_x, start_y, width, height);
	default:
		DEF_STACK_STRING_BUF(paramValue, 10);
		paramValue.appendInt(getValue());
		return OLED::main.drawStringCentered(paramValue, start_x, start_y + kHorizontalMenuSlotYOffset,
		                                     kTextTitleSpacingX, kTextTitleSizeY, width);
	}
}

void Number::drawPercent(int32_t start_x, int32_t start_y, int32_t width, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;

	DEF_STACK_STRING_BUF(valueString, 12);
	valueString.appendInt(getValue() * 2);

	constexpr char percent_char = '%';
	const int32_t value_width = image.getStringWidthInPixels(valueString.c_str(), kTextSpacingY);
	const int32_t percent_char_width = image.getCharWidthInPixels(percent_char, kTextSpacingY);
	constexpr int32_t padding_between = 2;
	const int32_t total_width = value_width + percent_char_width + padding_between;

	int32_t x = start_x + (width - total_width) / 2 + 1;
	int32_t y = start_y + kHorizontalMenuSlotYOffset;
	image.drawString(valueString.c_str(), x, y, kTextSpacingX, kTextSpacingY);

	x += value_width + padding_between;
	image.drawChar(percent_char, x, y, kTextSpacingX, kTextSpacingY);
}

void Number::drawKnob(int32_t start_x, int32_t start_y, int32_t width, int32_t height) {
	oled_canvas::Canvas& image = OLED::main;

	// Draw the background arc
	// Easier to adjust pixel-perfect, so we use a bitmap
	image.drawIconCentered(OLED::knobArcIcon, start_x, width, start_y - 1);

	// Calculate current value angle
	constexpr int32_t knob_radius = 10;
	const int32_t center_x = start_x + width / 2;
	const int32_t center_y = start_y + knob_radius;
	constexpr int32_t arc_range_angle = 210, beginning_angle = 165;
	const float value_percent = getNormalizedValue();
	const float current_angle = beginning_angle + (arc_range_angle * value_percent);

	// Draw the indicator line
	const float radians = current_angle * M_PI / 180.0f;
	constexpr float inner_radius = knob_radius - 1.0f;
	constexpr float outer_radius = 3.5f;
	const float cos_a = cos(radians);
	const float sin_a = sin(radians);
	const float line_start_x = center_x + outer_radius * cos_a;
	const float line_start_y = center_y + outer_radius * sin_a;
	const float line_end_x = center_x + inner_radius * cos_a;
	const float line_end_y = center_y + inner_radius * sin_a;
	image.drawLine(round(line_start_x), round(line_start_y), round(line_end_x), round(line_end_y), {.thick = true});
}

void Number::drawBar(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t bar_width = 15;
	constexpr uint8_t bar_height = 13;

	const uint8_t bar_start_x = start_x + (slot_width - bar_width) / 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_start_y = start_y + kHorizontalMenuSlotYOffset - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;

	const float value = getNormalizedValue();
	const float fill_height_f = value * bar_height;
	const float fill_height = static_cast<uint8_t>(fill_height_f);
	const uint8_t fill_y = bar_end_y - fill_height + 1;

	// Draw indicators
	constexpr uint8_t indicators_offset = 3;
	const uint8_t indicators_start_x = bar_start_x - indicators_offset;
	const uint8_t indicators_end_x = bar_end_x + indicators_offset;
	for (uint8_t y = bar_start_y; y <= bar_end_y; y += 4) {
		image.drawPixel(indicators_start_x, y);
		image.drawPixel(indicators_end_x, y);
	}

	// Draw bar
	if (fill_y <= bar_end_y) {
		image.invertArea(bar_start_x, bar_width, fill_y, bar_end_y);
	}
	else {
		for (uint8_t x = bar_start_x + 1; x <= bar_end_x; x += 4) {
			image.drawPixel(x, bar_end_y);
		}
	}

	// Apply dithering
	constexpr uint8_t dithering_segments = 3;
	constexpr uint8_t segment_width = bar_width / dithering_segments;
	const float dithering_amount = fill_height_f - fill_height;
	const uint8_t dithering_width = static_cast<uint8_t>(dithering_amount * segment_width);
	if (dithering_width > 0) {
		for (uint8_t segment = 0; segment < dithering_segments; segment++) {
			const uint8_t start_x = bar_start_x + segment_width * segment;
			uint8_t end_x = start_x + dithering_width;
			if (end_x == bar_end_x - 1) {
				end_x = bar_end_x;
			}
			image.drawHorizontalLine(fill_y - 1, start_x, end_x);
		}
	}
}

void Number::drawSlider(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = start_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = start_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const float value_normalized = getNormalizedValue();
	const int32_t value_line_min_x = min_x;
	const int32_t value_line_width = static_cast<int32_t>(value_normalized * (max_x - 1 - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t x = min_x; x <= max_x; x += 3) {
		if (x != value_line_x - 1 && x != value_line_x + 2) {
			image.drawPixel(x, center_y - 1);
			image.drawPixel(x, center_y + 1);
		}
	}

	image.drawVerticalLine(value_line_x, min_y, max_y);
	image.drawVerticalLine(value_line_x + 1, min_y, max_y);
}

void Number::drawLengthSlider(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height,
                              bool min_slider_pos) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = start_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = start_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const float value_normalized = getNormalizedValue();
	const int32_t value_line_min_x = min_x + min_slider_pos;
	const int32_t value_line_width = static_cast<int32_t>(value_normalized * (max_x - value_line_min_x));
	const int32_t value_line_x = value_line_min_x + value_line_width;

	const int32_t center_y = min_y + (max_y - min_y) / 2;
	for (int32_t y = center_y - 1; y <= center_y + 1; y++) {
		image.drawHorizontalLine(y, min_x, value_line_x);
	}
	image.drawVerticalLine(value_line_x, min_y, max_y);

	for (int32_t x = max_x; x >= value_line_min_x; x -= 3) {
		image.drawPixel(x, center_y - 1);
		image.drawPixel(x, center_y + 1);
	}
}

void Number::drawPan(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t width = 23;
	constexpr uint8_t circle_radius = 5;
	const uint8_t pan_start_x = start_x + 4;
	const uint8_t pan_end_x = pan_start_x + width - 1;
	const uint8_t center_x = start_x + slot_width / 2;
	const uint8_t center_y = start_y + kHorizontalMenuSlotYOffset + 4;

	const int32_t value = getValue();
	const int8_t direction = (value > 0) - (value < 0); // -1, 0, or +1
	const float norm = std::abs(value) / 25.f;

	// Draw the main circle
	const uint8_t circle_x = center_x + direction * norm * (width / 2);
	image.drawCircle(circle_x, center_y, circle_radius, pan_start_x + 1, pan_end_x - 1);
	image.drawCircle(circle_x, center_y, circle_radius - 1, pan_start_x + 1, pan_end_x - 1);

	if (norm == 0.f) {
		// Fill inside if we're at center position
		constexpr uint8_t fill_offset = 3;
		image.clearAreaExact(center_x - fill_offset, center_y - fill_offset, center_x + fill_offset,
		                     center_y + fill_offset);
		image.invertArea(center_x - fill_offset, fill_offset * 2 + 1, center_y - fill_offset, center_y + fill_offset);
		image.clearCircle(circle_x, center_y, circle_radius - 2, pan_start_x + 1, pan_end_x - 1);
	}

	// Fill remaining gaps
	constexpr uint8_t gaps_offset = 3;
	for (const uint8_t y : {center_y + gaps_offset, center_y - gaps_offset}) {
		for (const uint8_t x : {circle_x + gaps_offset, circle_x - gaps_offset}) {
			if (pan_start_x < x && x < pan_end_x) {
				image.drawPixel(x, y);
			}
		}
	}

	// Draw side dashed lines
	if (circle_x - circle_radius < pan_start_x) {
		for (uint8_t y = center_y - circle_radius; y <= center_y + circle_radius; y++) {
			image.clearPixel(pan_start_x + 1, y);
			if (y % 2 == 0) {
				image.drawPixel(pan_start_x, y);
			}
		}
	}
	if (circle_x + circle_radius > pan_end_x) {
		for (uint8_t y = center_y - circle_radius; y <= center_y + circle_radius; y++) {
			image.clearPixel(pan_end_x - 1, y);
			if (y % 2 == 0) {
				image.drawPixel(pan_end_x, y);
			}
		}
	}
	// Draw a vertical dashed line at center
	for (uint8_t x = pan_start_x; x <= pan_end_x; x += 2) {
		if (x <= circle_x - circle_radius || x >= circle_x + circle_radius) {
			image.drawPixel(x, center_y);
		}
	}
}

void Number::drawHpf(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t hpf_start_x = start_x + 5;
	const uint8_t hpf_end_x = hpf_start_x + width - 1;
	const uint8_t hpf_start_y = start_y + 1;
	const uint8_t hpf_end_y = hpf_start_y + height - 1;

	const float norm = getNormalizedValue();
	const uint8_t slope_start_x = std::lerp(hpf_start_x, hpf_end_x - slope_width - 4, norm);
	const uint8_t slope_end_x = slope_start_x + slope_width;

	image.drawLine(slope_start_x, hpf_end_y, slope_end_x, hpf_start_y, {.thick = true});
	image.drawHorizontalLine(hpf_start_y, slope_end_x, hpf_end_x);
	image.drawHorizontalLine(hpf_start_y + 1, slope_end_x, hpf_end_x);

	for (uint8_t x = hpf_start_x; x < slope_start_x; x += 3) {
		image.drawPixel(x, hpf_start_y);
	}
	if (slope_start_x != hpf_start_x) {
		for (uint8_t y = hpf_start_y; y < hpf_end_y; y += 3) {
			image.drawPixel(hpf_start_x, y);
		}
	}
}

void Number::drawLpf(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t lpf_start_x = start_x + 5;
	const uint8_t lpf_end_x = lpf_start_x + width - 1;
	const uint8_t lpf_start_y = start_y + 1;
	const uint8_t lpf_end_y = lpf_start_y + height - 1;

	const float norm = getNormalizedValue();
	const uint8_t slope_start_x = std::lerp(lpf_start_x + 3, lpf_end_x - slope_width, norm);
	const uint8_t slope_end_x = slope_start_x + slope_width;

	image.drawLine(slope_start_x, lpf_start_y, slope_end_x, lpf_end_y, {.thick = true});
	image.drawHorizontalLine(lpf_start_y, lpf_start_x, slope_start_x);
	image.drawHorizontalLine(lpf_start_y + 1, lpf_start_x, slope_start_x);

	for (uint8_t x = lpf_end_x; x > slope_end_x; x -= 3) {
		image.drawPixel(x, lpf_start_y);
	}
	if (slope_end_x != lpf_end_x) {
		for (uint8_t y = lpf_start_y; y < lpf_end_y; y += 3) {
			image.drawPixel(lpf_end_x, y);
		}
	}
}

void Number::drawRelease(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t width = 20;
	constexpr uint8_t height = 11;

	const uint8_t rel_start_x = start_x + 5;
	const uint8_t rel_end_x = rel_start_x + width - 1;
	const uint8_t rel_start_y = start_y + kHorizontalMenuSlotYOffset - 1;
	const uint8_t rel_end_y = rel_start_y + height - 1;

	const float norm = getNormalizedValue();
	const uint8_t rel_stage_start_x = rel_start_x + 4;
	const uint8_t rel_effective_x = std::lerp(rel_stage_start_x, rel_end_x, norm);

	image.drawHorizontalLine(rel_start_y, rel_start_x, rel_stage_start_x);
	image.drawLine(rel_stage_start_x, rel_start_y, rel_effective_x, rel_end_y);

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = rel_effective_x - indicator_offset; x <= rel_effective_x + indicator_offset; x++) {
		image.clearPixel(x, rel_end_y - indicator_offset - 1);
		image.clearPixel(x, rel_end_y + indicator_offset + 1);

		for (uint8_t y = rel_end_y - indicator_offset; y <= rel_end_y + indicator_offset; y++) {
			image.drawPixel(x, y);
		}
	}

	for (uint8_t x = rel_end_x; x > rel_effective_x + 1; x -= 2) {
		image.drawPixel(x, rel_end_y);
	}
}

void Number::drawAttack(int32_t start_x, int32_t start_y, int32_t slot_width, int32_t slot_height) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t width = 20;
	constexpr uint8_t height = 11;

	const uint8_t atk_start_x = start_x + 6;
	const uint8_t atk_end_x = atk_start_x + width - 1;
	const uint8_t atk_start_y = start_y + kHorizontalMenuSlotYOffset;
	const uint8_t atk_end_y = atk_start_y + height - 1;

	const float norm = getNormalizedValue();
	const uint8_t atk_effective_x = std::lerp(atk_start_x, atk_end_x - 3, norm);

	image.drawLine(atk_start_x, atk_end_y, atk_effective_x, atk_start_y, {.thick = false});

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = atk_effective_x - indicator_offset; x <= atk_effective_x + indicator_offset; x++) {
		image.clearPixel(x, atk_start_y - indicator_offset - 1);
		image.clearPixel(x, atk_start_y + indicator_offset + 1);

		for (uint8_t y = atk_start_y - indicator_offset; y <= atk_start_y + indicator_offset; y++) {
			image.drawPixel(x, y);
		}
	}

	for (uint8_t x = atk_end_x; x > atk_effective_x + 1; x -= 2) {
		image.drawPixel(x, atk_start_y);
	}
}

void Number::getNotificationValue(StringBuf& value) {
	value.appendInt(getRenderingStyle() == PERCENT ? getValue() * 2 : getValue());
}

} // namespace deluge::gui::menu_item
