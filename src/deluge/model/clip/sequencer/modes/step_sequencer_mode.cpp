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

#include "model/clip/sequencer/modes/step_sequencer_mode.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/scale/musical_key.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

namespace deluge::model::clip::sequencer::modes {

// Row masks for UI refresh
constexpr uint32_t kGateRow = (1 << 0);
constexpr uint32_t kOctaveRows = (1 << 1) | (1 << 2);
constexpr uint32_t kNoteRows = (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7);
constexpr uint32_t kAllRows = 0xFFFFFFFF;

void StepSequencerMode::initialize() {
	initialized_ = true;
	currentStep_ = 0;
	activeNoteCode_ = -1;
	ticksPerSixteenthNote_ = 0;
	lastAbsolutePlaybackPos_ = 0;

	// Clear the white progress column from normal clip mode
	// (set all tick squares to 255 = not displayed)
	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight); // 255 = not displayed
	memset(colours, 0, kDisplayHeight);
	PadLEDs::setTickSquares(tickSquares, colours);

	// Only initialize noteScrollOffset if not already set (preserve loaded data)
	if (noteScrollOffset_ == 0) {
		noteScrollOffset_ = 0;
	}

	// Update scale notes first
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
	updateScaleNotes(modelStackWithTimelineCounter);

	// Only initialize with defaults if current data matches the exact default pattern
	if (isDefaultPattern()) {
		setDefaultPattern();
	}
}

void StepSequencerMode::cleanup() {
	// Stop any playing notes before cleanup
	if (activeNoteCode_ >= 0) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
		stopNote(modelStackWithTimelineCounter, activeNoteCode_);
	}

	// Reset all state to prevent leaks
	initialized_ = false;
	currentStep_ = 0;
	activeNoteCode_ = -1;
	numScaleNotes_ = 0;
	noteScrollOffset_ = 0;
	lastAbsolutePlaybackPos_ = 0;
	ticksPerSixteenthNote_ = 0;
	pingPongDirection_ = 1;

	// Clear scale notes array (defensive cleanup)
	memset(scaleNotes_, 0, sizeof(scaleNotes_));
}

void StepSequencerMode::updateScaleNotes(void* modelStackPtr) {
	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	Song* song = modelStack->song;
	if (!song) {
		numScaleNotes_ = 0;
		return;
	}

	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());

	if (!clip || !clip->inScaleMode) {
		// Chromatic mode - all 12 notes
		numScaleNotes_ = 12;
		for (int32_t i = 0; i < 12; i++) {
			scaleNotes_[i] = i; // 0-11 (C, C#, D, D#, E, F, F#, G, G#, A, A#, B)
		}
	}
	else {
		// Scale mode - get notes from the scale (just degrees 0-11)
		NoteSet modeNotes = song->key.modeNotes;

		numScaleNotes_ = 0;
		for (int32_t i = 0; i < 12; i++) {
			if (modeNotes.has(i)) {
				scaleNotes_[numScaleNotes_++] = i;
			}
		}
	}

	// Clamp existing note indices to valid range
	if (numScaleNotes_ > 0) {
		for (int32_t i = 0; i < kNumSteps; i++) {
			if (steps_[i].noteIndex >= numScaleNotes_) {
				steps_[i].noteIndex = 0;
			}
		}
	}

	// Reset scroll if it's now out of range
	int32_t maxScroll = (static_cast<int32_t>(numScaleNotes_) > 5) ? (static_cast<int32_t>(numScaleNotes_) - 5) : 0;
	if (static_cast<int32_t>(noteScrollOffset_) > maxScroll) {
		noteScrollOffset_ = static_cast<uint8_t>(maxScroll);
	}
}

void StepSequencerMode::cycleGateType(int32_t step) {
	if (step < 0 || step >= kNumSteps)
		return;

	switch (steps_[step].gateType) {
	case GateType::OFF:
		steps_[step].gateType = GateType::ON;
		break;
	case GateType::ON:
		steps_[step].gateType = GateType::SKIP;
		break;
	case GateType::SKIP:
		steps_[step].gateType = GateType::OFF;
		break;
	}
}

void StepSequencerMode::adjustOctave(int32_t step, int32_t delta) {
	if (step < 0 || step >= kNumSteps)
		return;

	steps_[step].octave += delta;
	// Clamp to -3 to +3 range
	if (steps_[step].octave < -3)
		steps_[step].octave = -3;
	if (steps_[step].octave > 3)
		steps_[step].octave = 3;
}

