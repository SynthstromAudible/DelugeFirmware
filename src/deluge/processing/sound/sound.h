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
#include "libdeluge/system.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/sample/sample_recorder.h"
#include "model/snapshot/sound_snapshot.h"
#include "model/voiced.h"
#include "modulation/arpeggiator.h"
#include "modulation/knob.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/sidechain/sidechain.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "serialization/value.h"
#include "synth/lfo.h"
#include "synth/param_plane.h"
#include "synth/patcher.h"
#include "util/misc.h"
#include <bitset>
#include <memory>

struct CableGroup;
namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp
using StereoSample = deluge::dsp::StereoSample;
class Voice;
class Kit;
class ParamManagerForTimeline;
class TimelineCounter;
class Clip;
class GlobalEffectableForClip;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;
class ModelStackWithModControllable;

#include "engines/deluge/sound_state.h"
#include <optional>
#include <utility>

#define PARAM_LPF_OFF (-1)

struct ParamLPF {
	int32_t p; // PARAM_LPF_OFF means none
	int32_t currentValue;
};

#define NUM_MOD_SOURCE_SELECTION_BUTTONS 2

/*
 * Sound can be either an Instrument or a Drum, in the form of SoundInstrument or SoundDrum respectively.
 * These classes are implemented using “multiple inheritance”, which is sacrilegious to many C++ programmers.
 * I (Rohan) consider it to be a more or less appropriate solution in this case and a few others in the Deluge codebase
 * where it’s used. It’s a little while though since I’ve sat and thought about what the alternatives could be and
 * whether anything else would be appropriate.
 *
 * Anyway, Sound (which may be named a bit too broadly) basically means a synth or sample, or any combination of the
 * two. And, to reiterate the above, it can exist as a “synth” as the melodic Output of one entire Clip(s), or as just a
 * Drum - one of the many items in a Kit, normally associated with a row of notes.
 */

class Sound : public ModControllableAudio, public virtual Voiced {
public:
	using ActiveVoice = AudioEngine::VoicePool::pointer_type;

	Sound();
	~Sound() override {
		if (!voices_.empty()) {
			noteDestroyedWithLiveVoices();
		}
		AudioEngine::sounds.remove(this);
	}

	/// @brief How many Sounds have been destroyed while they still had live voices, since boot.
	/// @note A destruction that finds live voices does not survive itself: once this body returns, the voices_
	///       member's destructor recycles each voice, and ~Voice unassigns it back into this Sound, whose
	///       reassessRenderSkippingStatus() then calls the pure virtual getArpSettings(). The count is taken first.
	/// @return The running count of such destructions since boot.
	static int32_t soundsDestroyedWithLiveVoices();

	/// @brief [render] Set by the render when it lets go of the retired song this Sound belongs to, just before it
	///        takes every Sound so marked out of AudioEngine::sounds in one pass. Never cleared: the Sound is
	///        destroyed when the song is.
	bool letGoWithSong = false;

	/// The Deluge instrument's settings for this sound, once bound: &ownedState_ from Song::adoptOutput / adoptDrum on,
	/// null before (a sound under preview or audition writes its runtime fields and snapshot directly).
	deluge::instrument::SoundState* state_{nullptr};
	/// Bind this sound's own state and seed it from the runtime fields, which the reader filled before the state was
	/// bound.
	void bindState();
	/// The bound state, or null while unbound.
	[[nodiscard]] const deluge::instrument::SoundState* boundState() const { return state_; }
	/// Set this sound's runtime fields from @p state (the reader's work for the instrument's own settings).
	/// @return INSUFFICIENT_RAM if a key-split range cannot be had.
	Error applyState(const deluge::instrument::SoundState& state);
	/// The state binding would seed from this sound's runtime fields now (bound or not), through the same seeds.
	[[nodiscard]] deluge::instrument::SoundState seededState() const;
	/// The element this sound was last read from, handed over once (to fill its instrument slot); empty if it was not
	/// read from a document tree.
	[[nodiscard]] std::optional<deluge::serialization::Value> takeLoadedElement() {
		return std::exchange(loadedElement_, std::nullopt);
	}
	// Copy the DOM-owned scalars engine->record. Assumes state_ is bound. Used as the load-bridge from bindState
	// AND re-run at doneReadingFromFile — a SoundDrum's adoptDrum binds the record BEFORE its fields are parsed, so the
	// bind-time seed is stale for drums and must be refreshed once parsing has finished.
	void seedDocSoundScalars();
	/// The scalars seedDocSoundScalars copies, into @p d.
	void seedScalarsInto(deluge::instrument::SoundState& d) const;

