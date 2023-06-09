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

#include <Clip.h>
#include <ParamManager.h>
#include <ParamManager.h>
#include <soundinstrument.h>
#include <SessionView.h>
#include "functions.h"
#include "definitions.h"
#include "playbackhandler.h"
#include "Output.h"
#include "functions.h"
#include "GeneralMemoryAllocator.h"
#include "song.h"
#include "View.h"
#include "ClipInstance.h"
#include "PlaybackMode.h"
#include "numericdriver.h"
#include "storagemanager.h"
#include "Session.h"
#include "TimelineView.h"
#include "ActionLogger.h"
#include "ConsequenceOutputExistence.h"
#include <new>
#include "ConsequenceClipBeginLinearRecord.h"
#include "uart.h"
#include "ModelStack.h"
#include "AudioClip.h"

uint32_t loopRecordingCandidateRecentnessNextValue = 1;

Clip::Clip(int newType) : type(newType) {
	soloingInSessionMode = false;
	armState = ARM_STATE_OFF;
	activeIfNoSolo = true;
	wasActiveBefore = false; // Want to set this default in case a Clip was created during playback

	section = 0;
	output = NULL;
	beingRecordedFromClip = NULL;
	isPendingOverdub = false;
	isUnfinishedAutoOverdub = false;
	colourOffset = -60;
	overdubNature = OVERDUB_NORMAL;
	originalLength = 0;
	armedForRecording = true;

#if HAVE_SEQUENCE_STEP_CONTROL
	sequenceDirectionMode = SEQUENCE_DIRECTION_FORWARD;
#endif
}

Clip::~Clip() {
}

// This is more exhaustive than copyBasicsFrom(), and is designed to be used *between* different Clip types, just for the things which Clips have in common
void Clip::cloneFrom(Clip* otherClip) {
	Clip::copyBasicsFrom(otherClip);
	soloingInSessionMode = otherClip->soloingInSessionMode;
	armState = otherClip->armState;
	activeIfNoSolo = otherClip->activeIfNoSolo;
	wasActiveBefore = otherClip->wasActiveBefore;
	muteMIDICommand = otherClip->muteMIDICommand;
	lastProcessedPos = otherClip->lastProcessedPos;
	repeatCount = otherClip->repeatCount;
	armedForRecording = otherClip->armedForRecording;
}

void Clip::copyBasicsFrom(Clip* otherClip) {
	loopLength = otherClip->loopLength;
	colourOffset = otherClip->colourOffset;
	//modKnobMode = otherClip->modKnobMode;
	section = otherClip->section;
}

void Clip::setupForRecordingAsAutoOverdub(Clip* existingClip, Song* song, int newOverdubNature) {
	copyBasicsFrom(existingClip);

	uint32_t newLength = existingClip->loopLength;

	if (newOverdubNature != OVERDUB_CONTINUOUS_LAYERING) {
		uint32_t currentScreenLength = currentSong->xZoom[NAVIGATION_CLIP] << displayWidthMagnitude;

		// If new length is a multiple of screen length, just use screen length
		if ((newLength % currentScreenLength) == 0) newLength = currentScreenLength;
	}

	loopLength = originalLength = newLength;

	soloingInSessionMode = existingClip->soloingInSessionMode;
	armState = ARM_STATE_ON_NORMAL;
	activeIfNoSolo = false;
	wasActiveBefore = false;
	isPendingOverdub = true;
	isUnfinishedAutoOverdub = true;
}

bool Clip::cancelAnyArming() {
	if (armState) {
		armState = ARM_STATE_OFF;
	}
	else return false;

	return true;
}

int Clip::getMaxZoom() {
	int32_t maxLength = getMaxLength();
	unsigned int thisLength = displayWidth * 3;
	while (thisLength < maxLength)
		thisLength <<= 1;
	return thisLength >> displayWidthMagnitude;
}

uint32_t Clip::getLivePos() {
	int32_t currentPosHere = lastProcessedPos;

	int32_t numSwungTicksInSinceLastActioned = playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();

	if (currentlyPlayingReversed) {
		numSwungTicksInSinceLastActioned = -numSwungTicksInSinceLastActioned;
	}

	int32_t livePos = currentPosHere + numSwungTicksInSinceLastActioned;
	if (livePos < 0) livePos += loopLength; // Could happen if reversing and currentPosHere is 0.

	return livePos;
}

