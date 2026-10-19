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
#include "deluge-project/song.h"
#include "engines/effects/master_chain.h"
#include "gui/menu_item/reverb/model.h"
#include "gui/ui/browser/file_listing.h"
#include "io/midi/learned_midi.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/project_bridge/song_live.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_mapper.h"
#include "model/song/song_view_state.h"
#include "model/timeline_counter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/sync.h"
#include "storage/flash_storage.h"
#include "storage/preset_acquisition.h"
#include "util/c_string.h"
#include "util/containers.h"
#include "util/etl_string.h"
#include <atomic>
#include <utility>

class MidiCommand;
namespace deluge::storage {
struct AcquireResult;
}
class Clip;
class AudioClip;
class Instrument;
class InstrumentClip;
class Synth;
class ParamManagerForTimeline;
class Instrument;
class ParamManagerForTimeline;
class Drum;
class SoundInstrument;
class SoundDrum;
class Action;
class ArrangementRow;
class ArpeggiatorSettings;
class Kit;
class MIDIInstrument;
class NoteRow;
class Output;
class AudioOutput;
class ModelStack;
class ModelStackWithTimelineCounter;
Clip* getCurrentClip();
InstrumentClip* getCurrentInstrumentClip();
AudioClip* getCurrentAudioClip();
Output* getCurrentOutput();
Kit* getCurrentKit();
Instrument* getCurrentInstrument();
OutputType getCurrentOutputType();

#define MAX_NOTES_CHORD_MEM 10

class Song final : public TimelineCounter {
public:
	Song();
	~Song() override;

	/// @brief How many Songs have been destroyed while the render could still reach them, since boot: the Song was
	///        still currentSong, or a Sound of one of its outputs (a synth, or a kit's sound drum) was still in the
	///        Sound registry. Checked at the top of ~Song's body, before anything is torn down.
	static int32_t songsDestroyedWhileReachable();
	/// @brief How many retired objects (AudioEngine::retiredPending()) were still waiting to be freed when the most
	///        recent Song was constructed: a stopped-clock load or clear must have freed the old song before it builds
	///        the new one, so this reads 0 right after one. Evidence, not state; -1 before any Song is built.
	static int32_t retiredPendingAtLastConstruction();
	bool mayDoubleTempo();
	/// @brief Build the first session Clip if the song has none, on the built-in default synth.
	///
	/// Reads no card: it runs before the interaction tier exists (boot), and in clear-song on a Song that is not yet
	/// current, so the preset that replaces the default synth is asked for later, through @ref blankPresetPending.
	/// @return true if a Clip was created.
	bool ensureAtLeastOneSessionClip();
	/// @brief Take @p acquired -- the preset the blank song asked for -- in place of the default synth.
	///
	/// Does nothing unless the clip still holds that synth, untouched: anything else means the user got there first,
	/// and the delivery's own cleanup deletes the preset nobody took.
	/// Returns whether the preset replaced the default synth, which is then freed.
	bool completeBlankPreset(const deluge::storage::AcquireResult& acquired);

