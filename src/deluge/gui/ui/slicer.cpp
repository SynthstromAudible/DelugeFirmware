/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "gui/ui/slicer.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_basic_navigator.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <iterator>

using namespace deluge::gui;

Slicer slicer{};

namespace params = deluge::modulation::params;

void Slicer::focusRegained() {

	// `slicer` is a singleton, so an audition deferred during the PREVIOUS visit is still armed
	// here, naming a slice of whatever sample that visit was slicing. graphicsRoutine() starts
	// running again the moment this UI is current, so without this it would land that stale
	// audition into the new session.
	cancelPreview();

	actionLogger.deleteAllLogs();

	numClips = 16;

	numManualSlice = 1;
	currentSlice = 0;
	slicerMode = SLICER_MODE_REGION;
	for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) {
		manualSlicePoints[i].startPos = 0;
		manualSlicePoints[i].transpose = 0;
	}
}

void Slicer::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {

	int32_t windowWidth = 100;
	int32_t windowHeight = 31;
	int32_t horizontalShift = 6;

	int32_t windowMinX = (OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1;
	windowMinX += horizontalShift;
	int32_t windowMaxX = windowMinX + windowWidth;

	int32_t windowMinY = (OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1;
	windowMinY += 2;
	int32_t windowMaxY = windowMinY + windowHeight;

	canvas.clearAreaExact(windowMinX + 1, windowMinY + 1, windowMaxX - 1, windowMaxY - 1);

	canvas.drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY);
	canvas.drawHorizontalLine(windowMinY + 15, 26, OLED_MAIN_WIDTH_PIXELS - 22);
	canvas.drawString("Num. slices", 30, windowMinY + 6, kTextSpacingX, kTextSpacingY);

	char buffer[12];
	intToString(slicerMode == SLICER_MODE_REGION ? numClips : numManualSlice, buffer);
	canvas.drawStringCentred(buffer, windowMinY + 18, kTextSpacingX, kTextSpacingY,
	                         (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift);
}

void Slicer::redraw() {
}

bool Slicer::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {

	if (slicerMode == SLICER_MODE_REGION) {
		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, image, &waveformBasicNavigator.renderData);
	}
	else if (slicerMode == SLICER_MODE_MANUAL) {

		RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
		waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
		                                  waveformBasicNavigator.xZoom, myImage, &waveformBasicNavigator.renderData);

		for (int32_t xx = 0; xx < kDisplayWidth; xx++) {
			for (int32_t yy = 0; yy < kDisplayHeight / 2; yy++) {
				image[yy + 4][xx] = RGB::average(myImage[yy * 2][xx], myImage[yy * 2 + 1][xx]);
			}
		}
		for (int32_t i = 0; i < numManualSlice; i++) { // Slices
			int32_t x = manualSlicePoints[i].startPos / (waveformBasicNavigator.sample->lengthInSamples + 0.0) * 16;
			image[4][x] = RGB{
			    1,
			    (i == currentSlice) ? 200_u8 : 16_u8,
			    1,
			};
		}

		for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) { // Lower screen
			int32_t xx = (i % 4) + (i / 16) * 4;
			int32_t yy = (i / 4) % 4;
			int32_t page = i / 16;

			RGB colour = RGB::monochrome(3);
			size_t dimLevel = (i < numManualSlice) ? 2 : 6;
			if (page % 2 == 0) {
				colour = colours::green.dim(dimLevel);
			}
			else {
				colour = colours::darkblue.dim(dimLevel);
			}
			if (i == this->currentSlice) {
				colour = colours::green.dim();
			}

			image[yy][xx] = colour;
		}
	}
	return true;
}

const uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0};

