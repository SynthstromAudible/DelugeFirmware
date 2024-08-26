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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item::voice {
class VoiceCount : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;
	void readCurrentValue() override {
		uint8_t voiceCount = soundEditor.currentSound->maxVoiceCount;
		if (voiceCount > getMaxValue()) {
			voiceCount = 0;
		}
		this->setValue(voiceCount);
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();
		current_value = current_value == 0 ? 127 : current_value;

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Note: we need to apply the same filtering as stated in the isRelevant() function
					if (soundDrum->polyphonic == PolyphonyMode::POLY) {
						soundDrum->maxVoiceCount = current_value;
					}
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->maxVoiceCount = current_value;
		}
	}
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 16; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return (sound->polyphonic == PolyphonyMode::POLY);
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(l10n::String::STRING_FOR_MAX_VOICES_SHORT));
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		if (const auto value = getValue(); value == 0) {
			valueBuf.append(l10n::get(l10n::String::STRING_FOR_OFF));
		}
		else {
			valueBuf.appendInt(value);
		}
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (getValue() == 0) {
			return OLED::main.drawIconCentered(OLED::infinityIcon, slot.start_x, slot.width,
			                                   slot.start_y + kHorizontalMenuSlotYOffset + 1);
		}
		IntegerWithOff::renderInHorizontalMenu(slot);
	}
};

extern VoiceCount polyphonicVoiceCountMenu;

class PolyphonyType final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->polyphonic); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<PolyphonyMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->polyphonic = current_value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->polyphonic = current_value;
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		deluge::vector<std::string_view> options = {
		    l10n::getView(l10n::String::STRING_FOR_AUTO),
		    l10n::getView(l10n::String::STRING_FOR_POLYPHONIC),
		    l10n::getView(l10n::String::STRING_FOR_MONOPHONIC),
		    l10n::getView(l10n::String::STRING_FOR_LEGATO),
		};

		if (soundEditor.editingKit()) {
			options.push_back(l10n::getView(l10n::String::STRING_FOR_CHOKE));
		}
		return options;
	}
	MenuItem* selectButtonPress() override {
		if (this->getValue<PolyphonyMode>() == PolyphonyMode::POLY) {
			return &polyphonicVoiceCountMenu;
		}
		return Selection::selectButtonPress();
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(l10n::String::STRING_FOR_POLYPHONY_SHORT));
	}
};
} // namespace deluge::gui::menu_item::voice
