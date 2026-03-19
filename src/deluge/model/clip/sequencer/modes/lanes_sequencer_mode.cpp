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

#include "model/clip/sequencer/modes/lanes_sequencer_mode.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "io/debug/log.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/consequence/consequence_lanes_change.h"
#include "model/instrument/melodic_instrument.h"
#include "model/model_stack.h"
#include "model/scale/musical_key.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>

extern deluge::gui::menu_item::HorizontalMenu lanesTriggerEditorMenu;
extern deluge::gui::menu_item::HorizontalMenu lanesPitchEditorMenu;
extern deluge::gui::menu_item::HorizontalMenu lanesIntervalEditorMenu;

namespace deluge::model::clip::sequencer::modes {

using namespace deluge::gui;

// Lane colours indexed by lane: trigger, pitch, octave, velocity, gate, interval, retrig, prob, glide
static const RGB kLanesLaneColours[LanesEngine::kNumLanes] = {
    RGB(255, 180, 50), // trigger  - amber
    RGB(0, 200, 200),  // pitch    - cyan
    RGB(160, 0, 255),  // octave   - purple
    RGB(255, 0, 0),    // velocity - red
    RGB(0, 255, 0),    // gate     - green
    RGB(0, 0, 255),    // interval - blue
    RGB(255, 128, 0),  // retrig   - orange
    RGB(255, 255, 0),  // prob     - yellow
    RGB(0, 200, 160),  // glide    - teal
};

REGISTER_SEQUENCER_MODE(LanesSequencerMode, "lanes");

// ========== Utility ==========

LanesEngine* getCurrentLanesEngine() {
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip && clip->hasSequencerMode()) {
		auto* mode = clip->getSequencerMode();
		auto* lanesMode = static_cast<LanesSequencerMode*>(mode);
		if (lanesMode && clip->getSequencerModeName() == "lanes") {
			return lanesMode->getEngine();
		}
	}
	return nullptr;
}

// ========== Construction ==========

LanesSequencerMode::LanesSequencerMode() : engine_(std::make_unique<LanesEngine>()) {
}

// ========== Helpers ==========

int32_t LanesSequencerMode::lanesRowToLaneIdx(int32_t yDisplay) {
	int32_t row = (kDisplayHeight - 1) - yDisplay;
	if (row < 0 || row >= LanesEngine::kVisibleLanes) {
		return -1;
	}
	return LanesEngine::kLaneMap[row];
}

RGB LanesSequencerMode::laneColor(int32_t laneIdx) {
	if (laneIdx < 0 || laneIdx >= LanesEngine::kNumLanes) {
		return colours::black;
	}
	return kLanesLaneColours[laneIdx];
}

// ========== Lifecycle ==========

void LanesSequencerMode::initialize() {
	if (!engine_) {
		engine_ = std::make_unique<LanesEngine>();
	}
	engine_->reset();

	// Initialize default velocity from instrument
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip && clip->output && clip->output->type != OutputType::NONE) {
		engine_->defaultVelocity = static_cast<Instrument*>(clip->output)->defaultVelocity;
		engine_->velocity.values.fill(engine_->defaultVelocity);
	}

	resetPlaybackState();

	// Initialize control columns with lanes-appropriate defaults
	controlColumnState_.initialize();
	// Override: add EVOLVE on a pad (replace one of the NONE slots)
	// x16 y2 = EVOLVE at 30% (gentle drift)
	auto& pads = controlColumnState_;
	// We access pads via handlePad — just re-initialize with good defaults
	// The base initialize() already sets OCTAVE, TRANSPOSE, SCENE, RANDOM, RESET
	// Let's also put EVOLVE and CLOCK_DIV in useful spots

	D_PRINTLN("LanesSequencerMode::initialize() complete");

	// Clear the white progress column from normal clip mode
	uint8_t tickSquares[kDisplayHeight];
	uint8_t tickColours[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight);
	memset(tickColours, 0, kDisplayHeight);
	PadLEDs::setTickSquares(tickSquares, tickColours);
}

void LanesSequencerMode::cleanup() {
	// Stop any playing notes before cleanup
	if (currentNote_ >= 0) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(getCurrentClip());
		stopCurrentNote(modelStackWithTimelineCounter);
	}
	resetPlaybackState();
}

void LanesSequencerMode::resetPlaybackState() {
	currentNote_ = -1;
	gateTicksRemaining_ = 0;
	tieActive_ = false;
	retriggersRemaining_ = 0;
	retriggerSubTicks_ = 0;
	retriggerVelocity_ = 100;
	retriggerGapPhase_ = false;
	lastAbsolutePos_ = 0;
}

void LanesSequencerMode::togglePerformanceMode() {
	performanceMode_ = !performanceMode_;
	if (performanceMode_) {
		display->displayPopup("PERFORM");
	}
	else {
		display->displayPopup("LANES");
	}
	uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
}