void StepSequencerMode::setNoteIndex(int32_t step, int32_t noteIndex) {
	if (step < 0 || step >= kNumSteps)
		return;
	if (noteIndex < 0 || noteIndex >= numScaleNotes_)
		return;

	steps_[step].noteIndex = noteIndex;
}

int32_t StepSequencerMode::calculateNoteCode(const Step& step, const CombinedEffects& effects) const {
	if (numScaleNotes_ == 0)
		return 60; // Default to middle C

	Song* song = currentSong;
	if (!song)
		return 60;

	int32_t rootNote = song->key.rootNote;

	// Apply transpose to note index (keeps us in scale!)
	int32_t noteIndexInScale = step.noteIndex + effects.transpose;

	// Wrap to scale (same as Pulse Sequencer)
	while (noteIndexInScale < 0)
		noteIndexInScale += numScaleNotes_;
	while (noteIndexInScale >= numScaleNotes_)
		noteIndexInScale -= numScaleNotes_;

	// Get scale degree from transposed index
	int32_t scaleDegree = scaleNotes_[noteIndexInScale]; // 0-11

	// Calculate: rootNote + scaleDegree + C3 offset (48 for MIDI note C3) + step octave + control octave
	// (transpose already applied to noteIndex above)
	int32_t noteCode = rootNote + scaleDegree + 48 + (step.octave * 12) + (effects.octaveShift * 12);

	// Clamp to MIDI range
	if (noteCode < 0)
		noteCode = 0;
	if (noteCode > 127)
		noteCode = 127;

	return noteCode;
}

RGB StepSequencerMode::getNoteGradientColor(int32_t yPos) const {
	// Gradient from blue (y3) to magenta (y7)
	if (yPos < 3 || yPos > 7) {
		return RGB{128, 0, 255}; // Default purple
	}

	// Interpolate red channel from 0 (blue) to 255 (magenta)
	uint8_t red = (yPos - 3) * 64; // 0, 64, 128, 192, 255
	return RGB{red, 0, 255};
}

void StepSequencerMode::displayOctaveValue(int32_t octave) {
	if (!display)
		return;

	char buffer[8];
	if (octave > 0) {
		buffer[0] = '+';
		buffer[1] = '0' + octave;
		buffer[2] = '\0';
	}
	else if (octave < 0) {
		buffer[0] = '-';
		buffer[1] = '0' + (-octave); // Convert to positive for display
		buffer[2] = '\0';
	}
	else {
		buffer[0] = '0';
		buffer[1] = '\0';
	}
	display->displayPopup(buffer);
}

bool StepSequencerMode::handleModeSpecificVerticalEncoder(int32_t offset) {
	if (!initialized_ || numScaleNotes_ == 0) {
		return false;
	}

	// Scroll note selection up/down
	int32_t newScroll = static_cast<int32_t>(noteScrollOffset_) + offset;

	// Clamp to valid range (show 5 notes at a time, or all notes if less than 5)
	int32_t maxScroll = (static_cast<int32_t>(numScaleNotes_) > 5) ? (static_cast<int32_t>(numScaleNotes_) - 5) : 0;
	if (newScroll < 0)
		newScroll = 0;
	if (newScroll > maxScroll)
		newScroll = maxScroll;
	noteScrollOffset_ = static_cast<uint8_t>(newScroll);

	// Refresh only note pads (y3-y7)
	uiNeedsRendering(&instrumentClipView, kNoteRows, 0);
	return true;
}

