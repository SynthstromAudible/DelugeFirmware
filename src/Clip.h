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

#ifndef CLIP_H_
#define CLIP_H_

#include <TimelineCounter.h>
#include "r_typedefs.h"
#include "definitions.h"
#include "LearnedMIDI.h"

#define CLIP_TYPE_INSTRUMENT 0
#define CLIP_TYPE_AUDIO 1

#define OVERDUB_NORMAL 0
#define OVERDUB_CONTINUOUS_LAYERING 1

class Song;
class ParamManagerForTimeline;
class Output;
class InstrumentClip;
class Action;
class TimelineView;
class ParamManagerForTimeline;
class ModelStackWithTimelineCounter;

class Clip : public TimelineCounter {
public:
	Clip(int newType);
	virtual ~Clip();
	bool cancelAnyArming();
	int getMaxZoom();
	virtual int32_t getMaxLength();
	virtual int clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false) = 0;
	void cloneFrom(Clip* other);
	void beginInstance(Song* song, int32_t arrangementRecordPos);
	void endInstance(int32_t arrangementRecordPos, bool evenIfOtherClip = false);
	virtual void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos,
	                    bool useActualPosForParamManagers = true);
	virtual void setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos = true);
	virtual void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true) = 0;
	virtual void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true) = 0;
	virtual void reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack);
	virtual void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast);
	void prepareForDestruction(ModelStackWithTimelineCounter* modelStack, int instrumentRemovalInstruction);
	uint32_t getLivePos();
	uint32_t getActualCurrentPosAsIfPlayingInForwardDirection();
	int32_t getLastProcessedPos();
	int32_t getCurrentPosAsIfPlayingInForwardDirection();
	Clip* getClipBeingRecordedFrom();
	Clip* getClipToRecordTo();
	bool isArrangementOnlyClip();
	bool isActiveOnOutput();
	virtual bool deleteSoundsWhichWontSound(Song* song);
	virtual int appendClip(ModelStackWithTimelineCounter* thisModelStack,
	                       ModelStackWithTimelineCounter* otherModelStack);
	int resumeOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                    ModelStackWithTimelineCounter* modelStackClone);
	virtual int transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                                      ModelStackWithTimelineCounter* modelStackClone) {
		return NO_ERROR;
	}
	virtual void increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
	                                       int independentNoteRowInstruction,
	                                       bool completelyRenderOutIterationDependence = false, Action* action = NULL) {
	} // This is not implemented for AudioClips - because in the cases where we call this, we don't want it to happen for AudioClips
	virtual void lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action = NULL);
	virtual void getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager, Sound* sound);

	virtual void
	detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                 bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                 bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                 bool shouldBackUpExpressionParamsToo =
	                     true) = 0; // You're likely to want to call pickAnActiveClipIfPossible() after this

	virtual int undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack);
	virtual bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen,
	                               int32_t xScroll, uint32_t xZoom, uint8_t* image, uint8_t occupancyMask[],
	                               bool addUndefinedArea = true, int noteRowIndexStart = 0,
	                               int noteRowIndexEnd = 2147483647, int xStart = 0, int xEnd = displayWidth,
	                               bool allowBlur = true, bool drawRepeats = false);
	virtual int
	claimOutput(ModelStackWithTimelineCounter*
	                modelStack) = 0; // To be called after Song loaded, to link to the relevant Output object
	virtual void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop = NULL,
	                                   int buttonLatencyForTempolessRecord = 0) = 0;
	virtual int beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int buttonPressLatency) = 0;
	void drawUndefinedArea(int32_t localScroll, uint32_t, int32_t lengthToDisplay, uint8_t* image, uint8_t[],
	                       int imageWidth, TimelineView* editorScreen, bool tripletsOnHere);
	bool opportunityToBeginSessionLinearRecording(ModelStackWithTimelineCounter* modelStack, bool* newOutputCreated,
	                                              int buttonPressLatency);
	virtual Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, int newOverdubNature) = 0;
	virtual bool getCurrentlyRecordingLinearly() = 0;
	virtual bool currentlyScrollableAndZoomable() = 0;
	virtual void clear(Action* action, ModelStackWithTimelineCounter* modelStack);

	void writeToFile(Song* song);
	virtual void writeDataToFile(Song* song);
	virtual char const* getXMLTag() = 0;
	virtual int readFromFile(Song* song) = 0;
	void readTagFromFile(char const* tagName, Song* song, int32_t* readAutomationUpToPos);

	virtual void copyBasicsFrom(Clip* otherClip);
	void setupForRecordingAsAutoOverdub(Clip* existingClip, Song* song, int newOverdubNature);
	void outputChanged(ModelStackWithTimelineCounter* modelStack, Output* newOutput);
	virtual bool isAbandonedOverdub() = 0;
	virtual bool wantsToBeginLinearRecording(Song* song);
	virtual void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                                   uint32_t timeRemainder, int32_t suggestedLength,
	                                                   int32_t alternativeLongerLength) = 0;
	virtual void abortRecording() = 0;
	virtual void stopAllNotesPlaying(Song* song, bool actuallySoundChange = true) {}
	virtual bool willCloneOutputForOverdub() { return false; }
	void setSequenceDirectionMode(ModelStackWithTimelineCounter* modelStack, int newSequenceDirection);
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* modelStack);
	virtual void incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks);

	// ----- PlayPositionCounter implementation -------
	int32_t getLoopLength();
	bool isPlayingAutomationNow();
	bool backtrackingCouldLoopBackToEnd();
	int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack);
	TimelineCounter* getTimelineCounterToRecordTo();
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack);
	void expectEvent();

	Output* output;

	int16_t colourOffset;

	const uint8_t type;
	uint8_t section;
	bool soloingInSessionMode;
	uint8_t armState;
	bool activeIfNoSolo;
	bool wasActiveBefore; // A temporary thing used by Song::doLaunch()
	bool gotInstanceYet;  // For use only while loading song

	bool isPendingOverdub;
	bool isUnfinishedAutoOverdub;
	bool armedForRecording;
	bool wasWantingToDoLinearRecordingBeforeCountIn; // Only valid during a count-in
	uint8_t overdubNature;

	LearnedMIDI muteMIDICommand;

#if HAVE_SEQUENCE_STEP_CONTROL
	bool currentlyPlayingReversed;
	uint8_t sequenceDirectionMode;
#endif

	int32_t loopLength;
	int32_t
	    originalLength; // Before linear recording of this Clip began, and this Clip started getting extended to multiples of this

	int32_t lastProcessedPos;

	Clip* beingRecordedFromClip;

	int32_t repeatCount;

	uint32_t indexForSaving; // For use only while saving song

protected:
	virtual void
	posReachedEnd(ModelStackWithTimelineCounter*
	                  modelStack); // May change the TimelineCounter in the modelStack if new Clip got created
	virtual bool
	cloneOutput(ModelStackWithTimelineCounter* modelStack) = 0; // Returns whether a new Output was in fact created
	int solicitParamManager(Song* song, ParamManager* newParamManager = NULL,
	                        Clip* favourClipForCloningParamManager = NULL);
	virtual void pingpongOccurred(ModelStackWithTimelineCounter* modelStack) {
	}
};

#endif /* CLIP_H_ */
