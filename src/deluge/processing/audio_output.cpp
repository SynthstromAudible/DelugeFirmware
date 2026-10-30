/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "processing/audio_output.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "deluge-project/doc_output.h" // complete DocOutput type for the identity record
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/mix/track_monitor.h"
#include "dsp/stereo_sample.h"
#include "gui/views/view.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include "util/lookuptables/lookuptables.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <ranges>
#include <span>

namespace params = deluge::modulation::params;
EnumStringMap<AudioOutputMode, kNumAudioOutputModes> aoModeStringMap({{
    {AudioOutputMode::player, "Player"},
    {AudioOutputMode::sampler, "Sampler"},
    {AudioOutputMode::looper, "Looper/FX"},
}});
AudioOutputMode stringToAOMode(char const* string) {
	return aoModeStringMap(string);
}
char const* aoModeToString(AudioOutputMode mode) {
	return aoModeStringMap(mode);
}
AudioOutput::AudioOutput() : Output(OutputType::AUDIO) {
	modKnobMode = 0;
	inputChannel = AudioInputChannel::LEFT;
	mode = AudioOutputMode::player;
}

AudioOutput::~AudioOutput() {
	releaseMonitoringClaim();
}

void AudioOutput::cloneFrom(ModControllableAudio* other) {
	GlobalEffectableForClip::cloneFrom(other);
	auto ao = ((AudioOutput*)other);
	inputChannel = ao->inputChannel;

	auto modeInt = util::to_underlying(ao->mode);

	mode = static_cast<AudioOutputMode>(std::clamp<int>(modeInt, 0, kNumAudioOutputModes - 1));
	outputRecordingFrom = nullptr;
	// old style cloning overdubs
	if (mode == AudioOutputMode::looper || mode == AudioOutputMode::sampler) {
		// if the original track hasn't been recorded into then we'll just be a player. Avoids doubling monitoring
		if (ao->isEmpty()) {
			mode = AudioOutputMode::player;
		}
		else {
			// otherwise we'll become the new sampler/looper and the og will become a player
			ao->mode = AudioOutputMode::player;
		}
	}

	if (inputChannel == AudioInputChannel::SPECIFIC_OUTPUT) {
		outputRecordingFrom = ao->outputRecordingFrom;
		// now steal the monitoring of the original track if necessary (i.e. we're a looper or sampler)
		updateMonitoringClaim();
	}
}

void AudioOutput::renderOutput(ModelStack* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
                               int32_t reverbAmountAdjust, int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
                               bool isClipActive) {

	ParamManager* paramManager = getParamManager(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);

	GlobalEffectableForClip::renderOutput(modelStackWithTimelineCounter, paramManager, output, reverbBuffer,
	                                      reverbAmountAdjust, sideChainHitPending, shouldLimitDelayFeedback,
	                                      isClipActive, OutputType::AUDIO, recorder);
}

void AudioOutput::resetEnvelope() {
	if (activeClip) {
		AudioClip* activeAudioClip = (AudioClip*)activeClip;

		bool directlyToDecay = (activeAudioClip->attack() == -2147483648);
		envelope.noteOn(directlyToDecay, 2147483647);
	}
	amplitudeLastTime = 0;
	overrideAmplitudeEnvelopeReleaseRate = 0;
}

bool AudioOutput::modeAllowsMonitoring() const {
	if (mode == AudioOutputMode::player) {
		return false;
	}
	if (mode == AudioOutputMode::sampler) {
		// Reachable with a null activeClip via the SPECIFIC_OUTPUT branch, which renders a record-source output
		// directly and so bypasses the inValidState guard in Song::renderAudio.
		if (!activeClip) {
			return false;
		}
		auto& activeAudioClip = static_cast<AudioClip&>(*activeClip);
		return activeAudioClip.isEmpty();
	}
	if (mode == AudioOutputMode::looper) {
		return true;
	}
	return false;
}

