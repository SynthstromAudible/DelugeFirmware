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

#include "gui/ui/keyboard/layout.h"
#include "gui/ui/keyboard/layout/column_control_state.h"

namespace deluge::gui::ui::keyboard::layout {

using namespace deluge::gui::ui::keyboard::controls;

constexpr int32_t kMinIsomorphicRowInterval = 1;
constexpr int32_t kMaxIsomorphicRowInterval = 16;
constexpr uint32_t kHalfStep = 0x7FFFFF;

ColumnControlFunction nextControlFunction(ColumnControlFunction cur, ColumnControlFunction skip);
ColumnControlFunction prevControlFunction(ColumnControlFunction cur, ColumnControlFunction skip);

enum BeatRepeat {
	NO_BEAT_REPEAT = 0,
	DOT_EIGHT,
	EIGHT,
	TRIPLET,
	DOT_SIXTEENTH,
	SIXTEENTH,
	SEXTUPLET,
	THIRTYSECOND,
	SIXTYFOURTH,
	BEAT_REPEAT_MAX, /* should be 9, 8 beat repeat pads plus NO_BEAT_REPEAT */
};

// Note: may need to make this virtual inheritance in the future if we want multiple mix-in-style
// keyboard classes
class ColumnControlsKeyboard : public KeyboardLayout {
public:
	ColumnControlsKeyboard() = default;

	// call this instead of on notestate directly as chord and beat repeat helper
	void enableNote(uint8_t note, uint8_t velocity);

	// should be called by any children that override
	virtual void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;

	// in a child, call verticalEncoderHandledByColumns and ignore the encoder input if it returns
	// true
	virtual void handleVerticalEncoder(int32_t offset) override;

	// in a child, call horizontalEncoderHandledByColumns and ignore the encoder input if it returns
	// true
	virtual void handleHorizontalEncoder(int32_t offset, bool shiftEnabled) override;

	bool verticalEncoderHandledByColumns(int32_t offset);
	bool horizontalEncoderHandledByColumns(int32_t offset, bool shiftEnabled);

	virtual void renderSidebarPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	void checkNewInstrument(Instrument* newInstrument) override;

	void renderColumnBeatRepeat(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);

	int8_t leftColHeld = -1;
	int8_t rightColHeld = -1;
};

}; // namespace deluge::gui::ui::keyboard::layout
