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
#include "model/instrument/non_audio_instrument.h"
#include "util/containers.h"
#include <array>
#include <string_view>

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

	void ccReceivedFromInputMIDIChannel(int32_t cc, int32_t value, ModelStackWithTimelineCounter* modelStack) override;

	void allNotesOff();

	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) override;
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) override;
	bool readTagFromFile(Deserializer& reader, char const* tagName) override;
	Error readModKnobAssignmentsFromFile(int32_t readAutomationUpToPos,
	                                     ParamManagerForTimeline* paramManager = nullptr);

	// midi device definition file
	/// reading
	Error readDeviceDefinitionFile(Deserializer& reader, bool readFromPresetOrSong);
	void readDeviceDefinitionFileNameFromPresetOrSong(Deserializer& reader);
	Error readCCLabelsFromFile(Deserializer& reader);
	/// writing
	void writeDeviceDefinitionFile(Serializer& writer, bool writeFileNameToPresetOrSong);
	void writeDeviceDefinitionFileNameToPresetOrSong(Serializer& writer);
	void writeCCLabelsToFile(Serializer& writer);
	/// getting / updating cc labels
	std::string_view getNameFromCC(int32_t cc);
	void setNameForCC(int32_t cc, std::string_view name);
	/// definition file
	String deviceDefinitionFileName;
	bool loadDeviceDefinitionFile = false;

	void sendMIDIPGM() override;

	void sendNoteToInternal(bool on, int32_t note, uint8_t velocity, uint8_t channel);

	int32_t changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode);
	int32_t getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
	                         int32_t stopAt);
	Error moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack);
	int32_t moveAutomationToDifferentCC(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode,
	                                    ModelStackWithThreeMainThings* modelStack);
	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable, bool on,
	                       int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru) override;

	// ModControllable implementation
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) override;
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) override;
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) override;
	uint8_t* getModKnobMode() override { return &modKnobMode; }

	int32_t getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) override;
	ModelStackWithAutoParam* getParamToControlFromInputMIDIChannel(int32_t cc,
	                                                               ModelStackWithThreeMainThings* modelStack) override;
	bool doesAutomationExistOnMIDIParam(ModelStackWithThreeMainThings* modelStack, int32_t cc);
	int32_t getOutputMasterChannel();

	inline bool sendsToMPE() {
		return (getChannel() == MIDI_CHANNEL_MPE_LOWER_ZONE || getChannel() == MIDI_CHANNEL_MPE_UPPER_ZONE);
	}
	inline bool sendsToInternal() { return (getChannel() >= IS_A_DEST); }
	bool matchesPreset(OutputType otherType, int32_t otherChannel, int32_t otherSuffix, char const* otherName,
	                   char const* otherPath) override {
		bool match{false};
		if (type == otherType) {
			match = (getChannel() == otherChannel && (channelSuffix == otherSuffix));
		}
		return match;
	}
	int32_t channelSuffix{-1};
	int32_t lastNoteCode{32767};
	bool collapseAftertouch{false};
	bool collapseMPE{true};
	CCNumber outputMPEY{CC_EXTERNAL_MPE_Y};
	float ratio; // for combining per finger and global bend

	std::array<int8_t, kNumModButtons * kNumPhysicalModKnobs> modKnobCCAssignments;

	// Numbers 0 to 15 can all be an MPE member depending on configuration
	MPEOutputMemberChannel mpeOutputMemberChannels[16];

	// MIDI output device selection:
	// 0 = ALL devices (send to all connected MIDI outputs)
	// 1 = DIN MIDI port only
	// 2+ = USB MIDI devices (device index = USB index + 2)
	uint8_t outputDevice{0};

	// Store the device name for reliable matching when devices are reconnected
	// This ensures the correct device is selected even if USB devices are plugged in a different order
	String outputDeviceName;

	char const* getXMLTag() override { return "midi"; }
	char const* getSlotXMLTag() override {
		return sendsToMPE() ? "zone" : sendsToInternal() ? "internalDest" : "channel";
	}
	char const* getSubSlotXMLTag() override { return "suffix"; }

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                bool affectEntire, bool useMenuStack) override;

	bool valueChangedEnoughToMatter(int32_t old_value, int32_t new_value, deluge::modulation::params::Kind kind,
	                                uint32_t paramID) override {
		if (kind == deluge::modulation::params::Kind::EXPRESSION) {
			if (paramID == X_PITCH_BEND) {
				// pitch is in 14 bit instead of 7
				return old_value >> 18 != new_value >> 18;
			}
			// aftertouch and mod wheel are positive only and recorded into a smaller range than CCs
			return old_value >> 24 != new_value >> 24;
		}
		return old_value >> 25 != new_value >> 25;
	}

protected:
	void polyphonicExpressionEventPostArpeggiator(int32_t newValue, int32_t noteCodeAfterArpeggiation,
	                                              int32_t expressionDimension, ArpNote* arpNote,
	                                              int32_t noteIndex) override;
	void noteOnPostArp(int32_t noteCodePostArp, ArpNote* arpNote, int32_t noteIndex) override;
	void noteOffPostArp(int32_t noteCodePostArp, int32_t oldMIDIChannel, int32_t velocity, int32_t noteIndex) override;
	void monophonicExpressionEvent(int32_t newValue, int32_t expressionDimension) override;

private:
	void sendMonophonicExpressionEvent(int32_t expressionDimension);
	void combineMPEtoMono(int32_t value32, int32_t expressionDimension);
	void outputAllMPEValuesOnMemberChannel(int16_t const* mpeValuesToUse, int32_t outputMemberChannel);
	Error readMIDIParamFromFile(Deserializer& reader, int32_t readAutomationUpToPos,
	                            MIDIParamCollection* midiParamCollection, int8_t* getCC = nullptr);

	deluge::fast_map<uint8_t, std::string> labels;
};
