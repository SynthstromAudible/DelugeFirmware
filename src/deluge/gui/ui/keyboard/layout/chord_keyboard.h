/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
 * Copyright © 2024 Madeline Scyphers
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

#include "definitions.h"
#include "gui/ui/keyboard/chords.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "util/const_functions.h"
#include <array>
#include <set>

namespace deluge::gui::ui::keyboard::layout {

const int32_t SCALEFIRST = 0;
const int32_t SCALESECOND = 1;
const int32_t SCALETHIRD = 2;
const int32_t SCALEFOURTH = 3;
const int32_t SCALEFIFTH = 4;
const int32_t SCALESIXTH = 5;
const int32_t SCALESEVENTH = 6;
const int32_t SCALEOCTAVE = 7;

enum class ChordKeyboardMode { ROW, COLUMN };
const int32_t kChordKeyboardColumns = 14;

/// @brief Represents a keyboard layout for chord-based input.
class KeyboardLayoutChord : public ColumnControlsKeyboard {
public:
	KeyboardLayoutChord() = default;
	~KeyboardLayoutChord() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_CHORD_KEYBOARD; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	RequiredScaleMode requiredScaleMode() override { return RequiredScaleMode::Disabled; }

	ChordKeyboardMode mode = ChordKeyboardMode::COLUMN;

protected:
	bool allowSidebarType(ColumnControlFunction sidebarType) override;

private:
	void offsetPads(int32_t offset, bool shiftEnabled);
	void handleControlButton(int32_t x, int32_t y);
	uint8_t noteFromCoordsRow(int32_t x, int32_t y, int32_t root, NoteSet& scaleNotes, uint8_t scaleNoteCount);
	void evaluatePadsRow(PressedPad pressed);
	void evaluatePadsColumn(PressedPad pressed);
	void drawChordName(int16_t noteCode, const char* chordName = "", const char* voicingName = "");

	Scale lastScale = NO_SCALE;

	// ChordKeyboardMode mode = ChordKeyboardMode::ROW;

	std::array<RGB, static_cast<int>(ChordQuality::CHORD_QUALITY_MAX)> qualityColours = {
	    colours::blue, colours::purple, colours::green, colours::kelly::very_light_blue,
	    colours::cyan, colours::yellow};

	std::array<int32_t, kOctaveSize + kDisplayHeight + kDisplayWidth> qualities;

	std::array<int32_t, kDisplayWidth - 1> scaleSteps = {
	    SCALEFIRST,
	    SCALEFIFTH,
	    SCALETHIRD + SCALEOCTAVE,
	    SCALESEVENTH + SCALEOCTAVE,
	    SCALEFIFTH + SCALEOCTAVE,
	    SCALETHIRD + 2 * SCALEOCTAVE,
	    SCALESECOND + 2 * SCALEOCTAVE,
	    SCALESIXTH + SCALEOCTAVE,
	    SCALEOCTAVE,
	    SCALEFIFTH,
	    SCALESEVENTH,
	    SCALETHIRD - SCALEOCTAVE,
	    SCALESECOND,
	    SCALEFIFTH,
	    SCALEFIRST,
	    // SCALEFIRST - SCALEOCTAVE,
	};

	std::array<const std::array<Chord, majorChords.size()>*, 6> chordColumns = {
	    &majorChords, &minorChords, &diminishedChords, &augmentedChords, &dominantChords, &otherChords};

	std::set<Scale> acceptedScales = {Scale::MAJOR_SCALE,    Scale::MINOR_SCALE,  Scale::DORIAN_SCALE,
	                                  Scale::PHRYGIAN_SCALE, Scale::LYDIAN_SCALE, Scale::MIXOLYDIAN_SCALE,
	                                  Scale::LOCRIAN_SCALE};
};

}; // namespace deluge::gui::ui::keyboard::layout
