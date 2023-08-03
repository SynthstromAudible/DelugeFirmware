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
#include "gui/menu_item/selection.h"
#include "gui/menu_item/sync_level.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"

namespace menu_item::delay {

class Analog final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = soundEditor.currentModControllable->delay.analog; }
	void writeCurrentValue() { soundEditor.currentModControllable->delay.analog = soundEditor.currentValue; }
	char const** getOptions() {
		static char const* options[] = {"Digital", "Analog", NULL};
		return options;
	}
};

} // namespace menu_item::delay
