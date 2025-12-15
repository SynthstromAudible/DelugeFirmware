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

#pragma once

#include "gui/l10n/l10n.h"
#include "hid/led/pad_leds.h"
#include "model/clip/sequencer/sequencer_mode.h"
#include "model/iterance/iterance.h"
#include <array>

namespace deluge::model::clip::sequencer::modes {

// Constants
constexpr int32_t kMaxStages = 8;
constexpr int32_t kMaxPulseCount = 8;
constexpr int32_t kMaxNoteSlots = 16;
constexpr int32_t kPopupBufferSize = 30;
constexpr int32_t kNoteNameBufferSize = 10;
constexpr int32_t kFlashDurationTicks = 50;
constexpr int32_t kOctaveDownRow = 1; // Relative to gate line
constexpr int32_t kOctaveUpRow = 2;
constexpr int32_t kNotesStartRow = 3;


class PulseSequencerMode : public SequencerMode {
public:
	l10n::String name() override { return l10n::String::STRING_FOR_PULSE_SEQ; }

	// Support only melodic tracks
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	bool supportsMIDI() override { return true; }
	bool supportsCV() override { return true; }
	bool supportsAudio() override { return false; }

	// Pulse Sequencer supports all control types (direction, clock divider, transpose, octave)
	bool supportsControlType(ControlType type) override { return true; }

	void initialize() override;
	void cleanup() override;

	// Override rendering to show pulse pattern
	bool renderPads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) override;

	// Override sidebar rendering
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	// Override pad input to handle user interaction
	bool handlePadPress(int32_t x, int32_t y, int32_t velocity) override;

protected:
	// Override vertical encoder for view scrolling (mode-specific)
	bool handleModeSpecificVerticalEncoder(int32_t offset) override;

	// Override horizontal encoder for velocity adjustment (when note pad held)
	bool handleHorizontalEncoder(int32_t offset, bool encoderPressed) override;

public:
	// Handle select encoder for probability adjustment (when note pad is held)
	// Public so InstrumentClipView can call it
	bool handleSelectEncoder(int32_t offset);
	// Override playback to generate pulsed notes
	int32_t processPlayback(void* modelStack, int32_t absolutePlaybackPos) override;

	// Stop all notes - prevents hung notes when playback stops
	void stopAllNotes(void* modelStack) override;

	// Scene management
	size_t captureScene(void* buffer, size_t maxSize) override;
	bool recallScene(const void* buffer, size_t size) override;

	// Generative mutations (wired to existing functionality)
	void resetToInit() override;
	void randomizeAll(int32_t mutationRate = 100) override;
	void evolveNotes(int32_t mutationRate = 30) override;

	// Pattern persistence
	void writeToFile(Serializer& writer, bool includeScenes = true) override;
	Error readFromFile(Deserializer& reader) override;
	bool copyFrom(SequencerMode* other) override;

	// Gate types enum
	enum class GateType : int32_t { OFF = 0, SINGLE = 1, MULTIPLE = 2, HELD = 3, SKIP = 4 };

protected:
	// Made protected for accessor functions (used by shared encoder helpers)
	bool initialized_ = false;
	int32_t ticksPerSixteenthNote_ = 0;
	int32_t lastAbsolutePlaybackPos_ = 0; // Track for position indicator

	// Stage data (8 stages, one per column)
	// Optimized types to match StepSequencerMode memory efficiency
	struct StageData {
		GateType gateType = GateType::OFF;
		uint8_t noteIndex = 0;      // Index in current scale (0-31, optimized from int32_t)
		int8_t octave = 0;          // Octave offset from base (-3 to +3, optimized from int32_t)
		uint8_t pulseCount = 1;     // 1-8, default is 1 (optimized from int32_t)
		uint8_t velocity = 100;     // Note velocity 1-127 (default 100, optimized from int32_t)
		uint8_t velocitySpread = 0; // 0-127, randomization amount (optimized from int32_t)
		uint8_t probability = 20;   // 0-20 (0-100% in 5% increments, default 20 = 100%, optimized from int32_t)
		uint8_t gateLength = 50;    // 0-100%, note length as % of period (optimized from int32_t)
		Iterance iterance{kDefaultIteranceValue}; // Iterance (default OFF)
	};

	std::array<StageData, kMaxStages> stages_;

	// Sequencer state
	struct {
		int32_t currentPulse = 0;
		int32_t lastPlayedStage = -1;
		int32_t totalPatternLength = 8;
		int32_t repeatCount_ = 0; // Track how many times we've looped through the pattern (for iterance)

		// Visual feedback
		bool gatePadFlashing = false;
		uint32_t flashStartTime = 0;
		uint32_t flashDuration = kFlashDurationTicks;
		uint32_t lastRefreshTick = 0; // For UI refresh throttling

