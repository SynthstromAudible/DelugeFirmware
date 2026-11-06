#include "gui/ui/browser/sample_preview.h"

#include "definitions_cxx.hpp"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/global_effectable/output_stage_gain.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/volume_param_gain.h"

#include <utility>

namespace deluge::gui::ui {

namespace params = deluge::modulation::params;

namespace {
/// The target Sound's VELOCITY -> LOCAL_VOLUME patch cable, or nullptr if it has none.
PatchCable* targetVelocityToVolumeCable(ParamManagerForTimeline* paramManager) {
	PatchCableSet* cables = paramManager->getPatchCableSet();
	for (int32_t c = 0; c < cables->numPatchCables; c++) {
		PatchCable& cable = cables->patchCables[c];
		if (cable.from == PatchSource::VELOCITY && cable.destination.is_param()
		    && cable.destination.param_index() == params::LOCAL_VOLUME) {
			return &cable;
		}
	}
	return nullptr;
}
} // namespace

/// The volume the sample will play at once it's loaded into the current instrument / kit-row, so the preview can
/// match it (issue #1591). That's the target Sound's own volume, plus the output stages the preview skips by rendering
/// outside the song: the song's volume stage and, for a kit, the kit's output volume and the fixed gain of its output
/// stage. Master FX apply to both.
///
/// Not modelled: the kit's filter resonance, which its chain folds into its post-FX volume. A resonant kit will still
/// preview slightly off.
AudioEngine::PreviewVolume targetPreviewVolume(const PreviewSound& target) {
	AudioEngine::PreviewVolume previewVolume = AudioEngine::PreviewVolume::neutral();

	if (target.sound == nullptr || target.params == nullptr || !target.params->containsAnyMainParamCollections()) {
		return previewVolume;
	}

	PatchedParamSet* targetParams = target.params->getPatchedParamSet();
	previewVolume.volumePostFX = targetParams->getValue(params::GLOBAL_VOLUME_POST_FX);
	previewVolume.localVolume = targetParams->getValue(params::LOCAL_VOLUME);

	// The preview drum always notes on at velocity 64, so matching both the depth and the polarity of this cable makes
	// the preview's final per-voice volume match the target's. The polarity matters as much as the depth: at velocity
	// 64 a bipolar cable contributes nothing, while a unipolar one contributes about half its depth.
	PatchCable* velocityCable = targetVelocityToVolumeCable(target.params);
	if (velocityCable != nullptr) {
		previewVolume.velocityCableDepth = velocityCable->param.getCurrentValue();
		previewVolume.velocityCablePolarity = velocityCable->polarity;
	}
	else {
		previewVolume.velocityCableDepth = 0;
	}

	// The output stages' combined gain, in the pos domain of util/volume_param_gain.h. The song's volume stage halves
	// its volume - about -6 dB at neutral song volume.
	int32_t outputGainPos = kVolumeParamUnityPos;
	if (currentSong != nullptr) {
		int32_t songVolumeParam = currentSong->paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME);
		outputGainPos = multiplyGainPos(volumeParamToGainPos(songVolumeParam), songOutputStageGainPos());
	}

	if (target.kit && target.clip != nullptr) {
		ParamManagerForTimeline& kitParamManager = target.clip->paramManager;
		if (kitParamManager.containsAnyParamCollectionsIncludingExpression()) {
			int32_t kitVolumeParam = kitParamManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME);
			// The kit's output stage halves its volume, adds a baseline and passes through the filter set's gain
			// adjustment - about -6 dB at neutral kit volume.
			outputGainPos = multiplyGainPos(
			    outputGainPos, multiplyGainPos(volumeParamToGainPos(kitVolumeParam), kitOutputStageGainPos()));
		}
	}

	// Fold the output gain into LOCAL_VOLUME first: it sits at neutral by default, so it has more than the headroom the
	// maximum output gain demands. Anything that doesn't fit - only possible when the Sound's own local volume is
	// already boosted - spills into GLOBAL_VOLUME_POST_FX.
	VolumeParamGain local = applyGainToVolumeParam(previewVolume.localVolume, outputGainPos);
	previewVolume.localVolume = local.param;
	previewVolume.volumePostFX = applyGainToVolumeParam(previewVolume.volumePostFX, local.remainingGainPos).param;
	return previewVolume;
}

