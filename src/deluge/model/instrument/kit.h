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
#include "dsp-next/core/stereo_sample.h"
#include "model/global_effectable/global_effectable_for_clip.h"
#include "model/instrument/instrument.h"
#include "model/instrument/rendering_drums.h"
#include "modulation/arpeggiator.h"
#include "util/containers.h"
#include "util/functions.h"
class InstrumentClip;
class Drum;
class Sound;
class SoundDrum;
class NoteRow;
class GateDrum;
class ModelStack;
class ModelStackWithNoteRow;
enum class MIDIMatchType;
class Kit final : public Instrument, public GlobalEffectableForClip {
public:
	Kit();
	[[nodiscard]] deluge::model::TrackChannel& channel() override { return channel_; }
	[[nodiscard]] const deluge::model::TrackChannel& channel() const override { return channel_; }

	ArpeggiatorForKit arpeggiator;
	ArpeggiatorSettings defaultArpSettings;

	Drum* getNextDrum(Drum* fromSoundSource);
	Drum* getPrevDrum(Drum* fromSoundSource);
	bool writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) override;
	void addDrum(Drum* newDrum, Song* adoptSong = nullptr);
	Error readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) override;
	Drum* getFirstUnassignedDrum(InstrumentClip* clip);
	~Kit() override;
	int32_t getDrumIndex(Drum* drum);
	Drum* getDrumFromIndex(int32_t index);
	Drum* getDrumFromIndexAllowNull(int32_t index);
	/// @brief The row whose id is @p id.
	/// @param id A row's id.
	/// @return The row, or nullptr when the kit has none with that id.
	[[nodiscard]] Drum* drumWithId(uint32_t id) const;

	deluge::storage::Walk walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor) override;
	void cutAllSound() override;
	void renderOutput(ModelStack* modelStack, std::span<StereoSample> buffer, deluge::dsp::Q<31>* reverbBuffer,
	                  int32_t reverbAmountAdjust, int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
	                  bool isClipActive) override;

	void offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                     uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) override;
	void receivedCCForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                      MIDIMatchType match, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
	                      Clip* clip);
	void offerReceivedCCToModControllable(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                      ModelStackWithTimelineCounter* modelStack);
	void offerReceivedCCToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t ccNumber, uint8_t value,
	                                    ModelStackWithTimelineCounter* modelStack) override;

	void offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                            uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru) override;
	void receivedPitchBendForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                              uint8_t data1, uint8_t data2, MIDIMatchType match, uint8_t channel,
	                              bool* doingMidiThru);
	void receivedPitchBendForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                             MIDIMatchType match, uint8_t channel, uint8_t data1, uint8_t data2,
	                             bool* doingMidiThru);
	bool offerReceivedPitchBendToLearnedParams(MIDICable& cable, uint8_t channel, uint8_t data1, uint8_t data2,
	                                           ModelStackWithTimelineCounter* modelStack) override;

	// drums don't receive other CCs
	void receivedMPEYForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                         MIDIMatchType match, uint8_t channel, uint8_t value);

	void offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                             int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru) override;

	void receivedAftertouchForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
	                               MIDIMatchType match, uint8_t channel, uint8_t value);

	void receivedAftertouchForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable,
	                              MIDIMatchType match, int32_t channel, int32_t value, int32_t noteCode,
	                              bool* doingMidiThru);

	void offerReceivedNote(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDICable& cable, bool on,
	                       int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
	                       bool* doingMidiThru) override;
	void receivedNoteForDrum(ModelStackWithTimelineCounter* modelStack, MIDICable& cable, bool on, int32_t channel,
	                         int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru,
	                         Drum* thisDrum);
	void receivedNoteForKit(ModelStackWithTimelineCounter* modelStack, MIDICable& cable, bool on, int32_t channel,
	                        int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru,
	                        InstrumentClip* clip);

	void processParamFromInputMIDIChannel(int32_t cc, int32_t newValue,
	                                      ModelStackWithTimelineCounter* modelStack) override {}

	void beenEdited(bool shouldMoveToEmptySlot = true) override;
	void choke();
	void resyncLFOs(bool reset_state = false) override;
	void removeDrumFromKitArpeggiator(int32_t drumIndex);
	void removeDrum(Drum* drum);
	ModControllable* toModControllable() override;
	Drum* getDrumFromName(std::string_view name, bool onlyIfNoNoteRow = false);
	Error makeDrumNameUnique(std::string* name, int32_t startAtNumber);
	bool setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) override;
	void setupPatching(ModelStackWithTimelineCounter* modelStack) override;
	void deleteBackedUpParamManagers(Song* song) override;
	void prepareForHibernationOrDeletion() override;
	int32_t doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) override;
	deluge::storage::Walk walkCrucialSampleDemands(deluge::storage::SampleLoadVisitor& visitor) override;
	GateDrum* getGateDrumForChannel(int32_t gateChannel);
	void resetDrumTempValues();
	void setupWithoutActiveClip(ModelStack* modelStack) override;
	[[nodiscard]] deluge::model::ReverbCandidate reverbCandidate(deluge::model::ReverbCandidate best) override;
	uint8_t* getModKnobMode() override { return &modKnobMode; }
	Output* toOutput() override { return this; }

	bool isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow) override;
	bool allowNoteTails(NoteRow* noteRow);
	void stopAnyAuditioning(ModelStack* modelStack) override;
	bool isAnyAuditioningHappening() override;
	void beginAuditioningforDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity,
	                             int16_t const* mpeValues, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE);
	void endAuditioningForDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity = kDefaultLiftValue);
	void offerBendRangeUpdate(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone, int32_t whichBendRange,
	                          int32_t bendSemitones) override;

	char const* getXMLTag() override { return "kit"; }

	Drum* firstDrum;
	Drum* selectedDrum;

	RenderingDrums renderingDrums; ///< the drums rendering now: the render walks them every block

	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                bool affectEntire, bool useMenuStack, bool forEdit) override;
	ModelStackWithAutoParam* getModelStackWithParamForKit(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                      int32_t paramID, deluge::modulation::params::Kind paramKind,
	                                                      bool useMenuStack, bool forEdit);
	ModelStackWithAutoParam* getModelStackWithParamForKitRow(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                                                         int32_t paramID,
	                                                         deluge::modulation::params::Kind paramKind,
	                                                         bool useMenuStack, bool forEdit);

	Drum* getDrumFromNoteCode(InstrumentClip* clip, int32_t noteCode);

	void noteOnPreKitArp(ModelStackWithThreeMainThings* modelStack, Drum* drum, uint8_t velocity,
	                     int16_t const* mpeValues, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE,
	                     uint32_t sampleSyncLength = 0, int32_t ticksLate = 0, uint32_t samplesLate = 0);
	void noteOffPreKitArp(ModelStackWithThreeMainThings* modelStack, Drum* drum, int32_t velocity = kDefaultLiftValue);

