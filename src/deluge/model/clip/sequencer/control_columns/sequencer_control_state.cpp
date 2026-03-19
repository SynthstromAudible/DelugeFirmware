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

#include "model/clip/sequencer/control_columns/sequencer_control_state.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/sequencer/sequencer_mode.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace deluge::model::clip::sequencer {

// Forward declare helper functions from sequencer_control_group.cpp
namespace helpers {
const char* getTypeName(ControlType type);
RGB getColorForType(ControlType type);
const int32_t* getAvailableValues(ControlType type);
int32_t getNumAvailableValues(ControlType type);
int32_t getValue(ControlType type, int32_t valueIndex);
const char* formatValue(ControlType type, int32_t value);
} // namespace helpers

namespace {
// Helper: Request UI refresh for sidebar
void refreshSidebar() {
	uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
	PadLEDs::sendOutSidebarColoursSoon();
}
} // namespace

SequencerControlState::SequencerControlState() {
	// Initialize scene buffers
	for (size_t i = 0; i < kMaxScenes; ++i) {
		sceneSizes_[i] = 0;
	}
	initialize();
}

void SequencerControlState::initialize() {
	// Default configuration for 16 pads:
	// x16 (pads 0-7):
	//   y0: OCTAVE +1
	//   y1: TRANSPOSE +5
	//   y2-y7: NONE (user configurable)
	// x17 (pads 8-15):
	//   y0: SCENE 1
	//   y1: SCENE 2
	//   y2-y5: NONE (user configurable)
	//   y6: RANDOM
	//   y7: RESET

	// x16 column - mostly empty for user configuration
	pads_[0].type = ControlType::OCTAVE;
	pads_[1].type = ControlType::TRANSPOSE;
	pads_[2].type = ControlType::NONE;
	pads_[3].type = ControlType::NONE;
	pads_[4].type = ControlType::NONE;
	pads_[5].type = ControlType::NONE;
	pads_[6].type = ControlType::NONE;
	pads_[7].type = ControlType::NONE;

	// x17 column - basic scene + generative controls
	pads_[8].type = ControlType::SCENE;
	pads_[9].type = ControlType::SCENE;
	pads_[10].type = ControlType::NONE;
	pads_[11].type = ControlType::NONE;
	pads_[12].type = ControlType::NONE;
	pads_[13].type = ControlType::NONE;
	pads_[14].type = ControlType::RANDOM;
	pads_[15].type = ControlType::RESET;

	// Set useful default values
	// Octave +1 (valueIndex 5 in kOctaveValues = +1)
	pads_[0].valueIndex = 6; // +1 octave (kOctaveValues[6] = +1)

	// Transpose +5 (valueIndex 17 in kTransposeValues = +5)
	pads_[1].valueIndex = 17; // +5 semitones (kTransposeValues[17] = +5)

	// Scenes: 1-2
	pads_[8].valueIndex = 0; // Scene 1
	pads_[9].valueIndex = 1; // Scene 2

	// Random: default to 50%
	pads_[14].valueIndex = 4; // 50% for random

	// Reset has no value
	pads_[15].valueIndex = 0;
}

int32_t SequencerControlState::getPadIndex(int32_t x, int32_t y) const {
	if (x == kDisplayWidth) {
		// x16 column
		if (y >= 0 && y < 8) {
			return y; // pads 0-7
		}
	}
	else if (x == (kDisplayWidth + 1)) {
		// x17 column
		if (y >= 0 && y < 8) {
			return 8 + y; // pads 8-15
		}
	}
	return -1; // Invalid
}

// ========== HELPER FUNCTIONS ==========

void SequencerControlState::deactivateAllScenePads() {
	for (auto& p : pads_) {
		if (p.type == ControlType::SCENE) {
			p.active = false;
		}
	}
}

