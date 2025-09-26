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

#include "gui/ui/keyboard/layout/column_controls.h"
#include "gui/l10n/strings.h"

// Forward declarations
class ArpeggiatorSettings;
class Arpeggiator;

namespace deluge::gui::ui::keyboard::layout {

/// Pulse sequencer keyboard layout that generates rhythmic pulses and patterns
/// Based on arp control but focused on pulse generation and rhythm creation
class KeyboardLayoutPulseSequencer : public ColumnControlsKeyboard {
public:
	KeyboardLayoutPulseSequencer() = default;
	~KeyboardLayoutPulseSequencer() override = default;

	void evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) override;
	void handleVerticalEncoder(int32_t offset) override;
	void handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses],
	                             bool encoderPressed = false) override;
	void precalculate() override;

	void renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) override;

	l10n::String name() override { return l10n::String::STRING_FOR_KEYBOARD_LAYOUT_PULSE_SEQUENCER; }
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }

	/// Update animation and check if display needs refreshing
	void updateAnimation();

	/// Direct pad LED update for real-time animation
	void updatePadLEDsDirect();

	/// Update playback progress bar on top row
	void updatePlaybackProgressBar();

	/// Update display for both OLED and 7-segment
	void updateDisplay();

private:
	/// Get the current arpeggiator settings from the active clip
	ArpeggiatorSettings* getArpSettings();

	/// Get the current arpeggiator instance from the active instrument
	Arpeggiator* getArpeggiator();

	/// Visualize the current rhythm pattern on the main pads
	void renderRhythmPattern(RGB image[][kDisplayWidth + kSideBarWidth]);

	/// Show current arpeggiator parameters in the top rows
	void renderParameterDisplay(RGB image[][kDisplayWidth + kSideBarWidth]);

	/// Show the current step position if arp is playing
	void renderCurrentStep(RGB image[][kDisplayWidth + kSideBarWidth]);

	/// Get color for a rhythm step based on its state
	RGB getStepColor(bool isActive, bool isCurrent, uint8_t velocity = 64);

	/// Get current step index in the rhythm pattern (returns -1 if not playing)
	int32_t getCurrentRhythmStep();

	/// Convert arpeggiator preset enum to display string
	char const* getArpPresetDisplayName(ArpPreset preset);

	/// Convert octave mode enum to display string
	char const* getOctaveModeDisplayName(ArpOctaveMode mode);

	/// Check if arpeggiator settings have changed
	bool hasArpSettingsChanged();

	// Display state
	struct {
		int32_t currentRhythm = 0;
		int32_t lastRhythmStep = -1;
		ArpPreset currentPreset = ArpPreset::OFF;
		ArpOctaveMode currentOctaveMode = ArpOctaveMode::UP;
		uint8_t currentOctaves = 1;
		bool needsRefresh = true;
		bool wasPlaying = false;
	} displayState;
};

}; // namespace deluge::gui::ui::keyboard::layout
