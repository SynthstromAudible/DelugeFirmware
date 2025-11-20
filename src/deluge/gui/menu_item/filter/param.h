/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/filter/info.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/value_scaling.h"
#include "modulation/params/param.h"

#include <util/comparison.h>

namespace deluge::gui::menu_item::filter {

using namespace deluge::modulation;
using namespace hid::display;

class FilterParam : public patched_param::Integer {
public:
	FilterParam(l10n::String newName, int32_t newP, FilterSlot slot_, FilterParamType type_)
	    : Integer{newName, newP}, info{slot_, type_} {}
	FilterParam(l10n::String newName, l10n::String title, int32_t newP, FilterSlot slot_, FilterParamType type_)
	    : Integer{newName, title, newP}, info{slot_, type_} {}

	[[nodiscard]] std::string_view getName() const override { return info.getMorphNameOr(Integer::getName()); }
	[[nodiscard]] std::string_view getTitle() const override { return info.getMorphNameOr(Integer::getTitle()); }
	[[nodiscard]] bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return info.isOn();
	}
	[[nodiscard]] FilterInfo const& getFilterInfo() const { return info; }

	void getColumnLabel(StringBuf& label) override { label.append(info.getMorphNameOr(Integer::getName(), true)); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (info.getFilterParamType() == FilterParamType::MORPH && info.isMorphable()) {
			int32_t value = getValue();
			if (info.getSlot() == FilterSlot::HPF) {
				// Treat HPF as fully morphed LPF visually
				value = 50 - value;
			}
			drawSlider(slot, value);
		}
		else {
			drawBar(slot);
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL
		    && info.getFilterParamType() == FilterParamType::MORPH && info.isMorphable()
		    && info.getSlot() == FilterSlot::HPF) {
			// Treat HPF as fully morphed LPF visually, reverse direction
			offset *= -1;
		}
		Integer::selectEncoderAction(offset);
	}

private:
	FilterInfo info;
};

class UnpatchedFilterParam final : public UnpatchedParam {
public:
	UnpatchedFilterParam(l10n::String newName, l10n::String title, int32_t newP, FilterSlot slot_,
	                     FilterParamType type_)
	    : UnpatchedParam{newName, title, newP}, info{slot_, type_} {}

	[[nodiscard]] std::string_view getName() const override { return info.getMorphNameOr(UnpatchedParam::getName()); }
	[[nodiscard]] std::string_view getTitle() const override { return info.getMorphNameOr(UnpatchedParam::getTitle()); }
	[[nodiscard]] bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return info.isOn();
	}
	void getColumnLabel(StringBuf& label) override { label.append(info.getMorphNameOr(Integer::getName(), true)); }
	[[nodiscard]] FilterInfo const& getFilterInfo() const { return info; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (info.getFilterParamType() == FilterParamType::MORPH && info.isMorphable()) {
			int32_t value = getValue();
			if (info.getSlot() == FilterSlot::HPF) {
				// Treat HPF as fully morphed LPF visually
				value = 50 - value;
			}
			drawSlider(slot, value);
		}
		else {
			drawBar(slot);
		}
	}
	void selectEncoderAction(int32_t offset) override {
		if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL
		    && info.getFilterParamType() == FilterParamType::MORPH && info.isMorphable()
		    && info.getSlot() == FilterSlot::HPF) {
			// Treat HPF as fully morphed LPF visually, reverse direction
			offset *= -1;
		}
		Integer::selectEncoderAction(offset);
	}

private:
	FilterInfo info;
};

} // namespace deluge::gui::menu_item::filter
