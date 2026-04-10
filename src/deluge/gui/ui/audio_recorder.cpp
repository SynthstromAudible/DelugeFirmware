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

		drum->name.set(&newName);
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
	const char* current_status_text = updatedRecordingStatus ? "Recording" : "Waiting";
	canvas.drawStringCentred(current_status_text, 19, kTextBigSpacingX, kTextBigSizeY);
}

bool AudioRecorder::setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels,
                                         AudioRecordingFolder folderID, bool writeLoopPoints, bool shouldNormalize) {

	if (ALPHA_OR_BETA_VERSION && recordingSource > AudioInputChannel::NONE) {
		FREEZE_WITH_ERROR("E242");
	}

	recorder = AudioEngine::getNewRecorder(newNumChannels, folderID, newMode, false, writeLoopPoints,
	                                       kInternalButtonPressLatency, false, nullptr);
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
	if (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		if (recorder->status >= RecorderStatus::COMPLETE) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RecordingMode::NORMAL));
			finishRecording();
		}
	}
}

void AudioRecorder::process() {
	while (true) {
		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

		if (display->haveOLED()) {
			oledRoutine();
		}
		PIC::flush();

		readButtonsAndPads();

		AudioEngine::slowRoutine();

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
			else if (!updatedRecordingStatus && recorder->numSamplesCaptured) {
				if (display->have7SEG()) {
					display->setText("REC", false, 255, true);
				}
				else {
					deluge::hid::display::OLED::clearMainImage();
					deluge::hid::display::OLED::main.drawStringCentred("Recording", 19, kTextBigSpacingX,
					                                                   kTextBigSizeY);
					deluge::hid::display::OLED::sendMainImage();
				}
				updatedRecordingStatus = true;
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
