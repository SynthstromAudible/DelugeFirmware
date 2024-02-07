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

#include "model/instrument/non_audio_instrument.h"

class ParamManagerMIDI;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;

struct MPEOutputMemberChannel {
	int16_t lastNoteCode{32767};
	uint16_t noteOffOrder{0};
	int16_t lastXValueSent{0};        // The actual 14-bit number. But signed (goes positive and negative).
	int8_t lastYAndZValuesSent[2]{0}; // The actual 7-bit numbers. Y goes both positive and negative.
};

class MIDIInstrument final : public NonAudioInstrument {
public:
	MIDIInstrument();

	void ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack);

	void allNotesOff();

	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs);
	bool writeDataToFile(Clip* clipForSavingOutputOnly, Song* song);
	bool readTagFromFile(char const* tagName);
	int32_t readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos, ParamManagerForTimeline* paramManager = NULL);
	void sendMIDIPGM();
	int32_t changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode);
	int32_t getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
	                         int32_t stopAt);
	int32_t moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack);
	int32_t moveAutomationToDifferentCC(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode,
	                                    ModelStackWithThreeMainThings* modelStack);
	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
	                       bool on, int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru);

	// ModControllable implementation
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager);
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true);
	uint8_t* getModKnobMode() { return &modKnobMode; }

	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack);
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t cc,
	                                                               ModelStackWithThreeMainThings* modelStack);
	bool doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int32_t cc);
	int32_t getOutputMasterChannel();

	inline bool sendsToMPE() { return (channel >= 16); }

	int32_t channelSuffix{-1};
	int32_t lastNoteCode{32767};
	bool collapseAftertouch{false};
	bool collapseMPE{true};
	float ratio; // for combining per finger and global bend

	int8_t modKnobCCAssignments[kNumModButtons * kNumPhysicalModKnobs];

	// Numbers 0 to 15 can all be an MPE member depending on configuration
	MPEOutputMemberChannel mpeOutputMemberChannels[16];

	// for tracking mono expression output
	int32_t lastMonoExpression[3]{0};
	int32_t lastCombinedPolyExpression[3]{0};
	// could be int8 for aftertouch/Y but Midi 2 will allow those to be 14 bit too
	int16_t lastOutputMonoExpression[3]{0};
	char const* getXMLTag() { return sendsToMPE() ? "mpeZone" : "midiChannel"; }
	char const* getSlotXMLTag() { return sendsToMPE() ? "zone" : "channel"; }
	char const* getSubSlotXMLTag() { return "suffix"; }

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind);

protected:
	void polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
	                                              int32_t whichExpressionDimension, ArpNote* arpNote);
	void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote);
	void noteOffPostArp(int32_t noteCodePostArp, int32_t oldMIDIChannel, int32_t velocity);
	void monophonicExpressionEvent(int32_t newValue, int32_t whichExpressionDimension);

private:
	void sendMonophonicExpressionEvent(int32_t whichExpressionDimension);
	void combineMPEtoMono(int32_t value32, int32_t whichExpressionDimension);
	void outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int32_t outputMemberChannel);
};
