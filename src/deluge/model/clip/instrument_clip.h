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
#include "gui/ui/keyboard/state_data.h"
#include "gui/views/instrument_clip_view.h"
#include "io/midi/harmonizer_settings.h"
#include "model/note/note_row_vector.h"
#include "modulation/arpeggiator.h"

class Song;

class NoteRow;
class InstrumentClip;
class Instrument;
class ModControllable;
class Drum;
class ParamManagerForTimeline;
class Sound;
class Note;
class Action;
class ParamManagerForTimeline;
class TimelineView;
class ModelStackWithTimelineCounter;
class ModelStackWithModControllable;
class ModelStackWithNoteRow;

struct PendingNoteOn;

enum class VerticalNudgeType { ROW, OCTAVE };

class InstrumentClip final : public Clip {
public:
	explicit InstrumentClip(Song* song = nullptr);
	~InstrumentClip() override;
	void increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
	                               IndependentNoteRowLengthIncrease independentNoteRowInstruction,
	                               bool completelyRenderOutIterationDependence = false,
	                               Action* action = nullptr) override;
	void halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter* modelStack);
	void repeatOrChopToExactLength(ModelStackWithTimelineCounter* modelStack, int32_t newLength);
	void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t posIncrement) override;
	bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
	                       uint32_t xZoom, RGB* image, uint8_t occupancyMask[], bool addUndefinedArea,
	                       int32_t noteRowIndexStart = 0, int32_t noteRowIndexEnd = 2147483647, int32_t xStart = 0,
	                       int32_t xEnd = kDisplayWidth, bool allowBlur = true, bool drawRepeats = false) override;
	void toggleNoteRowMute(ModelStackWithNoteRow* modelStack);
	RGB getMainColourFromY(int32_t yNote, int8_t);
	void stopAllNotesPlaying(ModelStackWithTimelineCounter* modelStack, bool actuallySoundChange = true);
	void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true) override;
	void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos,
	            bool useActualPosForParamManagers = true) override;
	void replaceMusicalMode(const ScaleChange& changes, ModelStackWithTimelineCounter* modelStack);
	void seeWhatNotesWithinOctaveArePresent(NoteSet&, MusicalKey);
	void transpose(int32_t, ModelStackWithTimelineCounter* modelStack);
	void nudgeNotesVertically(int32_t direction, VerticalNudgeType, ModelStackWithTimelineCounter* modelStack);
	void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true) override;
	Error clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false) const override;
	NoteRow* createNewNoteRowForYVisual(int32_t, Song* song);
	int32_t getYVisualFromYNote(int32_t, Song* song);
	int32_t getYNoteFromYVisual(int32_t, Song* song);
	int32_t getYNoteFromYDisplay(int32_t yDisplay, Song* song);
	int32_t guessRootNote(Song* song, int32_t previousRoot);
	int32_t getNumNoteRows();
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithTimelineCounter* modelStack,
	                                                            Sound* sound);
	bool deleteSoundsWhichWontSound(Song* song) override;
	void setBackedUpParamManagerMIDI(ParamManagerForTimeline* newOne);
	void restoreBackedUpParamManagerMIDI(ModelStackWithModControllable* modelStack);
	int32_t getNoteRowId(NoteRow* noteRow, int32_t noteRowIndex);
	NoteRow* getNoteRowFromId(int32_t id);
	/// Return true if successfully shifted. Instrument clips always succeed
	bool shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount, bool shiftAutomation,
	                       bool shiftSequenceAndMPE) override;
	bool isEmpty(bool displayPopup = true) override;
	bool containsAnyNotes();
	ModelStackWithNoteRow* getNoteRowOnScreen(int32_t yDisplay, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowOnScreen(int32_t yDisplay, Song* song, int32_t* getIndex = nullptr);
	bool currentlyScrollableAndZoomable() override;
	void recordNoteOn(ModelStackWithNoteRow* modelStack, int32_t velocity, bool forcePos0 = false,
	                  int16_t const* mpeValuesOrNull = nullptr, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE);
	void recordNoteOff(ModelStackWithNoteRow* modelStack, int32_t velocity = kDefaultLiftValue);

	void copyBasicsFrom(Clip const* otherClip) override;

	ArpeggiatorSettings arpSettings;
	HarmonizerSettings harmonizerSettings;

	ParamManagerForTimeline backedUpParamManagerMIDI;

	bool inScaleMode; // Probably don't quiz this directly - call isScaleModeClip() instead

	int32_t yScroll;

	// TODO: Unscope this once namespacing is done
	deluge::gui::ui::keyboard::KeyboardState keyboardState;

	int32_t ticksTilNextNoteRowEvent{};
	int32_t noteRowsNumTicksBehindClip{};

	LearnedMIDI soundMidiCommand; // This is now handled by the Instrument, but for loading old songs, we need to
	                              // capture and store this

	NoteRowVector noteRows;

	bool wrapEditing;
	uint32_t wrapEditLevel{};

	// These *only* store a valid preset number for the instrument-types that the Clip is not currently on
	int8_t backedUpInstrumentSlot[4]{};
	int8_t backedUpInstrumentSubSlot[4]{};
	String backedUpInstrumentName[2];
	String backedUpInstrumentDirPath[2];

	bool affectEntire;

	bool onKeyboardScreen;

	uint8_t midiBank; // 128 means none
	uint8_t midiSub;  // 128 means none
	uint8_t midiPGM;  // 128 means none

	OutputType outputTypeWhileLoading; // For use only while loading song

	void lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action = nullptr) override;
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, bool atStart,
	                                int32_t* getIndex = nullptr);
	Error changeInstrument(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument,
	                       ParamManagerForTimeline* paramManager, InstrumentRemoval instrumentRemovalInstruction,
	                       InstrumentClip* favourClipForCloningParamManager = nullptr,
	                       bool keepNoteRowsWithMIDIInput = true, bool giveMidiAssignmentsToNewInstrument = false);
	void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                      bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                      bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                      bool shouldBackUpExpressionParamsToo = true) override;
	void assignDrumsToNoteRows(ModelStackWithTimelineCounter* modelStack, bool shouldGiveMIDICommandsToDrums = false,
	                           int32_t numNoteRowsPreviouslyDeletedFromBottom = 0);
	void unassignAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumNames,
	                                  bool shouldRetainLinksToSounds, bool shouldGrabMidiCommands,
	                                  bool shouldBackUpExpressionParamsToo);
	Error readFromFile(Deserializer& reader, Song* song) override;
	void writeDataToFile(Serializer& writer, Song* song) override;
	void prepNoteRowsForExitingKitMode(Song* song);
	void deleteNoteRow(ModelStackWithTimelineCounter* modelStack, int32_t i);
	int16_t getTopYNote();
	int16_t getBottomYNote();
	uint32_t getWrapEditLevel();
	ModelStackWithNoteRow* getNoteRowForYNote(int32_t yNote, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowForYNote(int32_t, int32_t* getIndex = nullptr);
	ModelStackWithNoteRow* getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* modelStack);
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter* modelStack, Drum* drum);
	NoteRow* getNoteRowForDrum(Drum* drum, int32_t* getIndex = nullptr);
	ModelStackWithNoteRow* getOrCreateNoteRowForYNote(int32_t yNote, ModelStackWithTimelineCounter* modelStack,
	                                                  Action* action = nullptr, bool* scaleAltered = nullptr);

	bool hasSameInstrument(InstrumentClip* otherClip);
	bool isScaleModeClip();
	bool allowNoteTails(ModelStackWithNoteRow* modelStack);
	Error setAudioInstrument(Instrument* newInstrument, Song* song, bool shouldNotifyInstrument,
	                         ParamManager* newParamManager, InstrumentClip* favourClipForCloningParamManager = nullptr);

	void expectEvent() override;
	int32_t getDistanceToNextNote(Note* givenNote, ModelStackWithNoteRow* modelStack);
	void reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack) override;
	void stopAllNotesForMIDIOrCV(ModelStackWithTimelineCounter* modelStack);
	void sendMIDIPGM();
	void noteRemovedFromMode(int32_t yNoteWithinOctave, Song* song);
	void clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
	           bool clearSequenceAndMPE) override;
	bool doesProbabilityExist(int32_t apartFromPos, int32_t probability, int32_t secondProbability = -1);
	void clearArea(ModelStackWithTimelineCounter* modelStack, int32_t startPos, int32_t endPos, Action* action);
	ScaleType getScaleType();
	void backupPresetSlot();
	void setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos = true) override;
	ModelStackWithNoteRow* getNoteRowForDrumName(ModelStackWithTimelineCounter* modelStack, char const* name);
	void compensateVolumeForResonance(ModelStackWithTimelineCounter* modelStack);
	Error undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack) override;
	Error setNonAudioInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager = nullptr);
	Error setInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager,
	                    InstrumentClip* favourClipForCloningParamManager = nullptr);
	void deleteOldDrumNames();
	void ensureScrollWithinKitBounds();
	bool isScrollWithinRange(int32_t scrollAmount, int32_t newYNote);
	Error appendClip(ModelStackWithTimelineCounter* thisModelStack,
	                 ModelStackWithTimelineCounter* otherModelStack) override;
	void instrumentBeenEdited() override;
	Instrument* changeOutputType(ModelStackWithTimelineCounter* modelStack, OutputType newOutputType);
	Error transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                                ModelStackWithTimelineCounter* modelStackClone) override;
	void getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager,
	                              Sound* sound) override;
	ParamManagerForTimeline* getCurrentParamManager() override;
	Error claimOutput(ModelStackWithTimelineCounter* modelStack) override;
	char const* getXMLTag() override { return "instrumentClip"; }
	void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
	                           int32_t buttonLatencyForTempolessRecord) override;
	Error beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) override;
	Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature) override;
	bool isAbandonedOverdub() override;
	void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                           uint32_t timeRemainder, int32_t suggestedLength,
	                                           int32_t alternativeLongerLength) override;
	void setupAsNewKitClipIfNecessary(ModelStackWithTimelineCounter* modelStack);
	bool getCurrentlyRecordingLinearly() override;
	void abortRecording() override;
	void yDisplayNoLongerAuditioning(int32_t yDisplay, Song* song);
	void shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int32_t shiftAmount, bool shiftAutomation,
	                                     bool shiftSequenceAndMPE);
	int32_t getMaxLength() override;
	bool hasAnyPitchExpressionAutomationOnNoteRows();
	ModelStackWithNoteRow* duplicateModelStackForClipBeingRecordedFrom(ModelStackWithNoteRow* modelStack,
	                                                                   char* otherModelStackMemory);
	void incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks) override;

	// ----- TimelineCounter implementation -------
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack) override;

	bool renderSidebar(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return instrumentClipView.renderSidebar(whichRows, image, occupancyMask);
	};