uint32_t Clip::getActualCurrentPosAsIfPlayingInForwardDirection() {
	int32_t actualPos = lastProcessedPos;

	int32_t numSwungTicksInSinceLastActioned = playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();

#if HAVE_SEQUENCE_STEP_CONTROL
	if (currentlyPlayingReversed) {
		actualPos = loopLength - actualPos;
	}
#endif
	actualPos += numSwungTicksInSinceLastActioned;

	return actualPos;
}

int32_t Clip::getCurrentPosAsIfPlayingInForwardDirection() {
	int posToReturn = lastProcessedPos;

#if HAVE_SEQUENCE_STEP_CONTROL
	if (currentlyPlayingReversed) {
		posToReturn = loopLength - posToReturn;
	}
#endif
	return posToReturn;
}

int32_t Clip::getLastProcessedPos() {
	return lastProcessedPos;
}

Clip* Clip::getClipBeingRecordedFrom() {
	if (beingRecordedFromClip) return beingRecordedFromClip;
	else return this;
}

bool Clip::isArrangementOnlyClip() {
	return (section == 255);
}

bool Clip::isActiveOnOutput() {
	return (output->activeClip == this);
}

// Note: it's now the caller's job to increment currentPos before calling this! But we check here whether it's looped and needs setting back to "0".
// We may change the TimelineCounter in the modelStack if new Clip got created.
void Clip::processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast) {

	// Firstly, a bit of stuff that has to be dealt with ideally before calling posReachedEnd(), and definitely before we think about pingponging while in reverse.
	// The consequence of not doing this is only apparent in one special case, where a NoteRow contains just one tail-less sound (e.g. kick sample) right on beat "0".
	// Pingponging breaks in such a case. Reason being, when coming back from right to left, lastProcessedPos keeps going into the negative, because there are no events,
	// and eventually reaches -loopLength when we get here. We need to have our wrapping-negative-lastProcessedPos-to-positive code here at the start, so it can
	// be correctly wrapped up to exactly 0 - because various places below check if lastProcessedPos == 0.
	if (currentlyPlayingReversed) {
		// So yeah, if actually got left of zero, it's time to loop/wrap. Normally this wouldn't happen if pingponging because direction changes
		// right when we hit zero. Except the case discussed above, where actually our getting left of 0 is something that happens when we start moving in reverse,
		// regardless of whether it happened as part of a pingpong.
		if (lastProcessedPos < 0) {

			// But in some cases, we might have got here and still need to pingpong (if length changed or something?), so go check that.
			// Actually wait, don't, because doing one normal pingpong from forward to reverse will put us in this position, and
			// we don't want to do a second pingpong right after, or else there's effectively no proper pingpong!
			// if (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG) goto doPingpongReversed;

			lastProcessedPos += loopLength;
			//repeatCount++;
		}
	}

	// If we've reached the end, need to call posReachedEnd() - but that just deals with stuff like extending and appending Clips. It doesn't deal with the wrapping and pingponging
	// stuff that this function is mostly concerned with.
	int endPos = currentlyPlayingReversed ? 0 : loopLength;
	if (lastProcessedPos == endPos && repeatCount >= 0) {
		posReachedEnd(
		    modelStack); // This may alter length, changing what happens in the below if statements, which is why we can't combine this.
		if (modelStack->getTimelineCounter() != this) return; // Why exactly?
	}

	int32_t ticksTilEnd;
	bool didPingpong = false;

	if (currentlyPlayingReversed) {
		// Normally we do the pingpong when we hit pos 0, so the direction will change and we'll start going right again now, in time for
		// NoteRows and stuff to know the direction as they're processed and predict what notes we're going to hit next etc.
		if (!lastProcessedPos) { // Possibly only just became the case, above.
			repeatCount++;
			if (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG) {
				lastProcessedPos = -lastProcessedPos; // In case it did get left of zero.
				currentlyPlayingReversed = !currentlyPlayingReversed;
				pingpongOccurred(modelStack);
				didPingpong = true;
				goto playingForwardNow;
			}
		}

		ticksTilEnd = lastProcessedPos;
		if (!ticksTilEnd) ticksTilEnd = loopLength;
	}

	else {
playingForwardNow:
		ticksTilEnd = loopLength - lastProcessedPos;
		if (ticksTilEnd
		    <= 0) { // Yes, note it might not always arrive directly at the end. When (Audio) Clip length is shortened,
			// the lastProcessedPos is altered, but it could be that many swung ticks have actually passed since we last
			// processed, so there might be a big jump forward and we end up past the loop point.

			lastProcessedPos -= loopLength;
			repeatCount++;

			if (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG) {
				// Normally we'll have hit the exact loop point, meaning lastProcessedPos will have wrapped to 0, above. But
				// just in case we went further, and need to wrap back to somewhere nearish the right-hand edge of the Clip...
				if (lastProcessedPos > 0) lastProcessedPos = loopLength - lastProcessedPos;
				currentlyPlayingReversed = !currentlyPlayingReversed;
				pingpongOccurred(modelStack);
				didPingpong = true;
			}
			ticksTilEnd += loopLength; // Yes, we might not be right at the loop point - see comment above.
		}
	}

	if (paramManager.mightContainAutomation()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

		if (didPingpong) {
			paramManager.notifyPingpongOccurred(modelStackWithThreeMainThings);
		}

		bool mayInterpolate = (output->type != INSTRUMENT_TYPE_MIDI_OUT && output->type != INSTRUMENT_TYPE_CV);
		paramManager.processCurrentPos(modelStackWithThreeMainThings, ticksSinceLast, currentlyPlayingReversed,
		                               didPingpong, mayInterpolate);
		if (paramManager.ticksTilNextEvent < playbackHandler.swungTicksTilNextEvent)
			playbackHandler.swungTicksTilNextEvent = paramManager.ticksTilNextEvent;
	}

	// At least make sure we come back at the end of this Clip
	if (ticksTilEnd < playbackHandler.swungTicksTilNextEvent) playbackHandler.swungTicksTilNextEvent = ticksTilEnd;
}