bool StepSequencerMode::renderPads(uint32_t whichRows, RGB* image,
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                   uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());

	// Always update scale notes (like Pulse Sequencer does)
	updateScaleNotes(modelStackWithTimelineCounter);

	// Render all 16 steps (columns)
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		const Step& step = steps_[x];
		bool isCurrentStep = (x == currentStep_);
		bool isDisabled = (x >= numActiveSteps_); // Dim columns beyond active step count

		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (!(whichRows & (1 << y)))
				continue;

			RGB color{0, 0, 0};

			// y0: Gate type pad
			if (y == 0) {
				if (isCurrentStep && step.gateType != GateType::SKIP) {
					// Current step highlighted in red (but not for SKIP steps)
					color = RGB{255, 0, 0};
				}
				else {
					// Color based on gate type
					switch (step.gateType) {
					case GateType::OFF:
						color = RGB{64, 64, 64}; // White/gray
						break;
					case GateType::ON:
						color = RGB{0, 255, 0}; // Green
						break;
					case GateType::SKIP:
						color = RGB{255, 0, 255}; // Magenta
						break;
					}
				}
			}
			// y1: Octave down
			else if (y == 1) {
				if (step.octave < 0) {
					color = RGB{0, 128, 255}; // Blue when octave is set
				}
				else {
					// Bright white only when step type is ON, dim otherwise
					color = (step.gateType == GateType::ON) ? RGB{255, 255, 255} : RGB{16, 16, 32};
				}
			}
			// y2: Octave up
			else if (y == 2) {
				if (step.octave > 0) {
					color = RGB{0, 128, 255}; // Blue when octave is set
				}
				else {
					// Bright white only when step type is ON, dim otherwise
					color = (step.gateType == GateType::ON) ? RGB{255, 255, 255} : RGB{16, 16, 32};
				}
			}
			// y3-y7: Note pads (5 notes with scrolling)
			else if (y >= 3 && y <= 7) {
				int32_t displaySlot = y - 3;                               // 0-4 (which of 5 visible pads)
				int32_t actualNoteIndex = displaySlot + noteScrollOffset_; // Actual note in scale

				if (actualNoteIndex < numScaleNotes_ && actualNoteIndex == step.noteIndex) {
					// This is the selected note
					if (isCurrentStep && step.gateType != GateType::SKIP) {
						// Current playing step - show in red (but not for SKIP steps)
						color = RGB{255, 0, 0};
					}
					else {
						// Selected but not current - use gradient color
						color = getNoteGradientColor(y);
					}
				}
				else {
					// Unselected notes are black/off
					color = RGB{0, 0, 0};
				}
			}

			// Apply dimming based on gate type and active step count
			// Disabled columns (beyond numActiveSteps_): make entirely invisible (black)
			if (isDisabled) {
				color = RGB{0, 0, 0};
			}
			// SKIP: dim entire column (all pads)
			else if (step.gateType == GateType::SKIP) {
				dimColor(color);
			}
			// OFF: dim only note pads (y3-y7)
			else if (step.gateType == GateType::OFF && y >= 3 && y <= 7) {
				dimColor(color);
			}

			image[y * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[y][x] = (color.r || color.g || color.b) ? 64 : 0;
			}
		}
	}

	return true;
}

bool StepSequencerMode::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	// Use base class implementation to render control columns
	return SequencerMode::renderSidebar(whichRows, image, occupancyMask);
}

bool StepSequencerMode::handlePadPress(int32_t x, int32_t y, int32_t velocity) {
	// Let base class handle control columns (x16-x17)
	if (x >= kDisplayWidth) {
		return SequencerMode::handlePadPress(x, y, velocity);
	}

	// If Shift is pressed, don't handle pad presses - let instrument clip view handle it
	// (for editing synth parameters, etc.)
	if (Buttons::isShiftButtonPressed()) {
		return false;
	}

	// Only handle main grid pads (x0-x15) and presses (not releases)
	if (x < 0 || velocity == 0) {
		return false;
	}

	// y0: Gate type - cycle through OFF/ON/SKIP
	if (y == 0) {
		cycleGateType(x);

		if (display) {
			const char* gateTypeName = "";
			switch (steps_[x].gateType) {
			case GateType::OFF:
				gateTypeName = "OFF";
				break;
			case GateType::ON:
				gateTypeName = "ON";
				break;
			case GateType::SKIP:
				gateTypeName = "SKIP";
				break;
			}
			display->displayPopup(gateTypeName);
		}

		uiNeedsRendering(&instrumentClipView, kGateRow, 0);
		return true;
	}
	// y1: Octave down
	else if (y == 1) {
		adjustOctave(x, -1);
		displayOctaveValue(steps_[x].octave);
		uiNeedsRendering(&instrumentClipView, kOctaveRows, 0);
		return true;
	}
	// y2: Octave up
	else if (y == 2) {
		adjustOctave(x, 1);
		displayOctaveValue(steps_[x].octave);
		uiNeedsRendering(&instrumentClipView, kOctaveRows, 0);
		return true;
	}
	// y3-y7: Note selection (with scrolling)
	else if (y >= 3 && y <= 7) {
		int32_t displaySlot = y - 3;
		int32_t actualNoteIndex = displaySlot + noteScrollOffset_;

		if (actualNoteIndex < numScaleNotes_) {
			setNoteIndex(x, actualNoteIndex);

			if (display) {
				// Show note with current control column effects applied
				CombinedEffects effects = getCombinedEffects();
				int32_t noteCode = calculateNoteCode(steps_[x], effects);
				char buffer[16];
				noteCodeToString(noteCode, buffer, nullptr, true);
				display->displayPopup(buffer);
			}

			uiNeedsRendering(&instrumentClipView, kNoteRows, 0);
			return true;
		}
	}

	return false;
}