void SequencerControlState::renderPadAtPosition(int32_t y, int32_t x, const ControlPad& pad,
                                                RGB image[][kDisplayWidth + kSideBarWidth],
                                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	RGB color = helpers::getColorForType(pad.type);
	bool isBright = pad.active || pad.held;
	bool isEmpty = (pad.type == ControlType::SCENE && !pad.sceneValid) || (pad.type == ControlType::NONE);

	if (isBright) {
		image[y][x] = color;
	}
	else if (isEmpty) {
		// Very dim for empty scenes or unused pads
		image[y][x] = RGB{static_cast<uint8_t>(color.r / 16), static_cast<uint8_t>(color.g / 16),
		                  static_cast<uint8_t>(color.b / 16)};
	}
	else {
		// Normal dim
		image[y][x] = RGB{static_cast<uint8_t>(color.r / 8), static_cast<uint8_t>(color.g / 8),
		                  static_cast<uint8_t>(color.b / 8)};
	}

	if (occupancyMask) {
		occupancyMask[y][x] = 64;
	}
}

bool SequencerControlState::handleSceneCapture(ControlPad& pad, SequencerMode* mode) {
	int32_t sceneNum = pad.valueIndex;
	if (sceneNum < 0 || sceneNum >= kMaxScenes) {
		return false;
	}

	size_t modeDataSize = mode->captureScene(sceneBuffers_[sceneNum], kMaxSceneDataSize);

	if (modeDataSize > 0 && modeDataSize <= kMaxSceneDataSize) {
		sceneSizes_[sceneNum] = modeDataSize;
		pad.sceneValid = true;

		deactivateAllScenePads();
		pad.active = true;

		if (display) {
			display->displayPopup("CAPTURED");
		}
		return true;
	}

	if (display) {
		display->displayPopup(modeDataSize > kMaxSceneDataSize ? "SCENE TOO BIG" : "CAPTURE FAILED");
	}
	return false;
}

bool SequencerControlState::handleSceneClear(ControlPad& pad) {
	int32_t sceneNum = pad.valueIndex;
	if (sceneNum < 0 || sceneNum >= kMaxScenes) {
		return false;
	}

	sceneSizes_[sceneNum] = 0;
	pad.sceneValid = false;
	pad.active = false;

	if (display) {
		display->displayPopup("CLEARED");
	}
	return true;
}

bool SequencerControlState::handleSceneRecall(ControlPad& pad, SequencerMode* mode) {
	int32_t sceneNum = pad.valueIndex;
	if (sceneNum < 0 || sceneNum >= kMaxScenes || sceneSizes_[sceneNum] == 0) {
		if (display) {
			display->displayPopup("EMPTY");
		}
		return false;
	}

	bool success = mode->recallScene(sceneBuffers_[sceneNum], sceneSizes_[sceneNum]);
	if (success) {
		deactivateAllScenePads();
		pad.active = true;
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);

		if (display) {
			int32_t val = helpers::getValue(pad.type, pad.valueIndex);
			static char popup[40];
			snprintf(popup, sizeof(popup), "%s: %s", helpers::getTypeName(pad.type),
			         helpers::formatValue(pad.type, val));
			display->displayPopup(popup);
		}
		return true;
	}
	return false;
}

// ========== RENDERING ==========

void SequencerControlState::render(RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	// Render x16 column (pads 0-7)
	for (int32_t y = 0; y < 8; y++) {
		renderPadAtPosition(y, kDisplayWidth, pads_[y], image, occupancyMask);
	}

	// Render x17 column (pads 8-15)
	for (int32_t y = 0; y < 8; y++) {
		renderPadAtPosition(y, kDisplayWidth + 1, pads_[8 + y], image, occupancyMask);
	}
}