void SamplePreviewer::request(std::string path, int32_t movementDirection) {
	wanted_ = Target{
	    .path = std::move(path),
	    .movementDirection = movementDirection,
	};
}

void SamplePreviewer::cancel() {
	wanted_.reset();
	load_.cancel();
	generation_++;
}

std::optional<SamplePreviewer::Landed> SamplePreviewer::tick(const PreviewSound& sound) {
	if (!pending()) {
		return std::nullopt;
	}

	if (wanted_.has_value()) {
		Target target = std::move(*wanted_);
		wanted_.reset();
		start(std::move(target));
	}

	if (auto outcome = load_.tick(); outcome.has_value()) {
		return finish(std::move(*outcome), sound);
	}
	return std::nullopt;
}

void SamplePreviewer::start(Target target) {
	// This more formally does the thing that actually was happening accidentally for ages, as found by Michael B.
	if (lastStarted_ != nullptr) {
		*lastStarted_ = target.path;
	}
	generation_++;

	AudioEngine::stopAnyPreviewing(); // Also unbinds the preview's holder, so the load looks the new path up.
	MultisampleRange* range = (MultisampleRange*)AudioEngine::sampleForPreview->sources[0].getOrCreateFirstRange();
	if (range == nullptr) {
		load_.cancel();
		display->displayError(Error::INSUFFICIENT_RAM);
		return;
	}
	range->sampleHolder.filePath.assign(target.path);
	// CLUSTER_ENQUEUE, not CLUSTER_LOAD_IMMEDIATELY: the immediate mode reads the covered clusters from
	// the card SYNCHRONOUSLY, measured at 38.2-40.1 ms on device. That stalls the audio path (a 9.8 ms
	// late render, plus four more in the same second), which inflates the engine's measured render time,
	// which makes it declare an overload and force-cull a voice -- the very preview voice the fill was
	// warming. That is how a preview truncates.
	//
	// Enqueueing never blocks. The note-on then sees LOADING and defers (SourceSlot), so the
	// preview starts a few ms late instead of either stalling audio or dropping the voice.
	load_.request(
	    deluge::storage::SampleDemand{
	        .holder = &range->sampleHolder,
	        .manuallySelected = true,
	        .clusterLoadInstruction = CLUSTER_ENQUEUE,
	    },
	    /*context=*/nullptr, std::move(target));
}

std::optional<SamplePreviewer::Landed>
SamplePreviewer::finish(deluge::storage::LatestSampleLoad<Target>::Outcome outcome, const PreviewSound& sound) {
	using Result = deluge::storage::LatestSampleLoad<Target>::Result;
	using Reason = deluge::storage::AsyncSampleLoad::Reason;

	if (outcome.result == Result::Failed) {
		// Nothing more can arrive for this load: it stopped moving, or the card cannot be read. Give up as a load that
		// could not reach the card, leaving the display as it is. An abort shows nothing: the browser never loads in
		// the mode that aborts on input.
		if (outcome.reason == Reason::Stalled) {
			giveUps_++;
		}
		if (outcome.reason == Reason::Stalled || outcome.reason == Reason::CardUnusable) {
			display->displayError(Error::SD_CARD);
		}
		return std::nullopt;
	}

	if (outcome.error != Error::NONE) {
		display->displayError(outcome.error); // Rare, shouldn't cause later problems.
	}

	bool shouldActuallySound = false;

	// Decide if we're actually going to sound it.
	if (!instrumentClipView.fileBrowserShouldNotPreview) {
		switch (FlashStorage::sampleBrowserPreviewMode) {
		case PREVIEW_ONLY_WHILE_NOT_PLAYING:
			if (playbackHandler.playbackState) {
				break;
			}
			// No break

		case PREVIEW_ON:
			shouldActuallySound = true;
			break;
		}
	}

	if (shouldActuallySound) {
		AudioEngine::soundPreview(targetPreviewVolume(sound));
	}

	bool hasSample = false;
	Source& source = AudioEngine::sampleForPreview->sources[0];
	if (std::ssize(source.ranges) >= 1) {
		hasSample = ((MultisampleRange*)source.ranges.getElement(0))->sampleHolder.audioFile != nullptr;
	}
	return Landed{.target = std::move(outcome.payload), .hasSample = hasSample, .generation = generation_};
}

} // namespace deluge::gui::ui