bool StepSequencerMode::handleHorizontalEncoder(int32_t offset, bool encoderPressed) {
	// If shift is pressed and no control column pad is held, adjust active step count
		if (Buttons::isShiftButtonPressed() && heldControlColumnX_ < 0) {
		int32_t newCount = static_cast<int32_t>(numActiveSteps_) + offset;
		if (newCount < 1)
			newCount = 1;
		if (newCount > kNumSteps)
			newCount = kNumSteps;

		if (static_cast<int32_t>(newCount) != static_cast<int32_t>(numActiveSteps_)) {
			numActiveSteps_ = static_cast<uint8_t>(newCount);
			clampStateToActiveRange();
			// Refresh UI to show dimmed columns
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			// Show popup with new count
			if (display) {
				char buffer[8];
				snprintf(buffer, sizeof(buffer), "%d", numActiveSteps_);
				display->displayPopup(buffer);
			}
			return true;
		}
		return true; // Consume the action even if no change
	}

	// Otherwise, use base class implementation (for control columns)
	return SequencerMode::handleHorizontalEncoder(offset, encoderPressed);
}

int32_t StepSequencerMode::processPlayback(void* modelStackPtr, int32_t absolutePlaybackPos) {
	if (!initialized_) {
		return 2147483647;
	}

	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);

	// Get control column effects
	CombinedEffects effects = getCombinedEffects();

	// Calculate base timing on first call
	if (ticksPerSixteenthNote_ == 0) {
		ticksPerSixteenthNote_ = modelStack->song->getSixteenthNoteLength();
	}

	// Reset to first step when playback starts (only at position 0)
	if (absolutePlaybackPos == 0) {
		currentStep_ = 0;
		pingPongDirection_ = 1; // Reset ping pong direction
		// Reset state for additional play order modes
		pedalNextStep_ = 1;
		skip2OddPhase_ = true;
		pendulumGoingUp_ = true;
		pendulumLow_ = 0;
		pendulumHigh_ = 1;
		spiralFromLow_ = true;
		spiralLow_ = 0;
		spiralHigh_ = numActiveSteps_ - 1;
	}

	// Apply clock divider to timing
	// Positive = slower (/2, /4), Negative = faster (*2, *4)
	int32_t adjustedTicksPerStep = ticksPerSixteenthNote_;
	if (effects.clockDivider > 1) {
		adjustedTicksPerStep *= effects.clockDivider; // Divide: slower
	}
	else if (effects.clockDivider < -1) {
		adjustedTicksPerStep /= (-effects.clockDivider); // Multiply: faster
	}

	// Always update scale notes (like Pulse Sequencer does)
	updateScaleNotes(modelStackPtr);

	// Check if we're at a step boundary
	if (!atDivisionBoundary(absolutePlaybackPos, adjustedTicksPerStep)) {
		return ticksUntilNextDivision(absolutePlaybackPos, adjustedTicksPerStep);
	}

	// Advance to next step at the START of this boundary (except on first call at position 0)
	// This ensures currentStep_ points to the step we're about to process
	if (absolutePlaybackPos != 0 && lastAbsolutePlaybackPos_ != 0) {
		advanceStep(effects.direction);
	}

	// Stop previous note if still playing
	if (activeNoteCode_ >= 0) {
		stopNote(modelStackPtr, activeNoteCode_);
		activeNoteCode_ = -1;
	}

	// Refresh UI with current step BEFORE processing
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);

	// Process steps - handle SKIP steps immediately, ON/OFF steps get their full duration
	int32_t stepsChecked = 0;
	while (stepsChecked < kNumSteps) {
		const Step& step = steps_[currentStep_];

		if (step.gateType == GateType::SKIP) {
			// SKIP: advance immediately and process next step at same boundary
			advanceStep(effects.direction);
			stepsChecked++;
			// Refresh UI after advancing past skip
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			// Continue loop to process the next step immediately
		}
		else {
			// ON or OFF: process this step and wait for next boundary
			if (step.gateType == GateType::ON) {
				// Play this step with control column effects applied
				int32_t noteCode = calculateNoteCode(step, effects);

				if (noteCode >= 0 && noteCode <= 127) {
					// 75% gate length
					int32_t noteLength = (adjustedTicksPerStep * 3) / 4;
					playNote(modelStackPtr, noteCode, 100, noteLength);
					activeNoteCode_ = noteCode;
				}
			}
			// OFF: silent step - just count duration
			break; // Exit loop - this step gets its full duration
		}
	}

	lastAbsolutePlaybackPos_ = absolutePlaybackPos;

	// Return ticks until next step boundary (use adjusted ticks, not base)
	return adjustedTicksPerStep;
}

