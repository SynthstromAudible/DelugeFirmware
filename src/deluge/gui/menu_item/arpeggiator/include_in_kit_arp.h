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
#include "gui/l10n/strings.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::arpeggiator {
class IncludeInKitArp final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		if (!soundEditor.allowsNoteTails) {
			this->setValue(0);
		}
		else {
			this->setValue(soundEditor.currentArpSettings->includeInKitArp);
		}
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				bool allowsNoteTails = true;
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);

					char modelStackMemoryForSoundDrum[MODEL_STACK_MAX_SIZE];
					ModelStackWithSoundFlags* modelStackForSoundDrum =
					    getModelStackFromSoundDrum(modelStackMemoryForSoundDrum, soundDrum)->addSoundFlags();
					allowsNoteTails = soundDrum->allowNoteTails(modelStackForSoundDrum, true);
				}

				if (allowsNoteTails) {
					thisDrum->arpSettings.includeInKitArp = current_value != 0;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			if (soundEditor.allowsNoteTails) {
				soundEditor.currentArpSettings->includeInKitArp = current_value != 0;
			}
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_OFF), //<
		    l10n::getView(STRING_FOR_ON),  //<
		};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingKitRow();
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Selection::configureRenderingOptions(options);
		options.label = deluge::l10n::get(l10n::built_in::seven_segment, this->name);
	}

	// flag this selection menu as a toggle menu so we can use a checkbox to toggle value
	bool isToggle() override { return true; }

	// don't enter menu on select button press
	bool shouldEnterSubmenu() override { return false; }
};
} // namespace deluge::gui::menu_item::arpeggiator
