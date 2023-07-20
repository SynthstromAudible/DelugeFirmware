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

#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "model/clip/instrument_clip.h"
#include "model/clip/instrument_clip_minder.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/ui/browser/sample_browser.h"
#include "processing/sound/sound_drum.h"
#include "gui/ui/audio_recorder.h"
#include "storage/storage_manager.h"
#include "io/debug/print.h"
#include "dsp/stereo_sample.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"
#include "processing/source.h"
#include "hid/matrix/matrix_driver.h"
#include "model/drum/kit.h"
#include "model/song/song.h"
#include "gui/ui_timer_manager.h"
#include "model/action/action_logger.h"
#include <string.h>
#include "memory/general_memory_allocator.h"
#include "storage/multi_range/multisample_range.h"
#include <new>
#include "model/sample/sample_recorder.h"
#include "hid/led/indicator_leds.h"
#include "definitions.h"
#include "hid/led/pad_leds.h"
#include "extern.h"
#include "hid/display/oled.h"
#include "playback/playback_handler.h"

AudioRecorder audioRecorder{};

extern "C" void routineForSD(void);

extern "C" {
#include "fatfs/ff.h"
#include "fatfs/diskio.h"

#include "drivers/uart/uart.h"

#include "RZA1/spibsc/spibsc.h"
#include "RZA1/spibsc/r_spibsc_ioset_api.h"
#include "RZA1/spibsc/r_spibsc_flash_api.h"

void oledRoutine();
}

// We keep a separate FIL object here so we can be recording to a file at the same time as another file is open for reading.
// It no longer needs to be in this struct
struct RecorderFileSystemStuff recorderFileSystemStuff;

AudioRecorder::AudioRecorder() {
	recordingSource = AUDIO_INPUT_CHANNEL_NONE;
	recorder = NULL;
}

bool AudioRecorder::getGreyoutRowsAndCols(uint32_t* cols, uint32_t* rows) {
	*cols = 0xFFFFFFFF;
	return true;
}

bool AudioRecorder::opened() {

	actionLogger.deleteAllLogs();

	// If we're already recording (probably the output) then no!
	if (recordingSource) {
		return false;
	}

	// If recording for a Drum, set the name of the Drum
	if (currentSong->currentClip->output->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)currentSong->currentClip->output;
		SoundDrum* drum = (SoundDrum*)soundEditor.currentSound;
		String newName;

		int error = newName.set("REC");
		if (error) {
gotError:
			numericDriver.displayError(error);
			return false;
		}

		error = kit->makeDrumNameUnique(&newName, 1);
		if (error) {
			goto gotError;
		}

		drum->name.set(&newName);
	}

	PadLEDs::clearTickSquares(true);

	bool inStereo = (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn);
	int newNumChannels = inStereo ? 2 : 1;
	bool success = setupRecordingToFile(inStereo ? AUDIO_INPUT_CHANNEL_STEREO : AUDIO_INPUT_CHANNEL_LEFT,
	                                    newNumChannels, AUDIO_RECORDING_FOLDER_RECORD);
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
#if !HAVE_OLED
		numericDriver.setNextTransitionDirection(0);
		numericDriver.setText("REC", false, 255, true);
#endif
	}

	if (currentUIMode == UI_MODE_AUDITIONING) {
		instrumentClipView.cancelAllAuditioning();
	}

	return success;
}

#if HAVE_OLED
void AudioRecorder::renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
	OLED::drawStringCentred("Recording", 15, image[0], OLED_MAIN_WIDTH_PIXELS, TEXT_BIG_SPACING_X, TEXT_BIG_SIZE_Y);
}
#endif

bool AudioRecorder::setupRecordingToFile(int newMode, int newNumChannels, int folderID) {

	if (ALPHA_OR_BETA_VERSION && recordingSource) {
		numericDriver.freezeWithError("E242");
	}

	recorder = AudioEngine::getNewRecorder(newNumChannels, folderID, newMode, INTERNAL_BUTTON_PRESS_LATENCY);
	if (!recorder) {
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return false;
	}

	recorder->allowFileAlterationAfter = true;

	recordingSource = newMode; // This sets recording to begin happening even as the file is created, below

	return true;
}

