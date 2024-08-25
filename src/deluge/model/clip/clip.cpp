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

#include "model/clip/clip.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/consequence/consequence_clip_begin_linear_record.h"
#include "model/consequence/consequence_output_existence.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include <new>

namespace params = deluge::modulation::params;

uint32_t loopRecordingCandidateRecentnessNextValue = 1;

Clip::Clip(ClipType newType) : type(newType) {
	soloingInSessionMode = false;
	armState = ArmState::OFF;
	activeIfNoSolo = true;
	activeIfNoSoloBeforeStemExport = activeIfNoSolo;
	exportStem = false;
	wasActiveBefore = false; // Want to set this default in case a Clip was created during playback

	section = 0;
	output = NULL;
	beingRecordedFromClip = NULL;
	isPendingOverdub = false;
	isUnfinishedAutoOverdub = false;
	colourOffset = -60;
	overdubNature = OverDubType::Normal;
	originalLength = 0;
	armedForRecording = true;
	launchStyle = LaunchStyle::DEFAULT;
	fillEventAtTickCount = 0;

	// initialize automation clip view variables
	onAutomationClipView = false;
	lastSelectedParamID = kNoSelection;
	lastSelectedParamKind = params::Kind::NONE;
	lastSelectedParamShortcutX = kNoSelection;
	lastSelectedParamShortcutY = kNoSelection;
	lastSelectedParamArrayPosition = 0;
	lastSelectedOutputType = OutputType::NONE;
	lastSelectedPatchSource = PatchSource::NONE;
	// end initialize of automation clip view variables

#if HAVE_SEQUENCE_STEP_CONTROL
	sequenceDirectionMode = SequenceDirection::FORWARD;
#endif
}

Clip::~Clip() {
	if (getCurrentClip() == this) {
		currentSong->setCurrentClip(nullptr);
	}
}

// This is more exhaustive than copyBasicsFrom(), and is designed to be used *between* different Clip types, just for
// the things which Clips have in common
void Clip::cloneFrom(Clip const* otherClip) {
	Clip::copyBasicsFrom(otherClip);
	soloingInSessionMode = otherClip->soloingInSessionMode;
	armState = otherClip->armState;
	activeIfNoSolo = otherClip->activeIfNoSolo;
	wasActiveBefore = otherClip->wasActiveBefore;
	muteMIDICommand = otherClip->muteMIDICommand;
	lastProcessedPos = otherClip->lastProcessedPos;
	repeatCount = otherClip->repeatCount;
	armedForRecording = otherClip->armedForRecording;
	launchStyle = otherClip->launchStyle;
}

void Clip::copyBasicsFrom(Clip const* otherClip) {
	loopLength = otherClip->loopLength;
	colourOffset = otherClip->colourOffset;
	// modKnobMode = otherClip->modKnobMode;
	section = otherClip->section;
	launchStyle = otherClip->launchStyle;
	onAutomationClipView = otherClip->onAutomationClipView;
}

void Clip::setupForRecordingAsAutoOverdub(Clip* existingClip, Song* song, OverDubType newOverdubNature) {
	copyBasicsFrom(existingClip);

	uint32_t newLength = existingClip->loopLength;

	if (newOverdubNature != OverDubType::ContinuousLayering) {
		uint32_t currentScreenLength = currentSong->xZoom[NAVIGATION_CLIP] << kDisplayWidthMagnitude;

		// If new length is a multiple of screen length, just use screen length
		if ((newLength % currentScreenLength) == 0) {
			newLength = currentScreenLength;
		}
	}

	loopLength = originalLength = newLength;

	soloingInSessionMode = existingClip->soloingInSessionMode;
	armState = ArmState::ON_NORMAL;
	activeIfNoSolo = false;
	wasActiveBefore = false;
	isPendingOverdub = true;
	isUnfinishedAutoOverdub = true;
}

bool Clip::cancelAnyArming() {
	if (armState != ArmState::OFF) {
		armState = ArmState::OFF;
		return true;
	}
	return false;
}

int32_t Clip::getMaxZoom() {
	int32_t maxLength = getMaxLength();
	uint32_t thisLength = kDisplayWidth * 3;
	while (thisLength < maxLength) {
		thisLength <<= 1;
	}
	return thisLength >> kDisplayWidthMagnitude;
}

