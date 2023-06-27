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

#ifndef MIDIINSTRUMENT_H_
#define MIDIINSTRUMENT_H_

#include "NonAudioInstrument.h"

class ParamManagerMIDI;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;

struct MPEOutputMemberChannel {
	int16_t lastNoteCode;
	uint16_t noteOffOrder;
	int16_t lastXValueSent;        // The actual 14-bit number. But signed (goes positive and negative).
	int8_t lastYAndZValuesSent[2]; // The actual 7-bit numbers. Y goes both positive and negative.
};

class MIDIInstrument final : public NonAudioInstrument {
public:
	MIDIInstrument();

	void ccReceivedFromInputMIDIChannel(int cc, int value, ModelStackWithTimelineCounter* modelStack);

	void allNotesOff();

	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs);
	bool writeDataToFile(Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(char const* tagName);
	int readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos, ParamManagerForTimeline* paramManager = NULL);
	void sendMIDIPGM();
	int changeControlNumberForModKnob(int offset, int whichModEncoder, int modKnobMode);
	int getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int direction, int startAt, int stopAt);
	int moveAutomationToDifferentCC(int oldCC, int newCC, ModelStackWithThreeMainThings* modelStack);
	int moveAutomationToDifferentCC(int offset, int whichModEncoder, int modKnobMode,
	                                ModelStackWithThreeMainThings* modelStack);
	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       bool on, int channel, int note, int velocity, bool shouldRecordNotes, bool* doingMidiThru);

	// ModControllable implementation
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager);
	ModelStackWithAutoParam* getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true);
	uint8_t* getModKnobMode() { return &modKnobMode; }

	int getKnobPosForNonExistentParam(int whichModEncoder, ModelStackWithAutoParam* modelStack);
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int cc, ModelStackWithThreeMainThings* modelStack);
	bool doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int cc);
	int getOutputMasterChannel();

	inline bool sendsToMPE() { return (channel >= 16); }

	int channelSuffix;

	int8_t modKnobCCAssignments[NUM_MOD_BUTTONS * NUM_PHYSICAL_MOD_KNOBS];

	MPEOutputMemberChannel mpeOutputMemberChannels[15]; // Numbers 1 to 14. 0 is bogus

	char const* getXMLTag() { return sendsToMPE() ? "mpeZone" : "midiChannel"; }
	char const* getSlotXMLTag() { return sendsToMPE() ? "zone" : "channel"; }
	char const* getSubSlotXMLTag() { return "suffix"; }

protected:
	void polyphonicExpressionEventPostArpeggiator(int newValue, int noteCodeAfterArpeggiation,
	                                              int whichExpressionDimension, ArpNote* arpNote);
	void noteOnPostArp(int noteCodePostArp, ArpNote* arpNote);
	void noteOffPostArp(int noteCodePostArp, int oldMIDIChannel, int velocity);
	void monophonicExpressionEvent(int newValue, int whichExpressionDimension);

private:
	void outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int outputMemberChannel);
};

#endif /* MIDIINSTRUMENT_H_ */
