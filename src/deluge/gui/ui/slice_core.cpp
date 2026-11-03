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

#include "gui/ui/slice_core.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/setting_edit.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "model/voice/voice_slot.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "plugin-api/types/param_mapping.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <iterator>

namespace params = deluge::modulation::params;

namespace deluge::gui::ui {

void SliceCore::audition(const Sample& sample, const AuditionTarget& target) {

	if (!target.on) {
		// A release sends its note-off at once and drops any load a press is waiting on: a note-on whose load has not
		// landed must never sound after the finger has lifted. `aReleaseBeforeTheLoadLandsNeverSoundsItsNoteOn` pins
		// it.
		audition_load_.cancel();
		// A press may have pointed the row at the slicer's file for a load this release drops: its record and slot say
		// what it holds.
		deluge::gui::menu_item::recordRanges(*static_cast<SoundDrum*>(getCurrentKit()->firstDrum), 0);
		sound_audition(target);
		return;
	}

	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;
	MultisampleRange* range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();
	deluge::edits::laneSettingFor(*drum, "name", deluge::serialization::Value::makeScalar("1"));
	deluge::gui::menu_item::settingFor(
	    *drum, "osc1/loopMode", deluge::serialization::Value::makeNumber(std::to_underlying(SampleRepeatMode::ONCE)));

	// The path is what says "a different sample is being sliced now"; the null audioFile is what says "its load hasn't
	// landed yet". Both have to ask: a held load leaves the holder naming a file it does not hold yet.
	if (!(sample.filePath == range->sampleHolder.filePath)) {
		unbind_first_drum();
		range->sampleHolder.filePath = sample.filePath.c_str();
	}
	if (range->sampleHolder.audioFile == nullptr) {
		// The load, the params and the note stay one group: the params and the note wait for tickAudition() to see
		// the load land. A newer press replaces this one.
		audition_load_.request(
		    deluge::storage::SampleDemand{
		        .holder = &range->sampleHolder,
		        .manuallySelected = true,
		        .clusterLoadInstruction = CLUSTER_ENQUEUE,
		    },
		    /*context=*/nullptr, target);
		return;
	}
	audition_load_.cancel();
	sound_audition(target);
}

void SliceCore::sound_audition(const AuditionTarget& target) {
	if (target.on) {
		SoundDrum* drum = (SoundDrum*)getCurrentKit()->firstDrum;
		MultisampleRange* range = (MultisampleRange*)drum->sources[0].getOrCreateFirstRange();
		range->sampleHolder.startPos = target.startPoint;
		if (target.endPoint != -1) {
			range->sampleHolder.endPos = target.endPoint;
		}
		range->sampleHolder.transpose = target.transpose;
		// The preview-audition zone edits above changed the range's file/zone/transpose: reflect them into the
		// record.
		deluge::gui::menu_item::recordRanges(*drum, 0);

		alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ModelStackWithParamId* modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
		ModelStackWithAutoParam* modelStackWithAutoParam =
		    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithAutoParam,
		                                                                              deluge::plugin::preset_word(1));
		modelStackWithParamId =
		    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_ATTACK);
		modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
		modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithAutoParam,
		                                                                              deluge::plugin::preset_word(1));
	}
	instrumentClipView.sendAuditionNote(target.on, 0, 64, 0);
}

void SliceCore::tick_audition() {
	using Result = deluge::storage::LatestSampleLoad<AuditionTarget>::Result;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	const auto outcome = audition_load_.tick();
	if (!outcome.has_value()) {
		return; // nothing held, or still loading
	}
	if (outcome->result == Result::Failed) {
		// Nothing more can arrive for this load: it stopped moving, or the card cannot be read. The card error and no
		// note; the next press asks again.
		if (outcome->reason == Reason::Stalled) {
			give_ups_++;
		}
		if (outcome->reason == Reason::Stalled || outcome->reason == Reason::CardUnusable) {
			display->displayError(Error::SD_CARD);
		}
		// The press pointed the first drum at the file: its record says so, landed or not.
		deluge::gui::menu_item::recordRanges(*static_cast<SoundDrum*>(getCurrentKit()->firstDrum), 0);
		return;
	}
	// Bound, or Empty: a holder the lookup left empty sounds nothing, as it always has, with no error.
	sound_audition(outcome->payload);
}