int Clip::appendClip(ModelStackWithTimelineCounter* thisModelStack, ModelStackWithTimelineCounter* otherModelStack) {
	Clip* otherClip = (Clip*)otherModelStack->getTimelineCounter();
	if (paramManager.containsAnyParamCollectionsIncludingExpression()
	    && otherClip->paramManager.containsAnyParamCollectionsIncludingExpression()) {

		bool pingpongingGenerally = (otherClip->sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG);

		bool shouldReverseThisRepeat =
		    (pingpongingGenerally && (((uint32_t)loopLength / (uint32_t)otherClip->loopLength) & 1))
		    || (otherClip->sequenceDirectionMode == SEQUENCE_DIRECTION_REVERSE);

		int32_t reverseThisRepeatWithLength = shouldReverseThisRepeat ? otherClip->loopLength : 0;

		paramManager.appendParamManager(
		    thisModelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager),
		    otherModelStack->addOtherTwoThingsButNoNoteRow(otherClip->output->toModControllable(),
		                                                   &otherClip->paramManager),
		    loopLength, reverseThisRepeatWithLength, pingpongingGenerally);
	}
	loopLength += otherClip->loopLength;

	return NO_ERROR;
}

// Accepts any pos >= -length.
// Virtual function - gets extended by both InstrumentClip and AudioClip. They both invoke this,
// and are also required to call setPosForParamManagers() or do something equivalent - and that "something equivalent" allows InstrumentClip to
// save time by iterating through NoteRows only once.
void Clip::setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers) {

	// It's a bit complex and maybe not the best way for stuff to work, but newPos may be negative because Session::armClipToStartOrSoloUsingQuantization()
	// subtracts playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick() from it. And this bit of old code here deals with that.
	if (newPos < 0) {
		newPos += loopLength;
		repeatCount = -1;
	}

	else {
		repeatCount = (uint32_t)newPos / (uint32_t)loopLength;
		newPos -= repeatCount * loopLength;
	}

	currentlyPlayingReversed =
	    (sequenceDirectionMode
	         == SEQUENCE_DIRECTION_REVERSE // Syncing pingponging with repeatCount is particularly important for when resuming after
	     || (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG
	         && (repeatCount & 1))); // recording a clone of this Clip from session to arranger.

	if (currentlyPlayingReversed) {
		if (newPos) newPos = loopLength - newPos;
		else
			repeatCount--; // Cos it's going to get incremented as a side effect of reversed clips starting at pos 0 after which they'll immediately wrap
	}

	lastProcessedPos = newPos;

	expectEvent(); // Remember, this is a virtual function call - extended in InstrumentClip
}

