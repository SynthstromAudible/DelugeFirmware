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
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::filter {
class LPFMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		// Off is located past the HPFLadder, which isn't an option for the low pass filter (should it be?)
		int32_t selection = util::to_underlying(soundEditor.currentModControllable->lpfMode);
		this->setValue(std::min(selection, kNumLPFModes));
	}
	void writeCurrentValue() override {
		int32_t selection = this->getValue();
		FilterMode mode;
		// num lpf modes counts off but there's HPF modes in the middle
		if (selection >= kNumLPFModes) {
			mode = FilterMode::OFF;
		}
		else {
			mode = static_cast<FilterMode>(selection);
		}
		soundEditor.currentModControllable->lpfMode = mode;
	}
	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_12DB_LADDER), l10n::getView(STRING_FOR_24DB_LADDER),
		    l10n::getView(STRING_FOR_DRIVE),       l10n::getView(STRING_FOR_SVF_BAND),
		    l10n::getView(STRING_FOR_SVF_NOTCH),   l10n::getView(STRING_FOR_OFF),
		};
	}
	deluge::vector<std::string_view> getShortOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_12DB_LADDER),     l10n::getView(STRING_FOR_24DB_LADDER),
		    l10n::getView(STRING_FOR_DRIVE_SHORT),     l10n::getView(STRING_FOR_SVF_BAND_SHORT),
		    l10n::getView(STRING_FOR_SVF_NOTCH_SHORT), l10n::getView(STRING_FOR_OFF),
		};
	}

protected:
	bool wrapAround() override { return true; }
};
} // namespace deluge::gui::menu_item::filter
