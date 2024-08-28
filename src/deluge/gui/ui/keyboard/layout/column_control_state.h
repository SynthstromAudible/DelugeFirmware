/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/column_controls/chord.h"
#include "gui/ui/keyboard/column_controls/chord_mem.h"
#include "gui/ui/keyboard/column_controls/dx.h"
#include "gui/ui/keyboard/column_controls/keyboard_control.h"
#include "gui/ui/keyboard/column_controls/mod.h"
#include "gui/ui/keyboard/column_controls/scale_mode.h"
#include "gui/ui/keyboard/column_controls/session.h"
#include "gui/ui/keyboard/column_controls/song_chord_mem.h"
#include "gui/ui/keyboard/column_controls/velocity.h"

namespace deluge::gui::ui::keyboard::layout {

enum ColumnControlFunction : int8_t {
	VELOCITY = 0,
	MOD,
	CHORD,
	SONG_CHORD_MEM,
	CHORD_MEM,
	SCALE_MODE,
	DX,
	SESSION,
	KEYBOARD_CONTROL,
	// BEAT_REPEAT,
	COL_CTRL_FUNC_MAX,
};

using namespace deluge::gui::ui::keyboard::controls;

struct ColumnControlState {
	VelocityColumn velocityColumn{64};

	ModColumn modColumn{};
	ChordColumn chordColumn{};
	SongChordMemColumn songChordMemColumn{};
	ChordMemColumn chordMemColumn{};
	ScaleModeColumn scaleModeColumn{};
	DXColumn dxColumn{};
	SessionColumn sessionColumn{};
	KeyboardControlColumn keyboardControlColumn{};

	ColumnControlFunction leftColFunc = VELOCITY;
	ColumnControlFunction rightColFunc = MOD;
	bool rightColSetAtRuntime = false;
	ControlColumn* leftCol = &velocityColumn;
	ControlColumn* rightCol = &modColumn;

	ControlColumn* getColumnForFunc(ColumnControlFunction func);

	void writeToFile(Serializer& writer);
	void readFromFile(Deserializer& reader);
};

} // namespace deluge::gui::ui::keyboard::layout