bool SequencerControlState::handlePad(int32_t x, int32_t y, int32_t velocity, SequencerMode* mode) {
	int32_t padIndex = getPadIndex(x, y);
	if (padIndex < 0) {
		return false;
	}

	ControlPad& pad = pads_[padIndex];
	bool pressed = (velocity > 0);

	// SCENE TYPE: Special handling
	if (pad.type == ControlType::SCENE && mode) {
		if (pressed) {
			// SAVE button = capture scene
			if (Buttons::isButtonPressed(deluge::hid::button::SAVE)) {
				handleSceneCapture(pad, mode);
				pad.held = true;
				refreshSidebar();
				return true;
			}

			// SHIFT button = clear scene
			if (Buttons::isShiftButtonPressed()) {
				handleSceneClear(pad);
				pad.held = true;
				refreshSidebar();
				return true;
			}

			// Otherwise = recall scene
			if (pad.sceneValid) {
				handleSceneRecall(pad, mode);
			}
			else {
				if (display) {
					display->displayPopup("EMPTY");
				}
			}

			pad.held = true;
			refreshSidebar();
			return true;
		}
		else {
			// Release
			pad.held = false;
			refreshSidebar();
			return true;
		}
	}

	// RESET TYPE: Instant trigger, no state
	if (pad.type == ControlType::RESET && mode) {
		if (pressed) {
			mode->resetToInit();
			if (display) {
				display->displayPopup("RESET");
			}
			pad.held = true;
			pad.active = true;
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
			refreshSidebar();
			return true;
		}
		else {
			pad.held = false;
			pad.active = false;
			refreshSidebar();
			return true;
		}
	}

	// RANDOM/EVOLVE: Instant trigger with % value
	if ((pad.type == ControlType::RANDOM || pad.type == ControlType::EVOLVE) && mode) {
		if (pressed) {
			int32_t mutationRate = helpers::getValue(pad.type, pad.valueIndex);

			if (pad.type == ControlType::RANDOM) {
				mode->randomizeAll(mutationRate);
			}
			else if (pad.type == ControlType::EVOLVE) {
				mode->evolveNotes(mutationRate);
			}

			if (display) {
				static char popup[40];
				snprintf(popup, sizeof(popup), "%s: %s", helpers::getTypeName(pad.type),
				         helpers::formatValue(pad.type, mutationRate));
				display->displayPopup(popup);
			}

			pad.held = true;
			pad.active = true;
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
			refreshSidebar();
			return true;
		}
		else {
			pad.held = false;
			pad.active = false;
			refreshSidebar();
			return true;
		}
	}

	// NONE TYPE: Show configuration hint
	if (pad.type == ControlType::NONE) {
		if (pressed) {
			if (display) {
				display->displayPopup("<> TO CONFIGURE");
			}
			pad.held = true;
			refreshSidebar();
			return true;
		}
		else {
			pad.held = false;
			refreshSidebar();
			return true;
		}
	}

	// NORMAL CONTROLS: Clock, Octave, Transpose, Direction
	if (pressed) {
		pad.held = true;

		if (pad.mode == PadMode::TOGGLE) {
			// Toggle mode: flip state
			pad.active = !pad.active;

			// Clear base control for this type (user manually activating pad)
			if (pad.active && mode) {
				clearBaseControlForType(pad.type, mode);
			}

			if (display && pad.type != ControlType::NONE) {
				if (pad.active) {
					int32_t val = helpers::getValue(pad.type, pad.valueIndex);
					static char popup[40];
					snprintf(popup, sizeof(popup), "%s: %s", helpers::getTypeName(pad.type),
					         helpers::formatValue(pad.type, val));
					display->displayPopup(popup);
				}
				else {
					display->displayPopup("OFF");
				}
			}
		}
		else {
			// Momentary mode: activate on press
			pad.active = true;

			// Clear base control for this type (user manually activating pad)
			if (mode) {
				clearBaseControlForType(pad.type, mode);
			}

			if (display && pad.type != ControlType::NONE) {
				int32_t val = helpers::getValue(pad.type, pad.valueIndex);
				static char popup[40];
				snprintf(popup, sizeof(popup), "%s: %s", helpers::getTypeName(pad.type),
				         helpers::formatValue(pad.type, val));
				display->displayPopup(popup);
			}
		}
	}
	else {
		// Release
		if (pad.mode == PadMode::MOMENTARY) {
			pad.active = false;
		}
		pad.held = false;
	}

	refreshSidebar();
	return true;
}