void Slicer::graphicsRoutine() {
	// Re-ask for a deferred audition. This surface (every ~15 ms while the slicer is up) is the only
	// thing that ever finishes one.
	//
	// take_pending() takes the target OUT before driving it, not after: drivePreview() runs the attempt
	// inline, so it completes — and can arm a SECOND deferral — before it returns, which clearing
	// afterwards would wipe.
	if (auto target = previewLoad_.take_pending(); target.has_value()) {
		(void)drivePreview(*target);
	}

	int32_t newTickSquare = 255;
	VoiceSample* voiceSample = nullptr;
	SamplePlaybackGuide* guide = nullptr;

	MultisampleRange* range;
	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;

	if (getCurrentClip()->type == ClipType::INSTRUMENT && drum->hasActiveVoices()) {
		range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

		auto valid_voices_view = drum->voices() | std::views::filter([&](const Sound::ActiveVoice& voice) {
			                         // Ensure correct MultisampleRange.
			                         return voice->guides[0].audioFileHolder == range->getAudioFileHolder();
		                         });

		if (!valid_voices_view.empty()) {
			const Sound::ActiveVoice& voice = *std::ranges::max_element(valid_voices_view, {}, &Voice::orderSounded);

			VoiceUnisonPartSource* part = &voice->unisonParts[drum->numUnison >> 1].sources[0];
			if (part != nullptr && part->active) {
				voiceSample = part->voiceSample;
				guide = &voice->guides[soundEditor.currentSourceIndex];
			}
		}
	}

	if (voiceSample != nullptr) {
		int32_t samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
		if (samplePos >= waveformBasicNavigator.xScroll) {
			newTickSquare = (samplePos - waveformBasicNavigator.xScroll) / waveformBasicNavigator.xZoom;
			if (newTickSquare >= kDisplayWidth) {
				newTickSquare = 255;
			}
		}
	}

	uint8_t tickSquares[kDisplayHeight];
	memset(tickSquares, 255, kDisplayHeight);
	tickSquares[kDisplayHeight - 1] = newTickSquare;
	tickSquares[kDisplayHeight - 2] = newTickSquare;
	tickSquares[kDisplayHeight - 3] = newTickSquare;
	tickSquares[kDisplayHeight - 4] = newTickSquare;

	PadLEDs::setTickSquares(tickSquares, zeroes);
}