void LanesSequencerMode::stopCurrentNote(void* modelStackPtr) {
	if (currentNote_ >= 0) {
		stopNote(modelStackPtr, currentNote_);
		currentNote_ = -1;
	}
}

// ========== Rendering ==========

bool LanesSequencerMode::renderPads(uint32_t whichRows, RGB* image,
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll,
                                    uint32_t xZoom, int32_t renderWidth, int32_t imageWidth) {
	if (!engine_) {
		return false;
	}

	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		if (!(whichRows & (1 << yDisplay))) {
			image += imageWidth;
			continue;
		}

		int32_t laneIdx = lanesRowToLaneIdx(yDisplay);
		uint8_t* occRow = occupancyMask ? occupancyMask[yDisplay] : nullptr;

		if (laneIdx < 0 || laneIdx >= LanesEngine::kNumLanes) {
			std::fill(image, &image[renderWidth], colours::black);
			if (occRow) {
				memset(occRow, 0, renderWidth);
			}
			image += imageWidth;
			continue;
		}

		LanesLane* lane = engine_->getLane(laneIdx);
		RGB baseColor = kLanesLaneColours[laneIdx];

		for (int32_t x = 0; x < renderWidth; x++) {
			if (!lane || lane->length == 0 || x >= lane->length) {
				image[x] = baseColor.dim(5);
				if (occRow) {
					occRow[x] = 64;
				}
				continue;
			}

			int16_t value = lane->values[x];
			RGB padColor;

			if (laneIdx == 0) {
				padColor = (value != 0) ? baseColor : colours::black;
			}
			else {
				if (value == 0) {
					padColor = colours::black;
				}
				else {
					int32_t absVal = (value < 0) ? -value : value;
					int16_t maxVal = LanesEngine::kLaneMaxs[laneIdx];
					int16_t minVal = LanesEngine::kLaneMins[laneIdx];
					int32_t maxAbs = (-minVal > maxVal) ? -minVal : maxVal;
					uint8_t brightness = (maxAbs > 0) ? static_cast<uint8_t>(40 + (absVal * 215 / maxAbs)) : 255;
					padColor = baseColor.transform([brightness](RGB::channel_type ch) -> RGB::channel_type {
						return static_cast<RGB::channel_type>((ch * brightness) / 255);
					});
				}
			}

			// Playhead indicator: brighten the current position
			if (lane && x == lane->lastPosition && x < lane->length) {
				// Make playhead white-ish by blending with white
				padColor = RGB(std::min(255, padColor.r + 180), std::min(255, padColor.g + 180),
				               std::min(255, padColor.b + 180));
			}

			image[x] = padColor;
			if (occRow) {
				occRow[x] = 64;
			}
		}

		image += imageWidth;
	}

	return true;
}

bool LanesSequencerMode::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (!engine_) {
		return false;
	}

	// Performance mode: use the framework's control column rendering
	if (performanceMode_) {
		return SequencerMode::renderSidebar(whichRows, image, occupancyMask);
	}

	// Lane mode: show lane colours and mute state
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (!(whichRows & (1 << i))) {
			continue;
		}

		int32_t laneIdx = lanesRowToLaneIdx(i);
		if (laneIdx >= 0 && laneIdx < LanesEngine::kNumLanes) {
			RGB lc = kLanesLaneColours[laneIdx];
			// Audition column (x17): show lane colour, brighter when pressed
			if (instrumentClipView.auditionPadIsPressed[i]) {
				image[i][kDisplayWidth + 1] = lc;
			}
			else {
				image[i][kDisplayWidth + 1] = lc.dim(1);
			}

			// Mute column (x16): show lane mute state
			LanesLane* lane = engine_->getLane(laneIdx);
			if (lane && lane->muted) {
				image[i][kDisplayWidth] = colours::black;
			}
			else {
				image[i][kDisplayWidth] = kLanesLaneColours[laneIdx].dim(3);
			}
		}
		else {
			image[i][kDisplayWidth + 1] = colours::black;
			image[i][kDisplayWidth] = colours::black;
		}
	}

	return true;
}

// ========== Pad Input ==========

