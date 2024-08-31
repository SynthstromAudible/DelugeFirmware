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
#include "definitions_cxx.hpp"
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
#include <new>

extern "C" {
#include "drivers/ssi/ssi.h"
}

namespace params = deluge::modulation::params;

AudioOutput::AudioOutput() : Output(OutputType::AUDIO) {
	modKnobMode = 0;
	inputChannel = AudioInputChannel::LEFT;
	echoing = false;
}

AudioOutput::~AudioOutput() {
	if (outputRecordingFrom) {
		outputRecordingFrom->setRenderingToAudioOutput(false, nullptr);
	}
}

void AudioOutput::cloneFrom(ModControllableAudio* other) {
	GlobalEffectableForClip::cloneFrom(other);

	inputChannel = ((AudioOutput*)other)->inputChannel;
	outputRecordingFrom = ((AudioOutput*)other)->outputRecordingFrom;
}

void AudioOutput::renderOutput(ModelStack* modelStack, StereoSample* outputBuffer, StereoSample* outputBufferEnd,
                               int32_t numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
                               int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive) {

	ParamManager* paramManager = getParamManager(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);

	GlobalEffectableForClip::renderOutput(modelStackWithTimelineCounter, paramManager, outputBuffer, numSamples,
	                                      reverbBuffer, reverbAmountAdjust, sideChainHitPending,
	                                      shouldLimitDelayFeedback, isClipActive, OutputType::AUDIO, recorder);
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

// Beware - unlike usual, modelStack, a ModelStackWithThreeMainThings*,  might have a NULL timelineCounter
bool AudioOutput::renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack, StereoSample* renderBuffer,
                                                int32_t* bufferToTransferTo, int32_t numSamples, int32_t* reverbBuffer,
                                                int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                                bool shouldLimitDelayFeedback, bool isClipActive, int32_t pitchAdjust,
                                                int32_t amplitudeAtStart, int32_t amplitudeAtEnd) {
	bool rendered = false;
	// audio outputs can have an activeClip while being muted
	if (isClipActive) {
		AudioClip* activeAudioClip = (AudioClip*)activeClip;
		if (activeAudioClip->voiceSample) {

			int32_t attackNeutralValue = paramNeutralValues[params::LOCAL_ENV_0_ATTACK];
			int32_t attack = getExp(attackNeutralValue, -(activeAudioClip->attack >> 2));

renderEnvelope:
			int32_t amplitudeLocal =
			    (envelope.render(numSamples, attack, 8388608, 2147483647, 0, decayTableSmall4) >> 1) + 1073741824;

			if (envelope.state >= EnvelopeStage::OFF) {
				if (activeAudioClip->doingLateStart) {
					resetEnvelope();
					goto renderEnvelope;
				}

				else {
					// I think we can only be here for one shot audio clips, so maybe we shouldn't keep it?
					activeAudioClip->unassignVoiceSample(false);
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

				int32_t amplitudeIncrementEffective = (amplitudeEffectiveEnd - amplitudeEffectiveStart) / numSamples;

				int32_t* intBuffer = (int32_t*)renderBuffer;
				activeAudioClip->render(modelStack, intBuffer, numSamples, amplitudeEffectiveStart,
				                        amplitudeIncrementEffective, pitchAdjust);
				rendered = true;
				amplitudeLastTime = amplitudeLocal;

				// If we need to duplicate mono to stereo...
				if (AudioOutput::willRenderAsOneChannelOnlyWhichWillNeedCopying()) {

					// If can write directly into Song buffer...
					if (bufferToTransferTo) {
						int32_t const* __restrict__ input = intBuffer;
						int32_t const* const inputBufferEnd = input + numSamples;
						int32_t* __restrict__ output = bufferToTransferTo;

						int32_t const* const remainderEnd = input + (numSamples & 1);

						while (input != remainderEnd) {
							*output += *input;
							output++;
							*output += *input;
							output++;

							input++;
						}

						while (input != inputBufferEnd) {
							*output += *input;
							output++;
							*output += *input;
							output++;
							input++;

							*output += *input;
							output++;
							*output += *input;
							output++;
							input++;
						}
					}

					// Or, if duplicating within same rendering buffer (cos there's FX to be applied)
					else {
						int32_t i = numSamples - 1;

						int32_t firstStopAt = numSamples & ~3;

						while (i >= firstStopAt) {
							int32_t sampleValue = intBuffer[i];
							intBuffer[(i << 1)] = sampleValue;
							intBuffer[(i << 1) + 1] = sampleValue;
							i--;
						}

						while (i >= 0) {
							{
								int32_t sampleValue = intBuffer[i];
								intBuffer[(i << 1)] = sampleValue;
								intBuffer[(i << 1) + 1] = sampleValue;
								i--;
							}

							{
								int32_t sampleValue = intBuffer[i];
								intBuffer[(i << 1)] = sampleValue;
								intBuffer[(i << 1) + 1] = sampleValue;
								i--;
							}

							{
								int32_t sampleValue = intBuffer[i];
								intBuffer[(i << 1)] = sampleValue;
								intBuffer[(i << 1) + 1] = sampleValue;
								i--;
							}

							{
								int32_t sampleValue = intBuffer[i];
								intBuffer[(i << 1)] = sampleValue;
								intBuffer[(i << 1) + 1] = sampleValue;
								i--;
							}
						}
					}
				}
			}
		}
	}

	if (echoing && modelStack->song->isOutputActiveInArrangement(this)
	    && inputChannel != AudioInputChannel::SPECIFIC_OUTPUT) {
		rendered = true;
		StereoSample* __restrict__ outputPos = bufferToTransferTo ? (StereoSample*)bufferToTransferTo : renderBuffer;
		StereoSample const* const outputPosEnd = outputPos + numSamples;

		int32_t const* __restrict__ inputReadPos = (int32_t const*)AudioEngine::i2sRXBufferPos;

		AudioInputChannel inputChannelNow = inputChannel;
		if (inputChannelNow == AudioInputChannel::STEREO && !AudioEngine::renderInStereo) {
			inputChannelNow = AudioInputChannel::NONE; // 0 means combine channels
		}

		int32_t amplitudeIncrement = (int32_t)((double)(amplitudeAtEnd - amplitudeAtStart) / (double)numSamples);
		int32_t amplitudeNow = amplitudeAtStart;

		do {

			amplitudeNow += amplitudeIncrement;

			int32_t inputL = multiply_32x32_rshift32(inputReadPos[0], amplitudeNow) << 2;
			int32_t inputR = multiply_32x32_rshift32(inputReadPos[1], amplitudeNow) << 2;

			switch (inputChannelNow) {
			case AudioInputChannel::LEFT:
				outputPos->l += inputL;
				outputPos->r += inputL;
				break;

			case AudioInputChannel::RIGHT:
				outputPos->l += inputR;
				outputPos->r += inputR;
				break;

			case AudioInputChannel::BALANCED: {
				int32_t difference = (inputL >> 1) - (inputR >> 1);
				outputPos->l += difference;
				outputPos->r += difference;
				break;
			}

			case AudioInputChannel::NONE: // Means combine channels
			{
				int32_t sum = (inputL >> 1) + (inputR >> 1);
				outputPos->l += sum;
				outputPos->r += sum;
				break;
			}

			default: // STEREO
				outputPos->l += inputL;
				outputPos->r += inputR;

				// Remember, there is no case for echoing out the "output" channel - that function doesn't exist, cos
				// you're obviously already hearing the output channel
			}

			outputPos++;

			inputReadPos += NUM_MONO_INPUT_CHANNELS;
			if (inputReadPos >= getRxBufferEnd()) {
				inputReadPos -= SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS;
			}
		} while (outputPos < outputPosEnd);
	}
	else if (echoing && modelStack->song->isOutputActiveInArrangement(this)
	         && inputChannel == AudioInputChannel::SPECIFIC_OUTPUT && outputRecordingFrom) {
		rendered = true;
		StereoSample* __restrict__ outputBuffer = bufferToTransferTo ? (StereoSample*)bufferToTransferTo : renderBuffer;
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* songModelStack = setupModelStackWithSong(modelStackMemory, currentSong);
		outputRecordingFrom->renderOutput(songModelStack, outputBuffer, outputBuffer + numSamples, numSamples,
		                                  reverbBuffer, reverbAmountAdjust, sideChainHitPending,
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

	if (echoing) {
		writer.writeAttribute("echoingInput", 1);
	}
	writer.writeAttribute("inputChannel", inputChannelToString(inputChannel));
	writer.writeAttribute("outputRecordingIndex", currentSong->getOutputIndex(outputRecordingFrom));
	Output::writeDataToFile(writer, clipForSavingOutputOnly, song);

	GlobalEffectableForClip::writeAttributesToFile(writer, clipForSavingOutputOnly == NULL);

	writer.writeOpeningTagEnd();

	ParamManager* paramManager = NULL;
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
			echoing = reader.readTagOrAttributeValueInt();
			reader.exitTag("echoingInput");
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

			Error result = GlobalEffectableForClip::readTagFromFile(reader, tagName, &paramManager, 0, song);
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
		return NULL;
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
	return !echoing && (!activeClip || !((AudioClip*)activeClip)->voiceSample);
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
