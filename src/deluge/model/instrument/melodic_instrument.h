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

#include "io/midi/learned_midi.h"
#include "model/instrument/instrument.h"
#include "modulation/arpeggiator.h"
#include "util/containers.h"

class PostArpTriggerable;
class NoteRow;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;

class MelodicInstrument : public Instrument {
public:
	explicit MelodicInstrument(OutputType newType) : Instrument(newType) {}

	// Check activeClip before you call!
	// mpeValues must be provided for a note-on (can be 0s). Otherwise, can be NULL pointer
	virtual void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCode,
	                      int16_t const* mpeValues, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint8_t velocity = 64,
	                      uint32_t sampleSyncLength = 0, int32_t ticksLate = 0, uint32_t samplesLate = 0) = 0;

	virtual void ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack) {}

	bool writeMelodicInstrumentAttributesToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	void writeMelodicInstrumentTagsToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(Deserializer& reader, char const* tagName) override;

	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable, bool on,
	                       int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru) override;
	void receivedNote(ModelStackWithTimelineCounter* modelStack, MIDICable& cable, bool on, int32_t midiChannel,
	                  MIDIMatchType match, int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru);
	void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                            uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru) override;
	void receivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                       MIDIMatchType match, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru);
	void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                     uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) override;
	void receivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable, MIDIMatchType match,
	                uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru);
	void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                             int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru) override;
	void receivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                        MIDIMatchType match, int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) override;
	bool isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow) final;
	void stopAnyAuditioning(ModelStack* modelStack) final;
	bool isNoteAuditioning(int32_t noteCode);
	bool isAnyAuditioningHappening() final;
	void beginAuditioningForNote(ModelStack* modelStack, int32_t note, int32_t velocity, int16_t const* mpeValues,
	                             int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0);
	void endAuditioningForNote(ModelStack* modelStack, int32_t note, int32_t velocity = kDefaultLiftValue);
	virtual ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t cc,
	                                                                       ModelStackWithThreeMainThings* modelStack);
	void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue,
	                                      ModelStackWithTimelineCounter* modelStack) override;

	void polyphonicExpressionEventPossiblyToRecord(ModelStackWithTimelineCounter* modelStack, int32_t newValue,
	                                               int32_t expressionDimension, int32_t channelOrNoteNumber,
	                                               MIDICharacteristic whichCharacteristic);
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = nullptr);

	virtual void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                                      int32_t channelOrNoteNumber,
	                                                      MIDICharacteristic whichCharacteristic) = 0;

	void offerBendRangeUpdate(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone, int32_t whichBendRange,
	                          int32_t bendSemitones) override;

	Arpeggiator arpeggiator;

	struct EarlyNoteInfo {
		uint8_t velocity;
		bool still_active = false;
	};

	deluge::fast_map<int16_t, EarlyNoteInfo> earlyNotes; // note value, velocity, still_active
	deluge::fast_map<int16_t, EarlyNoteInfo> notesAuditioned;

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                bool affectEntire, bool useMenuStack) override;

private:
	void possiblyRefreshAutomationEditorGrid(int32_t ccNumber);
	void processSustainPedalParam(int32_t newValue, ModelStackWithTimelineCounter* modelStack);
	void releaseSustainedVoices(ModelStackWithTimelineCounter* modelStack);
};