bool LanesSequencerMode::handlePadPress(int32_t x, int32_t y, int32_t velocity) {
	if (!engine_) {
		return false;
	}

	// Sidebar pads (x16-17)
	if (x >= kDisplayWidth) {
		// Performance mode: delegate all sidebar pads to framework control columns
		if (performanceMode_) {
			return SequencerMode::handlePadPress(x, y, velocity);
		}

		// Lane mode: mute column toggle (x16)
		if (x == kDisplayWidth && velocity > 0) {
			int32_t laneIdx = lanesRowToLaneIdx(y);
			LanesLane* lane = engine_->getLane(laneIdx);
			if (lane) {
				lane->muted = !lane->muted;
				uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
				return true;
			}
		}
		// Audition column (x17)
		if (x == kDisplayWidth + 1) {
			if (velocity > 0) {
				instrumentClipView.lastAuditionedYDisplay = y;
				instrumentClipView.auditionPadIsPressed[y] = velocity;
				enterUIMode(UI_MODE_AUDITIONING);

				int32_t laneIdx = lanesRowToLaneIdx(y);
				if (laneIdx >= 0 && laneIdx < LanesEngine::kNumLanes) {
					LanesLane* lane = engine_->getLane(laneIdx);
					if (lane) {
						if (display->haveOLED()) {
							char buffer[30];
							if (lane->clockDivision > 1) {
								snprintf(buffer, sizeof(buffer), "%s L:%d D:/%d", LanesEngine::kLaneNames[laneIdx],
								         lane->length, lane->clockDivision);
							}
							else {
								snprintf(buffer, sizeof(buffer), "%s L:%d", LanesEngine::kLaneNames[laneIdx],
								         lane->length);
							}
							display->displayPopup(buffer);
						}
						else {
							char buffer[5];
							intToString(lane->length, buffer);
							display->displayPopup(buffer, 0, true);
						}
					}
					else {
						const char* laneName = display->haveOLED() ? LanesEngine::kLaneNames[laneIdx]
						                                           : LanesEngine::kLaneNames7Seg[laneIdx];
						display->displayPopup(laneName);
					}
				}
			}
			else {
				instrumentClipView.auditionPadIsPressed[y] = 0;
				bool anyPressed = false;
				for (int32_t i = 0; i < kDisplayHeight; i++) {
					if (instrumentClipView.auditionPadIsPressed[i]) {
						anyPressed = true;
						break;
					}
				}
				if (!anyPressed) {
					exitUIMode(UI_MODE_AUDITIONING);
				}
			}
			uiNeedsRendering(&instrumentClipView, 0, 1 << y);
			return true;
		}
		return SequencerMode::handlePadPress(x, y, velocity);
	}

	// Main grid pads (x0-x15): step editing
	int32_t laneIdx = lanesRowToLaneIdx(y);
	LanesLane* lane = engine_->getLane(laneIdx);
	if (!lane || x >= kMaxLanesSteps) {
		return true; // Consumed but nothing to do
	}

	if (velocity > 0) {
		// Press: record pad for knob editing
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (!instrumentClipView.editPadPresses[i].isActive) {
				instrumentClipView.editPadPresses[i].isActive = true;
				instrumentClipView.editPadPresses[i].yDisplay = y;
				instrumentClipView.editPadPresses[i].xDisplay = x;
				instrumentClipView.editPadPresses[i].deleteOnDepress = true;
				instrumentClipView.numEditPadPresses++;
				instrumentClipView.numEditPadPressesPerNoteRowOnScreen[y]++;
				enterUIMode(UI_MODE_NOTES_PRESSED);
				break;
			}
		}
	}
	else {
		// Release: find matching press
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive && instrumentClipView.editPadPresses[i].yDisplay == y
			    && instrumentClipView.editPadPresses[i].xDisplay == x) {

				if (instrumentClipView.editPadPresses[i].deleteOnDepress) {
					// Snapshot for undo
					InstrumentClip* clip = getCurrentInstrumentClip();
					Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);
					if (action) {
						void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
						if (mem) {
							action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
						}
					}

					// Toggle step value
					if (x >= lane->length) {
						// Extend lane
						int16_t fillVal = (laneIdx == 0) ? 0 : LanesEngine::kLaneDefaults[laneIdx];
						for (int32_t j = lane->length; j <= x; j++) {
							lane->values[j] = fillVal;
						}
						lane->length = x + 1;
						lane->values[x] = (laneIdx == 0) ? 1 : LanesEngine::kLaneDefaults[laneIdx];
					}
					else if (laneIdx == 0) {
						lane->values[x] = (lane->values[x] != 0) ? 0 : 1;
					}
					else {
						lane->values[x] = (lane->values[x] != 0) ? 0 : LanesEngine::kLaneDefaults[laneIdx];
					}
					uiNeedsRendering(&instrumentClipView, 1 << y, 0);
				}
				instrumentClipView.endEditPadPress(i);
				instrumentClipView.checkIfAllEditPadPressesEnded();
				break;
			}
		}
	}

	return true;
}

// ========== Encoder Input ==========

