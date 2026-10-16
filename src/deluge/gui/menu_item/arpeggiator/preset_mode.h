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
#include "definitions_cxx.hpp"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

#include "model/edits/arp_setting_for.h"
#include <hid/display/oled.h>
#include <numeric>

namespace deluge::gui::menu_item::arpeggiator {
class PresetMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->preset); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		const deluge::serialization::Value value = deluge::edits::arpChoice("arpeggiator/preset", this->getValue());

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			for (Drum* thisDrum = getCurrentKit()->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				deluge::edits::laneArpSettingFor(*thisDrum, "arpeggiator/preset", value);
			}
		}
		// Or, the normal case of just one sound
		else {
			deluge::edits::arpSettingFor(*soundEditor.currentArpSettings, "arpeggiator/preset", value);
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF),    //<
		    l10n::getView(STRING_FOR_UP),     //<
		    l10n::getView(STRING_FOR_DOWN),   //<
		    l10n::getView(STRING_FOR_BOTH),   //<
		    l10n::getView(STRING_FOR_RANDOM), //<
		    l10n::getView(STRING_FOR_WALK),   //<
		    l10n::getView(STRING_FOR_CUSTOM), //<
		};
	}

	MenuItem* selectButtonPress() override {
		auto current_value = this->getValue<ArpPreset>();
		if (current_value == ArpPreset::CUSTOM) {
			if (soundEditor.editingKitRow()) {
				return &arpOctaveModeToNoteModeMenuForDrums;
			}
			return &arpOctaveModeToNoteModeMenu;
		}
		return nullptr;
	}

	[[nodiscard]] bool showColumnLabel() const override { return false; }

	void getColumnLabel(etl::istring& label) override { label.append(l10n::get(l10n::String::STRING_FOR_MODE)); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		if (this->getValue<ArpPreset>() == ArpPreset::OFF) {
			const auto off = l10n::get(l10n::String::STRING_FOR_OFF);
			return image.drawStringCentered(off, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset + 5,
			                                kTextTitleSpacingX, kTextTitleSizeY, slot.width);
		}

		const auto arpPreset = getValue<ArpPreset>();
		const Icon& icon = [&] {
			switch (arpPreset) {
			case ArpPreset::UP:
			case ArpPreset::DOWN:
				return OLED::arpModeIconUp;
			case ArpPreset::BOTH:
				return OLED::arpModeIconUpDown;
			case ArpPreset::RANDOM:
				return OLED::diceIcon;
			case ArpPreset::WALK:
				return OLED::arpModeIconWalk;
			case ArpPreset::CUSTOM:
				return OLED::arpModeIconCustom;
			default:
				return OLED::arpModeIconUp;
			}
		}();

		const bool reversed = arpPreset == ArpPreset::DOWN;
		image.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset + 1, reversed);
	}
};
} // namespace deluge::gui::menu_item::arpeggiator