uint32_t Clip::getLivePos() const {
	int32_t currentPosHere = lastProcessedPos;

	int32_t numSwungTicksInSinceLastActioned = playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();

	if (currentlyPlayingReversed) {
		numSwungTicksInSinceLastActioned = -numSwungTicksInSinceLastActioned;
	}

	int32_t livePos = currentPosHere + numSwungTicksInSinceLastActioned;
	if (livePos < 0) {
		livePos += loopLength; // Could happen if reversing and currentPosHere is 0.
	}

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
	int32_t posToReturn = lastProcessedPos;

#if HAVE_SEQUENCE_STEP_CONTROL
	if (currentlyPlayingReversed) {
		posToReturn = loopLength - posToReturn;
	}
#endif
	return posToReturn;
}

int32_t Clip::getLastProcessedPos() const {
	return lastProcessedPos;
}

Clip* Clip::getClipBeingRecordedFrom() {
	if (beingRecordedFromClip) {
		return beingRecordedFromClip;
	}
	else {
		return this;
	}
}

bool Clip::isArrangementOnlyClip() const {
	return (section == 255);
}

bool Clip::isActiveOnOutput() {
	return (output->getActiveClip() == this);
}

// Note: it's now the caller's job to increment currentPos before calling this! But we check here whether it's looped
// and needs setting back to "0". We may change the TimelineCounter in the modelStack if new Clip got created.
void Clip::processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast) {

	// Firstly, a bit of stuff that has to be dealt with ideally before calling posReachedEnd(), and definitely before
	// we think about pingponging while in reverse. The consequence of not doing this is only apparent in one special
	// case, where a NoteRow contains just one tail-less sound (e.g. kick sample) right on beat "0". Pingponging breaks
	// in such a case. Reason being, when coming back from right to left, lastProcessedPos keeps going into the
	// negative, because there are no events, and eventually reaches -loopLength when we get here. We need to have our
	// wrapping-negative-lastProcessedPos-to-positive code here at the start, so it can be correctly wrapped up to
	// exactly 0 - because various places below check if lastProcessedPos == 0.
	if (currentlyPlayingReversed) {
		// So yeah, if actually got left of zero, it's time to loop/wrap. Normally this wouldn't happen if pingponging
		// because direction changes right when we hit zero. Except the case discussed above, where actually our getting
		// left of 0 is something that happens when we start moving in reverse, regardless of whether it happened as
		// part of a pingpong.
		if (lastProcessedPos < 0) {

			// But in some cases, we might have got here and still need to pingpong (if length changed or something?),
			// so go check that. Actually wait, don't, because doing one normal pingpong from forward to reverse will
			// put us in this position, and we don't want to do a second pingpong right after, or else there's
			// effectively no proper pingpong! if (sequenceDirectionMode == SequenceDirection::PINGPONG) goto
			// doPingpongReversed;

			lastProcessedPos += loopLength;
			// repeatCount++;
		}
	}

	// If we've reached the end, need to call posReachedEnd() - but that just deals with stuff like extending and
	// appending Clips. It doesn't deal with the wrapping and pingponging stuff that this function is mostly concerned
	// with.
	int32_t endPos = currentlyPlayingReversed ? 0 : loopLength;
	if (lastProcessedPos == endPos && repeatCount >= 0) {
		posReachedEnd(modelStack); // This may alter length, changing what happens in the below if statements, which is
		                           // why we can't combine this.
		if (modelStack->getTimelineCounter() != this) {
			return; // Why exactly?
		}
	}

	int32_t ticksTilEnd;
	bool didPingpong = false;

	if (currentlyPlayingReversed) {
		// Normally we do the pingpong when we hit pos 0, so the direction will change and we'll start going right again
		// now, in time for NoteRows and stuff to know the direction as they're processed and predict what notes we're
		// going to hit next etc.
		if (!lastProcessedPos) { // Possibly only just became the case, above.
			repeatCount++;
			if (sequenceDirectionMode == SequenceDirection::PINGPONG) {
				lastProcessedPos = -lastProcessedPos; // In case it did get left of zero.
				currentlyPlayingReversed = !currentlyPlayingReversed;
				pingpongOccurred(modelStack);
				didPingpong = true;
				goto playingForwardNow;
			}
		}

		ticksTilEnd = lastProcessedPos;
		if (!ticksTilEnd) {
			ticksTilEnd = loopLength;
		}
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

			if (sequenceDirectionMode == SequenceDirection::PINGPONG) {
				// Normally we'll have hit the exact loop point, meaning lastProcessedPos will have wrapped to 0, above.
				// But just in case we went further, and need to wrap back to somewhere nearish the right-hand edge of
				// the Clip...
				if (lastProcessedPos > 0) {
					lastProcessedPos = loopLength - lastProcessedPos;
				}
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

		bool mayInterpolate = (output->type != OutputType::MIDI_OUT && output->type != OutputType::CV);
		paramManager.processCurrentPos(modelStackWithThreeMainThings, ticksSinceLast, currentlyPlayingReversed,
		                               didPingpong, mayInterpolate);
		if (paramManager.ticksTilNextEvent < playbackHandler.swungTicksTilNextEvent) {
			playbackHandler.swungTicksTilNextEvent = paramManager.ticksTilNextEvent;
		}
	}

	// At least make sure we come back at the end of this Clip
	if (ticksTilEnd < playbackHandler.swungTicksTilNextEvent) {
		playbackHandler.swungTicksTilNextEvent = ticksTilEnd;
	}
}

Error Clip::appendClip(ModelStackWithTimelineCounter* thisModelStack, ModelStackWithTimelineCounter* otherModelStack) {
	Clip* otherClip = (Clip*)otherModelStack->getTimelineCounter();
	if (paramManager.containsAnyParamCollectionsIncludingExpression()
	    && otherClip->paramManager.containsAnyParamCollectionsIncludingExpression()) {

		bool pingpongingGenerally = (otherClip->sequenceDirectionMode == SequenceDirection::PINGPONG);

		bool shouldReverseThisRepeat =
		    (pingpongingGenerally && (((uint32_t)loopLength / (uint32_t)otherClip->loopLength) & 1))
		    || (otherClip->sequenceDirectionMode == SequenceDirection::REVERSE);

		int32_t reverseThisRepeatWithLength = shouldReverseThisRepeat ? otherClip->loopLength : 0;

		paramManager.appendParamManager(
		    thisModelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager),
		    otherModelStack->addOtherTwoThingsButNoNoteRow(otherClip->output->toModControllable(),
		                                                   &otherClip->paramManager),
		    loopLength, reverseThisRepeatWithLength, pingpongingGenerally);
	}
	loopLength += otherClip->loopLength;

	return Error::NONE;
}