ActionResult Slicer::horizontalEncoderAction(int32_t offset) {

	if (slicerMode == SLICER_MODE_MANUAL) {
		int32_t newPos = manualSlicePoints[currentSlice].startPos;
		newPos += ((Buttons::isShiftButtonPressed() == true) ? 10 : 100) * offset;

		if (currentSlice > 0 && currentSlice < numManualSlice - 1) {
			if (newPos <= manualSlicePoints[currentSlice - 1].startPos + 1)
				newPos = manualSlicePoints[currentSlice - 1].startPos + 1;
			if (newPos >= manualSlicePoints[currentSlice + 1].startPos - 1)
				newPos = manualSlicePoints[currentSlice + 1].startPos - 1;
		}

		if (newPos < 0)
			newPos = 0;
		if (newPos > waveformBasicNavigator.sample->lengthInSamples)
			newPos = waveformBasicNavigator.sample->lengthInSamples;
		manualSlicePoints[currentSlice].startPos = newPos;

		char buffer[24];
		strcpy(buffer, "Start: ");
		intToString(manualSlicePoints[currentSlice].startPos, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult Slicer::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (slicerMode == SLICER_MODE_MANUAL) {

		manualSlicePoints[currentSlice].transpose += offset;
		if (manualSlicePoints[currentSlice].transpose > 24)
			manualSlicePoints[currentSlice].transpose = 24;
		if (manualSlicePoints[currentSlice].transpose < -24)
			manualSlicePoints[currentSlice].transpose = -24;
		char buffer[32];
		snprintf(buffer, 32, "Transpose: %d", manualSlicePoints[currentSlice].transpose);
		display->popupTextTemporary(buffer);
	}
	return ActionResult::DEALT_WITH;
}

void Slicer::selectEncoderAction(int8_t offset) {
	if (slicerMode == SLICER_MODE_REGION) {
		numClips += offset;
		if (numClips == 257) {
			numClips = 2;
		}
		else if (numClips == 1) {
			numClips = 256;
		}
	}
	else { // SLICER_MODE_MANUAL
		if (offset < 0) {
			numManualSlice += offset;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
		}
		if (currentSlice >= numManualSlice - 1) {
			currentSlice = numManualSlice - 1;
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}

	renderUIsForOled();
}

ActionResult Slicer::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (currentUIMode != UI_MODE_NONE || !on) {
		return ActionResult::NOT_DEALT_WITH;
	}

	// switch slicer mode
	if (b == X_ENC && on) {
		// Auditions only exist in MANUAL mode, and this leaves the mode the pending one was taken
		// in — killing its voices on the way out. Nothing dispatches a replacement target, so
		// without cancelling, a deferred audition would sound after the mode switch that stopped it.
		cancelPreview();
		slicerMode++;
		slicerMode %= 2;
		if (slicerMode == SLICER_MODE_MANUAL)
			AudioEngine::stopAnyPreviewing();
		renderUIsForOled();

		getCurrentKit()->firstDrum->killAllVoices(); // stop
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
		return ActionResult::DEALT_WITH;
	}

	// pop up Transpose value
	if (b == Y_ENC && on && slicerMode == SLICER_MODE_MANUAL && currentSlice < numManualSlice) {
		char buffer[24];
		strcpy(buffer, "Transpose: ");
		intToString(manualSlicePoints[currentSlice].transpose, buffer + strlen(buffer));
		display->popupTextTemporary(buffer);
		return ActionResult::DEALT_WITH;
	}

	// delete slice
	if (b == SAVE && on && slicerMode == SLICER_MODE_MANUAL) {
		int32_t xx = (currentSlice % 4) + (currentSlice / 16) * 4;
		int32_t yy = (currentSlice / 4) % 4;
		if (matrixDriver.isPadPressed(xx, yy) && currentSlice < numManualSlice) {
			int32_t target = currentSlice;

			for (int32_t i = 0; i < MAX_MANUAL_SLICES - 1; i++) {
				manualSlicePoints[i] = manualSlicePoints[(i >= target) ? i + 1 : i];
			}
			manualSlicePoints[MAX_MANUAL_SLICES - 1].startPos = 0;
			manualSlicePoints[MAX_MANUAL_SLICES - 1].transpose = 0;

			numManualSlice--;
			if (numManualSlice <= 0) {
				numManualSlice = 1;
				manualSlicePoints[0].startPos = 0;
				manualSlicePoints[0].transpose = 0;
			}
			if (currentSlice >= numManualSlice - 1) {
				currentSlice = numManualSlice - 1;
			}

			uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
			renderUIsForOled();
			return ActionResult::DEALT_WITH;
		}
	}

	if (b == SELECT_ENC) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}

		// Snapshot the commit gesture — plain values only, no pointers into manualSlicePoints[]/
		// numManualSlice/numClips survive past this point — before dispatch. doSlice() and, for
		// MANUAL mode, the per-drum paint loop move into one op on the storage owner together
		// (see SliceCommitTarget/runDoSliceOp); the op runs entirely off this snapshot, never
		// re-reading these live members, so a concurrent encoder turn or pad press during the
		// dispatched load can't desync what gets sliced from what gets painted.

		// Committing ends in sampleBrowser.exitAndNeverDeleteDrum() → close(), i.e. the slicer is
		// gone by the time any deferred audition could be re-driven, and the drums it would sound
		// through have been rebuilt underneath it.
		cancelPreview();

		pendingSliceTarget_.isManual = (slicerMode == SLICER_MODE_MANUAL);
		pendingSliceTarget_.sliceCount = pendingSliceTarget_.isManual ? numManualSlice : numClips;
		if (pendingSliceTarget_.isManual) {
			std::copy(std::begin(manualSlicePoints), std::end(manualSlicePoints),
			          std::begin(pendingSliceTarget_.manualPoints));
			getCurrentKit()->firstDrum->killAllVoices(); // stop
		}

		// Close the gate before dispatch, not inside the op: doSlice() ends by calling
		// sampleBrowser.exitAndNeverDeleteDrum() -> close(), which pops this Slicer (and the
		// SampleBrowser beneath it) off the UI navigation stack from inside the dispatched op.
		// Without this gate, a BACK press reaching the BACK case below during that window would
		// call close() on `this` from the executor at the same time the op is mutating the same
		// uiNavigationHierarchy/numUIsOpen globals from the worker — a UI-stack-corruption hazard
		// (the same shape ClearSong and LoadSongUI::performLoad guard against). A repeat
		// SELECT_ENC would re-dispatch doSlice() mid-flight (double-slicing the same commit).
		// currentUIMode != UI_MODE_NONE makes buttonAction() a no-op for both (top-of-function
		// guard above) until runDoSliceOp() resets it.
		currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
		if (!deluge::storage::Owner::run_or_inline(&Slicer::runDoSliceOp, this)) {
			// Dispatch dropped (owner queue full) — runDoSliceOp will never run, so release the
			// gate we just closed here, or Slicer would be stuck (no BACK/SELECT_ENC) forever.
			currentUIMode = UI_MODE_NONE;
		}
	}

	else if (b == BACK) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		// Leaving the slicer: close() pops it off the UI stack, so graphicsRoutine() stops running
		// and a deferred audition would otherwise sit armed until the next visit.
		cancelPreview();
		if (slicerMode == SLICER_MODE_MANUAL) {
			RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];
			waveformRenderer.renderFullScreen(waveformBasicNavigator.sample, waveformBasicNavigator.xScroll,
			                                  waveformBasicNavigator.xZoom, PadLEDs::image,
			                                  &waveformBasicNavigator.renderData);
			getCurrentKit()->firstDrum->killAllVoices(); // stop
			Kit* kit = getCurrentKit();
			Drum* drum = kit->firstDrum;
			SoundDrum* soundDrum = (SoundDrum*)drum;
			MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
			Sample* sample = (Sample*)range->sampleHolder.audioFile;
			range->sampleHolder.startPos = 0;
			range->sampleHolder.endPos = sample->lengthInSamples;
			range->sampleHolder.transpose = 0;
		}

		close();
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}

	return ActionResult::DEALT_WITH;
}

