/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "gui/ui/browser/sample_claim.h"

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/multi_range.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/audio/async_sample_load.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/sample_load_walk.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"

#include <iterator>
#include <utility>

namespace params = deluge::modulation::params;

namespace deluge::gui {

SampleClaim sampleClaim{};

namespace {

bool claimingAudioClip() {
	return getCurrentClip()->type == ClipType::AUDIO;
}

/// The holder the claim loads into: the audio clip's, or the sound editor's current range's.
AudioFileHolder* claimHolder() {
	return soundEditor.getCurrentAudioFileHolder();
}

/// Cut the sound, unbind its holder, and point it at @p path: the start of every attempt at a type.
AudioFileHolder* prepareHolder(const std::string& path) {
	soundEditor.cutSound();
	AudioFileHolder* holder = claimHolder();
	holder->setAudioFile(nullptr);
	holder->filePath = path;
	return holder;
}

void closeGate() {
	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
}

void openGate() {
	if (currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES) {
		currentUIMode = UI_MODE_NONE;
	}
}

} // namespace

bool SampleClaim::explicitInFlight() const {
	return (phase_ != Phase::Idle && phase_ != Phase::Draining && current_.origin == ClaimOrigin::Explicit)
	       || (queued_.has_value() && queued_->origin == ClaimOrigin::Explicit);
}

void SampleClaim::request(ClaimRequest request) {
	if (request.origin == ClaimOrigin::AutoLoad && explicitInFlight()) {
		// The explicit claim is already claiming a file; this can only come from a preview that was already loading.
		return;
	}
	if (phase_ != Phase::Idle && phase_ != Phase::Draining) {
		// Latest-wins: an auto-load claim replaced by the next, or by an explicit claim. Browser input is refused while
		// an explicit claim is in flight, so no request reaches here past one.
		cancel();
	}
	if (request.origin == ClaimOrigin::Explicit) {
		closeGate();
	}
	queued_ = std::move(request);
	routine();
}

void SampleClaim::cancelAutoLoad() {
	if (queued_.has_value() && queued_->origin == ClaimOrigin::AutoLoad) {
		queued_.reset();
	}
	if (phase_ != Phase::Idle && phase_ != Phase::Draining && current_.origin == ClaimOrigin::AutoLoad) {
		cancel();
	}
}

void SampleClaim::cancel() {
	cancels_++;
	load_.cancel();
	if (animationShown_) {
		display->removeLoadingAnimation();
		animationShown_ = false;
	}
	if (current_.origin == ClaimOrigin::Explicit) {
		openGate();
	}
	if (opOutstanding()) {
		// The operation still reads or scans the holder's file: nothing may unbind it, or bind the holder, until it
		// reports. Its result is discarded; a wavetable it acquired stays in memory with no reason, and can be evicted.
		drains_++;
		phase_ = Phase::Draining;
		return;
	}
	phase_ = Phase::Idle;
	opDispatched_ = false;
}

void SampleClaim::routine() {
	for (;;) {
		if (phase_ == Phase::Idle) {
			if (!queued_.has_value()) {
				return;
			}
			if (opOutstanding()) {
				startsUnderOp_++;
			}
			current_ = std::move(*queued_);
			queued_.reset();
			opDispatched_ = false;
			phase_ = Phase::Start;
		}
		if (!step()) {
			return;
		}
	}
}

bool SampleClaim::step() {
	switch (phase_) {
	case Phase::Idle:
		return true;
	case Phase::Start:
		return stepStart();
	case Phase::PrepareSample:
		return stepPrepareSample();
	case Phase::LoadSample:
		return stepLoadSample();
	case Phase::DecideSample:
		return stepDecideSample();
	case Phase::PrepareWavetable:
		return stepPrepareWavetable();
	case Phase::AwaitWavetable:
		return stepAwaitWavetable();
	case Phase::DetectPitch:
		return stepDetectPitch();
	case Phase::AwaitPitch:
		return stepAwaitPitch();
	case Phase::Finish:
		return stepFinish();
	case Phase::Draining:
		if (!opRunning_) {
			phase_ = Phase::Idle;
			opDispatched_ = false;
			return true;
		}
		return false;
	}
	return false;
}

bool SampleClaim::stepStart() {
	// Counted for the sample-claim gate (SC-TIER): a claim that begins on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		loadsOnOwner_++;
	}

