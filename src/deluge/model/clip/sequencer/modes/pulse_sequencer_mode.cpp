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
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>

// Forward declaration for note name conversion
extern void noteCodeToString(int32_t noteCode, char* buffer, int32_t* getLengthWithoutDot, bool appendOctaveNo);

namespace deluge::model::clip::sequencer::modes {

void PulseSequencerMode::initialize() {
	initialized_ = true;
	ticksPerSixteenthNote_ = 0;

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

	// Only initialize performance controls if not already set (preserve loaded data)
	if (performanceControls_.numStages == 0) {
		performanceControls_.transpose = 0;
		performanceControls_.octave = 0;
		performanceControls_.clockDivider = 1; // Default: 16th notes (1=16th, 2=8th, 4=quarter)
		performanceControls_.numStages = 8;
		performanceControls_.playOrder = PlayOrder::FORWARDS;
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

	initialized_ = false;
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
	return isStageValid(stage) && stage < performanceControls_.numStages && performanceControls_.stageEnabled[stage];
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
	if (!isStageActive(stage)) {
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

int32_t PulseSequencerMode::cycleValue(int32_t current, const int32_t* values, int32_t count) const {
	for (int32_t i = 0; i < count; i++) {
		if (current == values[i]) {
			return values[(i + 1) % count];
		}
	}
	return values[0]; // Default to first value if not found
}

int32_t PulseSequencerMode::calculateTotalPatternLength() const {
	int32_t totalLength = 0;
	for (int32_t i = 0; i < performanceControls_.numStages; i++) {
		totalLength += stages_[i].pulseCount;
	}
	return totalLength;
}

int32_t PulseSequencerMode::getTicksPerPeriod(int32_t baseTicks) const {
	// Apply performance control clock divider first
	// Clock divider modes: 0=*2, 1=*1(default), 2=/2, 3=/4, 4=/8, 5=/16, 6=/32, 7=/64
	int32_t ticks = baseTicks;
	switch (performanceControls_.clockDivider) {
	case 0:
		ticks = baseTicks / 2; // *2 (32nd notes, faster)
		break;
	case 1:
		ticks = baseTicks; // *1 (16th notes, default)
		break;
	case 2:
		ticks = baseTicks * 2; // /2 (8th notes)
		break;
	case 3:
		ticks = baseTicks * 4; // /4 (quarter notes)
		break;
	case 4:
		ticks = baseTicks * 8; // /8
		break;
	case 5:
		ticks = baseTicks * 16; // /16
		break;
	case 6:
		ticks = baseTicks * 32; // /32
		break;
	case 7:
		ticks = baseTicks * 64; // /64
		break;
	default:
		ticks = baseTicks;
		break;
	}

	// Apply control column clock divider on top
	// Apply control column clock divider
	// Positive = slower (/2, /4), Negative = faster (*2, *4)
	CombinedEffects effects = getCombinedEffects();
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

	// y0: Clock divider/multiplier (x8-15) - RED
	if (whichRows & (1 << 0)) {
		// x8=*2, x9=*1(default), x10=/2, x11=/4, x12=/8, x13=/16, x14=/32, x15=/64
		for (int32_t x = 8; x < 16; x++) {
			bool isSelected = (performanceControls_.clockDivider == (x - 8));
			RGB color = isSelected ? RGB{255, 0, 0} : RGB{64, 0, 0};
			image[0 * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[0][x] = isSelected ? 64 : 32;
			}
		}
	}

	// y4: Stage count control (1-8 stages)
	if (whichRows & (1 << 4)) {
		for (int32_t x = 8; x < kDisplayWidth; x++) {
			int32_t stageNum = x - 7;
			RGB color = (stageNum <= performanceControls_.numStages) ? RGB{255, 255, 0} : RGB{0, 0, 0};
			image[4 * imageWidth + x] = color;
			if (occupancyMask && stageNum <= performanceControls_.numStages) {
				occupancyMask[4][x] = 64;
			}
		}
	}

	// y3: Stage enable/disable toggle (x8-15)
	if (whichRows & (1 << 3)) {
		for (int32_t x = 8; x < kDisplayWidth; x++) {
			int32_t stageIndex = x - 8;
			RGB color = performanceControls_.stageEnabled[stageIndex] ? RGB{255, 128, 0} : RGB{0, 0, 0};
			image[3 * imageWidth + x] = color;
			if (occupancyMask && performanceControls_.stageEnabled[stageIndex]) {
				occupancyMask[3][x] = 48;
			}
		}
	}

	// y2: Gate length per stage (x8-15) - GREEN gradient
	if (whichRows & (1 << 2)) {
		for (int32_t x = 8; x < 16; x++) {
			int32_t stage = x - 8;
			int32_t gateLen = stages_[stage].gateLength;
			int32_t intensity = (gateLen * 255) / 100; // 0-100 -> 0-255
			int32_t g = (intensity > 32) ? intensity : 32;
			RGB color = RGB{0, static_cast<uint8_t>(g), 0};
			image[2 * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[2][x] = 32;
			}
		}
	}

	// y1: Play order presets (x8-15)
	if (whichRows & (1 << 1)) {
		for (int32_t x = 8; x < 16; x++) {
			RGB color =
			    (static_cast<int32_t>(performanceControls_.playOrder) == (x - 8)) ? RGB{0, 255, 255} : RGB{0, 128, 128};
			image[1 * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[1][x] = (static_cast<int32_t>(performanceControls_.playOrder) == (x - 8)) ? 64 : 32;
			}
		}
	}

	// y5: Velocity spread per stage (x8-15) - Cyan gradient
	if (whichRows & (1 << 5)) {
		for (int32_t x = 8; x < 16; x++) {
			int32_t stage = x - 8;
			int32_t spread = stages_[stage].velocitySpread;
			int32_t intensity = (spread * 255) / 127; // 0-127 -> 0-255
			int32_t g = (intensity > 32) ? intensity : 32;
			int32_t b = (intensity > 32) ? intensity : 32;
			RGB color = RGB{0, static_cast<uint8_t>(g), static_cast<uint8_t>(b)};
			image[5 * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[5][x] = 32;
			}
		}
	}

	// y6: Probability per stage (x8-15) - Dark blue gradient
	if (whichRows & (1 << 6)) {
		for (int32_t x = 8; x < 16; x++) {
			int32_t stage = x - 8;
			int32_t prob = stages_[stage].probability;
			int32_t intensity = (prob * 255) / 100; // 0-100 -> 0-255
			int32_t val = (intensity > 32) ? intensity : 32;
			RGB color = RGB{0, 0, static_cast<uint8_t>(val)};
			image[6 * imageWidth + x] = color;
			if (occupancyMask) {
				occupancyMask[6][x] = 32;
			}
		}
	}

	// y7: Control buttons (x8-15)
	if (whichRows & (1 << 7)) {
		// Control buttons (x8-15)
		image[7 * imageWidth + 8] = RGB{128, 0, 255};  // Purple reset all
		image[7 * imageWidth + 9] = RGB{255, 0, 128};  // Magenta randomize
		image[7 * imageWidth + 10] = RGB{0, 255, 255}; // Cyan evolve
		image[7 * imageWidth + 11] = RGB{0, 100, 255}; // Blue reset performance controls

		// Transpose controls
		if (performanceControls_.transpose != 0) {
			image[7 * imageWidth + 12] = (performanceControls_.transpose < 0) ? RGB{255, 128, 0} : RGB{64, 32, 0};
			image[7 * imageWidth + 13] = (performanceControls_.transpose > 0) ? RGB{255, 128, 0} : RGB{64, 32, 0};
		}
		else {
			image[7 * imageWidth + 12] = RGB{64, 32, 0};
			image[7 * imageWidth + 13] = RGB{64, 32, 0};
		}

		// Octave controls
		if (performanceControls_.octave != 0) {
			image[7 * imageWidth + 14] = (performanceControls_.octave < 0) ? RGB{255, 0, 255} : RGB{64, 0, 64};
			image[7 * imageWidth + 15] = (performanceControls_.octave > 0) ? RGB{255, 0, 255} : RGB{64, 0, 64};
		}
		else {
			image[7 * imageWidth + 14] = RGB{64, 0, 64};
			image[7 * imageWidth + 15] = RGB{64, 0, 64};
		}

		if (occupancyMask) {
			for (int32_t x = 8; x < 16; x++) {
				occupancyMask[7][x] = 48;
			}
		}
	}

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
	static uint32_t lastRefreshTick = 0;
	uint32_t currentTick = playbackHandler.getCurrentInternalTickCount();
	if (currentTick - lastRefreshTick > 10) { // Refresh every 10 ticks for smooth updates
		int32_t gateLineY = getGateLineY();
		uint32_t rowsToRefresh = (1 << gateLineY);

		int32_t noteY = getNoteRowY(stages_[performanceControls_.currentStage].noteIndex);
		if (noteY >= 0 && noteY < kDisplayHeight) {
			rowsToRefresh |= (1 << noteY);
		}

		uiNeedsRendering(&instrumentClipView, rowsToRefresh, 0);
		lastRefreshTick = currentTick;
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

	if (stage >= 0 && stage < performanceControls_.numStages && performanceControls_.stageEnabled[stage]) {
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

	if (stageData.gateType == GateType::OFF) {
		return;
	}

	// Get all scale notes starting from C3 (MIDI 60) for better range
	int32_t scaleNotes[64];
	int32_t numNotes = getScaleNotes(modelStackPtr, scaleNotes, 64, 6, 0); // 6 octaves starting from root

	if (numNotes == 0) {
		return;
	}

	// Get control column effects
	CombinedEffects effects = getCombinedEffects();

	// Calculate note index with transpose (performance controls + control columns)
	int32_t totalTranspose = performanceControls_.transpose + effects.transpose;
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

	// Apply stage octave and global octave offsets (performance controls + control columns)
	int32_t totalOctaveShift = performanceControls_.octave + effects.octaveShift;
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

	if (freeSlot >= 0) {
		// Apply probability check
		if (!shouldPlayBasedOnProbability(stageData.probability)) {
			return; // Don't play this note
		}

		// Apply velocity spread
		uint8_t baseVelocity = 100;
		uint8_t velocity = applyVelocitySpread(baseVelocity, stageData.velocitySpread);

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
		if (performanceControls_.stageEnabled[i]) {
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
	// Special play orders that don't use the standard find-next-enabled loop
	switch (performanceControls_.playOrder) {
	case PlayOrder::RANDOM:
		advanceRandom();
		return;
	case PlayOrder::PEDAL:
		advancePedal();
		return;
	case PlayOrder::SKIP_2:
		advanceSkip2();
		return;
	case PlayOrder::PENDULUM:
		advancePendulum();
		return;
	case PlayOrder::SPIRAL:
		advanceSpiral();
		return;
	default:
		break; // Fall through to standard advancement
	}

	// Standard advancement for FORWARDS/BACKWARDS/PING_PONG
	int32_t nextStage = performanceControls_.currentStage;
	int32_t direction = (performanceControls_.playOrder == PlayOrder::BACKWARDS) ? -1 : 1;
	if (performanceControls_.playOrder == PlayOrder::PING_PONG) {
		direction = performanceControls_.pingPongDirection;
	}

	int32_t attempts = 0;
	do {
		nextStage += direction;
		attempts++;

		if (performanceControls_.playOrder == PlayOrder::PING_PONG) {
			advancePingPong(nextStage, direction);
		}
		else if (performanceControls_.playOrder == PlayOrder::FORWARDS) {
			advanceForwards(nextStage);
		}
		else {
			advanceBackwards(nextStage);
		}

		if (attempts > performanceControls_.numStages) {
			return; // Safety: avoid infinite loop
		}

	} while (!performanceControls_.stageEnabled[nextStage]);

	performanceControls_.currentStage = nextStage;
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

	// Only handle presses (not releases) for mode-specific controls
	if (velocity == 0) {
		return false; // Let releases pass through
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
			if (noteIdx >= 0 && noteIdx < displayState_.numScaleNotes) {
				// Select this note for this stage
				int32_t stage = x;
				stages_[stage].noteIndex = noteIdx;

				// Show popup with note name
				Song* song = currentSong;
				if (song) {
					// Get control column effects
					CombinedEffects effects = getCombinedEffects();

					int32_t rootNote = song->key.rootNote;
					int32_t scaleNoteOffset = displayState_.scaleNotes[noteIdx];
					int32_t noteCode = rootNote + scaleNoteOffset + 48;
					int32_t totalOctaveShift = performanceControls_.octave + effects.octaveShift;
					noteCode += (stages_[stage].octave * 12) + (totalOctaveShift * 12);

					if (noteCode < 0)
						noteCode = 0;
					if (noteCode > 127)
						noteCode = 127;

					char noteNameBuffer[kNoteNameBufferSize];
					int32_t lengthDummy = 0;
					noteCodeToString(noteCode, noteNameBuffer, &lengthDummy, true);
					showStagePopup(stage, "Stage %d: %s", stage + 1, noteNameBuffer);
				}
				return true;
			}
		}
	}

	// RIGHT SIDE FIXED CONTROLS (x8-15) - these have both fixed and dynamic functions
	// y5: Velocity spread (x8-15)
	if (y == 5 && x >= 8 && x < 16) {
		handleVelocitySpread(x - 8);
		return true;
	}
	// y6: Probability (x8-15)
	else if (y == 6 && x >= 8 && x < 16) {
		handleProbability(x - 8);
		return true;
	}
	// y7: Control buttons (x8-15)
	else if (y == 7 && x >= 8) {
		if (x == 8) {
			resetToDefaults();
			return true;
		}
		else if (x == 9) {
			randomizeSequence();
			return true;
		}
		else if (x == 10) {
			evolveSequence();
			return true;
		}
		else if (x == 11) {
			resetPerformanceControls();
			return true;
		}
		else if (x == 12) {
			handleTransposeChange(-1);
			return true;
		}
		else if (x == 13) {
			handleTransposeChange(1);
			return true;
		}
		else if (x == 14) {
			handleOctaveChange(-1);
			return true;
		}
		else if (x == 15) {
			handleOctaveChange(1);
			return true;
		}
	}

	// RIGHT SIDE FIXED CONTROLS (x8-15)
	// y4: Stage count control
	if (y == 4 && x >= 8 && x < kDisplayWidth) {
		handleStageCountChange(x - 7); // 1-8
		return true;
	}
	// y3: Stage enable/disable toggle
	else if (y == 3 && x >= 8 && x < kDisplayWidth) {
		handleStageToggle(x - 8); // 0-7
		return true;
	}
	// y2: Gate length (x8-15)
	else if (y == 2 && x >= 8 && x < 16) {
		handleGateLength(x - 8);
		return true;
	}
	// y1: Play order presets
	else if (y == 1 && x >= 8 && x < 16) {
		handlePlayOrderChange(x - 8); // 0-7
		return true;
	}
	// y0: Clock divider (x8-15)
	else if (y == 0 && x >= 8 && x < 16) {
		handleClockDividerChange(x - 8); // 0-7
		return true;
	}

	return false; // Didn't handle this pad
}

bool PulseSequencerMode::handleModeSpecificVerticalEncoder(int32_t offset) {
	// Scroll the entire left side (gate line + pulse counts + octave controls + notes)
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
	snprintf(buffer, sizeof(buffer), "Scroll: %d", displayState_.gateLineOffset);
	display->displayPopup(buffer);

	return true; // We handled it
}

// ================================================================================================
// PAD INPUT HANDLERS
// ================================================================================================

void PulseSequencerMode::handleGateType(int32_t stage) {
	if (!isStageValid(stage))
		return;

	// Cycle through gate types: OFF -> SINGLE -> MULTIPLE -> HELD -> OFF
	int32_t currentType = static_cast<int32_t>(stages_[stage].gateType);
	int32_t nextType = (currentType + 1) % 4;
	stages_[stage].gateType = static_cast<GateType>(nextType);

	// Show popup
	const char* gateNames[] = {"OFF", "SINGLE", "MULTIPLE", "HELD"};
	char buffer[kPopupBufferSize];
	snprintf(buffer, sizeof(buffer), "Stage %d: %s", stage + 1, gateNames[nextType]);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handleNoteSelection(int32_t stage) {
	if (!isStageValid(stage))
		return;

	// Cycle through note indices (0-15 for more range)
	stages_[stage].noteIndex = (stages_[stage].noteIndex + 1) % 16;

	// Calculate the actual note to show its name
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip && clip->output->type == OutputType::SYNTH) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack =
		    setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);

		int32_t scaleNotes[64];
		int32_t numNotes = getScaleNotes(modelStack, scaleNotes, 64, 6, 0);

		if (numNotes > 0) {
			// Get control column effects
			CombinedEffects effects = getCombinedEffects();

			// Calculate note with current settings (performance controls + control columns)
			int32_t totalTranspose = performanceControls_.transpose + effects.transpose;
			int32_t noteIndexInScale = stages_[stage].noteIndex + totalTranspose;
			while (noteIndexInScale < 0)
				noteIndexInScale += numNotes;
			while (noteIndexInScale >= numNotes)
				noteIndexInScale -= numNotes;

			int32_t note = scaleNotes[noteIndexInScale] + 48; // Base C3 offset
			int32_t totalOctaveShift = performanceControls_.octave + effects.octaveShift;
			note += (stages_[stage].octave * 12) + (totalOctaveShift * 12);

			if (note < 0)
				note = 0;
			if (note > 127)
				note = 127;

			// Convert to note name
			char noteNameBuffer[kNoteNameBufferSize];
			int32_t lengthDummy = 0;
			noteCodeToString(note, noteNameBuffer, &lengthDummy, true);
			showStagePopup(stage, "Stage %d: %s", stage + 1, noteNameBuffer);
			return;
		}
	}

	// Fallback
	char buffer[kPopupBufferSize];
	snprintf(buffer, sizeof(buffer), "Stage %d Note: %d", stage + 1, stages_[stage].noteIndex + 1);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handleOctaveAdjustment(int32_t stage, int32_t direction) {
	if (!isStageValid(stage))
		return;

	int32_t newOctave = stages_[stage].octave + direction;
	if (newOctave < -2)
		newOctave = -2;
	if (newOctave > 3)
		newOctave = 3;

	stages_[stage].octave = newOctave;

	// Show popup with octave value
	char buffer[kPopupBufferSize];
	snprintf(buffer, sizeof(buffer), "Stage %d Oct: %+d", stage + 1, stages_[stage].octave);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handlePulseCount(int32_t stage, int32_t position) {
	if (!isStageValid(stage) || position < 0 || position >= kMaxPulseCount) {
		return;
	}

	int32_t newPulseCount = position + 1;
	if (newPulseCount != stages_[stage].pulseCount) {
		stages_[stage].pulseCount = newPulseCount;
		sequencerState_.totalPatternLength = calculateTotalPatternLength();

		if (sequencerState_.currentPulse >= sequencerState_.totalPatternLength) {
			sequencerState_.currentPulse = 0;
		}
	}
}

void PulseSequencerMode::handleStageCountChange(int32_t numStages) {
	if (numStages < 1)
		numStages = 1;
	if (numStages > 8)
		numStages = 8;

	if (performanceControls_.numStages != numStages) {
		performanceControls_.numStages = numStages;
		sequencerState_.totalPatternLength = calculateTotalPatternLength();

		char buffer[30];
		snprintf(buffer, sizeof(buffer), "Stages: %d", numStages);
		display->displayPopup(buffer);
	}
}

void PulseSequencerMode::handlePlayOrderChange(int32_t playOrderIndex) {
	if (playOrderIndex < 0 || playOrderIndex > 7) {
		return;
	}

	PlayOrder newPlayOrder = static_cast<PlayOrder>(playOrderIndex);
	if (performanceControls_.playOrder != newPlayOrder) {
		performanceControls_.playOrder = newPlayOrder;
		performanceControls_.pingPongDirection = 1;

		// Show popup with order name
		const char* orderNames[] = {"FORWARDS", "BACKWARDS", "PING PONG", "RANDOM",
		                            "PEDAL",    "SKIP 2",    "PENDULUM",  "SPIRAL"};
		display->displayPopup(orderNames[playOrderIndex]);
	}
}

void PulseSequencerMode::handleClockDividerChange(int32_t dividerMode) {
	if (dividerMode < 0 || dividerMode > 7) {
		return;
	}

	if (performanceControls_.clockDivider != dividerMode) {
		performanceControls_.clockDivider = dividerMode;

		// Show popup with clock rate name
		const char* clockNames[] = {"32nd", "16th", "8th", "Quarter", "/8", "/16", "/32", "/64"};
		display->displayPopup(clockNames[dividerMode]);
	}
}

void PulseSequencerMode::handleTransposeChange(int32_t direction) {
	performanceControls_.transpose += direction;

	if (performanceControls_.transpose < -12) {
		performanceControls_.transpose = -12;
	}
	if (performanceControls_.transpose > 12) {
		performanceControls_.transpose = 12;
	}

	// Show popup
	char buffer[20];
	snprintf(buffer, sizeof(buffer), "Transpose: %+d", performanceControls_.transpose);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handleOctaveChange(int32_t direction) {
	performanceControls_.octave += direction;

	if (performanceControls_.octave < -3) {
		performanceControls_.octave = -3;
	}
	if (performanceControls_.octave > 3) {
		performanceControls_.octave = 3;
	}

	// Show popup
	char buffer[20];
	snprintf(buffer, sizeof(buffer), "Octave: %+d", performanceControls_.octave);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handleStageToggle(int32_t stage) {
	if (!isStageValid(stage))
		return;
	performanceControls_.stageEnabled[stage] = !performanceControls_.stageEnabled[stage];

	const char* status = performanceControls_.stageEnabled[stage] ? "ON" : "OFF";
	char buffer[30];
	snprintf(buffer, sizeof(buffer), "Stage %d: %s", stage + 1, status);
	display->displayPopup(buffer);
}

void PulseSequencerMode::handleVelocitySpread(int32_t stage) {
	if (!isStageValid(stage))
		return;

	static const int32_t spreads[] = {0, 20, 40, 60, 80, 100, 127};
	stages_[stage].velocitySpread = cycleValue(stages_[stage].velocitySpread, spreads, 7);
	showStagePopup(stage, "Stage %d Spread: %d", stage + 1, stages_[stage].velocitySpread);
}

void PulseSequencerMode::handleProbability(int32_t stage) {
	if (!isStageValid(stage))
		return;

	static const int32_t probs[] = {100, 80, 60, 40, 20};
	stages_[stage].probability = cycleValue(stages_[stage].probability, probs, 5);
	showStagePopup(stage, "Stage %d Prob: %d%%", stage + 1, stages_[stage].probability);
}

void PulseSequencerMode::handleGateLength(int32_t stage) {
	if (!isStageValid(stage))
		return;

	static const int32_t lengths[] = {10, 25, 50, 75, 90, 100};
	stages_[stage].gateLength = cycleValue(stages_[stage].gateLength, lengths, 6);
	showStagePopup(stage, "Stage %d Gate: %d%%", stage + 1, stages_[stage].gateLength);
}

void PulseSequencerMode::resetToDefaults() {
	performanceControls_.transpose = 0;
	performanceControls_.octave = 0;
	performanceControls_.clockDivider = 1; // Default: 16th notes (1=16th, 2=8th, 4=quarter)
	performanceControls_.numStages = 8;
	performanceControls_.playOrder = PlayOrder::FORWARDS;
	performanceControls_.pingPongDirection = 1;
	performanceControls_.currentStage = 0;

	for (int32_t i = 0; i < kMaxStages; i++) {
		performanceControls_.stageEnabled[i] = true;
		stages_[i].gateType = GateType::OFF;
		stages_[i].noteIndex = 0;
		stages_[i].octave = 0;
		stages_[i].pulseCount = 1;
		stages_[i].velocitySpread = 0;
		stages_[i].probability = 100;
		stages_[i].gateLength = 50;
	}

	sequencerState_.totalPatternLength = calculateTotalPatternLength();
	display->displayPopup("RESET ALL");
}

void PulseSequencerMode::resetPerformanceControls() {
	// Reset all stages to default performance values
	for (int32_t i = 0; i < kMaxStages; i++) {
		stages_[i].velocitySpread = 0;
		stages_[i].probability = 100;
		stages_[i].gateLength = 50;
	}
	display->displayPopup("RESET PERF");
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
			stages_[i].pulseCount = (getRandom255() % 3) + 5;
		}
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
			int32_t currentOctave = stages_[stageToChange].octave;
			int32_t octaveChange = (getRandom255() < 128) ? -1 : 1;
			int32_t newOctave = currentOctave + octaveChange;

			if (newOctave < -2)
				newOctave = -2;
			if (newOctave > 3)
				newOctave = 3;

			stages_[stageToChange].octave = newOctave;
		}
	}
	display->displayPopup("EVOLVE");
}

// ========== GENERATIVE MUTATIONS (wired to existing functionality) ==========

void PulseSequencerMode::resetToInit() {
	// Reset to defaults and clear performance controls
	resetToDefaults();
	resetPerformanceControls();

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
	writer.writeAttribute("playOrder", static_cast<int32_t>(performanceControls_.playOrder));
	writer.writeAttribute("clockDivider", performanceControls_.clockDivider);
	writer.writeAttribute("currentStage", performanceControls_.currentStage);
	writer.writeAttribute("pingPongDirection", performanceControls_.pingPongDirection);

	// Prepare stage data as byte array for writeAttributeHexBytes (7 bytes per stage)
	uint8_t stageData[kMaxStages * 7];
	for (int32_t i = 0; i < kMaxStages; ++i) {
		int32_t offset = i * 7;
		// Byte 0: gate type (0-3)
		stageData[offset] = static_cast<uint8_t>(stages_[i].gateType);
		// Byte 1: noteIndex (0-31)
		stageData[offset + 1] = static_cast<uint8_t>(stages_[i].noteIndex);
		// Byte 2: octave + 3 (unsigned 0-6 for -3 to +3)
		stageData[offset + 2] = static_cast<uint8_t>(stages_[i].octave + 3);
		// Byte 3: pulse count (1-8)
		stageData[offset + 3] = static_cast<uint8_t>(stages_[i].pulseCount);
		// Byte 4: velocity spread (0-127)
		stageData[offset + 4] = static_cast<uint8_t>(stages_[i].velocitySpread);
		// Byte 5: probability (0-100)
		stageData[offset + 5] = static_cast<uint8_t>(stages_[i].probability);
		// Byte 6: gate length (0-100)
		stageData[offset + 6] = static_cast<uint8_t>(stages_[i].gateLength);
	}

	// Always write stageData (fixed size array)
	writer.writeAttributeHexBytes("stageData", stageData, kMaxStages * 7);

	// Write stage enabled flags as hex byte
	uint8_t enabledBits = 0;
	for (int32_t i = 0; i < kMaxStages; ++i) {
		if (performanceControls_.stageEnabled[i]) {
			enabledBits |= (1 << i);
		}
	}
	writer.writeAttributeHexBytes("stageEnabled", &enabledBits, 1);
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
			performanceControls_.playOrder = static_cast<PlayOrder>(reader.readTagOrAttributeValueInt());
		}
		else if (!strcmp(tagName, "clockDivider")) {
			performanceControls_.clockDivider = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "currentStage")) {
			performanceControls_.currentStage = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "pingPongDirection")) {
			performanceControls_.pingPongDirection = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "stageData")) {
			// Parse hex string: 7 bytes per stage
			char const* hexData = reader.readTagOrAttributeValue();

			// Skip "0x" prefix if present
			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			// Parse 8 stages
			for (int32_t i = 0; i < kMaxStages; ++i) {
				int32_t offset = i * 14; // 7 bytes = 14 hex chars

				// Byte 0: gate type
				stages_[i].gateType = static_cast<GateType>(hexToIntFixedLength(&hexData[offset], 2));

				// Byte 1: noteIndex
				stages_[i].noteIndex = hexToIntFixedLength(&hexData[offset + 2], 2);

				// Byte 2: octave (stored as +3)
				stages_[i].octave = hexToIntFixedLength(&hexData[offset + 4], 2) - 3;

				// Byte 3: pulse count
				stages_[i].pulseCount = hexToIntFixedLength(&hexData[offset + 6], 2);

				// Byte 4: velocity spread
				stages_[i].velocitySpread = hexToIntFixedLength(&hexData[offset + 8], 2);

				// Byte 5: probability
				stages_[i].probability = hexToIntFixedLength(&hexData[offset + 10], 2);

				// Byte 6: gate length
				stages_[i].gateLength = hexToIntFixedLength(&hexData[offset + 12], 2);
			}
		}
		else if (!strcmp(tagName, "stageEnabled")) {
			// Parse hex byte with 8 bits
			char const* hexData = reader.readTagOrAttributeValue();

			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			uint8_t enabledBits = hexToIntFixedLength(hexData, 2);
			for (int32_t i = 0; i < kMaxStages; ++i) {
				performanceControls_.stageEnabled[i] = (enabledBits & (1 << i)) != 0;
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
		int32_t expectedVelocitySpread = 0;
		int32_t expectedProbability = 100;
		int32_t expectedGateLength = 50;

		if (stages_[i].gateType != expectedGate || stages_[i].noteIndex != expectedNoteIndex
		    || stages_[i].octave != expectedOctave || stages_[i].pulseCount != expectedPulseCount) {
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
		stages_[i].velocitySpread = 0;
		stages_[i].probability = 100;
		stages_[i].gateLength = 50;
	}
}

} // namespace deluge::model::clip::sequencer::modes
