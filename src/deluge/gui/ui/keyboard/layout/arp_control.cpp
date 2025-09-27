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

#include "gui/ui/keyboard/layout/arp_control.h"
#include "gui/colour/colour.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/arpeggiator_rhythms.h"
#include "modulation/params/param.h"
#include "playback/playback_handler.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutArpControl::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Clear active notes for now

	// For Phase 1, we're just visualizing - no pad input handling yet
	// Notes come from the clip, not from pad input
	// Future phases will add interactive control here

	// Handle column controls (beat repeat, etc.) - but only when user wants them
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutArpControl::handleVerticalEncoder(int32_t offset) {
	// Force direct control - bypass column controls completely
	// Don't call verticalEncoderHandledByColumns at all

	// Rhythm control: scroll through patterns and apply immediately if rhythm is ON
	if (offset > 0) {
		displayState.currentRhythm++;
		if (displayState.currentRhythm > 50) {
			displayState.currentRhythm = 1; // Wrap to first pattern (skip 0)
		}
	} else if (offset < 0) {
		displayState.currentRhythm--;
		if (displayState.currentRhythm < 1) {
			displayState.currentRhythm = 50; // Wrap to last pattern
		}
	}

	// If rhythm is currently ON, apply the new pattern immediately
	if (displayState.appliedRhythm > 0) {
		displayState.appliedRhythm = displayState.currentRhythm;
		
		// Apply the change using the parameter system
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip) {
			ArpeggiatorSettings* settings = &clip->arpSettings;
			
			// CRITICAL: Always ensure syncLevel is properly set (never 0)
			if (settings->syncLevel == 0) {
				settings->syncLevel = (SyncLevel)(8 - currentSong->insideWorldTickMagnitude - currentSong->insideWorldTickMagnitudeOffsetFromBPM);
			}

			UI* originalUI = getCurrentUI();
			
			// Set up sound editor context like the official menu
			if (soundEditor.setup(clip, nullptr, 0)) {
				// Now we're in sound editor context - use the official approach
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
				ModelStackWithAutoParam* modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(modulation::params::UNPATCHED_ARP_RHYTHM);
				
				if (modelStackWithParam && modelStackWithParam->autoParam) {
					int32_t finalValue = computeFinalValueForUnsignedMenuItem(displayState.appliedRhythm);
					modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
				}
				
				// Exit sound editor context back to original UI
				originalUI->focusRegained();
			}
		}
	}

	// Show rhythm name with appropriate status
	char statusMsg[50];
	if (displayState.appliedRhythm == 0) {
		snprintf(statusMsg, sizeof(statusMsg), "%s (preview)", arpRhythmPatternNames[displayState.currentRhythm]);
	} else {
		snprintf(statusMsg, sizeof(statusMsg), "%s", arpRhythmPatternNames[displayState.currentRhythm]);
	}
	display->displayPopup(statusMsg);

	// Update display and pads
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                                PressedPad presses[kMaxNumKeyboardPadPresses],
                                                                bool encoderPressed) {
	// Conditional: Only use column controls when user is holding column pads
	if (leftColHeld != -1 || rightColHeld != -1) {
		// User is holding column pads - let column controls handle it
		if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
			return;
		}
	}

	// Check for horizontal encoder button press to control octave count
	static bool lastHorizontalEncoderPressed = false;
	bool horizontalEncoderPressed = Buttons::isButtonPressed(hid::button::X_ENC);

	// For Phase 1, horizontal encoder could cycle through arp modes
	ArpeggiatorSettings* settings = getArpSettings();
	if (settings) {

		// If encoder is pressed, control octave count instead of modes
		if (horizontalEncoderPressed) {
			int32_t newOctaves = settings->numOctaves + offset;
			newOctaves = std::clamp(newOctaves, (int32_t)1, (int32_t)8); // Octaves 1-8
			settings->numOctaves = newOctaves;

			// Force arpeggiator to restart with new octave count
			settings->flagForceArpRestart = true;

			display->displayPopup(("Octaves: " + std::to_string(newOctaves)).c_str());

			// Display update
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			keyboardScreen.requestMainPadsRendering();

			lastHorizontalEncoderPressed = horizontalEncoderPressed;
			return;
		}
		lastHorizontalEncoderPressed = horizontalEncoderPressed;
		if (shiftEnabled) {
			// Shift + horizontal: change octave mode
			int32_t newOctaveMode = static_cast<int32_t>(settings->octaveMode) + offset;
			newOctaveMode = std::clamp(newOctaveMode, static_cast<int32_t>(0), static_cast<int32_t>(ArpOctaveMode::RANDOM));
			settings->octaveMode = static_cast<ArpOctaveMode>(newOctaveMode);

			// Force arpeggiator to restart so it picks up the new octave mode immediately
			settings->flagForceArpRestart = true;

			display->displayPopup(getOctaveModeDisplayName(settings->octaveMode));

			// Display update: Handle both OLED and 7-segment
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			// 7-segment display is already handled by displayPopup()
		} else {
			// Normal horizontal: change arp preset (not mode)
			int32_t newPreset = static_cast<int32_t>(settings->preset) + offset;
			newPreset = std::clamp(newPreset, static_cast<int32_t>(0), static_cast<int32_t>(ArpPreset::CUSTOM));
			settings->preset = static_cast<ArpPreset>(newPreset);

			// Update settings from preset to enable arpeggiator
			settings->updateSettingsFromCurrentPreset();


			// Force arpeggiator to restart so it picks up the new preset immediately
			settings->flagForceArpRestart = true;

			display->displayPopup(getArpPresetDisplayName(settings->preset));

			// Display update: Handle both OLED and 7-segment
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			// 7-segment display is already handled by displayPopup()

			// Pad update: Only because arp status changed
			keyboardScreen.requestMainPadsRendering();
		}
	}
}

