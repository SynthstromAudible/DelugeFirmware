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

#include "hid/encoder_input.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/encoders.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "libdeluge/encoder_io.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/stem_export/stem_export.h"
#include "util/functions.h"

namespace deluge::hid::encoders {

uint32_t timeModEncoderLastTurned[2];
static int8_t modEncoderInitialTurnDirection[2];

static uint32_t timeNextSDTestAction = 0;
static int32_t nextSDTestDirection = 1;

static uint32_t encodersWaitingForCardRoutineEnd;

void interpretEncodersTask() {
	interpretEncoders(false);
	blockTask(EncoderTaskID);
}

bool interpretEncoders(bool skipActioning) {
	// The board's ISR has accumulated signed quadrature edges since we last ran; fold them into each
	// encoder's position here in task context (detent reduction and the non-detent gold-knob handling
	// live in applyEdges). Ordinals match the board's encoder pin map (see the BSP's encoder_io).
	scrollY.applyEdges(deluge_encoder_take_edges(0));
	scrollX.applyEdges(deluge_encoder_take_edges(1));
	tempo.applyEdges(deluge_encoder_take_edges(2));
	select.applyEdges(deluge_encoder_take_edges(3));
	mod1.applyEdges(deluge_encoder_take_edges(4));
	mod0.applyEdges(deluge_encoder_take_edges(5));

	// do not interpret encoders when stem export is underway
	if (stemExport.processStarted) {
		return false;
	}

	skipActioning |= isSDRoutineActive(); // if the "sd routine" is yielding then always defer actioning encoders
	bool anything = false;

	if (!skipActioning) {
		encodersWaitingForCardRoutineEnd = 0;
	}

#if SD_TEST_MODE_ENABLED
	if (!skipActioning && playbackHandler.isEitherClockActive()
	    && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

		if (getRandom255() < 96)
			nextSDTestDirection *= -1;
		getCurrentUI()->selectEncoderAction(nextSDTestDirection);

		int32_t random = getRandom255();

		timeNextSDTestAction = AudioEngine::audioSampleTimer + ((random) << 6); // * 44 / 13;
		anything = true;
	}
#endif

	for (int32_t e = 0; e < (int32_t)kNumFunctionEncoders; e++) {
		// 0=scrollY 1=scrollX 2=tempo 3=select
		DetentedEncoder* const funcPtrs[] = {&scrollY, &scrollX, &tempo, &select};
		bool isScrollY = (e == 0);
		if (!isScrollY) {

			// Basically disables all function encoders during SD routine
			if (skipActioning && currentUIMode != UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
				continue;
			}
		}

		if (encodersWaitingForCardRoutineEnd & (1 << e)) {
			continue;
		}

		auto& fe = *funcPtrs[e];
		if (fe.pending()) {
			anything = true;

			int32_t detentDelta = fe.take();

			// Handlers that take int8_t (tempo, select) get a saturating narrowing cast so that very fast
			// spinning cannot overflow the parameter type.  Horizontal/vertical actions take int32_t and
			// receive the full accumulated delta for natural acceleration.
			auto saturatedDelta = static_cast<int8_t>(std::clamp(detentDelta, (int32_t)-128, (int32_t)127));

			ActionResult result;

			switch (e) {

			case 1: // scrollX
				result = getCurrentUI()->horizontalEncoderAction(detentDelta);
				// Actually, after coding this up, I realise I actually have it above stopping the X encoder from even
				// getting here during the SD routine. Ok so we'll leave it that way, in addition to me having made all
				// the horizontalEncoderAction() calls SD-routine-safe
checkResult:
				if (result == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE) {
					encodersWaitingForCardRoutineEnd |= (1 << e);
					fe.restore(detentDelta); // Put it back for next time
				}
				break;

			case 0: // scrollY
				if (Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
					PadLEDs::changeDimmerInterval(detentDelta);
				}
				else {
					result = getCurrentUI()->verticalEncoderAction(detentDelta, skipActioning);
					goto checkResult;
				}
				break;

			case 2: // tempo
				if ((getCurrentUI() == &instrumentClipView
				     || (getCurrentUI() == &automationView && automationView.inNoteEditor()))
				    && runtimeFeatureSettings.get(RuntimeFeatureSettingType::Quantize)
				           == RuntimeFeatureStateToggle::On) {
					instrumentClipView.tempoEncoderAction(saturatedDelta,
					                                      Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                      Buttons::isShiftButtonPressed());
				}
				else {
					playbackHandler.tempoEncoderAction(saturatedDelta,
					                                   Buttons::isButtonPressed(deluge::hid::button::TEMPO_ENC),
					                                   Buttons::isShiftButtonPressed());
				}
				break;

			case 3: // select
				if (Buttons::isButtonPressed(deluge::hid::button::CLIP_VIEW)) {
					PadLEDs::changeRefreshTime(saturatedDelta);
				}
				else if (Buttons::isButtonPressed(deluge::hid::button::RECORD)) {
					if (currentSong) {
						currentSong->changeThresholdRecordingMode(saturatedDelta);
					}
				}
				else {
					getCurrentUI()->selectEncoderAction(saturatedDelta);
				}
				break;
			}
		}
	}

	if (!skipActioning || currentUIMode == UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED) {
		// Mod knobs
		for (int32_t e = 0; e < 2; e++) {
			// 0=mod0 (lower gold), 1=mod1 (upper gold)
			auto& encoder = modEncoderAt(e);

			int8_t offset = encoder.take();

			// If encoder turned...
			if (offset != 0) {
				anything = true;

				// Do it, only if
				if (offset + modEncoderInitialTurnDirection[e] != 0) {
					int8_t offset_accelerated = offset * encoder.calcNextKnobSpeed(offset);

					getCurrentUI()->modEncoderAction(e, offset_accelerated);

					modEncoderInitialTurnDirection[e] = 0;
				}

				// Otherwise, write this off as an accidental wiggle
				else {
					modEncoderInitialTurnDirection[e] = offset;
				}
			}
		}
	}

	return anything;
}

} // namespace deluge::hid::encoders
