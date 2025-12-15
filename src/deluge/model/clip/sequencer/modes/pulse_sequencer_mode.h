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

public:
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

	// Gate types enum
	enum class GateType : int32_t { OFF = 0, SINGLE = 1, MULTIPLE = 2, HELD = 3 };

private:
	bool initialized_ = false;
	int32_t ticksPerSixteenthNote_ = 0;
	int32_t lastAbsolutePlaybackPos_ = 0; // Track for position indicator

	// Stage data (8 stages, one per column)
	struct StageData {
		GateType gateType = GateType::OFF;
		int32_t noteIndex = 0;      // Index in current scale
		int32_t octave = 0;         // Octave offset from base
		int32_t pulseCount = 1;     // 1-8, default is 1
		int32_t velocitySpread = 0; // 0-127, randomization amount
		int32_t probability = 100;  // 0-100%, chance to play
		int32_t gateLength = 50;    // 0-100%, note length as % of period
	};

	std::array<StageData, kMaxStages> stages_;

	// Sequencer state
	struct {
		int32_t currentPulse = 0;
		int32_t lastPlayedStage = -1;
		int32_t totalPatternLength = 8;

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
		std::array<bool, kMaxStages> stageEnabled = {true, true, true, true, true, true, true, true};

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
	void handleStageCountChange(int32_t numStages);
	void handleStageToggle(int32_t stage);
	void resetToDefaults();
	void randomizeSequence();
	void evolveSequence();

	// Default pattern management
	bool isDefaultPattern() const;
	void setDefaultPattern();
};

} // namespace deluge::model::clip::sequencer::modes