	if (claimingAudioClip() && getCurrentClip()->getCurrentlyRecordingLinearly()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_IS_RECORDING));
		end(false, Error::NONE);
		return true;
	}

	display->displayLoadingAnimationText("Working");
	animationShown_ = true;
	numTypesTried_ = 0;
	doingSingleCycle_ = false;

	if (claimingAudioClip()) {
		phase_ = Phase::PrepareSample;
		return true;
	}

	soundEditor.currentSound->killAllVoices(); // We used to only do this if osc type wasn't already SAMPLE...

	makeWaveTable_ = (current_.mayDoWaveTable == 2) || (current_.mayDoSingleCycle == 2)
	                 || (soundEditor.currentSound->getSynthMode() == SynthMode::RINGMOD);

	if (makeWaveTable_ || (current_.mayDoWaveTable == 1 && soundEditor.currentSource->oscType == OscType::WAVETABLE)) {
		phase_ = Phase::PrepareWavetable;
	}
	else {
		// A sample first counts as a type tried; a sample reached from a refused wavetable does not.
		numTypesTried_++;
		phase_ = Phase::PrepareSample;
	}
	return true;
}

bool SampleClaim::stepPrepareSample() {
	bool reversed = false;
	if (claimingAudioClip()) {
		reversed = getCurrentAudioClip()->sampleControls.isCurrentlyReversed();
	}
	else {
		soundEditor.currentSource->setOscType(OscType::SAMPLE);
		reversed = soundEditor.currentSource->sampleControls.isCurrentlyReversed();
	}
	AudioFileHolder* holder = prepareHolder(current_.path);
	load_.request(
	    deluge::storage::SampleDemand{
	        .holder = holder,
	        .reversed = reversed,
	        .manuallySelected = true,
	        .clusterLoadInstruction = CLUSTER_ENQUEUE,
	    },
	    /*context=*/nullptr, 0);
	phase_ = Phase::LoadSample;
	return true;
}

bool SampleClaim::stepLoadSample() {
	using Result = deluge::storage::LatestSampleLoad<uint32_t>::Result;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	const auto outcome = load_.tick();
	if (!outcome.has_value()) {
		return false; // still loading
	}
	if (outcome->result == Result::Failed) {
		// Nothing more can arrive for this load: it stopped moving, or the card cannot be read.
		if (outcome->reason == Reason::Stalled) {
			giveUps_++;
		}
		const bool cardError = (outcome->reason == Reason::Stalled || outcome->reason == Reason::CardUnusable);
		end(false, cardError ? Error::SD_CARD : Error::NONE);
		return true;
	}
	if (outcome->result == Result::Empty) {
		end(false, outcome->error);
		return true;
	}
	phase_ = Phase::DecideSample;
	return true;
}