// Beware - unlike usual, modelStack, a ModelStackWithThreeMainThings*,  might have a NULL timelineCounter
bool AudioOutput::renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack,
                                                std::span<StereoSample> render, int32_t* bufferToTransferTo,
                                                int32_t* reverbBuffer, int32_t reverbAmountAdjust,
                                                int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
                                                bool isClipActive, int32_t pitchAdjust, int32_t amplitudeAtStart,
                                                int32_t amplitudeAtEnd) {
	bool rendered = false;
	// audio outputs can have an activeClip while being muted
	if (isClipActive) {
		auto& activeAudioClip = static_cast<AudioClip&>(*activeClip);
		if (activeAudioClip.voiceSample) {

			int32_t attackNeutralValue = paramNeutralValues[params::LOCAL_ENV_0_ATTACK];
			int32_t attack = getExp(attackNeutralValue, -(activeAudioClip.attack() / 4));

renderEnvelope:
			int32_t amplitudeLocal =
			    (envelope.render(render.size(), attack, 8388608, 2147483647, 0, decayTableSmall4) >> 1) + 1073741824;

			if (envelope.stage() >= Envelope::Stage::OFF) {
				if (activeAudioClip.doingLateStart) {
					resetEnvelope();
					goto renderEnvelope;
				}

				else {
					// I think we can only be here for one shot audio clips, so maybe we shouldn't keep it?
					activeAudioClip.unassignVoiceSample(false);
				}
			}

			else {

				if (!amplitudeLastTime && attack > 245632) {
					amplitudeLastTime = amplitudeLocal;
				}

				int32_t amplitudeEffectiveStart =
				    multiply_32x32_rshift32(amplitudeLastTime, amplitudeAtStart); // Reduces amplitude another >>1
				int32_t amplitudeEffectiveEnd =
				    multiply_32x32_rshift32(amplitudeLocal, amplitudeAtEnd); // Reduces amplitude another >>1

				int32_t amplitudeIncrementEffective =
				    static_cast<int32_t>(static_cast<double>(amplitudeEffectiveEnd - amplitudeEffectiveStart)
				                         / static_cast<double>(render.size()));

				std::span render_mono{reinterpret_cast<q31_t*>(render.data()), render.size()};
				activeAudioClip.render(modelStack, render_mono, amplitudeEffectiveStart, amplitudeIncrementEffective,
				                       pitchAdjust);
				rendered = true;
				amplitudeLastTime = amplitudeLocal;

				// If we need to duplicate mono to stereo...
				if (AudioOutput::willRenderAsOneChannelOnlyWhichWillNeedCopying()) {
					// If can write directly into Song buffer...
					if (bufferToTransferTo != nullptr) {
						std::ranges::transform(render_mono, reinterpret_cast<StereoSample*>(bufferToTransferTo),
						                       StereoSample::fromMono);
					}

					// Or, if duplicating within same rendering buffer (cos there's FX to be applied)
					else {
						std::ranges::transform(render_mono.rbegin(), render_mono.rend(), render.rbegin(),
						                       StereoSample::fromMono);
					}
				}
			}
		}
	}

	std::span<StereoSample> output = (bufferToTransferTo != nullptr)
	                                     ? std::span{reinterpret_cast<StereoSample*>(bufferToTransferTo), render.size()}
	                                     : render;

	// add in the monitored audio if in sampler or looper mode. Suppress until the codec ADC has settled after
	// power-on, then fade it in (issue #4517) - otherwise its power-on transient gets passed straight through to the
	// output.
	q31_t const monitoring_gain_end = AudioEngine::inputMonitor.gain(AudioEngine::audioSampleTimer + render.size());
	if (monitoring_gain_end != 0 && modeAllowsMonitoring() && modelStack->song->isOutputActiveInArrangement(this)
	    && inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		rendered = true;
		int32_t const* __restrict__ input_ptr = AudioEngine::getInputReadPos(inputChannel);
		int32_t const* const input_end = AudioEngine::getInputBufferEnd(inputChannel);

		AudioInputChannel input_channel = this->inputChannel;
		if (input_channel == AudioInputChannel::USB) {
			input_channel = AudioInputChannel::STEREO; // Just a stereo input, from a different buffer
			AudioEngine::usbInputMonitoredByTrack = true;
		}
		if (input_channel == AudioInputChannel::STEREO && !AudioEngine::renderInStereo) {
			input_channel = AudioInputChannel::NONE; // 0 means combine channels
		}

		int32_t amplitude_start = amplitudeAtStart;
		int32_t amplitude_end = amplitudeAtEnd;
		if (!AudioEngine::inputMonitor.warmed_up()) {
			amplitude_start =
			    multiply_32x32_rshift32(amplitude_start, AudioEngine::inputMonitor.gain(AudioEngine::audioSampleTimer))
			    << 1;
			amplitude_end = multiply_32x32_rshift32(amplitude_end, monitoring_gain_end) << 1;
		}

		using deluge::dsp::fixed::TrackMonitor;
		TrackMonitor::Mode mode = TrackMonitor::Mode::STEREO;
		switch (input_channel) {
		case AudioInputChannel::LEFT:
			mode = TrackMonitor::Mode::LEFT;
			break;
		case AudioInputChannel::RIGHT:
			mode = TrackMonitor::Mode::RIGHT;
			break;
		case AudioInputChannel::BALANCED:
			mode = TrackMonitor::Mode::BALANCED;
			break;
		case AudioInputChannel::NONE: // Means combine channels
			mode = TrackMonitor::Mode::COMBINE;
			break;
		default: // STEREO. There is no case for echoing the "output" channel: you are already hearing it
			break;
		}

		// The block's input frames, copied out of the ring in the two segments a wrap leaves. A render is at most
		// SSI_TX_BUFFER_NUM_SAMPLES frames, and the ring far longer, so it wraps at most once.
		static_assert(NUM_MONO_INPUT_CHANNELS == 2, "the input ring holds stereo frames");
		const int32_t* const ring_start = input_end - (SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS);
		std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> input_frames;
		const std::span<StereoSample> input = std::span{input_frames}.first(output.size());
		const std::size_t before_wrap =
		    std::min<std::size_t>(input.size(), (input_end - input_ptr) / NUM_MONO_INPUT_CHANNELS);
		std::memcpy(input.data(), input_ptr, before_wrap * sizeof(StereoSample));
		std::memcpy(input.data() + before_wrap, ring_start, (input.size() - before_wrap) * sizeof(StereoSample));

		deluge::dsp::Mono<TrackMonitor> monitor{{
		    .amplitude_start = amplitude_start,
		    .amplitude_end = amplitude_end,
		    .mode = mode,
		}};
		monitor.renderBlock(output, input, output);
	}
	// Only render the record-source if it has an active clip: Output::renderOutput is documented to be called only
	// when there's an activeClip, but this branch bypasses the inValidState guard in Song::renderAudio.
	else if (modeAllowsMonitoring() && modelStack->song->isOutputActiveInArrangement(this)
	         && inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && (outputRecordingFrom != nullptr)
	         && (outputRecordingFrom->getActiveClip() != nullptr)) {
		rendered = true;
		std::byte modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* songModelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		outputRecordingFrom->renderOutput(songModelStack, output, reverbBuffer, reverbAmountAdjust, sideChainHitPending,
		                                  shouldLimitDelayFeedback, isClipActive);
	}
	return rendered;
}

