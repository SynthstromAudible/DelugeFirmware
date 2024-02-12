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
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/ui/sound_editor.h"

using namespace deluge::dsp::filter;
namespace deluge::gui::menu_item::filter {

class FilterMorph final : public patched_param::IntegerNonFM {
public:
	using patched_param::IntegerNonFM::IntegerNonFM;
	FilterMorph(l10n::String newName, int32_t newP, bool hpf) : IntegerNonFM{newName, newP}, hpf{hpf} {}
	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		auto filt = SpecificFilter(hpf ? soundEditor.currentModControllable->hpfMode
		                               : soundEditor.currentModControllable->lpfMode);
		return l10n::getView(filt.getMorphName());
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

private:
	bool hpf;
};
} // namespace deluge::gui::menu_item::filter