bool SampleClaim::stepDecideSample() {
	if (claimingAudioClip()) {
		AudioClip* clip = getCurrentAudioClip();

		// If there's a pre-margin, we want to set an attack-time
		if (clip->sampleHolder.startPos) {
			clip->attack = kAudioClipDefaultAttackIfPreMargin;
		}

		// load sample with time stretching
		uint64_t lengthInSamplesAt44 = (uint64_t)clip->sampleHolder.getDurationInSamples(true) * kSampleRate
		                               / ((Sample*)clip->sampleHolder.audioFile)->sampleRate;
		uint32_t sampleLengthInTicks = (lengthInSamplesAt44 << 32) / currentSong->timePerTimerTickBig;

		int32_t newLength = 3;
		while (newLength * 1.41 < sampleLengthInTicks) {
			newLength <<= 1;
		}

		int32_t oldLength = clip->loopLength;

		clip->loopLength = newLength;
		clip->originalLength = newLength;
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		clip->lengthChanged(modelStack, oldLength);

		clip->sampleHolder.transpose = 0;
		clip->sampleHolder.cents = 0;
		clip->sampleControls.reversed = false;
		clip->sampleControls.invertReversed = false;

		phase_ = Phase::Finish;
		return true;
	}

	Sample* sample = (Sample*)claimHolder()->audioFile;

	// If the file was actually clearly a wavetable file, and we're allowed to load one, then go do that instead.
	if (current_.mayDoWaveTable && numTypesTried_ <= 1 && sample->fileExplicitlySpecifiesSelfAsWaveTable) {
		phase_ = Phase::PrepareWavetable;
		return true;
	}

	int32_t mSec = sample->getLengthInMSec();

	// If 20ms or less, and we're not a kit, then we'd like to be a single-cycle waveform.
	if (!soundEditor.editingKit()
	    && (current_.mayDoSingleCycle == 2 || (current_.mayDoSingleCycle == 1 && mSec <= 20))) {

		// Ideally, we'd like to use the wavetable engine for this single-cycle-ness
		if (current_.mayDoWaveTable && numTypesTried_ <= 1 && sample->numChannels == 1
		    && sample->lengthInSamples >= kWavetableMinCycleSize && sample->lengthInSamples <= kWavetableMaxCycleSize) {
			makeWaveTable_ = true; // So that the loading functions don't just chicken out when it doesn't look all that
			                       // wavetabley.
			phase_ = Phase::PrepareWavetable;
			return true;
		}

		// Otherwise, set play mode to LOOP, and we'll just do single-cycle as a sample. (This is now pretty rare.)
		soundEditor.currentSource->repeatMode = SampleRepeatMode::LOOP;
		doingSingleCycle_ = true;
	}

	// If time stretching or looping on (or we just decided to do single-cycle, above), leave that the case
	if (soundEditor.currentSource->repeatMode == SampleRepeatMode::STRETCH
	    || soundEditor.currentSource->repeatMode == SampleRepeatMode::LOOP) {}

	// Otherwise...
	else {
		// If source file had loop points set...
		if (sample->fileLoopEndSamples) {
			// If this led to an actual loop end pos, with more waveform after it, and the sample's not too long, we can
			// do a ONCE.
			if (((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.loopEndPos && mSec < 2002) {
				soundEditor.currentSource->repeatMode = SampleRepeatMode::ONCE;
			}
			else {
				soundEditor.currentSource->repeatMode = SampleRepeatMode::LOOP;
			}
		}
		else {
			// If 2 seconds or less, set play mode to ONCE. Otherwise, CUT.
			soundEditor.currentSource->repeatMode = (mSec < 2002) ? SampleRepeatMode::ONCE : SampleRepeatMode::CUT;
		}
	}

	// If Kit...
	if (soundEditor.editingKit()) {
		SoundDrum* drum = (SoundDrum*)soundEditor.currentSound;

		sampleBrowser.autoDetectSideChainSending(drum, soundEditor.currentSource, current_.enteredText.c_str());

		// Give Drum no name, momentarily. We don't want it to show up when we're searching for duplicates
		drum->drumName.clear();

		std::string newName = current_.enteredText;

		// Ensure Drum name isn't a duplicate, and if need be, make a new name from the fileNamePostPrefix.
		if (getCurrentKit()->getDrumFromName(newName.c_str())) {
			Error error = getCurrentKit()->makeDrumNameUnique(&newName, 2);
			if (error != Error::NONE) {
				end(false, error);
				return true;
			}
		}

		drum->drumName = newName.c_str();
	}

	// If a synth...
	else {
		if (current_.mayDoPitchDetection) {
			phase_ = Phase::DetectPitch;
			return true;
		}
		// Otherwise, reset pitch. Popular request, late 2022.
		// https://forums.synthstrom.com/discussion/4814/v4-0-1-after-loading-a-non-c-sample-into-synth-reloading-the-sample-as-basic-doesnt-reset-pitch
		((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.transpose = 0;
		((MultisampleRange*)soundEditor.currentMultiRange)->sampleHolder.setCents(0);
	}

	removeWaveTableKnobs();
	phase_ = Phase::Finish;
	return true;
}

bool SampleClaim::stepPrepareWavetable() {
	numTypesTried_++;
	oldOscType_ = soundEditor.currentSource->getOscType();
	soundEditor.currentSource->setOscType(OscType::WAVETABLE);
	opHolder_ = prepareHolder(current_.path);
	opMakeWaveTable_ = makeWaveTable_;
	opFn_ = &SampleClaim::runWaveTableOp;
	op_ = {};
	opDispatched_ = false;
	dispatchOp();
	phase_ = Phase::AwaitWavetable;
	return false;
}

bool SampleClaim::stepAwaitWavetable() {
	if (!opDispatched_) {
		dispatchOp();
		return false;
	}
	if (!op_.done) {
		return false;
	}
	opDispatched_ = false;

	Error error = op_.error;
	if (error != Error::NONE) {
		// If word has come back that this file isn't wanting to load as a WaveTable...
		if (error == Error::FILE_NOT_LOADABLE_AS_WAVETABLE
		    || error == Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO) {
			// If that was what the user really specified they wanted, and we couldn't do it, then we have to tell them
			// no. Or if they don't really mind, just load it as a Sample.
			if (current_.mayDoWaveTable == 2 || numTypesTried_ > 1
			    || soundEditor.currentSound->getSynthMode() == SynthMode::RINGMOD) {
				end(false, error);
			}
			else {
				phase_ = Phase::PrepareSample;
			}
			return true;
		}
		end(false, error);
		return true;
	}

	if (op_.file != nullptr) {
		opHolder_->setAudioFile(op_.file, soundEditor.currentSource->sampleControls.isCurrentlyReversed(),
		                        /*manuallySelected=*/true, CLUSTER_ENQUEUE);
	}

	// if oscillator wasn't already a wavetable, update custom knob assignments, otherwise leave as is
	if (oldOscType_ != OscType::WAVETABLE) {
		if (soundEditor.currentSourceIndex == 0) { // Osc 1
			soundEditor.currentSound->modKnobs[7][1].paramDescriptor.setToHaveParamOnly(params::LOCAL_OSC_A_WAVE_INDEX);

			if (!soundEditor.currentSound->modKnobs[7][0].paramDescriptor.isSetToParamWithNoSource(
			        params::LOCAL_OSC_B_WAVE_INDEX)) {
				soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamAndSource(
				    params::LOCAL_OSC_A_WAVE_INDEX, PatchSource::LFO_LOCAL_1);
			}
		}
		else { // Osc 2
			soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamOnly(params::LOCAL_OSC_B_WAVE_INDEX);
		}
		getCurrentOutput()->modKnobMode = 7;
	}
	view.setKnobIndicatorLevels(); // Visually update.
	view.setModLedStates();

	phase_ = Phase::Finish;
	return true;
}

bool SampleClaim::stepDetectPitch() {
	opSample_ = (Sample*)claimHolder()->audioFile;
	opSingleCycle_ = doingSingleCycle_;
	opFn_ = &SampleClaim::runPitchOp;
	op_ = {};
	opDispatched_ = false;
	dispatchOp();
	phase_ = Phase::AwaitPitch;
	return false;
}

bool SampleClaim::stepAwaitPitch() {
	if (!opDispatched_) {
		dispatchOp();
		return false;
	}
	if (!op_.done) {
		return false;
	}
	opDispatched_ = false;

	auto* range = (MultisampleRange*)soundEditor.currentMultiRange;
	const float midiNote = ((Sample*)range->sampleHolder.audioFile)->midiNote;
	// The operation worked the note out. A note it could not find is left as it is: asking again here would scan the
	// sample on this tier, and would leave the transpose alone just the same.
	if (midiNote != MIDI_NOTE_ERROR && midiNote != MIDI_NOTE_UNSET) {
		bool shouldMinimizeOctaves = (std::ssize(soundEditor.currentSource->ranges) == 1);
		range->sampleHolder.setTransposeAccordingToSamplePitch(shouldMinimizeOctaves, doingSingleCycle_);
	}

	removeWaveTableKnobs();
	phase_ = Phase::Finish;
	return true;
}

bool SampleClaim::stepFinish() {
	if (!claimingAudioClip()) {
		sampleBrowser.audioFileIsNowSet();

		getCurrentInstrument()->beenEdited();

		// If there was only one MultiRange, don't go back to the range menu (that's the BOT-TOP thing).
		if (std::ssize(soundEditor.currentSource->ranges) <= 1 && soundEditor.navigationDepth
		    && soundEditor.menuItemNavigationRecord[soundEditor.navigationDepth - 1] == &menu_item::multiRangeMenu) {
			soundEditor.navigationDepth--;
		}
	}

	if (!current_.loadWithoutExiting) {
		sampleBrowser.exitAndNeverDeleteDrum();

		if (sampleBrowser.menuItemHeadingTo != nullptr && sampleBrowser.parentMenuHeadingTo != nullptr) {
			if (isUIOpen(&soundEditor)) {
				closeUI(&soundEditor);
			}

			sampleBrowser.parentMenuHeadingTo->focusChild(sampleBrowser.menuItemHeadingTo);
			soundEditor.menuItemNavigationRecord[0] = sampleBrowser.parentMenuHeadingTo;
			soundEditor.navigationDepth = 0;
			openUI(&soundEditor);

			sampleBrowser.parentMenuHeadingTo = nullptr;
			sampleBrowser.menuItemHeadingTo = nullptr;
		}

		uiNeedsRendering(&audioClipView);
	}
	display->removeWorkingAnimation();
	animationShown_ = false;
	finishes_++;
	end(true, Error::NONE);
	return true;
}

void SampleClaim::end(bool success, Error error) {
	if (!success) {
		if (animationShown_) {
			display->removeLoadingAnimation();
		}
		if (error != Error::NONE) {
			display->displayError(error);
		}
	}
	animationShown_ = false;
	load_.cancel();
	if (current_.origin == ClaimOrigin::Explicit
	    && !(queued_.has_value() && queued_->origin == ClaimOrigin::Explicit)) {
		openGate();
	}
	const auto completion = current_.completion;
	phase_ = Phase::Idle;
	current_ = {};
	if (completion != nullptr) {
		completion(success);
	}
}

void SampleClaim::dispatchOp() {
	// Marked before it is queued, so an operation that runs at once still clears it.
	opRunning_ = true;
	opDispatched_ = deluge::storage::Owner::run(opFn_, this);
	if (!opDispatched_) {
		opRunning_ = false;
	}
}

void SampleClaim::runWaveTableOp(void* self) {
	auto* claim = static_cast<SampleClaim*>(self);
	// Counted for SC-TIER: a wavetable lookup on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		claim->waveTableLookupsOnOwner_++;
	}
	Error error = Error::NONE;
	AudioFile* file = claim->opHolder_->acquire(/*mayActuallyReadFile=*/true, &error, claim->opMakeWaveTable_,
	                                            deluge::storage::AsyncLoadMode::Blocking, /*context=*/nullptr);
	claim->op_.file = file;
	claim->op_.error = error;
	claim->op_.done = true;
	claim->opRunning_ = false;
}

void SampleClaim::runPitchOp(void* self) {
	auto* claim = static_cast<SampleClaim*>(self);
	// Counted for SC-TIER: a pitch detection on the storage owner.
	if (deluge::storage::Owner::on_owner()) {
		claim->pitchScansOnOwner_++;
	}
	claim->opSample_->workOutMIDINote(claim->opSingleCycle_);
	claim->op_.done = true;
	claim->opRunning_ = false;
}

void SampleClaim::removeWaveTableKnobs() {
	// Anyway, by now we know we've loaded as a Sample, not a Wavetable. So remove WaveTable gold knob assignments.
	bool anyChange = false;
	int32_t p = params::LOCAL_OSC_A_WAVE_INDEX + soundEditor.currentSourceIndex;
	if (soundEditor.currentSound->modKnobs[7][0].paramDescriptor.getJustTheParam() == p) {
		soundEditor.currentSound->modKnobs[7][0].paramDescriptor.setToHaveParamOnly(params::UNPATCHED_BITCRUSHING
		                                                                            + params::UNPATCHED_START);
		anyChange = true;
	}
	if (soundEditor.currentSound->modKnobs[7][1].paramDescriptor.getJustTheParam() == p) {
		soundEditor.currentSound->modKnobs[7][1].paramDescriptor.setToHaveParamOnly(
		    params::UNPATCHED_SAMPLE_RATE_REDUCTION + params::UNPATCHED_START);
		anyChange = true;
	}

	if (anyChange) {
		getCurrentOutput()->modKnobMode = 1;
		view.setKnobIndicatorLevels(); // Visually update.
		view.setModLedStates();
	}
}

} // namespace deluge::gui
