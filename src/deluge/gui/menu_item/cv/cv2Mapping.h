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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/cv_instrument.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::cv {

class DualCVSelection final : public menu_item::Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {

		auto currentSettings = ((CVInstrument*)getCurrentOutput())->getCV2Mode();
		int index = static_cast<int>(currentSettings);
		if (index != 0) {
			index -= 1;
		}
		this->setValue(index);
	}
	void writeCurrentValue() override {
		auto current_value = this->getValue();
		if (current_value != 0) {
			current_value += 1; // skip over pitch until duophony works
		}
		auto newSetting = static_cast<CVMode>(current_value);

		((CVInstrument*)getCurrentOutput())->setCV2Mode(newSetting);
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		static auto o = l10n::getView(STRING_FOR_OFF);
		static auto y = l10n::getView(STRING_FOR_PATCH_SOURCE_Y);
		static auto a = l10n::getView(STRING_FOR_PATCH_SOURCE_AFTERTOUCH);
		static auto v = l10n::getView(STRING_FOR_VELOCITY);

		return {o, y, a, v};
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// not relevant for cv
		const auto type = getCurrentOutputType();
		return (type == OutputType::CV && ((CVInstrument*)getCurrentOutput())->getChannel() == both);
	}
	//	MenuItem* selectButtonPress() override {
	//		return Selection::selectButtonPress();
	//	}
};

} // namespace deluge::gui::menu_item::cv
