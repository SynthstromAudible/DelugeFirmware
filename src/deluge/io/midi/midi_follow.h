/*
 * Copyright (c) 2024 Sean Ditny
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
#include "deluge-project/param_address.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/owner_plugin.h"
#include "modulation/params/param.h"
#include "storage/byte_channel.h"
#include "storage/storage_manager.h"
#include "util/containers.h"
#include <array>
#include <cstdint>
#include <optional>

class AudioClip;
class InstrumentClip;
class Clip;
class Kit;
class Drum;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;
enum class MIDIMatchType;

namespace params = deluge::modulation::params;

class MidiFollow final {
public:
	MidiFollow();
	void factoryReset(bool showPopup = true);
	void writeDefaultsToFile();
	/// @brief writeDefaultsToFile's body, run where blocking on the card is legal.
	/// @return Whether the file reached the card.
	bool writeDefaultsInline();
	/// @brief Read the follow defaults, preferring the channel.
	/// @param onDone Told how the channel operation ended, when the channel took it.
	/// @return Whether the CHANNEL accepted the read -- i.e. whether @p onDone will be called.
	bool readDefaultsFromFile(deluge::storage::bytes::DoneFn onDone = nullptr);
	/// @brief readDefaultsFromFile's body, run where blocking on the card is legal.
	void readDefaultsInline();
	/// @brief Put the in-memory follow settings into their out-of-the-box state.
	///
	/// What must be true whether the file turns up or not, so both arms do it before reading.
	void resetToDefaults();
	/// @brief Walk an already-opened document into the in-memory follow settings.
	void parseDefaults(Deserializer& reader);
	/// @brief Write the follow settings into an already-opened serialiser, file- or memory-backed.
	void serialiseDefaults(Serializer& writer);
	/// @brief `deluge::storage::bytes::ParseFn` for @ref parseDefaults.
	static void parseDefaultsBytes(void* ctx, char* bytes, uint32_t len);
	/// @brief `deluge::storage::bytes::AbsentFn`: no MIDIFollow.XML, so write one.
	static void writeDefaultsAbsent(void* ctx);

	/// @brief The lane, in the context of @p clip, of the param a CC follows: @p sound for a sound (a synth, a kit
	/// row),
	/// @p global for a global effectable (a kit's affect-entire, an audio clip).
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                Clip* clip, std::optional<deluge::project::ParamTarget> sound,
	                                                std::optional<deluge::project::ParamTarget> global,
	                                                bool displayError = true);
	void noteMessageReceived(MIDICable& cable, bool on, int32_t channel, int32_t note, int32_t velocity,
	                         bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack);
	Output* sendNoteToClip(MIDICable& cable, Clip* clip, MIDIMatchType match, bool on, int32_t channel, int32_t note,
	                       int32_t velocity, bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack,
	                       bool updateClipForLastNoteReceived = true);
	void midiCCReceived(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue, bool* doingMidiThru,
	                    ModelStack* modelStack);
	void pitchBendReceived(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru,
	                       ModelStack* modelStack);
	void aftertouchReceived(MIDICable& cable, int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru,
	                        ModelStack* modelStack);

	/// @brief [interaction] Forget every clip stored for a note-off that is not one of currentSong's. Called by the
	///        interaction tier's side of a song swap (deluge::playback::songSwappedOut()), so no note-off reaches a
	///        swapped-out song's clip, while one a handler sent to the incoming song's clip still finds it.
	void forgetClipsNotOfCurrentSong();
	/// @brief [interaction] Forget @p clip, which is being deleted, wherever it is stored for a note-off.
	void removeClip(Clip* clip);

	// midi CC mappings
	/// @brief The CC that follows @p target for an owner of @p owner's plugin, or nullopt.
	[[nodiscard]] std::optional<uint8_t> ccOf(deluge::bridge::OwnerPlugin owner,
	                                          deluge::project::ParamTarget target) const;
	bool isGlobalEffectableContext();

	/// The param each CC follows in a sound (the instrument's ids) and in a global effectable (its table's ids).
	std::array<std::optional<deluge::project::ParamTarget>, kMaxMIDIValue + 1> ccToSoundTarget{};
	std::array<std::optional<deluge::project::ParamTarget>, kMaxMIDIValue + 1> ccToGlobalTarget{};
	/// The CC that follows each param, as the defaults or the file last assigned it (MIDI_CC_NONE for none): a sound's
	/// indexed by the instrument's id, a global effectable's by its runtime index.
	std::array<uint8_t, params::UNPATCHED_START + params::UNPATCHED_SOUND_MAX_NUM> soundParamToCC{};
	std::array<uint8_t, params::UNPATCHED_GLOBAL_MAX_NUM> globalParamToCC{};

	int32_t previousKnobPos[kMaxMIDIValue + 1]{};
	uint32_t timeLastCCSent[kMaxMIDIValue + 1]{};
	uint32_t timeAutomationFeedbackLastSent{};

	// public so it can be called from View::sendMidiFollowFeedback
	bool isFeedbackEnabled();
	void sendCCWithoutModelStackForMidiFollowFeedback(bool isAutomation = false);
	/// Resolve the configured MIDI Follow feedback targets and send a learned CC value to each one.
	void sendCCForMidiFollowFeedback(int32_t ccNumber, int32_t knobPos);

	void handleReceivedCC(MIDICable& cable, ModelStackWithTimelineCounter& modelStack, Clip* clip, int32_t ccNumber,
	                      int32_t ccValue);

private:
	// initialize
	void init();
	void initState();
	void clearMappings();
	void initDefaultMappings();
	/// Make @p cc follow the sound param @p target, both ways.
	void mapSound(int32_t cc, deluge::project::ParamTarget target);
	/// Make @p cc follow the global-effectable param @p target, both ways.
	void mapGlobal(int32_t cc, deluge::project::ParamTarget target);

	// note recieved
	Output* noteMessageReceivedForSelectedOrActiveClip(MIDICable& cable, bool on, int32_t channel, int32_t note,
	                                                   int32_t velocity, bool* doingMidiThru,
	                                                   bool shouldRecordNotesNowNow, ModelStack* modelStack);
	void noteMessageReceivedForSpecificTrack(MIDICable& cable, bool on, int32_t channel, int32_t note, int32_t velocity,
	                                         bool* doingMidiThru, bool shouldRecordNotesNowNow, ModelStack* modelStack,
	                                         Output* specific_track, int32_t specific_track_index);
	// cc received
	Output* midiCCReceivedForSelectedOrActiveClip(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue,
	                                              bool* doingMidiThru, ModelStack* modelStack);
	void midiCCReceivedForSpecificTrack(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t ccValue,
	                                    bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
	                                    int32_t specific_track_index);

	// pitch bend received
	Output* pitchBendReceivedForSelectedOrActiveClip(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                                 bool* doingMidiThru, ModelStack* modelStack);
	void pitchBendReceivedForSpecificTrack(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                       bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
	                                       int32_t specific_track_index);

	// after touch received
	Output* aftertouchReceivedForSelectedOrActiveClip(MIDICable& cable, int32_t channel, int32_t value,
	                                                  int32_t noteCode, bool* doingMidiThru, ModelStack* modelStack);
	void aftertouchReceivedForSpecificTrack(MIDICable& cable, int32_t channel, int32_t value, int32_t noteCode,
	                                        bool* doingMidiThru, ModelStack* modelStack, Output* specific_track,
	                                        int32_t specific_track_index);

	Clip* getSelectedOrActiveClip();
	Clip* getSelectedClip();
	Clip* getActiveClip(ModelStack* modelStack);
	[[nodiscard]] const size_t getTrackCount() const;
	Output* getTrackFromIndex(uint32_t trackIndex, uint32_t maxTrack);

	// get model stack with auto param for midi follow cc-param control
	using Target = std::optional<deluge::project::ParamTarget>;
	ModelStackWithAutoParam* getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                                       Clip* clip, Target sound, Target global);
	ModelStackWithAutoParam*
	getModelStackWithParamForSynthClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                   Target sound);
	ModelStackWithAutoParam*
	getModelStackWithParamForKitClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                 Target sound, Target global);
	ModelStackWithAutoParam*
	getModelStackWithParamForAudioClip(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Clip* clip,
	                                   Target global);
	void displayParamControlError(Target sound, Target global);

	MIDIMatchType checkMidiFollowMatch(MIDICable& cable, uint8_t channel);
	MIDIMatchType checkMidiFollowMatchForSpecificTrack(MIDICable& cable, uint8_t channel, int32_t specific_track_index);

	static constexpr size_t kMaxMIDIFollowFeedbackTargets = 2;
	using FeedbackChannelTypes = std::array<MIDIFollowChannelType, kMaxMIDIFollowFeedbackTargets>;
	size_t getChannelTypesForFeedback(FeedbackChannelTypes& feedbackChannelTypes);
	MIDIFollowChannelType getChannelTypeForTrackFeedback();
	bool addChannelTypeForFeedback(FeedbackChannelTypes& feedbackChannelTypes, size_t& numChannelTypes,
	                               MIDIFollowChannelType feedbackChannelType);
	/// Send a learned CC value to each already-resolved feedback target.
	void sendCCForMidiFollowFeedback(FeedbackChannelTypes const& feedbackChannelTypes, size_t numChannelTypes,
	                                 int32_t ccNumber, int32_t knobPos);
	/// Send a learned CC value to one feedback target channel.
	void sendCCForMidiFollowFeedback(MIDIFollowChannelType feedbackChannelType, int32_t ccNumber, int32_t knobPos);

	// Saving

	// CC Mappings
	void writeDefaultMappingsToFile(Serializer& writer);

	// Settings
	void writeDefaultSettingsToFile(Serializer& writer);
	void writeChannelSettingsToFile(Serializer& writer);
	void writeSpecificChannelSettingsToFile(Serializer& writer, MIDIFollowChannelType type);
	void writeKitRootNoteSettingToFile(Serializer& writer);
	void writeFeedbackSettingsToFile(Serializer& writer);
	void writeDisplayParamSettingToFile(Serializer& writer);

	// Loading
	bool successfullyReadDefaultsFromFile{};

	void completeLegacyFlashMigration(bool defaultsWereWritten);
	/// @brief `deluge::storage::bytes::DoneFn` for writeDefaultsToFile's channel write.
	static void defaultsWritten(void* ctx, deluge::storage::bytes::SettingsOutcome outcome);

	// CC Mappings
	void readDefaultMappingsFromFile(Deserializer& reader);

	// Settings
	void readDefaultSettingsFromFile(Deserializer& reader);
	void readChannelSettingsFromFile(Deserializer& reader);
	void readSpecificChannelSettingsFromFile(Deserializer& reader, MIDIFollowChannelType type);
	void readKitRootNoteSettingFromFile(Deserializer& reader);
	void readFeedbackSettingsFromFile(Deserializer& reader);
	void readDisplayParamSettingFromFile(Deserializer& reader);

	// string tags / values
	char const* getNameFromChannelType(MIDIFollowChannelType type);
	MIDIFollowChannelType getChannelTypeFromName(char const* name);

	char const* getNameFromFeedbackChannelType(MIDIFollowFeedbackChannelType type);
	MIDIFollowFeedbackChannelType getFeedbackChannelTypeFromName(char const* name);

	char const* getNameFromFeedbackAutomationMode(MIDIFollowFeedbackAutomationMode mode);
	MIDIFollowFeedbackAutomationMode getFeedbackAutomationModeFromName(char const* name);

	char const* getNameFromBool(bool value);
	bool getBoolFromName(char const* name);
};

extern MidiFollow midiFollow;