void StepSequencerMode::stopAllNotes(void* modelStackPtr) {
	// Stop any currently playing note
	if (activeNoteCode_ >= 0) {
		stopNote(modelStackPtr, activeNoteCode_);
		activeNoteCode_ = -1;
	}

	// Reset play head position and refresh UI to clear the play head indicator
	currentStep_ = 0;
	uiNeedsRendering(&instrumentClipView, kGateRow | kNoteRows, 0);
}

// ================================================================================================
// SCENE MANAGEMENT
// ================================================================================================

size_t StepSequencerMode::captureScene(void* buffer, size_t maxSize) {
	// Scene structure: all 16 steps + scroll offset + control values + active step count
	struct Scene {
		Step steps[kNumSteps];
		int32_t noteScrollOffset;
		int32_t clockDivider;
		int32_t octaveShift;
		int32_t transpose;
		int32_t direction;
		int32_t numActiveSteps;
	};

	if (maxSize < sizeof(Scene)) {
		return 0; // Buffer too small
	}

	Scene* scene = static_cast<Scene*>(buffer);

	// Save pattern data
	for (int32_t i = 0; i < kNumSteps; i++) {
		scene->steps[i] = steps_[i];
	}
	scene->noteScrollOffset = noteScrollOffset_;
	scene->numActiveSteps = static_cast<int32_t>(numActiveSteps_);

	// Save active control values (not pad layout)
	CombinedEffects effects = getCombinedEffects();
	scene->clockDivider = effects.clockDivider;
	scene->octaveShift = effects.octaveShift;
	scene->transpose = effects.transpose;
	scene->direction = effects.direction;

	return sizeof(Scene);
}

bool StepSequencerMode::recallScene(const void* buffer, size_t size) {
	struct Scene {
		Step steps[kNumSteps];
		int32_t noteScrollOffset;
		int32_t clockDivider;
		int32_t octaveShift;
		int32_t transpose;
		int32_t direction;
		int32_t numActiveSteps;
	};

	if (size < sizeof(Scene)) {
		return false; // Invalid data
	}

	const Scene* scene = static_cast<const Scene*>(buffer);

	// Restore pattern data
	for (int32_t i = 0; i < kNumSteps; i++) {
		steps_[i] = scene->steps[i];
	}
	noteScrollOffset_ = static_cast<uint8_t>(scene->noteScrollOffset);

	// Restore active step count (default to 16 if invalid)
	int32_t value = scene->numActiveSteps;
	if (value < 1 || value > kNumSteps) {
		value = 16; // Default to full length
	}
	numActiveSteps_ = static_cast<uint8_t>(value);
	// Clamp state variables to active range
	clampStateToActiveRange();

	// Clamp scroll offset to valid range
	int32_t maxScroll = (static_cast<int32_t>(numScaleNotes_) > 5) ? (static_cast<int32_t>(numScaleNotes_) - 5) : 0;
	if (static_cast<int32_t>(noteScrollOffset_) > maxScroll) {
		noteScrollOffset_ = static_cast<uint8_t>(maxScroll);
	}
	// noteScrollOffset_ is uint8_t, so it can't be < 0

	// Restore control values by activating matching pads
	int32_t unmatchedClock, unmatchedOctave, unmatchedTranspose, unmatchedDirection;
	controlColumnState_.applyControlValues(scene->clockDivider, scene->octaveShift, scene->transpose, scene->direction,
	                                       &unmatchedClock, &unmatchedOctave, &unmatchedTranspose, &unmatchedDirection);

	// Apply unmatched values to base controls (invisible effects)
	setBaseClockDivider(unmatchedClock);
	setBaseOctaveShift(unmatchedOctave);
	setBaseTranspose(unmatchedTranspose);
	setBaseDirection(unmatchedDirection);

	return true;
}

// ========== PATTERN PERSISTENCE ==========

