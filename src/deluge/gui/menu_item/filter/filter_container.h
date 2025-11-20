/*
 * Copyright (c) 2025 Leonid Burygin
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

#pragma once

#include "gui/menu_item/horizontal_menu_container.h"
#include "hid/led/indicator_leds.h"
#include "param.h"

namespace deluge::gui::menu_item::filter {

using namespace deluge::hid::display;

class FilterContainer final : public HorizontalMenuContainer {
public:
	FilterContainer(std::initializer_list<MenuItem*> items, FilterParam* morph_item)
	    : HorizontalMenuContainer(items), morph_item_{morph_item} {}
	FilterContainer(std::initializer_list<MenuItem*> items, UnpatchedFilterParam* morph_item)
	    : HorizontalMenuContainer(items), morph_item_unpatched_{morph_item} {}

	void render(const SlotPosition& slots, const MenuItem* selected_item, HorizontalMenu* parent,
	            bool* halt_remaining_rendering) override {
		oled_canvas::Canvas& image = OLED::main;

		const auto [freq_raw, reso_raw, morph_raw, is_morphable, is_hpf] = getFilterValues();
		const float freq_value = freq_raw / 50.f;
		const float reso_value = sigmoidLikeCurve(reso_raw, 50.f, 15.f);
		const float morph_value = [&] {
			float result = 0.f;
			if (is_morphable) {
				result = morph_raw / 50.f;
			}
			if (is_hpf) {
				result = 1.0f - result; // treat HPF as fully morphed LPF visually
			}
			return result;
		}();

		constexpr uint8_t reso_segment_width = 6;
		constexpr uint8_t freq_slope_width = 5;
		constexpr uint8_t padding_x = 4;
		const uint8_t total_width = slots.width - 2 - padding_x * 2;
		const uint8_t base_width = total_width - freq_slope_width - reso_segment_width;

		uint8_t min_x = slots.start_x + padding_x;
		uint8_t max_x = min_x + total_width;
		uint8_t reso_x0 = min_x - reso_segment_width + base_width * freq_value;
		uint8_t reso_x1 = reso_x0 + reso_segment_width;
		uint8_t reso_x2 = reso_x1 + reso_segment_width;
		int8_t slope0_x0 = reso_x0 - base_width - freq_slope_width;
		int8_t slope0_x1 = slope0_x0 + freq_slope_width;
		uint8_t slope1_x0 = reso_x2;
		uint8_t slope1_x1 = slope1_x0 + freq_slope_width;

		if (morph_value > 0) {
			const uint8_t slope_shift = std::lerp(0, total_width, morph_value);
			const uint8_t reso_shift = std::lerp(0, freq_slope_width + reso_segment_width, morph_value);
			slope0_x0 += slope_shift;
			slope0_x1 += slope_shift;
			slope1_x0 += slope_shift;
			slope1_x1 += slope_shift;
			reso_x0 += reso_shift;
			reso_x1 += reso_shift;
			reso_x2 += reso_shift;
		}

		constexpr uint8_t container_height = 23;
		const uint8_t start_y = slots.start_y + 1;
		const uint8_t end_y = start_y + container_height - 1;
		const uint8_t center_y = start_y + (end_y - start_y) / 2;
		const uint8_t reso_y = std::lerp(center_y, start_y, reso_value);

		image.drawRectangleRounded(min_x - 1, start_y - 1, max_x + 1, end_y + 1);

		auto draw_segment = [&](int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
			oled_canvas::Point last_point{-1, -1};
			auto draw_fill_pattern = [&](oled_canvas::Point point) {
				if (last_point.x != point.x && point.x % 3 == 0) {
					for (int32_t y = point.y; y <= end_y; y++) {
						if (y % 3 == 1) {
							image.drawPixel(point.x, y);
						}
					}
				}
				last_point = point;
			};
			image.drawLine(x0, y0, x1, y1, {.min_x = min_x, .max_x = max_x, .point_callback = draw_fill_pattern});
			return last_point;
		};

		// Slope 0
		auto slope0_last_point = slope0_x1 <= min_x ? oled_canvas::Point{min_x, center_y}
		                                            : draw_segment(slope0_x0, end_y, slope0_x1, center_y);

		// Body
		draw_segment(slope0_x1, center_y, reso_x0, center_y);

		// Resonance
		draw_segment(reso_x0, center_y, reso_x1, reso_y);
		draw_segment(reso_x1, reso_y, reso_x2, center_y);
		draw_segment(reso_x2, center_y, slope1_x0, center_y);

		// Slope 1
		auto slope1_last_point = slope1_x0 >= max_x ? oled_canvas::Point{max_x, center_y}
		                                            : draw_segment(slope1_x0, center_y, slope1_x1, end_y);

		auto freq_point = pickFreqPoint(slope0_last_point, slope1_last_point, min_x, max_x);
		syncFreqAndResonancePositionWithLEDs(freq_point == slope1_last_point, selected_item, parent,
		                                     halt_remaining_rendering);
	};

private:
	FilterParam* morph_item_{nullptr};
	UnpatchedFilterParam* morph_item_unpatched_{nullptr};

	struct FilterValues {
		int32_t freq_value{0};
		int32_t reso_value{0};
		int32_t morph_value{0};
		bool is_morphable{false};
		bool is_hpf{false};
	};

	[[nodiscard]] FilterValues getFilterValues() const {
		if (morph_item_ != nullptr) {
			// Get from patched params
			auto freq_item = static_cast<FilterParam*>(items_[0]);
			auto reso_item = static_cast<FilterParam*>(items_[1]);
			bool is_morphable = morph_item_->getFilterInfo().isMorphable();
			bool is_hpf = freq_item->getP() == params::LOCAL_HPF_FREQ;
			return {freq_item->getValue(), reso_item->getValue(), morph_item_->getValue(), is_morphable, is_hpf};
		}

		// Get from unpatched params
		auto freq_item = static_cast<UnpatchedFilterParam*>(items_[0]);
		auto reso_item = static_cast<UnpatchedFilterParam*>(items_[1]);
		bool is_morphable = morph_item_unpatched_->getFilterInfo().isMorphable();
		bool is_hpf = freq_item->getP() == params::UNPATCHED_HPF_FREQ;
		return {freq_item->getValue(), reso_item->getValue(), morph_item_unpatched_->getValue(), is_morphable, is_hpf};
	}

	static oled_canvas::Point pickFreqPoint(const oled_canvas::Point& slope0_last_point,
	                                        const oled_canvas::Point& slope1_last_point, uint8_t min_x, uint8_t max_x) {
		if (slope0_last_point.x > min_x && slope0_last_point.x < max_x) {
			return slope0_last_point;
		}
		if (slope1_last_point.x > min_x && slope1_last_point.x < max_x) {
			return slope1_last_point;
		}
		return slope0_last_point.y > slope1_last_point.y ? slope0_last_point : slope1_last_point;
	}

	void syncFreqAndResonancePositionWithLEDs(bool freq_is_on_right_side, const MenuItem* selected_item,
	                                          HorizontalMenu* parent, bool* halt_remaining_rendering) const {
		MenuItem* freq = items_[0];
		MenuItem* reso = items_[1];

		uint8_t freq_index = 1, reso_index = 2;
		if (freq_is_on_right_side) {
			std::swap(freq_index, reso_index);
		}

		auto& parent_items = parent->getItems();
		if (parent_items[freq_index] != freq) {
			parent_items[freq_index] = freq;
			parent_items[reso_index] = reso;

			// We can be inside a horizontal menu group or a plain horizontal menu
			auto host_menu = parent->parent != nullptr ? parent->parent : parent;

			// Reset the current item iterator, it points to incorrect item now after items order was changed
			host_menu->setCurrentItem(selected_item);

			// Should re-render the whole menu to apply new items order
			OLED::clearMainImage();
			host_menu->renderOLED();
			*halt_remaining_rendering = true;
		}
	}
};
} // namespace deluge::gui::menu_item::filter