	// M4b: the engine's derived read-view of the hot Sound render fields, PROJECTED from state_ by
	// projectSnapshot() at bind / doneReadingFromFile / after edits; the render reads it (getTranspose() etc.).
	// Always present — unbound sounds (no state_) write it directly, so the render never needs a null-fallback.
	deluge::snapshot::SoundSnapshot snapshot_{};
	// Re-derive snapshot_ from state_ (no-op when unbound). Additive per-slice as fields move onto the snapshot.
	void projectSnapshot();
	[[nodiscard]] const deluge::snapshot::SoundSnapshot& snapshot() const { return snapshot_; }
	// Copy the 16 mod-knob ParamDescriptors engine->record. modKnobs is mutated in-place (setToHaveParamOnly etc.) at
	// several gestures rather than through one setter, so instead of funnelling each write we re-seed the whole block
	// after any mutation (bind, doneReadingFromFile, learnKnob, setSynthMode, modEncoderButtonAction, sample
	// auto-assign). The native ModKnob array stays the working structure; the record mirrors it and is what save reads.
	// Public so the external sample-load auto-assign can re-seed.
	void seedModKnobsRecord();
	/// The mod knobs seedModKnobsRecord copies, into @p d.
	void seedModKnobsInto(deluge::instrument::SoundState& d) const;

	Patcher patcher;

	ParamLPF paramLPF{};

	Source sources[kNumSources];

	// The per-sound *global* modulation data plane. params.paramFinalValues begins with FIRST_GLOBAL, so
	// subtract that from your p value before indexing it; params.sourceValues holds only the global sources.
	GlobalParams params;

	uint32_t sourcesChanged{}; // Applies from first source up to FIRST_UNCHANGEABLE_SOURCE

	LFO globalLFO1;
	LFO globalLFO3;

	bool invertReversed{}; // Used by the arpeggiator to invert the reverse flag just for the current voice

	// modKnobs was historically force-aligned (alignas(8), and before that a 0xDEADBEEF padding word) to mask
	// issue #2660 — a "release" bug from PR #2004, when four uint8_t LFO fields became LFOConfig and shifted
	// Sound's layout. The cause was the offsetof-indexed param/patcher plumbing, since replaced by std::span
	// over the ParamPlane POD (no offsetof; no layout-sensitive param access). The band-aid is therefore
	// vestigial — removed here, gated on the golden masters not moving (x86 + ARM) + a #2660-repro hardware
	// ear-check. https://github.com/SynthstromAudible/DelugeFirmware/issues/2660
	ModKnob modKnobs[kNumModButtons][kNumPhysicalModKnobs];

	int32_t sideChainSendLevel = 0;

	PolyphonyMode polyphonic = PolyphonyMode::POLY;
	uint8_t maxVoiceCount = 8;

	// M4b: `transpose` retired into the SoundSnapshot read-view (see snapshot_ / getTranspose()).

	// For sending MIDI notes for SoundDrums
	uint8_t outputMidiChannel = MIDI_CHANNEL_NONE;
	uint8_t outputMidiNoteForDrum = MIDI_NOTE_NONE;

	int32_t volumeNeutralValueForUnison{0};

	int32_t lastNoteCode = std::numeric_limits<int32_t>::min();

	// M3 owner-5: voice priority is DOM-owned (`state_->voice_priority`). It is read per render block in
	// Voice::getPriorityRating(), so this field is kept as a hot derived cache — the record is authoritative (seeded at
	// bind, written via setVoicePriority, read at save); render keeps reading this field directly.
	void setVoicePriority(VoicePriority value);