bool SequencerControlState::handleHorizontalEncoder(int32_t heldX, int32_t heldY, int32_t offset, SequencerMode* mode) {
	int32_t padIndex = getPadIndex(heldX, heldY);
	if (padIndex < 0) {
		return false;
	}

	ControlPad& pad = pads_[padIndex];

	// All available control types (alphabetically sorted, NONE always first)
	constexpr ControlType availableTypes[] = {ControlType::NONE,   ControlType::CLOCK_DIV, ControlType::DIRECTION,
	                                          ControlType::EVOLVE, ControlType::OCTAVE,    ControlType::RANDOM,
	                                          ControlType::RESET,  ControlType::SCENE,     ControlType::TRANSPOSE};
	constexpr int32_t numTypes = sizeof(availableTypes) / sizeof(availableTypes[0]);

	// Find current type index
	int32_t currentIndex = 0;
	for (int32_t i = 0; i < numTypes; i++) {
		if (availableTypes[i] == pad.type) {
			currentIndex = i;
			break;
		}
	}

	// Cycle to next/prev type, skipping unsupported types
	int32_t direction = (offset > 0) ? 1 : -1;
	int32_t newIndex = currentIndex;
	int32_t attempts = 0;

	do {
		newIndex += direction;
		if (newIndex < 0) {
			newIndex = numTypes - 1;
		}
		else if (newIndex >= numTypes) {
			newIndex = 0;
		}

		// Check if this type is supported by the mode
		ControlType candidate = availableTypes[newIndex];
		if (!mode || mode->supportsControlType(candidate)) {
			break; // Found a supported type
		}

		attempts++;
	} while (attempts < numTypes); // Prevent infinite loop

	// Set new type
	pad.type = availableTypes[newIndex];
	pad.active = false; // Reset active state
	pad.valueIndex = 0; // Reset to first value

	// Set sensible default value based on type
	switch (pad.type) {
	case ControlType::OCTAVE:
		pad.valueIndex = 5; // 0 octaves
		break;
	case ControlType::TRANSPOSE:
		pad.valueIndex = 12; // 0 semitones
		break;
	case ControlType::CLOCK_DIV:
		pad.valueIndex = 1; // /1
		break;
	case ControlType::RANDOM:
	case ControlType::EVOLVE:
		pad.valueIndex = 4; // 50%
		break;
	default:
		break;
	}

	if (display) {
		display->displayPopup(helpers::getTypeName(pad.type));
	}

	refreshSidebar();
	return true;
}

bool SequencerControlState::handleVerticalEncoder(int32_t heldX, int32_t heldY, int32_t offset) {
	int32_t padIndex = getPadIndex(heldX, heldY);
	if (padIndex < 0) {
		return false;
	}

	ControlPad& pad = pads_[padIndex];

	// Skip types with no values
	if (pad.type == ControlType::NONE || pad.type == ControlType::RESET) {
		return false;
	}

	int32_t numValues = helpers::getNumAvailableValues(pad.type);
	if (numValues == 0) {
		return false;
	}

	// Special handling for SCENE type: skip scene numbers already assigned to other pads
	if (pad.type == ControlType::SCENE) {
		int32_t startIndex = pad.valueIndex;
		int32_t direction = (offset > 0) ? 1 : -1;
		int32_t attempts = 0;

		// Keep trying until we find an available scene number
		while (attempts < numValues) {
			// Move to next/prev value
			pad.valueIndex = (pad.valueIndex + direction + numValues) % numValues;

			// Get the actual scene number
			int32_t sceneNum = helpers::getValue(pad.type, pad.valueIndex);

			// Check if this scene number is already assigned to another pad
			bool isAvailable = true;
			for (size_t i = 0; i < pads_.size(); ++i) {
				if (i != static_cast<size_t>(padIndex) && pads_[i].type == ControlType::SCENE) {
					int32_t otherSceneNum = helpers::getValue(pads_[i].type, pads_[i].valueIndex);
					if (otherSceneNum == sceneNum) {
						isAvailable = false;
						break;
					}
				}
			}

			// If available, we're done
			if (isAvailable) {
				break;
			}

			attempts++;
		}

		// If all scene numbers are taken, stay at current
		if (attempts >= numValues) {
			pad.valueIndex = startIndex;
			if (display) {
				display->displayPopup("ALL SCENES USED");
			}
			return true;
		}
	}
	else {
		// Normal cycling for non-SCENE types
		pad.valueIndex = (pad.valueIndex + offset) % numValues;
		if (pad.valueIndex < 0) {
			pad.valueIndex += numValues;
		}
	}

	// Show current value
	if (display) {
		int32_t val = helpers::getValue(pad.type, pad.valueIndex);
		static char popup[40];
		snprintf(popup, sizeof(popup), "%s: %s", helpers::getTypeName(pad.type), helpers::formatValue(pad.type, val));
		display->displayPopup(popup);
	}

	refreshSidebar();
	return true;
}