protected:
	bool isKit() override { return true; }

private:
	/// @brief Renders the kit's drums into its channel's buffer, each through its own effects.
	/// @param modelStack The kit's model stack; its timeline counter may be null.
	/// @param buffer The channel's buffer, zeroed; the drums add into it.
	/// @param bufferToTransferTo Unused by a kit (null).
	/// @param reverbBuffer The song's reverb send buffer.
	/// @param reverbAmountAdjust The reverb send scaling, scaled by the kit's volume, Q31.
	/// @param sideChainHitPending The sidechain hit pending this block, Q31; 0 for none.
	/// @param shouldLimitDelayFeedback Whether the drums' delay feedback is capped below self-oscillation.
	/// @param isClipActive Whether the kit's active clip plays.
	/// @param pitchAdjust The kit's pitch adjust, applied to every drum.
	/// @param amplitudeAtStart Unused by a kit.
	/// @param amplitudeAtEnd Unused by a kit.
	/// @return Whether any drum rendered.
	bool renderContent(ModelStackWithTimelineCounter* modelStack, std::span<StereoSample> buffer,
	                   int32_t* bufferToTransferTo, deluge::dsp::Q<31>* reverbBuffer, int32_t reverbAmountAdjust,
	                   int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive,
	                   int32_t pitchAdjust, int32_t amplitudeAtStart, int32_t amplitudeAtEnd);
	/// The kit's channel strip: the chain its drums render into, its settings and view.
	deluge::model::TrackChannel channel_;
	Error readDrumFromFile(Deserializer& reader, Song* song, Clip* clip, DrumType drumType,
	                       int32_t readAutomationUpToPos);
	void writeDrumToFile(Serializer& writer, Drum* thisDrum, ParamManager* paramManagerForDrum, bool savingSong,
	                     int32_t* selectedDrumIndex, int32_t* drumIndex, Song* song);
	void removeDrumFromLinkedList(Drum* drum);
	void drumRemoved(Drum* drum);
	void possiblySetSelectedDrumAndRefreshUI(Drum* thisDrum);

	// Kit Arp
	void setupAndRenderArpPreOutput(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                ParamManager* paramManager, std::span<StereoSample> output);
	ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = nullptr);
	void renderNonAudioArpPostOutput(std::span<StereoSample> output);
};