void Slicer::stopAnyPreviewing() {
	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;
	drum->killAllVoices();
	if (std::ssize(drum->sources[0].ranges)) {
		MultisampleRange* range = (MultisampleRange*)drum->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(nullptr);
	}
}
void Slicer::preview(int64_t startPoint, int64_t endPoint, int32_t transpose, int32_t on) {
	// Snapshot the request — plain values only, no pointers into manualSlicePoints[] or any other
	// live UI state survive past this call — and hand it to drivePreview(), which runs the audition
	// here and arms a re-ask when its load defers. A burst of pad taps converges on whichever
	// slice/on-off state the finger is actually on either way, because `previewLoad_` is latest-wins.
	PreviewTarget target{
	    .startPoint = startPoint,
	    .endPoint = endPoint,
	    .transpose = transpose,
	    .on = on,
	};

	(void)drivePreview(target);
}

bool Slicer::drivePreview(const PreviewTarget& target) {
	if (!previewLoad_.request(target)) {
		// An attempt is already running and will pick this target up on completion (latest-wins).
		return false;
	}

	bool deferred = false;
	for (;;) {
		const auto attempt = previewLoad_.attempt();

		// is_current() gates the WORK, not merely the recording of a deferral: sounding a slice into a
		// slicer the user has left is the damage, and a stale re-ask is only the mechanism.
		//
		// It has no reachable false branch, and states the invariant rather than closing a
		// window. attempt() is the line above, and every path that stamps a target current
		// (request()'s dispatch arm, complete()'s hand-off arm) does so immediately before it, in a
		// drive that never yields between the two -- so nothing can bump the generation in that gap.
		// The async loader's loadFile() does no I/O, so this drive gives control up nowhere at all,
		// and a cancel lands either before request() or after this check has already passed. What
		// refuses a cancelled attempt is complete()'s `deferred && is_current(a)` five lines down,
		// and that is what tests/unit/slicer_preview_tests.cpp pins.
		//
		// boundPreviewWait() ends a wait that has deferred for the whole bound: it answers "not deferred", so
		// nothing is re-armed and no note sounds.
		deferred = boundPreviewWait(attempt, previewLoad_.is_current(attempt) && previewForTarget(attempt));

		// A deferral is armed for graphicsRoutine() to re-drive — that surface is the ONLY thing
		// that finishes a deferred audition.
		//
		// A returned target is a NEWER one that arrived while the attempt above ran (a pad tap
		// reaching preview() while a synchronous load holds this tier). It is already current and
		// still single-flight, so it is run here rather than re-requested: request() would see the
		// flight still open, return false, and drop it.
		if (!previewLoad_.complete(attempt, deferred).has_value()) {
			return deferred;
		}
	}
}