bool LanesSequencerMode::handleHorizontalEncoder(int32_t offset, bool encoderPressed) {
	if (!engine_) {
		return false;
	}

	// Let base class handle control column pads first
	if (SequencerMode::handleHorizontalEncoder(offset, encoderPressed)) {
		return true;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();

	// Encoder button press actions
	if (encoderPressed && offset == 0) {
		// Hold grid pad + press horiz encoder = randomize step
		if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
			Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);
			if (action) {
				void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
				if (mem) {
					action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
				}
			}
			for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
				if (instrumentClipView.editPadPresses[i].isActive) {
					instrumentClipView.editPadPresses[i].deleteOnDepress = false;
					int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.editPadPresses[i].yDisplay);
					LanesLane* lane = engine_->getLane(laneIdx);
					if (!lane) {
						continue;
					}
					int32_t step = instrumentClipView.editPadPresses[i].xDisplay;
					if (step >= lane->length) {
						continue;
					}
					int16_t newVal =
					    engine_->randomInRange(LanesEngine::kLaneRndMins[laneIdx], LanesEngine::kLaneRndMaxs[laneIdx]);
					lane->values[step] = newVal;

					if (laneIdx == 1) {
						char noteStr[8];
						int32_t semis = scaleDegreeToSemitoneOffset(newVal, currentSong->key);
						noteCodeToString(engine_->baseNote + semis, noteStr, nullptr, true);
						display->displayPopup(noteStr);
					}
					else if (display->haveOLED()) {
						char buffer[30];
						snprintf(buffer, sizeof(buffer), "%s: RND %d", LanesEngine::kLaneNames[laneIdx], newVal);
						display->displayPopup(buffer);
					}
					else {
						char buffer[5];
						intToString(newVal, buffer);
						display->displayPopup(buffer, 0, true);
					}
				}
			}
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			return true;
		}

		// Hold audition + press horiz encoder = randomize entire lane
		if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
			Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);
			if (action) {
				void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
				if (mem) {
					action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
				}
			}
			int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.lastAuditionedYDisplay);
			engine_->randomizeLane(laneIdx);
			if (display->haveOLED()) {
				char buffer[30];
				snprintf(buffer, sizeof(buffer), "%s: RANDOMIZED", LanesEngine::kLaneNames[laneIdx]);
				display->displayPopup(buffer);
			}
			else {
				display->displayPopup(LanesEngine::kLaneNames7Seg[laneIdx]);
			}
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
			return true;
		}
		return false;
	}

	// Encoder rotation

	// Hold pad + horizontal encoder = adjust step value / euclidean length
	if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
			if (mem) {
				action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
			}
		}
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive) {
				instrumentClipView.editPadPresses[i].deleteOnDepress = false;
				int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.editPadPresses[i].yDisplay);
				LanesLane* lane = engine_->getLane(laneIdx);
				if (!lane) {
					continue;
				}

				if (laneIdx == 0) {
					// TRIGGER: euclidean length
					int32_t newLen = lane->length + offset;
					newLen = std::clamp(newLen, (int32_t)1, (int32_t)kMaxLanesSteps);
					engine_->generateEuclidean(newLen, static_cast<int32_t>(lane->euclideanPulses));
					if (display->haveOLED()) {
						char buffer[30];
						snprintf(buffer, sizeof(buffer), "TRIG LEN:%d P:%d", lane->length, lane->euclideanPulses);
						display->displayPopup(buffer);
					}
					else {
						char buffer[5];
						intToString(lane->length, buffer);
						display->displayPopup(buffer, 0, true);
					}
				}
				else {
					int32_t step = instrumentClipView.editPadPresses[i].xDisplay;
					if (step >= lane->length) {
						continue;
					}
					int16_t newVal = lane->values[step] + offset;
					newVal = std::clamp(newVal, LanesEngine::kLaneMins[laneIdx], LanesEngine::kLaneMaxs[laneIdx]);
					lane->values[step] = newVal;

					if (laneIdx == 1) {
						char noteStr[8];
						int32_t semis = scaleDegreeToSemitoneOffset(newVal, currentSong->key);
						noteCodeToString(engine_->baseNote + semis, noteStr, nullptr, true);
						display->displayPopup(noteStr);
					}
					else if (laneIdx == 4) {
						if (newVal == LanesEngine::kGateTie) {
							display->displayPopup("TIE");
						}
						else if (newVal == LanesEngine::kGateLegato) {
							display->displayPopup("LEGATO");
						}
						else if (display->haveOLED()) {
							char buffer[20];
							snprintf(buffer, sizeof(buffer), "GATE: %d%%", newVal);
							display->displayPopup(buffer);
						}
						else {
							char buffer[5];
							intToString(newVal, buffer);
							display->displayPopup(buffer, 0, true);
						}
					}
					else if (display->haveOLED()) {
						char buffer[30];
						snprintf(buffer, sizeof(buffer), "%s: %d", LanesEngine::kLaneNames[laneIdx], newVal);
						display->displayPopup(buffer);
					}
					else {
						char buffer[5];
						intToString(newVal, buffer);
						display->displayPopup(buffer, 0, true);
					}
				}
			}
		}
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		return true;
	}

	// Hold audition + horizontal encoder = change lane length
	if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
			if (mem) {
				action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
			}
		}
		int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.lastAuditionedYDisplay);
		LanesLane* lane = engine_->getLane(laneIdx);
		if (lane) {
			int32_t newLength = lane->length + offset;
			newLength = std::clamp(newLength, (int32_t)1, (int32_t)kMaxLanesSteps);
			if (newLength > lane->length) {
				int16_t defaultVal = LanesEngine::kLaneDefaults[laneIdx];
				for (int32_t i = lane->length; i < newLength; i++) {
					lane->values[i] = (laneIdx == 0) ? 0 : defaultVal;
				}
			}
			lane->length = newLength;
			if (lane->position >= newLength) {
				lane->position = 0;
			}

			if (display->haveOLED()) {
				char buffer[20];
				snprintf(buffer, sizeof(buffer), "%s LEN:%d", LanesEngine::kLaneNames[laneIdx], newLength);
				display->displayPopup(buffer);
			}
			else {
				char buffer[5];
				intToString(newLength, buffer);
				display->displayPopup(buffer, 0, true);
			}
			uiNeedsRendering(&instrumentClipView, 1 << instrumentClipView.lastAuditionedYDisplay, 0);
		}
		return true;
	}

	return false;
}

