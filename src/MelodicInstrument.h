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

#ifndef MELODICINSTRUMENT_H_
#define MELODICINSTRUMENT_H_

#include <EarlyNoteArray.h>
#include <LearnedMIDI.h>
#include "instrument.h"
#include "Arpeggiator.h"

class PostArpTriggerable;
class NoteRow;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;
class MIDIDevice;

class MelodicInstrument : public Instrument {
public:
	MelodicInstrument(int newType);

	// Check activeClip before you call!
	// mpeValues must be provided for a note-on (can be 0s). Otherwise, can be NULL pointer
	virtual void sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int noteCode, int16_t const* mpeValues,
	                      int fromMIDIChannel = MIDI_CHANNEL_NONE, uint8_t velocity = 64, uint32_t sampleSyncLength = 0,
	                      int32_t ticksLate = 0, uint32_t samplesLate = 0) = 0;

	virtual void ccReceivedFromInputMIDIChannel(int cc, int value, ModelStackWithTimelineCounter* modelStack) {}

	bool writeMelodicInstrumentAttributesToFile(Clip* clipForSavingOutputOnly, Song* song);
	void writeMelodicInstrumentTagsToFile(Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(char const* tagName);

	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       bool on, int channel, int note, int velocity, bool shouldRecordNotes, bool* doingMidiThru);
	void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                            uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru);
	void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                     uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru);
	void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                             int channel, int value, int noteCode, bool* doingMidiThru);

	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs);
	bool isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow) final;
	void stopAnyAuditioning(ModelStack* modelStack) final;
	bool isNoteAuditioning(int noteCode);
	bool isAnyAuditioningHappening() final;
	void beginAuditioningForNote(ModelStack* modelStack, int note, int velocity, int16_t const* mpeValues,
	                             int fromMIDIChannel = MIDI_CHANNEL_NONE, uint32_t sampleSyncLength = 0);
	void endAuditioningForNote(ModelStack* modelStack, int note, int velocity = DEFAULT_LIFT_VALUE);
	virtual ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int cc,
	                                                                       ModelStackWithThreeMainThings* modelStack);
	void processParamFromInputMIDIChannel(int cc, int32_t newValue, ModelStackWithTimelineCounter* modelStack);

	void polyphonicExpressionEventPossiblyToRecord(ModelStackWithTimelineCounter* modelStack, int32_t newValue,
	                                               int whichExpressionDimension, int channelOrNoteNumber,
	                                               int whichCharacteristic);
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = NULL);

	virtual void polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension,
	                                                      int channelOrNoteNumber, int whichCharacteristic) = 0;

	void offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int channelOrZone, int whichBendRange,
	                          int bendSemitones);

	Arpeggiator arpeggiator;

	EarlyNoteArray earlyNotes;
	EarlyNoteArray notesAuditioned;

	LearnedMIDI midiInput;
};

#endif /* MELODICINSTRUMENT_H_ */
