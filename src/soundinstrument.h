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

#ifndef SOUNDINSTRUMENT_H
#define SOUNDINSTRUMENT_H

#include <sound.h>
#include "MelodicInstrument.h"
#include "Arpeggiator.h"

class ParamManagerForTimeline;
class ParamManagerForTimeline;
class ModelStack;
class ModelStackWithThreeMainThings;

class SoundInstrument final : public Sound, public MelodicInstrument {
public:
	SoundInstrument();
	bool writeDataToFile(Clip* clipForSavingOutputOnly, Song* song);
	int readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos);
	void cutAllSound();
	bool noteIsOn(int noteCode);

	void renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int numSamples,
	                  int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                  bool shouldLimitDelayFeedback, bool isClipActive);

	// A timelineCounter is required
	void offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack) {
		Sound::offerReceivedCCToLearnedParams(fromDevice, channel, ccNumber, value, modelStack);
	}
	bool offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack) {
		return Sound::offerReceivedPitchBendToLearnedParams(fromDevice, channel, data1, data2, modelStack);
	}

	int loadAllAudioFiles(bool mayActuallyReadFiles);
	void resyncLFOs();
	ModControllable* toModControllable();
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs);
	void setupPatchingForAllParamManagers(Song* song);
	char const* getFilePrefix() { return "SYNT"; }
	void setupPatching(ModelStackWithTimelineCounter* modelStack);

	void deleteBackedUpParamManagers(Song* song);
	void polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension, int channelOrNoteNumber,
	                                              int whichCharacteristic);
	void monophonicExpressionEvent(int newValue, int whichExpressionDimension);

	void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int noteCode, int16_t const* mpeValues,
	              int fromMIDIChannel, uint8_t velocity, uint32_t sampleSyncLength, int32_t ticksLate,
	              uint32_t samplesLate);

	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL);
	bool readTagFromFile(char const* tagName);

	void prepareForHibernationOrDeletion();
	void compensateInstrumentVolumeForResonance(ModelStackWithThreeMainThings* modelStack);
	bool isSkippingRendering() { return skippingRendering; }
	void loadCrucialAudioFilesOnly();
	void beenEdited(bool shouldMoveToEmptySlot = true);
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos);
	void setupWithoutActiveClip(ModelStack* modelStack);
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound);
	uint8_t* getModKnobMode() { return &modKnobMode; }
	ArpeggiatorBase* getArp();
	char const* getXMLTag() { return "sound"; }

	ArpeggiatorSettings defaultArpSettings;
};

#endif