// Returns whether it was actually begun
bool Clip::opportunityToBeginSessionLinearRecording(ModelStackWithTimelineCounter* modelStack, bool* newOutputCreated,
                                                    int buttonPressLatency) {

	*newOutputCreated = false;

	if (playbackHandler.recording && wantsToBeginLinearRecording(modelStack->song)) {

		Action* action = actionLogger.getNewAction(
		    ACTION_RECORD, true); // Allow addition to existing Action - one might have already been created because
		                          // note recorded slightly early just before end of count-in

		if (isPendingOverdub) {
			*newOutputCreated = cloneOutput(modelStack);

			if (action) {
				action->recordClipExistenceChange(modelStack->song, &modelStack->song->sessionClips, this, CREATE);

				if (*newOutputCreated) {
					void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceOutputExistence));
					if (consMemory) {
						ConsequenceOutputExistence* cons = new (consMemory) ConsequenceOutputExistence(output, CREATE);
						action->addConsequence(cons);
					}
				}
			}
		}
		else {
			if (action) {
				void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceClipBeginLinearRecord));
				if (consMemory) {
					ConsequenceClipBeginLinearRecord* cons = new (consMemory) ConsequenceClipBeginLinearRecord(this);
					action->addConsequence(cons);
				}
			}
		}

		originalLength = loopLength;
		isPendingOverdub = false;

		int error = beginLinearRecording(modelStack, buttonPressLatency);
		if (error) {
			numericDriver.displayError(error);
			return false;
		}

		if (action) {
			actionLogger.updateAction(action); // Needed for vertical scroll reasons
		}

		return true;
	}
	return false;
}

void Clip::setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useLivePos) {

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		uint32_t pos = useLivePos ? getLivePos() : lastProcessedPos;
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.setPlayPos(pos, modelStackWithThreeMainThings, currentlyPlayingReversed);
	}
}

Clip* Clip::getClipToRecordTo() {
	if (output->activeClip && output->activeClip->beingRecordedFromClip == this) {
		return output->activeClip;
	}
	else return this;
}

// Grabs automated values from current play-pos. To be called after a possible big change made to automation data, e.g. after an undo.
// This is only to be called if playbackHandler.isEitherClockActive().
void Clip::reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack) {

	if (!isActiveOnOutput()) return; // Definitely don't do this if we're not an active Clip!

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		uint32_t actualPos = getLivePos();

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.setPlayPos(actualPos, modelStackWithThreeMainThings, currentlyPlayingReversed);
	}
}

// This gets called on the "unique" copy of the original Clip
int Clip::resumeOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
                                          ModelStackWithTimelineCounter* modelStackClone) {

	// Take back control!
	activeIfNoSolo = false;
	beingRecordedFromClip = NULL; // This now just gets set by endInstancesOfActiveClips()

	Clip* originalClip = (Clip*)modelStackOriginal->getTimelineCounter();
	originalClip->activeIfNoSolo =
	    true; // Must set this before calling setPos, otherwise, ParamManagers won't know to expectEvent()

	// Deliberately leave lastProcessedPos as a pos potentially far beyond the length of the original Clip. setPos()
	// will see this and wrap the position itself - including for individual NoteRows with independent length.
	originalClip->setPos(modelStackOriginal, lastProcessedPos, true);

	transferVoicesToOriginalClipFromThisClone(modelStackOriginal, modelStackClone);

	expectNoFurtherTicks(modelStackClone->song, false);

	originalClip->resumePlayback(modelStackClone, false);

	output->setActiveClip(modelStackOriginal, false);

	return NO_ERROR;
}

bool Clip::deleteSoundsWhichWontSound(Song* song) {
	return (output->isSkippingRendering() && !song->isClipActive(this)
	        && this != view.activeModControllableModelStack.getTimelineCounterAllowNull()
	        && this != song->syncScalingClip);
}

void Clip::beginInstance(Song* song, int32_t arrangementRecordPos) {

	int clipInstanceI = output->clipInstances.getNumElements();
	ClipInstance* clipInstance;

	// If there's a previous instance, make sure it doesn't cut into the new one. This is only actually necessary if doing a "late start"
	if (clipInstanceI) {
		clipInstance = output->clipInstances.getElement(clipInstanceI - 1);
		int maxLength = arrangementRecordPos - clipInstance->pos;

		if (maxLength <= 0) { // Shouldn't normally go below 0...
			song->deletingClipInstanceForClip(output, clipInstance->clip, NULL, false); // Calls audio routine...
			clipInstanceI--;
			goto setupClipInstance;
		}
		else {
			if (clipInstance->length > maxLength) clipInstance->length = maxLength;
		}
	}

	if (!output->clipInstances.insertAtIndex(clipInstanceI)) {
		clipInstance = output->clipInstances.getElement(clipInstanceI);
setupClipInstance:
		clipInstance->clip = this;
		clipInstance->length = loopLength;
		clipInstance->pos = arrangementRecordPos;
	}
}

