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

#include "model/clip/sequencer/control_columns/sequencer_control_group.h"

class Serializer;
class Deserializer;

namespace deluge::model::clip::sequencer {

// Combined effects from all active control groups
struct CombinedEffects {
	int32_t clockDivider = 1; // From CLOCK_DIV group
	int32_t octaveShift = 0;  // From OCTAVE group
	int32_t transpose = 0;    // From TRANSPOSE group
	int32_t sceneIndex = -1;  // From SCENE group (-1 = none)
	int32_t direction = 0;    // From DIRECTION group (0=forward, 1=backward, 2=pingpong, 3=random)
};

// Individual pad configuration
struct ControlPad {
	ControlType type = ControlType::NONE;
	int32_t valueIndex = 0;
	PadMode mode = PadMode::TOGGLE;
	bool active = false;
	bool held = false;

	// Scene validity (scene data stored in shared buffer)
	bool sceneValid = false;
};

// Manages all 16 individual control pads for a sequencer mode
class SequencerControlState {
public:
	SequencerControlState();

	// Initialize with default control types
	void initialize();

	// Rendering
	void render(RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	// Input handling
	bool handlePad(int32_t x, int32_t y, int32_t velocity, class SequencerMode* mode = nullptr);
	bool handleHorizontalEncoder(int32_t heldX, int32_t heldY, int32_t offset, class SequencerMode* mode = nullptr);
	bool handleVerticalEncoder(int32_t heldX, int32_t heldY, int32_t offset);
	bool handleVerticalEncoderButton(int32_t heldX, int32_t heldY);

	// Get combined effects from all active pads
	CombinedEffects getCombinedEffects() const;

	// Check if any control column pad is currently held
	bool isAnyPadHeld() const;

	// Scene capture/restore (excludes scene pads)
	size_t captureState(void* buffer, size_t maxSize) const;
	bool restoreState(const void* buffer, size_t size);

	// ========== PATTERN PERSISTENCE ==========

	// Write control column configuration and scenes to file
	void writeToFile(Serializer& writer, bool includeScenes = true);

	// Read control column configuration and scenes from file
	Error readFromFile(Deserializer& reader);

	// Apply control values to matching pads (or return unmatched values)
	// Deactivates all pads, then activates matching ones if found
	// Returns true if all values were applied to pads, false if some need base controls
	void applyControlValues(int32_t clockDivider, int32_t octaveShift, int32_t transpose, int32_t direction,
	                        int32_t* unmatchedClock, int32_t* unmatchedOctave, int32_t* unmatchedTranspose,
	                        int32_t* unmatchedDirection);

	// Clear base controls for a specific control type (called when user manually activates a pad)
	void clearBaseControlForType(ControlType type, class SequencerMode* mode);

private:
	// 16 individual control pads:
	// [0-7] = x16 (y0-y7)
	// [8-15] = x17 (y0-y7)
	std::array<ControlPad, 16> pads_;

	// Shared scene buffers (8 scenes max, one buffer per scene)
	static constexpr size_t kMaxSceneDataSize = 512;
	static constexpr size_t kMaxScenes = 8;
	uint8_t sceneBuffers_[kMaxScenes][kMaxSceneDataSize];
	size_t sceneSizes_[kMaxScenes];

	// Helper to map x,y to pad index
	int32_t getPadIndex(int32_t x, int32_t y) const;

	// Scene management helpers
	void deactivateAllScenePads();
	bool handleSceneCapture(ControlPad& pad, class SequencerMode* mode);
	bool handleSceneClear(ControlPad& pad);
	bool handleSceneRecall(ControlPad& pad, class SequencerMode* mode);

	// Rendering helpers
	void renderPadAtPosition(int32_t y, int32_t x, const ControlPad& pad, RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
};

} // namespace deluge::model::clip::sequencer