// Accepts any pos >= -length.
// Virtual function - gets extended by both InstrumentClip and AudioClip. They both invoke this,
// and are also required to call setPosForParamManagers() or do something equivalent - and that "something equivalent"
// allows InstrumentClip to save time by iterating through NoteRows only once.
void Clip::setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers) {

	// It's a bit complex and maybe not the best way for stuff to work, but newPos may be negative because
	// Session::armClipToStartOrSoloUsingQuantization() subtracts
	// playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick() from it. And this bit of old code here deals with
	// that.
	if (newPos < 0) {
		newPos += loopLength;
		repeatCount = -1;
	}

	else {
		repeatCount = (uint32_t)newPos / (uint32_t)loopLength;
		newPos -= repeatCount * loopLength;
	}

	currentlyPlayingReversed =
	    (sequenceDirectionMode == SequenceDirection::REVERSE // Syncing pingponging with repeatCount is particularly
	                                                         // important for when resuming after
	     || (sequenceDirectionMode == SequenceDirection::PINGPONG
	         && (repeatCount & 1))); // recording a clone of this Clip from session to arranger.

	if (currentlyPlayingReversed) {
		if (newPos) {
			newPos = loopLength - newPos;
		}
		else {
			repeatCount--; // Cos it's going to get incremented as a side effect of reversed clips starting at pos 0
			               // after which they'll immediately wrap
		}
	}

	lastProcessedPos = newPos;

	expectEvent(); // Remember, this is a virtual function call - extended in InstrumentClip
}

