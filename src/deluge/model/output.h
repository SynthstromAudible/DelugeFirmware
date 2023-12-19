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
#include "model/clip/clip_instance_vector.h"
#include "util/d_string.h"
#include <cstdint>

class InstrumentClip;
class Song;
class Clip;
class ParamManagerForTimeline;
class Kit;
class Sound;
class StereoSample;
class TimelineCounter;
class ModControllable;
class GlobalEffectableForClip;
class ModelStack;
class ModelStackWithTimelineCounter;
class MIDIDevice;
class LearnedMIDI;
class ParamManager;

class Output {
public:
	Output(InstrumentType newType);
	virtual ~Output();

	ClipInstanceVector clipInstances;
	Clip* activeClip;
	String name; // Contains the display name as the user sees it.
	             // E.g. on numeric Deluge, SYNT000 will be just "0". Definitely no leading zeros, so not "000".
	             // On OLED Deluge I thiiink SYNT000 would be "SYNT000"?
	             // Definitely does not contain the ".XML" on the end.
	Output* next;
	const InstrumentType type;
	bool mutedInArrangementMode;
	bool soloingInArrangementMode;
	bool inValidState;
	bool wasCreatedForAutoOverdub;
	bool armedForRecording;
	int16_t colour{0};

	uint8_t modKnobMode;

	// Temp stuff for doLaunch()
	bool alreadyGotItsNewClip;
	bool isGettingSoloingClip;

	bool nextClipFoundShouldGetArmed; // Temp thing for Session::armClipsToStartOrSoloWithQuantization

	// reverbAmountAdjust has "1" as 67108864
	// Only gets called if there's an activeClip
	virtual void renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int32_t numSamples,
	                          int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
	                          bool shouldLimitDelayFeedback, bool isClipActive) = 0;

	virtual void setupWithoutActiveClip(ModelStack* modelStack);
	virtual bool setActiveClip(
	    ModelStackWithTimelineCounter* modelStack,
	    PgmChangeSend maySendMIDIPGMs = PgmChangeSend::ONCE); // Will have no effect if it already had that Clip
	void pickAnActiveClipForArrangementPos(ModelStack* modelStack, int32_t arrangementPos,
	                                       PgmChangeSend maySendMIDIPGMs);
	void pickAnActiveClipIfPossible(ModelStack* modelStack, bool searchSessionClipsIfNeeded = true,
	                                PgmChangeSend maySendMIDIPGMs = PgmChangeSend::ONCE,
	                                bool setupWithoutActiveClipIfNeeded = true);
	void detachActiveClip(Song* currentSong);

	virtual ModControllable* toModControllable() { return NULL; }
	virtual bool isSkippingRendering() { return true; } // Not valid for Kits
	bool clipHasInstance(Clip* clip);
	void clipLengthChanged(Clip* clip, int32_t oldLength);
	virtual void cutAllSound() {}
	virtual void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                                    GlobalEffectableForClip** globalEffectableWithMostReverb,
	                                    int32_t* highestReverbAmountFound) {}
	virtual bool offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1,
	                                                   uint8_t data2, ModelStackWithTimelineCounter* modelStack) {
		return false;
	} // A TimelineCounter is required
	virtual void offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber,
	                                            uint8_t value, ModelStackWithTimelineCounter* modelStack) {
	} // A TimelineCounter is required
	virtual int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) { return 2147483647; }
	void endAnyArrangementRecording(Song* song, int32_t actualEndPos, uint32_t timeRemainder);
	virtual bool wantsToBeginArrangementRecording() { return armedForRecording; }

	virtual int32_t readFromFile(
	    Song* song, Clip* clip,
	    int32_t readAutomationUpToPos); // I think that supplying clip here is only a hangover from old pre-2.0 files...
	virtual bool readTagFromFile(char const* tagName);
	void writeToFile(Clip* clipForSavingOutputOnly, Song* song);
	virtual bool writeDataToFile(Clip* clipForSavingOutputOnly,
	                             Song* song); // Returns true if it's ended the opening tag and gone into the sub-tags

	virtual int32_t loadAllAudioFiles(bool mayActuallyReadFiles) { return NO_ERROR; }
	virtual void loadCrucialAudioFilesOnly() {} // Caller must check that there is an activeClip.

	virtual void
	resyncLFOs(){}; // No activeClip needed. Call anytime the Instrument comes into existence on the main list thing
	virtual void sendMIDIPGM(){};
	virtual void deleteBackedUpParamManagers(Song* song) {}
	virtual void prepareForHibernationOrDeletion() {}

	virtual char const* getXMLTag() = 0;

	virtual ParamManager* getParamManager(Song* song);

	virtual char const* getNameXMLTag() { return "name"; }

	virtual void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                               bool on, int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                               bool* doingMidiThru, bool doingMidiFollow = false) {}
	virtual void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                    MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                    bool* doingMidiThru) {}
	virtual void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
	                             bool doingMidiFollow = false) {}
	virtual void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                     MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                                     bool* doingMidiThru, bool doingMidiFollow = false) {}

	virtual void stopAnyAuditioning(ModelStack* modelStack) {}
	virtual void offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
	                                  int32_t whichBendRange, int32_t bendSemitones, bool doingMidiFollow = false) {}

	// Arrangement stuff
	int32_t possiblyBeginArrangementRecording(Song* song, int32_t newPos);
	void endArrangementPlayback(Song* song, int32_t actualEndPos, uint32_t timeRemainder);
	bool recordingInArrangement;

protected:
	virtual Clip* createNewClipForArrangementRecording(ModelStack* modelStack) = 0;
};