void StepSequencerMode::writeToFile(Serializer& writer, bool includeScenes) {
	// Write step sequencer with hex-encoded data (Deluge convention)
	writer.writeOpeningTagBeginning("stepSequencer");
	writer.writeAttribute("numSteps", kNumSteps);
	writer.writeAttribute("currentStep", currentStep_);
	writer.writeAttribute("noteScrollOffset", noteScrollOffset_);
	writer.writeAttribute("numActiveSteps", numActiveSteps_);

	// Prepare step data as byte array for writeAttributeHexBytes
	uint8_t stepData[kNumSteps * 3]; // 3 bytes per step
	for (int32_t i = 0; i < kNumSteps; ++i) {
		int32_t offset = i * 3;
		// Byte 0: noteIndex (0-31)
		stepData[offset] = static_cast<uint8_t>(steps_[i].noteIndex);
		// Byte 1: octave + 3 (to make it unsigned 0-6 for -3 to +3)
		stepData[offset + 1] = static_cast<uint8_t>(steps_[i].octave + 3);
		// Byte 2: gate type (0=OFF, 1=ON, 2=SKIP)
		stepData[offset + 2] = static_cast<uint8_t>(steps_[i].gateType);
	}

	// Always write stepData (fixed size array)
	writer.writeAttributeHexBytes("stepData", stepData, kNumSteps * 3);
	writer.closeTag(); // Self-closing tag since no child content

	// Write control columns and scenes
	controlColumnState_.writeToFile(writer, includeScenes);
}

Error StepSequencerMode::readFromFile(Deserializer& reader) {
	char const* tagName;

	// Read stepSequencer tag contents
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "numSteps")) {
			int32_t numSteps = reader.readTagOrAttributeValueInt();
			if (numSteps != kNumSteps) {
				return Error::FILE_CORRUPTED; // Wrong number of steps
			}
		}
		else if (!strcmp(tagName, "currentStep")) {
			currentStep_ = static_cast<uint8_t>(reader.readTagOrAttributeValueInt());
		}
		else if (!strcmp(tagName, "noteScrollOffset")) {
			noteScrollOffset_ = static_cast<uint8_t>(reader.readTagOrAttributeValueInt());
		}
		else if (!strcmp(tagName, "numActiveSteps")) {
			int32_t value = reader.readTagOrAttributeValueInt();
			// Default to 16 if invalid
			if (value < 1 || value > kNumSteps) {
				value = 16;
			}
			numActiveSteps_ = static_cast<uint8_t>(value);
		}
		else if (!strcmp(tagName, "stepData")) {
			// Parse hex string: 3 bytes per step (noteIndex, octave+3, gate)
			char const* hexData = reader.readTagOrAttributeValue();

			// Skip "0x" prefix if present
			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			// Parse 16 steps
			for (int32_t i = 0; i < kNumSteps; ++i) {
				int32_t offset = i * 6; // 3 bytes = 6 hex chars

				// Byte 0: noteIndex
				steps_[i].noteIndex = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset], 2));

				// Byte 1: octave (stored as +3, so subtract 3)
				steps_[i].octave = static_cast<int8_t>(hexToIntFixedLength(&hexData[offset + 2], 2) - 3);

				// Byte 2: gate type
				steps_[i].gateType = static_cast<GateType>(hexToIntFixedLength(&hexData[offset + 4], 2));
			}
		}
		else {
			// Unknown tag - let the caller handle it
			// Don't call reader.exitTag() here to avoid XML parser state issues
			break;
		}
	}

	// After loading, update scale notes cache for current clip/song
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
	updateScaleNotes(modelStackWithTimelineCounter);

	// Clamp state variables to active range after loading
	clampStateToActiveRange();

	return Error::NONE;
}