// Returns whether it was actually begun
bool Clip::opportunityToBeginSessionLinearRecording(ModelStackWithTimelineCounter* modelStack, bool* newOutputCreated,
                                                    int32_t buttonPressLatency) {

	*newOutputCreated = false;

	if (playbackHandler.recording != RecordingMode::OFF && wantsToBeginLinearRecording(modelStack->song)) {

		// Allow addition to existing Action - one might have already been created because
		// note recorded slightly early just before end of count-in
		Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

		if (isPendingOverdub) {
			*newOutputCreated = cloneOutput(modelStack);

			if (action != nullptr) {
				action->recordClipExistenceChange(modelStack->song, &modelStack->song->sessionClips, this,
				                                  ExistenceChangeType::CREATE);

				if (*newOutputCreated) {
					void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceOutputExistence));
					if (consMemory != nullptr) {
						auto* cons = new (consMemory) ConsequenceOutputExistence(output, ExistenceChangeType::CREATE);
						action->addConsequence(cons);
					}
				}
			}
		}
		else {
			if (action != nullptr) {
				void* consMemory =
				    GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipBeginLinearRecord));
				if (consMemory != nullptr) {
					auto* cons = new (consMemory) ConsequenceClipBeginLinearRecord(this);
					action->addConsequence(cons);
				}
			}
		}

		originalLength = loopLength;
		isPendingOverdub = false;

		Error error = beginLinearRecording(modelStack, buttonPressLatency);
		if (error != Error::NONE) {
			display->displayError(error);
			return false;
		}

		if (action != nullptr) {
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
	if (output->getActiveClip() && output->getActiveClip()->beingRecordedFromClip == this) {
		return output->getActiveClip();
	}
	else {
		return this;
	}
}

// Grabs automated values from current play-pos. To be called after a possible big change made to automation data, e.g.
// after an undo. This is only to be called if playbackHandler.isEitherClockActive().
void Clip::reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack) {

	if (!isActiveOnOutput()) {
		return; // Definitely don't do this if we're not an active Clip!
	}

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		uint32_t actualPos = getLivePos();

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.setPlayPos(actualPos, modelStackWithThreeMainThings, currentlyPlayingReversed);
	}
}

// This gets called on the "unique" copy of the original Clip
Error Clip::resumeOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
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

	output->setActiveClip(modelStackOriginal, PgmChangeSend::NEVER);

	return Error::NONE;
}

bool Clip::deleteSoundsWhichWontSound(Song* song) {
	return (output->isSkippingRendering() && !song->isClipActive(this)
	        && this != view.activeModControllableModelStack.getTimelineCounterAllowNull()
	        && this != song->syncScalingClip);
}

void Clip::beginInstance(Song* song, int32_t arrangementRecordPos) {

	int32_t clipInstanceI = output->clipInstances.getNumElements();
	ClipInstance* clipInstance;

	// If there's a previous instance, make sure it doesn't cut into the new one. This is only actually necessary if
	// doing a "late start"
	if (clipInstanceI) {
		clipInstance = output->clipInstances.getElement(clipInstanceI - 1);
		int32_t maxLength = arrangementRecordPos - clipInstance->pos;

		if (maxLength <= 0) { // Shouldn't normally go below 0...
			song->deletingClipInstanceForClip(output, clipInstance->clip, NULL, false); // Calls audio routine...
			clipInstanceI--;
			goto setupClipInstance;
		}
		else {
			if (clipInstance->length > maxLength) {
				clipInstance->length = maxLength;
			}
		}
	}

	if (output->clipInstances.insertAtIndex(clipInstanceI) == Error::NONE) {
		clipInstance = output->clipInstances.getElement(clipInstanceI);
setupClipInstance:
		clipInstance->clip = this;
		clipInstance->length = loopLength;
		clipInstance->pos = arrangementRecordPos;
	}
}

void Clip::endInstance(int32_t arrangementRecordPos, bool evenIfOtherClip) {
	int32_t clipInstanceI = output->clipInstances.search(arrangementRecordPos, LESS);
	if (clipInstanceI >= 0) {
		ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);

		// evenIfOtherClip is an emergency hung over New Year's Day 2019 fix to the problem where this could get called
		// on the wrong Clip (same Instrument though) because getClipToRecordTo() returns wrong Clip because activeClip
		// has already been changed on Instrument because other Clip became it already in same launch.
		if (clipInstance->clip == this || evenIfOtherClip) {
			clipInstance->length = arrangementRecordPos - clipInstance->pos;
		}
	}

	beingRecordedFromClip = NULL;
}

// Returns error code
// This whole function is virtual and overridden in (and sometimes called from) InstrumentClip, so don't worry about
// MIDI / CV cases - they're dealt with there
Error Clip::undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack) {

	ModControllable* modControllable = output->toModControllable();

	bool success = modelStack->song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)modControllable,
	                                                                           this, &paramManager);

	if (!success) {
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("E245");
		}
		return Error::BUG;
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(modControllable, &paramManager);

	paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL, false);

	return Error::NONE;
}

