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
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class NoteMode : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->noteMode); }
	void writeCurrentValue() override {
		soundEditor.currentArpSettings->noteMode = this->getValue<ArpNoteMode>();
		soundEditor.currentArpSettings->updatePresetFromCurrentSettings();
		soundEditor.currentArpSettings->flagForceArpRestart = true;
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingKit();
	}

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_UP),        //<
		    l10n::getView(STRING_FOR_DOWN),      //<
		    l10n::getView(STRING_FOR_UP_DOWN),   //<
		    l10n::getView(STRING_FOR_AS_PLAYED), //<
		    l10n::getView(STRING_FOR_RANDOM),    //<
		};
	}
};

class NoteModeFromOctaveMode final : public NoteMode {
public:
	using NoteMode::NoteMode;
	void readCurrentValue() override {
		if (display->have7SEG()) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTE_MODE));
		}
		NoteMode::readCurrentValue();
	}
};

extern NoteModeFromOctaveMode arpNoteModeFromOctaveModeMenu;
} // namespace deluge::gui::menu_item::arpeggiator