void SliceCore::unbind_first_drum() {
	Kit* kit = getCurrentKit();
	SoundDrum* drum = (SoundDrum*)kit->firstDrum;
	drum->killAllVoices();
	if (std::ssize(drum->sources[0].ranges)) {
		MultisampleRange* range = (MultisampleRange*)drum->sources[0].ranges.getElement(0);
		range->sampleHolder.setAudioFile(nullptr);
		// Reflect the cleared audio file into the record.
		deluge::gui::menu_item::recordRanges(*drum, 0);
	}
}

void SliceCore::kill_voices() {
	getCurrentKit()->firstDrum->killAllVoices();
}

bool SliceCore::restore_full_range() {
	kill_voices();
	SoundDrum* soundDrum = static_cast<SoundDrum*>(getCurrentKit()->firstDrum);
	MultisampleRange* range = static_cast<MultisampleRange*>(soundDrum->sources[0].getOrCreateFirstRange());
	const Sample* sample = static_cast<const Sample*>(range->sampleHolder.audioFile);
	// A first drum that never held a file (manual mode left with nothing auditioned on a fresh kit) has no whole
	// file to give back.
	if (sample == nullptr) {
		return false;
	}
	range->sampleHolder.startPos = 0;
	range->sampleHolder.endPos = sample->lengthInSamples;
	range->sampleHolder.transpose = 0;
	// The zone reset above changed the range: reflect it into the record.
	deluge::gui::menu_item::recordRanges(*soundDrum, 0);
	return true;
}

std::optional<int32_t> SliceCore::play_position() {
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
			const Sound::ActiveVoice& voice =
			    *std::ranges::max_element(valid_voices_view, {}, &VoiceSlot::order_sounded);

			const int32_t u = drum->snapshot().num_unison >> 1;
			deluge::synth::SourceSlot* part = &voice->kernel.unison_parts[u].slots[0];
			if (part != nullptr && part->active) {
				voiceSample = voice->ports(u, 0).sample.voiceSample();
				guide = &voice->guides[soundEditor.currentSourceIndex];
			}
		}
	}

	if (voiceSample == nullptr) {
		return std::nullopt;
	}
	return voiceSample->getPlaySample((Sample*)range->sampleHolder.audioFile, guide);
}

void SliceCore::begin_slice(const Target& target, std::string_view path, Preview preview) {
	pending_ = target;
	if (pending_.isManual) {
		kill_voices(); // stop
	}
	if (preview == Preview::Release) {
		AudioEngine::stopAnyPreviewing();
	}
	else {
		AudioEngine::sampleForPreview->killAllVoices();
	}

	// The first drum names the file, set once here, and the load binds it.
	soundEditor.cutSound();
	AudioFileHolder* holder = soundEditor.getCurrentAudioFileHolder();
	holder->setAudioFile(nullptr);
	holder->filePath = std::string{path};
	slice_load_.request(
	    deluge::storage::SampleDemand{
	        .holder = holder,
	        .reversed = soundEditor.currentSource->sampleControls.isCurrentlyReversed(),
	        .manuallySelected = true,
	        .clusterLoadInstruction = CLUSTER_ENQUEUE,
	    },
	    /*context=*/nullptr, 0);
	slicing_ = true;
}