	/// @name The blank song's first preset
	/// @{
	/// Whether this song still wants the first SYNTHS preset in place of its default synth. Cleared by
	/// InstrumentLoad::routine() as it asks for one.
	bool blankPresetPending = false;
	Instrument* blankPresetInstrument = nullptr; ///< The default synth the preset would replace.
	InstrumentClip* blankPresetClip = nullptr;   ///< The clip that synth was built for.
	/// @}
	/// [interaction] -> [render] Set by AudioEngine::songSwapAboutToHappen() before it deletes the sounds that will not
	/// sound until a swap replaces this song; cleared again only if the swap never comes (a failed load). The render's
	/// reverb update reads it and leaves the reverb as it is while it is set. A property of the song, so a swap needs
	/// no write: the incoming song's is clear.
	std::atomic<bool> soundsDeletedForSwap{false};
	void transposeAllScaleModeClips(int32_t interval);
	void transposeAllScaleModeClips(int32_t offset, bool chromatic);
	bool anyScaleModeClips();
	void changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change);
	void rotateMusicalMode(int8_t change);
	void replaceMusicalMode(const ScaleChange& changes, bool affectMIDITranspose);
	ParamManagerForTimeline* findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = nullptr);
	void setupPatchingForAllParamManagersForDrum(SoundDrum* drum);
	void setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForInstrument(MIDICable& cable,
	                                                                                      SoundInstrument* instrument);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForAllParamManagersForDrum(MIDICable& cable, SoundDrum* drum,
	                                                                                Kit* kit);
	void grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything(MIDICable& cable);
	void getCurrentRootNoteAndScaleName(etl::istring& buffer);
	void displayCurrentRootNoteAndScaleName();

	// Scale-related methods

	/// Changes to next applicable scale.
	Scale cycleThroughScales();
	/// Returns current scale
	/// Changes to requested scale, will return the scale if successfull, or NO_SCALE if change
	/// could not be completed.
	Scale setScale(Scale newScale);
	/// Changes scale to the requested notes. Returns true if successful. If the notes are not a preset
	/// scale, defines the scale as the new user scale.
	bool setScaleNotes(NoteSet newScale);
	/// Learns a user scale from notes in scale mode clips.
	/// Returns true if the song has a user scale.
	/// Sets root note of key. If the previous scale no longer fits, changes to a new implied scale, which
	/// can result in a new user scale being set.
	void setRootNote(int32_t newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor = nullptr);
	/// Returns a NoteSet with all notes currently in used in scale mdoe clips.
	NoteSet notesInScaleModeClips();

	void setTempoFromNumSamples(double newTempoSamples, bool shouldLogAction);
	void setupDefault();
	void setBPM(float tempoBPM, bool shouldLogAction);
	void setTempoFromParams(int32_t magnitude, int8_t whichValue, bool shouldLogAction);
	void deleteSoundsWhichWontSound();
	/// @brief [interaction] Delete a clip the caller has already taken out of this song's arrays. Unless
	///        @p songBeingDestroyedToo, prepares it for destruction and hands it to AudioEngine::retire(), so it is
	///        freed only after the render let go of it: the caller must not touch @p clip afterwards. From ~Song()
	///        (@p songBeingDestroyedToo), destroys it at once.
	void
	deleteClipObject(Clip* clip, bool songBeingDestroyedToo = false,
	                 InstrumentRemoval instrumentRemovalInstruction = InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED);
	int32_t getMaxMIDIChannelSuffix(int32_t channel);
	void addOutput(Output* output, bool atStart = true);
	void deleteOutputThatIsInMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void markAllInstrumentsAsEdited();
	Instrument* getInstrumentFromPresetSlot(OutputType outputType, int32_t presetNumber, int32_t presetSubSlotNumber,
	                                        char const* name, char const* dirPath, bool searchHibernatingToo = true,
	                                        bool searchNonHibernating = true);
	AudioOutput* getAudioOutputFromName(std::string_view name);
	void setupPatchingForAllParamManagers();
	void replaceInstrument(Instrument* oldInstrument, Instrument* newInstrument, bool keepNoteRowsWithMIDIInput = true);
	/// Move @p output one place along the output list: towards the end if @p direction >= 0, else towards the start.
	/// The caller has checked there is an output to swap with. (The arranger's output drag.)
	void moveOutput(Output* output, int32_t direction);
	void stopAllMIDIAndGateNotesPlaying();
	void stopAllAuditioning();
	void deleteOrHibernateOutput(Output* output);
	Instrument* getNonAudioInstrumentToSwitchTo(OutputType newOutputType, Availability availabilityRequirement,
	                                            int16_t newSlot, int8_t newSubSlot, bool* instrumentWasAlreadyInSong);
	void removeSessionClipLowLevel(Clip* clip, int32_t clipIndex);
	void changeSwingInterval(int32_t newValue);
	int32_t convertSyncLevelFromFileValueToInternalValue(int32_t fileValue);
	int32_t convertSyncLevelFromInternalValueToFileValue(int32_t internalValue);
	std::string getSongFullPath();
	void setSongFullPath(const char* fullPath);
	int32_t getInputTickMagnitude() const {
		return project.transport.inside_world_tick_magnitude
		       + project.transport.inside_world_tick_magnitude_offset_from_bpm;
	}

	// The project model (deluge::project::Song), which ::Song, the controller, owns
	// (docs/dev/project_model_extraction.md). Named `project` rather than `model` because `model` is already the
	// reverb-model member.
	deluge::project::Song project;
	/// The parts of the song the engine still holds as live objects.
	SongLive live;

	// View/UI state that is serialized to the song file (scroll/zoom/layout/triplets/etc.).
	PersistentViewState persistentView;

	// project.transport.inside_world_tick_magnitude is how many orders of magnitude faster internal ticks are going
	// than input ticks. Used in combination with inputTickScale, which is usually 1, but is different if there's an
	// inputTickScaleClip. So, e.g. if it is 1, the inside world is spinning twice as fast as the external world, so
	// MIDI sync coming in representing an 8th-note would be interpreted internally as a quarter-note (because two
	// internal 8th-notes would have happened, twice as fast, making a quarter-note).
	//
	// Sometimes we do weird stuff to the magnitude for sync-scaling, which would make BPM values look weird. So we
	// keep project.transport.inside_world_tick_magnitude_offset_from_bpm separately.

	uint8_t sectionToReturnToAfterSongEnd{};

	bool wasLastInArrangementEditor{};

	bool outputClipInstanceListIsCurrentlyInvalid; // Set to true during scenarios like replaceInstrument(), to warn
	                                               // other functions not to look at Output::clipInstances

	// View/UI state used at runtime but never serialized (selection, transient flags).
	RuntimeViewState runtimeView;

	bool getAnyClipsSoloing() const;
	Clip* getCurrentClip();
	void setCurrentClip(Clip* clip) {
		if (runtimeView.currentClip != nullptr) {
			runtimeView.previousClip = runtimeView.currentClip;
		}
		runtimeView.currentClip = clip;
	}
	uint32_t getInputTickScale();
	Clip* getSyncScalingClip();
	void setInputTickScaleClip(Clip* clip);
	/// @brief Point the input clock's scaling at @p clip, in the engine and the record, with no re-derivation of the
	///        tick magnitude (load, a swap, a deleted clip).
	/// @param clip The clip, or nullptr for none.
	void setSyncScalingClipRaw(Clip* clip);
	/// @brief Give @p clip's value layer, and its rows' records, the bend ranges its expression sets hold where the
	///        file gave none (load).
	/// @param clip The clip.
	void seedBendRangesFromEngine(Clip& clip);
	inline bool isFillModeActive() { return runtimeView.fillModeActive; }
	void changeFillMode(bool on);
	void loadNextSong();
	void setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip = true);
	void doubleClipLength(InstrumentClip* clip, Action* action = nullptr);
	Clip* getClipWithOutput(Output* output, bool mustBeActive = false, Clip* excludeClip = nullptr);
	/// @brief Read the engine's part of a song document; the model's parts are decoded before (bridge::loadSong).
	/// @param reader           The song file reader.
	/// @param timePerTimerTick The tempo the model decoded, applied once the song is read.
	/// @return Error::NONE on success, else the read failure.
	Error readFromFile(Deserializer& reader, uint64_t timePerTimerTick);

	/// @brief Give every output and kit drum of the song that lacks one a durable id, then bind each to its DOM record
	///        (adoptOutput / adoptDrum).
	///
	/// Clips already have an id from construction. Called at load before the param relocation; idempotent.
	void assignMissingIds();

	/// @brief Bind a song-resident drum to the persistent DOM structural store.
	///
	/// Idempotent: returns early if already bound. Mints a durable id via generateNewDrumId() only if the drum has
	/// none, then gets or creates the drum's DocDrum in project.drum_structure, seeds it from the engine drum, and
	/// points docDrum_ at it. Called per drum from assignMissingIds (load) and Kit::addDrum (runtime).
	///
	/// @param drum The drum to bind.
	void adoptDrum(Drum* drum);
	/// @brief Whether this song's own file is being read: its bindings go into its table once every id is final.
	[[nodiscard]] bool isReadingFromFile() const { return readingFromFile_; }
	/// @brief Drop a drum's structural record when the drum is genuinely destroyed, so the store doesn't accumulate
	///        dead entries.
	///
	/// Guarded like removeDocClip: skipped during whole-song teardown.
	///
	/// @param drum The destroyed drum.
	void removeDocDrum(Drum* drum);

	/// @brief Bind a song-resident output to the persistent DOM identity store.
	///
	/// Idempotent: returns early if already bound. Mints a durable id via generateNewOutputId() only if the output
	/// has none, then gets or creates the output's DocOutput in project.output_structure, seeds it via
	/// Output::seedDocOutputIdentity(), and points docOutput_ at it. Called per output from assignMissingIds (load).
	///
	/// @param output The output to bind.
	void adoptOutput(Output* output);
	/// @brief Drop an output's identity record when the output is genuinely destroyed, so the store doesn't
	///        accumulate dead entries.
	///
	/// Guarded like removeDocDrum: skipped during whole-song teardown.
	///
	/// @param output The destroyed output.
	void removeDocOutput(Output* output);

	void writeToFile(Serializer& writer);
	void loadAllSamples(bool mayActuallyReadFiles = true, deluge::storage::AudioLoadContext* context = nullptr);
	/// @brief Report every audio file the song names to @p visitor, in load order: each output's, then each audio
	///        clip's, with a checkpoint before every clip. Loads nothing.
	void walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor);
	/// @brief Renders every output into @p outputBuffer and feeds the mix recorders, then ticks the song's own params.
	/// @param outputBuffer        Stereo mix buffer to render into.
	/// @param reverbBuffer        Reverb send buffer.
	/// @param sideChainHitPending Pending sidechain hit.
	/// @return The master chain's inputs for the half before the reverb, read before that tick.
	deluge::effects::MasterChainInputs renderAudio(std::span<StereoSample> outputBuffer, int32_t* reverbBuffer,
	                                               int32_t sideChainHitPending);
	bool isYNoteAllowed(int32_t yNote, bool inKeyMode);
	void setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction = false);
	bool hasAnySwing();
	void resyncLFOs(bool reset_state = false);
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Sound* sound);
	bool areAllClipsInSectionPlaying(int32_t section);
	void removeYNoteFromMode(int32_t yNoteWithinOctave);
	void turnSoloingIntoJustPlaying(bool getRidOfArmingToo = true);
	void reassessWhetherAnyClipsSoloing();
	float getTimePerTimerTickFloat();
	uint32_t getTimePerTimerTickRounded();
	int32_t getNumOutputs();
	Clip* getNextSessionClipWithOutput(int32_t offset, Output* output, Clip* prevClip);
	bool anyClipsSoloing;

	void moveInstrumentToHibernationList(Instrument* instrument);
	void deleteOrHibernateOutputIfNoClips(Output* output);
	void removeInstrumentFromHibernationList(Instrument* instrument);
	bool doesOutputHaveActiveClipInSession(Output* output);
	bool doesNonAudioSlotHaveActiveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot = -1);
	bool doesNonAudioSlotHaveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot);
	bool doesOutputHaveAnyClips(Output* output);
	void deleteBackedUpParamManagersForClip(Clip* clip);
	void deleteHibernatingInstrumentWithSlot(OutputType outputType, char const* name);
	void loadCrucialSamplesOnly(deluge::storage::AudioLoadContext* context = nullptr);
	/// @brief As walkSampleDemands(), but only what the active clips need to sound, with no clip checkpoints.
	void walkCrucialSampleDemands(deluge::storage::SampleLoadVisitor& visitor);
	Clip* getSessionClipWithOutput(Output* output, int32_t requireSection = -1, Clip* excludeClip = nullptr,
	                               int32_t* clipIndex = nullptr, bool excludePendingOverdubs = false);
	void restoreClipStatesBeforeArrangementPlay();
	void deleteOrAddToHibernationListOutput(Output* output);
	int32_t getLowestSectionWithNoSessionClipForOutput(Output* output);
	void assertActiveness(ModelStackWithTimelineCounter* modelStack, int32_t endInstanceAtTime = -1);
	[[nodiscard]] bool isClipActive(Clip const* clip) const;
	void sendAllMIDIPGMs();
	void sortOutWhichClipsAreActiveWithoutSendingPGMs(ModelStack* modelStack, int32_t playbackWillStartInArrangerAtPos);
	void deactivateAnyArrangementOnlyClips();
	Clip* getLongestClip(bool includePlayDisabled, bool includeArrangementOnly);
	Clip* getLongestActiveClipWithMultipleOrFactorLength(int32_t targetLength, bool revertToAnyActiveClipIfNone = true,
	                                                     Clip* excludeClip = nullptr);
	int32_t getOutputIndex(Output* output);
	void setHibernatingMIDIInstrument(MIDIInstrument* newInstrument);
	void deleteHibernatingMIDIInstrument();
	MIDIInstrument* grabHibernatingMIDIInstrument(int32_t newSlot, int32_t newSubSlot);
	NoteRow* findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = nullptr);

	bool anyOutputsSoloingInArrangement;
	bool getAnyOutputsSoloingInArrangement();
	void reassessWhetherAnyOutputsSoloingInArrangement();
	bool isOutputActiveInArrangement(Output* output);
	Output* getOutputFromIndex(int32_t index);

	// --- Stable per-song serial identity for clips/outputs ---
	// Each clip/output carries a persisted serial id (Clip::id / Output::id) from these per-song monotonic counters:
	// a clip takes one at construction, outputs and drums at load (assignMissingIds) or adoption; loaded ids are kept.
	// Unlike the transient positional index, the id survives reorders and reloads, so references (e.g. session macros)
	// can resolve by id instead of position.
	uint32_t generateNewClipId() { return clipIdCounter_++; }
	/// A new output id: past every id the counter has seen, and one no output record of the song holds (a hibernating
	/// instrument's id is not walked when the counter catches up after a load).
	uint32_t generateNewOutputId() {
		while (project.output_structure.find(outputIdCounter_) != nullptr) {
			outputIdCounter_++;
		}
		return outputIdCounter_++;
	}
	/// The durable id of @p output, a member of this song, minting one first if it has none yet (an output made
	/// since the load gets one only when something needs to name it).
	uint32_t outputIdFor(Output& output) {
		if (!output.id()) {
			output.setId(generateNewOutputId());
		}
		return *output.id();
	}
	/// A new drum id: past every id the counter has seen, and one no drum record of the song holds, so a new drum never
	/// shares a record with a drum the counter has not passed (a hibernating kit's).
	uint32_t generateNewDrumId() {
		while (project.drum_structure.find(drumIdCounter_) != nullptr) {
			drumIdCounter_++;
		}
		return drumIdCounter_++;
	}
	/// Advance the counters past every id seen in the just-loaded song, so newly created objects can't
	/// collide with restored ids. Called once after a load completes.
	void syncSerialIdCountersFromLoaded();
	/// Resolve a serial id to its live object, or nullptr if no object carries that id.
	[[nodiscard]] Clip* clipForId(uint32_t id);
	[[nodiscard]] Output* outputForId(uint32_t id);
	/// Resolve a drum serial id to its live Drum (scanning every Kit's drums), or nullptr if unassigned/absent.
	[[nodiscard]] Drum* drumForId(uint32_t id);

	void ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
	                                                         char const* errorMessageHibernating);
	Error placeFirstInstancesOfActiveClips(int32_t pos);
	void endInstancesOfActiveClips(int32_t pos, bool detachClipsToo = false);
	void clearArrangementBeyondPos(int32_t pos, Action* action);
	void deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip);
	bool arrangementHasAnyClipInstances();
	void resumeClipsClonedForArrangementRecording();
	void setParamsInAutomationMode(bool newState);
	bool shouldOldOutputBeReplaced(Clip* clip, Availability* availabilityRequirement = nullptr);
	/// @brief Navigate a MIDI or CV output through its channels. A Synth or Kit is acquired by InstrumentLoad and
	///        applied by applyNavigateThroughPresets(); this returns @p output unchanged for one.
	Output* navigateThroughPresetsForInstrument(Output* output, int32_t offset);
	/// @brief Swap in the Synth or Kit a navigate acquired for @p oldInstrument.
	/// @return The new Instrument, or @p oldInstrument when the acquisition found nothing (its error shown).
	Output* applyNavigateThroughPresets(Instrument* oldInstrument, const deluge::storage::AcquireResult& acquired);
	void instrumentSwapped(Instrument* newInstrument);
	/// @brief Change @p oldInstrument to a MIDI or CV Instrument. A Synth or Kit is acquired by InstrumentLoad and
	///        applied by applyChangeOutputType(); this returns nullptr for one.
	Instrument* changeOutputType(Instrument* oldInstrument, OutputType newOutputType);
	/// @brief Swap in the Synth or Kit a change-type acquired for @p oldInstrument.
	/// @return The new Instrument, or nullptr when the acquisition failed (its error shown).
	Instrument* applyChangeOutputType(Instrument* oldInstrument, const deluge::storage::AcquireResult& acquired);
	AudioOutput* getFirstAudioOutput();
	AudioOutput* createNewAudioOutput(Output* replaceOutput = nullptr);
	/// buffer must have at least 5 characters on 7seg, or 30 for OLED
	void getNoteLengthName(etl::istring& buffer, uint32_t noteLength, char const* notesString = "-notes",
	                       bool clarifyPerColumn = false) const;
	void replaceOutputLowLevel(Output* newOutput, Output* oldOutput);
	void removeSessionClip(Clip* clip, int32_t clipIndex, bool forceClipsAboveToMoveVertically = false);
	bool deletePendingOverdubs(Output* onlyWithOutput = nullptr, int32_t* originalClipIndex = nullptr,
	                           bool createConsequencesForOtherLinearlyRecordingClips = false);
	Clip* getPendingOverdubWithOutput(Output* output);
	Clip* getClipWithOutputAboutToBeginLinearRecording(Output* output);
	Clip* createPendingNextOverdubBelowClip(Clip* clip, int32_t clipIndex, OverDubType newOverdubNature);
	bool hasAnyPendingNextOverdubs();
	Output* getNextAudioOutput(int32_t offset, Output* oldOutput, Availability availabilityRequirement);
	void deleteOutput(Output* output);
	void clearRecordingFromReferencesTo(Output* output);
	void cullAudioClipVoice();
	int32_t getYScrollSongViewWithoutPendingOverdubs();
	int32_t removeOutputFromMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void swapClips(Clip* newClip, Clip* oldClip, int32_t clipIndex);
	Clip* replaceInstrumentClipWithAudioClip(Clip* oldClip, int32_t clipIndex);
	void setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity);
	void midiCableBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone,
	                                         int32_t whichBendRange, int32_t bendSemitones);
	/// @brief Merge this song's own Instruments for @p outputType into @p into, for the folder that
	///        listing was read from.
	Error addInstrumentsToFileItems(deluge::gui::browser::FileListing& into, OutputType outputType);

	uint32_t getQuarterNoteLength();
	uint32_t getBarLength();
	uint32_t getSixteenthNoteLength();
	ModelStackWithThreeMainThings* setupModelStackWithSongAsTimelineCounter(void* memory);
	ModelStackWithTimelineCounter* setupModelStackWithCurrentClip(void* memory);
	ModelStackWithThreeMainThings* addToModelStack(ModelStack* modelStack);
	/// Gets a modelstack with the song-global unpatched param paramID.
	/// used in performance view and in automation arranger view
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithThreeMainThings* modelStack, int32_t paramID);

	// TimelineCounter implementation
	[[nodiscard]] int32_t getLastProcessedPos() const override;
	[[nodiscard]] uint32_t getLivePos() const override;
	[[nodiscard]] int32_t getLoopLength() const override;
	[[nodiscard]] bool isPlayingAutomationNow() const override;
	[[nodiscard]] bool backtrackingCouldLoopBackToEnd() const override;
	[[nodiscard]] int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) const override;
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack) override;
	void expectEvent() override;
	TimelineCounter* getTimelineCounterToRecordTo() override;

	// START ~ new Automation Arranger View Variables
	int32_t lastSelectedParamID; // last selected Parameter to be edited in Automation Arranger View
	deluge::modulation::params::Kind
	    lastSelectedParamKind; // 0 = patched, 1 = unpatched, 2 = global effectable, 3 = none
	int32_t lastSelectedParamShortcutX;
	int32_t lastSelectedParamShortcutY;
	int32_t lastSelectedParamArrayPosition;
	// END ~ new Automation Arranger View Variables

	// Song level transpose control (encoder actions)
	void commandTranspose(int32_t interval);
	int32_t masterTransposeInterval = 0;
	void transpose(int32_t interval);
	void adjustMasterTransposeInterval(int32_t interval);
	void displayMasterTransposeInterval();

	// MIDI controlled song transpose
	bool hasBeenTransposed = 0;
	int16_t transposeOffset = 0;

	int32_t countAudioVoices() const;

	// Chord memory
	uint8_t chordMemNoteCount[kDisplayHeight] = {0};
	uint8_t chordMem[kDisplayHeight][MAX_NOTES_CHORD_MEM] = {0};

	// Tempo automation
	void clearTempoAutomation();
	void updateBPMFromAutomation();

	float calculateBPM() {
		float timePerTimerTick = getTimePerTimerTickFloat();
		return calculateBPM(timePerTimerTick);
	}
	float calculateBPM(float timePerTimerTick) {

		if (project.transport.inside_world_tick_magnitude > 0) {
			timePerTimerTick *= ((uint32_t)1 << (project.transport.inside_world_tick_magnitude));
		}
		float tempoBPM = (float)110250 / timePerTimerTick;
		if (project.transport.inside_world_tick_magnitude < 0) {
			tempoBPM *= ((uint32_t)1 << (-project.transport.inside_world_tick_magnitude));
		}
		return tempoBPM;
	}

	int8_t defaultAudioClipOverdubOutputCloning = -1; // -1 means no default set

	// Threshold
	void changeThresholdRecordingMode(int8_t offset);
	void displayThresholdRecordingMode();
	ThresholdRecordingMode thresholdRecordingMode;

private:
	void inputTickScalePotentiallyJustChanged(uint32_t oldScale);
	Error readClipsFromFile(Deserializer& reader, ClipArray* clipArray);
	void addInstrumentToHibernationList(Instrument* instrument);
	void deleteAllBackedUpParamManagers();
	void deleteAllBackedUpParamManagersWithClips();
	void deleteAllOutputs(Output** prevPointer);
	void setupClipIndexesForSaving();
	void setBPMInner(float tempoBPM, bool shouldLogAction);
	void clearTempoAutomation(float tempoBPM);
	int32_t intBPM{0};

	// Per-song monotonic serial-id counters (see generateNewClipId). First assigned id is 1; the
	// unassigned state is std::nullopt (Clip/Output::id), not a reserved id value.
	uint32_t clipIdCounter_{1};
	uint32_t outputIdCounter_{1};
	/// While a file is read: outputs added then are adopted once ids are known, not as they are added.
	bool readingFromFile_{false};
	/// The drum ids this song's read has given out so far: a second drum the file names with one gets its own.
	std::vector<uint32_t> drumIdsRead_;
	uint32_t drumIdCounter_{1};
};

extern Song* currentSong;
extern Song* preLoadedSong;
