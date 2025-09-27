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

/// Arpeggiator control keyboard layout that visualizes and controls arpeggiator patterns
/// Phase 1: Minimal extension - visualize existing arpeggiator state
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

	/// Update animation and check if display needs refreshing
	void updateAnimation();

	/// Direct pad LED update for real-time animation
	void updatePadLEDsDirect();

	/// Update playback progress bar on top row
	void updatePlaybackProgressBar();

	/// Update display for both OLED and 7-segment
	void updateDisplay();


	// Display state - public so keyboard screen can access for Y encoder press
	struct {
		int32_t currentRhythm = 1; // Currently selected rhythm pattern (for preview)
		int32_t appliedRhythm = 0; // Actually applied rhythm (0=OFF, 1-50=ON)
		int32_t lastRhythmStep = -1;
		ArpPreset currentPreset = ArpPreset::OFF;
		ArpOctaveMode currentOctaveMode = ArpOctaveMode::UP;
		uint8_t currentOctaves = 1;
		bool needsRefresh = true;
		bool wasPlaying = false;
	} displayState;

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

};

}; // namespace deluge::gui::ui::keyboard::layout
