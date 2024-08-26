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
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::arpeggiator {
class Rate final : public patched_param::Integer {
public:
	using patched_param::Integer::Integer;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip() && !soundEditor.editingNonAudioDrumRow()
		       && !soundEditor.editingKitAffectEntire();
	}
};

class KitRate final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.editingKitAffectEntire();
	}
};

} // namespace deluge::gui::menu_item::arpeggiator