// ----- TimelineCounter implementation -------

int32_t Clip::getLoopLength() const {
	// If being recorded, it's auto extending, so won't loop
	if (false && beingRecordedFromClip) {
		return 2147483647;

		// Or, normal case
	}
	else {
		return loopLength;
	}
}

bool Clip::isPlayingAutomationNow() const {
	return (currentSong->isClipActive(this)
	        || (beingRecordedFromClip && currentSong->isClipActive(beingRecordedFromClip)));
}

bool Clip::backtrackingCouldLoopBackToEnd() const {
	return (repeatCount > 0);
}

int32_t Clip::getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) const {
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
                             uint32_t xZoom, RGB* image, uint8_t occupancyMask[], bool addUndefinedArea,
                             int32_t noteRowIndexStart, int32_t noteRowIndexEnd, int32_t xStart, int32_t xEnd,
                             bool allowBlur, bool drawRepeats) {

	std::fill(&image[xStart], &image[xStart] + (xEnd - xStart), gui::colours::black);
	if (occupancyMask) {
		memset(&occupancyMask[xStart], 0, (xEnd - xStart));
	}

	return true;
}

void Clip::writeToFile(Serializer& writer, Song* song) {

	char const* xmlTag = getXMLTag();

	writer.writeOpeningTagBeginning(xmlTag, true);

	writeDataToFile(writer, song);

	Clip::writeMidiCommandsToFile(writer, song);

	writer.writeClosingTag(xmlTag, true, true);
}

void Clip::writeDataToFile(Serializer& writer, Song* song) {

	writer.writeAttribute("isPlaying", activeIfNoSolo);
	writer.writeAttribute("isSoloing", soloingInSessionMode);
	writer.writeAttribute("isArmedForRecording", armedForRecording);
	writer.writeAttribute("length", loopLength);
	if (sequenceDirectionMode != SequenceDirection::FORWARD) {
		writer.writeAttribute("sequenceDirection", sequenceDirectionModeToString(sequenceDirectionMode));
	}
	writer.writeAttribute("colourOffset", colourOffset);
	if (section != 255) {
		writer.writeAttribute("section", section);
	}

	// writer.writeTag("activeModFunction", modKnobMode);

	if (getCurrentClip() == this) {
		if (getRootUI()->toClipMinder()) {
			writer.writeAttribute("beingEdited", 1);
		}
		else {
			writer.writeAttribute("selected", 1);
		}
	}
	if (song->getSyncScalingClip() == this) {
		writer.writeAttribute("isSyncScaleClip", 1);
	}
	if (launchStyle != LaunchStyle::DEFAULT) {
		writer.writeAttribute("launchStyle", launchStyleToString(launchStyle));
	}
}

void Clip::writeMidiCommandsToFile(Serializer& writer, Song* song) {
	muteMIDICommand.writeNoteToFile(writer, "muteMidiCommand");
}