void Clip::endInstance(int32_t arrangementRecordPos, bool evenIfOtherClip) {
	int clipInstanceI = output->clipInstances.search(arrangementRecordPos, LESS);
	if (clipInstanceI >= 0) {
		ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);

		// evenIfOtherClip is an emergency hung over New Year's Day 2019 fix to the problem where this could get called on the wrong Clip
		// (same Instrument though) because getClipToRecordTo() returns wrong Clip because activeClip has already been changed on Instrument
		// because other Clip became it already in same launch.
		if (clipInstance->clip == this || evenIfOtherClip) {
			clipInstance->length = arrangementRecordPos - clipInstance->pos;
		}
	}

	beingRecordedFromClip = NULL;
}

// Returns error code
// This whole function is virtual and overridden in (and sometimes called from) InstrumentClip, so don't worry about MIDI / CV cases - they're dealt with there
int Clip::undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack) {

	ModControllable* modControllable = output->toModControllable();

	bool success = modelStack->song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)modControllable,
	                                                                           this, &paramManager);

	if (!success) {
		if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("E245");
		return ERROR_BUG;
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(modControllable, &paramManager);

	paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL, false);

	return NO_ERROR;
}

// ----- TimelineCounter implementation -------

int32_t Clip::getLoopLength() {
	// If being recorded, it's auto extending, so won't loop
	if (false && beingRecordedFromClip) return 2147483647;

	// Or, normal case
	else return loopLength;
}

bool Clip::isPlayingAutomationNow() {
	return (currentSong->isClipActive(this)
	        || (beingRecordedFromClip && currentSong->isClipActive(beingRecordedFromClip)));
}

bool Clip::backtrackingCouldLoopBackToEnd() {
	return (repeatCount > 0);
}

int32_t Clip::getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) {
	return currentPlaybackMode->getPosAtWhichClipWillCut(modelStack);
}

void Clip::getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager, Sound* sound) {
	*suggestedParamManager = &newClip->paramManager;
}

TimelineCounter* Clip::getTimelineCounterToRecordTo() {
	return getClipToRecordTo();
}

void Clip::getActiveModControllable(ModelStackWithTimelineCounter* modelStack) {
	modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
}

void Clip::expectEvent() {
	playbackHandler.expectEvent();
}

// Returns false if can't because in card routine
// occupancyMask can be NULL
bool Clip::renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
                             uint32_t xZoom, uint8_t* image, uint8_t occupancyMask[], bool addUndefinedArea,
                             int noteRowIndexStart, int noteRowIndexEnd, int xStart, int xEnd, bool allowBlur,
                             bool drawRepeats) {

	memset(&image[xStart * 3], 0, (xEnd - xStart) * 3);
	if (occupancyMask) memset(&occupancyMask[xStart], 0, (xEnd - xStart));

	return true;
}

void Clip::writeToFile(Song* song) {

	char const* xmlTag = getXMLTag();

	storageManager.writeOpeningTagBeginning(xmlTag);

	writeDataToFile(song);

	storageManager.writeClosingTag(xmlTag);
}

void Clip::writeDataToFile(Song* song) {

	storageManager.writeAttribute("isPlaying", activeIfNoSolo);
	storageManager.writeAttribute("isSoloing", soloingInSessionMode);
	storageManager.writeAttribute("isArmedForRecording", armedForRecording);
	storageManager.writeAttribute("length", loopLength);
	if (sequenceDirectionMode != SEQUENCE_DIRECTION_FORWARD)
		storageManager.writeAttribute("sequenceDirection", sequenceDirectionModeToString(sequenceDirectionMode));
	storageManager.writeAttribute("colourOffset", colourOffset);
	if (section != 255) storageManager.writeAttribute("section", section);

	//storageManager.writeTag("activeModFunction", modKnobMode);

	if (currentSong->currentClip == this) {
		if (getRootUI()->toClipMinder()) {
			storageManager.writeAttribute("beingEdited", "1");
		}
		else {
			storageManager.writeAttribute("selected", "1");
		}
	}
	if (song->getSyncScalingClip() == this) storageManager.writeAttribute("isSyncScaleClip", "1");

	storageManager.writeOpeningTagEnd();

	muteMIDICommand.writeNoteToFile("muteMidiCommand");
}

