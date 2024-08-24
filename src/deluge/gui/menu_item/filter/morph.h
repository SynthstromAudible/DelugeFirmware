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
#include "gui/menu_item/filter/filter_value.h"
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

using namespace deluge::dsp::filter;
namespace deluge::gui::menu_item::filter {

class FilterMorph final : public FilterValue {
public:
	FilterMorph(l10n::String newName, int32_t newP, bool hpf) : FilterValue{newName, newName, newP, hpf} {}
	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		auto filt = SpecificFilter(hpf ? soundEditor.currentModControllable->hpfMode
		                               : soundEditor.currentModControllable->lpfMode);
		return l10n::getView(filt.getMorphName());
	}
};

class UnpatchedFilterMorph final : public deluge::gui::menu_item::UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	UnpatchedFilterMorph(l10n::String newName, int32_t newP, bool hpf)
	    : UnpatchedParam{newName, newName, newP}, hpf{hpf} {}
	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		auto filt = SpecificFilter(hpf ? soundEditor.currentModControllable->hpfMode
		                               : soundEditor.currentModControllable->lpfMode);
		return l10n::getView(filt.getMorphName());
	}

private:
	bool hpf;
};
} // namespace deluge::gui::menu_item::filter
