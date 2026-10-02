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
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_pattern_ui.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/reversion.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/randomize_drums.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "processing/stem_export/stem_export.h"
#include "runtime/app_loop.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/storage_manager.h"
#include "sync/sd_access.h"
#include "util/c_string.h"
#include <iterator>
#include <string.h>

AudioRecorder audioRecorder{};

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

	// Refused, like the browsers refuse their input, while something could swap out what the take would attach to.
	// Checked before the undo log is wiped, so a refusal leaves the user's history intact.
	if (anOutputMayBeReplaced()) {
		display->displayPopup("Loading");
		return false;
	}

	actionLogger.deleteAllLogs();

	// If we're already recording (probably the output) then no!
	if (recordingSource > AudioInputChannel::NONE) {
		return false;
	}

	// If recording for a Drum, set the name of the Drum
	if (getCurrentOutputType() == OutputType::KIT) {
		Kit* kit = getCurrentKit();
		SoundDrum* drum = (SoundDrum*)soundEditor.currentSound;
		std::string newName;

		newName = "REC";

		Error error = kit->makeDrumNameUnique(&newName, 1);
		if (error != Error::NONE) {
			display->displayError(error);
			return false;
		}

		drum->drumName = newName.c_str();
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

		Clip* clip = getCurrentClip();
		target_ = {
		    .clip = clip,
		    .output = clip != nullptr ? clip->output : nullptr,
		    .sound = soundEditor.currentSound,
		    .range = soundEditor.currentMultiRange,
		    .sourceIndex = soundEditor.currentSourceIndex,
		};
		deluge::runtime::appLoop.request(jobId);
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
	// that cardRoutine() running on freed memory (then freeing it a second time). The Embassy scheduler runs
	// cardRoutine() inline and to completion, so it is never suspended part-way; this guard covers the rest.
	if (deluge::sync::sd_busy()) {
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
		// Settled, not merely terminal: an aborted recorder may still be closing its file.
		if (recorder->settled()) {
			indicator_leds::setLedState(IndicatorLED::RECORD, (playbackHandler.recording == RecordingMode::NORMAL));
			finishRecording();
		}
	}
}

bool AudioRecorder::anOutputMayBeReplaced() {
	using namespace deluge::gui;
	return instrumentLoad.inFlight() || loadPatternUI.loadInFlight() || reversion.inFlight()
	       || loadSongUI.isLoadInFlight() || randomizeDrums.inFlight() || sampleClaim.inFlight()
	       || folderImport.inFlight();
}

bool AudioRecorder::targetStillHeld() const {
	const TakeTarget& t = target_;
	if (currentSong == nullptr || t.clip == nullptr || t.output == nullptr || t.sound == nullptr
	    || t.range == nullptr) {
		return false;
	}
	if (soundEditor.currentSound != t.sound || soundEditor.currentMultiRange != t.range
	    || soundEditor.currentSourceIndex != t.sourceIndex) {
		return false;
	}
	// Addresses only until each is found among the song's own: the clip, then its output.
	if (currentSong->sessionClips.getIndexForClip(t.clip) < 0
	    && currentSong->arrangementOnlyClips.getIndexForClip(t.clip) < 0) {
		return false;
	}
	if (t.clip->output != t.output) {
		return false;
	}
	bool outputInSong = false;
	for (Output* output = currentSong->firstOutput; output != nullptr; output = output->next) {
		if (output == t.output) {
			outputInSong = true;
			break;
		}
	}
	if (!outputInSong) {
		return false;
	}

	bool soundHeld = false;
	if (t.output->type == OutputType::SYNTH) {
		soundHeld = static_cast<Sound*>(static_cast<SoundInstrument*>(t.output)) == t.sound;
	}
	else if (t.output->type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit*>(t.output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND && static_cast<Sound*>(static_cast<SoundDrum*>(drum)) == t.sound) {
				soundHeld = true;
				break;
			}
		}
	}
	if (!soundHeld) {
		return false;
	}

	Source& source = t.sound->sources[t.sourceIndex];
	for (int32_t i = 0; i < std::ssize(source.ranges); i++) {
		if (source.ranges.getElement(i) == t.range) {
			return true;
		}
	}
	return false;
}

bool AudioRecorder::inputTakeInProgress() const {
	return recorder != nullptr && recordingSource > AudioInputChannel::NONE
	       && recordingSource < AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION;
}

void AudioRecorder::tick() {
	if (!inputTakeInProgress()) {
		return;
	}

	// A card error ends the take, whichever UI is on top. Stop capturing, then wait for the recorder to close its
	// file and for the driver to answer anything it was given: freeing it any earlier frees memory the driver may
	// still be writing from.
	if (recorder->hadCardError) {
		endRecordingSoon(kInternalButtonPressLatency);
	}

	if (getCurrentUI() == this) {
		if (recorder->settled()) {
			finishInputTake();
			return;
		}

		if (recorder->recordingClippedRecently) {
			recorder->recordingClippedRecently = false;

			if (!display->hasPopup()) {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIPPING_OCCURRED));
			}
		}
		else if (!updatedRecordingStatus && (recorder->numSamplesCaptured > recorder->minThresholdMargin)) {
			deluge::hid::display::OLED::clearMainImage();
			deluge::hid::display::OLED::main.drawStringCentred("Recording", 19, kTextBigSpacingX, kTextBigSizeY);
			deluge::hid::display::OLED::sendMainImage();
			updatedRecordingStatus = true;
		}
	}
	else if (!isUIOpen(this)) {
		// The recorder UI was closed under the take, so the sound editor's source may no longer be the one it was
		// recording for: end the take and keep it on the card, attached to nothing.
		endRecordingSoon(kInternalButtonPressLatency);
		if (recorder->settled()) {
			finishRecording();
			target_ = {};
			return;
		}
	}

	deluge::runtime::appLoop.requestAt(jobId, deluge::runtime::nowUs() + kTickUs);
}

void AudioRecorder::finishInputTake() {
	// A take whose sound was replaced or freed under it stays on the card, attached to nothing.
	if (recorder->status != RecorderStatus::ABORTED && !recorder->hadCardError && targetStillHeld()) {
		// Attach the Sample to the Source right away.
		soundEditor.currentSound->killAllVoices();
		soundEditor.currentSource->setOscType(OscType::SAMPLE);
		soundEditor.currentMultiRange->getAudioFileHolder()->filePath = recorder->sample->filePath;
		soundEditor.currentMultiRange->getAudioFileHolder()->setAudioFile(
		    recorder->sample, soundEditor.currentSource->sampleControls.isCurrentlyReversed(), true);
	}
	finishRecording();
	target_ = {};

	close();
}

// Returns error code
void AudioRecorder::finishRecording() {
	recorder->pointerHeldElsewhere = false;

	AudioEngine::discardRecorder(recorder);

	recorder = nullptr;
	recordingSource = AudioInputChannel::NONE;
	display->removeLoadingAnimation();
}

ActionResult AudioRecorder::buttonAction(deluge::hid::Button b, bool on) {
	using namespace deluge::hid::button;

	if (!on) {
		// allow turning off fill mode if it was active while entering audio recorder
		if (b == SYNC_SCALING && currentSong->isFillModeActive()) {
			return getRootUI()->buttonAction(b, on);
		}
		return ActionResult::NOT_DEALT_WITH;
	}

	// We don't actually wrap up recording here, because this could be in fact called from the SD writing routines as
	// they wait - that'd be a tangle.
	if ((b == BACK) || (b == SELECT_ENC) || (b == RECORD)) {

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
