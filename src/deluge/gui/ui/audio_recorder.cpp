/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/audio_recorder.h"
#include "OSLikeStuff/scheduler_api.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "util/d_string.h"
#include <algorithm>
#include <string.h>

AudioRecorder audioRecorder{};

extern "C" void routineForSD(void);

extern "C" {

#include "RZA1/spibsc/r_spibsc_flash_api.h"

void oledRoutine();
}

AudioRecorder::AudioRecorder() {
	recordingSource = AudioInputChannel::NONE;
	recorder = nullptr;
}

bool AudioRecorder::getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool AudioRecorder::opened() {
	updatedRecordingStatus = false;

	actionLogger.deleteAllLogs();

	// If we're already recording (probably the output) then no!
	if (recordingSource > AudioInputChannel::NONE) {
		return false;
	}

	// If recording for a Drum, set the name of the Drum
	if (getCurrentOutputType() == OutputType::KIT) {
		Kit* kit = getCurrentKit();
		SoundDrum* drum = (SoundDrum*)soundEditor.currentSound;
		String newName;

		Error error = newName.set("REC");
		if (error != Error::NONE) {
gotError:
			display->displayError(error);
			return false;
		}

		error = kit->makeDrumNameUnique(&newName, 1);
		if (error != Error::NONE) {
			goto gotError;
		}

		drum->drumName = newName.get();
	}

	PadLEDs::clearTickSquares(true);

	bool inStereo = (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn);
	int32_t newNumChannels = inStereo ? 2 : 1;
	bool success = setupRecordingToFile(inStereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT, newNumChannels,
	                                    AudioRecordingFolder::RECORD);
	if (success) {
		soundEditor.setupShortcutBlink(soundEditor.currentSourceIndex, 4, 0);
		soundEditor.blinkShortcut();

		indicator_leds::setLedState(IndicatorLED::SYNTH, !soundEditor.editingKit());
		indicator_leds::setLedState(IndicatorLED::KIT, soundEditor.editingKit());
		indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
		indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
		indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);
		indicator_leds::blinkLed(IndicatorLED::BACK);
		indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
		if (display->have7SEG()) {
			display->setNextTransitionDirection(0);
			display->setText("WAIT", false, 255, true);
		}
	}

	if (currentUIMode == UI_MODE_AUDITIONING) {
		instrumentClipView.cancelAllAuditioning();
	}

	return success;
}

