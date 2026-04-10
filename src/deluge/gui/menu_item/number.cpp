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

float Number::normalize(int32_t value) {
	const int32_t min_value = getMinValue();
	const int32_t max_value = getMaxValue();
	return static_cast<float>(value - min_value) / static_cast<float>(max_value - min_value);
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

void Number::renderInHorizontalMenu(const SlotPosition& slot) {
	switch (getRenderingStyle()) {
	case PERCENT:
		return drawPercent(slot);
	case KNOB:
		return drawKnob(slot);
	case BAR:
		return drawBar(slot);
	case SLIDER:
		return drawSlider(slot);
	case LENGTH_SLIDER:
		return drawLengthSlider(slot);
	case PAN:
		return drawPan(slot);
	case HPF:
		return drawHpf(slot);
	case LPF:
		return drawLpf(slot);
	case ATTACK:
		return drawAttack(slot);
	case RELEASE:
		return drawRelease(slot);
	case SIDECHAIN_DUCKING:
		return drawSidechainDucking(slot);
	default:
		DEF_STACK_STRING_BUF(paramValue, 10);
		paramValue.appendInt(getValue());
		return OLED::main.drawStringCentered(paramValue, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                                     kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}
}

void Number::drawPercent(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	DEF_STACK_STRING_BUF(valueString, 12);
	valueString.appendInt(getValue() * 2);

	constexpr char percent_char = '%';
	const int32_t value_width = image.getStringWidthInPixels(valueString.c_str(), kTextSpacingY);
	const int32_t percent_char_width = image.getCharWidthInPixels(percent_char, kTextSpacingY);
	constexpr int32_t padding_between = 2;
	const int32_t total_width = value_width + percent_char_width + padding_between;

	int32_t x = slot.start_x + (slot.width - total_width) / 2 + 1;
	int32_t y = slot.start_y + kHorizontalMenuSlotYOffset;
	image.drawString(valueString.c_str(), x, y, kTextSpacingX, kTextSpacingY);

	x += value_width + padding_between;
	image.drawChar(percent_char, x, y, kTextSpacingX, kTextSpacingY);
}

void Number::drawKnob(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	// Draw the background arc
	// Easier to adjust pixel-perfect, so we use a bitmap
	image.drawIconCentered(OLED::knobArcIcon, slot.start_x, slot.width, slot.start_y - 1);

	// Calculate current value angle
	constexpr uint8_t knob_radius = 10;
	const uint8_t center_x = slot.start_x + slot.width / 2;
	const uint8_t center_y = slot.start_y + knob_radius;
	constexpr uint8_t arc_range_angle = 210;
	constexpr uint8_t beginning_angle = 165;
	const float value_percent = normalize(getValue());
	const float current_angle = beginning_angle + (arc_range_angle * value_percent);

	// Draw the leading line
	const float radians = current_angle * M_PI / 180.0f;
	constexpr float outer_radius = knob_radius - 1.0f;
	constexpr float inner_radius = 3.5f;
	const float cos_a = cos(radians);
	const float sin_a = sin(radians);
	const float line_start_x = center_x + inner_radius * cos_a;
	const float line_start_y = center_y + inner_radius * sin_a;
	const float line_end_x = center_x + outer_radius * cos_a;
	const float line_end_y = center_y + outer_radius * sin_a;
	image.drawLine(round(line_start_x), round(line_start_y), round(line_end_x), round(line_end_y), {.thick = true});
}

void Number::drawBar(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = slot.start_x + 5;
	const uint8_t bar_start_y = slot.start_y + kHorizontalMenuSlotYOffset + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;

	image.drawRectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                    bar_end_y + outline_padding);

	const float norm = normalize(getValue());
	const uint8_t fill_width = norm * bar_width;
	image.invertArea(bar_start_x, fill_width, bar_start_y, bar_end_y);
}

void Number::drawSlider(const SlotPosition& slot, std::optional<int32_t> value) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot.start_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot.start_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const float norm = normalize(value.value_or(getValue()));
	const int32_t value_line_min_x = min_x;
	const int32_t value_line_width = static_cast<int32_t>(norm * (max_x - 1 - value_line_min_x));
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

void Number::drawLengthSlider(const SlotPosition& slot, bool min_slider_pos) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t slider_width = 23;
	constexpr int32_t slider_height = 11;
	const int32_t min_x = slot.start_x + 4;
	const int32_t max_x = min_x + slider_width - 1;
	const int32_t min_y = slot.start_y + 1;
	const int32_t max_y = min_y + slider_height - 1;

	const float norm = normalize(getValue());
	const int32_t value_line_min_x = min_x + min_slider_pos;
	const int32_t value_line_width = static_cast<int32_t>(norm * (max_x - value_line_min_x));
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

void Number::drawPan(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t bar_width = 21;
	constexpr uint8_t bar_height = 5;
	constexpr uint8_t outline_padding = 2;
	const uint8_t bar_start_x = slot.start_x + 5;
	const uint8_t bar_start_y = slot.start_y + kHorizontalMenuSlotYOffset + 2;
	const uint8_t bar_end_x = bar_start_x + bar_width - 1;
	const uint8_t bar_end_y = bar_start_y + bar_height - 1;
	const uint8_t bar_center_x = bar_start_x + bar_width / 2;

	// Outline
	image.drawRectangle(bar_start_x - outline_padding, bar_start_y - outline_padding, bar_end_x + outline_padding,
	                    bar_end_y + outline_padding);

	// The top and the bottom notches
	for (const auto offset : {1, 2}) {
		image.drawPixel(bar_center_x, bar_end_y + outline_padding + offset);
		image.drawPixel(bar_center_x, bar_start_y - outline_padding - offset);
	}

	// Midpoint
	image.drawVerticalLine(bar_center_x, bar_start_y, bar_end_y);

	const int32_t value = getValue();
	const int8_t direction = (value > 0) - (value < 0);

	// Adjust the value a bit to make a perfect zero indication
	uint8_t abs_value = std::abs(value);
	if (abs_value > 0 && abs_value < 3) {
		abs_value = 3;
	}

	// Fill
	const uint8_t fill_width = abs_value / 25.f * (bar_width / 2);
	if (fill_width > 0) {
		const uint8_t fill_start_x = direction >= 0 ? bar_center_x + 1 : bar_center_x - fill_width - 1;
		image.invertArea(fill_start_x, fill_width + 1, bar_start_y, bar_end_y);
	}
}

void Number::drawHpf(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t hpf_start_x = slot.start_x + 5;
	const uint8_t hpf_end_x = hpf_start_x + width - 1;
	const uint8_t hpf_start_y = slot.start_y + 1;
	const uint8_t hpf_end_y = hpf_start_y + height - 1;

	const float norm = normalize(getValue());
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

void Number::drawLpf(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t slope_width = 5;
	constexpr uint8_t width = 21;
	constexpr uint8_t height = 11;

	const uint8_t lpf_start_x = slot.start_x + 5;
	const uint8_t lpf_end_x = lpf_start_x + width - 1;
	const uint8_t lpf_start_y = slot.start_y + 1;
	const uint8_t lpf_end_y = lpf_start_y + height - 1;

	const float norm = normalize(getValue());
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

void Number::drawRelease(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t width = 19;
	constexpr uint8_t height = 11;

	const uint8_t rel_start_x = slot.start_x + 5;
	const uint8_t rel_end_x = rel_start_x + width - 1;
	const uint8_t rel_start_y = slot.start_y + kHorizontalMenuSlotYOffset - 1;
	const uint8_t rel_end_y = rel_start_y + height - 1;

	const float norm = normalize(getValue());
	const uint8_t rel_stage_start_x = rel_start_x + 4;
	const uint8_t rel_effective_x = std::lerp(rel_stage_start_x, rel_end_x, norm);

	image.drawHorizontalLine(rel_start_y, rel_start_x, rel_stage_start_x);
	image.drawLine(rel_stage_start_x, rel_start_y, rel_effective_x, rel_end_y);

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = rel_effective_x - indicator_offset; x <= rel_effective_x + indicator_offset; x++) {
		for (uint8_t y = rel_end_y - indicator_offset; y <= rel_end_y + indicator_offset - 1; y++) {
			image.drawPixel(x, y);
		}
	}

	for (uint8_t x = rel_end_x; x > rel_effective_x + 1; x -= 2) {
		image.drawPixel(x, rel_end_y);
	}
}

void Number::drawAttack(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr uint8_t width = 19;
	constexpr uint8_t height = 11;

	const uint8_t atk_start_x = slot.start_x + 7;
	const uint8_t atk_end_x = atk_start_x + width - 1;
	const uint8_t atk_start_y = slot.start_y + kHorizontalMenuSlotYOffset;
	const uint8_t atk_end_y = atk_start_y + height - 1;

	const float norm = normalize(getValue());
	const uint8_t atk_effective_x = std::lerp(atk_start_x, atk_end_x - 2, norm);

	image.drawLine(atk_start_x, atk_end_y, atk_effective_x, atk_start_y, {.thick = false});

	constexpr uint8_t indicator_offset = 2;
	for (uint8_t x = atk_effective_x - indicator_offset; x <= atk_effective_x + indicator_offset; x++) {
		for (uint8_t y = atk_start_y - indicator_offset + 1; y <= atk_start_y + indicator_offset; y++) {
			image.drawPixel(x, y);
		}
	}

	for (uint8_t x = atk_end_x; x > atk_effective_x + 1; x -= 2) {
		image.drawPixel(x, atk_start_y);
	}
}

void Number::drawSidechainDucking(const SlotPosition& slot) {
	oled_canvas::Canvas& image = OLED::main;

	constexpr int32_t width = 23;
	constexpr int32_t height = 11;

	const int32_t left_padding = (slot.width - width) / 2;
	const int32_t min_x = slot.start_x + left_padding;
	const int32_t max_x = min_x + width;
	const int32_t min_y = slot.start_y + kHorizontalMenuSlotYOffset - 1;
	const int32_t max_y = min_y + height - 1;

	// Calculate shape height based on value
	const float norm = getMaxValue() > 50 ? std::abs(getValue()) / 5000.0f : getValue() / 50.0f;
	const int32_t fill_height = norm * height;
	const int32_t y_offset = (height - fill_height) / 2;

	int32_t ducking_start_y, ducking_end_y, y0, y1;
	if (getValue() >= 0) {
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
	image.drawLine(min_x, y0, max_x - offset_right, y1);
	image.drawLine(min_x, y0, min_x, y1);
	image.drawHorizontalLine(y1, max_x - offset_right, max_x);
}

void Number::getNotificationValue(StringBuf& value) {
	value.appendInt(getRenderingStyle() == PERCENT ? getValue() * 2 : getValue());
}

} // namespace deluge::gui::menu_item