bool AudioOutput::willRenderAsOneChannelOnlyWhichWillNeedCopying() {
	return (activeClip && ((AudioClip*)activeClip)->voiceSample
	        && (((AudioClip*)activeClip)->sampleHolder.audioFile->numChannels == 1 || !AudioEngine::renderInStereo));
}

void AudioOutput::cutAllSound() {
	if (activeClip) {
		((AudioClip*)activeClip)->unassignVoiceSample(false);
		((AudioClip*)activeClip)->abortRecording(); // Needed for when this is being called as part of a song-swap - we
		                                            // can't leave recording happening in such a case.
	}
}

void AudioOutput::getThingWithMostReverb(Sound** soundWithMostReverb,
                                         ParamManagerForTimeline** paramManagerWithMostReverb, Kit** kitWithMostReverb,
                                         int32_t* highestReverbAmountFound) {
}

// Unlike for Instruments, AudioOutputs will only be written as part of a Song, so clipForSavingOutputOnly will always
// be NULL
bool AudioOutput::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {

	// Read the DOM identity record's name when bound (rebuilt at save); fall back to the native field when
	// unbound.
	writer.writeAttribute("name", docOutput_ != nullptr ? docOutput_->name.c_str() : name.c_str());

	writer.writeAttribute("mode", aoModeToString(mode));

	writer.writeAttribute("inputChannel", inputChannelToString(inputChannel));
	// getOutputIndex() returns 0 for an output that isn't in the song, which would reload as recording from the first
	// track - possibly one that records from us. Save -1 (no source) instead.
	int32_t outputRecordingIndex = currentSong->getOutputIndex(outputRecordingFrom);
	if (!outputRecordingFrom || currentSong->getOutputFromIndex(outputRecordingIndex) != outputRecordingFrom) {
		outputRecordingIndex = -1;
	}
	writer.writeAttribute("outputRecordingIndex", outputRecordingIndex);
	Output::writeDataToFile(writer, clipForSavingOutputOnly, song);

	GlobalEffectableForClip::writeAttributesToFile(writer, clipForSavingOutputOnly == nullptr);

	writer.writeOpeningTagEnd();

	ParamManager* paramManager = nullptr;
	// If no activeClip, that means no Clip has this Instrument, so there should be a backedUpParamManager that we
	// should use / save
	if (!activeClip) {
		paramManager = song->live.param_backup.getPreferablyWithClip(this, NULL);
	}

	GlobalEffectableForClip::writeTagsToFile(writer, paramManager, true);

	return true;
}

