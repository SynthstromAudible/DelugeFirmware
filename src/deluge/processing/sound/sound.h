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
#include "deluge-project/instrument_slot.h"
#include "libdeluge/system.h"
#include "model/fx/channel_setup.h"
#include "model/fx/effects_owner.h"
#include "model/fx/knob_carrier.h"
#include "model/fx/reverb_candidate.h"
#include "model/fx/stutter_gesture.h"
#include "model/mod_controllable/mod_controllable.h"
#include "model/sample/sample_recorder.h"
#include "model/voiced.h"
#include "modulation/arpeggiator.h"
#include "modulation/knob.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/voice_budget.h"
#include "processing/sound/source_resources.h"
#include "serialization/value.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_block.h"
#include "synth/modulation/lfo.h"
#include "synth/modulation/param_plane.h"
#include "synth/modulation/patch_routing.h" // GLOBALITY_*
#include "synth/voice/sound_config.h"
#include "util/misc.h"
#include <atomic>
#include <bitset>
#include <memory>

struct CableGroup;
namespace deluge::dsp {
struct StereoSample;
} // namespace deluge::dsp
using StereoSample = deluge::dsp::StereoSample;
namespace deluge::project {
struct ArpRecord;
}

class SoundVoice;
class Kit;
class ParamManagerForTimeline;
class TimelineCounter;
class Clip;
class ModelStackWithThreeMainThings;
class ModelStackWithSoundFlags;
class ModelStackWithModControllable;

#include "effects/fx_section.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/voice_pool.h"
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

/// The depth of the VELOCITY -> LOCAL_VOLUME patch cable that Sound::setupAsSample() installs, as a user value.
constexpr int32_t kSampleDrumVelocityCableUserValue = 50;

/// @brief A cable depth from a preset number: 50 is a depth of 1 (Q30: 2^30 is 1), less the truncation of its step.
/// @param number The preset number, −50..50.
/// @return The cable's depth word.
[[nodiscard]] constexpr int32_t cableDepthFromPreset(int32_t number) {
	return number * 21474836;
}

/// @brief What a Sound's voices are made of, as far as their cost goes: the prior a Sound not yet measured is costed
/// at (see deluge::audio::VoiceCost).
enum class VoiceKind : uint8_t {
	OSCILLATOR, ///< Subtractive, every source an oscillator or a wavetable.
	SAMPLE,     ///< Subtractive, at least one source a sample or a live input.
	FM,         ///< FM, or any DX7 source.
	RINGMOD,    ///< Ring modulation.
};
inline constexpr size_t kNumVoiceKinds = 4;

class Sound;

/// @brief What a Sound's voice pool draws on: slots from the engine's voice pool, the render kept out by an
/// AudioExcludedSection, and the model stack a freed voice hands its sound's skipping reassess (null when the caller
/// has none).
struct SoundVoiceHost {
	using Slot = SoundVoice;
	using List = deluge::fast_vector<SoundVoice*>;
	using Exclusion = AudioExcludedSection;
	using Context = ModelStackWithSoundFlags*;

	Sound* sound; ///< The sound whose voices these are

	/// @brief A new voice of the sound from the engine's pool. @return It, or null when the pool has no memory.
	[[nodiscard]] SoundVoice* acquire() const;
	/// @brief Frees @p voice's resources while it is still listed, its sound's skipping reassessed with @p modelStack.
	static void unassign(SoundVoice& voice, Context modelStack);
	/// @brief Destroys @p voice and returns its memory to the engine's pool.
	static void release(SoundVoice& voice) noexcept;
	/// @brief Admits a voice costing @p cycles per block, which may cull another sound's voice.
	static void admit(uint32_t cycles);
	/// @brief A voice of the sound is about to start: its sidechain hit.
	void voice_starting() const;
	/// @brief After an edit of the sound's voices: its skipping reassess.
	void voices_changed() const;
	/// @brief The structural-edit probe's note of a voices edit.
	static void note_edit();
	/// @brief The engine's voice clock.
	static deluge::synth::VoiceClock clock();
};

