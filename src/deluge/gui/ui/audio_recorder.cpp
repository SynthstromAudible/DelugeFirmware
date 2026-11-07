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
#include "gui/menu_item/setting_edit.h"
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
#include <algorithm>
#include <iterator>
#include <string.h>

PLACE_SDRAM_BSS AudioRecorder audioRecorder{};

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

		deluge::edits::laneSettingFor(*drum, "name",
		                              deluge::serialization::Value::makeScalar(std::string{newName.c_str()}));
	}

	PadLEDs::clearTickSquares(true);

	bool inStereo = (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn);
	int32_t newNumChannels = inStereo ? 2 : 1;
	bool success = setupRecordingToFile(inStereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT, newNumChannels,
	                                    AudioRecordingFolder::RECORD);
	if (success) {
		recorder->monitorInputLevels.store(isThresholdRecording(), std::memory_order_relaxed);
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

namespace {
/// Where the level meter starts, x.
constexpr int32_t meter_start_x = 12;
/// Where the level meter ends, x.
constexpr int32_t meter_end_x = OLED_MAIN_WIDTH_PIXELS - 14;
/// The dotted caution line's x; a level past it shows the warning (!).
constexpr int32_t warning_start_x = 92;
/// Full scale: a level is the natural log of the mean absolute q31 sample, so ln(2^31).
constexpr float fullScaleLevel = 21.48756f;
/// Where the caution zone starts, about 19 dB below full scale.
constexpr float cautionLevel = fullScaleLevel - 8.8f / 4.0f;

/// A level's x on the meter: levels up to cautionLevel fill the space left of the caution line, and levels from
/// there to full scale the space right of it.
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

	// A mono take shows its one channel on both meters
	const deluge::dsp::fp::StereoLevel level = recorder->getInputLevels();
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
	// Without a threshold, the status alone
	if (!isThresholdRecording()) {
		const char* current_status_text = updatedRecordingStatus ? "Recording" : "Waiting";
		canvas.drawStringCentred(current_status_text, 19, kTextBigSpacingX, kTextBigSizeY);
		return;
	}

	// The status line's y; the rest of the layout sits relative to it
	constexpr int32_t top_y = (OLED_MAIN_HEIGHT_PIXELS - 40) / 2 + 2;

	// Drawn from a snapshot that graphicsRoutine() compares the next one against
	const ThresholdMeterState state = getThresholdMeterState();
	lastRenderedThresholdMeterState = state;

	// The status; while recording, a record dot blinks left of it
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

	// The channel labels
	canvas.drawString("L", 6, top_y + 19, kTextSmallSpacingX, kTextSmallSizeY);
	canvas.drawString("R", 6, top_y + 29, kTextSmallSpacingX, kTextSmallSizeY);

	// The dotted caution line
	for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
		canvas.drawPixel(warning_start_x, row);
	}

	// While waiting, a dotted line blinks at the threshold
	if (!state.recording && state.blinkOn) {
		const int32_t thresholdPosition = levelPosition(recorder->startValueThreshold);
		for (int32_t row = top_y + 18; row <= top_y + 34; row += 2) {
			canvas.drawPixel(thresholdPosition, row);
		}
	}

	// Either channel past the caution line shows the warning (!)
	if (std::max(state.levelEndX[0], state.levelEndX[1]) > warning_start_x) {
		canvas.drawChar('!', 119, top_y + 20, 3, kTextBigSizeY);
		// The 13 px glyph leaves the foot of the stem's right column empty
		canvas.drawPixel(121, top_y + 28);
	}

	// Each channel's meter: solid up to the caution line while recording, otherwise checkered
	for (int32_t channel = 0; channel < 2; ++channel) {
		const int32_t end = state.levelEndX[channel];
		for (int32_t column = meter_start_x + 1; column <= end; ++column) {
			for (int32_t row = 0; row < 5; ++row) {
				if ((state.recording && column < warning_start_x) || (column + row) % 2 == (state.recording ? 0 : 1)) {
					canvas.drawPixel(column, top_y + 19 + channel * 10 + row);
				}
			}
		}
	}
}

bool AudioRecorder::isThresholdRecording() const {
	// The recorder's own flag clears once the input triggers the take; the threshold it was given does not.
	return recorder != nullptr && recorder->startValueThreshold > 0;
}