std::optional<SliceCore::SliceEnd> SliceCore::tick_slice() {
	using Result = deluge::storage::LatestSampleLoad<uint32_t>::Result;

	if (!slicing_) {
		return std::nullopt;
	}
	const auto outcome = slice_load_.tick();
	if (!outcome.has_value()) {
		return std::nullopt; // still loading
	}
	slicing_ = false;

	if (outcome->result == Result::Bound) {
		const Error error = do_slice(pending_.sliceCount);
		if (error != Error::NONE) {
			return SliceEnd{.landed = false, .error = error};
		}
		if (pending_.isManual) {
			// The drums do_slice() made, in order: the loop indexes them, so it runs only once they all exist.
			Kit* kit = getCurrentKit();
			const auto* first = static_cast<MultisampleRange*>(
			    static_cast<SoundDrum*>(kit->firstDrum)->sources[0].getOrCreateFirstRange());
			const uint32_t length = static_cast<const Sample*>(first->sampleHolder.audioFile)->lengthInSamples;
			for (int32_t i = 0; i < pending_.sliceCount; i++) {
				SoundDrum* soundDrum = static_cast<SoundDrum*>(kit->getDrumFromIndex(i));
				MultisampleRange* range = (MultisampleRange*)soundDrum->sources[0].getOrCreateFirstRange();
				range->sampleHolder.startPos = pending_.manualPoints[i].startPos;
				range->sampleHolder.endPos =
				    (i == pending_.sliceCount - 1) ? length : pending_.manualPoints[i + 1].startPos;
				range->sampleHolder.transpose = pending_.manualPoints[i].transpose;
				// The manual-slice zone edits above overwrote this drum's range: reflect them into the record.
				deluge::gui::menu_item::recordRanges(*soundDrum, 0);
			}
		}
		return SliceEnd{.landed = true, .error = Error::NONE};
	}
	// No drums were made, so nothing is painted: Kit::getDrumFromIndex() answers the first drum for an index past the
	// end, so a paint loop here would write every slice's points onto that one drum. The first drum names the file,
	// landed or not, and its record says so.
	deluge::gui::menu_item::recordRanges(*soundEditor.currentSound, soundEditor.currentSourceIndex);
	return SliceEnd{.landed = false, .error = (outcome->result == Result::Empty) ? outcome->error : Error::SD_CARD};
}

