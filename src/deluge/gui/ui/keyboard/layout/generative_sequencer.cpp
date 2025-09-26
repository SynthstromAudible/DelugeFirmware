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

#include "gui/ui/keyboard/layout/generative_sequencer.h"
#include "gui/colour/colour.h"
#include "gui/menu_item/value_scaling.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/arpeggiator_rhythms.h"
#include "playback/playback_handler.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutGenerativeSequencer::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Clear active notes for now

	// For Phase 1, we're just visualizing - no pad input handling yet
	// Future phases will add interactive control here

	// Handle column controls (beat repeat, etc.)
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutGenerativeSequencer::handleVerticalEncoder(int32_t offset) {
	if (verticalEncoderHandledByColumns(offset)) {
		return;
	}

	// For Phase 1, vertical encoder could cycle through rhythm patterns
	ArpeggiatorSettings* settings = getArpSettings();
	if (settings) {
		// Get current rhythm value
		int32_t currentRhythm = computeCurrentValueForUnsignedMenuItem(settings->rhythm);

		// Cycle through available rhythm patterns
		currentRhythm += offset;
		currentRhythm = std::clamp(currentRhythm, 0, static_cast<int32_t>(kMaxPresetArpRhythm));

		// Update the rhythm setting
		settings->rhythm = (uint32_t)currentRhythm + 2147483648; // Convert back to internal format

		// Show rhythm name
		display->displayPopup(arpRhythmPatternNames[currentRhythm]);

		displayState.needsRefresh = true;
	}
}

void KeyboardLayoutGenerativeSequencer::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                bool encoderPressed) {
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}

	// For Phase 1, horizontal encoder could cycle through arp modes
	ArpeggiatorSettings* settings = getArpSettings();
	if (settings) {
		if (shiftEnabled) {
			// Shift + horizontal: change octave mode
			int32_t newOctaveMode = static_cast<int32_t>(settings->octaveMode) + offset;
			newOctaveMode = std::clamp(newOctaveMode, 0, static_cast<int32_t>(ArpOctaveMode::NUM_ARP_OCTAVE_MODES) - 1);
			settings->octaveMode = static_cast<ArpOctaveMode>(newOctaveMode);

			display->displayPopup(getOctaveModeDisplayName(settings->octaveMode));
		} else {
			// Normal horizontal: change arp mode
			int32_t newMode = static_cast<int32_t>(settings->mode) + offset;
			newMode = std::clamp(newMode, 0, static_cast<int32_t>(ArpMode::NUM_ARP_MODES) - 1);
			settings->mode = static_cast<ArpMode>(newMode);

			display->displayPopup(getArpModeDisplayName(settings->mode));
		}

		displayState.needsRefresh = true;
	}
}

void KeyboardLayoutGenerativeSequencer::precalculate() {
	displayState.needsRefresh = true;
}

void KeyboardLayoutGenerativeSequencer::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Clear the display
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[y][x] = colours::black;
		}
	}

	// Render different sections
	renderParameterDisplay(image);
	renderRhythmPattern(image);
	renderCurrentStep(image);

	displayState.needsRefresh = false;
}

ArpeggiatorSettings* KeyboardLayoutGenerativeSequencer::getArpSettings() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	return clip ? &clip->arpSettings : nullptr;
}

Arpeggiator* KeyboardLayoutGenerativeSequencer::getArpeggiator() {
	Instrument* instrument = getCurrentInstrument();
	if (instrument && instrument->type == OutputType::SYNTH) {
		return &((MelodicInstrument*)instrument)->arpeggiator;
	}
	return nullptr;
}

void KeyboardLayoutGenerativeSequencer::renderRhythmPattern(RGB image[][kDisplayWidth + kSideBarWidth]) {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Get current rhythm pattern
	int32_t rhythmIndex = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	const ArpRhythm& pattern = arpRhythmPatterns[rhythmIndex];

	// Display rhythm pattern in the middle rows (rows 2-4)
	const int32_t patternStartRow = 2;
	const int32_t maxStepsPerRow = kDisplayWidth;

	int32_t currentStep = getCurrentRhythmStep();

	for (int32_t step = 0; step < pattern.length && step < maxStepsPerRow * 3; step++) {
		int32_t x = step % maxStepsPerRow;
		int32_t y = patternStartRow + (step / maxStepsPerRow);

		if (y >= kDisplayHeight) break;

		bool isActive = pattern.steps[step];
		bool isCurrent = (step == currentStep);

		image[y][x] = getStepColor(isActive, isCurrent);
	}
}

