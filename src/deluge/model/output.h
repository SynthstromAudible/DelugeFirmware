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
#include "hid/button.h"
#include "model/clip/clip_instance_vector.h"
#include "model/sample/sample_recorder.h"
#include "modulation/params/param.h"
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
class ModelStackWithAutoParam;
class MIDIDevice;
class LearnedMIDI;
class ParamManager;
class Serializer;
class Deserializer;

inline const char* outputTypeToString(OutputType type) {
	switch (type) {
	case OutputType::SYNTH:
		return "synth";
	case OutputType::KIT:
		return "kit";
	case OutputType::MIDI_OUT:
		return "MIDI";
	case OutputType::CV:
		return "CV";
	case OutputType::AUDIO:
		return "audio";
	default:
		return "none";
	}
}

inline OutputType buttonToOutputType(deluge::hid::Button b) {
	using namespace deluge::hid::button;
	switch (b) {
	case SYNTH:
		return OutputType::SYNTH;
	case MIDI:
		return OutputType::MIDI_OUT;
	case KIT:
		return OutputType::KIT;
	case CV:
		return OutputType::CV;
	case SELECT_ENC:
		return OutputType::AUDIO;
	default:
		return OutputType::NONE;
	}
}
class Output {
public:
	Output(OutputType newType);
	virtual ~Output();

	ClipInstanceVector clipInstances;
	[[nodiscard]] Clip* getActiveClip() const;
	String name; // Contains the display name as the user sees it.
	             // E.g. on numeric Deluge, SYNT000 will be just "0". Definitely no leading zeros, so not "000".
	             // On OLED Deluge I thiiink SYNT000 would be "SYNT000"?
	             // Definitely does not contain the ".XML" on the end.
	Output* next;
	const OutputType type;
	bool mutedInArrangementMode;
	bool mutedInArrangementModeBeforeStemExport; // Used by stem export to restore previous state
	bool exportStem;                             // Used by stem export to flag if this output should be exported
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

	virtual ModControllable* toModControllable() { return nullptr; }
	virtual bool isSkippingRendering() { return true; } // Not valid for Kits
	bool clipHasInstance(Clip* clip);
	bool isEmpty(bool displayPopup = true);
	void clipLengthChanged(Clip* clip, int32_t oldLength);
	virtual void cutAllSound() {}
	virtual void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                                    GlobalEffectableForClip** globalEffectableWithMostReverb,
	                                    int32_t* highestReverbAmountFound) {}
	/// If there's a clip matching the name on this output, returns it.
	Clip* getClipFromName(String* name);

	/// Pitch bend is available in the mod matrix as X and shouldn't be learned to params anymore (post 4.0)
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

	// FIXME:I think that supplying clip here is only a hangover from old pre-2.0 files...
	virtual Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos);

	virtual bool readTagFromFile(Deserializer& reader, char const* tagName);
	void writeToFile(Clip* clipForSavingOutputOnly, Song* song);
	virtual bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly,
	                             Song* song); // Returns true if it's ended the opening tag and gone into the sub-tags

	virtual Error loadAllAudioFiles(bool mayActuallyReadFiles) { return Error::NONE; }
	virtual void loadCrucialAudioFilesOnly() {} // Caller must check that there is an activeClip.

	// No activeClip needed. Call anytime the Instrument comes into existence on the main list thing
	virtual void resyncLFOs(){};

	virtual void sendMIDIPGM(){};
	virtual void deleteBackedUpParamManagers(Song* song) {}
	virtual void prepareForHibernationOrDeletion() {}

	virtual char const* getXMLTag() = 0;
	virtual ParamManager* getParamManager(Song* song);

	virtual char const* getNameXMLTag() { return "name"; }

	virtual void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                               bool on, int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                               bool* doingMidiThru) {}
	virtual void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                    MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
	                                    bool* doingMidiThru) {}
	virtual void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) {}
	virtual void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                     MIDIDevice* fromDevice, int32_t channel, int32_t value, int32_t noteCode,
	                                     bool* doingMidiThru) {}

	virtual void stopAnyAuditioning(ModelStack* modelStack) {}
	virtual void offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
	                                  int32_t whichBendRange, int32_t bendSemitones) {}

	// Arrangement stuff
	Error possiblyBeginArrangementRecording(Song* song, int32_t newPos);
	void endArrangementPlayback(Song* song, int32_t actualEndPos, uint32_t timeRemainder);
	bool recordingInArrangement;

	virtual ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                        int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                        bool affectEntire, bool useMenuStack) = 0;
	virtual bool needsEarlyPlayback() const { return false; }
	bool hasRecorder() { return recorder; }
	bool shouldRenderInSong() { return !(recorderIsEchoing); }

	/// disable rendering to the song buffer if this clip is the input to an audio output that's monitoring
	void setRenderingToAudioOutput(bool monitoring, Output* output) {
		recorderIsEchoing = monitoring;
		outputRecordingThisOutput = output;
	}
	bool addRecorder(SampleRecorder* newRecorder) {
		if (recorder) {
			return false;
		}
		recorder = newRecorder;
		return true;
	}
	// returns whether a recorder was removed
	bool removeRecorder() {
		if (recorder) {
			recorder->removeFromOutput();
			recorder = nullptr;
			return true;
		}
		return false;
	}
	Output* getOutputRecordingThis() { return outputRecordingThisOutput; }

protected:
	virtual Clip* createNewClipForArrangementRecording(ModelStack* modelStack) = 0;
	bool recorderIsEchoing{false};
	// for clearing pointers when this output is deleted
	Output* outputRecordingThisOutput{nullptr};
	// only for audio outputs, will be used for instruments when I implement routing notes between clips
	virtual void clearRecordingFrom() {}
	Clip* activeClip{nullptr};
	SampleRecorder* recorder{nullptr};
};