bool Slicer::boundPreviewWait(const deluge::storage::DeferredLoad<PreviewTarget>::Attempt& attempt, bool deferred) {
	if (!deferred) {
		previewWaitAttempts_ = 0; // answered (loaded or failed), or not run at all
		return false;
	}
	if (attempt.generation != previewWaitGeneration_ || attempt.target != previewWaitTarget_) {
		// A different attempt: its wait starts from nothing.
		previewWaitGeneration_ = attempt.generation;
		previewWaitTarget_ = attempt.target;
		previewWaitAttempts_ = 0;
	}
	if (++previewWaitAttempts_ < kMaxPreviewWaitAttempts) {
		return true; // still loading: ask again next tick
	}
	// The loader has left this load unanswered for the whole bound. Give up as a load that could not reach the
	// card: the card error, no note, and no re-ask armed. The next press asks again.
	previewWaitAttempts_ = 0;
	previewGiveUps_++;
	display->displayError(Error::SD_CARD);
	return false;
}

bool Slicer::previewForTarget(const deluge::storage::DeferredLoad<PreviewTarget>::Attempt& attempt) {
	const PreviewTarget& target = attempt.target;

	if (target.on) {
		Kit* kit = getCurrentKit();
		SoundDrum* drum = (SoundDrum*)kit->firstDrum;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);

		MultisampleRange* range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();
		drum->drumName = "1";
		drum->sources[0].repeatMode = SampleRepeatMode::ONCE;

		// The path is what says "a different sample is being sliced now"; the null audioFile is what
		// says "the load for this path hasn't landed yet". Both have to ask, because the path is
		// assigned BEFORE the load: a load that answers LOAD_IN_PROGRESS leaves the holder naming a
		// file it does not hold, and a path-only check would then skip straight to the audition note
		// on the re-ask and sound a slice of nothing.
		const bool differentSample = !(waveformBasicNavigator.sample->filePath == range->sampleHolder.filePath);
		if (differentSample) {
			stopAnyPreviewing();
			range->sampleHolder.filePath = waveformBasicNavigator.sample->filePath.c_str();
		}
		if (differentSample || range->sampleHolder.audioFile == nullptr) {
			const Error error = range->sampleHolder.loadFile(false, true, true, CLUSTER_ENQUEUE, false,
			                                                 /*mayLoadAsync=*/true);
			if (error == Error::LOAD_IN_PROGRESS) {
				// Deferred, not failed. Return BEFORE the params and the note: the load, the params
				// and the audition note stay one group, and graphicsRoutine() re-drives it.
				return true; // deferred
			}
		}
		range->sampleHolder.startPos = target.startPoint;
		if (target.endPoint != -1)
			range->sampleHolder.endPos = target.endPoint;
		range->sampleHolder.transpose = target.transpose;

		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ModelStackWithParamId* modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
		modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_ATTACK);
		modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
		    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 1));
	}
	// Deliberately OUTSIDE the `if (target.on)` above, and after the only `return` in it: a release
	// does no I/O, so it can never defer, and its note-off always lands. Hoisting this into the
	// branch would make a finger lifted before the load arrives swallow the note-off and leave the
	// note sounding forever. `aReleaseNeverDefersAndAlwaysSoundsItsNoteOff` pins that.
	instrumentClipView.sendAuditionNote(target.on, 0, 64, 0);
	return false; // not deferred
}

