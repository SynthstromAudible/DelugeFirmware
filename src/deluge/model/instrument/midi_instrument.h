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
#include "deluge-project/doc_output.h"
#include "model/instrument/non_audio_instrument.h"
#include "util/containers.h"
#include "util/string.h"
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

	/// @brief Reseeds the record's identity fields from the current native values.
	///
	/// Calls the base (channel), then reseeds channelSuffix, deviceDefinitionFileName, collapseAftertouch,
	/// collapseMPE and outputMPEY.
	void seedDocOutputIdentity() override;

	/// @name "Record-authoritative fields"
	/// channelSuffix, deviceDefinitionFileName, collapseAftertouch, collapseMPE and outputMPEY are
	/// record-authoritative at save: every write funnels through these setters, which update the native cache and the
	/// record together (guarded, no FREEZE), like Output::set*() and Instrument::set*().
	/// @{

	/// @brief Sets the channel suffix.
	/// @param v The new channel suffix.
	void setChannelSuffix(int32_t v);
	/// @brief Sets the device definition file name.
	/// @param v The new file name.
	void setDeviceDefinitionFileName(std::string_view v);
	/// @brief Sets whether aftertouch is collapsed.
	/// @param v True to collapse aftertouch.
	void setCollapseAftertouch(bool v);
	/// @brief Sets whether MPE is collapsed.
	/// @param v True to collapse MPE.
	void setCollapseMPE(bool v);
	/// @brief Sets the CC number used for MPE Y output.
	/// @param v The new CC number.
	void setOutputMPEY(CCNumber v);
	/// @}
	Error readModKnobAssignmentsFromFile(Deserializer& reader, int32_t readAutomationUpToPos,
	                                     ParamManagerForTimeline* paramManager = nullptr);

	// midi device definition file
	/// reading
	Error readDeviceDefinitionFile(Deserializer& reader);
	void readDeviceDefinitionFileNameFromPresetOrSong(Deserializer& reader);
	Error readCCLabelsFromFile(Deserializer& reader);
	Error readHideUnlabeledCCFromFile(Deserializer& reader);
	/// writing
	void writeDeviceDefinitionFile(Serializer& writer, bool writeFileNameToPresetOrSong);
	void writeDeviceDefinitionFileNameToPresetOrSong(Serializer& writer);
	void writeCCLabelsToFile(Serializer& writer);
	/// getting / updating cc labels
	std::string_view getNameFromCC(int32_t cc);
	void setNameForCC(int32_t cc, std::string_view name);
	/// definition file
	std::string deviceDefinitionFileName{};
	bool hideUnlabeledCC = false;

	void sendMIDIPGM() override;

	void sendNoteToInternal(bool on, int32_t note, uint8_t velocity, uint8_t channel);

	int32_t getNextSelectableCC(int32_t cc, int32_t offset, bool includeNoCC = false);
	/// @brief Move knob @p whichModEncoder of mod button @p modKnobMode to the next selectable CC @p offset away,
	///        through SetModKnobCC.
	/// @return The knob's CC now.
	int32_t changeControlNumberForModKnob(int32_t offset, int32_t whichModEncoder, int32_t modKnobMode);
	/// @brief Take @p other's mod-knob CCs, and whether it saves them, on an instrument no song has adopted yet:
	///        adoption records them.
	/// @param other The instrument whose knobs this one copies.
	void copyModKnobCCsFrom(const MIDIInstrument& other);
	/// @brief What engine CC word @p cc names.
	static int8_t engineCcOf(const deluge::project::ModKnobCc& cc);
	/// @brief The record's spelling of engine CC word @p cc.
	static deluge::project::ModKnobCc modKnobCcOf(int8_t cc);
	int32_t getFirstUnusedCC(ModelStackWithThreeMainThings* modelStack, int32_t direction, int32_t startAt,
	                         int32_t stopAt);
	Error moveAutomationToDifferentCC(int32_t oldCC, int32_t newCC, ModelStackWithThreeMainThings* modelStack);
	/// @brief Move knob @p knob (0 then 1 of each mod button in turn) to the next CC @p offset away that no clip of the
	///        track automates, carrying the old CC's automation in every clip of the track. The engine half of
	///        MoveModKnobCC, which records the new CC.
	/// @return The new CC, or -1 for a knob that sends no CC or no free CC.
	int32_t moveKnobCCAutomation(int32_t knob, int32_t offset, Song& song);
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
	float ratio{}; // for combining per finger and global bend

	std::array<int8_t, kNumModButtons * kNumPhysicalModKnobs> modKnobCCAssignments{};

	// Numbers 0 to 15 can all be an MPE member depending on configuration
	MPEOutputMemberChannel mpeOutputMemberChannels[16];

	char const* getXMLTag() override { return "midi"; }
	char const* getSlotXMLTag() override {
		return sendsToMPE() ? "zone" : sendsToInternal() ? "internalDest" : "channel";
	}
	char const* getSubSlotXMLTag() override { return "suffix"; }

	/// Appends a CC name/number to buf for the given CC assignment.
	/// @return True if a user-defined custom name was appended rather than the bare CC number.
	bool appendCCName(etl::istring& buf, int32_t cc);

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

	deluge::fast_map<uint8_t, std::string> labels{};
};
