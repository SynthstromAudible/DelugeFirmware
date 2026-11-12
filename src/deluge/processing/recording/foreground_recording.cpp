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

#include "processing/recording/foreground_recording.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/sample/sample_recorder.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "sync/sd_access.h"

namespace deluge::recording {

namespace {

/// Whether the input take has no screen: slowRoutine() frees its recorder once it has settled.
bool inputTakeOrphaned = false;

bool setupRecordingToFile(AudioInputChannel newMode, int32_t newNumChannels, AudioRecordingFolder folderID,
                          bool writeLoopPoints = false, bool shouldNormalize = true) {

	// Already recording - don't orphan the running recorder by overwriting it
	if (AudioEngine::foregroundRecordingSource() > AudioInputChannel::NONE) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E242");
#endif
		return false;
	}

	SampleRecorder* recorder = AudioEngine::getNewRecorder(newNumChannels, folderID, newMode, false, writeLoopPoints,
	                                                       kInternalButtonPressLatency, false, nullptr, {false});
	if (!recorder) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	}

	recorder->allowFileAlterationAfter = true;
	recorder->allowNormalization = shouldNormalize;

	// This sets recording to begin happening even as the file is created, below
	AudioEngine::setForegroundRecording(recorder, newMode);

	return true;
}

/// Unpublishes and frees the foreground recorder.
void releaseRecorder() {
	SampleRecorder* recorder = AudioEngine::foregroundRecorder();
	recorder->pointerHeldElsewhere = false;
	// Unpublished first: the audio routine dereferences the pointer while the channel names a live capture.
	AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
	AudioEngine::discardRecorder(recorder);
}

/// Frees the foreground recorder and takes down the "Working" its end showed.
void finishRecording() {
	releaseRecorder();
	display->removeLoadingAnimation();
}

} // namespace

bool beginOutputRecording(AudioRecordingFolder folder, AudioInputChannel channel, bool writeLoopPoints,
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

void endRecordingSoon(int32_t buttonLatency) {
	SampleRecorder* recorder = AudioEngine::foregroundRecorder();

	// Make sure we don't call the same thing multiple times - I think there's a few scenarios where this could happen
	if (recorder && recorder->status == RecorderStatus::CAPTURING_DATA) {
		display->displayLoadingAnimationText("Working");
		recorder->endSyncedRecording(buttonLatency);
	}
}

void slowRoutine() {
	// finishRecording() frees the SampleRecorder, which discardRecorder() forbids doing from inside the SD card
	// routine - the recorder may be suspended part-way through its own cardRoutine(), and freeing it there leaves
	// that cardRoutine() running on freed memory (then freeing it a second time). The Embassy scheduler runs
	// cardRoutine() inline and to completion, so it is never suspended part-way; this guard covers the rest.
	if (deluge::sync::sd_busy()) {
		return;
	}

	if (AudioEngine::foregroundRecordingSource() >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION) {
		SampleRecorder* recorder = AudioEngine::foregroundRecorder();
		// setForegroundRecording() always publishes the recorder before the channel, so this can't happen - but don't
		// let it become a silent hang: StemExport's yield loop waits on the channel, which only finishRecording()
		// clears, so quietly skipping would wedge the export forever with nothing on screen.
		if (recorder == nullptr) {
			FREEZE_WITH_ERROR("E250");
			return;
		}
		// Settled, not merely terminal: an aborted recorder may still be closing its file.
		if (recorder->settled()) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RecordingMode::NORMAL));
			finishRecording();
		}
	}
	else if (inputTakeOrphaned) {
		// An input take whose screen went under it: freed once settled, attached to nothing.
		SampleRecorder* recorder = AudioEngine::foregroundRecorder();
		if (!inputTakeInProgress() || recorder == nullptr) {
			inputTakeOrphaned = false;
		}
		else if (recorder->settled()) {
			inputTakeOrphaned = false;
			// No "Working" was shown for it, so any animation up is another's: the song load that took its screen.
			releaseRecorder();
		}
	}
}

bool isCurrentlyResampling() {
	return (AudioEngine::foregroundRecordingSource() >= AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION
	        && AudioEngine::foregroundRecorder()
	        && AudioEngine::foregroundRecorder()->status == RecorderStatus::CAPTURING_DATA);
}

bool inputTakeInProgress() {
	return AudioEngine::foregroundRecorder() != nullptr
	       && AudioEngine::foregroundRecordingSource() > AudioInputChannel::NONE
	       && AudioEngine::foregroundRecordingSource() < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION;
}

void orphanInputTake() {
	SampleRecorder* recorder = AudioEngine::foregroundRecorder();
	if (!inputTakeInProgress() || recorder == nullptr) {
		return;
	}
	// Nothing is drawn for the take: the screen that showed it is gone.
	if (recorder->status == RecorderStatus::CAPTURING_DATA) {
		recorder->endSyncedRecording(kInternalButtonPressLatency);
	}
	inputTakeOrphaned = true;
}

bool beginInputRecording(AudioInputChannel channel, int32_t numChannels) {
	return setupRecordingToFile(channel, numChannels, AudioRecordingFolder::RECORD);
}

void finishInputRecording() {
	finishRecording();
}

} // namespace deluge::recording
