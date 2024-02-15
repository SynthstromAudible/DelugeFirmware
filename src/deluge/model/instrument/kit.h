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
#include "model/global_effectable/global_effectable_for_clip.h"
#include "model/instrument/instrument.h"
class InstrumentClip;
class Drum;
class Sound;
class SoundDrum;
class NoteRow;
class GateDrum;
class ModelStack;
class ModelStackWithNoteRow;
enum class MIDIMatchType;
class Kit final : public Instrument, public GlobalEffectableForClip {
public:
	Kit();
	Drum* getNextDrum(Drum* fromSoundSource);
	Drum* getPrevDrum(Drum* fromSoundSource);
	bool writeDataToFile(Clip* clipForSavingOutputOnly, Song* song);
	void addDrum(Drum* newDrum);
	int32_t readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos);
	Drum* getFirstUnassignedDrum(InstrumentClip* clip);
	~Kit();
	int32_t getDrumIndex(Drum* drum);
	Drum* getDrumFromIndex(int32_t index);
	Drum* getDrumFromIndexAllowNull(int32_t index);
	void modKnobAction(uint8_t whichKnob, int8_t offset);
	int32_t loadAllAudioFiles(bool mayActuallyReadFiles);
	void cutAllSound();
	void renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int32_t numSamples,
	                  int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                  bool shouldLimitDelayFeedback, bool isClipActive);
	void notifySamplesInterruptsSuspended();
	void offerReceivedCCToModControllable(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                      ModelStackWithTimelineCounter* modelStack);
	void offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack);
	bool offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack);

	void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                            uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru);
	void receivedPitchBendForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                              uint8_t data1, uint8_t data2, MIDIMatchType match, uint8_t channel,
	                              bool* doingMidiThru);
	void receivedMPEYForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                         MIDIMatchType match, uint8_t channel, uint8_t value);
	void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                     uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru);
	void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru);
	void receivedAftertouchForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                               MIDIMatchType match, uint8_t channel, uint8_t value);
	void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue, ModelStackWithTimelineCounter* modelStack) {}

	void receivedCCForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                      MIDIMatchType match, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
	                      Clip* clip);
	void choke();
	void resyncLFOs();
	void removeDrum(Drum* drum);
	ModControllable* toModControllable();
	SoundDrum* getDrumFromName(char const* name, bool onlyIfNoNoteRow = false);
	int32_t makeDrumNameUnique(String* name, int32_t startAtNumber);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs);
	void setupPatching(ModelStackWithTimelineCounter* modelStack);
	void compensateInstrumentVolumeForResonance(ParamManagerForTimeline* paramManager, Song* song);
	void deleteBackedUpParamManagers(Song* song);
	void prepareForHibernationOrDeletion();
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos);
	void loadCrucialAudioFilesOnly();
	GateDrum* getGateDrumForChannel(int32_t gateChannel);
	void resetDrumTempValues();
	void setupWithoutActiveClip(ModelStack* modelStack);
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound);
	uint8_t* getModKnobMode() { return &modKnobMode; }
	Output* toOutput() { return this; }
	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       bool on, int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru);
	void receivedNoteForDrum(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
	                         int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                         bool* doingMidiThru, Drum* thisDrum);
	bool isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow);
	bool allowNoteTails(NoteRow* noteRow);
	void stopAnyAuditioning(ModelStack* modelStack);
	bool isAnyAuditioningHappening();
	void beginAuditioningforDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity,
	                             int16_t const* mpeValues, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE);
	void endAuditioningForDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity = kDefaultLiftValue);
	void offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone, int32_t whichBendRange,
	                          int32_t bendSemitones);

	bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack, StereoSample* globalEffectableBuffer,
	                                   int32_t* bufferToTransferTo, int32_t numSamples, int32_t* reverbBuffer,
	                                   int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                                   bool shouldLimitDelayFeedback, bool isClipActive, int32_t pitchAdjust,
	                                   int32_t amplitudeAtStart, int32_t amplitudeAtEnd);

	char const* getXMLTag() { return "kit"; }

	Drum* firstDrum;
	Drum* selectedDrum;

	OrderedResizeableArrayWith32bitKey drumsWithRenderingActive;

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind);

	void receivedNoteForKit(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on, int32_t channel,
	                        int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru,
	                        InstrumentClip* clip);

	void receivedAftertouchForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                              MIDIMatchType match, int32_t channel, int32_t value, int32_t noteCode,
	                              bool* doingMidiThru);

	void receivedPitchBendForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             MIDIMatchType match, uint8_t channel, uint8_t data1, uint8_t data2,
	                             bool* doingMidiThru);

protected:
	bool isKit() { return true; }

private:
	int32_t readDrumFromFile(Song* song, Clip* clip, DrumType drumType, int32_t readAutomationUpToPos);
	void writeDrumToFile(Drum* thisDrum, ParamManager* paramManagerForDrum, bool savingSong, int32_t* selectedDrumIndex,
	                     int32_t* drumIndex, Song* song);
	void removeDrumFromLinkedList(Drum* drum);
	void drumRemoved(Drum* drum);
	void possiblySetSelectedDrumAndRefreshUI(Drum* thisDrum);
	Drum* getDrumFromNoteCode(InstrumentClip* clip, int32_t noteCode);
};