// clip will always be NULL and is of no consequence - see note in parent output.h
Error AudioOutput::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
	const deluge::storage::Value* element = deluge::storage::treeReaderOf(reader).currentElement();
	if (element == nullptr) {
		return Error::FILE_CORRUPTED;
	}
	char const* tagName;

	ParamManagerForTimeline paramManager;

	while (*(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "echoingInput")) {
			int echoing = reader.readTagOrAttributeValueInt();
			if (echoing) {
				// loopers behave like old monitored clips
				mode = AudioOutputMode::looper;
			}
			reader.exitTag("echoingInput");
		}
		else if (!strcmp(tagName, "mode")) {
			mode = stringToAOMode(reader.readTagOrAttributeValue());
			reader.exitTag("mode");
		}

		else if (!strcmp(tagName, "inputChannel")) {
			inputChannel = stringToInputChannel(reader.readTagOrAttributeValue());
			reader.exitTag("inputChannel");
		}

		else if (!strcmp(tagName, "outputRecordingIndex")) {
			outputRecordingFromIndex = reader.readTagOrAttributeValueInt();
		}

		else if (Output::readTagFromFile(reader, tagName)) {}

		else {

			Error result = GlobalEffectableForClip::readTagFromFile(reader, tagName, &paramManager, 0, nullptr, song);
			if (result == Error::NONE) {}
			else if (result == Error::RESULT_TAG_UNUSED) {
				reader.exitTag();
			}
			else {
				return result;
			}
		}
	}

	readEffectsFromElement(*element, song->getInputTickMagnitude());

	if (paramManager.containsAnyMainParamCollections()) {
		song->live.param_backup.backUp(this, NULL, &paramManager);
	}

	return Error::NONE;
}

void AudioOutput::deleteBackedUpParamManagers(Song* song) {
	song->live.param_backup.deleteForModControllable(this);
}

Clip* AudioOutput::createNewClipForArrangementRecording(ModelStack* modelStack) {

	// Allocate memory for audio clip
	void* clipMemory = deluge::memory::alloc_fast(sizeof(AudioClip));
	if (!clipMemory) {
		return nullptr;
	}

	AudioClip* newClip = new (clipMemory) AudioClip(*modelStack->song);
	Error error = newClip->setOutput(modelStack->addTimelineCounter(newClip), this);

	// A Clip without param collections would crash as soon as its params are accessed. Bail in all builds - the
	// caller handles NULL as insufficient RAM.
	if (error != Error::NONE || !newClip->paramManager.summaries[0].paramCollection) {
		if constexpr (ALPHA_OR_BETA_VERSION) {
			if (error == Error::NONE) {
				FREEZE_WITH_ERROR("E422"); // Trying to diversify Leo's E410
			}
		}
		newClip->~AudioClip();
		delugeDealloc(clipMemory);
		return nullptr;
	}

	return newClip;
}

bool AudioOutput::wantsToBeginArrangementRecording() {
	return (inputChannel > AudioInputChannel::NONE && Output::wantsToBeginArrangementRecording());
}

bool AudioOutput::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	if (activeClip
	    && (!modelStack || activeClip != modelStack->getTimelineCounter()
	        || (playbackHandler.playbackState && currentPlaybackMode == &arrangement))) {
		((AudioClip*)activeClip)->unassignVoiceSample(false);
	}
	bool clipChanged = Output::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}

	return clipChanged;
}

bool AudioOutput::isSkippingRendering() {
	return mode == AudioOutputMode::player && (!activeClip || !((AudioClip*)activeClip)->voiceSample);
}

void AudioOutput::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                         GlobalEffectableForClip** globalEffectableWithMostReverb,
                                         int32_t* highestReverbAmountFound) {
	GlobalEffectableForClip::getThingWithMostReverb(activeClip, soundWithMostReverb, paramManagerWithMostReverb,
	                                                globalEffectableWithMostReverb, highestReverbAmountFound);
}

ModelStackWithAutoParam* AudioOutput::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                             int32_t paramID, params::Kind paramKind, bool affectEntire,
                                                             bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);

	if (modelStackWithThreeMainThings) {
		modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}