bool LanesSequencerMode::handleModeSpecificVerticalEncoder(int32_t offset) {
	if (!engine_) {
		return false;
	}

	// Hold pad + vertical encoder = per-step prob / euclidean pulses
	if (isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
			if (mem) {
				action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
			}
		}
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive) {
				instrumentClipView.editPadPresses[i].deleteOnDepress = false;
				int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.editPadPresses[i].yDisplay);
				LanesLane* lane = engine_->getLane(laneIdx);
				if (!lane) {
					continue;
				}

				if (laneIdx == 0) {
					// TRIGGER: euclidean pulses
					int32_t newPulses = static_cast<int32_t>(lane->euclideanPulses) + offset;
					newPulses = std::clamp(newPulses, (int32_t)0, static_cast<int32_t>(lane->length));
					engine_->generateEuclidean(static_cast<int32_t>(lane->length), newPulses);
					if (display->haveOLED()) {
						char buffer[30];
						snprintf(buffer, sizeof(buffer), "TRIG LEN:%d P:%d", lane->length, lane->euclideanPulses);
						display->displayPopup(buffer);
					}
					else {
						char buffer[5];
						intToString(lane->euclideanPulses, buffer);
						display->displayPopup(buffer, 0, true);
					}
				}
				else if (LanesEngine::kLaneHasStepProb[laneIdx]) {
					int32_t step = instrumentClipView.editPadPresses[i].xDisplay;
					if (step >= lane->length) {
						continue;
					}
					int32_t newProb = static_cast<int32_t>(lane->stepProbability[step]) + offset * 5;
					newProb = std::clamp(newProb, (int32_t)0, (int32_t)100);
					lane->stepProbability[step] = static_cast<uint8_t>(newProb);

					if (display->haveOLED()) {
						char buffer[30];
						snprintf(buffer, sizeof(buffer), "%s PROB:%d%%", LanesEngine::kLaneNames[laneIdx], newProb);
						display->displayPopup(buffer);
					}
					else {
						char buffer[5];
						intToString(newProb, buffer);
						display->displayPopup(buffer, 0, true);
					}
				}
			}
		}
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		return true;
	}

	// Hold audition + vertical encoder = change lane clock division
	if (isUIModeActiveExclusively(UI_MODE_AUDITIONING)) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
			if (mem) {
				action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
			}
		}
		int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.lastAuditionedYDisplay);
		LanesLane* lane = engine_->getLane(laneIdx);
		if (lane) {
			int32_t newDiv = static_cast<int32_t>(lane->clockDivision) + offset;
			newDiv = std::clamp(newDiv, (int32_t)1, (int32_t)LanesEngine::kMaxClockDivision);
			lane->clockDivision = static_cast<uint8_t>(newDiv);
			lane->divisionCounter = static_cast<uint8_t>(newDiv);

			const char* divName;
			switch (newDiv) {
			case 1:
				divName = "1/16";
				break;
			case 2:
				divName = "1/8";
				break;
			case 3:
				divName = "1/8T";
				break;
			case 4:
				divName = "1/4";
				break;
			case 6:
				divName = "1/4T";
				break;
			case 8:
				divName = "1/2";
				break;
			case 12:
				divName = "1/2T";
				break;
			case 16:
				divName = "1 BAR";
				break;
			default:
				divName = nullptr;
				break;
			}
			if (display->haveOLED()) {
				char buffer[30];
				if (divName) {
					snprintf(buffer, sizeof(buffer), "%s DIV:%s", LanesEngine::kLaneNames[laneIdx], divName);
				}
				else {
					snprintf(buffer, sizeof(buffer), "%s DIV:/%d", LanesEngine::kLaneNames[laneIdx], newDiv);
				}
				display->displayPopup(buffer);
			}
			else if (divName) {
				display->displayPopup(divName, 0, true);
			}
			else {
				char shortBuf[5];
				intToString(newDiv, shortBuf);
				display->displayPopup(shortBuf, 0, true);
			}
		}
		return true;
	}

	return false;
}