void StepSequencerMode::advanceStep(int32_t direction) {
	switch (direction) {
	case 0: // Forward
		currentStep_ = static_cast<uint8_t>((static_cast<int32_t>(currentStep_) + 1) % static_cast<int32_t>(numActiveSteps_));
		break;

	case 1: // Backward
		{
			int32_t newStep = static_cast<int32_t>(currentStep_) - 1;
			if (newStep < 0)
				newStep = static_cast<int32_t>(numActiveSteps_) - 1;
			currentStep_ = static_cast<uint8_t>(newStep);
		}
		break;

	case 2: // Ping Pong
		{
			int32_t newStep = static_cast<int32_t>(currentStep_) + static_cast<int32_t>(pingPongDirection_);
			if (newStep >= static_cast<int32_t>(numActiveSteps_)) {
				newStep = static_cast<int32_t>(numActiveSteps_) - 2;
				pingPongDirection_ = -1;
			}
			else if (newStep < 0) {
				newStep = 1;
				pingPongDirection_ = 1;
			}
			currentStep_ = static_cast<uint8_t>(newStep);
		}
		break;

	case 3: // Random
		currentStep_ = static_cast<uint8_t>(rand() % static_cast<int32_t>(numActiveSteps_));
		break;

	case 4: // PEDAL: Always return to step 0: 0,1,0,2,0,3,0,4,...
		if (currentStep_ == 0) {
			currentStep_ = pedalNextStep_;
			pedalNextStep_++;
			if (pedalNextStep_ >= kNumSteps) {
				pedalNextStep_ = 1;
			}
		}
		else {
			currentStep_ = 0;
		}
		break;

	case 5: // SKIP_2: Skip every 2nd: 0,2,4,6,8,10,12,14,1,3,5,7,9,11,13,15
		{
			uint8_t newStep = static_cast<uint8_t>(static_cast<int32_t>(currentStep_) + 2);
			if (newStep >= numActiveSteps_) {
				currentStep_ = skip2OddPhase_ ? 1 : 0;
				skip2OddPhase_ = !skip2OddPhase_;
			}
			else {
				currentStep_ = newStep;
			}
		}
		break;

	case 6: // PENDULUM: Swing pattern: 0,1,0,1,2,1,2,3,2,3,4,3,4,5,...
		if (pendulumGoingUp_) {
			currentStep_ = pendulumHigh_;
			pendulumGoingUp_ = false;
		}
		else {
			currentStep_ = pendulumLow_;
			pendulumGoingUp_ = true;

			pendulumLow_++;
			pendulumHigh_++;

			if (pendulumHigh_ >= numActiveSteps_) {
				pendulumLow_ = 0;
				pendulumHigh_ = 1;
			}
		}
		break;

	case 7: // SPIRAL: Spiral inward: 0,15,1,14,2,13,3,12,4,11,5,10,6,9,7,8
		if (spiralFromLow_) {
			currentStep_ = spiralLow_;
			spiralLow_++;
			spiralFromLow_ = false;
		}
		else {
			currentStep_ = spiralHigh_;
			if (spiralHigh_ > 0) {
				spiralHigh_--;
			}
			else {
				spiralHigh_ = 0; // Prevent underflow
			}
			spiralFromLow_ = true;
		}

		if (spiralLow_ > spiralHigh_) {
			spiralLow_ = 0;
			spiralHigh_ = numActiveSteps_ - 1;
		}
		break;

	default: // Fallback to forward
		currentStep_ = static_cast<uint8_t>((static_cast<int32_t>(currentStep_) + 1) % static_cast<int32_t>(numActiveSteps_));
		break;
	}
}

