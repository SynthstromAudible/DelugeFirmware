/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "model/clip/sequencer/sequencer_mode.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include "util/cfunctions.h"
#include "definitions_cxx.hpp"
#include <cstring>

using deluge::hid::Display;

namespace deluge::model::clip::sequencer {

// ========== CONTROL COLUMN DEFAULT IMPLEMENTATIONS ==========

CombinedEffects SequencerMode::getCombinedEffects() const {
	// Start with base control values
	CombinedEffects effects;
	effects.clockDivider = baseClockDivider_;
	effects.octaveShift = baseOctaveShift_;
	effects.transpose = baseTranspose_;
	effects.direction = baseDirection_;
	effects.sceneIndex = -1; // Scene is always from pads only

	// Get pad effects (this may override base values)
	CombinedEffects padEffects = controlColumnState_.getCombinedEffects();

	// Merge: pad effects override base values if they're active
	// For clock/octave/transpose/direction, if any pad is active, it overrides the base
	// We check if pad effects differ from defaults to know if they're active
	if (padEffects.clockDivider != 1) {
		effects.clockDivider = padEffects.clockDivider;
	}
	if (padEffects.octaveShift != 0) {
		effects.octaveShift = padEffects.octaveShift;
	}
	if (padEffects.transpose != 0) {
		effects.transpose = padEffects.transpose;
	}
	if (padEffects.direction != 0) {
		effects.direction = padEffects.direction;
	}
	// Scene is always from pads
	effects.sceneIndex = padEffects.sceneIndex;

	return effects;
}

bool SequencerMode::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	// Safety check
	if (!image) {
		return false;
	}

	// Render control columns on sidebar (x16-x17, split into 4 groups)
	controlColumnState_.render(image, occupancyMask);

	return true; // We handled the sidebar
}

bool SequencerMode::handlePadPress(int32_t x, int32_t y, int32_t velocity) {
	// Track control column pad holds (for encoder routing)
	if (x == kDisplayWidth || x == (kDisplayWidth + 1)) {
		if (velocity > 0) {
			// Pad pressed - track it
			heldControlColumnX_ = x;
			heldControlColumnY_ = y;
		}
		else if (heldControlColumnX_ == x && heldControlColumnY_ == y) {
			// Pad released - clear tracking
			heldControlColumnX_ = -1;
			heldControlColumnY_ = -1;
		}
	}

	// Delegate to control column state, passing 'this' for scene capture/recall
	return controlColumnState_.handlePad(x, y, velocity, this);
}

bool SequencerMode::handleHorizontalEncoder(int32_t offset, bool encoderPressed) {
	// Only handle encoder if a control column pad is held
	if (heldControlColumnX_ < 0 || heldControlColumnY_ < 0) {
		return false; // No control column pad held
	}

	// Delegate to control column state, passing mode for compatibility checking
	return controlColumnState_.handleHorizontalEncoder(heldControlColumnX_, heldControlColumnY_, offset, this);
}

bool SequencerMode::handleVerticalEncoder(int32_t offset) {
	// If a control column pad is held, use vertical encoder to adjust value
	if (heldControlColumnX_ >= 0 && heldControlColumnY_ >= 0) {
		return controlColumnState_.handleVerticalEncoder(heldControlColumnX_, heldControlColumnY_, offset);
	}

	// Otherwise, delegate to mode-specific implementation
	return handleModeSpecificVerticalEncoder(offset);
}

bool SequencerMode::handleVerticalEncoderButton() {
	// If a control column pad is held, toggle momentary/toggle mode
	if (heldControlColumnX_ >= 0 && heldControlColumnY_ >= 0) {
		return controlColumnState_.handleVerticalEncoderButton(heldControlColumnX_, heldControlColumnY_);
	}

	return false;
}

// ========== HELPER IMPLEMENTATIONS ==========

int32_t SequencerMode::getScaleNotes(void* modelStackPtr, int32_t* noteArray, int32_t maxNotes, int32_t octaveRange,
                                     int32_t baseOctave) {
	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());

	int32_t noteCount = 0;

	if (clip->inScaleMode) {
		// In scale mode - fill array with scale notes
		MusicalKey& key = modelStack->song->key;
		int32_t scaleSize = key.modeNotes.count();

		// Fill array with scale notes across octaves
		for (int32_t octave = 0; octave < octaveRange && noteCount < maxNotes; octave++) {
			for (int32_t degree = 0; degree < scaleSize && noteCount < maxNotes; degree++) {
				int32_t semitone = key.modeNotes[degree];
				int32_t note = key.rootNote + ((baseOctave + octave) * 12) + semitone;

				// Only add if in MIDI range
				if (note >= 0 && note <= 127) {
					noteArray[noteCount++] = note;
				}
			}
		}
	}
	else {
		// Chromatic mode - all 12 notes per octave
		for (int32_t octave = 0; octave < octaveRange && noteCount < maxNotes; octave++) {
			for (int32_t semitone = 0; semitone < 12 && noteCount < maxNotes; semitone++) {
				int32_t note = 60 + ((baseOctave + octave) * 12) + semitone;

				// Only add if in MIDI range
				if (note >= 0 && note <= 127) {
					noteArray[noteCount++] = note;
				}
			}
		}
	}

	return noteCount;
}

uint8_t SequencerMode::applyVelocitySpread(uint8_t baseVelocity, int32_t spread) {
	if (spread == 0) {
		return baseVelocity;
	}

	// Random variation ± spread amount
	int32_t variation = (getRandom255() % (spread * 2 + 1)) - spread;
	int32_t newVelocity = baseVelocity + variation;

	// Clamp to valid MIDI velocity range
	if (newVelocity < 1)
		newVelocity = 1;
	if (newVelocity > 127)
		newVelocity = 127;

	return static_cast<uint8_t>(newVelocity);
}