		// Per-note tracking for proper note-off handling
		std::array<int16_t, kMaxNoteSlots> noteCodeActive;
		std::array<uint32_t, kMaxNoteSlots> noteGatePos;
		std::array<bool, kMaxNoteSlots> noteActive;
	} sequencerState_;

	// Performance controls (only stage-specific controls, others use control columns)
	struct {
		int32_t numStages = 8;
		int32_t pingPongDirection = 1;
		int32_t currentStage = 0;

		// Play order state variables (instance-based, not static)
		int32_t pedalNextStage = 1;
		bool skip2OddPhase = true;
		int32_t pendulumLow = 0;
		int32_t pendulumHigh = 1;
		bool pendulumGoingUp = true;
		int32_t spiralLow = 0;
		int32_t spiralHigh = 7;
		bool spiralFromLow = true;
	} performanceControls_;

	// Display state
	struct {
		int32_t gateLineOffset = 0; // 0 to (3+numScaleNotes), shifts entire left side
		int32_t scaleNotes[12];     // Current scale notes (max 12 for chromatic)
		int32_t numScaleNotes = 0;  // How many notes in current scale
	} displayState_;

	// Pad hold tracking (for velocity/gate length/probability adjustment)
	int8_t heldPadX_ = -1;            // X coordinate of held note pad, or -1 if none
	int8_t heldPadY_ = -1;            // Y coordinate of held note pad, or -1 if none

	// Core sequencer methods
	void generateNotes(void* modelStack);
	void playNoteForStage(void* modelStack, int32_t stage);
	void switchNoteOff(void* modelStack, int32_t noteSlot);
	void advanceToNextEnabledStage();
	bool evaluateRhythmPattern(int32_t stage, int32_t pulsePosition);
	void updateScaleNotes(); // Update scale notes from current song

	// Utility helper methods
	bool isStageValid(int32_t stage) const { return stage >= 0 && stage < kMaxStages; }
	bool isStageActive(int32_t stage) const;
	int32_t getNoteRowY(int32_t noteIdx) const { return getGateLineY() + kNotesStartRow + noteIdx; }
	int32_t getGateLineY() const { return displayState_.gateLineOffset + 4; }
	int32_t calculateTotalPatternLength() const;
	int32_t getTicksPerPeriod(int32_t baseTicks) const;
	void showStagePopup(int32_t stage, const char* format, ...);
	RGB dimColorIfDisabled(RGB color, int32_t stage) const;
	RGB getOctaveColor(int32_t octave) const;
	int32_t calculateNoteCode(int32_t stage, int32_t noteIndexInScale, const CombinedEffects& effects) const;

	// Helper: Check if a note pad is currently held
	// Note: This only checks that a pad is held in the valid range (x0-7, y >= 3)
	// We don't check against current gateLineY because it can change with scrolling
	// The pad was pressed at a specific Y coordinate, and that coordinate is stored in heldPadY_
	[[gnu::always_inline]] inline bool isNotePadHeld() const {
		// Check X coordinate: must be 0-7 (valid stage range for pulse sequencer)
		if (heldPadX_ < 0 || heldPadX_ >= kMaxStages) {
			return false;
		}
		// Check Y coordinate: must be >= 3 (note pads are always at y3 or above, regardless of scrolling)
		// Note pads start at gateLineY + 3, and gateLineY is at least 0, so note pads are always at y3+
		if (heldPadY_ < 3) {
			return false;
		}
		// Valid note pad coordinates
		return true;
	}

	// Display and utility helpers now use base class implementations

	// Rendering sub-methods
	void renderPulseCounts(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                       int32_t imageWidth, int32_t gateLineY);
	void renderGateLine(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                    int32_t imageWidth, int32_t gateLineY);
	void renderOctavePads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                      int32_t imageWidth, int32_t gateLineY);
	void renderNotePads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                    int32_t imageWidth, int32_t gateLineY);

	// Play order advancement methods
	void advanceForwards(int32_t& nextStage);
	void advanceBackwards(int32_t& nextStage);
	void advancePingPong(int32_t& nextStage, int32_t& direction);
	void advanceRandom();
	void advancePedal();
	void advanceSkip2();
	void advancePendulum();
	void advanceSpiral();

	// Pad input handlers
		void handleGateType(int32_t stage);
		void handleOctaveAdjustment(int32_t stage, int32_t direction);
		void handlePulseCount(int32_t stage, int32_t position);
	void resetToDefaults();
	void randomizeSequence();
	void evolveSequence();

	// Default pattern management
	bool isDefaultPattern() const;
	void setDefaultPattern();
};

} // namespace deluge::model::clip::sequencer::modes