void AudioRecorder::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// if not threshold recording, display standard UI and skip the threshold meter
	if (!isThresholdRecording()) {
		const char* current_status_text = updatedRecordingStatus ? "Recording" : "Waiting";
		canvas.drawStringCentred(current_status_text, 19, kTextBigSpacingX, kTextBigSizeY);
		return;
	}

	// top of the screen where we render "WAITING..." or "RECORDING"
	constexpr int32_t top_y = (OLED_MAIN_HEIGHT_PIXELS - 40) / 2 + 2;
	// x coordinate where level meter starts
	constexpr int32_t meter_start_x = 12;
	// x coordinate where level meter ends
	constexpr int32_t meter_end_x = OLED_MAIN_WIDTH_PIXELS - 14;
	// x coordinate where warning indicator (!) is rendered
	constexpr int32_t warning_start_x = 92;
	constexpr float fullScaleLevel = 21.48756f;
	constexpr float cautionLevel = fullScaleLevel - 8.8f / 4.0f;
	const uint32_t animationTime = static_cast<uint64_t>(AudioEngine::audioSampleTimer) * 1000 / kSampleRate;

	// lambda function to convert a level value to a position on the meter
	const auto levelPosition = [](float level) {
		if (level < cautionLevel) {
			return meter_start_x
			       + static_cast<int32_t>(std::clamp((level - 8.0f) / (cautionLevel - 8.0f), 0.0f, 1.0f)
			                              * (warning_start_x - meter_start_x));
		}
		return warning_start_x
		       + static_cast<int32_t>(std::clamp((level - cautionLevel) / (fullScaleLevel - cautionLevel), 0.0f, 1.0f)
		                              * (meter_end_x - warning_start_x));
	};

	// check if we're recording or waiting
	if (updatedRecordingStatus) {
		canvas.drawString("RECORDING", 12, top_y, kTextBigSpacingX, kTextBigSizeY);
		if ((animationTime / 480) % 2 == 0) {
			for (int32_t row = 0; row < 5; ++row) {
				const int32_t inset = (row == 0 || row == 4) ? 1 : 0;
				canvas.drawHorizontalLine(top_y + 4 + row, 5 + inset, 9 - inset);
			}
		}
	}
	else {
		canvas.drawStringCentred("WAITING...", top_y, kTextBigSpacingX, kTextBigSizeY);
	}

	// draw left and right channel labels (L and R)
	canvas.drawString("L", 6, top_y + 19, kTextSmallSpacingX, kTextSmallSizeY);
	canvas.drawString("R", 6, top_y + 29, kTextSmallSpacingX, kTextSmallSizeY);

	// ??? what is this
	canvas.drawPixel(8, top_y + 29);

	// draw clipping line
	for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
		canvas.drawPixel(warning_start_x, row);
	}

	// blink threshold line
	const uint32_t thresholdBlinkPhase = animationTime % 750;
	if (!updatedRecordingStatus && recorder->startValueThreshold > 0
	    && (thresholdBlinkPhase < 290 || thresholdBlinkPhase >= 570)) {
		int32_t thresholdPosition = levelPosition(recorder->startValueThreshold);
		for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
			canvas.drawPixel(thresholdPosition, row);
		}
	}

	// get current RMS levels for input source
	// if recording mono just take left channel, otherwise take both left and right channels
	const float levels[] = {recorder->approxRMSLevel.l, recorder->recordingNumChannels == 2
	                                                        ? recorder->approxRMSLevel.r
	                                                        : recorder->approxRMSLevel.l};

	// if input levels are clipping, then display warning indicator (!)
	if (std::max(levelPosition(levels[0]), levelPosition(levels[1])) > warning_start_x) {
		canvas.drawChar('!', 119, top_y + 20, 3, kTextBigSizeY);
		canvas.drawPixel(121, top_y + 28);
	}

	// render input level meters for each channel
	for (int32_t channel = 0; channel < 2; ++channel) {
		int32_t end = levelPosition(levels[channel]);
		for (int32_t column = meter_start_x + 1; column <= end; ++column) {
			for (int32_t row = 0; row < 5; ++row) {
				if ((updatedRecordingStatus && column < warning_start_x)
				    || (column + row) % 2 == (updatedRecordingStatus ? 0 : 1)) {
					canvas.drawPixel(column, top_y + 19 + channel * 10 + row);
				}
			}
		}
	}
}

bool AudioRecorder::isThresholdRecording() {
	// thresholdRecording clears as soon as the input triggers recording. Keep the
	// meter visible for the whole take using the configured threshold instead.
	return recorder && recorder->startValueThreshold > 0;
}

void AudioRecorder::graphicsRoutine() {
	// if we have an OLED display, are threshold recording and currently capturing data
	// then refresh OLED display to show the current input levels to be recorded
	if (display->haveOLED() && isThresholdRecording() && recorder->status == RecorderStatus::CAPTURING_DATA) {
		renderUIsForOled();
	}
}

bool AudioRecorder::setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels,
                                         AudioRecordingFolder folderID, bool writeLoopPoints, bool shouldNormalize) {

	// Already recording - don't orphan the running recorder by overwriting it
	if (recordingSource > AudioInputChannel::NONE) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E242");
#endif
		return false;
	}

	recorder = AudioEngine::getNewRecorder(newNumChannels, folderID, newMode, false, writeLoopPoints,
	                                       kInternalButtonPressLatency, false, nullptr, {false});
	if (!recorder) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	}

	recorder->allowFileAlterationAfter = true;
	recorder->allowNormalization = shouldNormalize;

	recordingSource = newMode; // This sets recording to begin happening even as the file is created, below

	return true;
}

bool AudioRecorder::beginOutputRecording(AudioRecordingFolder folder, AudioInputChannel channel, bool writeLoopPoints,
                                         bool shouldNormalize) {
	bool success = setupRecordingToFile(channel, 2, folder, writeLoopPoints, shouldNormalize);

	if (success) {
		indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
	}

	// Rohan: Not 100% sure if this will help. Leo was getting culled voices right on beginning resampling
	// via an audition pad. But I'd more expect it to happen after the first render-window, which this
	// won't help. Anyway, I suppose this can't do any harm here.
	AudioEngine::bypassCulling = true;

	return success;
}

void AudioRecorder::endRecordingSoon(int32_t buttonLatency) {

	// Make sure we don't call the same thing multiple times - I think there's a few scenarios where this could happen
	if (recorder && recorder->status == RecorderStatus::CAPTURING_DATA) {
		display->displayLoadingAnimationText("Working");
		recorder->endSyncedRecording(buttonLatency);
	}
}

