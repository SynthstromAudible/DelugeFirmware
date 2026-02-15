/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "model/instrument/melodic_instrument.h"
#include "modulation/arpeggiator.h"
#include "processing/sound/sound.h"

class ParamManagerForTimeline;
class ParamManagerForTimeline;
class ModelStack;
class ModelStackWithThreeMainThings;

class SoundInstrument final : public Sound, public MelodicInstrument {
public:
	SoundInstrument();
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) override;
	Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) override;
	void cutAllSound() override;
	bool noteIsOn(int32_t noteCode, bool resetTimeEntered);

	void renderOutput(ModelStack* modelStack, std::span<StereoSample> buffer, int32_t* reverbBuffer,
	                  int32_t reverbAmountAdjust, int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
	                  bool isClipActive) override;

	// A timelineCounter is required
	void offerReceivedCCToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack) override {
		Sound::offerReceivedCCToLearnedParamsForClip(cable, channel, ccNumber, value, modelStack);
	}
	bool offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack) override {
		return Sound::offerReceivedPitchBendToLearnedParams(cable, channel, data1, data2, modelStack);
	}

	Error loadAllAudioFiles(bool mayActuallyReadFiles) override;
	void resyncLFOs() override;
	ModControllable* toModControllable() override;
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) override;
	void setupPatchingForAllParamManagers(Song* song) override;
	void setupPatching(ModelStackWithTimelineCounter* modelStack) override;

	void deleteBackedUpParamManagers(Song* song) override;
	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                              int32_t channelOrNoteNumber,
	                                              MIDICharacteristic whichCharacteristic) override;
	void monophonicExpressionEvent(int32_t newValue, int32_t expressionDimension) override;

	void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCode, int16_t const* mpeValues,
	              int32_t fromMIDIChannel, uint8_t velocity, uint32_t sampleSyncLength, int32_t ticksLate,
	              uint32_t samplesLate) override;

	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = nullptr) override;
	bool readTagFromFile(Deserializer& reader, char const* tagName) override;

	void prepareForHibernationOrDeletion() override;
	void compensateInstrumentVolumeForResonance(ModelStackWithThreeMainThings* modelStack) override;
	bool isSkippingRendering() override { return skippingRendering; }
	void loadCrucialAudioFilesOnly() override;
	void beenEdited(bool shouldMoveToEmptySlot = true) override;
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) override;
	void setupWithoutActiveClip(ModelStack* modelStack) override;
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound) override;
	uint8_t* getModKnobMode() override { return &modKnobMode; }
	ArpeggiatorBase* getArp() override;
	char const* getXMLTag() override { return "sound"; }
	const char* getName() override { return name.get(); }

	ArpeggiatorSettings defaultArpSettings;
};