bool LanesSequencerMode::handleSelectEncoder(int32_t offset) {
	if (!engine_) {
		return false;
	}

	// Hold audition + select encoder = change lane direction
	if (isUIModeActive(UI_MODE_AUDITIONING)) {
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
		if (action) {
			void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceLanesChange));
			if (mem) {
				action->addConsequence(new (mem) ConsequenceLanesChange(engine_.get()));
			}
		}
		int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.lastAuditionedYDisplay);
		LanesLane* lane = engine_->getLane(laneIdx);
		if (lane) {
			static constexpr const char* kDirectionNames[] = {"Forward", "Reverse", "Pingpong"};
			static constexpr const char* kDirectionNames7Seg[] = {"FWD", "REV", "PONG"};
			int32_t dir = static_cast<int32_t>(lane->direction) + offset;
			dir = std::clamp(dir, (int32_t)0, (int32_t)2);
			lane->direction = static_cast<LaneDirection>(dir);

			if (display->haveOLED()) {
				char buffer[30];
				snprintf(buffer, sizeof(buffer), "%s: %s", LanesEngine::kLaneNames[laneIdx], kDirectionNames[dir]);
				display->displayPopup(buffer);
			}
			else {
				display->displayPopup(kDirectionNames7Seg[dir]);
			}
		}
		return true;
	}

	return false;
}