bool SequencerMode::shouldPlayBasedOnProbability(int32_t probability) {
	if (probability >= 100) {
		return true; // Always play
	}
	if (probability <= 0) {
		return false; // Never play
	}

	// Random check: probability is 0-100
	return (getRandom255() % 100) < probability;
}

void SequencerMode::playNote(void* modelStackPtr, int32_t noteCode, uint8_t velocity, int32_t length) {
	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	MelodicInstrument* instrument = static_cast<MelodicInstrument*>(clip->output);

	int16_t mpeValues[kNumExpressionDimensions];
	memset(mpeValues, 0, sizeof(mpeValues));

	char newModelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = setupModelStackWithThreeMainThingsButNoNoteRow(
	    newModelStackMemory, modelStack->song, instrument->toModControllable(), clip, &clip->paramManager);

	instrument->sendNote(modelStackWithThreeMainThings, true, noteCode, mpeValues, MIDI_CHANNEL_NONE, velocity, length,
	                     0);
}

void SequencerMode::stopNote(void* modelStackPtr, int32_t noteCode) {
	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	MelodicInstrument* instrument = static_cast<MelodicInstrument*>(clip->output);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = setupModelStackWithThreeMainThingsButNoNoteRow(
	    modelStackMemory, modelStack->song, instrument->toModControllable(), clip, &clip->paramManager);

	instrument->sendNote(modelStackWithThreeMainThings, false, noteCode, nullptr, MIDI_CHANNEL_NONE, 64, 0, 0);
}

// ========== DISPLAY HELPERS ==========

void SequencerMode::displayVelocity(uint8_t velocity) {
	extern deluge::hid::Display* display;
	if (::display) {
		char buffer[16]; // "Velocity: 127" = 13 chars
		memcpy(buffer, "Velocity: ", 10);
		intToString(velocity, &buffer[10], 1);
		::display->displayPopup(buffer);
	}
}

void SequencerMode::displayGateLength(uint8_t gateLength) {
	extern deluge::hid::Display* display;
	if (::display) {
		char buffer[16]; // "Gate length: 100" = 16 chars
		memcpy(buffer, "Gate length: ", 13);
		intToString(gateLength, &buffer[13], 1);
		::display->displayPopup(buffer);
	}
}

void SequencerMode::displayProbability(uint8_t probability) {
	extern deluge::hid::Display* display;
	if (::display) {
		// probability is 0-20 (representing 0-100% in 5% increments)
		uint8_t probabilityPercent = probability * 5;
		if (::display->haveOLED()) {
			char buffer[29];
			sprintf(buffer, "Probability %d%%", probabilityPercent);
			::display->popupText(buffer, PopupType::PROBABILITY);
		}
		else {
			char buffer[5];
			intToString(probabilityPercent, buffer);
			::display->displayPopup(buffer, 0, true, 255, 1, PopupType::PROBABILITY);
		}
	}
}

void SequencerMode::displayIterance(Iterance iterance) {
	extern deluge::hid::Display* display;
	if (::display) {
		int32_t iterancePreset = iterance.toPresetIndex();

		if (::display->haveOLED()) {
			char buffer[29];
			if (iterancePreset == kDefaultIterancePreset) {
				strcpy(buffer, "Iterance: OFF");
			}
			else if (iterancePreset == kCustomIterancePreset) {
				strcpy(buffer, "Iterance: CUSTOM");
			}
			else {
				Iterance iteranceValue = iterancePresets[iterancePreset - 1];
				int32_t i = iteranceValue.divisor;
				for (; i >= 0; i--) {
					// try to find which iteration step index is active
					if (iteranceValue.iteranceStep[i]) {
						break;
					}
				}
				sprintf(buffer, "Iterance: %d of %d", i + 1, iteranceValue.divisor);
			}
			::display->popupText(buffer, PopupType::ITERANCE);
		}
		else {
			char buffer[7]; // "CUSTOM" + null terminator
			if (iterancePreset == kDefaultIterancePreset) {
				strcpy(buffer, "OFF");
			}
			else if (iterancePreset == kCustomIterancePreset) {
				strcpy(buffer, "CUSTOM");
			}
			else {
				Iterance iteranceValue = iterancePresets[iterancePreset - 1];
				int32_t i = iteranceValue.divisor;
				for (; i >= 0; i--) {
					// try to find which iteration step index is active
					if (iteranceValue.iteranceStep[i]) {
						break;
					}
				}
				sprintf(buffer, "%dof%d", i + 1, iteranceValue.divisor);
			}
			::display->displayPopup(buffer, 0, true, 255, 1, PopupType::ITERANCE);
		}
	}
}

void SequencerMode::renderPlaybackPosition(RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                           int32_t imageWidth, int32_t absolutePlaybackPos, int32_t totalLength,
                                           RGB color, bool enabled) {
	if (!enabled || totalLength == 0) {
		return;
	}

	// Calculate which pad on y7 should be lit (0-15)
	int32_t positionInPattern = absolutePlaybackPos % totalLength;
	int32_t padX = (positionInPattern * kDisplayWidth) / totalLength;

	// Clamp to valid range
	if (padX < 0)
		padX = 0;
	if (padX >= kDisplayWidth)
		padX = kDisplayWidth - 1;

	// Light up the pad on y7
	int32_t y = 7;
	image[y * imageWidth + padX] = color;
	if (occupancyMask) {
		occupancyMask[y][padX] = 64;
	}
}


} // namespace deluge::model::clip::sequencer