ActionResult Slicer::padAction(int32_t x, int32_t y, int32_t on) {

	if (on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad on

		int32_t slicePadIndex = (x % 4 + (x / 4) * 16) + ((y % 4) * 4); //

		if (slicePadIndex < numManualSlice) { // play slice
			bool closePopup = (currentSlice != slicePadIndex);
			currentSlice = slicePadIndex;
			if (slicePadIndex + 1 < numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, manualSlicePoints[slicePadIndex + 1].startPos,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}
			else if (slicePadIndex + 1 == numManualSlice) {
				preview(manualSlicePoints[slicePadIndex].startPos, waveformBasicNavigator.sample->lengthInSamples,
				        manualSlicePoints[slicePadIndex].transpose, on);
			}

			if (closePopup) {
				display->cancelPopup();
			}
		}
		else { // do slice

			VoiceSample* voiceSample = nullptr;
			SamplePlaybackGuide* guide = nullptr;
			MultisampleRange* range;
			Kit* kit = getCurrentKit();
			SoundDrum* drum = (SoundDrum*)kit->firstDrum;

			if (getCurrentClip()->type == ClipType::INSTRUMENT && drum->hasActiveVoices()) {
				auto valid_voices_view = drum->voices() | std::views::filter([&](const Sound::ActiveVoice& voice) {
					                         // Ensure correct MultisampleRange.
					                         return voice->guides[0].audioFileHolder == range->getAudioFileHolder();
				                         });

				range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();

				if (!valid_voices_view.empty()) {
					const Sound::ActiveVoice& assigned_voice =
					    *std::ranges::max_element(valid_voices_view, {}, &Voice::orderSounded);

					VoiceUnisonPartSource* part = &assigned_voice->unisonParts[drum->numUnison >> 1].sources[0];
					if (part != nullptr && part->active) {
						voiceSample = part->voiceSample;
						guide = &assigned_voice->guides[soundEditor.currentSourceIndex];
					}
				}
			}
			if (voiceSample != nullptr) {
				int32_t samplePos = voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
				if (samplePos < waveformBasicNavigator.sample->lengthInSamples && numManualSlice < MAX_MANUAL_SLICES) {
					manualSlicePoints[numManualSlice].startPos = samplePos;
					manualSlicePoints[numManualSlice].transpose = 0;

					numManualSlice++;
					display->cancelPopup();

					SliceItem tmp{};
					for (int32_t i = 0; i < (numManualSlice - 1); i++) {
						for (int32_t j = (numManualSlice - 1); j > i; j--) {
							if (manualSlicePoints[j].startPos < manualSlicePoints[j - 1].startPos) {
								tmp = manualSlicePoints[j];
								manualSlicePoints[j] = manualSlicePoints[j - 1];
								manualSlicePoints[j - 1] = tmp;
							}
						}
					}
				}
			}
		}

		renderUIsForOled();
		uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	}
	else if (!on && x < kDisplayWidth && y < kDisplayHeight / 2 && slicerMode == SLICER_MODE_MANUAL) { // pad off
		preview(0, 0, 0, 0);                                                                           // off
	}

	if (slicerMode == SLICER_MODE_MANUAL) {
		return ActionResult::DEALT_WITH;
	}

	return sampleBrowser.padAction(x, y, on);
}

void Slicer::runDoSliceOp(void* self) {
	auto* s = static_cast<Slicer*>(self);
	s->commitSlice(s->pendingSliceTarget_);
}

void Slicer::commitSlice(const SliceCommitTarget& target) {
	doSlice(target.sliceCount);

	if (target.isManual) {
		Kit* kit = getCurrentKit();
		for (int32_t i = 0; i < target.sliceCount; i++) {
			Drum* drum = kit->getDrumFromIndex(i);
			SoundDrum* soundDrum = (SoundDrum*)drum;
			MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
			range->sampleHolder.startPos = target.manualPoints[i].startPos;
			range->sampleHolder.endPos = (i == target.sliceCount - 1) ? waveformBasicNavigator.sample->lengthInSamples
			                                                          : target.manualPoints[i + 1].startPos;
			range->sampleHolder.transpose = target.manualPoints[i].transpose;
		}
	}

	// Release the gate buttonAction() closed before dispatch (see its comment) — every doSlice()
	// exit path, success or its internal load-failure `getOut:` branch, lands here.
	currentUIMode = UI_MODE_NONE;
}

