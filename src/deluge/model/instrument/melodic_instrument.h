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
#include "util/container/array/early_note_array.h"

class PostArpTriggerable;
class NoteRow;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class MIDIDevice;

class MelodicInstrument : public Instrument {
public:
	explicit MelodicInstrument(OutputType newType) : Instrument(newType), earlyNotes(), notesAuditioned() {}

	// Check activeClip before you call!
	// mpeValues must be provided for a note-on (can be 0s). Otherwise, can be NULL pointer
	virtual void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int32_t noteCode,
	                      int16_t const* mpeValues, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE, uint8_t velocity = 64,
	                      uint32_t sampleSyncLength = 0, int32_t ticksLate = 0, uint32_t samplesLate = 0) = 0;

	virtual void ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack) {}

	bool writeMelodicInstrumentAttributesToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	void writeMelodicInstrumentTagsToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(Deserializer& reader, char const* tagName) override;

	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       bool on, int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru) override;
	void receivedNote(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on, int32_t midiChannel,
	                  MIDIMatchType match, int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru);
	void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                            uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru) override;
	void receivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       MIDIMatchType match, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru);
	void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                     uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) override;
	void receivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                MIDIMatchType match, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru);
	void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru) override;
	void receivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
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
	                                               int32_t whichExpressionDimension, int32_t channelOrNoteNumber,
	                                               MIDICharacteristic whichCharacteristic);
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = nullptr);

	virtual void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t whichExpressionDimension,
	                                                      int32_t channelOrNoteNumber,
	                                                      MIDICharacteristic whichCharacteristic) = 0;

	void offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone, int32_t whichBendRange,
	                          int32_t bendSemitones) override;

	Arpeggiator arpeggiator;

	EarlyNoteArray earlyNotes;
	EarlyNoteArray notesAuditioned;

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                bool affectEntire, bool useMenuStack) override;

private:
	void possiblyRefreshAutomationEditorGrid(int32_t ccNumber);
};