	// M3 owner-5: setters for the DOM-owned scalar batch (records in SoundState; native fields kept as caches, seeded
	// at bind, so this recipe is uniform whether the field is hot like oscillatorSync or cold). Each writes the record
	// (when bound) AND the cache; every runtime write site funnels through these, save reads the record.
	void setMaxVoiceCount(uint8_t value);
	void setSideChainSendLevel(int32_t value);
	void setOutputMidiChannel(uint8_t value);
	void setOutputMidiNoteForDrum(uint8_t value);
	void setOscillatorSync(bool value);
	// Slice C top-level scalars — same recipe. setSynthMode / setNumUnison / setUnison* / setModulator{Transpose,Cents}
	// already exist below (they do engine-side recalcs); those gained a record write too.
	void setPolyphonic(PolyphonyMode value);
	void setTranspose(int16_t value);
	[[nodiscard]] int16_t getTranspose() const { return snapshot_.transpose; }
	void setModulator1ToModulator0(bool value);
	void setOscRetriggerPhase(int32_t s, uint32_t value);
	void setModulatorRetriggerPhase(int32_t m, uint32_t value);
	// Store a modulator's transpose without setModulatorTranspose()'s phase-increment recalculation — for the load
	// path.
	void assignModulatorTranspose(int32_t m, int16_t value);
	// Store a modulator's cents without setModulatorCents()'s tuner and phase-increment recalculation — for the load
	// path.
	void assignModulatorCents(int32_t m, int8_t value);
	// Store the unison settings without recomputing the unison tables — for the load path and default setup;
	// doneReadingFromFile() and projectSnapshot() compute the tables.
	void assignNumUnison(uint8_t value);
	void assignUnisonDetune(int8_t value);
	void assignUnisonStereoSpread(uint8_t value);
	// LFO config (lfoConfig[] kept as hot render cache; record authoritative). Sync writes both fields together to
	// match the menu gesture (one sync value → type+level).
	void setLfoWaveType(int32_t lfoId, LFOType value);
	void setLfoSync(int32_t lfoId, SyncType syncType, SyncLevel syncLevel);

	bool skippingRendering = true;

	std::bitset<kNumExpressionDimensions> expressionSourcesChangedAtSynthLevel{0};

	// I really didn't want to store these here, since they're stored in the ParamManager, but.... complications! Always
	// 0 for Drums - that was part of the problem - a Drum's main ParamManager's expression data has been sent to the
	// "polyphonic" bit, and we don't want it to get referred to twice. These get manually refreshed in setActiveClip().
	std::array<int32_t, kNumExpressionDimensions> monophonicExpressionValues{};

	uint32_t timeStartedSkippingRenderingModFX{0};
	uint32_t timeStartedSkippingRenderingLFO{0};
	uint32_t timeStartedSkippingRenderingArp{0};
	uint32_t startSkippingRenderingAtTime = 0; // Valid when not 0. Allows a wait-time before render skipping starts,
	                                           // for if mod fx are on

	virtual ArpeggiatorSettings* getArpSettings(InstrumentClip* clip = nullptr) = 0;
	virtual void setSkippingRendering(bool newSkipping);

	ModFXType getModFXType() override;
	bool setModFXType(ModFXType newType) final;

