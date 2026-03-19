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

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "hid/led/pad_leds.h"
#include "model/clip/sequencer/control_columns/sequencer_control_state.h"
#include "model/iterance/iterance.h"

class Serializer;
class Deserializer;

namespace deluge::model::clip::sequencer {

/**
 * Base class for alternative sequencer modes that can replace linear clip playback
 * with pattern-based, algorithmic, or other non-linear sequencing approaches.
 *
 * This is the foundation for step sequencers, euclidean sequencers, granular modes,
 * generative sequencers, and other creative sequencing paradigms.
 */
class SequencerMode {
public:
	virtual ~SequencerMode() = default;

	// Core identification
	virtual l10n::String name() = 0;

	// Minimal interface - will be expanded in future steps
	virtual void initialize() {}
	virtual void cleanup() {}

	// Rendering - allow sequencer modes to override pad display
	// Returns true if the mode handled rendering, false to use default linear rendering
	virtual bool renderPads(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                        int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) {
		return false;
	}

	// Sidebar rendering - allow sequencer modes to override sidebar (x16-17)
	// Returns true if the mode handled sidebar, false to use default mute/audition
	// Default implementation renders control columns
	virtual bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	// Pad input - handle user interaction with pads
	// Returns true if the mode handled the pad press, false to use default behavior
	// x, y: pad coordinates (0-17, 0-7) - includes sidebar
	// velocity: press velocity (0 = release, 1-127 = press)
	// Default implementation handles control column pads (x16-17)
	virtual bool handlePadPress(int32_t x, int32_t y, int32_t velocity);

	// Horizontal encoder - handle horizontal scrolling/adjustment
	// Returns true if the mode handled the encoder, false to use default behavior
	// Default implementation routes to control column state
	virtual bool handleHorizontalEncoder(int32_t offset, bool encoderPressed);

	// Track which control column pad is currently held
	int8_t heldControlColumnX_ = -1; // -1 = none, 16-17 = x position
	int8_t heldControlColumnY_ = -1; // -1 = none, 0-7 = y position

	// Vertical encoder - handle vertical scrolling or control column value adjustment
	// Returns true if the mode handled the encoder, false to use default behavior
	// offset: encoder rotation amount (positive = clockwise, negative = counter-clockwise)
	// This method is FINAL - it handles control columns automatically then delegates to
	// handleModeSpecificVerticalEncoder Derived classes should NOT override this - override
	// handleModeSpecificVerticalEncoder instead
	virtual bool handleVerticalEncoder(int32_t offset) final;

	// Vertical encoder button - toggle momentary/toggle mode for control columns
	// Returns true if the mode handled the button, false to use default behavior
	virtual bool handleVerticalEncoderButton();

	// Playback - called during clip playback to generate notes
	// Return value: ticks until this mode needs to be called again
	// modelStack: ModelStackWithTimelineCounter* for note triggering
	// absolutePlaybackPos: playbackHandler.lastSwungTickActioned - NEVER resets, always incrementing
	virtual int32_t processPlayback(void* modelStack, int32_t absolutePlaybackPos) {
		return 2147483647;
	} // Max int = never

	// Stop all notes - called when playback stops to prevent hung notes
	// Override this in sequencer modes that track active notes
	virtual void stopAllNotes(void* modelStack) {}

	// Simple callback when a musical division boundary is crossed
	// Override this for easy timing - base class handles the modulo math
	// syncLevel: 7=16th, 6=8th, 8=32nd (same as arpeggiator)
	virtual void onMusicalDivision(void* modelStack) {}

	// ========== SCENE MANAGEMENT ==========

	// Scene capture/recall - allows saving and restoring sequencer state
	// Each mode implements its own scene data structure
	// Returns size of captured data, or 0 if scenes not supported
	virtual size_t captureScene(void* buffer, size_t maxSize) { return 0; }

	// Recall scene from captured data
	// Returns true if scene was successfully recalled
	virtual bool recallScene(const void* buffer, size_t size) { return false; }

	// ========== PATTERN PERSISTENCE ==========

	// Write sequencer mode data to file (for pattern saving)
	// includeScenes: whether to include scene data in the output
	virtual void writeToFile(Serializer& writer, bool includeScenes = true) {}

	// Read sequencer mode data from file (for pattern loading)
	virtual Error readFromFile(Deserializer& reader) { return Error::NONE; }

	// Clone data from another sequencer mode of the same type
	// Returns true if copy was successful, false if types don't match or copy failed
	virtual bool copyFrom(SequencerMode* other) { return false; }

	// Check if this mode can be saved as a standalone pattern
	virtual bool canSaveAsPattern() const { return true; }

	// Access to control columns (for scene capture/recall)
	SequencerControlState& getControlColumnState() { return controlColumnState_; }
	const SequencerControlState& getControlColumnState() const { return controlColumnState_; }

	// ========== GENERATIVE MUTATIONS ==========

	// Generative mutation actions - implement these in derived classes to support generative control group
	// Reset to initial state
	virtual void resetToInit() {}
	// Randomize all parameters (mutationRate 0-100%)
	virtual void randomizeAll(int32_t mutationRate = 100) {}
	// Evolve pattern (mutationRate 0-100%)
	// Low %: gentle melodic drift (notes only)
	// High % (>70%): more chaotic (notes, octaves, gates)
	virtual void evolveNotes(int32_t mutationRate = 30) {}

	// ========== MODE COMPATIBILITY ==========