bool SequencerControlState::handleVerticalEncoderButton(int32_t heldX, int32_t heldY) {
	int32_t padIndex = getPadIndex(heldX, heldY);
	if (padIndex < 0) {
		return false;
	}

	ControlPad& pad = pads_[padIndex];

	// Toggle mode for pads that support it
	if (pad.type != ControlType::NONE && pad.type != ControlType::SCENE && pad.type != ControlType::RESET
	    && pad.type != ControlType::RANDOM && pad.type != ControlType::EVOLVE) {

		pad.mode = (pad.mode == PadMode::TOGGLE) ? PadMode::MOMENTARY : PadMode::TOGGLE;

		if (display) {
			display->displayPopup(pad.mode == PadMode::TOGGLE ? "TOGGLE" : "MOMENTARY");
		}

		return true;
	}

	return false;
}

CombinedEffects SequencerControlState::getCombinedEffects() const {
	CombinedEffects effects;

	// Collect effects from all active pads
	for (const auto& pad : pads_) {
		if (pad.active) {
			int32_t value = helpers::getValue(pad.type, pad.valueIndex);

			switch (pad.type) {
			case ControlType::CLOCK_DIV:
				effects.clockDivider = value;
				break;
			case ControlType::OCTAVE:
				effects.octaveShift += value;
				break;
			case ControlType::TRANSPOSE:
				effects.transpose += value;
				break;
			case ControlType::SCENE:
				effects.sceneIndex = value;
				break;
			case ControlType::DIRECTION:
				effects.direction = value;
				break;
			default:
				break;
			}
		}
	}

	return effects;
}

bool SequencerControlState::isAnyPadHeld() const {
	for (const auto& pad : pads_) {
		if (pad.held) {
			return true;
		}
	}
	return false;
}

size_t SequencerControlState::captureState(void* buffer, size_t maxSize) const {
	uint8_t* ptr = static_cast<uint8_t*>(buffer);
	size_t offset = 0;

	// For each pad (excluding SCENE pads)
	for (size_t i = 0; i < pads_.size(); ++i) {
		const auto& pad = pads_[i];

		// Skip scene pads (they store the scene data itself)
		if (pad.type == ControlType::SCENE) {
			continue;
		}

		// Check buffer space (1 byte type + 4 bytes valueIndex + 1 byte mode + 1 byte active = 7 bytes)
		if (offset + 7 > maxSize) {
			return 0; // Not enough space
		}

		// Save pad data
		ptr[offset++] = static_cast<uint8_t>(pad.type);
		memcpy(&ptr[offset], &pad.valueIndex, sizeof(int32_t));
		offset += sizeof(int32_t);
		ptr[offset++] = static_cast<uint8_t>(pad.mode);
		ptr[offset++] = pad.active ? 1 : 0;
	}

	return offset;
}

bool SequencerControlState::restoreState(const void* buffer, size_t size) {
	const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
	size_t offset = 0;

	// For each pad (excluding scene pads)
	for (size_t i = 0; i < pads_.size(); ++i) {
		auto& pad = pads_[i];

		// Skip scene pads
		if (pad.type == ControlType::SCENE) {
			continue;
		}

		// Check if we have enough data
		if (offset + 7 > size) {
			return false;
		}

		// Restore pad data
		pad.type = static_cast<ControlType>(ptr[offset++]);
		memcpy(&pad.valueIndex, &ptr[offset], sizeof(int32_t));
		offset += sizeof(int32_t);
		pad.mode = static_cast<PadMode>(ptr[offset++]);
		pad.active = (ptr[offset++] != 0);
	}

	return true;
}

