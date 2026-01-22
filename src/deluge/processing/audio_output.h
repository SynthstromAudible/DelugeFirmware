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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "model/global_effectable/global_effectable_for_clip.h"
#include "model/output.h"
#include "modulation/envelope.h"
#include "util/container/enum_to_string_map.hpp"

class ModelStackWithTimelineCounter;

/// Player: plays back a file or samples from input without monitoring
/// Sampler: Monitoring is enabled but disabled after recording. Overdubbing creates a new clip.
/// Looper/FX: monitoring always enabled. Overdubbing overdubs the existing audio
enum class AudioOutputMode : uint8_t { player, sampler, looper };
constexpr int kNumAudioOutputModes = 3;
AudioOutputMode stringToAOMode(char const* string);
char const* aoModeToString(AudioOutputMode mode);

class AudioOutput final : public Output, public GlobalEffectableForClip {
public:
	AudioOutput();
	~AudioOutput() override;
	void cloneFrom(ModControllableAudio* other) override;

	void renderOutput(ModelStack* modelStack, std::span<StereoSample> buffer, int32_t* reverbBuffer,
	                  int32_t reverbAmountAdjust, int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
	                  bool isClipActive) override;

	bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack,
	                                   std::span<StereoSample> globalEffectableBuffer, int32_t* bufferToTransferTo,
	                                   int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                                   bool shouldLimitDelayFeedback, bool isClipActive, int32_t pitchAdjust,
	                                   int32_t amplitudeAtStart, int32_t amplitudeAtEnd) override;

	void resetEnvelope();
	bool matchesPreset(OutputType otherType, int32_t channel, int32_t channelSuffix, char const* otherName,
	                   char const* dirPath) override {
		return false;
	};

	ModControllable* toModControllable() override { return this; }
	uint8_t* getModKnobMode() override { return &modKnobMode; }

	void cutAllSound() override;
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManagerForTimeline** paramManagerWithMostReverb,
	                            Kit** kitWithMostReverb, int32_t* highestReverbAmountFound);

	Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) override;
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) override;
	void deleteBackedUpParamManagers(Song* song) override;
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack,
	                   PgmChangeSend maySendMIDIPGMs = PgmChangeSend::ONCE) override;
	bool isSkippingRendering() override;
	Output* toOutput() override { return this; }
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound) override;

	// A TimelineCounter is required
	void offerReceivedCCToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack) override {
		ModControllableAudio::offerReceivedCCToLearnedParamsForClip(cable, channel, ccNumber, value, modelStack);
	}
	bool offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack) override {
		return ModControllableAudio::offerReceivedPitchBendToLearnedParams(cable, channel, data1, data2, modelStack);
	}

	char const* getXMLTag() override { return "audioTrack"; }

	Envelope envelope;

	int32_t amplitudeLastTime;

	int32_t overrideAmplitudeEnvelopeReleaseRate;

	/// Audio channel used for recording and monitoring.
	AudioInputChannel inputChannel{AudioInputChannel::UNSET};

	/// Only used during loading - index changes as outputs are added/removed and this won't get updated. Pointer stays
	/// accurate through those changes.
	///
	/// int16 so it packs nicely with `echoing` below
	int16_t outputRecordingFromIndex{-1}; // int16 so it fits with the bool and mode

	AudioOutputMode mode{AudioOutputMode::player};

	Output* getOutputRecordingFrom() { return outputRecordingFrom; }
	void clearRecordingFrom() override { setOutputRecordingFrom(nullptr); }
	void setOutputRecordingFrom(Output* toRecordfrom) {
		if (toRecordfrom == this) {
			// can happen from bad save files
			return;
		}
		if (outputRecordingFrom) {
			outputRecordingFrom->setRenderingToAudioOutput(false, nullptr);
		}
		outputRecordingFrom = toRecordfrom;
		if (outputRecordingFrom) {
			// If we are a SAMPLER or a LOOPER then we're monitoring the audio, so tell the other output that we're in
			// charge of rendering
			outputRecordingFrom->setRenderingToAudioOutput(mode != AudioOutputMode::player, this);
		}
	}

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                bool affectEntire, bool useMenuStack) override;
	void scrollAudioOutputMode(int offset) {
		auto modeInt = util::to_underlying(mode);
		modeInt = (modeInt + offset) % kNumAudioOutputModes;

		mode = static_cast<AudioOutputMode>(std::clamp<int>(modeInt, 0, kNumAudioOutputModes - 1));
		if (outputRecordingFrom) {
			// update the output we're recording from on whether we're monitoring
			outputRecordingFrom->setRenderingToAudioOutput(mode != AudioOutputMode::player, this);
		}
		renderUIsForOled(); // oled shows the type on the clip screen (including while holding a clip in song view)
		if (display->have7SEG()) {
			const char* type;
			switch (mode) {
			case AudioOutputMode::player:
				type = "PLAY";
				break;
			case AudioOutputMode::sampler:
				type = "SAMP";
				break;
			case AudioOutputMode::looper:
				type = "LOOP";
			}
			display->displayPopup(type);
		}
	}

protected:
	Clip* createNewClipForArrangementRecording(ModelStack* modelStack) override;
	bool wantsToBeginArrangementRecording() override;
	bool willRenderAsOneChannelOnlyWhichWillNeedCopying() override;

	/// returns true for loopers and samplers without a file
	bool modeAllowsMonitoring() const;
	/// Which output to record from. Only valid when inputChannel is AudioInputChannel::SPECIFIC_OUTPUT.
	Output* outputRecordingFrom{nullptr};
};