void Clip::readTagFromFile(Deserializer& reader, char const* tagName, Song* song, int32_t* readAutomationUpToPos) {

	if (!strcmp(tagName, "isPlaying")) {
		activeIfNoSolo = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isSoloing")) {
		soloingInSessionMode = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "isArmedForRecording")) {
		armedForRecording = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "status")) { // For backwards compatibility
		soloingInSessionMode = false;
		int32_t newStatus = reader.readTagOrAttributeValueInt();
		activeIfNoSolo = (newStatus == 2);
	}

	else if (!strcmp(tagName, "section")) {
		section = reader.readTagOrAttributeValueInt();
		section = std::min(section, (uint8_t)(kMaxNumSections - 1));
	}

	else if (!strcmp(tagName, "trackLength") || !strcmp(tagName, "length")) {
		loopLength = reader.readTagOrAttributeValueInt();
		loopLength = std::max((int32_t)1, loopLength);
		originalLength = loopLength; // it's 0 otherwise, which is fine except for audio clips
		*readAutomationUpToPos = loopLength;
	}

	else if (!strcmp(tagName, "colourOffset")) {
		colourOffset = reader.readTagOrAttributeValueInt();
	}

	else if (!strcmp(tagName, "beingEdited")) {
		if (reader.readTagOrAttributeValueInt()) {
			song->setCurrentClip(this);
			song->inClipMinderViewOnLoad = true;
		}
	}

	else if (!strcmp(tagName, "selected")) {
		if (reader.readTagOrAttributeValueInt()) {
			song->setCurrentClip(this);
			song->inClipMinderViewOnLoad = false;
		}
	}

	else if (!strcmp(tagName, "isSyncScaleTrack") || !strcmp(tagName, "isSyncScaleClip")) {
		bool is = reader.readTagOrAttributeValueInt();

		// This is naughty - inputTickScaleClip shouldn't be accessed directly. But for simplicity, I'm using it to hold
		// this Clip for now, and then in song.cpp this gets made right in a moment...
		if (is) {
			song->syncScalingClip = this;
		}
	}

	else if (!strcmp(tagName, "muteMidiCommand")) {
		muteMIDICommand.readNoteFromFile(reader);
	}

	else if (!strcmp(tagName, "sequenceDirection")) {
		sequenceDirectionMode = stringToSequenceDirectionMode(reader.readTagOrAttributeValue());
	}

	else if (!strcmp(tagName, "launchStyle")) {
		launchStyle = stringToLaunchStyle(reader.readTagOrAttributeValue());
	}

	/*
	else if (!strcmp(tagName, "activeModFunction")) {
	    //modKnobMode = stringToInt(reader.readTagContents());
	    //modKnobMode = std::min(modKnobMode, (uint8_t)(NUM_MOD_BUTTONS - 1));
	}
	*/
}

void Clip::prepareForDestruction(ModelStackWithTimelineCounter* modelStack,
                                 InstrumentRemoval instrumentRemovalInstruction) {

	Output* oldOutput =
	    output; // There won't be an Instrument if the song is being deleted because it wasn't completely loaded

	modelStack->song->deleteBackedUpParamManagersForClip(this);

	if (output) {

		if (isActiveOnOutput() && playbackHandler.isEitherClockActive()) {
			expectNoFurtherTicks(modelStack->song); // Still necessary? Actually maybe... I can see that this would at
			                                        // least cause an AudioClip to abortRecording()...
		}

		detachFromOutput(modelStack, false);
	}

	if (oldOutput) { // One case where there won't be an Output is if the song is being deleted because it wasn't able
		             // to be completely loaded

		if (instrumentRemovalInstruction == InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED) {
			modelStack->song->deleteOrHibernateOutputIfNoClips(oldOutput);
		}

		else if (instrumentRemovalInstruction == InstrumentRemoval::DELETE) {
			modelStack->song->deleteOutputThatIsInMainList(oldOutput);
		}
	}
}

// Virtual function, extended by InstrumentClip.
void Clip::posReachedEnd(ModelStackWithTimelineCounter* modelStack) {
	// If linear recording (which means it must be a loop / session playback if we reached the end)
	if (getCurrentlyRecordingLinearly()) {

		// If they exited recording mode (as in the illuminated RECORD button), don't auto extend
		if (playbackHandler.recording == RecordingMode::OFF) {
			finishLinearRecording(modelStack);
		}

		// Otherwise, do auto extend
		else {
			int32_t oldLength = loopLength;

			loopLength += originalLength;

			sessionView.clipNeedsReRendering(this);

			// For InstrumentClips only, we record and make undoable the length-change here. For AudioClips, on the
			// other hand, it happens in one go at the end of the recording - because for those, if recording is aborted
			// part-way, the whole Clip is deleted. But, don't do this if this Clips still would get deleted as an
			// "abandoned overdub" (meaning it has no notes), cos if that happened, we definitely don't want to have a
			// consequence - pointer pointing to it!
			if (true || type != ClipType::AUDIO) {
				D_PRINTLN("getting new action");
				Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
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
				int32_t extraLengthsDone = (uint32_t)lastProcessedPos / (uint32_t)loopLength;
				lastProcessedPos -= loopLength * extraLengthsDone;
				repeatCount += extraLengthsDone;
			}
			expectEvent();
		}
	}
}

