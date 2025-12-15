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
#include "model/clip/sequencer/sequencer_mode.h"
#include <array>

namespace deluge::model::clip::sequencer::modes {

/**
 * Step Sequencer Mode - Analog-style 16-step sequencer
 *
 * Layout per column (x0-x15 = steps 1-16):
 * - y0: Gate type (OFF/ON/SKIP)
 * - y1: Octave down
 * - y2: Octave up
 * - y3-y7: Note selection (5 pads)
 */
class StepSequencerMode : public SequencerMode {
public:
	StepSequencerMode() = default;
	~StepSequencerMode() override = default;

	// Core identification
	l10n::String name() override { return l10n::String::STRING_FOR_STEP_SEQ; }

	// Support instrument tracks (Synth/MIDI/CV)
	bool supportsInstrument() override { return true; }
	bool supportsKit() override { return false; }
	bool supportsMIDI() override { return true; }
	bool supportsCV() override { return true; }
	bool supportsAudio() override { return false; }

	// Step Sequencer supports all control types (including DIRECTION)
	bool supportsControlType(ControlType type) override { return true; }

	// Lifecycle
	void initialize() override;
	void cleanup() override;

	// Rendering
	bool renderPads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) override;

	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	// Pad input
	bool handlePadPress(int32_t x, int32_t y, int32_t velocity) override;

protected:
	// Vertical encoder - scroll note selection (mode-specific)
	bool handleModeSpecificVerticalEncoder(int32_t offset) override;

public:
	// Playback - plays the sequence at 16th note intervals
	int32_t processPlayback(void* modelStack, int32_t absolutePlaybackPos) override;

	// Stop all notes - prevents hung notes when playback stops
	void stopAllNotes(void* modelStack) override;

	// Scene management
	size_t captureScene(void* buffer, size_t maxSize) override;
	bool recallScene(const void* buffer, size_t size) override;

	// Generative mutations
	void resetToInit() override;
	void randomizeAll(int32_t mutationRate = 100) override;
	void evolveNotes(int32_t mutationRate = 30) override;

	// Pattern persistence
	void writeToFile(Serializer& writer, bool includeScenes = true) override;
	Error readFromFile(Deserializer& reader) override;

	// Handle select encoder for probability adjustment (when note pad is held)
	// Public so InstrumentClipView can call it
	bool handleSelectEncoder(int32_t offset);

private:
	static constexpr int32_t kNumSteps = 16; // x0-x15
	static constexpr int32_t kMaxScaleNotes = 32;

	// Gate types for each step
	enum class GateType : int32_t {
		OFF = 0,  // No note, but step duration counts
		ON = 1,   // Play note
		SKIP = 2, // Skip step entirely, jump to next
	};

	// Per-step data
	struct Step {
		GateType gateType{GateType::OFF};
		int8_t octave{0};       // -3 to +3 (optimized from int32_t)
		uint8_t noteIndex{0};   // Index into scale notes array (0-31, optimized from int32_t)
		uint8_t velocity{100};  // Note velocity 1-127 (default 100)
		uint8_t gateLength{75}; // Gate length as percentage 1-100 (default 75%)
		uint8_t probability{100}; // Probability to play 0-100% (default 100%)
	};

	// State
	bool initialized_ = false;
	std::array<Step, kNumSteps> steps_;


	// Timing
	int32_t ticksPerSixteenthNote_ = 0;
	uint8_t currentStep_ = 0;         // 0-15, which step we're on (optimized from int32_t)
	int32_t lastAbsolutePlaybackPos_ = 0;
	int8_t pingPongDirection_ = 1;    // 1=forward, -1=backward (optimized from int32_t)

	// State for additional play order modes
	uint8_t pedalNextStep_ = 1;       // For PEDAL mode: next step to visit after returning to 0 (optimized from int32_t)
	bool skip2OddPhase_ = true;       // For SKIP_2 mode: phase tracking
	bool pendulumGoingUp_ = true;    // For PENDULUM mode: direction
	uint8_t pendulumLow_ = 0;         // For PENDULUM mode: low bound (optimized from int32_t)
	uint8_t pendulumHigh_ = 1;        // For PENDULUM mode: high bound (optimized from int32_t)
	bool spiralFromLow_ = true;      // For SPIRAL mode: direction
	uint8_t spiralLow_ = 0;           // For SPIRAL mode: low bound (optimized from int32_t)
	uint8_t spiralHigh_ = 15;         // For SPIRAL mode: high bound (optimized from int32_t)

	// Currently playing note (for note-off)
	int16_t activeNoteCode_ = -1;     // -1 to 127 (optimized from int32_t)

	// Active step count (1-16) - steps beyond this are dimmed and not played
	uint8_t numActiveSteps_ = 16;     // Optimized from int32_t

	// Pad hold tracking (for velocity/gate length adjustment)
	int8_t heldPadX_ = -1;            // X coordinate of held note pad, or -1 if none (optimized from int32_t)
	int8_t heldPadY_ = -1;            // Y coordinate of held note pad, or -1 if none (optimized from int32_t)

	// Scale notes cache
	int32_t scaleNotes_[kMaxScaleNotes];
	uint8_t numScaleNotes_ = 0;       // 0-32 (optimized from int32_t)
	uint8_t noteScrollOffset_ = 0;    // Scroll offset for note pads (optimized from int32_t)

	// Override horizontal encoder to handle Shift + encoder for step count adjustment
	bool handleHorizontalEncoder(int32_t offset, bool encoderPressed) override;

	// Helpers
	void updateScaleNotes(void* modelStackPtr);
	void cycleGateType(int32_t step);
	void adjustOctave(int32_t step, int32_t delta);
	void setNoteIndex(int32_t step, int32_t noteIndex);
	int32_t calculateNoteCode(const Step& step, const CombinedEffects& effects) const;
	RGB getNoteGradientColor(int32_t yPos) const; // y3=blue, y7=magenta
	void displayOctaveValue(int32_t octave);
	void advanceStep(int32_t direction); // Advance step based on direction mode

	// Helper: Dim color to 20% brightness with minimum of 2 to prevent flickering
	static void dimColor(RGB& color);

	// Helper: Clamp all state variables to active step range
	void clampStateToActiveRange();

	// Helper: Check if a valid note pad is currently held
	bool isNotePadHeld() const;

	// Helper: Clamp a value between min and max (explicitly use int32_t to avoid type deduction issues)
	static int32_t clampValue(int32_t value, int32_t min, int32_t max) {
		if (value < min) return min;
		if (value > max) return max;
		return value;
	}

	// Helper: Display a value on OLED/7seg with format string
	void displayValue(const char* format, int32_t value);

	// Default pattern management
	bool isDefaultPattern() const;
	void setDefaultPattern();
};

} // namespace deluge::model::clip::sequencer::modes