void SequencerControlState::applyControlValues(int32_t clockDivider, int32_t octaveShift, int32_t transpose,
                                               int32_t direction, int32_t* unmatchedClock, int32_t* unmatchedOctave,
                                               int32_t* unmatchedTranspose, int32_t* unmatchedDirection) {
	// Initialize unmatched outputs (assume all are unmatched initially)
	*unmatchedClock = clockDivider;
	*unmatchedOctave = octaveShift;
	*unmatchedTranspose = transpose;
	*unmatchedDirection = direction;

	// Deactivate all non-scene pads
	for (auto& pad : pads_) {
		if (pad.type != ControlType::SCENE) {
			pad.active = false;
		}
	}

	// Try to activate matching pads for each control type
	// Clock
	if (clockDivider != 1) {
		int32_t targetValue = clockDivider;
		for (auto& pad : pads_) {
			if (pad.type == ControlType::CLOCK_DIV) {
				int32_t padValue = helpers::getValue(ControlType::CLOCK_DIV, pad.valueIndex);
				if (padValue == targetValue) {
					pad.active = true;
					*unmatchedClock = 1; // Mark as matched (use default)
					break;
				}
			}
		}
	}
	else {
		*unmatchedClock = 1; // Default value, no need to apply
	}

	// Octave
	if (octaveShift != 0) {
		int32_t targetValue = octaveShift;
		for (auto& pad : pads_) {
			if (pad.type == ControlType::OCTAVE) {
				int32_t padValue = helpers::getValue(ControlType::OCTAVE, pad.valueIndex);
				if (padValue == targetValue) {
					pad.active = true;
					*unmatchedOctave = 0; // Mark as matched (use default)
					break;
				}
			}
		}
	}
	else {
		*unmatchedOctave = 0; // Default value, no need to apply
	}

	// Transpose
	if (transpose != 0) {
		int32_t targetValue = transpose;
		for (auto& pad : pads_) {
			if (pad.type == ControlType::TRANSPOSE) {
				int32_t padValue = helpers::getValue(ControlType::TRANSPOSE, pad.valueIndex);
				if (padValue == targetValue) {
					pad.active = true;
					*unmatchedTranspose = 0; // Mark as matched (use default)
					break;
				}
			}
		}
	}
	else {
		*unmatchedTranspose = 0; // Default value, no need to apply
	}

	// Direction
	if (direction != 0) {
		int32_t targetValue = direction;
		for (auto& pad : pads_) {
			if (pad.type == ControlType::DIRECTION) {
				int32_t padValue = helpers::getValue(ControlType::DIRECTION, pad.valueIndex);
				if (padValue == targetValue) {
					pad.active = true;
					*unmatchedDirection = 0; // Mark as matched (use default)
					break;
				}
			}
		}
	}
	else {
		*unmatchedDirection = 0; // Default value, no need to apply
	}

	// Request UI refresh
	refreshSidebar();
}

void SequencerControlState::clearBaseControlForType(ControlType type, SequencerMode* mode) {
	if (!mode)
		return;

	switch (type) {
	case ControlType::CLOCK_DIV:
		mode->setBaseClockDivider(1);
		break;
	case ControlType::OCTAVE:
		mode->setBaseOctaveShift(0);
		break;
	case ControlType::TRANSPOSE:
		mode->setBaseTranspose(0);
		break;
	case ControlType::DIRECTION:
		mode->setBaseDirection(0);
		break;
	default:
		break;
	}
}

// ========== PATTERN PERSISTENCE ==========

