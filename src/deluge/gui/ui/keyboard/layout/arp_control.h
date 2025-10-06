/*
 * Copyright © 2025 Synthstrom Audible Limited
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

#include "gui/l10n/strings.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "modulation/arpeggiator.h"

// Forward declarations
class ArpeggiatorSettings;

namespace deluge::gui::ui::keyboard::layout {

/// Clean, simple arpeggiator control keyboard layout
class KeyboardLayoutArpControl : public ColumnControlsKeyboard {
public:
	KeyboardLayoutArpControl() = default;
	~KeyboardLayoutArpControl() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_ARP_CONTROL; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

	/// Update animation - called by keyboard screen
	void updateAnimation();

	/// Update display for both OLED and 7-segment
	void updateDisplay();

	/// Direct pad LED update for real-time animation
	void updatePadLEDsDirect();

	/// Update playback progress bar on top row
	void updatePlaybackProgressBar();

private:
	/// Get arpeggiator settings from current clip
	ArpeggiatorSettings* getArpSettings();

	/// Handle arp mode control (top row, pads 0-2)
	void handleArpMode(int32_t x, ArpeggiatorSettings* settings);

	/// Handle octave control (top row, pads 4-11)
	void handleOctaves(int32_t x, ArpeggiatorSettings* settings);

	/// Handle sequence length control (row 1, pads 0-15)
	void handleSequenceLength(int32_t x, ArpeggiatorSettings* settings);

	/// Handle velocity spread control (row 2, pads 0-7)
	void handleVelocitySpread(int32_t x, ArpeggiatorSettings* settings);

	/// Handle gate control (row 3, pads 0-7)
	void handleGate(int32_t x, ArpeggiatorSettings* settings);

	/// Handle random octave control (row 1, pads 8-15)
	void handleRandomOctave(int32_t x, ArpeggiatorSettings* settings);

	/// Handle random gate control (row 2, pads 8-15)
	void handleRandomGate(int32_t x, ArpeggiatorSettings* settings);

	/// Handle randomizer lock toggle (row 0, pad 15)
	void handleRandomizerLock();

	/// Handle transpose control (row 3, pads 14-15)
	void handleTranspose(int32_t x);

	/// Handle keyboard notes (rows 4-7)
	void handleKeyboard(int32_t x, int32_t y, uint8_t velocity);

	/// Helper function to set parameters safely for different output types
	void setParameterSafely(int32_t paramId, int32_t value, bool useUnsignedScaling);

	/// Get color for arp mode display
	RGB getArpModeColor(ArpeggiatorSettings* settings, int32_t x);

	/// Get color for octave display
	RGB getOctaveColor(int32_t octave, int32_t currentOctaves);

	/// Get color for sequence length display
	RGB getSequenceLengthColor(int32_t length);

	/// Get color for velocity spread display
	RGB getVelocitySpreadColor(int32_t spread);

	/// Get color for gate display
	RGB getGateColor(int32_t gate);

	/// Get color for random octave display
	RGB getRandomOctaveColor(int32_t octave);

	/// Get color for random gate display
	RGB getRandomGateColor(int32_t gate);

	/// Get color for rhythm pattern visualization
	RGB getRhythmPatternColor(int32_t step);

	/// Apply current rhythm setting to arp settings
	void applyRhythmToArpSettings();

	/// Get color for keyboard notes
	RGB getKeyboardColor(int32_t x, int32_t y);

public:
	/// Handle rhythm toggle with proper track type handling
	void handleRhythmToggle();

	/// Convert arpeggiator preset enum to display string
	char const* getArpPresetDisplayName(ArpPreset preset);

	/// Simple keyboard helper functions
	inline uint16_t noteFromCoords(int32_t x, int32_t y) { return noteFromPadIndex(padIndexFromCoords(x, y)); }

	inline uint16_t padIndexFromCoords(int32_t x, int32_t y) {
		return getState().inKey.scrollOffset + x + y * getState().inKey.rowInterval;
	}

	inline uint16_t noteFromPadIndex(uint16_t padIndex) {
		NoteSet& scaleNotes = getScaleNotes();
		uint8_t scaleNoteCount = getScaleNoteCount();

		uint8_t octave = padIndex / scaleNoteCount;
		uint8_t octaveNoteIndex = padIndex % scaleNoteCount;
		return octave * kOctaveSize + getRootNote() + scaleNotes[octaveNoteIndex];
	}

	// Simple state tracking
	int32_t keyboardScrollOffset = 0;   // Keyboard transpose offset
	int32_t lastTouchedGatePad = -1;    // Track last touched gate pad for LED feedback
	int32_t lastTouchedVelocityPad = 0; // Track last touched velocity pad for LED feedback (default to pad 0)
	int32_t lastTouchedSequenceLengthPad =
	    0; // Track last touched sequence length pad for LED feedback (default to pad 0)
	int32_t lastTouchedRandomOctavePad = 0; // Track last touched random octave pad for LED feedback (default to pad 0)
	int32_t lastTouchedRandomGatePad = 0;   // Track last touched random gate pad for LED feedback (default to pad 0)

	// Individual pad values for tweaking
	static constexpr int32_t kNumControlValues = 8;
	int32_t sequenceLengthValues[kNumControlValues] = {0, 10, 20, 25, 35, 40, 45, 50}; // Each pad has its own value
	int32_t velocitySpreadValues[kNumControlValues] = {0, 10, 20, 25, 35, 40, 45, 50}; // Each pad has its own value
	int32_t gateValues[kNumControlValues] = {1, 10, 20, 25, 35, 40, 45, 50};           // Each pad has its own value
	int32_t randomOctaveValues[kNumControlValues] = {0, 5, 10, 15, 20, 25, 30, 35};    // Random octave spread values
	int32_t randomGateValues[kNumControlValues] = {0, 5, 10, 15, 20, 25, 30, 35};      // Random gate spread values

public:
	// Public display state for keyboard screen access (consolidated rhythm state)
	struct {
		int32_t currentRhythm = 1;
		int32_t appliedRhythm = 0;
	} displayState;
};

} // namespace deluge::gui::ui::keyboard::layout