// occupancyMask now optional
void Clip::drawUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay, RGB* rowImage,
                             uint8_t occupancyMask[], int32_t imageWidth, TimelineView* timelineView,
                             bool tripletsOnHere) {
	// If the visible pane extends beyond the end of the Clip, draw it as grey
	int32_t greyStart = timelineView->getSquareFromPos(lengthToDisplay - 1, NULL, xScroll, xZoom) + 1;

	if (greyStart < 0) {
		greyStart = 0; // This actually happened in a song of Marek's, due to another bug, but best to check for this
	}

	if (greyStart < imageWidth) {
		std::fill(rowImage + greyStart, rowImage + greyStart + (imageWidth - greyStart), deluge::gui::colours::grey);
		if (occupancyMask) {
			memset(occupancyMask + greyStart, 64, imageWidth - greyStart);
		}
	}

	if (tripletsOnHere && timelineView->supportsTriplets()) {
		for (int32_t xDisplay = 0; xDisplay < imageWidth; xDisplay++) {
			if (!timelineView->isSquareDefined(xDisplay, xScroll, xZoom)) {
				rowImage[xDisplay] = deluge::gui::colours::grey;

				if (occupancyMask) {
					occupancyMask[xDisplay] = 64;
				}
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
		// In any case, we want the newInstrument to have an activeClip, and if it doesn't yet have one, the supplied
		// Clip makes a perfect candidate
		if (!newOutput->getActiveClip()) {
			goto yesMakeItActive;
		}
	}
}

// Obviously don't call this for MIDI clips!
Error Clip::solicitParamManager(Song* song, ParamManager* newParamManager, Clip* favourClipForCloningParamManager) {

	// Occasionally, like for AudioClips changing their Output, they will actually have a paramManager already, so
	// everything's fine and we can return
	if (paramManager.containsAnyMainParamCollections()) {
		return Error::NONE;
	}

	if (newParamManager) {
		paramManager.stealParamCollectionsFrom(newParamManager, true);
	}

	if (!paramManager.containsAnyMainParamCollections()) {

		ModControllable* modControllable = output->toModControllable();

		// If they're offering a Clip to just clone the ParamManager from...
		if (favourClipForCloningParamManager) {

			// Let's first just see if there already was a *perfect* backed-up one for this *exact* Clip, that we could
			// just have. If so, great, we're done.
			if (song->getBackedUpParamManagerForExactClip((ModControllableAudio*)modControllable, this,
			                                              &paramManager)) {
trimFoundParamManager:
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, modControllable, this,
				                                                   &paramManager);
				paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL,
				                          false); // oldLength actually has no consequence anyway
				return Error::NONE;
			}

			// Ok, still here, let's do that cloning
			paramManager.cloneParamCollectionsFrom(&favourClipForCloningParamManager->paramManager, false, true);
			// That might not work if there was insufficient RAM - very unlikely - but we'll still try the other options
			// below
		}

		// If there wasn't one...
		if (!paramManager.containsAnyMainParamCollections()) {

			bool success = song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)modControllable, this,
			                                                               &paramManager);

			if (success) {
				goto trimFoundParamManager;
			}

			// Still no ParamManager, so copy it from another Clip
			Clip* otherClip = song->getClipWithOutput(output, false, this); // Exclude self
			if (otherClip) {

				Error error = paramManager.cloneParamCollectionsFrom(&otherClip->paramManager, false, true);

				if (error != Error::NONE) {
					FREEZE_WITH_ERROR("E050");
					return error;
				}
			}
			// Unless I've done something wrong, there *has* to be another Clip if the Output didn't have a backed-up
			// ParamManager. But, just in case
			else {
				FREEZE_WITH_ERROR("E051");
				return Error::UNSPECIFIED;
			}
		}
	}

	return Error::NONE;
}

void Clip::clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
                 bool clearSequenceAndMPE) {
	// the following code iterates through all param collections and clears automation and MPE separately
	// automation only gets cleared if clearAutomation is true
	// MPE only gets cleared if clearSequenceAndMPE is true
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager.summaries;

		int32_t i = 0;

		while (summary->paramCollection) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollection(summary->paramCollection, summary);

			// Special case for MPE only - not even "mono" / Clip-level expression.
			if (i == paramManager.getExpressionParamSetOffset()) {
				if (clearSequenceAndMPE) {
					((ExpressionParamSet*)summary->paramCollection)
					    ->deleteAllAutomation(action, modelStackWithParamCollection);
				}
			}

			// Normal case (non MPE automation)
			else {
				if (clearAutomation) {
					summary->paramCollection->deleteAllAutomation(action, modelStackWithParamCollection);
				}
			}
			summary++;
			i++;
		}
	}
}