bool LanesSequencerMode::enterLaneEditor() {
	if (!engine_) {
		return false;
	}

	int32_t laneIdx = lanesRowToLaneIdx(instrumentClipView.lastAuditionedYDisplay);

	deluge::gui::menu_item::HorizontalMenu* laneMenu = nullptr;
	switch (laneIdx) {
	case 0: // Trigger
		laneMenu = &lanesTriggerEditorMenu;
		break;
	case 1: // Pitch
		laneMenu = &lanesPitchEditorMenu;
		break;
	case 5: // Interval
		laneMenu = &lanesIntervalEditorMenu;
		break;
	default: {
		const char* laneName =
		    display->haveOLED() ? LanesEngine::kLaneNames[laneIdx] : LanesEngine::kLaneNames7Seg[laneIdx];
		display->displayPopup(laneName);
		return false;
	}
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	display->setNextTransitionDirection(1);
	if (soundEditor.setup(clip, laneMenu)) {
		openUI(&soundEditor);
		return true;
	}
	return false;
}

// ========== Playback ==========

int32_t LanesSequencerMode::processPlayback(void* modelStackPtr, int32_t absolutePlaybackPos) {
	if (!engine_) {
		return 2147483647;
	}

	ModelStackWithTimelineCounter* modelStack = static_cast<ModelStackWithTimelineCounter*>(modelStackPtr);
	InstrumentClip* clip = static_cast<InstrumentClip*>(modelStack->getTimelineCounter());
	Song* song = modelStack->song;

	const int32_t stepTicks = song->getSixteenthNoteLength();
	if (stepTicks <= 0) {
		D_PRINTLN("Lanes: stepTicks <= 0!");
		return 2147483647;
	}

	// Calculate ticks since last call
	int32_t ticksSinceLast = 0;
	if (lastAbsolutePos_ > 0) {
		ticksSinceLast = absolutePlaybackPos - lastAbsolutePos_;
	}
	lastAbsolutePos_ = absolutePlaybackPos;

	if (ticksSinceLast <= 0 && ticksSinceLast != 0) {
		// Backward movement or wrap — reset
		stopCurrentNote(modelStackPtr);
		resetPlaybackState();
		lastAbsolutePos_ = absolutePlaybackPos;
		engine_->reset();
		return stepTicks;
	}

	// Gate/retrigger timer processing
	if (gateTicksRemaining_ > 0) {
		gateTicksRemaining_ -= ticksSinceLast;
		if (gateTicksRemaining_ <= 0) {
			if (retriggerGapPhase_) {
				retriggerGapPhase_ = false;
				if (currentNote_ >= 0) {
					playNote(modelStackPtr, currentNote_, retriggerVelocity_, 0);
					gateTicksRemaining_ = std::max(retriggerSubTicks_ / 2, (int32_t)1);
				}
				else {
					retriggersRemaining_ = 0;
					gateTicksRemaining_ = 0;
				}
			}
			else if (retriggersRemaining_ > 0 && !tieActive_) {
				stopNote(modelStackPtr, currentNote_);
				retriggersRemaining_--;
				retriggerGapPhase_ = true;
				int32_t gap = retriggerSubTicks_ - retriggerSubTicks_ / 2;
				gateTicksRemaining_ = std::max(gap, (int32_t)1);
			}
			else if (!tieActive_) {
				stopCurrentNote(modelStackPtr);
				gateTicksRemaining_ = 0;
			}
		}
	}

	// Check if we're at a step boundary
	bool atStepBoundary = atDivisionBoundary(absolutePlaybackPos, stepTicks);

	if (atStepBoundary) {
		LanesNote note = engine_->step(currentSong->key);

		// Apply performance control effects (transpose, octave shift)
		if (!note.isRest()) {
			CombinedEffects effects = getCombinedEffects();
			note.noteCode += effects.transpose;
			note.noteCode += effects.octaveShift * 12;
			note.noteCode = std::clamp((int32_t)note.noteCode, (int32_t)0, (int32_t)127);
		}

		// Refresh pad display to show playhead movement
		uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);

		retriggersRemaining_ = 0;
		retriggerGapPhase_ = false;

		if (!note.isRest()) {
			if (note.gate == LanesEngine::kGateTie) {
				if (currentNote_ == note.noteCode && currentNote_ >= 0) {
					tieActive_ = true;
					gateTicksRemaining_ = stepTicks;
				}
				else {
					stopCurrentNote(modelStackPtr);
					playNote(modelStackPtr, note.noteCode, note.velocity, 0);
					currentNote_ = note.noteCode;
					tieActive_ = true;
					gateTicksRemaining_ = stepTicks;
				}
			}
			else if (note.gate == LanesEngine::kGateLegato) {
				int16_t oldNote = currentNote_;
				playNote(modelStackPtr, note.noteCode, note.velocity, 0);
				if (oldNote >= 0) {
					stopNote(modelStackPtr, oldNote);
				}
				currentNote_ = note.noteCode;
				tieActive_ = false;
				gateTicksRemaining_ = stepTicks;
			}
			else {
				stopCurrentNote(modelStackPtr);
				playNote(modelStackPtr, note.noteCode, note.velocity, 0);
				currentNote_ = note.noteCode;
				tieActive_ = false;

				if (note.retrigger >= 2) {
					retriggersRemaining_ = note.retrigger - 1;
					retriggerSubTicks_ = stepTicks / note.retrigger;
					retriggerVelocity_ = note.velocity;
					gateTicksRemaining_ = std::max(retriggerSubTicks_ / 2, (int32_t)1);
				}
				else {
					int32_t gatePercent = std::clamp((int32_t)note.gate, (int32_t)5, (int32_t)100);
					gateTicksRemaining_ = stepTicks * gatePercent / 100;
					if (gateTicksRemaining_ < 1) {
						gateTicksRemaining_ = 1;
					}
				}
			}
		}
		else {
			stopCurrentNote(modelStackPtr);
			tieActive_ = false;
			gateTicksRemaining_ = 0;
		}
	}

	// Return ticks until next event
	int32_t ticksToNextStep = ticksUntilNextDivision(absolutePlaybackPos, stepTicks);
	int32_t nextEvent = ticksToNextStep;
	if (gateTicksRemaining_ > 0 && gateTicksRemaining_ < nextEvent) {
		nextEvent = gateTicksRemaining_;
	}
	return nextEvent;
}

void LanesSequencerMode::stopAllNotes(void* modelStackPtr) {
	stopCurrentNote(modelStackPtr);
	resetPlaybackState();
	if (engine_) {
		engine_->reset();
	}
}

// ========== Persistence ==========

void LanesSequencerMode::writeToFile(Serializer& writer, bool includeScenes) {
	if (engine_) {
		engine_->writeToFile(writer);
	}
}

Error LanesSequencerMode::readFromFile(Deserializer& reader) {
	if (!engine_) {
		engine_ = std::make_unique<LanesEngine>();
	}
	engine_->readFromFile(reader);
	return Error::NONE;
}