void AudioRecorder::slowRoutine() {
	// finishRecording() frees the SampleRecorder, which discardRecorder() forbids doing from inside the SD card
	// routine - the recorder may be suspended part-way through its own cardRoutine(), and freeing it there leaves
	// that cardRoutine() running on freed memory (then freeing it a second time). As a scheduler task we're already
	// held off by our RESOURCE_SD | RESOURCE_SD_ROUTINE registration; this guard covers the one call site that
	// bypasses the scheduler, the legacy non-USE_TASK_MANAGER mainLoop.
	if (sdRoutineLock || currentlyAccessingCard) {
		return;
	}

	if (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		// setupRecordingToFile() always assigns recorder before recordingSource, so this can't happen - but don't let
		// it become a silent hang: StemExport's yield loop waits on recordingSource, which only finishRecording()
		// clears, so quietly skipping would wedge the export forever with nothing on screen.
		if (recorder == nullptr) {
			FREEZE_WITH_ERROR("E250");
			return;
		}
		if (recorder->status >= RecorderStatus::COMPLETE) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RecordingMode::NORMAL));
			finishRecording();
		}
	}
}

void AudioRecorder::process() {
	while (true) {
		// Sean: TO DO, confirm that this makes sense
#ifdef USE_TASK_MANAGER
		// Keep the normal audio, input, timer and display tasks running even before
		// the threshold triggers, when SD writes aren't yielding to the scheduler.
		yieldWithTimeout([]() { return false; }, 0.001);
#else
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

		// This loop bypasses the normal UI rendering task. Service redraws here
		// even while waiting for the threshold, when there are no SD writes to yield.
		doAnyPendingUIRendering();

		if (display->haveOLED()) {
			oledRoutine();
		}
		PIC::flush();

		readButtonsAndPads();

		AudioEngine::slowRoutine();
#endif

		// If recording has finished...
		if (recorder->status >= RecorderStatus::COMPLETE || recorder->hadCardError) {

			if (recorder->status == RecorderStatus::ABORTED || recorder->hadCardError) {}

			else {
				// We want to attach that Sample to a Source right away...
				soundEditor.currentSound->killAllVoices();
				soundEditor.currentSource->setOscType(OscType::SAMPLE);
				soundEditor.currentMultiRange->getAudioFileHolder()->filePath.set(&recorder->sample->filePath);
				soundEditor.currentMultiRange->getAudioFileHolder()->setAudioFile(
				    recorder->sample, soundEditor.currentSource->sampleControls.isCurrentlyReversed(), true);
			}
			finishRecording();

			close();
			return;
		}

		// Or if recording's ongoing...
		else {
			if (recorder->recordingClippedRecently) {
				recorder->recordingClippedRecently = false;

				if (!display->hasPopup()) {
					display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIPPING_OCCURRED));
				}
			}
			else if (!updatedRecordingStatus && (recorder->numSamplesCaptured > recorder->minThresholdMargin)) {
				// update recording status flag --> used to indicate that we've started recording
				// also used by the OLED UI to decide whether to render "WAITING" or "RECORDING"
				updatedRecordingStatus = true;
				if (display->have7SEG()) {
					display->setText("REC", false, 255, true);
				}
				else {
					renderUIsForOled();
				}
			}
		}
	}
}

// Returns error code
void AudioRecorder::finishRecording() {
	recorder->pointerHeldElsewhere = false;

	AudioEngine::discardRecorder(recorder);

	recorder = nullptr;
	recordingSource = AudioInputChannel::NONE;
	display->removeLoadingAnimation();
}

ActionResult AudioRecorder::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (!on) {
		// allow turning off fill mode if it was active while entering audio recorder
		if (b == SYNC_SCALING && currentSong->isFillModeActive()) {
			return getRootUI()->buttonAction(b, on, inCardRoutine);
		}
		return ActionResult::NOT_DEALT_WITH;
	}

	// We don't actually wrap up recording here, because this could be in fact called from the SD writing routines as
	// they wait - that'd be a tangle.
	if ((b == BACK) || (b == SELECT_ENC) || (b == RECORD)) {

		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		endRecordingSoon(kInternalButtonPressLatency);
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

bool AudioRecorder::isCurrentlyResampling() {
	return (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && recorder
	        && recorder->status == RecorderStatus::CAPTURING_DATA);
}