void SequencerControlState::writeToFile(Serializer& writer, bool includeScenes) {
	// Write control column pads as single hex string (Deluge convention)
	writer.writeOpeningTagBeginning("controlColumns");

	// Count non-NONE pads and calculate total size
	size_t totalPads = 0;
	size_t totalBytes = 0;
	for (size_t i = 0; i < pads_.size(); ++i) {
		if (pads_[i].type != ControlType::NONE) {
			totalPads++;
			totalBytes += (pads_[i].type == ControlType::SCENE) ? 10 : 9; // Variable size
		}
	}

	// Prepare pad data as byte array for writeAttributeHexBytes
	std::vector<uint8_t> padData;
	padData.reserve(totalBytes);

	for (size_t i = 0; i < pads_.size(); ++i) {
		const auto& pad = pads_[i];

		// Skip NONE type pads
		if (pad.type == ControlType::NONE) {
			continue;
		}

		// Determine x, y coordinates from index
		int32_t x = (i < 8) ? kDisplayWidth : (kDisplayWidth + 1);
		int32_t y = (i < 8) ? i : (i - 8);

		// Byte 0: y coordinate
		padData.push_back(static_cast<uint8_t>(y));

		// Byte 1: x coordinate
		padData.push_back(static_cast<uint8_t>(x));

		// Byte 2: control type
		padData.push_back(static_cast<uint8_t>(pad.type));

		// Bytes 3-6: valueIndex (int32_t, 4 bytes, big-endian)
		padData.push_back(static_cast<uint8_t>((pad.valueIndex >> 24) & 0xFF));
		padData.push_back(static_cast<uint8_t>((pad.valueIndex >> 16) & 0xFF));
		padData.push_back(static_cast<uint8_t>((pad.valueIndex >> 8) & 0xFF));
		padData.push_back(static_cast<uint8_t>(pad.valueIndex & 0xFF));

		// Byte 7: mode
		padData.push_back(static_cast<uint8_t>(pad.mode));

		// Byte 8: active flag
		padData.push_back(pad.active ? 1 : 0);

		// Byte 9: sceneValid flag (for scene pads)
		if (pad.type == ControlType::SCENE) {
			padData.push_back(pad.sceneValid ? 1 : 0);
		}
	}

	// Write padData (empty string if no active pads)
	if (padData.empty()) {
		writer.writeAttribute("padData", "");
	}
	else {
		writer.writeAttributeHexBytes("padData", padData.data(), padData.size());
	}

	// Close controlColumns opening tag
	writer.writeOpeningTagEnd();

	// Write scene data if requested (as child tags)
	if (includeScenes) {
		writer.writeArrayStart("scenes");

		for (int32_t i = 0; i < kMaxScenes; ++i) {
			if (sceneSizes_[i] > 0) {
				writer.writeOpeningTagBeginning("scene");
				writer.writeAttribute("index", i);
				writer.writeAttribute("size", static_cast<int32_t>(sceneSizes_[i]));

				// Write scene data as hex attribute for consistency with other hex data
				writer.writeAttributeHexBytes("data", sceneBuffers_[i], sceneSizes_[i]);
				writer.closeTag(); // Self-closing tag
			}
		}

		writer.writeArrayEnding("scenes");
	}

	// Close controlColumns tag
	writer.writeClosingTag("controlColumns");
}