void StepSequencerMode::resetToInit() {
	// Reset all steps to default state (all OFF)
	for (int32_t i = 0; i < kNumSteps; ++i) {
		steps_[i].gateType = GateType::OFF;
		steps_[i].noteIndex = 0; // First note in scale
		steps_[i].octave = 0;
	}

	// Reset scroll to default
	noteScrollOffset_ = 0;

	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

void StepSequencerMode::randomizeAll(int32_t mutationRate) {
	// Randomize all step parameters based on mutation rate
	for (int32_t i = 0; i < kNumSteps; ++i) {
		if (rand() % 100 < mutationRate) {
			// Random gate type (70% ON, 20% OFF, 10% SKIP)
			int32_t gateRand = rand() % 100;
			if (gateRand < 70) {
				steps_[i].gateType = GateType::ON;
			}
			else if (gateRand < 90) {
				steps_[i].gateType = GateType::OFF;
			}
			else {
				steps_[i].gateType = GateType::SKIP;
			}

			// Random note from scale
			if (numScaleNotes_ > 0) {
				steps_[i].noteIndex = static_cast<uint8_t>(rand() % static_cast<int32_t>(numScaleNotes_));
			}

			// Random octave (-2 to +2)
			steps_[i].octave = (rand() % 5) - 2;
		}
	}

	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

void StepSequencerMode::evolveNotes(int32_t mutationRate) {
	// Evolve pattern with adaptive behavior based on mutation rate
	// Low %: Gentle melodic drift (notes only)
	// High % (>70%): More chaotic (notes, octaves, gates)

	bool isHighRate = mutationRate > 70;

	for (int32_t i = 0; i < kNumSteps; ++i) {
		if ((rand() % 100) < mutationRate) {
			// Always mutate notes
			if (numScaleNotes_ > 0) {
				int32_t change;
				if (isHighRate) {
					// High rate: larger jumps
					change = (rand() % 5) - 2; // -2 to +2
				}
				else {
					// Low rate: gentle steps
					change = (rand() % 3) - 1; // -1, 0, +1
				}

				// Handle change with int32_t arithmetic to handle negative values correctly
				int32_t newNoteIndex = static_cast<int32_t>(steps_[i].noteIndex) + change;

				// Wrap around to valid range
				while (newNoteIndex < 0) {
					newNoteIndex += static_cast<int32_t>(numScaleNotes_);
				}
				while (newNoteIndex >= static_cast<int32_t>(numScaleNotes_)) {
					newNoteIndex -= static_cast<int32_t>(numScaleNotes_);
				}
				steps_[i].noteIndex = static_cast<uint8_t>(newNoteIndex);
			}

			// High rate: also mutate octaves and gates
			if (isHighRate) {
				// Mutate octave (40% chance)
				if ((rand() % 100) < 40) {
					int32_t octaveChange = (rand() % 3) - 1; // -1, 0, or +1
					steps_[i].octave += octaveChange;
					if (steps_[i].octave < -3)
						steps_[i].octave = -3;
					if (steps_[i].octave > 3)
						steps_[i].octave = 3;
				}

				// Mutate gates (25% chance)
				if ((rand() % 100) < 25) {
					int32_t gateRand = rand() % 3;
					steps_[i].gateType = static_cast<GateType>(gateRand);
				}
			}
		}
	}

	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

// Helper methods for default pattern management
bool StepSequencerMode::isDefaultPattern() const {
	// Check if current pattern matches the exact default we would create
	for (int32_t i = 0; i < kNumSteps; i++) {
		// Default pattern: all OFF, octave 0, noteIndex 0 (first in scale)
		GateType expectedGate = GateType::OFF;
		int32_t expectedOctave = 0;
		int32_t expectedNoteIndex = 0;

		if (steps_[i].gateType != expectedGate || steps_[i].octave != expectedOctave
		    || steps_[i].noteIndex != expectedNoteIndex) {
			return false; // Found non-default data
		}
	}
	return true; // All steps match default pattern
}

void StepSequencerMode::setDefaultPattern() {
	// Set the default pattern - centralized so it's easy to change later
	for (int32_t i = 0; i < kNumSteps; i++) {
		steps_[i].gateType = GateType::OFF; // All gates OFF by default
		steps_[i].octave = 0;               // Default: no octave shift
		steps_[i].noteIndex = 0;            // First note in scale
	}
}

// ========== HELPER FUNCTIONS ==========

void StepSequencerMode::dimColor(RGB& color) {
	// Dim to 20% brightness with minimum of 2 to prevent flickering at low brightness levels
	int32_t dimmedR = (color.r * 2) / 10;
	int32_t dimmedG = (color.g * 2) / 10;
	int32_t dimmedB = (color.b * 2) / 10;
	color.r = (dimmedR < 2) ? 2 : static_cast<uint8_t>(dimmedR);
	color.g = (dimmedG < 2) ? 2 : static_cast<uint8_t>(dimmedG);
	color.b = (dimmedB < 2) ? 2 : static_cast<uint8_t>(dimmedB);
}

void StepSequencerMode::clampStateToActiveRange() {
	// Ensure current step is within active range
	if (currentStep_ >= numActiveSteps_) {
		currentStep_ = numActiveSteps_ - 1;
	}
	// Clamp spiral bounds to active range
	if (spiralHigh_ >= numActiveSteps_) {
		spiralHigh_ = numActiveSteps_ - 1;
	}
	if (spiralLow_ >= numActiveSteps_) {
		spiralLow_ = numActiveSteps_ - 1;
	}
	// Clamp pendulum bounds to active range
	if (pendulumHigh_ >= numActiveSteps_) {
		pendulumHigh_ = numActiveSteps_ - 1;
	}
	if (pendulumLow_ >= numActiveSteps_) {
		pendulumLow_ = numActiveSteps_ - 1;
	}
	// Clamp pedal next step to active range
	if (pedalNextStep_ >= numActiveSteps_) {
		pedalNextStep_ = 1;
	}
}

} // namespace deluge::model::clip::sequencer::modes