void Clip::readTagFromFile(char const* tagName, Song* song, int32_t* readAutomationUpToPos) {

	if (!strcmp(tagName, "isPlaying")) {
		activeIfNoSolo = storageManager.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isSoloing")) {
		soloingInSessionMode = storageManager.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isArmedForRecording")) {
		armedForRecording = storageManager.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "status")) { // For backwards compatibility
		soloingInSessionMode = false;
		int newStatus = storageManager.readTagOrAttributeValueInt();
		activeIfNoSolo = (newStatus == 2);
	}

	else if (!strcmp(tagName, "section")) {
		section = storageManager.readTagOrAttributeValueInt();
		section = getMin(section, (uint8_t)(MAX_NUM_SECTIONS - 1));
	}

	else if (!strcmp(tagName, "trackLength") || !strcmp(tagName, "length")) {
		loopLength = storageManager.readTagOrAttributeValueInt();
		loopLength = getMax((int32_t)1, loopLength);
		*readAutomationUpToPos = loopLength;
	}

	else if (!strcmp(tagName, "colourOffset")) {
		colourOffset = storageManager.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "beingEdited")) {
		if (storageManager.readTagOrAttributeValueInt()) {
			song->currentClip = this;
			song->inClipMinderViewOnLoad = true;
		}
	}

	else if (!strcmp(tagName, "selected")) {
		if (storageManager.readTagOrAttributeValueInt()) {
			song->currentClip = this;
			song->inClipMinderViewOnLoad = false;
		}
	}

	else if (!strcmp(tagName, "isSyncScaleTrack") || !strcmp(tagName, "isSyncScaleClip")) {
		bool is = storageManager.readTagOrAttributeValueInt();

		// This is naughty - inputTickScaleClip shouldn't be accessed directly. But for simplicity, I'm using it to hold this Clip for now, and then
		// in song.cpp this gets made right in a moment...
		if (is) song->syncScalingClip = this;
	}

	else if (!strcmp(tagName, "muteMidiCommand")) {
		muteMIDICommand.readNoteFromFile();
	}

	else if (!strcmp(tagName, "sequenceDirection")) {
		sequenceDirectionMode = stringToSequenceDirectionMode(storageManager.readTagOrAttributeValue());
	}

	/*
	else if (!strcmp(tagName, "activeModFunction")) {
		//modKnobMode = stringToInt(storageManager.readTagContents());
		//modKnobMode = getMin(modKnobMode, (uint8_t)(NUM_MOD_BUTTONS - 1));
	}
	*/
}

void Clip::prepareForDestruction(ModelStackWithTimelineCounter* modelStack, int instrumentRemovalInstruction) {

	Output* oldOutput =
	    output; // There won't be an Instrument if the song is being deleted because it wasn't completely loaded

	modelStack->song->deleteBackedUpParamManagersForClip(this);

	if (output) {

		if (isActiveOnOutput() && playbackHandler.isEitherClockActive())
			expectNoFurtherTicks(
			    modelStack
			        ->song); // Still necessary? Actually maybe... I can see that this would at least cause an AudioClip to abortRecording()...

		detachFromOutput(modelStack, false);
	}

	if (oldOutput) { // One case where there won't be an Output is if the song is being deleted because it wasn't able to be completely loaded

		if (instrumentRemovalInstruction == INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED) {
			modelStack->song->deleteOrHibernateOutputIfNoClips(oldOutput);
		}

		else if (instrumentRemovalInstruction == INSTRUMENT_REMOVAL_DELETE) {
			modelStack->song->deleteOutputThatIsInMainList(oldOutput);
		}
	}
}

// Virtual function, extended by InstrumentClip.
void Clip::posReachedEnd(ModelStackWithTimelineCounter* modelStack) {
	// If linear recording (which means it must be a loop / session playback if we reached the end)
	if (getCurrentlyRecordingLinearly()) {

		// If they exited recording mode (as in the illuminated RECORD button), don't auto extend
		if (!playbackHandler.recording) {
			finishLinearRecording(modelStack);
		}

		// Otherwise, do auto extend
		else {
			int32_t oldLength = loopLength;

			loopLength += originalLength;

			sessionView.clipNeedsReRendering(this);

			// For InstrumentClips only, we record and make undoable the length-change here. For AudioClips, on the other hand, it happens
			// in one go at the end of the recording - because for those, if recording is aborted part-way, the whole Clip is deleted.
			// But, don't do this if this Clips still would get deleted as an "abandoned overdub" (meaning it has no notes), cos if that happened,
			// we definitely don't want to have a consequence - pointer pointing to it!
			if (true || type != CLIP_TYPE_AUDIO) {
				Uart::println("getting new action");
				Action* action = actionLogger.getNewAction(ACTION_RECORD, ACTION_ADDITION_ALLOWED);
				if (action) {
					action->recordClipLengthChange(this, oldLength);
				}
			}
		}
	}
}

