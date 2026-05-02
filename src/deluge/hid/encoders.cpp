/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoders.h"
#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "util/functions.h"
#include <atomic>
#include <new>

extern "C" {
#include "RZA1/gpio/gpio.h"
#include "RZA1/intc/devdrv_intc.h"
}

namespace deluge::hid::encoders {

std::array<Encoder, util::to_underlying(EncoderName::MAX_ENCODER)> encoders = {};
extern uint32_t timeModEncoderLastTurned[];
uint32_t timeModEncoderLastTurned[2];
int8_t modEncoderInitialTurnDirection[2];

uint32_t encodersWaitingForCardRoutineEnd;

TaskID action_task_id = -1;

namespace {
constexpr size_t kNumEncoders = util::to_underlying(EncoderName::MAX_ENCODER);
struct EncoderIrqEntry {
	/// @brief  A-side pin that's routed via PFC alt-2 to RZ/A1L IRQn.
	uint8_t irqPin;

	/// @brief companion (B-side) pin, read as plain GPIO inside the ISR.
	uint8_t compPin;
	uint8_t irqNum;

	/// @brief flip the direction sense when the A/B wiring is swapped relative to the polled order in `setPins(...)`.
	bool invert;
};

constexpr EncoderIrqEntry kEncoderIrqMap[kNumEncoders] = {
    [util::to_underlying(EncoderName::SCROLL_Y)] = {.irqPin = 8, .compPin = 10, .irqNum = 0, .invert = false},
    [util::to_underlying(EncoderName::SCROLL_X)] = {.irqPin = 11, .compPin = 12, .irqNum = 3, .invert = false},
    [util::to_underlying(EncoderName::TEMPO)] = {.irqPin = 6, .compPin = 7, .irqNum = 2, .invert = true},
    [util::to_underlying(EncoderName::SELECT)] = {.irqPin = 3, .compPin = 2, .irqNum = 7, .invert = true},
    [util::to_underlying(EncoderName::MOD_1)] = {.irqPin = 5, .compPin = 4, .irqNum = 1, .invert = false},
    [util::to_underlying(EncoderName::MOD_0)] = {.irqPin = 0, .compPin = 15, .irqNum = 4, .invert = false},
};

/// Atomic edge counters written by ISRs, drained by `readEncoders()`.
std::atomic<int8_t> encoderEdgeDeltas[kNumEncoders] = {};

template <size_t IDX>
void encoderIrqHandler(uint32_t /*sense*/) {
	constexpr EncoderIrqEntry m = kEncoderIrqMap[IDX];
	bool a = readInput(1, m.irqPin) != 0;
	bool b = readInput(1, m.compPin) != 0;
	bool cw = m.invert ? (a != b) : (a == b);
	int8_t inc = cw ? +1 : -1;
	encoderEdgeDeltas[IDX].fetch_add(inc, std::memory_order_relaxed);
	clearIRQInterrupt(m.irqNum);
}

using IrqHandler = void (*)(uint32_t);
constexpr IrqHandler kEncoderIrqHandlers[kNumEncoders] = {
    &encoderIrqHandler<0>, &encoderIrqHandler<1>, &encoderIrqHandler<2>,
    &encoderIrqHandler<3>, &encoderIrqHandler<4>, &encoderIrqHandler<5>,
};

constexpr uint8_t kEncoderIrqPriority = 14;

void initInterrupts() {
	for (size_t i = 0; i < kNumEncoders; i++) {
		const auto& m = kEncoderIrqMap[i];

		// Route the A-side pin to its IRQn input via PFC alt-function 2.
		setPinMux(1, m.irqPin, 2);
		enableInputBuffer(1, m.irqPin);

		setPinAsInput(1, m.compPin);

		setIRQInterruptBothEdges(m.irqNum);

		clearIRQInterrupt(m.irqNum);
		setupAndEnableInterrupt(kEncoderIrqHandlers[i], INTC_ID_IRQ0 + m.irqNum, kEncoderIrqPriority);
	}
}
} // namespace

Encoder& getEncoder(EncoderName which) {
	return encoders[util::to_underlying(which)];
}

void init() {
	getEncoder(EncoderName::SCROLL_X).setPins(1, 11, 1, 12);
	getEncoder(EncoderName::TEMPO).setPins(1, 7, 1, 6);
	getEncoder(EncoderName::MOD_0).setPins(1, 0, 1, 15);
	getEncoder(EncoderName::MOD_1).setPins(1, 5, 1, 4);
	getEncoder(EncoderName::SCROLL_Y).setPins(1, 8, 1, 10);
	getEncoder(EncoderName::SELECT).setPins(1, 2, 1, 3);

	getEncoder(EncoderName::MOD_0).setNonDetentMode();
	getEncoder(EncoderName::MOD_1).setNonDetentMode();

	initInterrupts();
}

void readEncoders() {
	int8_t ticksAccumulated = 0;
	for (size_t i = 0; i < util::to_underlying(EncoderName::MAX_ENCODER); i++) {
		int8_t edges = encoderEdgeDeltas[i].exchange(0, std::memory_order_relaxed);
		if (edges != 0) {
			ticksAccumulated += encoders[i].applyEdges(edges);
		}
	}
	// did we touch any of the encoders and we've setup the action encoders task?
	if (ticksAccumulated > 0 && action_task_id != -1) {
		// run Encoders::interpretEncoders(false) task so that scheduler is aware
		unblockTask(action_task_id);
	}
}

void setActionTaskID(TaskID id) {
	action_task_id = id;
}

bool interpretEncoders(bool skipActioning) {
	// do not interpret encoders when stem export is underway
	if (stemExport.processStarted) {
		return false;
	}

	skipActioning |= sdRoutineLock; // if the "sd routine" is yielding then always defer actioning encoders
	bool anything = false;

	if (!skipActioning) {
		encodersWaitingForCardRoutineEnd = 0;
	}

	for (int32_t e = 0; e < util::to_underlying(EncoderName::MAX_FUNCTION_ENCODERS); e++) {
		auto name = static_cast<EncoderName>(e);
		if (name != EncoderName::SCROLL_Y) {

			// Basically disables all function encoders during SD routine
			if (skipActioning && currentUIMode != UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
				continue;
			}
		}

		if (encodersWaitingForCardRoutineEnd & (1 << e)) {
			continue;
		}

		if (encoders[e].detentPos != 0) {
			anything = true;

			// Limit. Some functions can break if they receive bigger numbers, e.g. LoadSongUI::selectEncoderAction()
			int32_t limitedDetentPos = encoders[e].detentPos;
			encoders[e].detentPos = 0; // Reset. Crucial that this happens before we call selectEncoderAction()
			if (limitedDetentPos >= 0) {
				limitedDetentPos = 1;
			}
			else {
				limitedDetentPos = -1;
			}

			ActionResult result;

			switch (name) {

			case EncoderName::SCROLL_X:
				result = getCurrentUI()->horizontalEncoderAction(limitedDetentPos);
				// Actually, after coding this up, I realise I actually have it above stopping the X encoder from even
				// getting here during the SD routine. Ok so we'll leave it that way, in addition to me having made all
				// the horizontalEncoderAction() calls SD-routine-safe
checkResult:
				if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
					encodersWaitingForCardRoutineEnd |= (1 << e);
					encoders[e].detentPos = limitedDetentPos; // Put it back for next time
				}
				break;

			case EncoderName::SCROLL_Y:
				if (Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
					PadLEDs::changeDimmerInterval(limitedDetentPos);
				}
				else {
					result = getCurrentUI()->verticalEncoderAction(limitedDetentPos, skipActioning);
					goto checkResult;
				}
				break;

			case EncoderName::TEMPO:
				if ((getCurrentUI() == &instrumentClipView
				     || (getCurrentUI() == &automationView && automationView.inNoteEditor()))
				    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize)
				           == RuntimeFeatureStateToggle::On) {
					instrumentClipView.tempoEncoderAction(limitedDetentPos,
					                                      Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                      Buttons::isShiftButtonPressed());
				}
				else {
					playbackHandler.tempoEncoderAction(limitedDetentPos,
					                                   Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                   Buttons::isShiftButtonPressed());
				}
				break;

			case EncoderName::SELECT:
				if (Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW)) {
					PadLEDs::changeRefreshTime(limitedDetentPos);
				}
				else if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
					if (currentSong) {
						currentSong->changeThresholdRecordingMode(limitedDetentPos);
					}
				}
				else {
					getCurrentUI()->selectEncoderAction(limitedDetentPos);
				}
				break;

