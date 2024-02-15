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

namespace deluge::gui::ui::keyboard::layout {

constexpr int32_t kMinIsomorphicRowInterval = 1;
constexpr int32_t kMaxIsomorphicRowInterval = 16;
constexpr uint32_t kVelModShift = 24;
constexpr uint32_t kHalfStep = 0x7FFFFF;
constexpr int32_t kMaxNotesChordMem = 10;

enum ColumnControlFunction : int8_t {
	VELOCITY = 0,
	MOD,
	CHORD,
	CHORD_MEM,
	SCALE_MODE,
	// BEAT_REPEAT,
	COL_CTRL_FUNC_MAX,
};

ColumnControlFunction nextControlFunction(ColumnControlFunction cur, ColumnControlFunction skip);
ColumnControlFunction prevControlFunction(ColumnControlFunction cur, ColumnControlFunction skip);

enum ChordModeChord {
	NO_CHORD = 0,
	FIFTH,
	SUS2,
	MINOR,
	MAJOR,
	SUS4,
	MINOR7,
	DOMINANT7,
	MAJOR7,
	CHORD_MODE_CHORD_MAX, /* should be 9, 8 chord pads plus NO_CHORD */
};

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

protected:
	uint8_t velocity = 64;

private:
	void handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, ColumnControlFunction func,
	               PressedPad pad);

	bool verticalEncoderHandledByFunc(ColumnControlFunction func, int8_t pad, int32_t offset);

	void setActiveChord(ChordModeChord chord);

	void renderColumnVelocity(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);
	void renderColumnMod(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);
	void renderColumnChord(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);
	void renderColumnChordMem(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);
	void renderColumnScaleMode(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);
	void renderColumnBeatRepeat(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column);

	ColumnControlFunction leftColPrev = VELOCITY;
	ColumnControlFunction rightColPrev = MOD;
	ColumnControlFunction leftCol = VELOCITY;
	ColumnControlFunction rightCol = MOD;

	// use higher precision internally so that scaling and stepping is cleaner
	uint32_t velocityMax = 127 << kVelModShift;
	uint32_t velocityMin = 15 << kVelModShift;
	uint32_t velocityStep = 16 << kVelModShift;
	uint32_t velocity32 = velocity << kVelModShift;
	uint32_t vDisplay = velocity;

	uint32_t modMax = 127 << kVelModShift;
	uint32_t modMin = 15 << kVelModShift;
	uint32_t modStep = 16 << kVelModShift;
	uint32_t mod32 = 0 << kVelModShift;
	uint32_t modDisplay;

	ChordModeChord activeChord = NO_CHORD;
	ChordModeChord defaultChord = NO_CHORD;
	uint8_t chordSemitoneOffsets[4] = {0};

	uint8_t chordMemNoteCount[8] = {0};
	uint8_t chordMem[8][kMaxNotesChordMem] = {0};
	uint8_t activeChordMem = 0xFF;

	int32_t currentScalePad = currentSong->getCurrentPresetScale();
	int32_t previousScalePad = currentSong->getCurrentPresetScale();
	uint8_t scaleModes[8] = {0, 1, 2, 3, 4, 5, 6, 7};

	bool horizontalScrollingLeftCol = false;
	bool horizontalScrollingRightCol = false;
	int8_t leftColHeld = -1;
	int8_t rightColHeld = -1;
};

}; // namespace deluge::gui::ui::keyboard::layout