// Caller must call resumePlayback on this Clip, unless you have a good reason not to.
void Clip::lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action) {

	if (loopLength < oldLength) {
		if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
			paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, action);
		}

		// If current pos is after the new length, have to wrap that!
		if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
			if (lastProcessedPos >= loopLength) {
				int extraLengthsDone = (uint32_t)lastProcessedPos / (uint32_t)loopLength;
				lastProcessedPos -= loopLength * extraLengthsDone;
				repeatCount += extraLengthsDone;
			}
			expectEvent();
		}
	}
}

// occupancyMask now optional
void Clip::drawUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay, uint8_t* rowImage,
                             uint8_t occupancyMask[], int imageWidth, TimelineView* timelineView, bool tripletsOnHere) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, NULL, xScroll, xZoom) + 1;

	if (greyStart < 0)
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check for this

	if (greyStart < imageWidth) {
		memset(rowImage + greyStart * 3, UNDEFINED_GREY_SHADE, (imageWidth - greyStart) * 3);
		if (occupancyMask) memset(occupancyMask + greyStart, 64, imageWidth - greyStart);
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int xDisplay = 0; xDisplay < imageWidth; xDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				uint8_t* pixel = rowImage + xDisplay * 3;
				pixel[0] = UNDEFINED_GREY_SHADE;
				pixel[1] = UNDEFINED_GREY_SHADE;
				pixel[2] = UNDEFINED_GREY_SHADE;

				if (occupancyMask) occupancyMask[xDisplay] = 64;
			}
		}
	}
}

void Clip::outputChanged(ModelStackWithTimelineCounter* modelStack, Output* newOutput) {

	// If we're currently playing and this Clip is active, we need to make it the Instrument's activeClip
	if (playbackHandler.playbackState && modelStack->song->isClipActive(this)) {
yesMakeItActive:
		newOutput->setActiveClip(modelStack);
	}

	else {
		// In any case, we want the newInstrument to have an activeClip, and if it doesn't yet have one, the supplied Clip makes a perfect candidate
		if (!newOutput->activeClip) goto yesMakeItActive;
	}
}

// Obviously don't call this for MIDI clips!
int Clip::solicitParamManager(Song* song, ParamManager* newParamManager, Clip* favourClipForCloningParamManager) {

	// Occasionally, like for AudioClips changing their Output, they will actually have a paramManager already, so everything's fine and we can return
	if (paramManager.containsAnyMainParamCollections()) return NO_ERROR;

	if (newParamManager) paramManager.stealParamCollectionsFrom(newParamManager, true);

	if (!paramManager.containsAnyMainParamCollections()) {

		ModControllable* modControllable = output->toModControllable();

		// If they're offering a Clip to just clone the ParamManager from...
		if (favourClipForCloningParamManager) {

			// Let's first just see if there already was a *perfect* backed-up one for this *exact* Clip, that we could just have. If so, great, we're done.
			if (song->getBackedUpParamManagerForExactClip((ModControllableAudio*)modControllable, this,
			                                              &paramManager)) {
trimFoundParamManager:
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, modControllable, this,
				                                                   &paramManager);
				paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL,
				                          false); // oldLength actually has no consequence anyway
				return NO_ERROR;
			}

			// Ok, still here, let's do that cloning
			paramManager.cloneParamCollectionsFrom(&favourClipForCloningParamManager->paramManager, false, true);
			// That might not work if there was insufficient RAM - very unlikely - but we'll still try the other options below
		}

		// If there wasn't one...
		if (!paramManager.containsAnyMainParamCollections()) {

			bool success = song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)modControllable, this,
			                                                               &paramManager);

			if (success) goto trimFoundParamManager;

			// Still no ParamManager, so copy it from another Clip
			Clip* otherClip = song->getClipWithOutput(output, false, this); // Exclude self
			if (otherClip) {

				int error = paramManager.cloneParamCollectionsFrom(&otherClip->paramManager, false, true);

				if (error) {
					numericDriver.freezeWithError("E050");
					return error;
				}
			}
			// Unless I've done something wrong, there *has* to be another Clip if the Output didn't have a backed-up ParamManager. But, just in case
			else {
				numericDriver.freezeWithError("E051");
				return ERROR_UNSPECIFIED;
			}
		}
	}

	return NO_ERROR;
}

void Clip::clear(Action* action, ModelStackWithTimelineCounter* modelStack) {
	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.deleteAllAutomation(action, modelStackWithThreeMainThings);
	}
}