void Slicer::doSlice(int32_t sliceCount) {

	AudioEngine::stopAnyPreviewing();

	Error error = sampleBrowser.claimAudioFileForInstrument();
	if (error != Error::NONE) {
getOut:
		display->displayError(error);
		return;
	}

	Kit* kit = getCurrentKit();

	// Do the first Drum

	// Ensure osc type is "sample"
	if (soundEditor.currentSource->oscType != OscType::SAMPLE) {
		soundEditor.currentSound->killAllVoices();
		soundEditor.currentSource->setOscType(OscType::SAMPLE);
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
		int32_t paramId = params::LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
		ModelStackWithAutoParam* modelStackWithParam =
		    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

		// Reset osc volume, if it's not automated
		if (!modelStackWithParam->autoParam->isAutomated()) {
			modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);
			//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(params::LOCAL_OSC_A_VOLUME +
			// soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong,
			// getCurrentClip(), false);
		}

		SoundDrum* firstDrum = (SoundDrum*)soundEditor.currentSound;

		if (firstDrum->nameIsDiscardable) {
			firstDrum->drumName = "1";
		}

		MultisampleRange* firstRange = (MultisampleRange*)firstDrum->sources[0].getOrCreateFirstRange();

		Sample* sample = (Sample*)firstRange->sampleHolder.audioFile;

		uint32_t lengthInSamples = sample->lengthInSamples;

		uint32_t lengthSamplesPerSlice = lengthInSamples / sliceCount;
		uint32_t lengthMSPerSlice = lengthSamplesPerSlice * 1000 / sample->sampleRate;

		bool doEnvelopes =
		    (lengthMSPerSlice >= 90); // Only do fades in and out if we've got at least 100ms to play with

		firstRange->sampleHolder.startPos = 0;
		uint32_t nextDrumStart = lengthInSamples / sliceCount;
		firstRange->sampleHolder.endPos = nextDrumStart;

		firstDrum->sources[0].repeatMode =
		    (lengthMSPerSlice < 2002) ? SampleRepeatMode::ONCE : FlashStorage::defaultSliceMode;

		firstDrum->sources[0].sampleControls.reversed = false;
		firstDrum->sources[0].sampleControls.invertReversed = false;

#if 1 || ALPHA_OR_BETA_VERSION
		if (!firstRange->sampleHolder.audioFile) {
			FREEZE_WITH_ERROR("i032"); // Trying to narrow down E368 that Kevin F got
		}
#endif

		firstRange->sampleHolder.claimClusterReasons(firstDrum->sources[0].sampleControls.isCurrentlyReversed(),
		                                             CLUSTER_ENQUEUE);
		if (doEnvelopes) {
			ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
			ModelStackWithParamId* modelStackWithParamId =
			    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
			modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
			    modelStackWithAutoParam, getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
		}

		// Do the rest of the Drums
		for (int32_t i = 1; i < sliceCount; i++) {

			// Make the Drum and its ParamManager
			ParamManagerForTimeline paramManager;
			error = paramManager.setupWithPatching();
			if (error != Error::NONE) {
				goto getOut;
			}

			void* drumMemory = deluge::memory::alloc_fast(sizeof(SoundDrum));
			if (!drumMemory) {
ramError:
				error = Error::INSUFFICIENT_RAM;
				goto getOut;
			}

			SoundDrum* newDrum = new (drumMemory) SoundDrum();

			MultisampleRange* range = (MultisampleRange*)newDrum->sources[0].getOrCreateFirstRange();
			if (!range) {
ramError2:
				newDrum->~SoundDrum();
				delugeDealloc(drumMemory);
				goto ramError;
			}

			newDrum->drumName = deluge::string::fromInt(i + 1);

			Sound::initParams(&paramManager);

			kit->addDrum(newDrum);
			newDrum->setupAsSample(&paramManager);

			range->sampleHolder.startPos = nextDrumStart;
			nextDrumStart = (uint64_t)lengthInSamples * (i + 1) / sliceCount;
			range->sampleHolder.endPos = nextDrumStart;

			newDrum->sources[0].repeatMode =
			    (lengthMSPerSlice < 2002) ? SampleRepeatMode::ONCE : FlashStorage::defaultSliceMode;

			range->sampleHolder.filePath = sample->filePath;
			range->sampleHolder.loadFile(false, false, true);

			if (doEnvelopes) {
				paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
				    getParamFromUserValue(params::LOCAL_ENV_0_ATTACK, 1));
				if (i != sliceCount - 1) {
					paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
					    getParamFromUserValue(params::LOCAL_ENV_0_RELEASE, 1));
				}
			}

			// I moved this here, from being earlier/above. Is this fine?
			currentSong->backUpParamManager(newDrum, getCurrentClip(), &paramManager, true);
		}

		// Make NoteRows for all these new Drums
		getCurrentKit()->resetDrumTempValues();
		firstDrum->noteRowAssignedTemp = 1;
	}
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)modelStackMemory;
	getCurrentInstrumentClip()->assignDrumsToNoteRows(modelStack);

	getCurrentInstrument()->beenEdited();

	// New NoteRows have probably been created, whose colours haven't been grabbed yet.
	instrumentClipView.recalculateColours();

	sampleBrowser.exitAndNeverDeleteDrum();
	uiNeedsRendering(&instrumentClipView);
}
