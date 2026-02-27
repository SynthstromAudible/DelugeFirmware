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
#include "util/lookuptables/lookuptables.h"
#include <algorithm>
#include <new>
#include <ranges>

extern "C" {
#include "drivers/ssi/ssi.h"
}

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
	if (outputRecordingFrom) {
		outputRecordingFrom->setRenderingToAudioOutput(false, nullptr);
	}
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
		if (mode != AudioOutputMode::player) {
			outputRecordingFrom->setRenderingToAudioOutput(true, this);
		}
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

		bool directlyToDecay = (activeAudioClip->attack == -2147483648);
		envelope.noteOn(directlyToDecay);
	}
	amplitudeLastTime = 0;
	overrideAmplitudeEnvelopeReleaseRate = 0;
}

bool AudioOutput::modeAllowsMonitoring() const {
	if (mode == AudioOutputMode::player) {
		return false;
	}
	if (mode == AudioOutputMode::sampler) {
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
			int32_t attack = getExp(attackNeutralValue, -(activeAudioClip.attack / 4));

renderEnvelope:
			int32_t amplitudeLocal =
			    (envelope.render(render.size(), attack, 8388608, 2147483647, 0, decayTableSmall4) >> 1) + 1073741824;

			if (envelope.state >= EnvelopeStage::OFF) {
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

	// add in the monitored audio if in sampler or looper mode
	if (modeAllowsMonitoring() && modelStack->song->isOutputActiveInArrangement(this)
	    && inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		rendered = true;
		int32_t const* __restrict__ input_ptr = (int32_t const*)AudioEngine::i2sRXBufferPos;

		AudioInputChannel input_channel = this->inputChannel;
		if (input_channel == AudioInputChannel::STEREO && !AudioEngine::renderInStereo) {
			input_channel = AudioInputChannel::NONE; // 0 means combine channels
		}

		auto amplitude_increment = static_cast<int32_t>((static_cast<double>(amplitudeAtEnd - amplitudeAtStart) //
		                                                 / static_cast<double>(render.size())));
		int32_t amplitude = amplitudeAtStart;

		for (StereoSample& output_sample : output) {
			amplitude += amplitude_increment;

			StereoSample input = {
			    .l = multiply_32x32_rshift32(input_ptr[0], amplitude) << 2,
			    .r = multiply_32x32_rshift32(input_ptr[1], amplitude) << 2,
			};

			switch (input_channel) {
			case AudioInputChannel::LEFT:
				output_sample += StereoSample::fromMono(input.l);
				break;

			case AudioInputChannel::RIGHT:
				output_sample += StereoSample::fromMono(input.r);
				break;

			case AudioInputChannel::BALANCED:
				output_sample += StereoSample::fromMono((input.l / 2) - (input.r / 2));
				break;

			case AudioInputChannel::NONE: // Means combine channels
				output_sample += StereoSample::fromMono((input.l / 2) + (input.r / 2));
				break;

			default: // STEREO
				output_sample += input;
				break;
				// Remember, there is no case for echoing out the "output" channel - that function doesn't exist, cos
				// you're obviously already hearing the output channel
			}

			input_ptr += NUM_MONO_INPUT_CHANNELS;
			if (input_ptr >= getRxBufferEnd()) {
				input_ptr -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		}
	}
	else if (modeAllowsMonitoring() && modelStack->song->isOutputActiveInArrangement(this)
	         && inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && (outputRecordingFrom != nullptr)) {
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

	writer.writeAttribute("name", name.get());

	writer.writeAttribute("mode", aoModeToString(mode));

	writer.writeAttribute("inputChannel", inputChannelToString(inputChannel));
	writer.writeAttribute("outputRecordingIndex", currentSong->getOutputIndex(outputRecordingFrom));
	Output::writeDataToFile(writer, clipForSavingOutputOnly, song);

	GlobalEffectableForClip::writeAttributesToFile(writer, clipForSavingOutputOnly == nullptr);

	writer.writeOpeningTagEnd();

	ParamManager* paramManager = nullptr;
	// If no activeClip, that means no Clip has this Instrument, so there should be a backedUpParamManager that we
	// should use / save
	if (!activeClip) {
		paramManager = song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
	}

	GlobalEffectableForClip::writeTagsToFile(writer, paramManager, true);

	return true;
}

// clip will always be NULL and is of no consequence - see note in parent output.h
Error AudioOutput::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {
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

	if (paramManager.containsAnyMainParamCollections()) {
		song->backUpParamManager(this, NULL, &paramManager);
	}

	return Error::NONE;
}

void AudioOutput::deleteBackedUpParamManagers(Song* song) {
	song->deleteBackedUpParamManagersForModControllable(this);
}

Clip* AudioOutput::createNewClipForArrangementRecording(ModelStack* modelStack) {

	// Allocate memory for audio clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AudioClip));
	if (!clipMemory) {
		return nullptr;
	}

	AudioClip* newClip = new (clipMemory) AudioClip();
	newClip->setOutput(modelStack->addTimelineCounter(newClip), this);

#if ALPHA_OR_BETA_VERSION
	if (!newClip->paramManager.summaries[0].paramCollection) {
		FREEZE_WITH_ERROR("E422"); // Trying to diversify Leo's E410
	}
#endif

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
