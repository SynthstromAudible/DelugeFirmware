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
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "gui/menu_item/value_scaling.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/arpeggiator_rhythms.h"
#include "util/d_string.h"
#include "model/output.h"

namespace deluge::gui::ui::keyboard::layout {

// Constants
static constexpr int32_t kMaxRhythmPattern = 50;
static constexpr int32_t kMaxCVNote = 180;
static constexpr int32_t kMaxMIDINote = 128;

// Helper function to set parameters safely for different output types
void KeyboardLayoutArpControl::setParameterSafely(int32_t paramId, int32_t value, bool useUnsignedScaling) {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	OutputType outputType = getCurrentOutputType();

	if (outputType == OutputType::SYNTH) {
		// Use soundEditor.setup() for synth tracks (works properly)
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip) {
			UI* originalUI = getCurrentUI();

			// Set up sound editor context like the official menu
			if (soundEditor.setup(clip, nullptr, 0)) {
				// Now we're in sound editor context - use the official approach
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
				ModelStackWithAutoParam* modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(paramId);

				if (modelStackWithParam && modelStackWithParam->autoParam) {
					// Use appropriate scaling based on parameter type
					int32_t finalValue = useUnsignedScaling ? 
						computeFinalValueForUnsignedMenuItem(value) : 
						computeFinalValueForStandardMenuItem(value);
					modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(finalValue, modelStackWithParam);
				}

				// Exit sound editor context back to original UI
				originalUI->focusRegained();
			}
		}
	}
	else {
		// Use direct parameter setting for CV/MIDI tracks (avoids crash)
		// Use proper value scaling like the official menu system
		int32_t scaledValue = useUnsignedScaling ? 
			computeFinalValueForUnsignedMenuItem(value) : 
			computeFinalValueForStandardMenuItem(value);
		
		// Set the appropriate parameter based on paramId
		switch (paramId) {
			case modulation::params::UNPATCHED_ARP_SEQUENCE_LENGTH:
				settings->sequenceLength = scaledValue;
				break;
			case modulation::params::UNPATCHED_SPREAD_VELOCITY:
				settings->spreadVelocity = scaledValue;
				break;
			case modulation::params::UNPATCHED_ARP_GATE:
				settings->gate = scaledValue;
				break;
			case modulation::params::UNPATCHED_ARP_SPREAD_OCTAVE:
				settings->spreadOctave = scaledValue;
				break;
			case modulation::params::UNPATCHED_ARP_SPREAD_GATE:
				settings->spreadGate = scaledValue;
				break;
			case modulation::params::UNPATCHED_ARP_RHYTHM:
				settings->rhythm = scaledValue;
				break;
		}
	}
}

void KeyboardLayoutArpControl::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	// Clear current notes state
	currentNotesState = NotesState{};

	// Get arp settings once
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Process pad presses - only handle pads within kDisplayWidth (0-15)
	for (int32_t i = 0; i < kMaxNumKeyboardPadPresses; i++) {
		if (presses[i].active && presses[i].x < kDisplayWidth) {
			int32_t x = presses[i].x;
			int32_t y = presses[i].y;
			uint8_t velocity = 127; // Default velocity

			// Handle control pads first
			if (y == 0) {
				// Top row: Arp mode, octaves, and randomizer lock
				if (x >= 0 && x < 3) {
					handleArpMode(x, settings);
				}
				else if (x >= 4 && x < 12) {
					handleOctaves(x, settings);
				}
				else if (x == 15) {
					handleRandomizerLock();
				}
			}
			else if (y == 1) {
				// Row 1: Velocity spread and random octave
				if (x >= 0 && x < 8) {
					handleVelocitySpread(x, settings);
				}
				else if (x >= 8 && x < 16) {
					handleRandomOctave(x - 8, settings);
				}
			}
			else if (y == 2) {
				// Row 2: Gate and random gate
				if (x >= 0 && x < 8) {
					handleGate(x, settings);
				}
				else if (x >= 8 && x < 16) {
					handleRandomGate(x - 8, settings);
				}
			}
			else if (y == 3) {
				// Row 3: Sequence length, rhythm patterns, and transpose
				if (x >= 0 && x < 8) {
					handleSequenceLength(x, settings);
				}
				else if (x >= 14 && x < 16) {
					handleTranspose(x);
				}
			}
			else if (y >= 4 && y < 8) {
				// Rows 4-7: Keyboard
				handleKeyboard(x, y, velocity);
			}
		}
	}

	// Update display
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	keyboardScreen.requestMainPadsRendering();

	// Handle column controls (columns 16 & 17) - should be called last
	ColumnControlsKeyboard::evaluatePads(presses);
}

