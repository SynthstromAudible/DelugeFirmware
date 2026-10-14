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
#include "gui/menu_item/selection.h"
#include "gui/menu_item/setting_edit.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include <string>
#include <utility>

namespace deluge::gui::menu_item::delay {

/// The delay's type: off, digital or analog, in DelayType's order.
class Type final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		this->setValue(std::to_underlying(soundEditor.currentModControllable->effects().delay.type));
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		const auto type = static_cast<deluge::effects::DelayType>(this->getValue());
		const deluge::serialization::Value word =
		    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(type)));

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					effectSettingFor(*soundDrum, "delay/type", word);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			effectSettingFor(*soundEditor.currentModControllable, "delay/type", word);
		}
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_OFF), l10n::getView(STRING_FOR_DIGITAL),
		        l10n::getView(optType == OptType::SHORT ? STRING_FOR_ANALOG_SHORT : STRING_FOR_ANALOG)};
	}
};

} // namespace deluge::gui::menu_item::delay