void KeyboardLayoutGenerativeSequencer::renderParameterDisplay(RGB image[][kDisplayWidth + kSideBarWidth]) {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Top row: Show arp mode and status
	RGB modeColor = (settings->mode == ArpMode::OFF) ? colours::red : colours::green;

	// Show mode in first few pads of top row
	for (int32_t x = 0; x < 3; x++) {
		image[0][x] = modeColor;
	}

	// Show octave count in next few pads
	RGB octaveColor = colours::blue;
	for (int32_t x = 4; x < 4 + settings->numOctaves && x < kDisplayWidth; x++) {
		image[0][x] = octaveColor;
	}

	// Second row: Show rhythm pattern info
	int32_t rhythmIndex = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	RGB rhythmColor = colours::yellow;

	// Show rhythm index as number of lit pads
	for (int32_t x = 0; x < rhythmIndex && x < kDisplayWidth; x++) {
		image[1][x] = rhythmColor;
	}
}

void KeyboardLayoutGenerativeSequencer::renderCurrentStep(RGB image[][kDisplayWidth + kSideBarWidth]) {
	int32_t currentStep = getCurrentRhythmStep();
	if (currentStep < 0) return; // Not playing

	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	int32_t rhythmIndex = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	const ArpRhythm& pattern = arpRhythmPatterns[rhythmIndex];

	// Highlight current step with brighter color
	const int32_t patternStartRow = 2;
	const int32_t maxStepsPerRow = kDisplayWidth;

	if (currentStep < pattern.length) {
		int32_t x = currentStep % maxStepsPerRow;
		int32_t y = patternStartRow + (currentStep / maxStepsPerRow);

		if (y < kDisplayHeight) {
			// Make current step extra bright
			RGB currentColor = image[y][x];
			image[y][x] = RGB(
				std::min(255, currentColor.r + 100),
				std::min(255, currentColor.g + 100),
				std::min(255, currentColor.b + 100)
			);
		}
	}
}

RGB KeyboardLayoutGenerativeSequencer::getStepColor(bool isActive, bool isCurrent, uint8_t velocity) {
	if (!isActive) {
		return isCurrent ? colours::grey : colours::black;
	}

	// Active steps get colored based on velocity
	RGB baseColor = colours::white;

	if (isCurrent) {
		// Current step is brighter
		return RGB(255, 255, 255);
	} else {
		// Dim the color based on velocity
		uint8_t brightness = (velocity * 200) / 127 + 55; // Scale to 55-255 range
		return RGB(brightness, brightness, brightness);
	}
}

int32_t KeyboardLayoutGenerativeSequencer::getCurrentRhythmStep() {
	Arpeggiator* arp = getArpeggiator();
	if (!arp || !playbackHandler.isEitherClockActive()) {
		return -1; // Not playing
	}

	// Get current rhythm step from arpeggiator state
	return arp->notesPlayedFromRhythm;
}

char const* KeyboardLayoutGenerativeSequencer::getArpModeDisplayName(ArpMode mode) {
	switch (mode) {
		case ArpMode::OFF: return "OFF";
		case ArpMode::UP: return "UP";
		case ArpMode::DOWN: return "DOWN";
		case ArpMode::BOTH: return "BOTH";
		case ArpMode::RANDOM: return "RANDOM";
		case ArpMode::AS_PLAYED: return "AS_PLAYED";
		default: return "UNKNOWN";
	}
}

char const* KeyboardLayoutGenerativeSequencer::getOctaveModeDisplayName(ArpOctaveMode mode) {
	switch (mode) {
		case ArpOctaveMode::UP: return "OCT_UP";
		case ArpOctaveMode::DOWN: return "OCT_DOWN";
		case ArpOctaveMode::ALTERNATE: return "OCT_ALT";
		case ArpOctaveMode::RANDOM: return "OCT_RAND";
		default: return "OCT_UNK";
	}
}

}; // namespace deluge::gui::ui::keyboard::layout
