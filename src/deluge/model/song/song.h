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
#include "io/midi/learned_midi.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/scale/note_set.h"
#include "model/timeline_counter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "storage/flash_storage.h"
#include "util/container/array/ordered_resizeable_array_with_multi_word_key.h"
#include "util/d_string.h"

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
class BackedUpParamManager;
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

class Section {
public:
	LearnedMIDI launchMIDICommand;
	int16_t numRepetitions;

	Section() { numRepetitions = 0; }
};

struct BackedUpParamManager {
	ModControllableAudio* modControllable;
	Clip* clip;
	ParamManager paramManager;
};

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
	void setRootNote(int32_t newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor = NULL);
	void addModeNote(uint8_t modeNote);
	void addMajorDependentModeNotes(uint8_t i, bool preferHigher, NoteSet& notesWithinOctavePresent);
	bool yNoteIsYVisualWithinOctave(int32_t yNote, int32_t yVisualWithinOctave);
	uint8_t getYNoteWithinOctaveFromYNote(int32_t yNote);
	void changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change);
	void rotateMusicalMode(int8_t change);
	void replaceMusicalMode(int8_t changes[], bool affectMIDITranspose);
	int32_t getYVisualFromYNote(int32_t yNote, bool inKeyMode);
	int32_t getYVisualFromYNote(int32_t yNote, bool inKeyMode, int16_t root, uint8_t nModeNotes, uint8_t mNotes[]);
	int32_t getYNoteFromYVisual(int32_t yVisual, bool inKeyMode);
	int32_t getYNoteFromYVisual(int32_t yVisual, bool inKeyMode, int16_t root, uint8_t nModeNotes, uint8_t mNotes[]);
	bool mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset);
	bool modeContainsYNote(int32_t yNote);
	ParamManagerForTimeline* findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = NULL);
	void setupPatchingForAllParamManagersForDrum(SoundDrum* drum);
	void setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForInstrument(MIDIDevice* device,
	                                                                                       SoundInstrument* instrument);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum(MIDIDevice* device,
	                                                                                 SoundDrum* drum, Kit* kit);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForEverything(MIDIDevice* device);
	void displayCurrentRootNoteAndScaleName();
	const char* getScaleName(int32_t scale);
	int32_t cycleThroughScales();
	int32_t getCurrentPresetScale();
	int32_t setPresetScale(int32_t newScale);
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
	AudioOutput* getAudioOutputFromName(String* name);
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
	String getSongFullPath();
	void setSongFullPath(const char* fullPath);

	GlobalEffectableForSong globalEffectable;

	ClipArray sessionClips;
	ClipArray arrangementOnlyClips;

	Output* firstOutput;
	Instrument*
	    firstHibernatingInstrument; // All Instruments have inValidState set to false when they're added to this list

	OrderedResizeableArrayWithMultiWordKey backedUpParamManagers;

	uint32_t xZoom[2];  // Set default zoom at max zoom-out;
	int32_t xScroll[2]; // Leave this as signed
	int32_t xScrollForReturnToSongView;
	int32_t xZoomForReturnToSongView;
	bool tripletsOn;
	uint32_t tripletsLevel; // The number of ticks in one of the three triplets

	uint64_t timePerTimerTickBig;
	int32_t divideByTimePerTimerTick;

	// How many orders of magnitude faster internal ticks are going than input ticks. Used in combination with
	// inputTickScale, which is usually 1, but is different if there's an inputTickScaleClip. So, e.g. if
	// insideWorldTickMagnitude is 1, this means the inside world is spinning twice as fast as the external world, so
	// MIDI sync coming in representing an 8th-note would be interpreted internally as a quarter-note (because two
	// internal 8th-notes would have happened, twice as fast, making a quarter-note)
	int32_t insideWorldTickMagnitude;

	// Sometimes, we'll do weird stuff to insideWorldTickMagnitude for sync-scaling, which would make BPM values look
	// weird. So, we keep insideWorldTickMagnitudeOffsetFromBPM
	int32_t insideWorldTickMagnitudeOffsetFromBPM;

	int8_t swingAmount;
	uint8_t swingInterval;

	Section sections[kMaxNumSections];

	// Scales
	uint8_t modeNotes[12];
	uint8_t numModeNotes;
	int16_t rootNote;

	uint16_t slot;
	int8_t subSlot;
	String name;

	bool affectEntire;

	SessionLayoutType sessionLayout = FlashStorage::defaultSessionLayout;
	int32_t songGridScrollX = 0;
	int32_t songGridScrollY = 0;
	int32_t songViewYScroll;
	int32_t arrangementYScroll;

	uint8_t sectionToReturnToAfterSongEnd;

	bool wasLastInArrangementEditor;
	int32_t lastClipInstanceEnteredStartPos; // -1 means we are not "inside" an arrangement. While we're in the
	                                         // ArrangementEditor, it's 0

	bool arrangerAutoScrollModeActive;

	MIDIInstrument* hibernatingMIDIInstrument;

	bool outputClipInstanceListIsCurrentlyInvalid; // Set to true during scenarios like replaceInstrument(), to warn
	                                               // other functions not to look at Output::clipInstances

	bool paramsInAutomationMode;

	bool inClipMinderViewOnLoad; // Temp variable only valid while loading Song

	int32_t unautomatedParamValues[deluge::modulation::params::kMaxNumUnpatchedParams];

	String dirPath;

	bool getAnyClipsSoloing() const;
	Clip* getCurrentClip();
	void setCurrentClip(Clip* clip) {
		if (currentClip != nullptr) {
			previousClip = currentClip;
		}
		currentClip = clip;
	}
	uint32_t getInputTickScale();
	Clip* getSyncScalingClip();
	void setInputTickScaleClip(Clip* clip);
	inline bool isFillModeActive() { return fillModeActive; }
	void changeFillMode(bool on);
	void setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip = true);
	void doubleClipLength(InstrumentClip* clip, Action* action = NULL);
	Clip* getClipWithOutput(Output* output, bool mustBeActive = false, Clip* excludeClip = NULL);
	Error readFromFile(Deserializer& reader);
	void writeToFile(StorageManager& bdsm);
	void loadAllSamples(bool mayActuallyReadFiles = true);
	bool modeContainsYNoteWithinOctave(uint8_t yNoteWithinOctave);
	uint8_t getYNoteIndexInMode(int32_t yNote);
	void renderAudio(StereoSample* outputBuffer, int32_t numSamples, int32_t* reverbBuffer,
	                 int32_t sideChainHitPending);
	bool isYNoteAllowed(int32_t yNote, bool inKeyMode);
	Clip* syncScalingClip;
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

	ParamManager* getBackedUpParamManagerForExactClip(ModControllableAudio* modControllable, Clip* clip,
	                                                  ParamManager* stealInto = NULL);
	ParamManager* getBackedUpParamManagerPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
	                                                        ParamManager* stealInto = NULL);
	void backUpParamManager(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
	                        bool shouldStealExpressionParamsToo = false);
	void moveInstrumentToHibernationList(Instrument* instrument);
	void deleteOrHibernateOutputIfNoClips(Output* output);
	void removeInstrumentFromHibernationList(Instrument* instrument);
	bool doesOutputHaveActiveClipInSession(Output* output);
	bool doesNonAudioSlotHaveActiveClipInSession(OutputType outputType, int32_t slot, int32_t subSlot = -1);
	bool doesOutputHaveAnyClips(Output* output);
	void deleteBackedUpParamManagersForClip(Clip* clip);
	void deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable);
	void deleteHibernatingInstrumentWithSlot(OutputType outputType, char const* name);
	void loadCrucialSamplesOnly();
	Clip* getSessionClipWithOutput(Output* output, int32_t requireSection = -1, Clip* excludeClip = NULL,
	                               int32_t* clipIndex = NULL, bool excludePendingOverdubs = false);
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
	                                                     Clip* excludeClip = NULL);
	int32_t getOutputIndex(Output* output);
	void setHibernatingMIDIInstrument(MIDIInstrument* newInstrument);
	void deleteHibernatingMIDIInstrument();
	MIDIInstrument* grabHibernatingMIDIInstrument(int32_t newSlot, int32_t newSubSlot);
	NoteRow* findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = NULL);

	bool anyOutputsSoloingInArrangement;
	bool getAnyOutputsSoloingInArrangement();
	void reassessWhetherAnyOutputsSoloingInArrangement();
	bool isOutputActiveInArrangement(Output* output);
	Output* getOutputFromIndex(int32_t index);
	void ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
	                                                         char const* errorMessageHibernating);
	Error placeFirstInstancesOfActiveClips(int32_t pos);
	void endInstancesOfActiveClips(int32_t pos, bool detachClipsToo = false);
	void clearArrangementBeyondPos(int32_t pos, Action* action);
	void deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip);
	bool arrangementHasAnyClipInstances();
	void resumeClipsClonedForArrangementRecording();
	void setParamsInAutomationMode(bool newState);
	bool shouldOldOutputBeReplaced(Clip* clip, Availability* availabilityRequirement = NULL);
	Output* navigateThroughPresetsForInstrument(Output* output, int32_t offset);
	void instrumentSwapped(Instrument* newInstrument);
	Instrument* changeOutputType(Instrument* oldInstrument, OutputType newOutputType);
	AudioOutput* getFirstAudioOutput();
	AudioOutput* createNewAudioOutput(Output* replaceOutput = NULL);
	/// buffer must have at least 5 characters on 7seg, or 30 for OLED
	void getNoteLengthName(StringBuf& buffer, uint32_t noteLength, char const* notesString = "-notes",
	                       bool clarifyPerColumn = false) const;
	void replaceOutputLowLevel(Output* newOutput, Output* oldOutput);
	void removeSessionClip(Clip* clip, int32_t clipIndex, bool forceClipsAboveToMoveVertically = false);
	bool deletePendingOverdubs(Output* onlyWithOutput = NULL, int32_t* originalClipIndex = NULL,
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
	void midiDeviceBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
	                                          int32_t whichBendRange, int32_t bendSemitones);
	Error addInstrumentsToFileItems(OutputType outputType);

	uint32_t getQuarterNoteLength();
	uint32_t getBarLength();
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

	// Whether this song wants notes/cc/etc from delly midi clips looped back
	bool midiLoopback = false;

	// Reverb params to be stored here between loading and song being made the active one
	float reverbRoomSize;
	float reverbDamp;
	float reverbWidth;
	int32_t reverbPan;
	int32_t reverbSidechainVolume;
	int32_t reverbSidechainShape;
	int32_t reverbSidechainAttack;
	int32_t reverbSidechainRelease;
	SyncLevel reverbSidechainSync;

	// START ~ new Automation Arranger View Variables
	int32_t lastSelectedParamID; // last selected Parameter to be edited in Automation Arranger View
	deluge::modulation::params::Kind
	    lastSelectedParamKind; // 0 = patched, 1 = unpatched, 2 = global effectable, 3 = none
	int32_t lastSelectedParamShortcutX;
	int32_t lastSelectedParamShortcutY;
	int32_t lastSelectedParamArrayPosition;
	// END ~ new Automation Arranger View Variables

	// Song level transpose control (encoder actions)
	int32_t masterTransposeInterval;
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

private:
	uint8_t indexLastUnusedScaleDegreeFrom7To6 = 0;
	uint8_t indexLastUnusedScaleDegreeFrom6To5 = 0;
	bool fillModeActive;
	Clip* currentClip = nullptr;
	Clip* previousClip = nullptr; // for future use, maybe finding an instrument clip or something
	void inputTickScalePotentiallyJustChanged(uint32_t oldScale);
	Error readClipsFromFile(Deserializer& reader, ClipArray* clipArray);
	void addInstrumentToHibernationList(Instrument* instrument);
	void deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector = true);
	void deleteAllBackedUpParamManagersWithClips();
	void deleteAllOutputs(Output** prevPointer);
	void setupClipIndexesForSaving();
};

extern Song* currentSong;
extern Song* preLoadedSong;
extern int8_t defaultAudioClipOverdubOutputCloning;