class Sound : public ModControllable, public virtual Voiced {
public:
	Sound();
	~Sound() override {
		const bool liveVoices = !voices_.empty();
		if (liveVoices) {
			noteDestroyedWithLiveVoices();
		}
		AudioEngine::sounds.remove(this);
		deluge::model::releaseDelayMemory(core().delay());
		deluge::model::endStutterIfStuttering(core());
		if (liveVoices) {
			voices_.kill_all();
		}
	}

	/// @brief The sound's effects after its voices.
	/// @return The sound's FxSection.
	[[nodiscard]] deluge::effects::FxSection& fxSection() { return fxSection_; }
	[[nodiscard]] deluge::effects::ChainCore& core() { return fxSection_.core(); }
	[[nodiscard]] const deluge::effects::ChainCore& core() const { return fxSection_.core(); }
	/// @brief The sound's effect settings: the record's they are kept in, else the ones effectsBinding_ holds.
	/// @return The effect settings.
	[[nodiscard]] const deluge::effects::EffectStates& effects() const { return effectsBinding_.effects(); }
	/// @brief The render's copy of the effect settings: it reads nothing else of them.
	/// @return The render's copy.
	[[nodiscard]] const deluge::effects::EffectStates& renderEffects() const { return effectsBinding_.renderEffects(); }
	/// @brief Make @p next the sound's effect settings and project them (model::storeEffects).
	/// @param next The new effect settings.
	void setEffects(const deluge::effects::EffectStates& next) { deluge::model::storeEffects(*effectsOwner(), next); }
	/// @brief Keep the sound's effect settings in @p record, or in effectsBinding_ itself (model::bindEffects).
	/// @param record The record that holds the settings, or nullptr.
	void bindEffects(deluge::effects::EffectStates* record) { deluge::model::bindEffects(*effectsOwner(), record); }
	/// @brief Copy the settings into the render's copy, with the render excluded (model::projectEffects).
	void projectEffects() { deluge::model::projectEffects(*effectsOwner()); }
	[[nodiscard]] std::optional<deluge::model::EffectsOwner> effectsOwner() override {
		return deluge::model::EffectsOwner{
		    .core = &fxSection_.core(), .binding = &effectsBinding_, .sound = this, .knobs = &knobs_};
	}

	/// @brief How many Sounds have been destroyed while they still had live voices, since boot.
	/// @note A destruction that finds live voices does not survive itself: the destructor's last step kills them,
	///       and each one's unassign reaches this Sound's reassessRenderSkippingStatus(), which calls the pure virtual
	///       getArpSettings(). The count is taken first.
	/// @return The running count of such destructions since boot.
	static int32_t soundsDestroyedWithLiveVoices();

	/// @brief [render] Set by the render when it lets go of the retired song this Sound belongs to, just before it
	///        takes every Sound so marked out of AudioEngine::sounds in one pass. Never cleared: the Sound is
	///        destroyed when the song is.
	bool letGoWithSong = false;

	/// @brief The Deluge instrument's settings for this sound, from its construction on: its own state (ownedState_)
	/// until a record adopts it (Song::adoptOutput / adoptDrum), the record's from then until the record goes.
	deluge::instrument::SoundState* state_{&ownedState_};
	/// @brief Bind this sound into its record's slot: the slot's state (@p record) and routing (@p routing) take what
	/// the sound holds, its own moved in or another record's copied, and every setter writes the record while bound.
	/// @param record  The state the sound's record owns; it must outlive the binding (unbindState before it goes).
	/// @param routing The routing the same slot owns, beside @p record.
	void bindState(deluge::instrument::SoundState& record, deluge::project::SoundRouting& routing);
	/// @brief Play @p loaded, the state a load decoded or a setup made, its runtime already set to the same: in the
	/// slot's record the sound is bound into, else in its own state, which its record takes when it is adopted.
	/// @param loaded The state to play.
	void adoptState(deluge::instrument::SoundState loaded);
	/// @brief Take the state and the routing back from the record the sound is bound into, as the record goes: they
	/// move into the sound's own, which it then plays, and the record is left empty. Nothing when it plays its own
	/// state already.
	void unbindState();
	/// @brief Where this sound's output goes beyond its instrument: its record's slot's routing once adopted, its own
	/// (ownedRouting_) before, as a preset being read or the sample preview holds it.
	/// @return The routing.
	[[nodiscard]] const deluge::project::SoundRouting& routing() const { return *routing_; }
	/// @brief The routing to write a change into, before applyRouting() projects it.
	/// @return The routing.
	[[nodiscard]] deluge::project::SoundRouting& routingRecord() { return *routing_; }
	/// @brief Set the render's send and the MIDI output's channel and note from the routing.
	void applyRouting();
	/// @brief The state this sound plays: its record's once adopted, its own before.
	/// @return The state.
	[[nodiscard]] const deluge::instrument::SoundState& state() const { return *state_; }
	/// @brief The state this sound plays, to write a change into before applying it.
	/// @return The state.
	[[nodiscard]] deluge::instrument::SoundState& record() { return *state_; }
	/// @brief Set this sound's runtime fields from @p state (the reader's work for the instrument's own settings).
	/// @param state The settings to apply.
	/// @return INSUFFICIENT_RAM if a key-split range cannot be had.
	Error applyState(const deluge::instrument::SoundState& state);