	void patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
	                                    int32_t newValue);
	void render(ModelStackWithThreeMainThings* modelStack, std::span<StereoSample> output, int32_t* reverbBuffer,
	            int32_t sideChainHitPending, int32_t reverbAmountAdjust = 134217728,
	            bool shouldLimitDelayFeedback = false, int32_t pitchAdjust = kMaxSampleValue,
	            SampleRecorder* recorder = nullptr);

	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) final; // Song may be NULL
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack);
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager);
	void ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack);
	void ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p);

	PatchCableAcceptance maySourcePatchToParam(PatchSource s, uint8_t p, ParamManager* paramManager);

	void resyncGlobalLFOs();

	int8_t getKnobPos(uint8_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	int32_t getKnobPosBig(int32_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	bool learnKnob(MIDICable* cable, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
	               uint8_t midiChannel, Song* song) final;

	bool hasFilters();

	void sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack);
	void setNumUnison(int32_t newNum, ModelStackWithSoundFlags* modelStack);
	void setUnisonDetune(int32_t newAmount, ModelStackWithSoundFlags* modelStack);
	void setUnisonStereoSpread(int32_t newAmount);
	void setModulatorTranspose(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack);
	void setModulatorCents(int32_t m, int32_t value, ModelStackWithSoundFlags* modelStack);
	Error readFromFile(Deserializer& reader, ModelStackWithModControllable* modelStack, int32_t readAutomationUpToPos,
	                   ArpeggiatorSettings* arpSettings);
	void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager, ArpeggiatorSettings* arpSettings,
	                 const char* pathAttribute = NULL);

	void voiceUnassigned(ModelStackWithSoundFlags* modelStack);
	bool isSourceActiveCurrently(int32_t s, ParamManagerForTimeline* paramManager);
	bool isSourceActiveEverDisregardingMissingSample(int32_t s, ParamManager* paramManager);
	bool isSourceActiveEver(int32_t s, ParamManager* paramManager);
	bool isNoiseActiveEver(ParamManagerForTimeline* paramManager);
	void noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCode,
	            int16_t const* mpeValues, uint32_t sampleSyncLength = 0, int32_t ticksLate = 0,
	            uint32_t samplesLate = 0, int32_t velocity = 64, int32_t fromMIDIChannel = 16);
	void noteOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int32_t noteCode);
	void allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator);

	void noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t noteCode = -32768);
	void noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int32_t newNoteCodeBeforeArpeggiation,
	                           int32_t newNoteCodeAfterArpeggiation, int32_t velocity, int16_t const* mpeValues,
	                           uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
	                           int32_t fromMIDIChannel = 16);
	void polyphonicExpressionEventOnChannelOrNote(int32_t newValue, int32_t expressionDimension,
	                                              int32_t channelOrNoteNumber,
	                                              MIDICharacteristic whichCharacteristic) override;

	int16_t getMaxOscTranspose(InstrumentClip* clip);
	int16_t getMinOscTranspose();
	void setSynthMode(SynthMode value, Song* song);
	[[nodiscard]] SynthMode getSynthMode() const { return snapshot_.synthMode; }
	// Store the synth mode (bound: record, then reproject; unbound: the snapshot) without setSynthMode()'s gesture side
	// effects (voice kill, mod-knob and filter swaps) — for the load path.
	void assignSynthMode(SynthMode value);

	virtual bool isDrum() { return false; }
	void setupAsSample(ParamManagerForTimeline* paramManager);
	void recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack);
	void set_cents(int8_t new_cents);
	/// @brief Report the files of every sample or wavetable source to @p visitor, in source order. Loads nothing.
	/// @param visitor Receives a demand for each source's audio file and a checkpoint before each one.
	/// @return Walk::Stop if a checkpoint stopped the walk.
	deluge::storage::Walk walkSourceDemands(deluge::storage::SampleLoadVisitor& visitor);
	bool envelopeHasSustainCurrently(int32_t e, ParamManagerForTimeline* paramManager);
	bool envelopeHasSustainEver(int32_t e, ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager);
	bool renderingOscillatorSyncEver(ParamManager* paramManager);
	void setupAsBlankSynth(ParamManager* paramManager, bool is_dx = false);
	void setupAsDefaultSynth(ParamManager* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) final;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) final;
	// The real body; the override above wraps it to re-seed the modKnobs record after any in-place reassignment,
	// without having to touch this function's many early returns.
	bool modEncoderButtonActionImpl(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack);
	static void writeParamsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation);
	static void readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
	                               int32_t readAutomationUpToPos);
	static void initParams(ParamManager* paramManager);
	static Error createParamManagerForLoading(ParamManagerForTimeline* paramManager);
	int32_t hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength = false,
	                                 int32_t note = 0);
	int32_t hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int32_t note, bool* anyLooping = nullptr);
	bool hasCutModeSamples(ParamManagerForTimeline* paramManager);
	bool allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager);
	void fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack);
	void recalculatePatchingToParam(uint8_t p, ParamManagerForTimeline* paramManager);
	void doneReadingFromFile();
	virtual void setupPatchingForAllParamManagers(Song* song) {}
	void compensateVolumeForResonance(ModelStackWithThreeMainThings* modelStack);
	// void channelAftertouchReceivedFromInputMIDIChannel(int32_t newValue);
	ModelStackWithAutoParam* getParamFromModEncoder(int32_t whichModEncoder, ModelStackWithThreeMainThings* modelStack,
	                                                bool allowCreation = true) final;
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX = false);
	void getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager);
	virtual bool readTagFromFile(Deserializer& reader, char const* tagName) = 0;
	void detachSourcesFromAudioFiles();

	// Yup, inlining this helped a tiny bit.
	[[gnu::always_inline]] int32_t getSmoothedPatchedParamValue(int32_t p, ParamManager& paramManager) const {
		if (paramLPF.p == p) {
			return paramLPF.currentValue;
		}
		return paramManager.getPatchedParamSet()->getValue(p);
	}

	// Snapshot the per-block inputs the Patcher reads from ParamManager-world into a ControlBlock, so the
	// patcher needs no Sound&/ParamManager&. presetValue() on the result mirrors getSmoothedPatchedParamValue.
	ControlBlock patchControlBlock(ParamManager& paramManager) const {
		return {paramManager.getPatchedParamSet(), paramManager.getPatchCableSet(), paramLPF.p, paramLPF.currentValue};
	}

	void notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
	                             int32_t oldValue, int32_t newValue, bool fromAutomation);
	void deleteMultiRange(int32_t s, int32_t r);
	void wontBeRenderedForAWhile() override;
	ModelStackWithAutoParam* getParamFromMIDIKnob(MIDIKnob& knob, ModelStackWithThreeMainThings* modelStack) final;
	virtual ArpeggiatorBase* getArp() = 0;
	void possiblySetupDefaultExpressionPatching(ParamManager* paramManager);

	int32_t getShiftAmountForSaturation() const {
		return (modControllableSnapshot_.clippingAmount >= 2) ? (modControllableSnapshot_.clippingAmount - 2) : 0;
	}

	///	clipping amount must be greater than 0! Check before calling
	/// Shift amount is givben by getShiftAmountForSaturation
	[[gnu::always_inline]] void saturate(int32_t* data, uint32_t* workingValue, int32_t shiftAmount) {
		// Clipping
		//*data = getTanHUnknown(*data, 5 + clippingAmount) << (shiftAmount);
		*data = getTanHAntialiased(*data, workingValue, 5 + modControllableSnapshot_.clippingAmount) << (shiftAmount);
	}
	uint32_t getSyncedLFOPhaseIncrement(const LFOConfig& config);

	/// @brief Does this sound have any active voices?
	[[nodiscard]] bool hasActiveVoices() const override {
		AudioExcludedSection section; // the render adds and removes voices
		return !voices_.empty();
	}

	/// @brief Get the number of active voices
	[[nodiscard]] size_t numActiveVoices() const { return voices_.size(); }

	/// @brief Immediately ends all active voices
	void killAllVoices() override;

	/// @brief Get the voice with the lowest priority
	/// @return The voice with the lowest priority
	[[nodiscard]] const ActiveVoice& getLowestPriorityVoice() const;

	/// @brief Get the voices for this sound
	[[nodiscard]] const deluge::fast_vector<ActiveVoice>& voices() const { return voices_; }

	/// @brief Releases a given voice from the Sound
	/// @param voice The voice to release
	/// @param modelStack The model stack to use for the release
	/// @param erase Whether to erase the voice from the list of active voices (default: true)
	void freeActiveVoice(const ActiveVoice& voice, ModelStackWithSoundFlags* modelStack = nullptr, bool erase = true);

	// Voiced overrides
	bool anyNoteIsOn() override;
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) override;
	void prepareForHibernation() override;
	void process_postarp_notes(ModelStackWithSoundFlags* modelStackWithSoundFlags, ArpeggiatorSettings* arpSettings,
	                           ArpReturnInstruction instruction);

	virtual const char* getName() { return nullptr; }