Error SequencerControlState::readFromFile(Deserializer& reader) {
	char const* tagName;

	// Read controlColumns tag
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "padData")) {
			// Parse hex string for pad configurations
			char const* hexData = reader.readTagOrAttributeValue();

			// Skip "0x" prefix if present
			if (hexData[0] == '0' && hexData[1] == 'x') {
				hexData += 2;
			}

			// Parse pads (variable size: 9 bytes for non-scene, 10 bytes for scene)
			int32_t hexOffset = 0;
			int32_t padCount = 0;

			while (hexData[hexOffset] != '\0' && padCount < static_cast<int32_t>(pads_.size())) {
				// Byte 0: y coordinate
				int32_t y = hexToIntFixedLength(&hexData[hexOffset], 2);
				hexOffset += 2;

				// Byte 1: x coordinate
				int32_t x = hexToIntFixedLength(&hexData[hexOffset], 2);
				hexOffset += 2;

				// Byte 2: control type
				ControlType type = static_cast<ControlType>(hexToIntFixedLength(&hexData[hexOffset], 2));
				hexOffset += 2;

				// Bytes 3-6: valueIndex (4 bytes)
				int32_t valueIndex = hexToIntFixedLength(&hexData[hexOffset], 8);
				hexOffset += 8;

				// Byte 7: mode
				PadMode mode = static_cast<PadMode>(hexToIntFixedLength(&hexData[hexOffset], 2));
				hexOffset += 2;

				// Byte 8: active
				bool active = hexToIntFixedLength(&hexData[hexOffset], 2) != 0;
				hexOffset += 2;

				// Byte 9: sceneValid (only for scene pads)
				bool sceneValid = false;
				if (type == ControlType::SCENE) {
					sceneValid = hexToIntFixedLength(&hexData[hexOffset], 2) != 0;
					hexOffset += 2;
				}

				// Find the pad index for this x, y
				int32_t padIndex = getPadIndex(x, y);
				if (padIndex >= 0 && padIndex < static_cast<int32_t>(pads_.size())) {
					pads_[padIndex].type = type;
					pads_[padIndex].valueIndex = valueIndex;
					pads_[padIndex].mode = mode;
					pads_[padIndex].active = active;
					pads_[padIndex].sceneValid = sceneValid;
				}

				padCount++;
			}
		}
		else if (!strcmp(tagName, "scenes")) {
			// Read scenes array (XML format)
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "scene")) {
					int32_t sceneIndex = -1;
					int32_t sceneSize = 0;

					// Read scene attributes
					char const* innerTag;
					while (*(innerTag = reader.readNextTagOrAttributeName())) {
						if (!strcmp(innerTag, "index")) {
							sceneIndex = reader.readTagOrAttributeValueInt();
						}
						else if (!strcmp(innerTag, "size")) {
							sceneSize = reader.readTagOrAttributeValueInt();
						}
						else if (!strcmp(innerTag, "data")) {
							// Scene data as hex attribute
							char const* hexData = reader.readTagOrAttributeValue();

							if (sceneIndex >= 0 && sceneIndex < kMaxScenes && sceneSize > 0
			    && sceneSize <= static_cast<int32_t>(kMaxSceneDataSize)) {
								// Skip "0x" prefix if present
								if (hexData[0] == '0' && hexData[1] == 'x') {
									hexData += 2;
								}

								// Parse hex data into scene buffer with safety checks
								int32_t hexLength = strlen(hexData);
								int32_t maxBytes = hexLength / 2; // Each byte = 2 hex chars
								int32_t bytesToRead = std::min(sceneSize, maxBytes);
								bytesToRead = std::min(bytesToRead, static_cast<int32_t>(kMaxSceneDataSize));

								for (int32_t i = 0; i < bytesToRead; ++i) {
									if ((i * 2 + 1) < hexLength) {
										sceneBuffers_[sceneIndex][i] = hexToIntFixedLength(&hexData[i * 2], 2);
									}
									else {
										sceneBuffers_[sceneIndex][i] = 0; // Pad with zeros if hex data is short
									}
								}
								sceneSizes_[sceneIndex] = bytesToRead; // Use actual bytes read
							}
						}
						else {
							// Handle old format (hex data as tag content) for backward compatibility
							char const* hexData = reader.readTagOrAttributeValue();

							if (sceneIndex >= 0 && sceneIndex < kMaxScenes && sceneSize > 0
							    && sceneSize <= static_cast<int32_t>(kMaxSceneDataSize)) {
								// Skip "0x" prefix if present
								if (hexData[0] == '0' && hexData[1] == 'x') {
									hexData += 2;
								}

								// Parse hex data into scene buffer
								for (int32_t i = 0; i < sceneSize; ++i) {
									sceneBuffers_[sceneIndex][i] = hexToIntFixedLength(&hexData[i * 2], 2);
								}
								sceneSizes_[sceneIndex] = sceneSize;
							}

							reader.exitTag(innerTag);
						}
					}
					reader.exitTag("scene");
				}
				else {
					reader.exitTag(tagName);
				}
			}
		}
		else {
			reader.exitTag(tagName);
		}
	}

	return Error::NONE;
}

} // namespace deluge::model::clip::sequencer
