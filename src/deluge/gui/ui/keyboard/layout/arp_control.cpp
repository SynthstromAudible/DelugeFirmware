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

	// Handle green pad presses in top row (toggle between OFF and UP)
	for (int32_t i = 0; i < kMaxNumKeyboardPadPresses; i++) {
		if (presses[i].active) {
			int32_t x = presses[i].x;
			int32_t y = presses[i].y;

			// Check if pressing green pads (top row, first 3 pads)
			if (y == 0 && x >= 0 && x < 3) {
				ArpeggiatorSettings* settings = getArpSettings();
				if (settings) {
					// Toggle between OFF and UP
					if (settings->preset == ArpPreset::OFF) {
						settings->preset = ArpPreset::UP;
						display->displayPopup("Arp UP");
					} else {
						settings->preset = ArpPreset::OFF;
						display->displayPopup("Arp OFF");
					}

					// Update settings from preset to apply the change
					settings->updateSettingsFromCurrentPreset();

					// Force arpeggiator to restart with new preset
					settings->flagForceArpRestart = true;

					// Update display
					if (display->haveOLED()) {
						renderUIsForOled();
					}
					keyboardScreen.requestMainPadsRendering();
				}
				break; // Only handle one pad press at a time
			}

			// Check if pressing blue octave pads (top row, positions 4-11)
			if (y == 0 && x >= 4 && x < std::min((int32_t)(4 + 8), (int32_t)kDisplayWidth)) {
				ArpeggiatorSettings* settings = getArpSettings();
				if (settings) {
					// Set octave count based on pad position (1-8)
					int32_t newOctaves = x - 4 + 1;
					settings->numOctaves = newOctaves;

					// Force arpeggiator to restart with new octave count
					settings->flagForceArpRestart = true;

					display->displayPopup(("Octaves: " + std::to_string(newOctaves)).c_str());

					// Update display
					if (display->haveOLED()) {
						renderUIsForOled();
					}
					keyboardScreen.requestMainPadsRendering();
				}
				break; // Only handle one pad press at a time
			}

			// Check if pressing yellow rhythm pads (top row, positions 12-14)
			if (y == 0 && x >= 12 && x < 15 && x < kDisplayWidth) {
				// Toggle rhythm on/off (same as Y encoder press)
				if (displayState.appliedRhythm == 0) {
					// Turn ON: apply the currently selected pattern
					displayState.appliedRhythm = displayState.currentRhythm;
					display->displayPopup("Rhythm ON");
				} else {
					// Turn OFF: keep current pattern for next time
					displayState.appliedRhythm = 0;
					display->displayPopup("Rhythm OFF");
				}

				// Apply the change to the actual arpeggiator
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
							// Use appliedRhythm for the actual parameter value
							int32_t finalValue = computeFinalValueForUnsignedMenuItem(displayState.appliedRhythm);
							modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
						}

						// Exit sound editor context back to original UI
						originalUI->focusRegained();
					}
				}

				// Update display and pads
				if (display->haveOLED()) {
					renderUIsForOled();
				}
				keyboardScreen.requestMainPadsRendering();
				break; // Only handle one pad press at a time
			}

			// Check if pressing transpose pads (row 3, positions 15-16)
			if (y == 3 && x >= 15 && x < 17 && x < kDisplayWidth) {
				if (x == 15) {
					// Down octave (red pad)
					displayState.keyboardScrollOffset -= 12; // Down one octave
					if (displayState.keyboardScrollOffset < 0) {
						displayState.keyboardScrollOffset = 0; // Don't go below C0
					}
					display->displayPopup("Keyboard -1 Oct");
				} else if (x == 16) {
					// Up octave (purple pad)
					displayState.keyboardScrollOffset += 12; // Up one octave
					if (displayState.keyboardScrollOffset > 96) {
						displayState.keyboardScrollOffset = 96; // Don't go above reasonable range
					}
					display->displayPopup("Keyboard +1 Oct");
				}

				// Update display
				if (display->haveOLED()) {
					renderUIsForOled();
				}
				keyboardScreen.requestMainPadsRendering();
				break;
			}

			// Check if pressing keyboard pads (rows 4-7)
			if (y >= 4 && y < 8) {
				uint16_t note = noteFromCoords(x, y);
				if (note < 128) {
					enableNote(note, velocity);
				}
			}
		}
	}

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
	// Only handle column controls - keep it simple like other keyboard layouts
	// This prevents interference with SELECT encoder preset selection
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}

	// Don't handle any other horizontal encoder actions
	// All arp controls are now handled by pads and Y encoder
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
	renderKeyboard(image); // Add keyboard in rows 4-7

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

	// Show octave count: all 8 octave pads dimly lit, with active ones bright
	RGB dimOctaveColor = RGB(0, 0, 40); // Dim blue for available octaves
	RGB brightOctaveColor = colours::blue; // Bright blue for active octaves

	// Show all 8 possible octaves (positions 4-11, but limit to display width)
	int32_t maxOctaveX = std::min((int32_t)(4 + 8), (int32_t)kDisplayWidth);
	for (int32_t x = 4; x < maxOctaveX; x++) {
		int32_t octaveNum = x - 4 + 1; // Octave 1-8
		if (octaveNum <= settings->numOctaves) {
			image[0][x] = brightOctaveColor; // Active octaves are bright
		} else {
			image[0][x] = dimOctaveColor; // Inactive octaves are dim
		}
	}

	// Show yellow rhythm toggle pads (top row, positions 13-16)
	RGB rhythmColor = (displayState.appliedRhythm > 0) ? colours::yellow : RGB(40, 40, 0); // Dim yellow when rhythm OFF
	for (int32_t x = 13; x < 16 && x < kDisplayWidth; x++) {
		image[0][x] = rhythmColor;
	}

	// Row 1 is now free for future features (old rhythm index removed)

	// Row 3: Show transpose controls (positions 15-16)
	if (kDisplayWidth > 15) {
		image[3][15] = colours::red;    // Down octave
	}
	if (kDisplayWidth > 16) {
		image[3][16] = colours::magenta; // Up octave (purple)
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

void KeyboardLayoutArpControl::renderKeyboard(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Render keyboard in rows 4-7 (adapted from IN-KEY layout)
	// Simple chromatic keyboard for now

	// Precreate list of all active notes
	bool activeNotes[128] = {false};
	for (uint8_t idx = 0; idx < currentNotesState.count; ++idx) {
		if (currentNotesState.notes[idx].note < 128) {
			activeNotes[currentNotesState.notes[idx].note] = true;
		}
	}

	// Render keyboard in rows 4-7 only
	for (int32_t y = 4; y < 8 && y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			uint16_t note = noteFromCoords(x, y);

			// Limit to valid MIDI range
			if (note >= 128) {
				image[y][x] = colours::black;
				continue;
			}

			// Color based on note type and state
			bool isActive = activeNotes[note];
			bool isRoot = (note % 12) == 0; // C notes
			bool isBlackKey = (note % 12 == 1 || note % 12 == 3 || note % 12 == 6 || note % 12 == 8 || note % 12 == 10);

			RGB color;
			if (isActive) {
				// Active notes are bright
				if (isRoot) {
					color = colours::red; // C notes are red when active
				} else if (isBlackKey) {
					color = colours::blue; // Black keys are blue when active
				} else {
					color = colours::white; // White keys are white when active
				}
			} else {
				// Inactive notes are dim
				if (isRoot) {
					color = RGB(40, 0, 0); // Dim red for C notes
				} else if (isBlackKey) {
					color = RGB(0, 0, 20); // Dim blue for black keys
				} else {
					color = RGB(10, 10, 10); // Dim white for white keys
				}
			}

			image[y][x] = color;
		}
	}
}


}; // namespace deluge::gui::ui::keyboard::layout

