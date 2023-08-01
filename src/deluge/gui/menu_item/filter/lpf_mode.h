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
#include "gui/menu_item/selection/typed_selection.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::filter {
class LPFMode final : public TypedSelection<::LPFMode, kNumLPFModes> {
public:
	using TypedSelection::TypedSelection;
	void readCurrentValue() override { this->value_ = soundEditor.currentModControllable->lpfMode; }
	void writeCurrentValue() override { soundEditor.currentModControllable->lpfMode = this->value_; }
	static_vector<string, capacity()> getOptions() override { return {"12dB", "24dB", "Drive", "SVF"}; }
	bool isRelevant(Sound* sound, int32_t whichThing) override {
		return ((sound == nullptr) || sound->synthMode != SynthMode::FM);
	}
};
} // namespace deluge::gui::menu_item::filter