bool LanesSequencerMode::copyFrom(SequencerMode* other) {
	// RTTI is disabled, but copyFrom is only called with matching mode types
	auto* otherLanes = static_cast<LanesSequencerMode*>(other);
	if (!otherLanes || !otherLanes->engine_) {
		return false;
	}

	if (!engine_) {
		engine_ = std::make_unique<LanesEngine>();
	}

	// Deep copy engine state
	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		LanesLane* src = otherLanes->engine_->getLane(i);
		LanesLane* dst = engine_->getLane(i);
		if (src && dst) {
			*dst = *src;
		}
	}
	engine_->baseNote = otherLanes->engine_->baseNote;
	engine_->intervalAccumulator = otherLanes->engine_->intervalAccumulator;
	engine_->intervalMin = otherLanes->engine_->intervalMin;
	engine_->intervalMax = otherLanes->engine_->intervalMax;
	engine_->intervalScaleAware = otherLanes->engine_->intervalScaleAware;
	engine_->defaultVelocity = otherLanes->engine_->defaultVelocity;
	engine_->trackLength = otherLanes->engine_->trackLength;
	engine_->locked = otherLanes->engine_->locked;
	engine_->lockedSeed = otherLanes->engine_->lockedSeed;

	return true;
}

// ========== Generative ==========

void LanesSequencerMode::resetToInit() {
	if (engine_) {
		engine_->loadDefaultPattern();
	}
}

void LanesSequencerMode::randomizeAll(int32_t mutationRate) {
	if (!engine_) {
		return;
	}
	for (int32_t i = 0; i < LanesEngine::kNumLanes; i++) {
		engine_->randomizeLane(i);
	}
}

void LanesSequencerMode::evolveNotes(int32_t mutationRate) {
	if (!engine_) {
		return;
	}

	// Adaptive evolution based on mutation rate:
	// Low % (1-70%):  Gentle melodic drift — pitch and velocity only, small steps
	// High % (>70%):  Chaotic — also mutates trigger, octave, and gate lanes

	bool isHighRate = mutationRate > 70;

	// Always evolve pitch lane (lane 1) — small melodic drift
	LanesLane* pitchLane = engine_->getLane(1);
	if (pitchLane) {
		for (int32_t step = 0; step < pitchLane->length; step++) {
			if (engine_->randomInRange(0, 100) < mutationRate) {
				int16_t delta = isHighRate ? engine_->randomInRange(-3, 3) : engine_->randomInRange(-1, 1);
				int16_t newVal = pitchLane->values[step] + delta;
				newVal = std::clamp(newVal, LanesEngine::kLaneMins[1], LanesEngine::kLaneMaxs[1]);
				pitchLane->values[step] = newVal;
			}
		}
	}

	// Always evolve velocity lane (lane 3) — subtle dynamics shift
	LanesLane* velLane = engine_->getLane(3);
	if (velLane) {
		for (int32_t step = 0; step < velLane->length; step++) {
			if (engine_->randomInRange(0, 100) < mutationRate) {
				int16_t delta = isHighRate ? engine_->randomInRange(-15, 15) : engine_->randomInRange(-5, 5);
				int16_t newVal = velLane->values[step] + delta;
				newVal = std::clamp(newVal, LanesEngine::kLaneMins[3], LanesEngine::kLaneMaxs[3]);
				velLane->values[step] = newVal;
			}
		}
	}

	if (isHighRate) {
		// Evolve trigger lane (lane 0) — occasional gate flips
		LanesLane* trigLane = engine_->getLane(0);
		if (trigLane) {
			for (int32_t step = 0; step < trigLane->length; step++) {
				if (engine_->randomInRange(0, 100) < 25) { // 25% chance per step
					trigLane->values[step] = trigLane->values[step] ? 0 : 1;
				}
			}
		}

		// Evolve octave lane (lane 2) — occasional ±1
		LanesLane* octLane = engine_->getLane(2);
		if (octLane) {
			for (int32_t step = 0; step < octLane->length; step++) {
				if (engine_->randomInRange(0, 100) < 40) {
					int16_t delta = engine_->randomInRange(-1, 1);
					int16_t newVal = octLane->values[step] + delta;
					newVal = std::clamp(newVal, LanesEngine::kLaneMins[2], LanesEngine::kLaneMaxs[2]);
					octLane->values[step] = newVal;
				}
			}
		}

		// Evolve gate lane (lane 4) — shift gate lengths
		LanesLane* gateLane = engine_->getLane(4);
		if (gateLane) {
			for (int32_t step = 0; step < gateLane->length; step++) {
				if (engine_->randomInRange(0, 100) < 30) {
					int16_t delta = engine_->randomInRange(-10, 10);
					int16_t newVal = gateLane->values[step] + delta;
					newVal = std::clamp(newVal, LanesEngine::kLaneMins[4], LanesEngine::kLaneMaxs[4]);
					gateLane->values[step] = newVal;
				}
			}
		}
	}

	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
}

// ========== Scenes ==========

size_t LanesSequencerMode::captureScene(void* buffer, size_t maxSize) {
	// For now, return 0 (scenes not yet supported for lanes)
	return 0;
}

bool LanesSequencerMode::recallScene(const void* buffer, size_t size) {
	return false;
}

} // namespace deluge::model::clip::sequencer::modes