void KeyboardLayoutArpControl::precalculate() {
	displayState.needsRefresh = true;
}

void KeyboardLayoutArpControl::updateAnimation() {
	// Simplified animation - only update when arp step changes during playback
	int32_t currentStep = getCurrentRhythmStep();
	ArpeggiatorSettings* settings = getArpSettings();

	// Update progress bar using clip view approach (no OLED interference)
	updatePlaybackProgressBar();

	// Only refresh for arp step changes to avoid OLED interference
	if (currentStep != displayState.lastRhythmStep && currentStep >= 0 && settings && settings->preset != ArpPreset::OFF) {
		displayState.lastRhythmStep = currentStep;

		// Direct pad LED update for arp step highlighting
		updatePadLEDsDirect();
	}
}

void KeyboardLayoutArpControl::updatePadLEDsDirect() {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) {
		return;
	}

	// Update playback progress bar on top row (like clip view)
	updatePlaybackProgressBar();

	if (settings->preset == ArpPreset::OFF) {
		return;
	}

	// Get current step and pattern
	int32_t currentStep = getCurrentRhythmStep();
	int32_t rhythmIndex = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	const ArpRhythm& pattern = arpRhythmPatterns[rhythmIndex];

	if (currentStep < 0) {
		return; // Not playing
	}

	// Update only the current step pad directly using PadLEDs::set
	const int32_t patternStartRow = 2;
	const int32_t maxStepsPerRow = kDisplayWidth;

	// Clear previous step highlighting by re-rendering just the pattern area
	for (int32_t step = 0; step < pattern.length && step < maxStepsPerRow * 3; step++) {
		int32_t x = step % maxStepsPerRow;
		int32_t y = patternStartRow + (step / maxStepsPerRow);

		if (y >= kDisplayHeight) break;

		bool isActive = pattern.steps[step];
		bool isCurrent = (step == currentStep);

		RGB color = getStepColor(isActive, isCurrent);
		PadLEDs::set({x, y}, color);
	}

	// Let the normal rendering system handle sending colors
	// Don't call PadLEDs::sendOutMainPadColours() to avoid OLED conflicts
}

void KeyboardLayoutArpControl::updateDisplay() {
	// Handle both OLED and 7-segment displays properly
	if (display->haveOLED()) {
		// OLED display - trigger full UI render
		renderUIsForOled();
	} else {
		// 7-segment display - the popup should already be displayed
		// No additional action needed for 7-segment
	}
}

void KeyboardLayoutArpControl::updatePlaybackProgressBar() {
	// Use horizontal progress bar on bottom row (row 7)
	int32_t newTickSquare;

	// Calculate playback position (same logic as clip views)
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(getCurrentClip())
	    || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
	    || playbackHandler.ticksLeftInCountIn) {
		newTickSquare = 255; // Off screen
	}
	else {
		// Calculate position within clip (same as getTickSquare() in clip views)
		Clip* clip = getCurrentClip();
		if (clip) {
			newTickSquare = (uint64_t)(clip->lastProcessedPos + playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick())
			                * kDisplayWidth / clip->loopLength;
			if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		} else {
			newTickSquare = 255;
		}
	}

	// Create horizontal progress bar - only on bottom row (row 7)
	uint8_t tickSquares[kDisplayHeight];
	for (int32_t row = 0; row < kDisplayHeight; row++) {
		if (row == 7) { // Bottom row gets the progress indicator
			tickSquares[row] = newTickSquare;
		} else {
			tickSquares[row] = 255; // Off screen for other rows
		}
	}

	// Set colors for the progress line
	std::array<uint8_t, 8> coloursArray = {0}; // Default color for bottom row

	// Send to hardware using the same system as clip views
	PadLEDs::setTickSquares(tickSquares, coloursArray.data());
}

