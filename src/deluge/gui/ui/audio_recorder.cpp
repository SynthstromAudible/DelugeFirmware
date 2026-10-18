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

PLACE_SDRAM_BSS AudioRecorder audioRecorder{};

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
		recorder->monitorInputLevels = display->haveOLED() && isThresholdRecording();
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

namespace {
// x coordinate where level meter starts
constexpr int32_t meter_start_x = 12;
// x coordinate where level meter ends
constexpr int32_t meter_end_x = OLED_MAIN_WIDTH_PIXELS - 14;
// x coordinate of the dotted caution line; levels past it trigger the warning indicator (!)
constexpr int32_t warning_start_x = 92;
// approxRMSLevel is the natural log of the mean absolute q31 sample value, so ln(2^31) is full scale
constexpr float fullScaleLevel = 21.48756f;
// start of the caution zone, roughly 19dB below full scale
constexpr float cautionLevel = fullScaleLevel - 8.8f / 4.0f;

// convert a level to an x position on the meter: levels up to cautionLevel fill the space left of the
// caution line, and levels from cautionLevel to full scale fill the space to its right
int32_t levelPosition(float level) {
	if (level < cautionLevel) {
		return meter_start_x
		       + static_cast<int32_t>(std::clamp((level - 8.0f) / (cautionLevel - 8.0f), 0.0f, 1.0f)
		                              * (warning_start_x - meter_start_x));
	}
	return warning_start_x
	       + static_cast<int32_t>(std::clamp((level - cautionLevel) / (fullScaleLevel - cautionLevel), 0.0f, 1.0f)
	                              * (meter_end_x - warning_start_x));
}
} // namespace

AudioRecorder::ThresholdMeterState AudioRecorder::getThresholdMeterState() {
	const uint32_t animationTime = static_cast<uint64_t>(AudioEngine::audioSampleTimer) * 1000 / kSampleRate;

	// get current approximate RMS levels for the input source
	// when recording mono, show the left channel on both meters
	const auto level = recorder->approxRMSLevel;
	const float right = recorder->recordingNumChannels == 2 ? level.r : level.l;

	ThresholdMeterState state;
	state.levelEndX = {levelPosition(level.l), levelPosition(right)};
	state.recording = updatedRecordingStatus;
	if (state.recording) {
		state.blinkOn = (animationTime / 480) % 2 == 0;
	}
	else {
		const uint32_t thresholdBlinkPhase = animationTime % 750;
		state.blinkOn = thresholdBlinkPhase < 290 || thresholdBlinkPhase >= 570;
	}
	return state;
}

void AudioRecorder::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	// if not threshold recording, display the standard UI without the level meter
	if (!isThresholdRecording()) {
		const char* current_status_text = updatedRecordingStatus ? "Recording" : "Waiting";
		canvas.drawStringCentred(current_status_text, 19, kTextBigSpacingX, kTextBigSizeY);
		return;
	}

	// y coordinate of the "WAITING..." / "RECORDING" text; the rest of the layout is positioned relative to it
	constexpr int32_t top_y = (OLED_MAIN_HEIGHT_PIXELS - 40) / 2 + 2;

	// draw from a snapshot of the state and remember it, so graphicsRoutine() can tell when it changes
	const ThresholdMeterState state = getThresholdMeterState();
	lastRenderedThresholdMeterState = state;

	// draw the recording status; while recording, also blink a record dot to the left of the text
	if (state.recording) {
		canvas.drawString("RECORDING", 12, top_y, kTextBigSpacingX, kTextBigSizeY);
		if (state.blinkOn) {
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

	// fill in the top-right corner of the 5px "R" glyph (which is rounded) so it matches the squared-off mockup
	// canvas.drawPixel(8, top_y + 29);

	// draw dotted caution line
	for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
		canvas.drawPixel(warning_start_x, row);
	}

	// while waiting, blink a dotted line at the threshold level
	if (!state.recording && state.blinkOn) {
		int32_t thresholdPosition = levelPosition(recorder->startValueThreshold);
		for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
			canvas.drawPixel(thresholdPosition, row);
		}
	}

	// if either channel is past the caution line, display the warning indicator (!)
	if (std::max(state.levelEndX[0], state.levelEndX[1]) > warning_start_x) {
		canvas.drawChar('!', 119, top_y + 20, 3, kTextBigSizeY);
		// fill the gap at the bottom of the "!" stem's right column, which the 13px glyph leaves empty
		canvas.drawPixel(121, top_y + 28);
	}

	// render input level meters for each channel: solid up to the caution line while recording,
	// otherwise checkered
	for (int32_t channel = 0; channel < 2; ++channel) {
		int32_t end = state.levelEndX[channel];
		for (int32_t column = meter_start_x + 1; column <= end; ++column) {
			for (int32_t row = 0; row < 5; ++row) {
				if ((state.recording && column < warning_start_x) || (column + row) % 2 == (state.recording ? 0 : 1)) {
					canvas.drawPixel(column, top_y + 19 + channel * 10 + row);
				}
			}
		}
	}
}

bool AudioRecorder::recorderNeedsAttention() {
	return recorder->status >= RecorderStatus::COMPLETE
	       || recorder->hadCardError
	       // only wake for clipping once any popup has gone, so sustained clipping doesn't keep waking us
	       || (recorder->recordingClippedRecently && !display->hasPopup())
	       || (!updatedRecordingStatus && recorder->numSamplesCaptured > recorder->minThresholdMargin);
}

bool AudioRecorder::isThresholdRecording() {
	// thresholdRecording clears as soon as the input triggers recording. Keep the
	// meter visible for the whole take using the configured threshold instead.
	return recorder && recorder->startValueThreshold > 0;
}

void AudioRecorder::graphicsRoutine() {
	// while threshold recording on OLED, redraw only when the meter level or blink state has changed
	if (display->haveOLED() && isThresholdRecording() && recorder->status == RecorderStatus::CAPTURING_DATA
	    && getThresholdMeterState() != lastRenderedThresholdMeterState) {
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
		// Let the scheduler run its other tasks (audio, input, timers, UI rendering) until there's something here to
		// act on. Before the threshold triggers nothing is written to the SD card, so nothing else would yield to the
		// scheduler, and openUI()'s pending OLED render (e.g. "Waiting") would never be flushed (#5054).
		// This must be one long yield, not short ones in a loop: every yield pushes the task we were called from (the
		// buttons task) back by its back-off time, so short repeated yields would stop Record / Back / Select from
		// ever being read.
		yield([]() { return audioRecorder.recorderNeedsAttention(); });

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
			// Checked independently of clipping so that sustained clipping can't hold this off, which would also keep
			// recorderNeedsAttention() true and turn the yield above into a busy loop
			if (!updatedRecordingStatus && (recorder->numSamplesCaptured > recorder->minThresholdMargin)) {
				// mark that recording has started; the OLED UI uses this to choose "WAITING..." or "RECORDING".
				// Set it before requesting the redraw so renderOLED() sees the new state.
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
