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
#include "model/drum/drum.h"
#include "processing/sound/sound.h"
#include "util/d_string.h"

class ModelStackWithTimelineCounter;

class SoundDrum final : public Sound, public Drum {
public:
	String name;
	String path;
	bool nameIsDiscardable;

	ArpeggiatorForDrum arpeggiator;
	ArpeggiatorSettings arpSettings;

	SoundDrum();
	bool isDrum() { return true; }
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false);
	bool anyNoteIsOn();
	bool hasAnyVoices();
	void noteOn(ModelStackWithThreeMainThings* modelStack, uint8_t velocity, Kit* kit, int16_t const* mpeValues,
	            int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0);
	void noteOff(ModelStackWithThreeMainThings* modelStack, int32_t velocity);
	void unassignAllVoices();
	void setupPatchingForAllParamManagers(Song* song);
	bool readTagFromFile(Deserializer& reader, char const* tagName);
	Error loadAllSamples(bool mayActuallyReadFiles);
	void prepareForHibernation();
	void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager);
	void writeToFileAsInstrument(bool savingSong, ParamManager* paramManager);
	void getName(char* buffer);
	Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos);
	void choke(ModelStackWithSoundFlags* modelStack);
	void setSkippingRendering(bool newSkipping);
	uint8_t* getModKnobMode();
	void drumWontBeRenderedForAWhile();
	ModControllable* toModControllable() { return this; }

	void expressionEvent(int32_t newValue, int32_t whichExpressionDimension);
	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension,
	                                              int32_t channelOrNoteNumber, MIDICharacteristic whichCharacteristic);

	ArpeggiatorBase* getArp();
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL) { return &arpSettings; }
	void resetTimeEnteredState();
};