Error Clip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) {

	// if we're not in a clip level view, set to the clip that's starting linear recording
	// todo: this should probably only happen if a single clip is recording linearly, but that's not tracked
	if (!getRootUI() || !getRootUI()->toClipMinder()) {
		modelStack->song->setCurrentClip(this);
	}
	return Error::NONE;
}

bool Clip::wantsToBeginLinearRecording(Song* song) {
	return (armedForRecording && song->syncScalingClip != this);
}

void Clip::setSequenceDirectionMode(ModelStackWithTimelineCounter* modelStack, SequenceDirection newMode) {
	bool reversedBefore = currentlyPlayingReversed;
	sequenceDirectionMode = newMode;

	if (newMode != SequenceDirection::PINGPONG) {
		currentlyPlayingReversed = (newMode == SequenceDirection::REVERSE);

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

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT && playbackHandler.isEitherClockActive()
	    && !isArrangementOnlyClip() && modelStack->song->isClipActive(this)) {

		if (output->getActiveClip() && output->getActiveClip()->beingRecordedFromClip == this) {
			modelStack->setTimelineCounter(output->getActiveClip());
		}

		else {

			if (!modelStack->song->arrangementOnlyClips.ensureEnoughSpaceAllocated(1)) {
				return false;
			}

			// Find the ClipInstance which we expect to have already been created
			int32_t clipInstanceI =
			    output->clipInstances.search(playbackHandler.getActualArrangementRecordPos() + 1, LESS);

			// If it can't be found (should be impossible), we'll just get out and leave everything the same, so at
			// least nothing will crash
			if (clipInstanceI < 0) {
				return false;
			}

			ClipInstance* clipInstance = output->clipInstances.getElement(clipInstanceI);

			if (type == ClipType::AUDIO) {

				// So, we want to create a bunch of repeats. Often there'll be many at the start which just repeat with
				// untouched params, so that can all be one ClipInstance

				if (repeatCount >= 1) {

					int32_t oldClipInstancePos = clipInstance->pos;

					clipInstance->length = repeatCount * loopLength;

					// And then we'll need a new ClipInstance for this new instance that we're gonna record some
					// automation on
					clipInstanceI++;

					Error error = output->clipInstances.insertAtIndex(clipInstanceI);
					if (error != Error::NONE) {
						return false;
					}

					clipInstance = output->clipInstances.getElement(clipInstanceI);

					clipInstance->pos = oldClipInstancePos + repeatCount * loopLength;
				}
			}

			Error error = clone(modelStack, true); // Puts the cloned Clip into the modelStack. Flattens reversing.
			if (error != Error::NONE) {
				return false;
			}

			Clip* newClip = (Clip*)modelStack->getTimelineCounter();

			newClip->section = 255;

			int32_t newLength = loopLength;

			if (type == ClipType::INSTRUMENT) {
				newLength *= (repeatCount + 1);
				// Yes, call this even if length is staying the same,  because there might be shorter NoteRows.
				newClip->increaseLengthWithRepeats(modelStack, newLength, IndependentNoteRowLengthIncrease::ROUND_UP,
				                                   true);
			}

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
				if (newPlayPos < 0) {
					newPlayPos += loopLength;
				}
			}
			if (type == ClipType::INSTRUMENT) {
				newPlayPos += repeatCount * loopLength;
			}
			newClip->setPos(modelStack, newPlayPos, true);
			newClip->resumePlayback(modelStack, false); // Don't sound

			if (type == ClipType::AUDIO) {
				((AudioClip*)newClip)->voiceSample = ((AudioClip*)this)->voiceSample;
				((AudioClip*)this)->voiceSample = NULL;
			}

			newClip->activeIfNoSolo = false; // And now, we want it to actually be false
			newClip->beingRecordedFromClip = this;
			output->setActiveClip(modelStack, PgmChangeSend::NEVER);
		}

		return true;
	}

	return false;
}

void Clip::incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks) {
	if (currentlyPlayingReversed) {
		numTicks = -numTicks;
	}
	lastProcessedPos += numTicks;
}
void Clip::setupOverdubInPlace(OverDubType type) {
	originalLength = loopLength;
	armState = ArmState::ON_TO_RECORD;
	// This is used to indicate a cloned overdub clip that doesn't have anything in it, not overdub in place
	isPendingOverdub = false;
	overdubNature = type;
}