void KeyboardLayoutArpControl::handleArpMode(int32_t x, ArpeggiatorSettings* settings) {
	// Cycle through all arp presets
	int32_t currentPreset = static_cast<int32_t>(settings->preset);
	int32_t newPreset = currentPreset + 1;

	// Wrap around to OFF if we go past CUSTOM
	if (newPreset > static_cast<int32_t>(ArpPreset::CUSTOM)) {
		newPreset = static_cast<int32_t>(ArpPreset::OFF);
	}

	settings->preset = static_cast<ArpPreset>(newPreset);

	// Update all settings from the new preset
	settings->updateSettingsFromCurrentPreset();
	// Force arpeggiator to restart with new mode
	settings->flagForceArpRestart = true;

	// Show mode name in popup
	const char* modeName = KeyboardLayoutArpControl::getArpPresetDisplayName(settings->preset);
	display->displayPopup(modeName);

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleOctaves(int32_t x, ArpeggiatorSettings* settings) {
	// Direct octave control (1-8)
	int32_t newOctaves = x - 4 + 1;
	settings->numOctaves = newOctaves;
	// Force arpeggiator to restart with new octave count
	settings->flagForceArpRestart = false;
	display->displayPopup(("Octaves: " + std::to_string(newOctaves)).c_str());

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}


void KeyboardLayoutArpControl::handleSequenceLength(int32_t x, ArpeggiatorSettings* settings) {
	// Track the last touched sequence length pad for LED feedback
	lastTouchedSequenceLengthPad = x;

	// Direct sequence length control - each pad has its own value
	int32_t newLength = sequenceLengthValues[x];

	// Use helper function to set parameter safely
	setParameterSafely(modulation::params::UNPATCHED_ARP_SEQUENCE_LENGTH, newLength, true);

	// Display "OFF" for value 0, otherwise show the value
	if (newLength == 0) {
		display->displayPopup("Seq Length: OFF");
	} else {
		display->displayPopup(("Seq Length: " + std::to_string(newLength)).c_str());
	}

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleVelocitySpread(int32_t x, ArpeggiatorSettings* settings) {
	// Track the last touched velocity pad for LED feedback
	lastTouchedVelocityPad = x;

	// Direct velocity spread control - each pad has its own value
	int32_t newVelocity = velocitySpreadValues[x];

	// Use helper function to set parameter safely
	setParameterSafely(modulation::params::UNPATCHED_SPREAD_VELOCITY, newVelocity, true);

	// Display "OFF" for value 0, otherwise show the value
	if (newVelocity == 0) {
		display->displayPopup("Spread Velocity: OFF");
	} else {
		display->displayPopup(("Spread Velocity: " + std::to_string(newVelocity)).c_str());
	}

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleGate(int32_t x, ArpeggiatorSettings* settings) {
	// Track the last touched gate pad for LED feedback
	lastTouchedGatePad = x;

	// Direct gate control - each pad has its own value
	int32_t newGate = gateValues[x];

	// Use helper function to set parameter safely
	setParameterSafely(modulation::params::UNPATCHED_ARP_GATE, newGate, false);

	display->displayPopup(("Gate: " + std::to_string(newGate)).c_str());

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleRandomOctave(int32_t x, ArpeggiatorSettings* settings) {
	// Track the last touched random octave pad for LED feedback
	lastTouchedRandomOctavePad = x;

	// Direct random octave control - each pad has its own value
	int32_t newOctave = randomOctaveValues[x];

	// Use helper function to set parameter safely
	setParameterSafely(modulation::params::UNPATCHED_ARP_SPREAD_OCTAVE, newOctave, true);

	display->displayPopup(("Random Octave: " + std::to_string(newOctave)).c_str());

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleRandomGate(int32_t x, ArpeggiatorSettings* settings) {
	// Track the last touched random gate pad for LED feedback
	lastTouchedRandomGatePad = x;

	// Direct random gate control - each pad has its own value
	int32_t newGate = randomGateValues[x];

	// Use helper function to set parameter safely
	setParameterSafely(modulation::params::UNPATCHED_ARP_SPREAD_GATE, newGate, true);

	display->displayPopup(("Random Gate: " + std::to_string(newGate)).c_str());

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleRandomizerLock() {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Toggle randomizer lock
	settings->randomizerLock = !settings->randomizerLock;

	display->displayPopup(settings->randomizerLock ? "Randomizer Lock: ON" : "Randomizer Lock: OFF");

	// Force UI update
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleTranspose(int32_t x) {
	if (x == 14) {
		keyboardScrollOffset -= 12; // Down one octave
		display->displayPopup("Keyboard -1 Oct");
	} else if (x == 15) {
		keyboardScrollOffset += 12; // Up one octave
		display->displayPopup("Keyboard +1 Oct");
	}
}

void KeyboardLayoutArpControl::handleKeyboard(int32_t x, int32_t y, uint8_t velocity) {
	uint16_t note = noteFromCoords(x, y) + keyboardScrollOffset;

	// Check note range based on output type
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip && clip->output) {
		if (clip->output->type == OutputType::CV) {
			// CV tracks can handle wider note range - check voltage calculation limits
			// CV engine clamps voltage to 0-65535, which corresponds to roughly -24 to +120 semitones from C3
			// For safety, limit to a reasonable range that won't cause voltage overflow
			if (note <= kMaxCVNote) { // ~10 octaves from C3
				enableNote(note, velocity);
			}
		}
		else if (clip->output->type == OutputType::MIDI_OUT) {
			// MIDI tracks are limited to 0-127
			if (note < kMaxMIDINote) {
				enableNote(note, velocity);
			}
		}
		else {
			// Synth tracks use the standard keyboard range
			if (note < kHighestKeyboardNote) {
				enableNote(note, velocity);
			}
		}
	}
	else {
		// Fallback to standard range
		if (note < kHighestKeyboardNote) {
			enableNote(note, velocity);
		}
	}
}

void KeyboardLayoutArpControl::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Top row: Arp mode, octaves, and randomizer lock
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		if (x >= 0 && x < 3) {
			// Arp mode display
			image[0][x] = getArpModeColor(settings, x);
		}
		else if (x >= 4 && x < 12) {
			// Octave display
			image[0][x] = getOctaveColor(x - 4, settings->numOctaves);
		}
		else if (x == 15) {
			// Randomizer lock button
			image[0][x] = settings->randomizerLock ? colours::yellow : colours::yellow.adjust(32, 3);
		}
		else {
			// Unused pads are black
			image[0][x] = colours::black;
		}
	}

	// Row 1: Velocity spread and random octave
	for (int32_t x = 0; x < 8; x++) {
		image[1][x] = getVelocitySpreadColor(x);
	}
	for (int32_t x = 8; x < 16; x++) {
		image[1][x] = getRandomOctaveColor(x - 8);
	}

	// Row 2: Gate and random gate
	for (int32_t x = 0; x < 8; x++) {
		image[2][x] = getGateColor(x);
	}
	for (int32_t x = 8; x < 16; x++) {
		image[2][x] = getRandomGateColor(x - 8);
	}

	// Row 3: Sequence length, rhythm visualization, and transpose
	for (int32_t x = 0; x < 8; x++) {
		image[3][x] = getSequenceLengthColor(x);
	}

	// Rhythm pattern visualization on pads x8-x13
	for (int32_t x = 8; x < 14; x++) {
		image[3][x] = getRhythmPatternColor(x - 8);
	}

	image[3][14] = colours::red; // Down transpose
	image[3][15] = colours::purple; // Up transpose

	// Rows 4-7: Keyboard (stop at y=7, leave y=8-15 for control columns)
	for (int32_t y = 4; y < 8; y++) {
		for (int32_t x = 0; x < 15; x++) {
			image[y][x] = getKeyboardColor(x, y);
		}
	}

	// Rows 8-15: Control columns (y16, y17 when 0-indexed)
	for (int32_t y = 8; y < 16; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			// Control columns - will be handled by holding y16, y17
			image[y][x] = colours::black;
		}
	}
}

// Color functions
RGB KeyboardLayoutArpControl::getArpModeColor(ArpeggiatorSettings* settings, int32_t x) {
	switch (settings->preset) {
		case ArpPreset::OFF: return colours::red;
		case ArpPreset::UP:
			switch (x) {
				case 0: return colours::green;
				case 1: return colours::green;
				case 2: return colours::pink;
				default: return colours::green;
			}
		case ArpPreset::DOWN:
			switch (x) {
				case 0: return colours::pink;
				case 1: return colours::green;
				case 2: return colours::green;
				default: return colours::green;
			}
		case ArpPreset::BOTH:
			switch (x) {
				case 0: return colours::pink;
				case 1: return colours::green;
				case 2: return colours::pink;
				default: return colours::green;
			}
		case ArpPreset::RANDOM:
			switch (x) {
				case 0: return colours::pink;
				case 1: return colours::pink;
				case 2: return colours::pink;
				default: return colours::pink;
			}
		case ArpPreset::WALK: return colours::magenta;
		case ArpPreset::CUSTOM: return colours::white;
		default: return colours::red;
	}
}

RGB KeyboardLayoutArpControl::getOctaveColor(int32_t octave, int32_t currentOctaves) {
	return (octave < currentOctaves) ? colours::blue : RGB(0, 0, 40);
}


RGB KeyboardLayoutArpControl::getSequenceLengthColor(int32_t length) {
	// Highlight the last touched sequence length pad, dim all others
	if (lastTouchedSequenceLengthPad == length) {
		return colours::orange; // Bright orange for last touched pad
	} else {
		return colours::orange.adjust(32, 3); // Dim orange for other pads
	}
}

RGB KeyboardLayoutArpControl::getVelocitySpreadColor(int32_t spread) {
	// Highlight the last touched velocity pad, dim all others
	if (lastTouchedVelocityPad == spread) {
		return colours::cyan; // Bright cyan for last touched pad
	} else {
		return colours::cyan.adjust(32, 3); // Dim cyan for other pads
	}
}

RGB KeyboardLayoutArpControl::getGateColor(int32_t gate) {
	// Highlight the last touched gate pad, dim all others
	if (lastTouchedGatePad == gate) {
		return colours::green; // Bright green for last touched pad
	} else {
		return colours::green.adjust(32, 3); // Dim green for other pads
	}
}

RGB KeyboardLayoutArpControl::getRandomOctaveColor(int32_t octave) {
	// Highlight the last touched random octave pad, dim all others
	if (lastTouchedRandomOctavePad == octave) {
		return RGB(0, 100, 255); // Bright light blue for last touched pad
	} else {
		return RGB(0, 100, 255).adjust(32, 3); // Dim light blue for other pads
	}
}

RGB KeyboardLayoutArpControl::getRandomGateColor(int32_t gate) {
	// Highlight the last touched random gate pad, dim all others
	if (lastTouchedRandomGatePad == gate) {
		return colours::lime; // Bright lime for last touched pad
	} else {
		return colours::lime.adjust(32, 3); // Dim lime for other pads
	}
}

RGB KeyboardLayoutArpControl::getRhythmPatternColor(int32_t step) {
	// Show the currently selected rhythm pattern (not necessarily applied)
	if (displayState.currentRhythm == 0) {
		// Pattern 0 (all notes) - show all steps as dim white
		return RGB::monochrome(32);
	}

	// Clamp rhythm to valid range
	int32_t rhythmIndex = std::clamp(displayState.currentRhythm, static_cast<int32_t>(0), static_cast<int32_t>(kMaxRhythmPattern));

	// Get the rhythm pattern
	const ArpRhythm& pattern = arpRhythmPatterns[rhythmIndex];

	// Check if this step should be active
	if (step < pattern.length && pattern.steps[step]) {
		// Bright white for active steps, dimmer if not applied
		if (displayState.appliedRhythm == displayState.currentRhythm) {
			return RGB::monochrome(128); // Bright white when applied
		} else {
			return RGB::monochrome(60); // Dimmer white when not applied
		}
	} else {
		return RGB::monochrome(0); // Dim white for inactive steps
	}
}

void KeyboardLayoutArpControl::applyRhythmToArpSettings() {
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Check output type to determine which approach to use
	OutputType outputType = getCurrentOutputType();

	if (outputType == OutputType::SYNTH) {
		// Use soundEditor.setup() for synth tracks (works properly)
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (!clip) return;

		// Use the same approach as the keyboard screen for proper parameter setting
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
	else {
		// Use direct parameter setting for CV/MIDI tracks (avoids crash)
		// Use proper value scaling like the official menu system
		int32_t scaledValue = computeFinalValueForUnsignedMenuItem(displayState.appliedRhythm);
		settings->rhythm = scaledValue;
	}

}

void KeyboardLayoutArpControl::handleRhythmToggle() {
	// Toggle logic: apply current pattern or turn OFF
	if (displayState.appliedRhythm == 0) {
		// Turn ON: apply the currently selected pattern
		displayState.appliedRhythm = displayState.currentRhythm;
		display->displayPopup("Rhythm ON");
	} else {
		// Turn OFF: keep current pattern for next time
		displayState.appliedRhythm = 0;
		display->displayPopup("Rhythm OFF");
	}

	// Apply the change using our safe method
	applyRhythmToArpSettings();

	// Update display and pads
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	keyboardScreen.requestMainPadsRendering();
}

RGB KeyboardLayoutArpControl::getKeyboardColor(int32_t x, int32_t y) {
	// Get the note for this pad position
	auto note = noteFromCoords(x, y);

	// Calculate note within octave relative to root
	int32_t noteWithinOctave = (uint16_t)((note + kOctaveSize) - getRootNote()) % kOctaveSize;

	// Check if this note is currently pressed
	bool isPressed = false;
	for (int32_t i = 0; i < currentNotesState.count; i++) {
		if (currentNotesState.notes[i].note == note) {
			isPressed = true;
			break;
		}
	}

	// Get color source using getNoteColour function
	RGB colourSource = getNoteColour(note);

	// Full brightness and colour for active root note
	if (noteWithinOctave == 0 && isPressed) {
		return colourSource.adjust(255, 1);
	}
	// Full colour but less brightness for inactive root note
	else if (noteWithinOctave == 0) {
		return colourSource.adjust(255, 2);
	}
	// Toned down colour but high brightness for active scale note
	else if (isPressed) {
		return colourSource.adjust(127, 3);
	}
	// Dimly white for inactive scale notes
	else {
		return RGB::monochrome(1);
	}
}

// Essential functions
ArpeggiatorSettings* KeyboardLayoutArpControl::getArpSettings() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!clip) return nullptr;
	return &clip->arpSettings;
}

void KeyboardLayoutArpControl::handleVerticalEncoder(int32_t offset) {
	// Scroll through rhythm patterns (but don't apply until encoder is pressed)
	if (offset > 0) {
		displayState.currentRhythm++;
		if (displayState.currentRhythm > kMaxRhythmPattern) displayState.currentRhythm = 1;
	} else {
		displayState.currentRhythm--;
		if (displayState.currentRhythm < 1) displayState.currentRhythm = kMaxRhythmPattern;
	}

	// Display rhythm pattern name and status
	DEF_STACK_STRING_BUF(buffer, 30);
	if (displayState.currentRhythm == 0) {
		buffer.append("Rhythm: None");
	} else {
		buffer.append("Rhythm: ");
		buffer.append(arpRhythmPatternNames[displayState.currentRhythm]);
		if (displayState.appliedRhythm == 0) {
			buffer.append(" (OFF)");
		} else {
			buffer.append(" (ON)");
		}
	}
	display->displayPopup(buffer.c_str());

	// Update OLED display to show the rhythm pattern
	if (display->haveOLED()) {
		renderUIsForOled();
	}

	// Update pads to show the rhythm pattern visualization
	keyboardScreen.requestMainPadsRendering();
}

void KeyboardLayoutArpControl::handleHorizontalEncoder(int32_t offset, bool shiftEnabled, PressedPad presses[kMaxNumKeyboardPadPresses], bool encoderPressed) {

	// Check if column controls are handling the encoder
	if (horizontalEncoderHandledByColumns(offset, shiftEnabled)) {
		return;
	}

	// Arp preset control
	ArpeggiatorSettings* settings = getArpSettings();
	if (!settings) return;

	// Only change preset if offset is non-zero (avoid display refresh on select encoder)
	if (offset != 0) {
		// Normal horizontal: change arp preset (not rate)
		int32_t newPreset = static_cast<int32_t>(settings->preset) + offset;
		newPreset = std::clamp(newPreset, static_cast<int32_t>(0), static_cast<int32_t>(ArpPreset::CUSTOM));
		settings->preset = static_cast<ArpPreset>(newPreset);

		// Update settings from preset to enable arpeggiator
		settings->updateSettingsFromCurrentPreset();

		// Force arpeggiator to restart so it picks up the new preset immediately
		settings->flagForceArpRestart = true;

		display->displayPopup(KeyboardLayoutArpControl::getArpPresetDisplayName(settings->preset));

		// Display update: Handle both OLED and 7-segment
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		// 7-segment display is already handled by displayPopup()

		// Pad update: Only because arp status changed
		keyboardScreen.requestMainPadsRendering();
	}
}

void KeyboardLayoutArpControl::precalculate() {
	// No precalculation needed for this layout
}

void KeyboardLayoutArpControl::updateAnimation() {
	// No animation updates needed for this layout
}

void KeyboardLayoutArpControl::updateDisplay() {
	// No display updates needed for this layout
}

void KeyboardLayoutArpControl::updatePadLEDsDirect() {
	// No direct pad LED updates needed for this layout
}

void KeyboardLayoutArpControl::updatePlaybackProgressBar() {
	// No progress bar updates needed for this layout
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

} // namespace deluge::gui::ui::keyboard::layout
