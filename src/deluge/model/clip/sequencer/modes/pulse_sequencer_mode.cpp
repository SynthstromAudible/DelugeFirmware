/*
 * Copyright © 2024 Synthstrom Audible Deluge Firmware.
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

#include "model/clip/sequencer/modes/pulse_sequencer_mode.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/lookuptables/lookuptables.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Forward declaration for note name conversion
extern void noteCodeToString(int32_t noteCode, char* buffer, int32_t* getLengthWithoutDot, bool appendOctaveNo);

namespace deluge::model::clip::sequencer::modes {

void PulseSequencerMode::initialize() {
	initialized_ = true;
	ticksPerSixteenthNote_ = 0;

	// Clear the white progress column from normal clip mode
	// (set all tick squares to 255 = not displayed)
	uint8_t tickSquares[kDisplayHeight];
	uint8_t colours[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight); // 255 = not displayed
	memset(colours, 0, kDisplayHeight);
	PadLEDs::setTickSquares(tickSquares, colours);

	// Initialize scale notes first so we know how many notes are in the scale
	updateScaleNotes();

	// Only initialize with defaults if current data matches the exact default pattern
	if (isDefaultPattern()) {
		setDefaultPattern();
	}

	// Initialize sequencer state
	sequencerState_.currentPulse = 0;
	sequencerState_.lastPlayedStage = -1;
	sequencerState_.totalPatternLength = calculateTotalPatternLength();
	sequencerState_.gatePadFlashing = false;

	for (int32_t i = 0; i < kMaxNoteSlots; i++) {
		sequencerState_.noteCodeActive[i] = -1;
		sequencerState_.noteGatePos[i] = 0;
		sequencerState_.noteActive[i] = false;
	}

	// Initialize refresh tick
	sequencerState_.lastRefreshTick = 0;

	// Only initialize performance controls if not already set (preserve loaded data)
	if (performanceControls_.numStages == 0) {
		performanceControls_.numStages = 8;
		performanceControls_.currentStage = 0;
	}
}

void PulseSequencerMode::cleanup() {
	// Stop any playing notes before cleanup
	if (initialized_) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
		stopAllNotes(modelStackWithTimelineCounter);
	}

	// Reset all state to prevent leaks
	initialized_ = false;
	ticksPerSixteenthNote_ = 0;
	lastAbsolutePlaybackPos_ = 0;

	// Reset sequencer state
	sequencerState_.currentPulse = 0;
	sequencerState_.lastPlayedStage = -1;
	sequencerState_.totalPatternLength = 0;
	sequencerState_.gatePadFlashing = false;
	sequencerState_.flashStartTime = 0;
	sequencerState_.lastRefreshTick = 0;

	// Clear all note tracking slots
	for (int32_t i = 0; i < kMaxNoteSlots; i++) {
		sequencerState_.noteCodeActive[i] = -1;
		sequencerState_.noteGatePos[i] = 0;
		sequencerState_.noteActive[i] = false;
	}
}

void PulseSequencerMode::updateScaleNotes() {
	// Get the current scale notes from the song (just one octave, no transpose)
	Song* song = currentSong;
	if (!song) {
		displayState_.numScaleNotes = 0;
		return;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!clip || !clip->inScaleMode) {
		// If not in scale mode, use chromatic (all 12 notes)
		displayState_.numScaleNotes = 12;
		for (int32_t i = 0; i < 12; i++) {
			displayState_.scaleNotes[i] = i; // 0-11 (C, C#, D, D#, E, F, F#, G, G#, A, A#, B)
		}
		return;
	}

	// Get notes from the scale mode
	int32_t rootNote = song->key.rootNote % 12; // Just the note within octave
	NoteSet modeNotes = song->key.modeNotes;

	displayState_.numScaleNotes = 0;
	for (int32_t i = 0; i < 12; i++) {
		if (modeNotes.has(i)) {
			displayState_.scaleNotes[displayState_.numScaleNotes++] = i;
		}
	}
}

// ================================================================================================
// UTILITY HELPER METHODS
// ================================================================================================

bool PulseSequencerMode::isStageActive(int32_t stage) const {
	// Stage is active if it's valid, within numStages, enabled, and not SKIP type
	return isStageValid(stage) && stage < performanceControls_.numStages
	       && stages_[stage].gateType != GateType::SKIP;
}

void PulseSequencerMode::showStagePopup(int32_t stage, const char* format, ...) {
	char buffer[kPopupBufferSize];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, kPopupBufferSize, format, args);
	va_end(args);
	display->displayPopup(buffer);
}

RGB PulseSequencerMode::dimColorIfDisabled(RGB color, int32_t stage) const {
	// Stages beyond numStages: completely off (0 brightness) - like step sequencer
	if (stage >= performanceControls_.numStages) {
		return RGB{0, 0, 0};
	}
	// SKIP stages: dimmed (keep dimmed state)
	if (stages_[stage].gateType == GateType::SKIP) {
		color.r /= 8;
		color.g /= 8;
		color.b /= 8;
	}
	return color;
}

RGB PulseSequencerMode::getOctaveColor(int32_t octave) const {
	RGB color;
	if (octave == 0) {
		// At default pitch: white
		color = RGB{200, 200, 200};
	}
	else if (octave > 0) {
		// Going up: brighter orange based on how far up (range 1 to 3)
		int32_t brightness = (octave * 127) / 3;
		color = RGB{static_cast<uint8_t>(128 + brightness), static_cast<uint8_t>(64 + brightness / 2), 0};
	}
	else {
		// Going down: brighter orange based on how far down (range -2 to -1)
		int32_t brightness = (-octave * 127) / 2;
		color = RGB{static_cast<uint8_t>(128 + brightness), static_cast<uint8_t>(64 + brightness / 2), 0};
	}
	return color;
}


// Helper: Calculate note code from stage note index with transpose/octave applied
int32_t PulseSequencerMode::calculateNoteCode(int32_t stage, int32_t noteIndexInScale,
                                               const CombinedEffects& effects) const {
	if (!isStageValid(stage)) {
		return 60; // Default to middle C
	}

	Song* song = currentSong;
	if (!song) {
		return 60;
	}

	// Use displayState scale notes (already cached)
	if (noteIndexInScale < 0 || noteIndexInScale >= displayState_.numScaleNotes) {
		return 60;
	}

	int32_t rootNote = song->key.rootNote;
	int32_t scaleNoteOffset = displayState_.scaleNotes[noteIndexInScale];
	int32_t noteCode = rootNote + scaleNoteOffset + 48; // Base C3 offset
	int32_t totalOctaveShift = effects.octaveShift;
	noteCode += (stages_[stage].octave * 12) + (totalOctaveShift * 12);

	// Clamp to MIDI range
	if (noteCode < 0)
		noteCode = 0;
	if (noteCode > 127)
		noteCode = 127;

	return noteCode;
}

int32_t PulseSequencerMode::calculateTotalPatternLength() const {
	int32_t totalLength = 0;
	for (int32_t i = 0; i < performanceControls_.numStages; i++) {
		totalLength += stages_[i].pulseCount;
	}
	return totalLength;
}

int32_t PulseSequencerMode::getTicksPerPeriod(int32_t baseTicks) const {
	// Apply control column clock divider
	// Positive = slower (/2, /4), Negative = faster (*2, *4)
	CombinedEffects effects = getCombinedEffects();
	int32_t ticks = baseTicks;
	if (effects.clockDivider > 1) {
		ticks = ticks * effects.clockDivider; // Divide: slower
	}
	else if (effects.clockDivider < -1) {
		ticks = ticks / (-effects.clockDivider); // Multiply: faster
	}

	return ticks;
}

// ================================================================================================
// RENDERING
// ================================================================================================

bool PulseSequencerMode::renderPads(uint32_t whichRows, RGB* image,
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                    uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) {
	// Clear all pads first
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		if (whichRows & (1 << y)) {
			for (int32_t x = 0; x < renderWidth; x++) {
				image[y * imageWidth + x] = {0, 0, 0};
				if (occupancyMask) {
					occupancyMask[y][x] = 0;
				}
			}
		}
	}

	// Update scale notes
	updateScaleNotes();

	// ===============================================================================
	// SCROLLABLE LEFT SIDE (x0-7) - Everything moves together
	// ===============================================================================
	// Gate line is the anchor, default at y4 (when gateLineOffset = 0)
	int32_t gateLineY = getGateLineY(); // 4 + gateLineOffset

	// Render pulse count rows (below gate line, y0-y3 by default)
	for (int32_t i = 0; i < kMaxPulseCount; i++) {
		int32_t yPos = gateLineY - 1 - i;
		if (yPos >= 0 && yPos < kDisplayHeight && (whichRows & (1 << yPos))) {
			for (int32_t stage = 0; stage < kMaxStages; stage++) {
				if (i < stages_[stage].pulseCount) {
					// Active pulse - cyan to purple gradient
					int32_t intensity = (i * 255) / (kMaxPulseCount - 1);
					RGB color = RGB{static_cast<uint8_t>(intensity), static_cast<uint8_t>(255 - intensity), 255};
					color = dimColorIfDisabled(color, stage);

					image[yPos * imageWidth + stage] = color;
					if (occupancyMask) {
						occupancyMask[yPos][stage] = 32 + (i * 4);
					}
				}
			}
		}
	}

	// Render gate line (at gateLineY, y4 by default)
	if (gateLineY >= 0 && gateLineY < kDisplayHeight && (whichRows & (1 << gateLineY))) {
		for (int32_t stage = 0; stage < kMaxStages; stage++) {
			bool shouldFlash = false;
			if (sequencerState_.gatePadFlashing && sequencerState_.lastPlayedStage == stage) {
				uint32_t currentTime = playbackHandler.getCurrentInternalTickCount();
				uint32_t flashElapsed = currentTime - sequencerState_.flashStartTime;
				if (flashElapsed < sequencerState_.flashDuration) {
					shouldFlash = true;
				}
				else {
					sequencerState_.gatePadFlashing = false;
				}
			}

			RGB color;
			if (shouldFlash) {
				color = (stages_[stage].gateType == GateType::OFF) ? RGB{255, 100, 0} : RGB{255, 0, 0};
			}
			else {
				switch (stages_[stage].gateType) {
				case GateType::OFF:
					color = RGB{100, 100, 100};
					break;
				case GateType::SINGLE:
					color = RGB{0, 255, 0};
					break;
				case GateType::MULTIPLE:
					color = RGB{0, 0, 255};
					break;
				case GateType::HELD:
					color = RGB{255, 0, 255};
					break;
				case GateType::SKIP:
					color = RGB{50, 50, 50}; // Dim gray for SKIP
					break;
				}
			}

			color = dimColorIfDisabled(color, stage);
			image[gateLineY * imageWidth + stage] = color;
			if (occupancyMask) {
				occupancyMask[gateLineY][stage] = 64;
			}
		}
	}

	// Render octave down (above gate line, y5 by default)
	int32_t octaveDownY = gateLineY + kOctaveDownRow;
	if (octaveDownY >= 0 && octaveDownY < kDisplayHeight && (whichRows & (1 << octaveDownY))) {
		for (int32_t stage = 0; stage < kMaxStages; stage++) {
			int32_t octave = stages_[stage].octave;
			RGB color = getOctaveColor(octave);

			// For down pad: show active when octave is negative
			if (octave > 0) {
				// Dim when going up (opposite direction)
				int32_t dimness = (octave * 60) / 3;
				color = RGB{static_cast<uint8_t>(90 - dimness), static_cast<uint8_t>(45 - dimness / 2), 0};
			}

			color = dimColorIfDisabled(color, stage);
			image[octaveDownY * imageWidth + stage] = color;
			if (occupancyMask) {
				occupancyMask[octaveDownY][stage] = (octave != 0) ? 48 : 32;
			}
		}
	}

	// Render octave up (above octave down, y6 by default)
	int32_t octaveUpY = gateLineY + kOctaveUpRow;
	if (octaveUpY >= 0 && octaveUpY < kDisplayHeight && (whichRows & (1 << octaveUpY))) {
		for (int32_t stage = 0; stage < kMaxStages; stage++) {
			int32_t octave = stages_[stage].octave;
			RGB color = getOctaveColor(octave);

			// For up pad: show active when octave is positive
			if (octave < 0) {
				// Dim when going down (opposite direction)
				int32_t dimness = (-octave * 60) / 2;
				color = RGB{static_cast<uint8_t>(90 - dimness), static_cast<uint8_t>(45 - dimness / 2), 0};
			}

			color = dimColorIfDisabled(color, stage);
			image[octaveUpY * imageWidth + stage] = color;
			if (occupancyMask) {
				occupancyMask[octaveUpY][stage] = (octave != 0) ? 48 : 32;
			}
		}
	}

	// Render note pads (above octave up, y7+ by default)
	for (int32_t noteIdx = 0; noteIdx < displayState_.numScaleNotes; noteIdx++) {
		int32_t yPos = getNoteRowY(noteIdx);
		if (yPos >= 0 && yPos < kDisplayHeight && (whichRows & (1 << yPos))) {
			for (int32_t stage = 0; stage < kMaxStages; stage++) {
				bool isSelected = (stages_[stage].noteIndex == noteIdx);
				RGB color = isSelected ? RGB{255, 200, 50} : RGB{0, 0, 0};
				color = dimColorIfDisabled(color, stage);

				image[yPos * imageWidth + stage] = color;
				if (occupancyMask) {
					occupancyMask[yPos][stage] = isSelected ? 64 : 8;
				}
			}
		}
	}

	// ===============================================================================
	// FIXED RIGHT SIDE CONTROLS (x8-15) - These don't scroll
	// ===============================================================================

	// y4: No longer used (stage count now controlled via Shift + horizontal encoder)

	// y3: No longer used for stage enable/disable (replaced by SKIP gate type)

	// After rendering all pads, brighten the playback position
	// Show on both gate line AND the selected note pad for the current stage
	if (isStageValid(performanceControls_.currentStage)) {
		int32_t gateLineY = getGateLineY();
		int32_t currentStage = performanceControls_.currentStage;

		// Make the gate pad bright red for the current stage (if visible)
		if (gateLineY >= 0 && gateLineY < kDisplayHeight && (whichRows & (1 << gateLineY))) {
			image[gateLineY * imageWidth + currentStage] = RGB{255, 0, 0};
			if (occupancyMask) {
				occupancyMask[gateLineY][currentStage] = 64;
			}
		}

		// Also make the selected note pad RED for the current stage (if visible)
		int32_t noteY = getNoteRowY(stages_[currentStage].noteIndex);
		if (noteY >= 0 && noteY < kDisplayHeight && (whichRows & (1 << noteY))) {
			// Make it red like the gate line flash
			image[noteY * imageWidth + currentStage] = RGB{255, 0, 0};
			if (occupancyMask) {
				occupancyMask[noteY][currentStage] = 64;
			}
		}
	}

	return true;
}

bool PulseSequencerMode::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	// Use base class implementation to render control columns
	return SequencerMode::renderSidebar(whichRows, image, occupancyMask);
}

int32_t PulseSequencerMode::processPlayback(void* modelStackPtr, int32_t absolutePlaybackPos) {
	if (!initialized_) {
		return 2147483647;
	}

	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());

	// Only work with melodic instruments
	if (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::MIDI_OUT
	    && clip->output->type != OutputType::CV) {
		return 2147483647;
	}

	// Reset repeat count when playback starts (only at position 0)
	if (absolutePlaybackPos == 0) {
		sequencerState_.repeatCount_ = 0;
	}

	// Calculate base tick rate (16th notes from song)
	if (ticksPerSixteenthNote_ == 0) {
		ticksPerSixteenthNote_ = modelStack->song->getSixteenthNoteLength();
	}

	// Apply clock divider to timing
	int32_t ticksPerPeriod = getTicksPerPeriod(ticksPerSixteenthNote_);

	// Store clip position
	lastAbsolutePlaybackPos_ = clip->lastProcessedPos;

	// Check for any notes that need to be turned off
	for (int32_t i = 0; i < kMaxNoteSlots; i++) {
		if (sequencerState_.noteActive[i]) {
			// Check if note should end now
			if (absolutePlaybackPos >= sequencerState_.noteGatePos[i]) {
				stopNote(modelStackPtr, sequencerState_.noteCodeActive[i]);
				sequencerState_.noteActive[i] = false;
				sequencerState_.noteCodeActive[i] = -1;
			}
		}
	}

	// Check if we're at a period boundary
	bool atBoundary = atDivisionBoundary(absolutePlaybackPos, ticksPerPeriod);

	if (atBoundary) {
		// Store old stage to detect changes
		int32_t oldStage = sequencerState_.lastPlayedStage;

		// Flash pad for visual feedback
		sequencerState_.gatePadFlashing = true;
		sequencerState_.flashStartTime = playbackHandler.getCurrentInternalTickCount();
		sequencerState_.lastPlayedStage = performanceControls_.currentStage;

		// Generate notes
		generateNotes(modelStackPtr);

		// Refresh the gate line and note pads when stage changes
		if (oldStage != performanceControls_.currentStage) {
			int32_t gateLineY = getGateLineY();
			uint32_t rowsToRefresh = (1 << gateLineY);

			// Refresh the note rows for both old and new stages
			if (isStageValid(oldStage)) {
				int32_t oldNoteY = getNoteRowY(stages_[oldStage].noteIndex);
				if (oldNoteY >= 0 && oldNoteY < kDisplayHeight) {
					rowsToRefresh |= (1 << oldNoteY);
				}
			}

			int32_t newNoteY = getNoteRowY(stages_[performanceControls_.currentStage].noteIndex);
			if (newNoteY >= 0 && newNoteY < kDisplayHeight) {
				rowsToRefresh |= (1 << newNoteY);
			}

			uiNeedsRendering(&instrumentClipView, rowsToRefresh, 0);
		}
	}

	// Always refresh the current stage's gate and note pads during playback for smooth tracking
	// This ensures the red indicator is visible even with slow clock dividers
	uint32_t currentTick = playbackHandler.getCurrentInternalTickCount();
	if (currentTick - sequencerState_.lastRefreshTick > 10) { // Refresh every 10 ticks for smooth updates
		int32_t gateLineY = getGateLineY();
		uint32_t rowsToRefresh = (1 << gateLineY);

		int32_t noteY = getNoteRowY(stages_[performanceControls_.currentStage].noteIndex);
		if (noteY >= 0 && noteY < kDisplayHeight) {
			rowsToRefresh |= (1 << noteY);
		}

		uiNeedsRendering(&instrumentClipView, rowsToRefresh, 0);
		sequencerState_.lastRefreshTick = currentTick;
	}

	return ticksUntilNextDivision(absolutePlaybackPos, ticksPerPeriod);
}

void PulseSequencerMode::stopAllNotes(void* modelStackPtr) {
	// Stop all currently playing notes
	for (int32_t i = 0; i < kMaxNoteSlots; i++) {
		if (sequencerState_.noteActive[i]) {
			stopNote(modelStackPtr, sequencerState_.noteCodeActive[i]);
			sequencerState_.noteActive[i] = false;
			sequencerState_.noteCodeActive[i] = -1;
		}
	}
}

void PulseSequencerMode::generateNotes(void* modelStackPtr) {
	int32_t stage = performanceControls_.currentStage;

	if (isStageActive(stage)) {
		int32_t pulseInStage = sequencerState_.currentPulse;

		if (evaluateRhythmPattern(stage, pulseInStage)) {
			playNoteForStage(modelStackPtr, stage);
		}
	}

	sequencerState_.lastPlayedStage = performanceControls_.currentStage;

	// Advance to next pulse
	sequencerState_.currentPulse++;
	if (sequencerState_.currentPulse >= stages_[performanceControls_.currentStage].pulseCount) {
		sequencerState_.currentPulse = 0;
		advanceToNextEnabledStage();
	}
}

void PulseSequencerMode::playNoteForStage(void* modelStackPtr, int32_t stage) {
	StageData& stageData = stages_[stage];

	if (stageData.gateType == GateType::OFF || stageData.gateType == GateType::SKIP) {
		return; // Don't play OFF or SKIP stages
	}

	// Check iterance first (if set) - same as step sequencer
	bool shouldPlay = true;
	if (stageData.iterance != kDefaultIteranceValue) {
		shouldPlay = stageData.iterance.passesCheck(sequencerState_.repeatCount_);
	}
	if (!shouldPlay) {
		return; // Iterance check failed - don't play this cycle
	}

	// Get all scale notes starting from C3 (MIDI 60) for better range
	int32_t scaleNotes[64];
	int32_t numNotes = getScaleNotes(modelStackPtr, scaleNotes, 64, 6, 0); // 6 octaves starting from root

	if (numNotes == 0) {
		return;
	}

	// Get control column effects
	CombinedEffects effects = getCombinedEffects();

	// Calculate note index with transpose (from control columns)
	int32_t totalTranspose = effects.transpose;
	int32_t noteIndexInScale = stageData.noteIndex + totalTranspose;

	// Wrap to scale
	while (noteIndexInScale < 0)
		noteIndexInScale += numNotes;
	while (noteIndexInScale >= numNotes)
		noteIndexInScale -= numNotes;

	// Get note from scale (already includes the root note from getScaleNotes)
	int32_t note = scaleNotes[noteIndexInScale];

	// Add base octave offset to start from C3 (MIDI 60) instead of very low notes
	// The root note from scale is already included, so we just shift up to C3 range
	note += 48; // Shift up 4 octaves to C3 range

	// Apply stage octave and global octave offsets (from control columns)
	int32_t totalOctaveShift = effects.octaveShift;
	note += (stageData.octave * 12) + (totalOctaveShift * 12);

	// Clamp to MIDI range
	if (note < 0)
		note = 0;
	if (note > 127)
		note = 127;

	// Calculate note length based on gate type
	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());

	int32_t noteLength;
	int32_t periodTicks = getTicksPerPeriod(ticksPerSixteenthNote_);
	if (stageData.gateType == GateType::HELD) {
		// HELD: note lasts for entire stage duration (95% to prevent overhang)
		noteLength = (periodTicks * stageData.pulseCount * 95) / 100;
	}
	else {
		// SINGLE/MULTIPLE: short staccato notes (50% of period)
		noteLength = periodTicks / 2;
	}

	// Find free slot to track this note
	int32_t freeSlot = -1;
	for (int32_t i = 0; i < kMaxNoteSlots; i++) {
		if (!sequencerState_.noteActive[i]) {
			freeSlot = i;
			break;
		}
	}

	// If all slots are full, find the oldest note (earliest gate position) and reuse its slot
	if (freeSlot < 0) {
		uint32_t oldestGatePos = sequencerState_.noteGatePos[0];
		freeSlot = 0;
		for (int32_t i = 1; i < kMaxNoteSlots; i++) {
			if (sequencerState_.noteGatePos[i] < oldestGatePos) {
				oldestGatePos = sequencerState_.noteGatePos[i];
				freeSlot = i;
			}
		}
		// Stop the oldest note before reusing its slot
		if (sequencerState_.noteCodeActive[freeSlot] >= 0) {
			stopNote(modelStackPtr, sequencerState_.noteCodeActive[freeSlot]);
		}
	}

	// Apply probability check
	// Convert probability from 0-20 (5% increments) to 0-100 for shouldPlayBasedOnProbability
	if (!shouldPlayBasedOnProbability(stageData.probability * 5)) {
		return; // Don't play this note
	}

	// Apply velocity with spread randomization
	uint8_t velocity = applyVelocitySpread(stageData.velocity, stageData.velocitySpread);

	// Apply gate length to note duration
	noteLength = (noteLength * stageData.gateLength) / 100;
	if (noteLength < 1)
		noteLength = 1; // Ensure at least 1 tick

	// Send note-on (Deluge convention: pass length but we still track note-off ourselves)
	playNote(modelStackPtr, note, velocity, noteLength);

	// Track for automatic note-off
	sequencerState_.noteCodeActive[freeSlot] = note;
	sequencerState_.noteGatePos[freeSlot] = clip->lastProcessedPos + noteLength;
	sequencerState_.noteActive[freeSlot] = true;
}

void PulseSequencerMode::switchNoteOff(void* modelStackPtr, int32_t noteSlot) {
	if (noteSlot < 0 || noteSlot >= kMaxNoteSlots || !sequencerState_.noteActive[noteSlot]) {
		return;
	}

	int32_t note = sequencerState_.noteCodeActive[noteSlot];
	if (note >= 0) {
		stopNote(modelStackPtr, note);
	}

	sequencerState_.noteCodeActive[noteSlot] = -1;
	sequencerState_.noteGatePos[noteSlot] = 0;
	sequencerState_.noteActive[noteSlot] = false;
}

// ================================================================================================
// PLAY ORDER ADVANCEMENT METHODS
// ================================================================================================

void PulseSequencerMode::advanceRandom() {
	int32_t enabledStages[kMaxStages];
	int32_t enabledCount = 0;
	for (int32_t i = 0; i < performanceControls_.numStages; i++) {
		if (isStageActive(i)) {
			enabledStages[enabledCount++] = i;
		}
	}
	if (enabledCount > 0) {
		performanceControls_.currentStage = enabledStages[getRandom255() % enabledCount];
	}
}

void PulseSequencerMode::advancePedal() {
	// Always return to stage 1: 1,2,1,3,1,4,1,5,1,6,1,7,1,8
	if (performanceControls_.currentStage == 0) {
		performanceControls_.currentStage = performanceControls_.pedalNextStage;
		performanceControls_.pedalNextStage++;
		if (performanceControls_.pedalNextStage >= performanceControls_.numStages) {
			performanceControls_.pedalNextStage = 1;
		}
	}
	else {
		performanceControls_.currentStage = 0;
	}
}

void PulseSequencerMode::advanceSkip2() {
	// Skip every 2nd: 1,3,5,7,2,4,6,8
	if (performanceControls_.skip2OddPhase) {
		performanceControls_.currentStage += 2;
		if (performanceControls_.currentStage >= performanceControls_.numStages) {
			performanceControls_.currentStage = 1;
			performanceControls_.skip2OddPhase = false;
		}
	}
	else {
		performanceControls_.currentStage += 2;
		if (performanceControls_.currentStage >= performanceControls_.numStages) {
			performanceControls_.currentStage = 0;
			performanceControls_.skip2OddPhase = true;
		}
	}
}

void PulseSequencerMode::advancePendulum() {
	// Swing pattern: 1,2,3,2,3,4,3,4,5,4,5,6,5,6,7,6,7,8
	if (performanceControls_.pendulumGoingUp) {
		performanceControls_.currentStage = performanceControls_.pendulumHigh;
		performanceControls_.pendulumGoingUp = false;
	}
	else {
		performanceControls_.currentStage = performanceControls_.pendulumLow;
		performanceControls_.pendulumGoingUp = true;

		performanceControls_.pendulumLow++;
		performanceControls_.pendulumHigh++;

		if (performanceControls_.pendulumHigh >= performanceControls_.numStages) {
			performanceControls_.pendulumLow = 0;
			performanceControls_.pendulumHigh = 1;
		}
	}
}

void PulseSequencerMode::advanceSpiral() {
	// Spiral inward: 1,8,2,7,3,6,4,5
	if (performanceControls_.spiralFromLow) {
		performanceControls_.currentStage = performanceControls_.spiralLow;
		performanceControls_.spiralLow++;
		performanceControls_.spiralFromLow = false;
	}
	else {
		performanceControls_.currentStage = performanceControls_.spiralHigh;
		performanceControls_.spiralHigh--;
		performanceControls_.spiralFromLow = true;
	}

	if (performanceControls_.spiralLow > performanceControls_.spiralHigh) {
		performanceControls_.spiralLow = 0;
		performanceControls_.spiralHigh = performanceControls_.numStages - 1;
	}
}

void PulseSequencerMode::advancePingPong(int32_t& nextStage, int32_t& direction) {
	if (nextStage >= performanceControls_.numStages) {
		nextStage = performanceControls_.numStages - 2;
		performanceControls_.pingPongDirection = -1;
		direction = -1;
	}
	else if (nextStage < 0) {
		nextStage = 1;
		performanceControls_.pingPongDirection = 1;
		direction = 1;
	}
}

void PulseSequencerMode::advanceForwards(int32_t& nextStage) {
	if (nextStage >= performanceControls_.numStages) {
		nextStage = 0;
	}
}

void PulseSequencerMode::advanceBackwards(int32_t& nextStage) {
	if (nextStage < 0) {
		nextStage = performanceControls_.numStages - 1;
	}
}

void PulseSequencerMode::advanceToNextEnabledStage() {
	// Get play order from control columns (direction: 0-7)
	CombinedEffects effects = getCombinedEffects();
	int32_t playOrder = effects.direction;

	// Track old stage for repeat count before advancing
	int32_t oldStage = performanceControls_.currentStage;

	// Special play orders that don't use the standard find-next-enabled loop
	switch (playOrder) {
	case 3: // RANDOM
		advanceRandom();
		// For random, track cycles when returning to stage 0 (simplification)
		if (oldStage == performanceControls_.numStages - 1 && performanceControls_.currentStage == 0) {
			sequencerState_.repeatCount_++;
		}
		return;
	case 4: // PEDAL
		advancePedal();
		// For pedal, track cycles when returning to stage 0
		if (oldStage != 0 && performanceControls_.currentStage == 0) {
			sequencerState_.repeatCount_++;
		}
		return;
	case 5: // SKIP_2
		advanceSkip2();
		// For skip2, track cycles when returning to stage 0
		if (oldStage == performanceControls_.numStages - 1 && performanceControls_.currentStage == 0) {
			sequencerState_.repeatCount_++;
		}
		return;
	case 6: // PENDULUM
		advancePendulum();
		// For pendulum, track cycles when returning to stage 0
		if (oldStage == performanceControls_.numStages - 1 && performanceControls_.currentStage == 0) {
			sequencerState_.repeatCount_++;
		}
		return;
	case 7: // SPIRAL
		advanceSpiral();
		// For spiral, track cycles when returning to stage 0
		if (oldStage == performanceControls_.numStages - 1 && performanceControls_.currentStage == 0) {
			sequencerState_.repeatCount_++;
		}
		return;
	default:
		break; // Fall through to standard advancement
	}

	// Standard advancement for FORWARDS(0)/BACKWARDS(1)/PING_PONG(2)
	int32_t nextStage = performanceControls_.currentStage;
	int32_t direction = (playOrder == 1) ? -1 : 1; // 1 = BACKWARDS
	if (playOrder == 2) { // PING_PONG
		direction = performanceControls_.pingPongDirection;
	}

	int32_t attempts = 0;
	do {
		nextStage += direction;
		attempts++;

		if (playOrder == 2) { // PING_PONG
			advancePingPong(nextStage, direction);
		}
		else if (playOrder == 0) { // FORWARDS
			advanceForwards(nextStage);
		}
		else { // BACKWARDS (1)
			advanceBackwards(nextStage);
		}

		if (attempts > performanceControls_.numStages) {
			return; // Safety: avoid infinite loop
		}

	} while (!isStageActive(nextStage));

	// Track repeat count for iterance: increment when we complete a full cycle (loop back to stage 0)
	performanceControls_.currentStage = nextStage;

	// Check if we've completed a full cycle (went from last stage back to 0)
	// For forward direction, we complete a cycle when going from last stage to 0
	if (playOrder == 0 && oldStage == performanceControls_.numStages - 1 && nextStage == 0) {
		sequencerState_.repeatCount_++;
	}
	// For backward direction, we complete a cycle when going from 0 to last stage
	else if (playOrder == 1 && oldStage == 0 && nextStage == performanceControls_.numStages - 1) {
		sequencerState_.repeatCount_++;
	}
	// For other modes, increment on forward cycles (simplification)
	else if (playOrder != 0 && playOrder != 1 && oldStage == performanceControls_.numStages - 1 && nextStage == 0) {
		sequencerState_.repeatCount_++;
	}
}

bool PulseSequencerMode::evaluateRhythmPattern(int32_t stage, int32_t pulsePosition) {
	StageData& stageData = stages_[stage];

	switch (stageData.gateType) {
	case GateType::SINGLE:
		return (pulsePosition == 0);
	case GateType::MULTIPLE:
		return (pulsePosition < stageData.pulseCount);
	case GateType::HELD:
		return (pulsePosition == 0);
	case GateType::SKIP:
	case GateType::OFF:
		return false;
	}

	return false;
}

// ================================================================================================
// PAD INPUT HANDLING
// ================================================================================================

bool PulseSequencerMode::handlePadPress(int32_t x, int32_t y, int32_t velocity) {
	// Control columns (x16-x17) - delegate to base class (handles both presses and releases)
	if (x >= kDisplayWidth) {
		return SequencerMode::handlePadPress(x, y, velocity);
	}

	// Handle pad releases first - clear held pad tracking if it matches
	if (velocity == 0) {
		if (heldPadX_ == x && heldPadY_ == y) {
			heldPadX_ = -1;
			heldPadY_ = -1;
		}
		return false; // Let instrument clip view handle releases
	}

	// If Shift is pressed, don't handle pad presses - let instrument clip view handle it
	// (for editing synth parameters, etc.)
	if (Buttons::isShiftButtonPressed()) {
		return false;
	}

	// Only handle presses (not releases) for mode-specific controls
	if (x < 0) {
		return false;
	}

	// Ignore pads at x8-x15 completely - return true to prevent clip view from handling them
	// This prevents them from entering UI_MODE_NOTES_PRESSED and triggering iterance/prob
	if (x >= kMaxStages) {
		heldPadX_ = -1;
		heldPadY_ = -1;
		return true; // Consume the pad press - don't let clip view handle it
	}

	// LEFT SIDE SCROLLABLE CONTROLS (x0-7) - all relative to gate line
	int32_t gateLineY = getGateLineY();

	if (x < 8) {
		// Gate line
		if (y == gateLineY) {
			handleGateType(x);
			return true;
		}
		// Pulse count pads (below gate line)
		else if (y < gateLineY) {
			int32_t pulseIndex = gateLineY - 1 - y;
			if (pulseIndex >= 0 && pulseIndex < 8) {
				handlePulseCount(x, pulseIndex);
				return true;
			}
		}
		// Octave down (gate + 1)
		else if (y == gateLineY + 1) {
			handleOctaveAdjustment(x, -1);
			return true;
		}
		// Octave up (gate + 2)
		else if (y == gateLineY + 2) {
			handleOctaveAdjustment(x, 1);
			return true;
		}
		// Note pads (gate + 3 and above)
		else if (y >= gateLineY + 3) {
			int32_t noteIdx = y - (gateLineY + 3);
			// Only track and handle if noteIdx is valid (within scale notes range)
			if (noteIdx >= 0 && noteIdx < displayState_.numScaleNotes) {
				// Track held pad for encoder adjustments (only for valid notes)
				heldPadX_ = static_cast<int8_t>(x);
				heldPadY_ = static_cast<int8_t>(y);

				// Select this note for this stage
				int32_t stage = x;
				stages_[stage].noteIndex = noteIdx;

				// Show popup with note name
				CombinedEffects effects = getCombinedEffects();
				int32_t noteCode = calculateNoteCode(stage, noteIdx, effects);

				char noteNameBuffer[kNoteNameBufferSize];
				int32_t lengthDummy = 0;
				noteCodeToString(noteCode, noteNameBuffer, &lengthDummy, true);
				showStagePopup(stage, "Stage %d: %s", stage + 1, noteNameBuffer);
				return true;
			}
			// Invalid note pad (blank pad) - don't track, let clip view handle it
			return false;
		}
	}

	// RIGHT SIDE FIXED CONTROLS (x8-15)
	// y4: No longer used (stage count now controlled via Shift + horizontal encoder)
	// y3: No longer used (was stage enable/disable, now replaced by SKIP gate type)
	// (Already handled above - pads at x >= 8 are cleared and return false)

	return false; // Didn't handle this pad
}

bool PulseSequencerMode::handleModeSpecificVerticalEncoder(int32_t offset) {
	// If encoder button is pressed
	if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		// If a note pad is held, adjust gate length
		if (isNotePadHeld()) {
			int32_t stage = heldPadX_;
			if (isStageValid(stage)) {
				int32_t newGateLength = static_cast<int32_t>(stages_[stage].gateLength) + offset;
				stages_[stage].gateLength = static_cast<uint8_t>(SequencerMode::clampValue(newGateLength, static_cast<int32_t>(1), static_cast<int32_t>(100)));

				// Show gate length in display
				SequencerMode::displayGateLength(static_cast<uint8_t>(stages_[stage].gateLength));

				uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
				return true;
			}
		}
		// Otherwise, encoder button pressed but no note pad - shift all stages' octaves
		for (int32_t i = 0; i < kMaxStages; i++) {
			int32_t newOctave = static_cast<int32_t>(stages_[i].octave) + offset;
			stages_[i].octave = static_cast<int8_t>(SequencerMode::clampValue(newOctave, static_cast<int32_t>(-2), static_cast<int32_t>(3)));
		}
		// Show popup with actual octave value (use first stage as reference)
		extern deluge::hid::Display* display;
		if (::display) {
			char buffer[20];
			int32_t octaveValue = static_cast<int32_t>(stages_[0].octave);
			if (::display->haveOLED()) {
				memcpy(buffer, "Octave: ", 8);
				intToString(octaveValue, &buffer[8], 1);
			}
			else {
				intToString(octaveValue, buffer, 1);
			}
			::display->displayPopup(buffer);
		}
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		return true;
	}

	// If a note pad is held (without encoder button), adjust gate length instead of scrolling
	if (isNotePadHeld()) {
		int32_t stage = heldPadX_;
		if (isStageValid(stage)) {
			int32_t newGateLength = static_cast<int32_t>(stages_[stage].gateLength) + offset;
			stages_[stage].gateLength = static_cast<uint8_t>(SequencerMode::clampValue(newGateLength, static_cast<int32_t>(1), static_cast<int32_t>(100)));

			// Show gate length in display
			SequencerMode::displayGateLength(static_cast<uint8_t>(stages_[stage].gateLength));

			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			return true;
		}
	}

	// Otherwise, scroll the entire left side (gate line + pulse counts + octave controls + notes)
	displayState_.gateLineOffset += offset;

	// Clamp to valid range
	// Max: 4 (to see all 8 pulse count rows, gate at y7+)
	// Min: negative enough to bring first note row to y7
	// First note is at gate+3, so to get it to y7: gate must be at y4
	// But we can scroll down further to see notes at top
	// Min = -(numScaleNotes - 1) to see last note at y7
	int32_t minOffset = -(displayState_.numScaleNotes - 1);
	int32_t maxOffset = 4;

	if (displayState_.gateLineOffset < minOffset) {
		displayState_.gateLineOffset = minOffset;
	}
	if (displayState_.gateLineOffset > maxOffset) {
		displayState_.gateLineOffset = maxOffset;
	}

	// Show popup indicating scroll position
	char buffer[20];
	memcpy(buffer, "Scroll: ", 8);
	intToString(displayState_.gateLineOffset, &buffer[8], 1);
	display->displayPopup(buffer);

	return true; // We handled it
}

bool PulseSequencerMode::handleHorizontalEncoder(int32_t offset, bool encoderPressed) {
	// Handle Shift + encoder for stage count adjustment (1-8)
	if (Buttons::isShiftButtonPressed() && heldControlColumnX_ < 0) {
		int32_t newCount = performanceControls_.numStages + offset;
		if (newCount < 1)
			newCount = 1;
		if (newCount > kMaxStages)
			newCount = kMaxStages;

		if (newCount != performanceControls_.numStages) {
			performanceControls_.numStages = newCount;
			sequencerState_.totalPatternLength = calculateTotalPatternLength();

			// Clamp current stage to valid range
			if (performanceControls_.currentStage >= performanceControls_.numStages) {
				performanceControls_.currentStage = performanceControls_.numStages - 1;
			}

			// Refresh UI to show dimmed columns
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			// Show popup with new count
			extern deluge::hid::Display* display;
			if (::display) {
				char buffer[4]; // Max 1 digit for 1-8
				intToString(newCount, buffer, 1);
				::display->displayPopup(buffer);
			}
			return true;
		}
		return true; // Consume the action even if no change
	}

	// ONLY handle if a note pad is held (and Shift not pressed) - adjust velocity
	// Otherwise, allow normal horizontal scrolling and patch changes
	if (!Buttons::isShiftButtonPressed() && isNotePadHeld()) {
		int32_t stage = heldPadX_;
		if (isStageValid(stage)) {
			int32_t newVelocity = static_cast<int32_t>(stages_[stage].velocity) + offset;
			stages_[stage].velocity = static_cast<uint8_t>(SequencerMode::clampValue(newVelocity, static_cast<int32_t>(1), static_cast<int32_t>(127)));

			// Show velocity in display
			SequencerMode::displayVelocity(static_cast<uint8_t>(stages_[stage].velocity));

			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			return true;
		}
	}

	// Otherwise, use base class implementation (for control columns)
	return SequencerMode::handleHorizontalEncoder(offset, encoderPressed);
}

bool PulseSequencerMode::handleSelectEncoder(int32_t offset) {
	// Use EXACT same check as velocity/gate encoders - they work!
	// Check pad directly like velocity/gate do - don't use isNotePadHeld() wrapper
	if (heldPadX_ < 0 || heldPadX_ >= kMaxStages || heldPadY_ < 3) {
		return false;
	}

	// Access display (global variable) - same as step sequencer
	extern deluge::hid::Display* display;
	if (!::display) {
		return false;
	}

	// Check if there's already a popup showing to continue editing that parameter
	bool hasProbabilityPopup = ::display->hasPopupOfType(PopupType::PROBABILITY);
	bool hasIterancePopup = ::display->hasPopupOfType(PopupType::ITERANCE);
	bool hasPopup = hasProbabilityPopup || hasIterancePopup;

	// if there's no probability or iterance pop-up yet and we're turning encoder left, edit probability
	// if there's a probability pop-up, continue editing probability
	bool shouldEditProbability = (!hasPopup && (offset < 0)) || hasProbabilityPopup;

	// if there's no probability or iterance pop-up yet and we're turning encoder right, edit iterance
	// if there's an iterance pop-up, continue editing iterance
	bool shouldEditIterance = (!hasPopup && (offset > 0)) || hasIterancePopup;

	if (shouldEditProbability) {
		// Adjust probability in 5% increments (0-20, representing 0-100%)
		int32_t newProbability = static_cast<int32_t>(stages_[heldPadX_].probability) + offset;
		stages_[heldPadX_].probability = static_cast<uint8_t>(SequencerMode::clampValue(newProbability, static_cast<int32_t>(0), static_cast<int32_t>(kNumProbabilityValues)));

		// Show probability in display
		SequencerMode::displayProbability(static_cast<uint8_t>(stages_[heldPadX_].probability));

		// Request UI refresh to update pad display
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		return true;
	}
	else if (shouldEditIterance) {
		// Get current iterance preset index
		int32_t currentPreset = stages_[heldPadX_].iterance.toPresetIndex();
		int32_t newPreset = currentPreset + offset;

		// Clamp to valid range (0 = OFF, 1-35 = presets, 36 = CUSTOM)
		if (newPreset < 0)
			newPreset = 0;
		if (newPreset > kCustomIterancePreset)
			newPreset = kCustomIterancePreset;

		// Convert preset index to iterance value
		stages_[heldPadX_].iterance = Iterance::fromPresetIndex(newPreset);

		// Show iterance in display
		SequencerMode::displayIterance(stages_[heldPadX_].iterance);

		// Request UI refresh to update pad display
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		return true;
	}

	return false;
}

// ================================================================================================
// DISPLAY FUNCTIONS - Now use base class implementations
// ================================================================================================
// PAD INPUT HANDLERS
// ================================================================================================

void PulseSequencerMode::handleGateType(int32_t stage) {
	if (!isStageValid(stage))
		return;

	// Cycle through gate types: OFF -> SINGLE -> MULTIPLE -> HELD -> SKIP -> OFF
	int32_t currentType = static_cast<int32_t>(stages_[stage].gateType);
	int32_t nextType = (currentType + 1) % 5;
	stages_[stage].gateType = static_cast<GateType>(nextType);

	// Show popup
	const char* gateNames[] = {"OFF", "SINGLE", "MULTIPLE", "HELD", "SKIP"};
	char buffer[kPopupBufferSize];
	memcpy(buffer, "Stage ", 6);
	intToString(stage + 1, &buffer[6], 1);
	int32_t numLen = strlen(&buffer[6]);
	buffer[6 + numLen] = ':';
	buffer[6 + numLen + 1] = ' ';
	memcpy(&buffer[6 + numLen + 2], gateNames[nextType], strlen(gateNames[nextType]));
	buffer[6 + numLen + 2 + strlen(gateNames[nextType])] = 0;
	display->displayPopup(buffer);
}


void PulseSequencerMode::handleOctaveAdjustment(int32_t stage, int32_t direction) {
	if (!isStageValid(stage))
		return;

	int32_t newOctave = static_cast<int32_t>(stages_[stage].octave) + direction;
	if (newOctave < -2)
		newOctave = -2;
	if (newOctave > 3)
		newOctave = 3;

	stages_[stage].octave = static_cast<int8_t>(newOctave);

	// Show popup with octave value
	char buffer[kPopupBufferSize];
	memcpy(buffer, "Stage ", 6);
	intToString(stage + 1, &buffer[6], 1);
	int32_t numLen = strlen(&buffer[6]);
	memcpy(&buffer[6 + numLen], " Oct: ", 6);
	intToString(static_cast<int32_t>(stages_[stage].octave), &buffer[6 + numLen + 6], 1);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handlePulseCount(int32_t stage, int32_t position) {
	if (!isStageValid(stage) || position < 0 || position >= kMaxPulseCount) {
		return;
	}

	int32_t newPulseCount = position + 1;
	if (newPulseCount != static_cast<int32_t>(stages_[stage].pulseCount)) {
		stages_[stage].pulseCount = static_cast<uint8_t>(newPulseCount);
		sequencerState_.totalPatternLength = calculateTotalPatternLength();

		if (sequencerState_.currentPulse >= sequencerState_.totalPatternLength) {
			sequencerState_.currentPulse = 0;
		}
	}
}





void PulseSequencerMode::resetToDefaults() {
	performanceControls_.numStages = 8;
	performanceControls_.pingPongDirection = 1;
	performanceControls_.currentStage = 0;

	for (int32_t i = 0; i < kMaxStages; i++) {
		stages_[i].gateType = GateType::OFF;
		stages_[i].noteIndex = 0;
		stages_[i].octave = 0;
		stages_[i].pulseCount = 1;
		stages_[i].velocity = 100;
		stages_[i].velocitySpread = 0;
		stages_[i].probability = kNumProbabilityValues; // 20 = 100%
		stages_[i].iterance = kDefaultIteranceValue;
		stages_[i].gateLength = 50;
	}

	sequencerState_.totalPatternLength = calculateTotalPatternLength();
	display->displayPopup("RESET ALL");
}


void PulseSequencerMode::randomizeSequence() {
	// Update scale notes first to ensure we have the current scale
	updateScaleNotes();

	// Ensure we have at least one note in the scale
	int32_t maxNoteIndex = (displayState_.numScaleNotes > 0) ? displayState_.numScaleNotes : 1;

	for (int32_t i = 0; i < kMaxStages; i++) {
		// Randomize gate type (skip OFF for better sounding patterns)
		int32_t gateTypeIndex = (getRandom255() % 3) + 1; // 1, 2, or 3 (SINGLE, MULTIPLE, HELD)
		stages_[i].gateType = static_cast<GateType>(gateTypeIndex);

		// Randomize note index within the current scale
		stages_[i].noteIndex = getRandom255() % maxNoteIndex;

		// Randomize octave (-2 to +2, 4 octave range)
		stages_[i].octave = (getRandom255() % 5) - 2;

		// Randomize pulse count (bias toward lower values)
		uint8_t random = getRandom255();
		if (random < 128) {
			stages_[i].pulseCount = 1;
		}
		else if (random < 192) {
			stages_[i].pulseCount = 2;
		}
		else if (random < 224) {
			stages_[i].pulseCount = 3;
		}
		else if (random < 240) {
			stages_[i].pulseCount = 4;
		}
		else {
			stages_[i].pulseCount = static_cast<uint8_t>((getRandom255() % 3) + 5);
		}

		// Randomize velocity (1-127, bias toward middle-high values)
		stages_[i].velocity = (getRandom255() % 100) + 28; // 28-127 range
	}

	sequencerState_.totalPatternLength = calculateTotalPatternLength();
	display->displayPopup("RANDOMISE");
}

void PulseSequencerMode::evolveSequence() {
	// Update scale notes to get current scale size
	updateScaleNotes();
	int32_t maxNoteIndex = (displayState_.numScaleNotes > 0) ? displayState_.numScaleNotes : 1;

	int32_t numStagesToChange = (getRandom255() % 4) + 1;

	for (int32_t change = 0; change < numStagesToChange; change++) {
		int32_t stageToChange = getRandom255() % 8;

		if (getRandom255() < 179) { // 70% chance - change note
			int32_t currentNote = stages_[stageToChange].noteIndex;
			int32_t noteChange = (getRandom255() % 5) - 2; // -2 to +2
			int32_t newNote = currentNote + noteChange;

			// Wrap around within the current scale
			while (newNote < 0)
				newNote += maxNoteIndex;
			while (newNote >= maxNoteIndex)
				newNote -= maxNoteIndex;

			stages_[stageToChange].noteIndex = newNote;
		}
		else { // 30% chance - change octave
			int32_t currentOctave = static_cast<int32_t>(stages_[stageToChange].octave);
			int32_t octaveChange = (getRandom255() < 128) ? -1 : 1;
			int32_t newOctave = currentOctave + octaveChange;

			if (newOctave < -2)
				newOctave = -2;
			if (newOctave > 3)
				newOctave = 3;

			stages_[stageToChange].octave = static_cast<int8_t>(newOctave);
		}
	}
	display->displayPopup("EVOLVE");
}

// ========== GENERATIVE MUTATIONS (wired to existing functionality) ==========

void PulseSequencerMode::resetToInit() {
	// Reset to defaults
	resetToDefaults();

	// Full UI refresh
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

void PulseSequencerMode::randomizeAll(int32_t mutationRate) {
	// Use existing randomize implementation
	// TODO: Could apply mutationRate to partially randomize
	randomizeSequence();

	// Full UI refresh
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

void PulseSequencerMode::evolveNotes(int32_t mutationRate) {
	// Use existing evolve implementation
	// Run evolve multiple times based on mutation rate
	// Low %: 1-2 evolves (gentle)
	// High % (>70%): 4+ evolves (chaotic)
	int32_t numEvolves;
	if (mutationRate > 70) {
		numEvolves = (mutationRate / 20) + 1; // 80% = 5, 100% = 6 evolves
	}
	else {
		numEvolves = (mutationRate / 40) + 1; // 30% = 1, 60% = 2 evolves
	}

	for (int32_t i = 0; i < numEvolves; i++) {
		evolveSequence();
	}

	// Full UI refresh
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
}

// ========== SCENE MANAGEMENT ==========

size_t PulseSequencerMode::captureScene(void* buffer, size_t maxSize) {
	if (!buffer || maxSize == 0) {
		return 0;
	}

	uint8_t* ptr = static_cast<uint8_t*>(buffer);
	size_t offset = 0;

	// Calculate required size for pattern data + control values
	size_t stagesSize = sizeof(stages_);
	size_t perfControlsSize = sizeof(performanceControls_);
	size_t displayStateSize = sizeof(displayState_.gateLineOffset);
	size_t controlValuesSize = sizeof(int32_t) * 4; // clock, octave, transpose, direction
	size_t totalSize = stagesSize + perfControlsSize + displayStateSize + controlValuesSize;

	if (offset + totalSize > maxSize) {
		return 0; // Not enough space
	}

	// Copy stages data
	memcpy(&ptr[offset], stages_.data(), stagesSize);
	offset += stagesSize;

	// Copy performance controls
	memcpy(&ptr[offset], &performanceControls_, perfControlsSize);
	offset += perfControlsSize;

	// Copy display state offset
	memcpy(&ptr[offset], &displayState_.gateLineOffset, displayStateSize);
	offset += displayStateSize;

	// Save active control values
	CombinedEffects effects = getCombinedEffects();
	memcpy(&ptr[offset], &effects.clockDivider, sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&ptr[offset], &effects.octaveShift, sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&ptr[offset], &effects.transpose, sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&ptr[offset], &effects.direction, sizeof(int32_t));
	offset += sizeof(int32_t);

	return offset;
}

bool PulseSequencerMode::recallScene(const void* buffer, size_t size) {
	if (!buffer || size == 0) {
		return false;
	}

	const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
	size_t offset = 0;

	size_t stagesSize = sizeof(stages_);
	size_t perfControlsSize = sizeof(performanceControls_);
	size_t displayStateSize = sizeof(displayState_.gateLineOffset);
	size_t controlValuesSize = sizeof(int32_t) * 4;
	size_t totalSize = stagesSize + perfControlsSize + displayStateSize + controlValuesSize;

	if (size < totalSize) {
		return false; // Not enough data
	}

	// Restore stages data
	memcpy(stages_.data(), &ptr[offset], stagesSize);
	offset += stagesSize;

	// Restore performance controls
	memcpy(&performanceControls_, &ptr[offset], perfControlsSize);
	offset += perfControlsSize;

	// Restore display state offset
	memcpy(&displayState_.gateLineOffset, &ptr[offset], displayStateSize);
	offset += displayStateSize;

	// Restore control values
	int32_t clockDivider, octaveShift, transpose, direction;
	memcpy(&clockDivider, &ptr[offset], sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&octaveShift, &ptr[offset], sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&transpose, &ptr[offset], sizeof(int32_t));
	offset += sizeof(int32_t);
	memcpy(&direction, &ptr[offset], sizeof(int32_t));
	offset += sizeof(int32_t);

	// Apply control values by activating matching pads
	int32_t unmatchedClock, unmatchedOctave, unmatchedTranspose, unmatchedDirection;
	controlColumnState_.applyControlValues(clockDivider, octaveShift, transpose, direction, &unmatchedClock,
	                                       &unmatchedOctave, &unmatchedTranspose, &unmatchedDirection);

	// Apply unmatched values to base controls (invisible effects)
	setBaseClockDivider(unmatchedClock);
	setBaseOctaveShift(unmatchedOctave);
	setBaseTranspose(unmatchedTranspose);
	setBaseDirection(unmatchedDirection);

	// Update scale notes to current scale
	updateScaleNotes();

	return true;
}

// ========== PATTERN PERSISTENCE ==========

void PulseSequencerMode::writeToFile(Serializer& writer, bool includeScenes) {
	// Write pulse sequencer with hex-encoded data (Deluge convention)
	writer.writeOpeningTagBeginning("pulseSequencer");
	writer.writeAttribute("numStages", performanceControls_.numStages);
	writer.writeAttribute("currentPulse", sequencerState_.currentPulse);
	writer.writeAttribute("gateLineOffset", displayState_.gateLineOffset);
	writer.writeAttribute("currentStage", performanceControls_.currentStage);
	writer.writeAttribute("pingPongDirection", performanceControls_.pingPongDirection);
	writer.writeAttribute("pedalNextStage", performanceControls_.pedalNextStage);
	writer.writeAttribute("skip2OddPhase", performanceControls_.skip2OddPhase ? 1 : 0);
	writer.writeAttribute("pendulumGoingUp", performanceControls_.pendulumGoingUp ? 1 : 0);
	writer.writeAttribute("pendulumLow", performanceControls_.pendulumLow);
	writer.writeAttribute("pendulumHigh", performanceControls_.pendulumHigh);
	writer.writeAttribute("spiralFromLow", performanceControls_.spiralFromLow ? 1 : 0);
	writer.writeAttribute("spiralLow", performanceControls_.spiralLow);
	writer.writeAttribute("spiralHigh", performanceControls_.spiralHigh);

	// Prepare stage data as byte array for writeAttributeHexBytes (10 bytes per stage)
	uint8_t stageData[kMaxStages * 10];
	for (int32_t i = 0; i < kMaxStages; ++i) {
		int32_t offset = i * 10;
		// Byte 0: gate type (0-3)
		stageData[offset] = static_cast<uint8_t>(stages_[i].gateType);
		// Byte 1: noteIndex (0-31)
		stageData[offset + 1] = static_cast<uint8_t>(stages_[i].noteIndex);
		// Byte 2: octave + 3 (unsigned 0-6 for -3 to +3)
		stageData[offset + 2] = static_cast<uint8_t>(stages_[i].octave + 3);
		// Byte 3: pulse count (1-8)
		stageData[offset + 3] = static_cast<uint8_t>(stages_[i].pulseCount);
		// Byte 4: velocity (1-127)
		stageData[offset + 4] = static_cast<uint8_t>(stages_[i].velocity);
		// Byte 5: velocity spread (0-127)
		stageData[offset + 5] = static_cast<uint8_t>(stages_[i].velocitySpread);
		// Byte 6: probability (0-20, representing 0-100% in 5% increments)
		stageData[offset + 6] = static_cast<uint8_t>(stages_[i].probability);
		// Byte 7: gate length (0-100)
		stageData[offset + 7] = static_cast<uint8_t>(stages_[i].gateLength);
		// Bytes 8-9: iterance (2 bytes, uint16_t)
		uint16_t iteranceInt = stages_[i].iterance.toInt();
		stageData[offset + 8] = static_cast<uint8_t>(iteranceInt & 0xFF);
		stageData[offset + 9] = static_cast<uint8_t>((iteranceInt >> 8) & 0xFF);
	}

	// Always write stageData (fixed size array)
	writer.writeAttributeHexBytes("stageData", stageData, kMaxStages * 10);

	// stageEnabled removed - now using SKIP gate type instead
	writer.closeTag(); // Self-closing tag since no child content

	// Write control columns and scenes
	controlColumnState_.writeToFile(writer, includeScenes);
}

Error PulseSequencerMode::readFromFile(Deserializer& reader) {
	char const* tagName;

	// Read pulseSequencer tag
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "numStages")) {
			performanceControls_.numStages = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "currentPulse")) {
			sequencerState_.currentPulse = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "gateLineOffset")) {
			displayState_.gateLineOffset = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "playOrder")) {
			// Legacy field - now handled by control columns, read and ignore for backward compatibility
			reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "clockDivider")) {
			// Legacy field - now handled by control columns, read and ignore for backward compatibility
			reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "currentStage")) {
			performanceControls_.currentStage = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "pingPongDirection")) {
			performanceControls_.pingPongDirection = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "pedalNextStage")) {
			performanceControls_.pedalNextStage = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "skip2OddPhase")) {
			performanceControls_.skip2OddPhase = (reader.readTagOrAttributeValueInt() != 0);
		}
		else if (!strcmp(tagName, "pendulumGoingUp")) {
			performanceControls_.pendulumGoingUp = (reader.readTagOrAttributeValueInt() != 0);
		}
		else if (!strcmp(tagName, "pendulumLow")) {
			performanceControls_.pendulumLow = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "pendulumHigh")) {
			performanceControls_.pendulumHigh = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "spiralFromLow")) {
			performanceControls_.spiralFromLow = (reader.readTagOrAttributeValueInt() != 0);
		}
		else if (!strcmp(tagName, "spiralLow")) {
			performanceControls_.spiralLow = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "spiralHigh")) {
			performanceControls_.spiralHigh = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "stageData")) {
			// Parse hex string: 10 bytes per stage (new format with iterance)
			// Backwards compatible with old 8-byte format
			char const* hexData = reader.readTagOrAttributeValue();

			// Skip "0x" prefix if present
			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			// Determine format: old format = 8 bytes (16 hex chars) per stage, new = 10 bytes (20 hex chars) per stage
			size_t hexLen = strlen(hexData);
			int32_t bytesPerStage = (hexLen >= 160) ? 10 : 8; // New format has 160+ chars (8 stages * 20 hex chars)

			// Parse 8 stages
			for (int32_t i = 0; i < kMaxStages; ++i) {
				int32_t offset = i * (bytesPerStage * 2); // 2 hex chars per byte

				// Byte 0: gate type
				stages_[i].gateType = static_cast<GateType>(hexToIntFixedLength(&hexData[offset], 2));

				// Byte 1: noteIndex
				stages_[i].noteIndex = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 2], 2));

				// Byte 2: octave (stored as +3)
				stages_[i].octave = static_cast<int8_t>(hexToIntFixedLength(&hexData[offset + 4], 2) - 3);

				// Byte 3: pulse count
				stages_[i].pulseCount = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 6], 2));

				// Byte 4: velocity (1-127)
				uint8_t velocity = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 8], 2));
				if (velocity < 1)
					velocity = 1;
				if (velocity > 127)
					velocity = 127;
				stages_[i].velocity = velocity;

				// Byte 5: velocity spread
				stages_[i].velocitySpread = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 10], 2));

				if (bytesPerStage >= 10) {
					// New format (10 bytes): read probability, gateLength, and iterance
					stages_[i].probability = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 12], 2));
					if (stages_[i].probability > kNumProbabilityValues) {
						stages_[i].probability = kNumProbabilityValues; // Default if invalid (20 = 100%)
					}
					stages_[i].gateLength = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 14], 2));
					// Bytes 8-9: iterance (2 bytes, uint16_t)
					uint16_t iteranceInt = (static_cast<uint16_t>(hexToIntFixedLength(&hexData[offset + 16], 2)) << 8)
					                       | static_cast<uint16_t>(hexToIntFixedLength(&hexData[offset + 18], 2));
					stages_[i].iterance = Iterance::fromInt(iteranceInt);
				}
				else {
					// Old format (8 bytes): read probability and gateLength, convert probability, default iterance
					// Convert old probability (0-100) to new format (0-20)
					uint8_t oldProb = hexToIntFixedLength(&hexData[offset + 12], 2);
					if (oldProb > 100)
						oldProb = 100;
					stages_[i].probability = static_cast<uint8_t>(oldProb / 5); // Convert 0-100 to 0-20
					stages_[i].gateLength = static_cast<uint8_t>(hexToIntFixedLength(&hexData[offset + 14], 2));
					stages_[i].iterance = kDefaultIteranceValue; // Default iterance
				}
			}
		}
		else if (!strcmp(tagName, "stageEnabled")) {
			// Legacy field - convert to SKIP gate type for backward compatibility
			char const* hexData = reader.readTagOrAttributeValue();

			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			uint8_t enabledBits = hexToIntFixedLength(hexData, 2);
			for (int32_t i = 0; i < kMaxStages; ++i) {
				// If stage was disabled, set gate type to SKIP
				if ((enabledBits & (1 << i)) == 0) {
					stages_[i].gateType = GateType::SKIP;
				}
			}
		}
		else {
			// Unknown tag - let the caller handle it
			// Don't call reader.exitTag() here to avoid XML parser state issues
			break;
		}
	}

	// After loading, update scale notes cache for current clip/song
	updateScaleNotes();

	return Error::NONE;
}

// Helper methods for default pattern management
bool PulseSequencerMode::isDefaultPattern() const {
	// Check if current pattern matches the exact default we would create
	for (int32_t i = 0; i < kMaxStages; i++) {
		// Default pattern: all gates OFF, note 0, pulse count 1, other values default
		GateType expectedGate = GateType::OFF;
		int32_t expectedNoteIndex = 0; // First note in scale
		int32_t expectedOctave = 0;
		int32_t expectedPulseCount = 1;
		int32_t expectedVelocity = 100;
		int32_t expectedVelocitySpread = 0;
		int32_t expectedProbability = 100;
		int32_t expectedGateLength = 50;

		if (stages_[i].gateType != expectedGate || stages_[i].noteIndex != expectedNoteIndex
		    || stages_[i].octave != expectedOctave || stages_[i].pulseCount != expectedPulseCount
		    || stages_[i].velocity != expectedVelocity) {
			return false; // Found non-default data
		}
	}
	return true; // All stages match default pattern
}

void PulseSequencerMode::setDefaultPattern() {
	// Set the default pattern - centralized so it's easy to change later
	for (int32_t i = 0; i < kMaxStages; i++) {
		stages_[i].gateType = GateType::OFF; // All gates OFF by default
		stages_[i].noteIndex = 0;            // First note in scale
		stages_[i].octave = 0;
		stages_[i].pulseCount = 1; // Pulse count 1
		stages_[i].velocity = 100;
		stages_[i].velocitySpread = 0;
		stages_[i].probability = kNumProbabilityValues; // 20 = 100%
		stages_[i].iterance = kDefaultIteranceValue;
		stages_[i].gateLength = 50;
	}
}

bool PulseSequencerMode::copyFrom(SequencerMode* other) {
	// Use static_cast since RTTI is disabled - caller must ensure types match
	PulseSequencerMode* otherPulse = static_cast<PulseSequencerMode*>(other);

	// Copy all stage data
	stages_ = otherPulse->stages_;

	// Copy sequencer state
	sequencerState_ = otherPulse->sequencerState_;

	// Copy performance controls
	performanceControls_ = otherPulse->performanceControls_;

	// Copy display state
	displayState_ = otherPulse->displayState_;

	// Copy playback state (critical for playback to work)
	initialized_ = otherPulse->initialized_;
	ticksPerSixteenthNote_ = otherPulse->ticksPerSixteenthNote_;
	lastAbsolutePlaybackPos_ = otherPulse->lastAbsolutePlaybackPos_;

	// Copy control columns
	controlColumnState_ = otherPulse->controlColumnState_;

	return true;
}

} // namespace deluge::model::clip::sequencer::modes
