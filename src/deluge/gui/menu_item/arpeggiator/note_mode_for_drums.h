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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class NoteModeForDrums : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->noteMode); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ArpNoteMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				// Note: we need to apply the same filtering as stated in the isRelevant() function
				if (thisDrum->type != DrumType::GATE) {
					thisDrum->arpSettings.noteMode = current_value;
					thisDrum->arpSettings.updatePresetFromCurrentSettings();
					thisDrum->arpSettings.flagForceArpRestart = true;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentArpSettings->noteMode = current_value;
			soundEditor.currentArpSettings->updatePresetFromCurrentSettings();
			soundEditor.currentArpSettings->flagForceArpRestart = true;
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingKitRow() && !soundEditor.editingGateDrumRow();
	}
	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		Selection::configureRenderingOptions(options);
		options.label = deluge::l10n::get(l10n::built_in::seven_segment, this->name);
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_UP),      //<
		    l10n::getView(STRING_FOR_DOWN),    //<
		    l10n::getView(STRING_FOR_UP_DOWN), //<
		    l10n::getView(STRING_FOR_RANDOM),  //<
		    l10n::getView(STRING_FOR_WALK1),   //<
		    l10n::getView(STRING_FOR_WALK2),   //<
		    l10n::getView(STRING_FOR_WALK3),   //<
		};
	}
};

class NoteModeFromOctaveModeForDrums final : public NoteModeForDrums {
public:
	using NoteModeForDrums::NoteModeForDrums;
	void readCurrentValue() override {
		if (display->have7SEG()) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTE_MODE));
		}
		NoteModeForDrums::readCurrentValue();
	}
};

extern NoteModeFromOctaveModeForDrums arpNoteModeFromOctaveModeMenuForDrums;
} // namespace deluge::gui::menu_item::arpeggiator
