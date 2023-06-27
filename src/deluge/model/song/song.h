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

#ifndef SONG_H
#define SONG_H

#include <ClipArray.h>
#include <GlobalEffectableForSong.h>
#include <ParamManager.h>
#include <TimelineCounter.h>
#include "OrderedResizeableArrayWithMultiWordKey.h"
#include "DString.h"
#include "LearnedMIDI.h"

class MidiCommand;
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

class Song final : public TimelineCounter {
public:
	Song();
	~Song();
	bool mayDoubleTempo();
	bool ensureAtLeastOneSessionClip();
	void transposeAllScaleModeClips(int offset);
	bool anyScaleModeClips();
	void setRootNote(int newRootNote, InstrumentClip* clipToAvoidAdjustingScrollFor = NULL);
	void addModeNote(uint8_t modeNote);
	void addMajorDependentModeNotes(uint8_t i, bool preferHigher, bool notesWithinOctavePresent[]);
	bool yNoteIsYVisualWithinOctave(int yNote, int yVisualWithinOctave);
	uint8_t getYNoteWithinOctaveFromYNote(int yNote);
	void changeMusicalMode(uint8_t yVisualWithinOctave, int8_t change);
	int getYVisualFromYNote(int yNote, bool inKeyMode);
	int getYNoteFromYVisual(int yVisual, bool inKeyMode);
	bool mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset);
	bool modeContainsYNote(int yNote);
	ParamManagerForTimeline* findParamManagerForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = NULL);
	void setupPatchingForAllParamManagersForDrum(SoundDrum* drum);
	void setupPatchingForAllParamManagersForInstrument(SoundInstrument* sound);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForInstrument(MIDIDevice* device,
	                                                                                       SoundInstrument* instrument);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForAllParamManagersForDrum(MIDIDevice* device,
	                                                                                 SoundDrum* drum, Kit* kit);
	void grabVelocityToLevelFromMIDIDeviceAndSetupPatchingForEverything(MIDIDevice* device);
	int cycleThroughScales();
	int getCurrentPresetScale();
	void setTempoFromNumSamples(double newTempoSamples, bool shouldLogAction);
	void setupDefault();
	void setBPM(float tempoBPM, bool shouldLogAction);
	void setTempoFromParams(int32_t magnitude, int8_t whichValue, bool shouldLogAction);
	void deleteSoundsWhichWontSound();
	void deleteClipObject(Clip* clip, bool songBeingDestroyedToo = false,
	                      int instrumentRemovalInstruction = INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED);
	int getMaxMIDIChannelSuffix(int channel);
	void addOutput(Output* output, bool atStart = true);
	void deleteOutputThatIsInMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void markAllInstrumentsAsEdited();
	Instrument* getInstrumentFromPresetSlot(int instrumentType, int presetNumber, int presetSubSlotNumber,
	                                        char const* name, char const* dirPath, bool searchHibernatingToo = true,
	                                        bool searchNonHibernating = true);
	AudioOutput* getAudioOutputFromName(String* name);
	void setupPatchingForAllParamManagers();
	void replaceInstrument(Instrument* oldInstrument, Instrument* newInstrument, bool keepNoteRowsWithMIDIInput = true);
	void stopAllMIDIAndGateNotesPlaying();
	void stopAllAuditioning();
	void deleteOrHibernateOutput(Output* output);
	uint32_t getLivePos();
	int32_t getLoopLength();
	Instrument* getNonAudioInstrumentToSwitchTo(int newInstrumentType, int availabilityRequirement, int16_t newSlot,
	                                            int8_t newSubSlot, bool* instrumentWasAlreadyInSong);
	void removeSessionClipLowLevel(Clip* clip, int clipIndex);
	void changeSwingInterval(int newValue);
	int convertSyncLevelFromFileValueToInternalValue(int fileValue);
	int convertSyncLevelFromInternalValueToFileValue(int internalValue);

	GlobalEffectableForSong globalEffectable;

	ClipArray sessionClips;
	ClipArray arrangementOnlyClips;

	Output* firstOutput;
	Instrument*
	    firstHibernatingInstrument; // All Instruments have inValidState set to false when they're added to this list

	Clip* currentClip;

	OrderedResizeableArrayWithMultiWordKey backedUpParamManagers;

	uint32_t xZoom[2];  // Set default zoom at max zoom-out;
	int32_t xScroll[2]; // Leave this as signed
	int32_t xScrollForReturnToSongView;
	int32_t xZoomForReturnToSongView;
	bool tripletsOn;
	uint32_t tripletsLevel; // The number of ticks in one of the three triplets

	uint64_t timePerTimerTickBig;
	int32_t divideByTimePerTimerTick;

	// How many orders of magnitude faster internal ticks are going than input ticks. Used in combination with inputTickScale, which is usually 1,
	// but is different if there's an inputTickScaleClip.
	// So, e.g. if insideWorldTickMagnitude is 1, this means the inside world is spinning twice as fast as the external world, so MIDI sync coming in representing
	// an 8th-note would be interpreted internally as a quarter-note (because two internal 8th-notes would have happened, twice as fast, making a quarter-note)
	int insideWorldTickMagnitude;

	// Sometimes, we'll do weird stuff to insideWorldTickMagnitude for sync-scaling, which would make BPM values look weird. So, we keep insideWorldTickMagnitudeOffsetFromBPM
	int insideWorldTickMagnitudeOffsetFromBPM;

	int8_t swingAmount;
	uint8_t swingInterval;

	Section sections[MAX_NUM_SECTIONS];

	// Scales
	uint8_t modeNotes[12];
	uint8_t numModeNotes;
	int16_t rootNote;

	uint16_t slot;
	int8_t subSlot;
	String name;

	bool affectEntire;

	int songViewYScroll;
	int arrangementYScroll;

	uint8_t sectionToReturnToAfterSongEnd;

	bool wasLastInArrangementEditor;
	int32_t
	    lastClipInstanceEnteredStartPos; // -1 means we are not "inside" an arrangement. While we're in the ArrangementEditor, it's 0

	bool arrangerAutoScrollModeActive;

	MIDIInstrument* hibernatingMIDIInstrument;

	bool
	    outputClipInstanceListIsCurrentlyInvalid; // Set to true during scenarios like replaceInstrument(), to warn other functions not to look at Output::clipInstances

	bool paramsInAutomationMode;

	bool inClipMinderViewOnLoad; // Temp variable only valid while loading Song

	int32_t unautomatedParamValues[MAX_NUM_UNPATCHED_PARAMS];

	String dirPath;

	bool getAnyClipsSoloing();
	uint32_t getInputTickScale();
	Clip* getSyncScalingClip();
	void setInputTickScaleClip(Clip* clip);

	void setClipLength(Clip* clip, uint32_t newLength, Action* action, bool mayReSyncClip = true);
	void doubleClipLength(InstrumentClip* clip, Action* action = NULL);
	Clip* getClipWithOutput(Output* output, bool mustBeActive = false, Clip* excludeClip = NULL);
	int readFromFile();
	void writeToFile();
	void loadAllSamples(bool mayActuallyReadFiles = true);
	bool modeContainsYNoteWithinOctave(uint8_t yNoteWithinOctave);
	void renderAudio(StereoSample* outputBuffer, int numSamples, int32_t* reverbBuffer, int32_t sideChainHitPending);
	bool isYNoteAllowed(int yNote, bool inKeyMode);
	Clip* syncScalingClip;
	void setTimePerTimerTick(uint64_t newTimeBig, bool shouldLogAction = false);
	bool hasAnySwing();
	void resyncLFOsAndArpeggiators();
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Sound* sound);
	bool areAllClipsInSectionPlaying(int section);
	void removeYNoteFromMode(int yNoteWithinOctave);
	void turnSoloingIntoJustPlaying(bool getRidOfArmingToo = true);
	void reassessWhetherAnyClipsSoloing();
	float getTimePerTimerTickFloat();
	uint32_t getTimePerTimerTickRounded();
	int getNumOutputs();
	Clip* getNextSessionClipWithOutput(int offset, Output* output, Clip* prevClip);
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
	bool doesNonAudioSlotHaveActiveClipInSession(int instrumentType, int slot, int subSlot = -1);
	bool doesOutputHaveAnyClips(Output* output);
	void deleteBackedUpParamManagersForClip(Clip* clip);
	void deleteBackedUpParamManagersForModControllable(ModControllableAudio* modControllable);
	void deleteHibernatingInstrumentWithSlot(int instrumentType, char const* name);
	void loadCrucialSamplesOnly();
	Clip* getSessionClipWithOutput(Output* output, int requireSection = -1, Clip* excludeClip = NULL,
	                               int* clipIndex = NULL, bool excludePendingOverdubs = false);
	void restoreClipStatesBeforeArrangementPlay();
	void deleteOrAddToHibernationListOutput(Output* output);
	int getLowestSectionWithNoSessionClipForOutput(Output* output);
	void assertActiveness(ModelStackWithTimelineCounter* modelStack, int32_t endInstanceAtTime = -1);
	bool isClipActive(Clip* clip);
	void sendAllMIDIPGMs();
	void sortOutWhichClipsAreActiveWithoutSendingPGMs(ModelStack* modelStack, int playbackWillStartInArrangerAtPos);
	void deactivateAnyArrangementOnlyClips();
	Clip* getLongestClip(bool includePlayDisabled, bool includeArrangementOnly);
	Clip* getLongestActiveClipWithMultipleOrFactorLength(int32_t targetLength, bool revertToAnyActiveClipIfNone = true,
	                                                     Clip* excludeClip = NULL);
	int getOutputIndex(Output* output);
	void setHibernatingMIDIInstrument(MIDIInstrument* newInstrument);
	void deleteHibernatingMIDIInstrument();
	MIDIInstrument* grabHibernatingMIDIInstrument(int newSlot, int newSubSlot);
	NoteRow* findNoteRowForDrum(Kit* kit, Drum* drum, Clip* stopTraversalAtClip = NULL);

	bool anyOutputsSoloingInArrangement;
	bool getAnyOutputsSoloingInArrangement();
	void reassessWhetherAnyOutputsSoloingInArrangement();
	bool isOutputActiveInArrangement(Output* output);
	Output* getOutputFromIndex(int index);
	void ensureAllInstrumentsHaveAClipOrBackedUpParamManager(char const* errorMessageNormal,
	                                                         char const* errorMessageHibernating);
	int placeFirstInstancesOfActiveClips(int32_t pos);
	void endInstancesOfActiveClips(int32_t pos, bool detachClipsToo = false);
	void clearArrangementBeyondPos(int32_t pos, Action* action);
	void deletingClipInstanceForClip(Output* output, Clip* clip, Action* action, bool shouldPickNewActiveClip);
	bool arrangementHasAnyClipInstances();
	void resumeClipsClonedForArrangementRecording();
	bool isPlayingAutomationNow();
	bool backtrackingCouldLoopBackToEnd();
	int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack);
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack);
	void expectEvent();
	TimelineCounter* getTimelineCounterToRecordTo();
	int32_t getLastProcessedPos();
	void setParamsInAutomationMode(bool newState);
	bool canOldOutputBeReplaced(Clip* clip, int* availabilityRequirement = NULL);
	void instrumentSwapped(Instrument* newInstrument);
	Instrument* changeInstrumentType(Instrument* oldInstrument, int newInstrumentType);
	AudioOutput* getFirstAudioOutput();
	AudioOutput* createNewAudioOutput(Output* replaceOutput = NULL);
	void getNoteLengthName(char* text, uint32_t noteLength, bool clarifyPerColumn = false);
	void replaceOutputLowLevel(Output* newOutput, Output* oldOutput);
	void removeSessionClip(Clip* clip, int clipIndex, bool forceClipsAboveToMoveVertically = false);
	bool deletePendingOverdubs(Output* onlyWithOutput = NULL, int* originalClipIndex = NULL,
	                           bool createConsequencesForOtherLinearlyRecordingClips = false);
	Clip* getPendingOverdubWithOutput(Output* output);
	Clip* getClipWithOutputAboutToBeginLinearRecording(Output* output);
	Clip* createPendingNextOverdubBelowClip(Clip* clip, int clipIndex, int newOverdubNature);
	bool hasAnyPendingNextOverdubs();
	Output* getNextAudioOutput(int offset, Output* oldOutput, int availabilityRequirement);
	void deleteOutput(Output* output);
	void cullAudioClipVoice();
	int getYScrollSongViewWithoutPendingOverdubs();
	int removeOutputFromMainList(Output* output, bool stopAnyAuditioningFirst = true);
	void swapClips(Clip* newClip, Clip* oldClip, int clipIndex);
	Clip* replaceInstrumentClipWithAudioClip(Clip* oldClip, int clipIndex);
	void setDefaultVelocityForAllInstruments(uint8_t newDefaultVelocity);
	void midiDeviceBendRangeUpdatedViaMessage(ModelStack* modelStack, MIDIDevice* device, int channelOrZone,
	                                          int whichBendRange, int bendSemitones);
	int addInstrumentsToFileItems(int instrumentType);

	uint32_t getQuarterNoteLength();
	uint32_t getBarLength();
	ModelStackWithThreeMainThings* setupModelStackWithSongAsTimelineCounter(void* memory);
	ModelStackWithTimelineCounter* setupModelStackWithCurrentClip(void* memory);
	ModelStackWithThreeMainThings* addToModelStack(ModelStack* modelStack);

	// Reverb params to be stored here between loading and song being made the active one
	float reverbRoomSize;
	float reverbDamp;
	float reverbWidth;
	int32_t reverbPan;
	int32_t reverbCompressorVolume;
	int32_t reverbCompressorShape;
	int32_t reverbCompressorAttack;
	int32_t reverbCompressorRelease;
	SyncLevel reverbCompressorSync;

private:
	void inputTickScalePotentiallyJustChanged(uint32_t oldScale);
	int readClipsFromFile(ClipArray* clipArray);
	void addInstrumentToHibernationList(Instrument* instrument);
	void deleteAllBackedUpParamManagers(bool shouldAlsoEmptyVector = true);
	void deleteAllBackedUpParamManagersWithClips();
	void deleteAllOutputs(Output** prevPointer);
	void setupClipIndexesForSaving();
};

extern Song* currentSong;
extern Song* preLoadedSong;

#endif // SONG_H
