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
#include "io/midi/learned_midi.h"
#include "model/clip/clip_instance_vector.h"
#include "model/output.h"

class StereoSample;
class ModControllable;
class InstrumentClip;
class ParamManagerForTimeline;
class Song;
class ArpeggiatorSettings;
class Kit;
class Sound;
class TimelineCounter;
class NoteRow;
class ModelStackWithTimelineCounter;
class ModelStackWithThreeMainThings;

/*
 * An Instrument is the “Output” of a Clip - the thing which turns the sequence or notes into sound (or MIDI or CV
 * output). Instruments include Kit, MIDIInstrument, and CVInsttrument. And then there’s SoundInstrument, which is
 * basically a synth.
 */

class Instrument : public Output {
public:
	Instrument(OutputType newType);
	// This needs to be initialized / defaulted to "SYNTHS" or "KITS" (for those Instrument types). The constructor does
	// not do this, partly because I don't want it doing memory allocation, and also because in many cases, the function
	// creating the object hard-sets this anyway.
	String dirPath;

	bool editedByUser;
	bool existsOnCard;
	bool shouldHibernate{true};
	bool matchesPreset(OutputType otherType, int32_t channel, int32_t channelSuffix, char const* otherName,
	                   char const* otherPath) override {
		bool match{false};
		if (type == otherType) {

			if (otherType == OutputType::SYNTH || otherType == OutputType::KIT) {
				match = !strcasecmp(otherName, name.get()) && !strcasecmp(otherPath, dirPath.get());
			}
		}
		return match;
	}
	virtual bool doAnySoundsUseCC(uint8_t channel, uint8_t ccNumber, uint8_t value) { return false; }
	virtual void beenEdited(bool shouldMoveToEmptySlot = true);
	virtual void setupPatching(ModelStackWithTimelineCounter* modelStack) {
	} // You must call this when an Instrument comes into existence or something... for every Clip, not just for the
	  // activeClip
	void deleteAnyInstancesOfClip(InstrumentClip* clip);

	// virtual void writeInstrumentDataToFile(bool savingSong, char const* slotName = "presetSlot", char const*
	// subSlotName = "presetSubSlot");
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(Deserializer& reader, char const* tagName);

	virtual void compensateInstrumentVolumeForResonance(ModelStackWithThreeMainThings* modelStack) {}
	virtual bool isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow) = 0;
	virtual void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue,
	                                              ModelStackWithTimelineCounter* modelStack) = 0;

	char const* getNameXMLTag() { return "presetName"; }
	virtual char const* getSlotXMLTag() { return "presetSlot"; }
	virtual char const* getSubSlotXMLTag() { return "presetSubSlot"; }

	virtual bool isAnyAuditioningHappening() = 0;

	uint8_t defaultVelocity;
	LearnedMIDI midiInput;

protected:
	Clip* createNewClipForArrangementRecording(ModelStack* modelStack) final;
	Error setupDefaultAudioFileDir();
};