bool KeyboardLayoutArpControl::hasArpSettingsChanged() {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return false;

	bool changed = false;

	// Check rhythm pattern - but don't overwrite our display state when we're controlling it
	int32_t currentRhythm = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	// Only update display state if we're not actively controlling the rhythm
	// This prevents the OLED from showing converted values that don't match our encoder
	if (currentRhythm != displayState.currentRhythm && currentRhythm >= 1 && currentRhythm <= 50) {
		// Only accept values in our valid range (1-50)
		displayState.currentRhythm = currentRhythm;
		changed = true;
	}

	// Check preset
	if (settings->preset != displayState.currentPreset) {
		displayState.currentPreset = settings->preset;
		changed = true;
	}

	// Check octave mode
	if (settings->octaveMode != displayState.currentOctaveMode) {
		displayState.currentOctaveMode = settings->octaveMode;
		changed = true;
	}

	// Check octave count
	if (settings->numOctaves != displayState.currentOctaves) {
		displayState.currentOctaves = settings->numOctaves;
		changed = true;
	}

	return changed;
}

void KeyboardLayoutArpControl::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
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

ArpeggiatorSettings* KeyboardLayoutArpControl::getArpSettings() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	return clip ? &clip->arpSettings : nullptr;
}

Arpeggiator* KeyboardLayoutArpControl::getArpeggiator() {
	Instrument* instrument = getCurrentInstrument();
	if (instrument && instrument->type == OutputType::SYNTH) {
		return &((MelodicInstrument*)instrument)->arpeggiator;
	}
	return nullptr;
}

void KeyboardLayoutArpControl::renderRhythmPattern(RGB image[][kDisplayWidth + kSideBarWidth]) {
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

		RGB stepColor = getStepColor(isActive, isCurrent);
		// Dim the pattern when rhythm is not applied
		if (displayState.appliedRhythm == 0) {
			stepColor = RGB(stepColor.r / 4, stepColor.g / 4, stepColor.b / 4);
		}
		image[y][x] = stepColor;
	}
}

void KeyboardLayoutArpControl::renderParameterDisplay(RGB image[][kDisplayWidth + kSideBarWidth]) {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Top row: Show arp mode and status
	RGB modeColor = (settings->preset == ArpPreset::OFF) ? colours::red : colours::green;

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
	RGB rhythmColor = (displayState.appliedRhythm > 0) ? colours::yellow : RGB(40, 40, 0); // Dim yellow when rhythm OFF

	// Show rhythm index as number of lit pads
	for (int32_t x = 0; x < rhythmIndex && x < kDisplayWidth; x++) {
		image[1][x] = rhythmColor;
	}
}

void KeyboardLayoutArpControl::renderCurrentStep(RGB image[][kDisplayWidth + kSideBarWidth]) {
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

RGB KeyboardLayoutArpControl::getStepColor(bool isActive, bool isCurrent, uint8_t velocity) {
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

int32_t KeyboardLayoutArpControl::getCurrentRhythmStep() {
	Arpeggiator* arp = getArpeggiator();
	ArpeggiatorSettings* settings = getArpSettings();

	if (!arp || !settings || !playbackHandler.isEitherClockActive() || settings->preset == ArpPreset::OFF) {
		return -1; // Not playing or arp is off
	}

	// Get current rhythm pattern
	int32_t rhythmIndex = computeCurrentValueForUnsignedMenuItem(settings->rhythm);
	const ArpRhythm& pattern = arpRhythmPatterns[rhythmIndex];

	// Get current rhythm step from arpeggiator state and normalize to pattern length
	int32_t currentStep = arp->notesPlayedFromRhythm % pattern.length;

	return currentStep;
}

char const* KeyboardLayoutArpControl::getArpPresetDisplayName(ArpPreset preset) {
	switch (preset) {
		case ArpPreset::OFF: return "OFF";
		case ArpPreset::UP: return "UP";
		case ArpPreset::DOWN: return "DOWN";
		case ArpPreset::BOTH: return "BOTH";
		case ArpPreset::RANDOM: return "RANDOM";
		case ArpPreset::WALK: return "WALK";
		case ArpPreset::CUSTOM: return "CUSTOM";
		default: return "UNKNOWN";
	}
}

char const* KeyboardLayoutArpControl::getOctaveModeDisplayName(ArpOctaveMode mode) {
	switch (mode) {
		case ArpOctaveMode::UP: return "OCT_UP";
		case ArpOctaveMode::DOWN: return "OCT_DOWN";
		case ArpOctaveMode::ALTERNATE: return "OCT_ALT";
		case ArpOctaveMode::RANDOM: return "OCT_RAND";
		default: return "OCT_UNK";
	}
}


}; // namespace deluge::gui::ui::keyboard::layout