Error SliceCore::do_slice(int32_t sliceCount) {
	Kit* kit = getCurrentKit();

	// Do the first Drum

	// Ensure osc type is "sample"
	if (soundEditor.currentSource->oscType != OscType::SAMPLE) {
		soundEditor.currentSound->killAllVoices();
		deluge::gui::menu_item::settingFor(
		    *soundEditor.currentSound, deluge::gui::menu_item::oscSettingPath(soundEditor.currentSourceIndex, "type"),
		    deluge::serialization::Value::makeScalar("sample"));
	}

	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	{
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
		ParamSet* paramSet = (ParamSet*)summary->paramCollection;
		int32_t paramId = params::LOCAL_OSC_A_VOLUME + soundEditor.currentSourceIndex;
		ModelStackWithAutoParam* modelStackWithParam =
		    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

		// Reset osc volume, if it's not automated
		if (!modelStackWithParam->autoParam->isAutomated(*modelStackWithParam->doc())) {
			modelStackWithParam->autoParam->setCurrentValueWithNoReversionOrRecording(modelStackWithParam, 2147483647);
			//((ParamManagerBase*)soundEditor.currentParamManager)->setPatchedParamValue(params::LOCAL_OSC_A_VOLUME +
			// soundEditor.currentSourceIndex, 2147483647, 0xFFFFFFFF, 0, soundEditor.currentSound, currentSong,
			// getCurrentClip(), false);
		}

		SoundDrum* firstDrum = (SoundDrum*)soundEditor.currentSound;

		if (firstDrum->nameIsDiscardable) {
			deluge::edits::laneSettingFor(*firstDrum, "name", deluge::serialization::Value::makeScalar("1"));
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

		const SampleRepeatMode sliceMode =
		    (lengthMSPerSlice < 2002) ? SampleRepeatMode::ONCE : FlashStorage::defaultSliceMode;
		deluge::gui::menu_item::settingFor(*firstDrum, "osc1/loopMode",
		                                   deluge::serialization::Value::makeNumber(std::to_underlying(sliceMode)));

		deluge::gui::menu_item::settingFor(*firstDrum, "osc1/reversed", deluge::serialization::Value::makeNumber(0));
		firstDrum->sources[0].sampleControls.invertReversed = false;

		if (!firstRange->sampleHolder.audioFile) {
			FREEZE_WITH_ERROR("i032"); // Trying to narrow down E368 that Kevin F got
		}

		firstRange->sampleHolder.claimClusterReasons(firstDrum->sources[0].sampleControls.isCurrentlyReversed(),
		                                             CLUSTER_ENQUEUE);
		// The zone edits above set up the first drum's slice range: record them.
		deluge::gui::menu_item::recordRanges(*firstDrum, 0);
		if (doEnvelopes) {
			ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
			ModelStackWithParamId* modelStackWithParamId =
			    modelStack->addParamCollectionAndId(summary->paramCollection, summary, params::LOCAL_ENV_0_RELEASE);
			ModelStackWithAutoParam* modelStackWithAutoParam =
			    modelStackWithParamId->paramCollection->getAutoParamFromId(modelStackWithParamId);
			modelStackWithAutoParam->autoParam->setCurrentValueWithNoReversionOrRecording(
			    modelStackWithAutoParam, deluge::plugin::preset_word(1));
		}

		// Do the rest of the Drums
		for (int32_t i = 1; i < sliceCount; i++) {

			// Make the Drum and its ParamManager
			ParamManagerForTimeline paramManager;
			Error error = paramManager.setupWithPatching();
			if (error != Error::NONE) {
				return error;
			}

			void* drumMemory = deluge::memory::alloc_fast(sizeof(SoundDrum));
			if (!drumMemory) {
				return Error::INSUFFICIENT_RAM;
			}

			SoundDrum* newDrum = new (drumMemory) SoundDrum();

			MultisampleRange* range = (MultisampleRange*)newDrum->sources[0].getOrCreateFirstRange();
			if (!range) {
				AudioEngine::retire(newDrum);
				return Error::INSUFFICIENT_RAM;
			}

			Sound::initParams(&paramManager);

			// The drum is a sample drum before addDrum binds it, so its record starts as one.
			newDrum->setupAsSample(&paramManager);
			kit->addDrum(newDrum);
			// Name after addDrum binds the DocDrum (setName writes the model, which requires binding).
			deluge::edits::laneSettingFor(
			    *newDrum, "name",
			    deluge::serialization::Value::makeScalar(std::string{deluge::string::fromInt(i + 1)}));

			range->sampleHolder.startPos = nextDrumStart;
			nextDrumStart = (uint64_t)lengthInSamples * (i + 1) / sliceCount;
			range->sampleHolder.endPos = nextDrumStart;

			deluge::gui::menu_item::settingFor(*newDrum, "osc1/loopMode",
			                                   deluge::serialization::Value::makeNumber(std::to_underlying(sliceMode)));

			range->sampleHolder.filePath = sample->filePath;
			// The first drum's load bound this sample already: every other slice binds the same one, from memory.
			range->sampleHolder.setAudioFile(sample, false, false, CLUSTER_ENQUEUE);
			// This new drum's slice range was just populated (zone + file): reflect it into the record.
			deluge::gui::menu_item::recordRanges(*newDrum, 0);

			if (doEnvelopes) {
				paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
				    *paramManager.getPatchedParamSet()->docForParam(params::LOCAL_ENV_0_ATTACK),
				    deluge::plugin::preset_word(1));
				if (i != sliceCount - 1) {
					paramManager.getPatchedParamSet()->params[params::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
					    *paramManager.getPatchedParamSet()->docForParam(params::LOCAL_ENV_0_RELEASE),
					    deluge::plugin::preset_word(1));
				}
			}

			// I moved this here, from being earlier/above. Is this fine?
			currentSong->live.param_backup.backUp(newDrum, getCurrentClip(), &paramManager, true);
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

	return Error::NONE;
}

} // namespace deluge::gui::ui