	// Track type compatibility (default: support all)
	virtual bool supportsInstrument() { return true; }
	virtual bool supportsKit() { return true; }
	virtual bool supportsMIDI() { return true; }
	virtual bool supportsCV() { return true; }
	virtual bool supportsAudio() { return false; } // Audio modes need special handling

	// Control column compatibility (override in specific modes)
	virtual bool supportsControlType(ControlType type) {
		// By default, all modes support all control types
		return true;
	}

protected:
	// ========== MODE-SPECIFIC ENCODER HANDLING ==========

	// Mode-specific vertical encoder handling
	// Override this in derived classes to implement custom scrolling/adjustment behavior
	// This is called ONLY when control columns are NOT handling the encoder
	// Return true if you handled it, false to use default clip view behavior
	virtual bool handleModeSpecificVerticalEncoder(int32_t offset) { return false; }

	// ========== SIMPLE HELPERS FOR SEQUENCER MODES ==========

	// TIMING HELPERS - Musical divisions
	// Use song->getSixteenthNoteLength(), song->getQuarterNoteLength(), song->getBarLength()
	// to get tick periods that automatically handle tempo and resolution

	static bool atDivisionBoundary(int32_t absolutePos, int32_t ticksPerPeriod) {
		return (absolutePos % ticksPerPeriod) == 0;
	}

	static int32_t ticksUntilNextDivision(int32_t absolutePos, int32_t ticksPerPeriod) {
		int32_t howFarIntoPeriod = absolutePos % ticksPerPeriod;
		return howFarIntoPeriod == 0 ? ticksPerPeriod : (ticksPerPeriod - howFarIntoPeriod);
	}

	// SCALE HELPERS - Get notes in current scale
	// Fills an array with all scale notes across specified octave range
	// Returns the number of notes filled
	// maxNotes: size of the noteArray buffer
	// octaveRange: how many octaves to span
	// baseOctave: starting octave (0 = root octave, 1 = one octave up, etc.)
	static int32_t getScaleNotes(void* modelStackPtr, int32_t* noteArray, int32_t maxNotes, int32_t octaveRange = 2,
	                             int32_t baseOctave = 0);

	// NOTE HELPERS - Easy note triggering
	// Sends a note on/off to the instrument
	static void playNote(void* modelStackPtr, int32_t noteCode, uint8_t velocity, int32_t length);
	static void stopNote(void* modelStackPtr, int32_t noteCode);

	// RANDOMIZATION HELPERS - Use before calling playNote()
	// Apply velocity spread to a base velocity (randomizes ±spread amount)
	static uint8_t applyVelocitySpread(uint8_t baseVelocity, int32_t spread);
	// Check if note should play based on probability (0-100, where 100 = always play)
	static bool shouldPlayBasedOnProbability(int32_t probability);

	// PLAYBACK POSITION INDICATOR - Show current position across top row (y7, x0-15)
	// Call this from your renderPads() to show playback position
	// absolutePlaybackPos: current playback position (from processPlayback)
	// totalLength: total length of your pattern in ticks
	// color: color for the position indicator
	static void renderPlaybackPosition(RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                                   int32_t imageWidth, int32_t absolutePlaybackPos, int32_t totalLength,
	                                   RGB color = RGB{255, 255, 255}, bool enabled = true);

	// ========== DISPLAY HELPERS ==========
	// Shared display functions for velocity, gate length, probability, and iterance
	// These are used by both Step and Pulse sequencers
	static void displayVelocity(uint8_t velocity);
	static void displayGateLength(uint8_t gateLength);
	static void displayProbability(uint8_t probability); // probability is 0-20 (representing 0-100% in 5% increments)
	static void displayIterance(Iterance iterance);

	// ========== UTILITY HELPERS ==========
	// Clamp a value between min and max
	[[gnu::always_inline]] static inline int32_t clampValue(int32_t value, int32_t min, int32_t max) {
		if (value < min) return min;
		if (value > max) return max;
		return value;
	}

public:
	// ========== CONTROL COLUMNS ==========

	/**
	 * Get the combined active control values from all groups.
	 * Use this during playback to apply clock div, transpose, octave, etc.
	 * Merges base control values with active pad effects.
	 */
	CombinedEffects getCombinedEffects() const;

	/**
	 * Set base control values (used when no matching pad exists for a scene value)
	 * These are public so SequencerControlState can clear them when user activates pads
	 */
	void setBaseClockDivider(int32_t divider) { baseClockDivider_ = divider; }
	void setBaseOctaveShift(int32_t shift) { baseOctaveShift_ = shift; }
	void setBaseTranspose(int32_t transpose) { baseTranspose_ = transpose; }
	void setBaseDirection(int32_t direction) { baseDirection_ = direction; }

	/**
	 * Clear all base control values
	 */
	void clearBaseControls() {
		baseClockDivider_ = 1;
		baseOctaveShift_ = 0;
		baseTranspose_ = 0;
		baseDirection_ = 0;
	}

protected:
	// Protected constructor - only concrete implementations can be instantiated
	SequencerMode() = default;

	// Base control values - apply when no matching pad exists
	// These are "invisible" effects that work without visible pads
	int32_t baseClockDivider_ = 1;
	int32_t baseOctaveShift_ = 0;
	int32_t baseTranspose_ = 0;
	int32_t baseDirection_ = 0;

	// Control column state (per-mode instance)
	SequencerControlState controlColumnState_;
};

} // namespace deluge::model::clip::sequencer
