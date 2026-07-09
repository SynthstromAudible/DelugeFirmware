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
#include "gui/menu_item/reverb/model.h"
#include "io/midi/learned_midi.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_mapper.h"
#include "model/song/song_view_state.h"
#include "model/sync.h"
#include "model/timeline_counter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "storage/flash_storage.h"
#include "util/c_string.h"
#include "util/containers.h"
#include "util/etl_string.h"
#include <utility>

class MidiCommand;
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
	bool mayDoubleTempo();
	bool ensureAtLeastOneSessionClip();
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
	void writeTemplateSong(const char* templateSong);
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

	// The project model (deluge::project::Song). Project data migrates into this group by group
	// (docs/dev/project_model_extraction.md); ::Song is the controller that owns it. Empty for now.
	// (Named `project` rather than `model` — `model` is already the reverb-model enum member.)
	deluge::project::Song project;

	// Backed-up ParamManagers, keyed by (ModControllable, Clip). For a given ModControllable, the entry with a
	// null Clip (if any) sorts first. Node-based so ParamManagers never move in memory.

	// View/UI state that IS serialized to the song file (scroll/zoom/layout/triplets/etc.).
	PersistentViewState persistentView;

	// How many orders of magnitude faster internal ticks are going than input ticks. Used in combination with
	// inputTickScale, which is usually 1, but is different if there's an inputTickScaleClip. So, e.g. if
	// insideWorldTickMagnitude is 1, this means the inside world is spinning twice as fast as the external world, so
	// MIDI sync coming in representing an 8th-note would be interpreted internally as a quarter-note (because two
	// internal 8th-notes would have happened, twice as fast, making a quarter-note)

	// Sometimes, we'll do weird stuff to insideWorldTickMagnitude for sync-scaling, which would make BPM values look
	// weird. So, we keep insideWorldTickMagnitudeOffsetFromBPM

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
	inline bool isFillModeActive() { return runtimeView.fillModeActive; }
	void changeFillMode(bool on);
	void loadNextSong();
	void setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip = true);
	void doubleClipLength(InstrumentClip* clip, Action* action = nullptr);
	Clip* getClipWithOutput(Output* output, bool mustBeActive = false, Clip* excludeClip = nullptr);
	Error readFromFile(Deserializer& reader);

	/// M3: mint a transient session id for every clip / output / drum that lacks a durable serialId, so every entity
	/// is id-addressable. Called at load before the param relocation; idempotent (skips entities that already have an
	/// id). Session ids are never serialized → golden-neutral.
	void assignSessionIds();
	/// Bind a song-resident clip to the persistent DOM structural store (mint a session id if it has none, seed its
	/// DocClip from the current values, repoint reads/writes at the tree). Idempotent — a no-op on an already-bound
	/// clip. Called at load (for every clip) and at every runtime clip insertion, so no live clip stays on its
	/// transient ownDoc_.
	void adoptClip(Clip* clip);
	void writeToFile(Serializer& writer);
	void loadAllSamples(bool mayActuallyReadFiles = true);
	void renderAudio(std::span<StereoSample> outputBuffer, int32_t* reverbBuffer, int32_t sideChainHitPending);
	bool isYNoteAllowed(int32_t yNote, bool inKeyMode);
	void setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction = false);
	bool hasAnySwing();
	void resyncLFOs();
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
	void loadCrucialSamplesOnly();
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

	// --- Stable per-song serial identity for clips/outputs (the DOM reference foundation) ---
	// Each clip/output carries a persisted serial id (Clip::serialId / Output::serialId), assigned
	// lazily on first save from these per-song monotonic counters and restored on load. Unlike the
	// transient positional index, the id survives reorders and reloads, so DOM references (e.g.
	// session macros) can resolve by id instead of position.
	uint32_t generateNewClipId() { return clipIdCounter_++; }
	uint32_t generateNewOutputId() { return outputIdCounter_++; }
	uint32_t generateNewDrumId() { return drumIdCounter_++; }
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
	Output* navigateThroughPresetsForInstrument(Output* output, int32_t offset);
	void instrumentSwapped(Instrument* newInstrument);
	Instrument* changeOutputType(Instrument* oldInstrument, OutputType newOutputType);
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
	void cullAudioClipVoice();
	int32_t getYScrollSongViewWithoutPendingOverdubs();
	int32_t removeOutputFromMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void swapClips(Clip* newClip, Clip* oldClip, int32_t clipIndex);
	Clip* replaceInstrumentClipWithAudioClip(Clip* oldClip, int32_t clipIndex);
	void setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity);
	void midiCableBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDICable& cable, int32_t channelOrZone,
	                                         int32_t whichBendRange, int32_t bendSemitones);
	Error addInstrumentsToFileItems(OutputType outputType);

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

	// Reverb params to be stored here between loading and song being made the active one
	dsp::Reverb::Model model;

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

	int32_t countAudioClips() const;

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
	// unassigned state is std::nullopt (Clip/Output::serialId), not a reserved id value.
	uint32_t clipIdCounter_{1};
	uint32_t outputIdCounter_{1};
	uint32_t drumIdCounter_{1};
};

extern Song* currentSong;
extern Song* preLoadedSong;