	/// @brief The sound's settings as its voices play them, derived from state_ by projectSnapshot() at bind,
	/// doneReadingFromFile and after edits; the render reads it.
	///
	/// Always present, so the render never needs a fallback.
	deluge::synth::SoundConfig config_{};
	/// @brief Re-derive config_ from state_.
	void projectSnapshot();
	/// @brief The sound's settings as its voices play them.
	[[nodiscard]] const deluge::synth::SoundConfig& config() const { return config_; }
	/// @brief Oscillator @p s's first range, made when it has none and then recorded as the record's.
	/// @details The range it makes is the empty default, written to the record at once so a gesture's range Edit
	/// (`editRange`) finds it there; the gesture's own Edit then records what it puts in the range.
	/// @return The range, or null when there is no memory for one.
	MultiRange* getOrCreateFirstRange(int32_t s);
	/// @brief Record oscillator @p s's ranges as its runtime holds them (files found, zones loading clamped), when the
	/// record holds others.
	void recordHeldRanges(int32_t s);
	/// @brief Record oscillator @p s's DX7 patch as its runtime holds it, when the record holds another.
	void recordHeldDxPatch(int32_t s);
	/// @brief What oscillator @p s plays.
	[[nodiscard]] OscType oscType(int32_t s) const { return config_.sources[s].osc_type; }
	/// @brief Whether source @p s plays backwards now: its configured direction, inverted for an arpeggiated note.
	[[nodiscard]] bool playsReversed(int32_t s) const {
		return config_.sources[s].reversed != sourceResources[s].invertReversed;
	}

	/// @brief The sound's own patching pass over its param plane: the global params from the global sources.
	[[nodiscard]] deluge::synth::matrix::ScopePass matrixPass() {
		return {.scope = GLOBALITY_GLOBAL, .sources = params.source_values, .final_values = params.param_final_values};
	}
	/// @brief A note's first patching of the sound's own params, from every source.
	void performInitialPatching(const deluge::synth::matrix::MatrixBlock& block);

	ParamLPF paramLPF{};

	SourceResources sourceResources[kNumSources];

	// The per-sound *global* modulation data plane. params.paramFinalValues begins with FIRST_GLOBAL, so
	// subtract that from your p value before indexing it; params.sourceValues holds only the global sources.
	GlobalParams params;

	/// The sources whose values changed since the sound's render last read them, one bit each up to
	/// FIRST_UNCHANGEABLE_SOURCE. The render and param edits mark bits; the render clears only the bits its sound
	/// pass read, so a bit marked while it renders is read by the next block rather than lost.
	std::atomic<uint32_t> sourcesChanged{};

	LFO globalLFO1;
	LFO globalLFO3;

	bool invertReversed{}; // Used by the arpeggiator to invert the reverse flag just for the current voice

	// modKnobs was historically force-aligned (alignas(8), and before that a 0xDEADBEEF padding word) to mask
	// issue #2660 — a "release" bug from PR #2004, when four uint8_t LFO fields became one per-LFO struct and shifted
	// Sound's layout. The cause was the offsetof-indexed param/patcher plumbing, since replaced by std::span
	// over the ParamPlane POD (no offsetof; no layout-sensitive param access). The band-aid is therefore
	// vestigial — removed here, gated on the golden masters not moving (x86 + ARM) + a #2660-repro hardware
	// ear-check. https://github.com/SynthstromAudible/DelugeFirmware/issues/2660
	ModKnob modKnobs[kNumModButtons][kNumPhysicalModKnobs];