void AudioRecorder::graphicsRoutine() {
	if (isThresholdRecording() && recorder->status == RecorderStatus::CAPTURING_DATA
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
	if (currentSong->live.clips.session_clips.getIndexForClip(t.clip) < 0
	    && currentSong->live.clips.arrangement_only_clips.getIndexForClip(t.clip) < 0) {
		return false;
	}
	if (t.clip->output != t.output) {
		return false;
	}
	bool outputInSong = false;
	for (Output* output = currentSong->live.media.first_output; output != nullptr; output = output->next) {
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

	if (!isUIOpen(this)) {
		// The recorder UI was closed under the take, or the whole stack taken down by a song load or clear, so the
		// sound editor's source may no longer be the one it was recording for: end the take and keep it on the card,
		// attached to nothing.
		endRecordingSoon(kInternalButtonPressLatency);
		if (recorder->settled()) {
			finishRecording();
			target_ = {};
			return;
		}
	}
	else if (getCurrentUI() == this) {
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
		// Apart from the clipping popup, so a take that keeps clipping still shows it has started
		if (!updatedRecordingStatus && (recorder->numSamplesCaptured > recorder->minThresholdMargin)) {
			// Before the redraw, which reads it
			updatedRecordingStatus = true;
			renderUIsForOled();
		}
	}

	deluge::runtime::appLoop.requestAt(jobId, deluge::runtime::nowUs() + kTickUs);
}

void AudioRecorder::finishInputTake() {
	// A take whose sound was replaced or freed under it stays on the card, attached to nothing.
	if (recorder->status != RecorderStatus::ABORTED && !recorder->hadCardError && targetStillHeld()) {
		// Attach the Sample to the Source right away.
		soundEditor.currentSound->killAllVoices();
		deluge::gui::menu_item::settingFor(
		    *soundEditor.currentSound, deluge::gui::menu_item::oscSettingPath(soundEditor.currentSourceIndex, "type"),
		    deluge::serialization::Value::makeScalar("sample"));
		soundEditor.currentMultiRange->getAudioFileHolder()->filePath = recorder->sample->filePath;
		soundEditor.currentMultiRange->getAudioFileHolder()->setAudioFile(
		    recorder->sample, soundEditor.currentSource->sampleControls.isCurrentlyReversed(), true);
		deluge::gui::menu_item::recordRanges(*soundEditor.currentSound, soundEditor.currentSourceIndex);
	}
	finishRecording();
	target_ = {};

	close();
}

// Returns error code
//
// WARNING -- USE-AFTER-FREE, live in shipping firmware. Found by the E3 scenes port (2026-09), which
// takes the correct order in its own replacement but deliberately does not patch this copy; recorded
// here and in `src/scenes/README.md`, "Defects this port found in shipping firmware".
//
// `discardRecorder` destroys and frees the object (`~SampleRecorder` + `delugeDealloc`) and only
// THEN do the two members stop advertising it. In that window `recorder` is dangling while
// `recordingSource` still says LEFT/STEREO -- and `AudioEngine::setMonitoringMode`, reached from
// `renderAudio`, tests exactly that pair and dereferences `audioRecorder.recorder`
// (`processing/engines/audio_engine.cpp` `setMonitoringMode`). The audio task is priority 0
// (`deluge.cpp` `main`), which the Rust scheduler routes onto the PREEMPTIVE SGI executor
// (`src/bsp/rust/.../scheduler.rs`), so the interrupt can land on any instruction inside
// `discardRecorder`. `audio_engine.cpp`'s output/stem-recording paths read the same member the same
// way. The `recorder != nullptr` double-check in `setMonitoringMode` does not help: the pointer is
// non-null and freed for the whole window.
//
// The fix is the order the port uses -- clear `recordingSource` FIRST, then `recorder`, then free --
// so the pointer's visible window strictly contains the channel's, with a compiler barrier between
// the two stores (`AudioEngine::setForegroundRecording`, whose header states that invariant). It is
// not applied here because these members are written from five other legacy sites this port is not
// entitled to re-order; doing it properly is a small, separate, device-tested change.
void AudioRecorder::finishRecording() {
	recorder->pointerHeldElsewhere = false;

	AudioEngine::discardRecorder(recorder);

	recorder = nullptr;
	recordingSource = AudioInputChannel::NONE;
	display->removeLoadingAnimation();
	// Every take ends here, so a MIDI next-song held back for it goes now (PlaybackHandler::slowRoutine()).
	deluge::runtime::appLoop.request(playbackHandler.slowJob);
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
