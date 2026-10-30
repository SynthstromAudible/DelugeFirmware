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
#include "gui/menu_item/short_label.h"
#include "gui/ui/sound_editor.h"
#include "model/edits/arp_setting_for.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::arpeggiator {
class NoteMode : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->noteMode); }
	void writeCurrentValue() override {
		deluge::edits::arpSettingFor(*soundEditor.currentArpSettings, "arpeggiator/noteMode",
		                             deluge::edits::arpChoice("arpeggiator/noteMode", this->getValue()));
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return !soundEditor.editingKitRow();
	}
	void getColumnLabel(etl::istring& label) override {
		const std::string_view text = shortLabel(this->name);
		label.append(text.data(), text.size());
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		if (optType == OptType::SHORT) {
			return {
			    shortLabel(STRING_FOR_UP),        //<
			    shortLabel(STRING_FOR_DOWN),      //<
			    shortLabel(STRING_FOR_UP_DOWN),   //<
			    shortLabel(STRING_FOR_RANDOM),    //<
			    shortLabel(STRING_FOR_WALK1),     //<
			    shortLabel(STRING_FOR_WALK2),     //<
			    shortLabel(STRING_FOR_WALK3),     //<
			    shortLabel(STRING_FOR_AS_PLAYED), //<
			    shortLabel(STRING_FOR_PATTERN),   //<
			};
		}
		return {
		    l10n::getView(STRING_FOR_UP),        //<
		    l10n::getView(STRING_FOR_DOWN),      //<
		    l10n::getView(STRING_FOR_UP_DOWN),   //<
		    l10n::getView(STRING_FOR_RANDOM),    //<
		    l10n::getView(STRING_FOR_WALK1),     //<
		    l10n::getView(STRING_FOR_WALK2),     //<
		    l10n::getView(STRING_FOR_WALK3),     //<
		    l10n::getView(STRING_FOR_AS_PLAYED), //<
		    l10n::getView(STRING_FOR_PATTERN),   //<
		};
	}
};

class NoteModeFromOctaveMode final : public NoteMode {
public:
	using NoteMode::NoteMode;
	void readCurrentValue() override { NoteMode::readCurrentValue(); }
};

extern NoteModeFromOctaveMode arpNoteModeFromOctaveModeMenu;
} // namespace deluge::gui::menu_item::arpeggiator
