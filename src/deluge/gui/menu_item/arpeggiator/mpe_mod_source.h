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
class ArpMpeModSourceSelection : public Selection {
public:
	using Selection::Selection;

	ArpMpeModSourceSelection(l10n::String newName, l10n::String title, ArpMpeModSource* (*getSelectionPtr)())
	    : Selection(newName, title) {
		getSPtr = getSelectionPtr;
	}

	void readCurrentValue() override { this->setValue(*(getSPtr())); }
	void writeCurrentValue() override { *(getSPtr()) = this->getValue<ArpMpeModSource>(); }

	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_DISABLED),                //<
		    l10n::getView(STRING_FOR_PATCH_SOURCE_AFTERTOUCH), //<
		    l10n::getView(STRING_FOR_PATCH_SOURCE_Y),          //<
		};
	}

	ArpMpeModSource* (*getSPtr)();
};
} // namespace deluge::gui::menu_item::arpeggiator