protected:
	void posReachedEnd(ModelStackWithTimelineCounter* modelStack) override;
	bool wantsToBeginLinearRecording(Song* song) override;
	bool cloneOutput(ModelStackWithTimelineCounter* modelStack) override;
	void pingpongOccurred(ModelStackWithTimelineCounter* modelStack) override;

private:
	InstrumentClip* instrumentWasLoadedByReferenceFromClip{};

	void deleteEmptyNoteRowsAtEitherEnd(bool onlyIfNoDrum, ModelStackWithTimelineCounter* modelStack,
	                                    bool mustKeepLastOne = true, bool keepOnesWithMIDIInput = true);
	void sendPendingNoteOn(ModelStackWithTimelineCounter* modelStack, PendingNoteOn* pendingNoteOn);
	Error undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack);
	void deleteBackedUpParamManagerMIDI();
	bool possiblyDeleteEmptyNoteRow(NoteRow* noteRow, bool onlyIfNoDrum, Song* song, bool onlyIfNonNumeric = false,
	                                bool keepIfHasMIDIInput = true);
	void actuallyDeleteEmptyNoteRow(ModelStackWithNoteRow* modelStack);
	void prepareToEnterKitMode(Song* song);
	Error readMIDIParamsFromFile(Deserializer& reader, int32_t readAutomationUpToPos);

	bool lastProbabilities[kNumProbabilityValues]{};
	int32_t lastProbabiltyPos[kNumProbabilityValues]{};
	bool currentlyRecordingLinearly;
};