	int32_t sideChainSendLevel = 0;

	// The sound's transpose lives in the SoundConfig read-view (see config_ / getTranspose()).

	// For sending MIDI notes for SoundDrums
	uint8_t outputMidiChannel = MIDI_CHANNEL_NONE;
	uint8_t outputMidiNoteForDrum = MIDI_NOTE_NONE;

	int32_t lastNoteCode = std::numeric_limits<int32_t>::min();

	[[nodiscard]] int16_t getTranspose() const { return config_.voicing.transpose; }

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

	void patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
	                                    int32_t newValue);
	void render(ModelStackWithThreeMainThings* modelStack, std::span<StereoSample> output,
	            deluge::dsp::Q<31>* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust = 134217728,
	            bool shouldLimitDelayFeedback = false, deluge::dsp::Q<24> pitchAdjust = deluge::dsp::Q<24>::one(),
	            SampleRecorder* recorder = nullptr);

	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song); // Song may be NULL
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack);
	/// @param song The song whose table holds this sound's learned knobs, or null for none.
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager,
	                                                                              Song* song);
	void ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack);
	void ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int32_t p, Song* song);
	/// @brief Whether a learned MIDI knob of this sound turns patched param @p p: one @p song's table holds at this
	/// sound's id, or, before the table takes them, one it holds outside the table
	/// (model::KnobCarrier::loaded).
	/// @param song The song, or null for none.
	/// @param p    The patched param.
	/// @return True if one does.
	[[nodiscard]] bool learnedKnobTurns(Song* song, int32_t p);

	PatchCableAcceptance maySourcePatchToParam(PatchSource s, uint8_t p, ParamManager* paramManager);

	void resyncGlobalLFOs(bool reset_state = false);

	int8_t getKnobPos(uint8_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	int32_t getKnobPosBig(int32_t p, ParamManagerForTimeline* paramManager, uint32_t timePos, TimelineCounter* counter);
	/// @return Whether the last read of this sound's element held an arpeggiator (a synth track's own, which its record
	/// then keeps).
	[[nodiscard]] bool arpRead() const { return arp_read_; }

	/// @brief Learn mod knob @p whichKnob of mode @p modKnobMode to @p target.
	/// @param target      The param the knob turns.
	/// @param whichKnob   The mod knob: 0 lower, 1 upper.
	/// @param modKnobMode The mod knob mode it is learned in.
	/// @param song        The song the sound belongs to, or nullptr.
	/// @return Whether the knob was learned.
	bool learnModKnob(deluge::modulation::KnobTarget target, uint8_t whichKnob, uint8_t modKnobMode, Song* song);

	bool hasFilters();

	void sampleZoneChanged(MarkerType markerType, int32_t s, ModelStackWithSoundFlags* modelStack);
	/// @brief Give the live voices' sample parts the new unison count: a part that comes plays on from its neighbour,
	/// a part that goes lets go of its clusters. The count, detunes and level are already the new ones.
	void revoiceUnison(int32_t oldNum, int32_t newNum, ModelStackWithSoundFlags* modelStack);
	Error readFromFile(Deserializer& reader, ModelStackWithModControllable* modelStack, int32_t readAutomationUpToPos,
	                   ArpeggiatorSettings* arpSettings);
	/// @param knobsSong   The song whose table holds the sound's learned knobs (the one being saved, or the one a
	///                    preset is saved from), or null for none.
	/// @param arpSettings The arp a sound's element carries (a kit row's, or a synth preset's from its clip), or null.
	/// @param arpRecord   That arp's record, which its settings are written from; null where it has none.
	void writeToFile(Serializer& writer, bool savingSong, ParamManager* paramManager, Song* knobsSong,
	                 ArpeggiatorSettings* arpSettings, const deluge::project::ArpRecord* arpRecord,
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
	/// @brief Send a per-note or channel expression change on the sound's MIDI out channel, if it has one: pressure
	///        only, as poly or channel aftertouch.
	void sendPolyphonicExpressionToMidiOut(int32_t newValue, int32_t expressionDimension, int32_t channelOrNoteNumber,
	                                       MIDICharacteristic whichCharacteristic);

	int16_t getMaxOscTranspose(InstrumentClip* clip);
	int16_t getMinOscTranspose();
	[[nodiscard]] SynthMode getSynthMode() const { return config_.engine.mode; }
	/// @brief What this Sound's voices are made of, as far as their cost goes.
	[[nodiscard]] VoiceKind voiceKind() const;

	virtual bool isDrum() { return false; }
	void setupAsSample(ParamManagerForTimeline* paramManager);
	void recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack);
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
	void recalculatePatchingToParam(uint8_t p, ParamManager* paramManager);
	void doneReadingFromFile();
	virtual void setupPatchingForAllParamManagers(Song* song) {}
	// void channelAftertouchReceivedFromInputMIDIChannel(int32_t newValue);
	void reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX = false);
	/// @brief The reverb's auto duck's search past this sound: its send, when @p paramManager holds one unautomated and
	/// it beats @p best; then how it ducks the return.
	/// @param best The best so far.
	/// @param paramManager The params the sound plays: its clip's or, for a drum, its note row's.
	/// @return The new best.
	[[nodiscard]] deluge::model::ReverbCandidate reverbCandidate(deluge::model::ReverbCandidate best,
	                                                             ParamManagerForTimeline& paramManager);
	virtual bool readTagFromFile(Deserializer& reader, char const* tagName) = 0;
	void detachSourcesFromAudioFiles();

	// Yup, inlining this helped a tiny bit.
	[[gnu::always_inline]] int32_t getSmoothedPatchedParamValue(int32_t p, ParamManager& paramManager) const {
		if (paramLPF.p == p) {
			return paramLPF.currentValue;
		}
		return paramManager.getPatchedParamSet()->getValue(p);
	}

	/// @brief Which preset values a MatrixBlock gathers: the routed destinations' (the per-block patching reads only
	/// those) or every param's (the initial patching and a recalculation of a param with no cables).
	enum class PresetGather : uint8_t { ROUTED, ALL };

	/// @brief Gather what the matrix reads beside its program into a MatrixBlock, as copies: the program, the preset
	/// values
	/// @p gather asks for (the smoothed one for the param being smoothed, as getSmoothedPatchedParamValue gives) and
	/// each routed cable's depth.
	deluge::synth::matrix::MatrixBlock matrixBlock(ParamManager& paramManager, PresetGather gather) const;

	void notifyValueChangeViaLPF(int32_t p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
	                             int32_t oldValue, int32_t newValue, bool fromAutomation);
	void deleteMultiRange(int32_t s, int32_t r);
	void wontBeRenderedForAWhile();
	virtual ArpeggiatorBase* getArp() = 0;

	deluge::dsp::UQ<32> getSyncedLFOPhaseIncrement(const deluge::synth::LfoState& config);

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
	[[nodiscard]] SoundVoice* getLowestPriorityVoice() const;

	/// @brief Get the voices for this sound
	[[nodiscard]] const SoundVoiceHost::List& voices() const { return voices_.list(); }
	/// @brief The cycles a new voice of this Sound is predicted to cost per 128-frame block: its measured cost, or
	/// its kind's prior until it has one.
	[[nodiscard]] uint32_t predictedVoiceCycles() const {
		return voiceCost_.per_block(deluge::audio::kPriorCyclesPerVoiceBlock[static_cast<size_t>(voiceKind())]);
	}

	/// @brief Releases a given voice from the Sound
	/// @param voice The voice to release
	/// @param modelStack The model stack to use for the release
	/// @param erase Whether to erase the voice from the list of active voices (default: true)
	void freeActiveVoice(SoundVoice* voice, ModelStackWithSoundFlags* modelStack = nullptr, bool erase = true);

	// Voiced overrides
	bool anyNoteIsOn() override;
	bool allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop = false) override;
	void prepareForHibernation() override;
	void process_postarp_notes(ModelStackWithSoundFlags* modelStackWithSoundFlags, ArpeggiatorSettings* arpSettings,
	                           ArpReturnInstruction instruction);

	virtual const char* getName() { return nullptr; }

