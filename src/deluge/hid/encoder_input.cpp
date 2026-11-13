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
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/screensaver.h"
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

static uint32_t timeNextSDTestAction = 0;
static int32_t nextSDTestDirection = 1;

/// Queue the gold encoder's runs the board has decoded at \p index, each at the time it was decoded.
static void takeTimedEdges(ContinuousEncoder& encoder, uint8_t index) {
	DelugeEncoderEdges runs[DELUGE_ENCODER_EDGE_RUNS];
	const uint8_t n = deluge_encoder_take_timed_quadrature_edges(index, runs, DELUGE_ENCODER_EDGE_RUNS);
	for (uint8_t i = 0; i < n; i++) {
		encoder.applyEdges(runs[i].edges, static_cast<double>(runs[i].time_us) / 1'000'000.0);
	}
}

void interpretEncodersTask() {
	interpretEncoders();
}

bool interpretEncoders() {
	// Fold the quadrature edges the board has decoded since last time into each encoder.
	// Detent reduction (DetentedEncoder) and gold-knob velocity (ContinuousEncoder) happen in
	// applyEdges; this runs in task context now, not the ISR. The gold knobs take their edges with the time
	// each run was decoded, which their acceleration is computed on. The board's edge index order matches
	// EncoderName: 0=scrollY 1=scrollX 2=tempo 3=select 4=mod1 5=mod0.
	functionEncoderAt(0).applyEdges(deluge_encoder_take_quadrature_edges(0)); // scrollY
	functionEncoderAt(1).applyEdges(deluge_encoder_take_quadrature_edges(1)); // scrollX
	functionEncoderAt(2).applyEdges(deluge_encoder_take_quadrature_edges(2)); // tempo
	functionEncoderAt(3).applyEdges(deluge_encoder_take_quadrature_edges(3)); // select
	takeTimedEdges(modEncoderAt(1), 4);                                       // mod1
	takeTimedEdges(modEncoderAt(0), 5);                                       // mod0

	// do not interpret encoders when stem export is underway
	if (stemExport.processStarted) {
		return false;
	}

	bool anything = false;

#if SD_TEST_MODE_ENABLED
	if (playbackHandler.isEitherClockActive() && (int32_t)(AudioEngine::audioSampleTimer - timeNextSDTestAction) >= 0) {

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

		auto& fe = *funcPtrs[e];
		if (fe.pending()) {
			anything = true;

			int32_t detentDelta = fe.take();

			// Handlers that take int8_t (tempo, select) get a saturating narrowing cast so that very fast
			// spinning cannot overflow the parameter type.  Horizontal/vertical actions take int32_t and
			// receive the full accumulated delta for natural acceleration.
			auto saturatedDelta = static_cast<int8_t>(std::clamp(detentDelta, (int32_t)-128, (int32_t)127));

			switch (e) {

			case 1: // scrollX
				getCurrentUI()->horizontalEncoderAction(detentDelta);
				break;

			case 0: // scrollY
				if (Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(deluge::hid::button::LEARN)) {
					PadLEDs::changeDimmerInterval(detentDelta);
				}
				else {
					getCurrentUI()->verticalEncoderAction(detentDelta);
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
				// With every UI taken down by a load, the select encoder sets the armed swap's loop count.
				else if (uiStackEmpty()) {
					loadSongUI.armedSelectEncoderAction(saturatedDelta);
				}
				else {
					getCurrentUI()->selectEncoderAction(saturatedDelta);
				}
				break;
			}
		}
	}

	// Mod knobs: each run the board decoded is one of `main`'s turns, on its own time.
	for (int32_t e = 0; e < 2; e++) {
		// 0=mod0 (lower gold), 1=mod1 (upper gold)
		if (modEncoderAt(e).actionTurns([e](int8_t offset) { getCurrentUI()->modEncoderAction(e, offset); })) {
			anything = true;
		}
	}

	if (anything) {
		deluge::hid::display::Screensaver::noteActivity();
	}

	return anything;
}

} // namespace deluge::hid::encoders