int Clip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int buttonPressLatency) {
	if (!getRootUI() || !getRootUI()->toClipMinder()) {
		modelStack->song->currentClip = this;
	}
	return NO_ERROR;
}

bool Clip::wantsToBeginLinearRecording(Song* song) {
	return (armedForRecording && song->syncScalingClip != this);
}

void Clip::setSequenceDirectionMode(ModelStackWithTimelineCounter* modelStack, int newMode) {
	bool reversedBefore = currentlyPlayingReversed;
	sequenceDirectionMode = newMode;

	if (newMode != SEQUENCE_DIRECTION_PINGPONG) {
		currentlyPlayingReversed = (newMode == SEQUENCE_DIRECTION_REVERSE);

		if (reversedBefore != currentlyPlayingReversed) {
			lastProcessedPos = loopLength - lastProcessedPos;
			if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
				resumePlayback(modelStack, true);
			}
		}
	}
}

int32_t Clip::getMaxLength() {
	return loopLength;
}

bool Clip::possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* modelStack) {

	if (playbackHandler.recording == RECORDING_ARRANGEMENT && playbackHandler.isEitherClockActive()
	    && !isArrangementOnlyClip() && modelStack->song->isClipActive(this)) {

		if (output->activeClip && output->activeClip->beingRecordedFromClip == this) {
			modelStack->setTimelineCounter(output->activeClip);
		}

		else {

			if (!modelStack->song->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) return false;

			// Find the ClipInstance which we expect to have already been created
			int clipInstanceI = output->clipInstances.search(playbackHandler.getActualArrangementRecordPos() + 1, LESS);

			// If it can't be found (should be impossible), we'll just get out and leave everything the same, so at least nothing will crash
			if (clipInstanceI < 0) return false;

			ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);

			if (type == CLIP_TYPE_AUDIO) {

				// So, we want to create a bunch of repeats. Often there'll be many at the start which just repeat with untouched params,
				// so that can all be one ClipInstance

				if (repeatCount >= 1) {

					int32_t oldClipInstancePos = clipInstance->pos;

					clipInstance->length = repeatCount * loopLength;

					// And then we'll need a new ClipInstance for this new instance that we're gonna record some automation on
					clipInstanceI++;

					int error = output->clipInstances.insertAtIndex(clipInstanceI);
					if (error) return false;

					clipInstance = output->clipInstances.getElement(clipInstanceI);

					clipInstance->pos = oldClipInstancePos + repeatCount * loopLength;
				}
			}

			int error = clone(modelStack, true); // Puts the cloned Clip into the modelStack. Flattens reversing.
			if (error) return false;

			Clip* newClip = (Clip*)modelStack->getTimelineCounter();

			newClip->section = 255;

			int32_t newLength = loopLength;

			if (type == CLIP_TYPE_INSTRUMENT) {
				newLength *= (repeatCount + 1);
				newClip->increaseLengthWithRepeats(modelStack, newLength, INDEPENDENT_NOTEROW_LENGTH_INCREASE_ROUND_UP,
				                                   true); // Yes, call this even if length is staying the same,
			}                                             // because there might be shorter NoteRows.

			// Add to Song
			modelStack->song->arrangementOnlyClips.insertClipAtIndex(newClip, 0); // Can't fail

			expectNoFurtherTicks(modelStack->song, false); // Don't sound

			clipInstance->clip = newClip;
			clipInstance->length = newLength;

			newClip->activeIfNoSolo =
			    true; // Must set this before calling setPos, otherwise, ParamManagers won't know to expectEvent()

			// Sort out new play-pos. Must "flatten" reversing.
			int32_t newPlayPos = lastProcessedPos;
			if (currentlyPlayingReversed) {
				newPlayPos = -newPlayPos;
				if (newPlayPos < 0) newPlayPos += loopLength;
			}
			if (type == CLIP_TYPE_INSTRUMENT) newPlayPos += repeatCount * loopLength;
			newClip->setPos(modelStack, newPlayPos, true);
			newClip->resumePlayback(modelStack, false); // Don't sound

			if (type == CLIP_TYPE_AUDIO) {
				((AudioClip*)newClip)->voiceSample = ((AudioClip*)this)->voiceSample;
				((AudioClip*)this)->voiceSample = NULL;
			}

			newClip->activeIfNoSolo = false; // And now, we want it to actually be false
			newClip->beingRecordedFromClip = this;
			output->setActiveClip(modelStack, false);
		}

		return true;
	}

	return false;
}

void Clip::incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks) {
	if (currentlyPlayingReversed) numTicks = -numTicks;
	lastProcessedPos += numTicks;
}