private:
	/// The Deluge instrument's settings for this sound, owned here and bound on adoption (bindState).
	deluge::instrument::SoundState ownedState_;
	/// The element this sound was last read from (see takeLoadedElement).
	std::optional<deluge::serialization::Value> loadedElement_;
	/// @brief Count one destruction that found live voices (see soundsDestroyedWithLiveVoices()).
	static void noteDestroyedWithLiveVoices();

	uint32_t getGlobalLFOPhaseIncrement(LFO_ID lfoId, deluge::modulation::params::Global param);
	void recalculateFineTuner();
	void recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack);
	void setupUnisonDetuners(ModelStackWithSoundFlags* modelStack);
	void setupUnisonStereoSpread();
	void computeUnisonDetuners();
	void calculateEffectiveVolume();
	void ensureKnobReferencesCorrectVolume(Knob& knob);
	Error readTagFromFileOrError(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                             int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song);

	Error readSourceFromFile(Deserializer& reader, int32_t s, ParamManagerForTimeline* paramManager,
	                         int32_t readAutomationUpToPos);
	void stopSkippingRendering(ArpeggiatorSettings* arpSettings);
	void startSkippingRendering(ModelStackWithSoundFlags* modelStack);
	void getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings);
	void doParamLPF(int32_t numSamples, ModelStackWithSoundFlags* modelStack);
	void stopParamLPF(ModelStackWithSoundFlags* modelStack);

	bool renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack);
	void setupDefaultExpressionPatching(ParamManager* paramManager);
	void pushSwitchActionOnEncoderForParam(int32_t p, bool on, ModelStackWithThreeMainThings* modelStack);
	ModelStackWithAutoParam* getParamFromModEncoderDeeper(int32_t whichModEncoder,
	                                                      ModelStackWithThreeMainThings* modelStack,
	                                                      bool allowCreation = true);

	/// @brief the list of active voices for this sound
	// O(n) lookup is fine when most of the time we're iterating over all voices anyways.
	// Went with a vector instead of a list so that we don't need to always allocate if
	// we're constantly cycling between a minimum and maximum number of voices.
	deluge::fast_vector<ActiveVoice> voices_{};

	// Every structural edit of voices_ goes through these: the render walks every Sound's voices, so each edit runs
	// inside an AudioExcludedSection (free on the audio tier) and reports to the structural-edit probe
	// (util/structural_edit_probe.h).
	using ActiveVoiceIterator = deluge::fast_vector<ActiveVoice>::iterator;
	void publishVoice(ActiveVoice voice);
	template <class Pred>
	void eraseVoicesIf(Pred pred) {
		AudioExcludedSection section; // the render walks every Sound's voices
		noteVoicesEdit();
		std::erase_if(voices_, pred);
	}
	ActiveVoiceIterator eraseVoiceAt(ActiveVoiceIterator it);
	void clearVoices();
	static void noteVoicesEdit();

	/// @brief Re-trigger a voice already in voices_ (a MONO/LEGATO reuse, or a stolen voice), which may still be
	///        sounding: its envelopes resume from where they are. The caller excludes the render (or is the render
	///        itself).
	void retriggerVoice(const ActiveVoice& voice, ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp,
	                    int32_t noteCodePostArp, int32_t velocity, int16_t const* mpeValues, uint32_t sampleSyncLength,
	                    int32_t ticksLate, uint32_t samplesLate, int32_t fromMIDIChannel);

	/// @brief Check if a voice exists in the list of active voices
	void checkVoiceExists(const ActiveVoice& voice, const char* error) const;

	/// @brief Steals a currently-in-use voice for use by another note
	/// @return The stolen voice
	const ActiveVoice& stealOneActiveVoice();

	/// @brief Force a voice to release very quickly - will be almost instant but click-free
	void terminateOneActiveVoice();

	/// @brief Force a voice to release, or speed up its release if it's already releasing
	void forceReleaseOneActiveVoice();
};