private:
	friend struct SoundVoiceHost;

	/// @brief Its voices' measured cost per 128-frame block: written by the render, read by a note-on with the render
	/// excluded.
	deluge::audio::VoiceCost voiceCost_;
	/// @brief The Deluge instrument's settings for this sound, owned here and bound on adoption (bindState).
	deluge::instrument::SoundState ownedState_;
	/// @brief The sound's routing, owned here until a record adopts it (bindState).
	deluge::project::SoundRouting ownedRouting_;
	/// @brief The routing the sound plays: ownedRouting_, or its record's slot's.
	deluge::project::SoundRouting* routing_{&ownedRouting_};
	/// @brief Count one destruction that found live voices (see soundsDestroyedWithLiveVoices()).
	static void noteDestroyedWithLiveVoices();

	template <deluge::modulation::params::Global P>
	deluge::dsp::UQ<32> getGlobalLFOPhaseIncrement(LFO_ID lfoId);
	void recalculateFineTuner();
	void recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack);
	void setupUnisonDetuners(ModelStackWithSoundFlags* modelStack);
	/// @brief The unison layout and level its count, detune and spread make (synth::derive_unison).
	void refreshUnison();
	void ensureKnobReferencesCorrectVolume(ModKnob& knob);
	Error readTagFromFileOrError(Deserializer& reader, char const* tagName, ParamManagerForTimeline* paramManager,
	                             int32_t readAutomationUpToPos, ArpeggiatorSettings* arpSettings, Song* song);

	void stopSkippingRendering(ArpeggiatorSettings* arpSettings);
	void startSkippingRendering(ModelStackWithSoundFlags* modelStack);
	void getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings);
	void doParamLPF(int32_t numSamples, ModelStackWithSoundFlags* modelStack);
	void stopParamLPF(ModelStackWithSoundFlags* modelStack);

	bool renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack);
	void setupDefaultExpressionPatching(ParamManager* paramManager);

	/// @brief The sound's effect settings and the render's copy of them.
	deluge::model::EffectsBinding effectsBinding_;
	/// @brief The sound's effects after its voices; outlives voices_, whose teardown reads its delay.
	deluge::effects::FxSection fxSection_;
	/// @brief The learned knobs the sound holds outside a song's table.
	deluge::model::KnobCarrier knobs_;
	/// @brief Set by a read whose element held an arpeggiator.
	bool arp_read_{false};

	/// @brief What the sound's voice pool draws on; declared before it, which holds it.
	SoundVoiceHost voiceHost_{this};
	/// @brief The sound's voices. Only the pool edits the list, each edit inside an AudioExcludedSection (free on the
	/// audio tier, the render walking every Sound's voices) and reported to the structural-edit probe
	/// (util/structural_edit_probe.h).
	deluge::instrument::VoicePool<SoundVoiceHost> voices_{voiceHost_};

	static void noteVoicesEdit();

	/// @brief Re-trigger a voice already in voices_ (a MONO/LEGATO reuse, or a stolen voice), which may still be
	///        sounding: its envelopes resume from where they are. The caller excludes the render (or is the render
	///        itself).
	void retriggerVoice(SoundVoice* voice, ModelStackWithSoundFlags* modelStack, int32_t noteCodePreArp,
	                    int32_t noteCodePostArp, int32_t velocity, int16_t const* mpeValues, uint32_t sampleSyncLength,
	                    int32_t ticksLate, uint32_t samplesLate, int32_t fromMIDIChannel);

	/// @brief Check if a voice exists in the list of active voices
	void checkVoiceExists(const SoundVoice* voice, const char* error) const;

	/// @brief Steals a currently-in-use voice for use by another note
	/// @return The stolen voice
	SoundVoice* stealOneActiveVoice();

	/// @brief Force a voice to release very quickly - will be almost instant but click-free
	void terminateOneActiveVoice();

	/// @brief Force a voice to release, or speed up its release if it's already releasing
	void forceReleaseOneActiveVoice();
};
