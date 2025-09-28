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
#include "model/sync.h"
#include "util/d_string.h"
#include "playback/playback_handler.h"
#include "util/functions.h"

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutArpControl::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Clear active notes for now

	// Cache settings and track changes for batched updates
	ArpeggiatorSettings* settings = getArpSettings();
	bool controlsChanged = false;

	// PHASE 1: Handle control pads (non-keyboard) - process all controls first
	for (int32_t i = 0; i < kMaxNumKeyboardPadPresses; i++) {
		if (presses[i].active) {
			int32_t x = presses[i].x;
			int32_t y = presses[i].y;

			// Skip keyboard area (rows 4-7) in this phase
			if (y >= 4 && y < 8) continue;

			// Optimized control detection - consolidated handler
			bool controlHandled = handleControlPad(x, y, settings, controlsChanged);
			if (controlHandled) continue; // Move to next pad if control was handled

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
					controlsChanged = true;
				}
				controlHandled = true;
			}

			// Check if pressing yellow rhythm pads (top row, positions 12-14)
			if (y == 0 && x >= 12 && x < 15 && x < kDisplayWidth) {
				// Toggle rhythm on/off
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
			if (y == 3 && x >= 14 && x < 16 && x < kDisplayWidth) {
				if (x == 14) {
					// Down octave (red pad)
					displayState.keyboardScrollOffset -= 12; // Down one octave
					if (displayState.keyboardScrollOffset < -36) {
						displayState.keyboardScrollOffset = -36; // Don't go below C-2 (note 0)
					}
					display->displayPopup("Keyboard -1 Oct");
				} else if (x == 15) {
					// Up octave (purple pad)
					displayState.keyboardScrollOffset += 12; // Up one octave
					if (displayState.keyboardScrollOffset > 48) {
						displayState.keyboardScrollOffset = 48; // Don't go above C8 (note 84+36=120)
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

			// Check if pressing gate length pads (row 3, positions 0-7)
			if (y == 3 && x >= 0 && x < 8) {
				ArpeggiatorSettings* settings = getArpSettings();
				if (settings) {
					// Gate length control (1-8 = 12.5% to 100%)
					int32_t gateIndex = x + 1; // 1-8
					uint32_t gateLength = (gateIndex * kMaxMenuValue) / 8; // Scale to parameter range
					settings->gate = gateLength;

					// Force arpeggiator to restart with new gate
					settings->flagForceArpRestart = true;

					display->displayPopup(("Gate: " + std::to_string((gateIndex * 100) / 8) + "%").c_str());

					// Update display
					if (display->haveOLED()) {
						renderUIsForOled();
					}
					keyboardScreen.requestMainPadsRendering();
				}
			}

			// Check if pressing velocity spread pads (row 3, positions 8-13)
			else if (y == 3 && x >= 8 && x < 14) {
				ArpeggiatorSettings* settings = getArpSettings();
				if (settings) {
					// Velocity spread control (0-100%)
					int32_t spreadIndex = x - 8 + 1; // 1-6
					uint32_t spreadAmount = (spreadIndex * kMaxMenuValue) / 6; // Scale to parameter range
					settings->spreadVelocity = spreadAmount;

					// Force arpeggiator to restart with new spread
					settings->flagForceArpRestart = true;

					display->displayPopup(("Vel Spread: " + std::to_string((spreadIndex * 100) / 6) + "%").c_str());

					// Update display
					if (display->haveOLED()) {
						renderUIsForOled();
					}
					keyboardScreen.requestMainPadsRendering();
				}
			}

			// Check if pressing sequence length pads (row 1, positions 0-15)
			else if (y == 1 && x < kDisplayWidth) {
				ArpeggiatorSettings* settings = getArpSettings();
				if (settings) {
					// Set custom sequence length (1-16 steps)
					int32_t newLength = x + 1; // x=0 gives length=1, x=15 gives length=16
					settings->sequenceLength = (newLength * kMaxMenuValue) / 16; // Scale to parameter range

					// Force arpeggiator to restart with new length
					settings->flagForceArpRestart = true;

					display->displayPopup(("Seq Length: " + std::to_string(newLength)).c_str());

					// Update display
					if (display->haveOLED()) {
						renderUIsForOled();
					}
					keyboardScreen.requestMainPadsRendering();
				}
			}


			// Check if pressing keyboard pads (rows 4-7)
			else if (y >= 4 && y < 8) {
				uint16_t note = noteFromCoords(x, y);
				if (note < 128) {
					enableNote(note, velocity);
				}
			}
		}
	}

	// PHASE 2: Handle keyboard pads (rows 4-7) - separate phase for better performance
	for (int32_t i = 0; i < kMaxNumKeyboardPadPresses; i++) {
		if (presses[i].active) {
			int32_t x = presses[i].x;
			int32_t y = presses[i].y;

			// Only handle keyboard area in this phase
			if (y >= 4 && y < 8) {
				uint16_t note = noteFromCoords(x, y);
				if (note < 128) {
					enableNote(note, velocity);
				}
			}
		}
	}

	// Note: Latch functionality removed - requires complex note-off override system

	// Note: Column controls removed to prevent interference with arp controls

	// SINGLE UI REFRESH: Only refresh once at the end if controls changed
	if (controlsChanged) {
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		keyboardScreen.requestMainPadsRendering();
	}
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
	// First check if column controls should handle this
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}

	// Use horizontal encoder for arp sync rate control
	ArpeggiatorSettings* settings = getArpSettings();
	if (settings && offset != 0) {
		// Control sync level (1-9: WHOLE to 256TH notes)
		int32_t newSyncLevel = static_cast<int32_t>(settings->syncLevel) + offset;
		newSyncLevel = std::clamp(newSyncLevel, (int32_t)1, (int32_t)9); // Valid range 1-9
		settings->syncLevel = static_cast<SyncLevel>(newSyncLevel);

		// Force arpeggiator to restart with new sync rate
		settings->flagForceArpRestart = true;

		// Show sync rate name using official function
		char syncNameBuffer[30];
		StringBuf syncNameStr(syncNameBuffer, sizeof(syncNameBuffer));
		syncValueToString(newSyncLevel, syncNameStr, currentSong->getInputTickMagnitude());
		display->displayPopup(syncNameStr.c_str());

		// Update display
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		keyboardScreen.requestMainPadsRendering();
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

	// Create horizontal progress bar - on top row (row 7)
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

	// Always show the currently selected rhythm pattern (for preview)
	int32_t patternIndex = displayState.currentRhythm; // Use our display state, not the applied rhythm
	if (patternIndex < 1 || patternIndex > 50) {
		patternIndex = 1; // Default to pattern 1 if invalid
	}
	const ArpRhythm& pattern = arpRhythmPatterns[patternIndex];

	// Display rhythm pattern in the middle rows (rows 2-3) - avoid keyboard area
	const int32_t patternStartRow = 2;
	const int32_t maxStepsPerRow = kDisplayWidth;

	// Only show current step highlighting when actually playing and rhythm is applied
	int32_t currentStep = (displayState.appliedRhythm > 0) ? getCurrentRhythmStep() : -1;

	for (int32_t step = 0; step < pattern.length && step < maxStepsPerRow * 2; step++) {
		int32_t x = step % maxStepsPerRow;
		int32_t y = patternStartRow + (step / maxStepsPerRow);

		if (y >= 4) break; // Don't overlap with keyboard (starts at row 4)

		bool isActive = pattern.steps[step];
		bool isCurrent = (step == currentStep);

		RGB stepColor = getStepColor(isActive, isCurrent);
		// Dim the pattern when rhythm is not applied (preview mode)
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

	// Row 1: Show sequence length - all pads visible like octaves
	int32_t currentSeqLength = (settings->sequenceLength * 16) / kMaxMenuValue; // Convert back to 1-16 range
	if (currentSeqLength < 1) currentSeqLength = 1;
	if (currentSeqLength > 16) currentSeqLength = 16;

	RGB brightSeqColor = colours::cyan; // Bright cyan for active length
	RGB dimSeqColor = RGB(0, 40, 40); // Dim cyan for available positions

	// Show all 16 sequence length positions
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		int32_t lengthNum = x + 1; // Length 1-16
		if (lengthNum <= currentSeqLength) {
			image[1][x] = brightSeqColor; // Active length positions are bright
		} else {
			image[1][x] = dimSeqColor; // Inactive positions are dim
		}
	}

	// Row 3: Show gate length (0-7), velocity spread (8-13), and transpose controls (14-15)

	// Gate length visualization (positions 0-7) - all pads visible like octaves
	int32_t currentGate = (settings->gate * 8) / kMaxMenuValue; // Convert to 1-8 range
	if (currentGate < 1) currentGate = 1;
	if (currentGate > 8) currentGate = 8;

	RGB brightGateColor = colours::orange; // Bright orange for active gate
	RGB dimGateColor = RGB(40, 20, 0); // Dim orange for available positions

	// Show all 8 gate length positions
	for (int32_t x = 0; x < 8; x++) {
		int32_t gateNum = x + 1; // Gate 1-8
		if (gateNum <= currentGate) {
			image[3][x] = brightGateColor; // Active gate positions are bright
		} else {
			image[3][x] = dimGateColor; // Inactive positions are dim
		}
	}

	// Velocity spread visualization (positions 8-13) - all pads visible
	int32_t currentSpread = (settings->spreadVelocity * 6) / kMaxMenuValue; // Convert to 1-6 range
	if (currentSpread < 1) currentSpread = 1;
	if (currentSpread > 6) currentSpread = 6;

	RGB brightSpreadColor = colours::pink; // Bright pink for active spread
	RGB dimSpreadColor = RGB(40, 0, 20); // Dim pink for available positions

	// Show all 6 velocity spread positions
	for (int32_t x = 8; x < 14; x++) {
		int32_t spreadNum = x - 8 + 1; // Spread 1-6
		if (spreadNum <= currentSpread) {
			image[3][x] = brightSpreadColor; // Active spread positions are bright
		} else {
			image[3][x] = dimSpreadColor; // Inactive positions are dim
		}
	}

	// Row 2: Position 15 free for future features

	// Transpose controls (positions 14-15)
	if (kDisplayWidth > 14) {
		image[3][14] = colours::red;    // Down octave
	}
	if (kDisplayWidth > 15) {
		image[3][15] = colours::magenta; // Up octave (purple)
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

void KeyboardLayoutArpControl::updateArpAndRefreshUI(ArpeggiatorSettings* settings, const char* popupMessage) {
	if (!settings) return;

	// Force arpeggiator restart to apply changes
	settings->flagForceArpRestart = true;

	// Show popup message (this is immediate and doesn't conflict)
	if (popupMessage) {
		display->displayPopup(popupMessage);
	}

	// NOTE: UI refresh will be done at end of evaluatePads() for better performance
}

bool KeyboardLayoutArpControl::handleControlPad(int32_t x, int32_t y, ArpeggiatorSettings* settings, bool& controlsChanged) {
	if (!settings) return false;

	// OPTIMIZED CONTROL DETECTION: Check most common controls first

	// Row 0 - Main controls (most frequently used)
	if (y == 0) {
		// Green pads (0-2): Arp mode cycling
		if (x >= 0 && x < 3) {
			const char* modeMessage = nullptr;
			switch (settings->preset) {
				case ArpPreset::OFF: settings->preset = ArpPreset::UP; modeMessage = "Arp UP"; break;
				case ArpPreset::UP: settings->preset = ArpPreset::DOWN; modeMessage = "Arp DOWN"; break;
				case ArpPreset::DOWN: settings->preset = ArpPreset::BOTH; modeMessage = "Arp UP&DOWN"; break;
				case ArpPreset::BOTH: settings->preset = ArpPreset::RANDOM; modeMessage = "Arp RANDOM"; break;
				default: settings->preset = ArpPreset::OFF; modeMessage = "Arp OFF"; break;
			}
			settings->updateSettingsFromCurrentPreset();
			settings->flagForceArpRestart = true;
			display->displayPopup(modeMessage);
			controlsChanged = true;
			return true;
		}

		// Blue pads (4-11): Octave count
		if (x >= 4 && x < 12) {
			int32_t newOctaves = x - 4 + 1;
			settings->numOctaves = newOctaves;
			settings->flagForceArpRestart = true;
			display->displayPopup(("Octaves: " + std::to_string(newOctaves)).c_str());
			controlsChanged = true;
			return true;
		}

		// Yellow pads (13-15): Rhythm toggle
		if (x >= 13 && x < 16) {
			// Toggle rhythm on/off
			if (displayState.appliedRhythm == 0) {
				displayState.appliedRhythm = displayState.currentRhythm;
				display->displayPopup("Rhythm ON");
			} else {
				displayState.appliedRhythm = 0;
				display->displayPopup("Rhythm OFF");
			}

			// Apply rhythm change safely
			if (settings->syncLevel == 0) {
				settings->syncLevel = (SyncLevel)(8 - currentSong->insideWorldTickMagnitude - currentSong->insideWorldTickMagnitudeOffsetFromBPM);
			}
			settings->rhythm = (displayState.appliedRhythm > 0) ? displayState.appliedRhythm : 1;
			settings->flagForceArpRestart = true;
			controlsChanged = true;
			return true;
		}
	}

	// Row 1 - Sequence length
	if (y == 1 && x < kDisplayWidth) {
		int32_t newLength = x + 1;
		settings->sequenceLength = (newLength * kMaxMenuValue) / 16;
		settings->flagForceArpRestart = true;
		display->displayPopup(("Seq Length: " + std::to_string(newLength)).c_str());
		controlsChanged = true;
		return true;
	}

	// Row 2 - Position 15 free for future features (latch too complex for current implementation)

	// Row 3 - Performance controls
	if (y == 3) {
		// Gate length (0-7)
		if (x >= 0 && x < 8) {
			int32_t gateIndex = x + 1;
			settings->gate = (gateIndex * kMaxMenuValue) / 8;
			settings->flagForceArpRestart = true;
			display->displayPopup(("Gate: " + std::to_string((gateIndex * 100) / 8) + "%").c_str());
			controlsChanged = true;
			return true;
		}

		// Velocity spread (8-13)
		if (x >= 8 && x < 14) {
			int32_t spreadIndex = x - 8 + 1;
			settings->spreadVelocity = (spreadIndex * kMaxMenuValue) / 6;
			settings->flagForceArpRestart = true;
			display->displayPopup(("Vel Spread: " + std::to_string((spreadIndex * 100) / 6) + "%").c_str());
			controlsChanged = true;
			return true;
		}

		// Transpose controls (14-15)
		if (x == 14) {
			displayState.keyboardScrollOffset -= 12;
			if (displayState.keyboardScrollOffset < -36) {
				displayState.keyboardScrollOffset = -36;
			}
			display->displayPopup("Keyboard -1 Oct");
			controlsChanged = true;
			return true;
		}
		if (x == 15) {
			displayState.keyboardScrollOffset += 12;
			if (displayState.keyboardScrollOffset > 48) {
				displayState.keyboardScrollOffset = 48;
			}
			display->displayPopup("Keyboard +1 Oct");
			controlsChanged = true;
			return true;
		}
	}

	return false; // No control handled
}

void KeyboardLayoutArpControl::renderKeyboard(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// Render simple chromatic keyboard in rows 4-7
	
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
			auto padIndex = padIndexFromCoords(x, y - 4); // Adjust for keyboard starting at row 4
			auto note = noteFromPadIndex(padIndex);
			
			// Limit to valid MIDI range
			if (note >= 128) {
				image[y][x] = colours::black;
				continue;
			}
			
			// Simple chromatic coloring
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