			// explicit fallthrough cases
			case EncoderName::MOD_0: // nothing, really?
			case EncoderName::MAX_ENCODER:
			case EncoderName::MAX_FUNCTION_ENCODERS:;
			}
		}
	}

	if (!skipActioning || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		// Mod knobs
		for (int32_t e = 0; e < 2; e++) {
			// check encoder 0, then encoder 1
			auto& encoder = encoders[util::to_underlying(EncoderName::MOD_0) - e];

			// If encoder turned...
			if (encoder.encPos != 0) {
				anything = true;

				bool turnedRecently = (AudioEngine::audioSampleTimer - timeModEncoderLastTurned[e] < kShortPressTime);

				// If it was turned recently...
				if (turnedRecently) {

					// Mark as turned recently again. Must do this before the encoder-action gets invoked below, because
					// that might want to reset this
					timeModEncoderLastTurned[e] = AudioEngine::audioSampleTimer;

					// Do it, only if
					if (encoder.encPos + modEncoderInitialTurnDirection[e] != 0) {
						getCurrentUI()->modEncoderAction(e, encoder.encPos);
						modEncoderInitialTurnDirection[e] = 0;
					}

					// Otherwise, write this off as an accidental wiggle
					else {
						modEncoderInitialTurnDirection[e] = encoder.encPos;
					}
				}

				// Or if it wasn't turned recently, it's going to get marked as turned recently now, but remember what
				// direction we came, so that if we go back that direction again we can write it off as an accidental
				// wiggle
				else {

					// If the other one also hasn't been turned for a while...
					bool otherTurnedRecently =
					    (AudioEngine::audioSampleTimer - timeModEncoderLastTurned[1 - e] < kShortPressTime);
					if (!otherTurnedRecently) {
						actionLogger.closeAction(ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE);
					}

					modEncoderInitialTurnDirection[e] = encoder.encPos;

					// Mark as turned recently
					timeModEncoderLastTurned[e] = AudioEngine::audioSampleTimer;
				}

				encoder.encPos = 0;
			}
		}
	}

	// did we try to action anything and we've setup the actioning task?
	if (!skipActioning && action_task_id != -1) {
		// re-block the task so that it is only actioned when it needs to
		blockTask(action_task_id);
	}

	return anything;
}

} // namespace deluge::hid::encoders