bool AudioRecorder::beginOutputRecording() {
	bool success = setupRecordingToFile(AUDIO_INPUT_CHANNEL_OUTPUT, 2, AUDIO_RECORDING_FOLDER_RESAMPLE);

	if (success) {
		indicator_leds::blinkLed(IndicatorLED::RECORD, 255, 1);
	}

	AudioEngine::bypassCulling =
	    true; // Not 100% sure if this will help. Leo was getting culled voices right on beginning resampling
	          // via an audition pad. But I'd more expect it to happen after the first render-window, which this
	          // won't help. Anyway, I suppose this can't do any harm here.

	return success;
}

void AudioRecorder::endRecordingSoon(int buttonLatency) {

	if (recorder
	    && recorder->status
	           == RECORDER_STATUS_CAPTURING_DATA) { // Make sure we don't call the same thing multiple times - I think there's a few scenarios where this could happen
#if HAVE_OLED
		OLED::displayWorkingAnimation("Working");
#else
		numericDriver.displayLoadingAnimation();
#endif
		recorder->endSyncedRecording(buttonLatency);
	}
}

void AudioRecorder::slowRoutine() {
	if (recordingSource == AUDIO_INPUT_CHANNEL_OUTPUT) {
		if (recorder->status >= RECORDER_STATUS_COMPLETE) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RECORDING_NORMAL));
			finishRecording();
		}
	}
}

void AudioRecorder::process() {
	while (true) {

		AudioEngine::routineWithClusterLoading();

		uiTimerManager.routine();

#if HAVE_OLED
		oledRoutine();
#endif
		uartFlushIfNotSending(UART_ITEM_PIC);

		readButtonsAndPads();

		AudioEngine::slowRoutine();

		// If recording has finished...
		if (recorder->status >= RECORDER_STATUS_COMPLETE || recorder->hadCardError) {

			if (recorder->status == RECORDER_STATUS_ABORTED || recorder->hadCardError) {}

			else {
				// We want to attach that Sample to a Source right away...
				soundEditor.currentSound->unassignAllVoices();
				soundEditor.currentSource->setOscType(OSC_TYPE_SAMPLE);
				soundEditor.currentMultiRange->getAudioFileHolder()->filePath.set(&recorder->sample->filePath);
				soundEditor.currentMultiRange->getAudioFileHolder()->setAudioFile(
				    recorder->sample, soundEditor.currentSource->sampleControls.reversed, true);
			}
			finishRecording();

			close();
			return;
		}

		// Or if recording's ongoing...
		else {

			if (recorder->recordingClippedRecently) {
				recorder->recordingClippedRecently = false;

				if (!numericDriver.popupActive) {
					numericDriver.displayPopup(HAVE_OLED ? "Clipping occurred" : "CLIP");
				}
			}
		}
	}
}

// Returns error code
void AudioRecorder::finishRecording() {
	recorder->pointerHeldElsewhere = false;

	AudioEngine::discardRecorder(recorder);

	recorder = NULL;
	recordingSource = 0;
#if HAVE_OLED
	OLED::removeWorkingAnimation();
#else
	numericDriver.removeTopLayer();
#endif
}

int AudioRecorder::buttonAction(hid::Button b, bool on, bool inCardRoutine) {
	using namespace hid::button;

	if (!on) {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	// We don't actually wrap up recording here, because this could be in fact called from the SD writing routines as they wait - that'd be a tangle.
	if ((b == BACK) || (b == SELECT_ENC) || (b == RECORD)) {

		if (inCardRoutine) {
			return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		endRecordingSoon(INTERNAL_BUTTON_PRESS_LATENCY);
	}
	else {
		return ACTION_RESULT_NOT_DEALT_WITH;
	}

	return ACTION_RESULT_DEALT_WITH;
}

bool AudioRecorder::isCurrentlyResampling() {
	return (recordingSource >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION && recorder
	        && recorder->status == RECORDER_STATUS_CAPTURING_DATA);
}
