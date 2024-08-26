/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/automation/auto_param.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/matrix/matrix_driver.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_node.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <math.h>

#define SAMPLES_TO_CLEAR_AFTER_RECORD 8820          // 200ms
#define SAMPLES_TO_IGNORE_AFTER_BEGIN_OVERRIDE 9200 // 200ms + a bit
#define TIME_TO_INTERPOLATE_WITHIN 6615             // 150ms

// These refer to how many samples to wait *after* the override length, which is specified above.
// And these only apply if we hit a non-interpolated node (which, remember, is all of them for MIDI CCs
#define UNINTERPOLATED_NODE_CANCELS_OVERRIDING_AFTER_SAMPLES                                                           \
	6630 // 150ms. Only seems to have an effect for MIDI, which is confusing me...

AutoParam::AutoParam() {
	init();
	currentValue = 0;
	valueIncrementPerHalfTick = 0;
	renewedOverridingAtTime = 0;
}

void AutoParam::init() {
	nodes.init();
}

void AutoParam::cloneFrom(AutoParam* otherParam, bool copyAutomation) {
	if (copyAutomation) {
		nodes.cloneFrom(&otherParam->nodes);
	}
	else {
		nodes.init();
	}
	currentValue = otherParam->currentValue;

	renewedOverridingAtTime = 0;
}

void AutoParam::copyOverridingFrom(AutoParam* otherParam) {
	if (otherParam->renewedOverridingAtTime) {
		renewedOverridingAtTime = otherParam->renewedOverridingAtTime;
		valueIncrementPerHalfTick = 0;
	}
	currentValue = otherParam->currentValue;
}

// This is mostly for "expression" params, which we frequently want to bump back to 0 - often when there is no
// automation, or when playback is stopped.
void AutoParam::setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t value) {
	int32_t oldValue = currentValue;
	currentValue = value;
	bool automatedNow = isAutomated();
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, automatedNow, automatedNow);
}

// Clip not required, but if you don't supply it, you can't record anything.
// You can assume that this will always change the currentValue.
// livePos may be supplied as -1, meaning get it live. Or, you can override this by specifying one.
// The main purpose of mayDeleteNodesInLinearRun is so that it can be prevented from happening for MPE recording - even
// though MPE will normally interpolate for internal synths, we want to preserve the original MIDI as closely as
// possible in case they later switch it from Synth to MIDI output.
void AutoParam::setCurrentValueInResponseToUserInput(int32_t value, ModelStackWithAutoParam const* modelStack,
                                                     bool shouldLogAction, int32_t livePos,
                                                     bool mayDeleteNodesInLinearRun, bool doMPEMode) {
	int32_t oldValue = currentValue;
	bool automatedBefore = isAutomated();
	bool automationChanged = false;
	valueIncrementPerHalfTick = 0;

	bool isPlaying =
	    (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
	     && (!modelStack->timelineCounterIsSet() || modelStack->getTimelineCounter()->isPlayingAutomationNow()));

	if (isPlaying) {

		// If recording...
		if (playbackHandler.recording != RecordingMode::OFF && modelStack->timelineCounterIsSet()
		    && modelStack->getTimelineCounter()->armedForRecording) {

			// If in record mode and shift button held down, delete automation
			if (Buttons::isShiftButtonPressed()) {

				if (isAutomated()) {
					Action* action =
					    actionLogger.getNewAction(ActionType::AUTOMATION_DELETE, ActionAddition::NOT_ALLOWED);
					deleteAutomation(action, modelStack);
					display->displayPopup(
					    deluge::l10n::get(deluge::l10n::String::STRING_FOR_PARAMETER_AUTOMATION_DELETED));
				}
				return;
			}

			Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

			if (livePos == -1) {
				livePos = modelStack->getLivePos();
			}

			// We're going to clear 0.2s of time ahead of the current play pos. Why?
			// 1. While recording, any nodes in that region are going to be ignored anyway.
			// 2. If recording is exited, we want to have a 0.2s transition before going back to the next node
			uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();
			int32_t ticksToClear = (uint16_t)SAMPLES_TO_CLEAR_AFTER_RECORD / timePerInternalTick;

			// If the Clip is too short to meaningfully record anything / not cause an error
			int32_t effectiveLength = modelStack->getLoopLength();
			if (ticksToClear >= effectiveLength) {
				deleteAutomation(NULL, modelStack);
				goto getOut;
			}

			// Ok, now we know we're gonna change stuff, so back up our state
			if (action) {
				action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
			}

			int32_t posAtWhichPlaybackWillCut = modelStack->getPosAtWhichPlaybackWillCut();

			bool reversed = modelStack->isCurrentlyPlayingReversed();

			// Since May 2020, we don't interpolate the start of the region if there was not another node just before
			// it. Perhaps in a perfect world, we'd also consider how different the new value is from the old value, and
			// maybe even insert a note to interpolate from.
			bool shouldInterpolateRegionStart = false;

			if (!reversed
			    && nodes.getNumElements()) { // Yeah turns out we just don't need the result from this if we're
				                             // reversed. RIP the work I put into making this code reverse-compatible.
				int32_t prevNodeI = nodes.search(livePos + (int32_t)reversed, reversed ? GREATER_OR_EQUAL : LESS);
				if (prevNodeI >= 0 && prevNodeI < nodes.getNumElements()) { // If there was a Node before livePos...
investigatePrevNode:
					ParamNode* prevNode = (ParamNode*)nodes.getElementAddress(prevNodeI);
					int32_t ticksAgo = livePos - prevNode->pos;
					if (reversed) {
						ticksAgo = -ticksAgo;
					}
					if (ticksAgo <= 0) {
						ticksAgo += effectiveLength;
					}
					shouldInterpolateRegionStart = (ticksAgo * timePerInternalTick < TIME_TO_INTERPOLATE_WITHIN);
				}
				else { // Or if there was no Node before livePos...

					int32_t timeSinceLoopPoint = livePos;
					if (reversed) {
						timeSinceLoopPoint = effectiveLength - timeSinceLoopPoint;
					}

					// If livePos was close enough to 0 that we need to look at wrapped Nodes back around on the
					// right...
					if (timeSinceLoopPoint * timePerInternalTick < TIME_TO_INTERPOLATE_WITHIN) {
						prevNodeI = reversed ? 0 : (nodes.getNumElements() - 1);
						goto investigatePrevNode;
					}
				}
			}

			int32_t leftI;

			// Special case (though I feel like this could maybe be used more...)
			// - recording MPE (not mono expression) *linearly*. Just insert one node, and that can change the value
			// everywhere from here to the next node - we don't need to preserve the "original" value in any part of
			// that region because it's going to get overridden any time another note is inserted into it, anyway. And
			// we want our value to last as long as possible, for the note's release-tail.
			// might as well do this for when the ticks are longer than 0.2s too
			if (doMPEMode || ticksToClear == 0) {
				leftI = setNodeAtPos(livePos, value, reversed || shouldInterpolateRegionStart);
				if (leftI == -1) {
					goto getOut;
				}
			}

			// Or, normal case.
			else {

				bool shouldInterpolateLeft = reversed || shouldInterpolateRegionStart;

#if ENABLE_SEQUENTIALITY_TESTS
				// drbourbon got, when check was inside homogenizeRegion(). Now trying to work out where that came from.
				// March 2022.
				nodes.testSequentiality("E435");
#endif

				leftI = homogenizeRegion(modelStack, livePos, ticksToClear, value, shouldInterpolateLeft, true,
				                         effectiveLength, reversed, posAtWhichPlaybackWillCut);
				if (leftI == -1) {
					goto getOut;
				}

				if (reversed) {
					int32_t iFurtherRight = leftI + 2;
					if (iFurtherRight >= nodes.getNumElements()) {
						iFurtherRight -= nodes.getNumElements();
					}
					if (iFurtherRight >= nodes.getNumElements()) {
						iFurtherRight -= nodes.getNumElements();
					}
					ParamNode* nodeFurtherRight = nodes.getElement(iFurtherRight);
					nodeFurtherRight->interpolated = true; // Imperfect, but sorta have to.
				}
			}

			// Now that we've definitely left any previous nodes alone, see if they form a linear run and we can delete
			// some of them? We'll just not bother with this if reversed, for now... or ever...
			if (mayDeleteNodesInLinearRun && !reversed
			    && modelStack->paramCollection->mayParamInterpolate(modelStack->paramId)
			    && nodes.getNumElements() >= 3) {
				bool backtrackingCouldLoopBackToEnd =
				    modelStack->getTimelineCounter()
				        ->backtrackingCouldLoopBackToEnd(); // Wait, I can no longer see why this matters...
				int32_t prevI = leftI - 1;
				if (prevI == -1) {
					if (!backtrackingCouldLoopBackToEnd) {
						goto skipThat;
					}
					prevI = nodes.getNumElements() - 1;
				}
				deleteRedundantNodeInLinearRun(prevI, effectiveLength, backtrackingCouldLoopBackToEnd);

skipThat: {}
			}

#if ENABLE_SEQUENTIALITY_TESTS
			nodes.testSequentiality("ffff");
#endif

			if (!doMPEMode) {
				renewedOverridingAtTime = 1; // Latch - until we come to the next node
			}
			automationChanged = true;

#if ALPHA_OR_BETA_VERSION
			if (nodes.getNumElements()) {
				ParamNode* rightmostNode = nodes.getElement(nodes.getNumElements() - 1);
				if (rightmostNode->pos >= effectiveLength) {
					FREEZE_WITH_ERROR("llll");
				}
			}
#endif
		}

		// Or if not recording...
		else {
			if (nodes.getNumElements()) {
				renewedOverridingAtTime = AudioEngine::audioSampleTimer;
				if (renewedOverridingAtTime <= 1) {
					renewedOverridingAtTime = 0xFFFFFFFF;
				}
			}
		}
	}

	// If still unautomated (or not currently playing), record value change
	if (!nodes.getNumElements() || !isPlaying) {
		if (value != currentValue) {
			actionLogger.recordUnautomatedParamChange(modelStack);
		}
	}

getOut:
	currentValue = value;
	bool automatedNow = isAutomated();
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          automatedNow);
}

bool AutoParam::deleteRedundantNodeInLinearRun(int32_t lastNodeInRunI, int32_t effectiveLength,
                                               bool mayLoopAroundBackToEnd) {

	if (nodes.getNumElements() < 3) {
		return false;
	}

	ParamNode* lastNodeInRun = nodes.getElement(lastNodeInRunI);

	// But first, now that we've moved on from prevNode, see if prevNode concluded a linear run of nodes for which we
	// can now delete the middle node
	int32_t middleNodeInRunI = lastNodeInRunI - 1;
	if (middleNodeInRunI == -1) {
		if (!mayLoopAroundBackToEnd) {
			return false;
		}
		middleNodeInRunI = nodes.getNumElements() - 1;
	}
	ParamNode* middleNodeInRun = nodes.getElement(middleNodeInRunI);

	if (lastNodeInRun->interpolated || !middleNodeInRun->interpolated) {

		int32_t firstNodeInRunI = middleNodeInRunI - 1;
		if (firstNodeInRunI == -1) {
			if (!mayLoopAroundBackToEnd) {
				return false;
			}
			firstNodeInRunI = nodes.getNumElements() - 1;
		}
		ParamNode* firstNodeInRun = nodes.getElement(firstNodeInRunI);

		if (middleNodeInRun->value == firstNodeInRun->value
		    && (middleNodeInRun->value == lastNodeInRun->value || !middleNodeInRun->interpolated)) {
removeMiddleNodeInRun:
			nodes.deleteAtIndex(middleNodeInRunI);
			return true;
		}

		else if (middleNodeInRun->interpolated) {

			// float timeFraction = (float)howFarAfter(middleNodeInRun->pos, firstNodeInRun->pos, clip) /
			// howFarAfter(prevNode->pos, firstNodeInRun->pos, clip);
			float valueFraction = (float)((middleNodeInRun->value >> 1) - (firstNodeInRun->value >> 1))
			                      / ((lastNodeInRun->value >> 1) - (firstNodeInRun->value >> 1));

			int32_t distanceFirstToLast = lastNodeInRun->pos - firstNodeInRun->pos;
			if (distanceFirstToLast <= 0) {
				distanceFirstToLast += effectiveLength;
			}

			int32_t distanceFirstToMiddle = middleNodeInRun->pos - firstNodeInRun->pos;
			if (distanceFirstToMiddle <= 0) {
				distanceFirstToMiddle += effectiveLength;
			}

			// If nodes lay in a straight line (approximately)
			if (round(valueFraction * distanceFirstToLast) == distanceFirstToMiddle) {

				goto removeMiddleNodeInRun;
			}
		}
	}
	return false;
}

// action is optional. If you don't supply it, consequences won't be recorded
void AutoParam::deleteAutomation(Action* action, ModelStackWithAutoParam const* modelStack, bool shouldNotify) {

	bool wasAutomated = isAutomated();

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStack, true);
	}

	else {
		nodes.empty();
	}

	valueIncrementPerHalfTick = 0;
	renewedOverridingAtTime = 0;

	if (shouldNotify && wasAutomated) {
		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, getCurrentValue(), true, true, false);
	}
}

// Beware. As this will do no notifying, the caller must ensure that any required notification is done.
// I.e. a ParamSet must be notified if automation is deleted.
void AutoParam::deleteAutomationBasicForSetup() {
	nodes.empty();
	valueIncrementPerHalfTick = 0;
	renewedOverridingAtTime = 0;
}

#define OVERRIDE_DURATION_MAGNITUDE_INTERPOLATING 15 // Means 2^x audio samples in length

int32_t AutoParam::processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed, bool didPinpong,
                                     bool mayInterpolate, bool mustUpdateValueAtEveryNode) {

	// If no automation...
	if (!nodes.getNumElements()) {
		return 2147483647;
	}

	int32_t currentPos = modelStack->getLastProcessedPos();
	int32_t effectiveLength = modelStack->getLoopLength();

	// Find next node - here or further along in our direction
	int32_t searchDirection = -(int32_t)reversed;
	int32_t searchPos = currentPos + (int32_t)reversed;
	int32_t iJustReached = nodes.search(searchPos, searchDirection);
	if (iJustReached < 0) {
		iJustReached += nodes.getNumElements();
	}
	else if (iJustReached >= nodes.getNumElements()) {
		iJustReached = 0;
	}

	ParamNode* nodeJustReached = nodes.getElement(iJustReached);
	int32_t howFarUntilThisNode = nodeJustReached->pos - currentPos;

	// If we haven't reached the next node yet...
	if (howFarUntilThisNode) {
		if (reversed) {
			howFarUntilThisNode =
			    -howFarUntilThisNode; // Adjust for direction. No need to do until we know we're returning.
		}
		if (howFarUntilThisNode < 0) {
			howFarUntilThisNode += effectiveLength;
		}
		return howFarUntilThisNode;
	}

	int32_t valueJustReached = nodeJustReached->value;
	bool noNeedToJumpToValue = nodeJustReached->interpolated && mayInterpolate;

	// Ok, if we're here, we just reached the node!

	/*
	    D_PRINTLN("");
	    D_PRINT("at node: ");
	    D_PRINT(nodeJustReached->pos);
	    D_PRINT(", ");
	    D_PRINT(nodeJustReached->value);
	    if (nodeJustReached->interpolated) D_PRINT(", interp");
	    D_PRINTLN("");
	    if (renewedOverridingAtTime) D_PRINTLN("overriding");
	*/

	// Stop any pre-existing interpolation (though we might set up some more, below)
	valueIncrementPerHalfTick = 0;

	// Now start thinking about the *next* node, which we'll get to in a while
	int32_t iRight = iJustReached + 1;
	if (iRight >= nodes.getNumElements()) {
		iRight = 0;
	}

	ParamNode* nextNodeInOurDirection;

	if (reversed) {
		int32_t iLeft = iJustReached - 1;
		if (iLeft < 0) {
			iLeft += nodes.getNumElements();
		}
		ParamNode* nodeToLeft = nodes.getElement(iLeft);

		if (!noNeedToJumpToValue) {
			valueJustReached = nodeToLeft->value; // At the time of this condition, weInterpolatedHere still means the
			                                      // interpolation to our *left*.
		}
		// noNeedToJumpToValue = noNeedToJumpToValue || nodeToRight->interpolated; 	// If playing reversed, we probably
		// want to jump directly to the value of the node to the left,
		//  unless from the node right here there's a slope left *and* a slope right (cos if there's
		//  no slope right we'll already be at the correct value from before).
		nextNodeInOurDirection = nodeToLeft;
	}
	else {
		ParamNode* nodeToRight = nodes.getElement(iRight);
		nextNodeInOurDirection = nodeToRight;
	}

	// If overriding...
	if (renewedOverridingAtTime) {

		int32_t timeSinceOverrideEnd;
		bool shouldCancelOverridingNow;

		// If latched...
		if (renewedOverridingAtTime == 1) {

			// If recording, we need to actually modify the node we just reached.
			if (playbackHandler.isCurrentlyRecording()) {
				goto recordOverNodeJustReached;
			}

			// If not recording, but we're still latching, well latching is only meant to happen as a result of
			// recording, so they must have just cancelled recording. So, exit out of latching and just give them normal
			// overriding.
			else {
				renewedOverridingAtTime = AudioEngine::audioSampleTimer
				                          - SAMPLES_TO_CLEAR_AFTER_RECORD; // Copied from below. Specifics don't really
				                                                           // matter - this is a rare case
				if (renewedOverridingAtTime <= 1) {
					renewedOverridingAtTime = 0xFFFFFFFF;
				}
				goto getOut; // Nothing else to do. We don't want to even obey any automation just now.
			}
		}

		timeSinceOverrideEnd =
		    AudioEngine::audioSampleTimer - (uint32_t)renewedOverridingAtTime - SAMPLES_TO_IGNORE_AFTER_BEGIN_OVERRIDE;

		// If we overrode (turned the knob) less than 0.2s ago and are not recording, then we don't want to obey any
		// automation at all
		if (timeSinceOverrideEnd <= 0 && !playbackHandler.isCurrentlyRecording()) {
			goto getOut;
		}

		// if (timeSinceOverrideEnd >= UNINTERPOLATED_NODE_CANCELS_OVERRIDING_AFTER_SAMPLES * 2) goto
		// yesCancelOverriding; // Fix I played with in Aug 2021, seems good - I think that if it's been "quite a
		// while", this new node should just cancel overriding no matter what, right?

		// If not interpolating, let it choose to get out of overriding before looking at doing recording (I'm not 100%
		// sure that this was the best way...)
		shouldCancelOverridingNow =
		    mayInterpolate
		        ? !nextNodeInOurDirection->interpolated
		              && timeSinceOverrideEnd >= UNINTERPOLATED_NODE_CANCELS_OVERRIDING_AFTER_SAMPLES
		        : // For non-MIDI params, this actually doesn't appear necessary and overriding instead gets cancelled
		          // like 70 lines below. But I've left it here for safety. This whole function is so complicated...
		        timeSinceOverrideEnd
		            >= UNINTERPOLATED_NODE_CANCELS_OVERRIDING_AFTER_SAMPLES; // Whereas for MIDI CCs, for some reason I
		                                                                     // now can't work out, this is crucial and
		                                                                     // cancelling won't happen until this time
		                                                                     // - but perhaps only for knob-recorded
		                                                                     // automation, not per-step automation?
		                                                                     // God.

		if (shouldCancelOverridingNow) {
yesCancelOverriding:
			renewedOverridingAtTime = 0;
			D_PRINTLN("cancel overriding, basic way");
		}

		// Otherwise...
		else {

			// If recording, modify the node we just reached to resemble the drifting-back-to-the-automation that's
			// happening live, now
			if (playbackHandler.isCurrentlyRecording()) {
recordOverNodeJustReached:

				// Back up state if necessary. It normally would have already been, but not if the user only just
				// activated recording while already overriding!
				Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
				if (action) {
					action->recordParamChangeIfNotAlreadySnapshotted(modelStack);
				}

				int32_t ticksTilNextNode = nextNodeInOurDirection->pos - nodeJustReached->pos;
				if (reversed) {
					ticksTilNextNode = -ticksTilNextNode;
				}
				if (ticksTilNextNode < 0) {
					ticksTilNextNode += effectiveLength;
				}

				// I used to add 3 onto the end of this cos it helped with ensuring a nice drift-back when we didn't
				// have any latching. But now we do so it's unnecessary...
				// ...and also, adding any extra constant on here causes latching to sometimes cancel, because an actual
				// record action homogenizes a region that doesn't include that extra constant bit of length, so can
				// cause a next tick to happen soon enough to cause latching to cancel, just below
				int32_t ticksToClear =
				    (uint16_t)SAMPLES_TO_CLEAR_AFTER_RECORD / playbackHandler.getTimePerInternalTick();

				int32_t posOverridingEnds;
				int32_t valueOverridingEnds;

				// If next node too far away...
				bool newNodeShouldBeInterpolated = true;
				bool insertingNodeAtEndOfClearing = (ticksTilNextNode > ticksToClear);
				if (insertingNodeAtEndOfClearing) {

					// Special case for song params for recording session->arrangement. TODO: think about reversing for
					// this
					if (playbackHandler.recording == RecordingMode::ARRANGEMENT
					    && modelStack->getTimelineCounter() == modelStack->song) {

						// If there's already a node at 0, we don't need to do anything
						if (nodes.search(1, LESS) >= 0) {
							insertingNodeAtEndOfClearing = false; // Set this to false, but that doesn't mean we want to
							                                      // do the "else" condition below, so skip past it
							goto adjustNodeJustReached;
						}

						// Alright, there was no node at 0, so proceed to add one there
						posOverridingEnds = 0;
						newNodeShouldBeInterpolated = false;
					}

					// Or, normal case - we need to insert a node where the overriding ends
					else {
						if (reversed) {
							ticksToClear = -ticksToClear; // From here on, ticksToClear is negative if we're reversing.
							                              // But we only use it one more time.
						}
						posOverridingEnds = nodeJustReached->pos + ticksToClear;
						int32_t posAtWhichClipWillCut = modelStack->getPosAtWhichPlaybackWillCut();

						// If eating past the point where the Clip will cut, just make sure don't put anything past
						// that. And don't interpolate. If recording to arrangement, do this too, and it'll have the
						// reverse effect - extending overriding out to the "end of time" at 2147483647
						if (reversed) {
							if (posOverridingEnds <= posAtWhichClipWillCut) {
								posOverridingEnds = posAtWhichClipWillCut;
								// TODO: could make it not interpolate and set its value to our new value?
							}

							// May need to wrap pos back around to the start.
							if (posOverridingEnds < 0) {
								posOverridingEnds += effectiveLength;
							}
						}

						else {
							if (posOverridingEnds >= posAtWhichClipWillCut) {
								posOverridingEnds = posAtWhichClipWillCut;
								newNodeShouldBeInterpolated = false;
							}

							// May need to wrap pos back around to the start.
							if (posOverridingEnds >= effectiveLength) {
								posOverridingEnds -= effectiveLength;
							}
						}
					}

					valueOverridingEnds = getValueAtPos(posOverridingEnds, modelStack, reversed);
				}

				// Or if next node's actually coming up quite soon, cancel latching if it was on
				else {
					if (renewedOverridingAtTime == 1) {

						// If the upcoming node is non-interpolated, we want no overriding at all so we can jump
						// directly to it
						if (!nextNodeInOurDirection->interpolated) {
							renewedOverridingAtTime = 0;
							// That's how overriding is most often cancelled for non-MIDI params. But for some reason,
							// MIDI CCs, even if we removed their overriding cancellation code above, don't seem
							// affected by this...
						}

						// Or if it is interpolated, we'll just do regular overriding so we can drift into it
						else {
							// Pretend that it began SAMPLES_TO_CLEAR_AFTER_RECORD samples ago - because we had to wait
							// that long to get to this node just now after we recorded a value
							renewedOverridingAtTime = AudioEngine::audioSampleTimer - SAMPLES_TO_CLEAR_AFTER_RECORD;
							if (renewedOverridingAtTime <= 1) {
								renewedOverridingAtTime = 0xFFFFFFFF;
							}
						}
						D_PRINTLN("cancel latching");
					}
				}

adjustNodeJustReached:
				// D_PRINTLN("adjusting node value");
				if (!didPinpong) {
					nodeJustReached->value = currentValue;
				}
				nodeJustReached->interpolated = true;
				// TODO: if reversing, should we set the one to the right to interpolating too?
				noNeedToJumpToValue = true;

				bool needToReGetNextNode = false;

				// Having changed that node's value, there's a chance it may have made the node before it redundant
				if (!reversed) {
					needToReGetNextNode = deleteRedundantNodeInLinearRun(
					    iJustReached, effectiveLength); // Shouldn't I make it so this doesn't get called for MPE?
				}

				if (insertingNodeAtEndOfClearing) {
					int32_t iNew = nodes.insertAtKey(
					    posOverridingEnds); // Can only do this now, after updating nodeJustReached, above
					if (iNew != -1) {
						iRight = iNew;
						nextNodeInOurDirection = nodes.getElement(iRight);
						nextNodeInOurDirection->value = valueOverridingEnds;
						nextNodeInOurDirection->interpolated = newNodeShouldBeInterpolated;

						/*
						D_PRINTLN("new one:  %d ,  %d", posOverridingEnds, valueOverridingEnds);
						*/
						if (!reversed) {
							needToReGetNextNode = deleteRedundantNodeInLinearRun(
							    iRight,
							    effectiveLength); // If returns false, storage wasn't changed, and we've got the next
							                      // node right here! Shouldn't I make it so this doesn't get called for
							                      // MPE?
						}
					}
				}

				// Figure out what's the next node, again - because we just possibly deleted a node, and that's possibly
				// changed the storage
				if (needToReGetNextNode) { // Not doing this if reversed.
					iRight = nodes.search(currentPos + 1, GREATER_OR_EQUAL);
					if (iRight == nodes.getNumElements()) {
						iRight = 0;
					}
					nextNodeInOurDirection = nodes.getElement(iRight);
				}

#if ENABLE_SEQUENTIALITY_TESTS
				nodes.testSequentiality("eeee");
#endif
			}
		}
	}

	// If this node we've just reached wasn't interpolated, and automation is not overridden (which may have only just
	// become the case), we need to jump to the node's value. (Or, it'll be the value of the node to the left if the
	// node here isn't interpolated.)
	if ((!noNeedToJumpToValue || mustUpdateValueAtEveryNode) && !renewedOverridingAtTime) {
		int32_t oldValue = currentValue;
		currentValue = valueJustReached;

		// The call to notifyParamModifiedInSomeWay() below normally has the ability to delete this AutoParam, which we
		// want it not to. It won't if we still contain automation, which I think we have to... Let's just verify that.
#if ALPHA_OR_BETA_VERSION
		if (!isAutomated()) {
			FREEZE_WITH_ERROR("E372");
		}
#endif
		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, true, true);
	}

	if (mayInterpolate) {
		if ((reversed ? nodeJustReached : nextNodeInOurDirection)->interpolated) {
			setupInterpolation(nextNodeInOurDirection, effectiveLength, currentPos, reversed);
		}
	}

getOut:
	int32_t ticksTilNextNode = nextNodeInOurDirection->pos - currentPos;
	if (reversed) {
		ticksTilNextNode = -ticksTilNextNode;
	}
	if (ticksTilNextNode <= 0) {
		ticksTilNextNode += effectiveLength;
	}

	// Ok, no node should be at or past the effectiveLength. Sometimes somehow this is still happening - see
	// https://forums.synthstrom.com/discussion/4499/v4-0-0-beta8-freeze-while-recording-long-mpe-clips-jjjj I'm so
	// sorry, but I'm going to just make it manually fix itself, here.
	if (nodes.getNumElements()) {
		int32_t i = nodes.getNumElements() - 1;
		ParamNode* rightmostNode = nodes.getElement(i);
		if (rightmostNode->pos >= effectiveLength) {
			nodes.deleteAtIndex(i);
			// FREEZE_WITH_ERROR("jjjj"); // drbourbon got! And Quixotic7, on V4.0.0-beta8.
		}
	}

	return ticksTilNextNode;
}

// You now much check before calling this that interpolation should happen at all
void AutoParam::setupInterpolation(ParamNode* nextNodeInOurDirection, int32_t effectiveLength, int32_t currentPos,
                                   bool reversed) {

	if (renewedOverridingAtTime == 1) {
		return; // If it's latched-until-next-node-hit, we're not allowed to interpolate.
	}

	int32_t halfDistance = (nextNodeInOurDirection->value >> 1) - (currentValue >> 1);

	if (!halfDistance) {
		return;
	}

	int32_t ticksTilNextNode = nextNodeInOurDirection->pos - currentPos;
	if (reversed) {
		ticksTilNextNode = -ticksTilNextNode;
	}

	if (ticksTilNextNode <= 0) {
		ticksTilNextNode += effectiveLength;
	}

	valueIncrementPerHalfTick = halfDistance / ticksTilNextNode;

	// If automation still overridden (at least to some extent), limit how fast interpolation can occur
	if (renewedOverridingAtTime) {

		int32_t timeSinceOverridden =
		    AudioEngine::audioSampleTimer - (uint32_t)renewedOverridingAtTime - SAMPLES_TO_IGNORE_AFTER_BEGIN_OVERRIDE;

		// If overriding was renewed aaages ago, we can just stop that.
		if (timeSinceOverridden >= (1 << OVERRIDE_DURATION_MAGNITUDE_INTERPOLATING)) {
			renewedOverridingAtTime = 0;
		}

		// Or if still going...
		else {
			timeSinceOverridden = std::max(timeSinceOverridden, (int32_t)0);

			int32_t limit = timeSinceOverridden << (26 - OVERRIDE_DURATION_MAGNITUDE_INTERPOLATING);
			if (valueIncrementPerHalfTick > limit) {
				valueIncrementPerHalfTick = limit;
			}
			else if (valueIncrementPerHalfTick < -limit) {
				valueIncrementPerHalfTick = -limit;

				// If we didn't even have to limit it, there's no need to be overriding anymore
			}
			else {
				renewedOverridingAtTime = 0;
			}
		}
	}
}

bool AutoParam::tickSamples(int32_t numSamples) {
	if (!valueIncrementPerHalfTick) {
		return false;
	}

	int32_t oldValue = currentValue;
	currentValue +=
	    multiply_32x32_rshift32_rounded(valueIncrementPerHalfTick, playbackHandler.getTimePerInternalTickInverse()) * 6
	    * numSamples;

	// Ensure no overflow
	bool overflowOccurred = (valueIncrementPerHalfTick >= 0) ? (currentValue < oldValue) : (currentValue > oldValue);
	if (overflowOccurred) {
		currentValue = (valueIncrementPerHalfTick >= 0) ? 2147483647 : -2147483648;
		valueIncrementPerHalfTick = 0;
	}

	return true;
}

bool AutoParam::tickTicks(int32_t numTicks) {
	if (valueIncrementPerHalfTick == 0) {
		return false;
	}

	currentValue = add_saturation(currentValue, valueIncrementPerHalfTick * numTicks * 2);

	return true;
}

void AutoParam::setValuePossiblyForRegion(int32_t value, ModelStackWithAutoParam const* modelStack, int32_t pos,
                                          int32_t length, bool mayDeleteNodesInLinearRun) {
	if (length && modelStack->timelineCounterIsSet()) {
		setValueForRegion(pos, length, value, modelStack);
	}
	else {
		setCurrentValueInResponseToUserInput(value, modelStack, true, -1, mayDeleteNodesInLinearRun);
	}
}

// For MPE when a note gets deleted, and we want to just simply delete nodes and let previous ones' value spill into
// this area.
void AutoParam::deleteNodesWithinRegion(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t length) {
	if (!isAutomated()) {
		return;
	}

	int32_t oldValue = currentValue;

	int32_t effectiveLength = modelStack->getLoopLength();

	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);

	if (length >= effectiveLength) {
		deleteAutomation(action, modelStack);
	}
	else {

		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
		}

		bool wrapping;
		int32_t resultingIndexes[2];

		{
			int32_t searchTerms[2];
			searchTerms[0] = pos;
			searchTerms[1] = pos + length;
			wrapping = (searchTerms[1] >= effectiveLength);
			if (wrapping) {
				searchTerms[0] = searchTerms[1] - effectiveLength;
				searchTerms[1] = pos;
			}

			nodes.searchDual(searchTerms, resultingIndexes);
		}

		if (wrapping) {
			if (resultingIndexes[0]) {
				nodes.deleteAtIndex(0, resultingIndexes[0]);
			}
			int32_t numAtEnd = nodes.getNumElements() - resultingIndexes[1];
			if (numAtEnd) {
				nodes.deleteAtIndex(resultingIndexes[1], numAtEnd);
			}
		}

		else {
			int32_t numToDelete = resultingIndexes[1] - resultingIndexes[0];
			if (numToDelete) {
				nodes.deleteAtIndex(resultingIndexes[0], numToDelete);
			}
		}

		if (!isAutomated()) {
			currentValue = 0; // For safety, with MPE. Actually very necessary.
		}
	}

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, true, true, isAutomated());
}

int32_t AutoParam::setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate) {
	int32_t i = nodes.search(pos, GREATER_OR_EQUAL);
	ParamNode* ourNode;

	// Check there's not already a node there
	if (i < nodes.getNumElements()) {
		ourNode = nodes.getElement(i);
		if (ourNode->pos == pos) {
			goto setupNode;
		}
	}

	{
		Error error = nodes.insertAtIndex(i);
		if (error != Error::NONE) {
			return -1;
		}
	}
	ourNode = nodes.getElement(i);
	ourNode->pos = pos;
setupNode:
	ourNode->value = value;
	ourNode->interpolated = shouldInterpolate;

	return i;
}

void AutoParam::setValueForRegion(uint32_t pos, uint32_t length, int32_t value,
                                  ModelStackWithAutoParam const* modelStack, ActionType actionType) {

	int32_t oldValue = currentValue;
	bool automatedBefore = isAutomated();
	bool automationChanged = false;

	int32_t firstI, mostRecentI;

	int32_t effectiveLength = modelStack->getLoopLength();

	// If the user is holding down a pad for an extended NoteRow, which is beyond the length of the Clip, and they're
	// trying to edit this Param for the Clip, well that can't happen because they're then trying to edit beyond the
	// length that this automation may exist within.
	if (pos >= effectiveLength) {
		return;
	}

	Action* action = actionLogger.getNewAction(actionType, ActionAddition::ALLOWED);

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStack);
	}

	// First, special case if our region covers the whole NoteRow / Clip / TimelineCounter
	if (length == effectiveLength) {
		if (isAutomated()) {
			deleteAutomation(action, modelStack);
		}
		else {
			if (action) {
				action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
			}
		}
		currentValue = value;
	}

	// Or, normal case
	else {

#if ENABLE_SEQUENTIALITY_TESTS
		// drbourbon got, when check was inside homogenizeRegion(). Now trying to work out where that came from.
		// March 2022. Sven got, oddly while editing note velocity. Then again by "Adding some snares while playing".
		nodes.testSequentiality("E441");
#endif

		// automation interpolation
		// when this feature is enabled, interpolation is enforced on manual automation editing in the automation
		// instrument clip view

		if (getRootUI() == &automationView) {
			firstI = homogenizeRegion(modelStack, pos, length, value, automationView.interpolationBefore,
			                          automationView.interpolationAfter, effectiveLength, false);
		}
		else {
			firstI = homogenizeRegion(modelStack, pos, length, value, false, false, effectiveLength, false);
		}

		if (firstI == -1) {
			return;
		}

		automationChanged = true;

		if (!playbackHandler.isEitherClockActive()) {
			goto yesChangeCurrentValue;
		}

		// If we're in the region right now...
		mostRecentI = nodes.search(modelStack->getLivePos() + !modelStack->isCurrentlyPlayingReversed(), LESS);
		if (mostRecentI == -1) {
			mostRecentI = nodes.getNumElements() - 1;
		}
		if (mostRecentI == firstI) {
			valueIncrementPerHalfTick = 0;
yesChangeCurrentValue:
			currentValue = value;
		}
		else {
			view.notifyParamAutomationOccurred(modelStack->paramManager);
		}
	}

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          isAutomated());
}

#define REGION_EDGE_LEFT 0
#define REGION_EDGE_RIGHT 1

// Returns index of leftmost node of region, or -1 if error
int32_t AutoParam::homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos, int32_t length,
                                    int32_t startValue, bool interpolateLeftNode, bool interpolateRightNode,
                                    int32_t effectiveLength, bool reversed, int32_t posAtWhichClipWillCut) {

#if ALPHA_OR_BETA_VERSION
	// Chasing "E433" / "GGGG" error (probably now largely solved - except got E435, see below).
	if (length <= 0) {
		FREEZE_WITH_ERROR("E427");
	}
	if (startPos < 0) {
		FREEZE_WITH_ERROR("E437");
	}

	// #if ENABLE_SEQUENTIALITY_TESTS
	//	// nodes.testSequentiality("E435"); // drbourbon got! March 2022. Now moved check to each caller.
	// #endif

	if (nodes.getNumElements() && nodes.getFirst()->pos < 0) {
		FREEZE_WITH_ERROR("E436");
	}
	// Should probably also check that stuff doesn't exist too far right - but that's a bit more complicated.
#endif

	int32_t edgePositions[2];
	bool anyWrap;

	// Playing forwards...
	if (!reversed) {
		edgePositions[REGION_EDGE_LEFT] = startPos;

		// First, limit the length if we're coming up to a cut-point.
		int32_t maxLength = posAtWhichClipWillCut - edgePositions[REGION_EDGE_LEFT];
		if (length >= maxLength) {
			length = maxLength;
#if ALPHA_OR_BETA_VERSION
			if (length <= 0) {
				FREEZE_WITH_ERROR("E428"); // Chasing Leo's GGGG error (probably now solved).
			}
#endif
			interpolateRightNode = false;
			edgePositions[REGION_EDGE_RIGHT] = posAtWhichClipWillCut;
			anyWrap = (edgePositions[REGION_EDGE_RIGHT] >= effectiveLength);
			if (anyWrap) {
				edgePositions[REGION_EDGE_RIGHT] =
				    0; // Gotta wrap it - we're not allowed a node right at e.g. the Clip length point.
			}
		}

		// Or if we didn't do that, there could be a loop-point, which we treat almost the same - except we don't let it
		// limit our region length - we just wrap our region end around.
		else {
			edgePositions[REGION_EDGE_RIGHT] = edgePositions[REGION_EDGE_LEFT] + length;
			anyWrap = (edgePositions[REGION_EDGE_RIGHT] >= effectiveLength);
			if (anyWrap) {
				// But wait - if we're linearly recording, then it's not so much a "loop" point, because the Clip will
				// extend. So ensure we don't wrap back to the start, past the current loopLength.
				if (((Clip*)modelStack->getTimelineCounter())->getCurrentlyRecordingLinearly()) {
					length = effectiveLength - edgePositions[REGION_EDGE_LEFT];
					edgePositions[REGION_EDGE_RIGHT] = 0;
				}
				else {
					edgePositions[REGION_EDGE_RIGHT] -= effectiveLength;
				}
			}
		}
	}

	// Or, playing reversed...
	else {
		if constexpr (ALPHA_OR_BETA_VERSION) {
			if (startPos < posAtWhichClipWillCut) {
				FREEZE_WITH_ERROR("E445");
			}
		}
		edgePositions[REGION_EDGE_RIGHT] = startPos;
		edgePositions[REGION_EDGE_LEFT] = edgePositions[REGION_EDGE_RIGHT] - length;

		// First, limit the length if we're coming up to a cut-point.
		if (edgePositions[REGION_EDGE_LEFT] < posAtWhichClipWillCut) {
			edgePositions[REGION_EDGE_LEFT] = posAtWhichClipWillCut;
			length = edgePositions[REGION_EDGE_RIGHT] - edgePositions[REGION_EDGE_LEFT];
			if constexpr (ALPHA_OR_BETA_VERSION) {
				if (edgePositions[REGION_EDGE_LEFT] >= edgePositions[REGION_EDGE_RIGHT]) {
					FREEZE_WITH_ERROR("HHHH");
				}
			}

			interpolateLeftNode = false; // Maybe not really perfect
			anyWrap = false;
		}

		// Or if we didn't do that, there could be a loop-point, which we treat almost the same - except we don't let it
		// limit our region length - we just wrap our region end around.
		else {
			anyWrap = (edgePositions[REGION_EDGE_LEFT] < 0);
			if (anyWrap) {
				edgePositions[REGION_EDGE_LEFT] += effectiveLength;
			}
		}
	}

	if (anyWrap) { // Temporarily swap edge positions
		int32_t temp = edgePositions[0];
		edgePositions[0] = edgePositions[1];
		edgePositions[1] = temp;
	}

	int32_t edgeIndexes[2];
	nodes.searchDual(edgePositions, edgeIndexes);

	if (anyWrap) { // Swap edge positions and indexes back
		int32_t temp = edgeIndexes[0];
		edgeIndexes[0] = edgeIndexes[1];
		edgeIndexes[1] = temp;

		temp = edgePositions[0];
		edgePositions[0] = edgePositions[1];
		edgePositions[1] = temp;
	}
	// Ok, edgeIndexes and edgePositions are now ordered so as to be accessible with REGION_EDGE_LEFT (0) and
	// REGION_EDGE_RIGHT (1)

	ParamNode* edgeNodes[2];
	edgeNodes[REGION_EDGE_LEFT] = NULL;
	edgeNodes[REGION_EDGE_RIGHT] = NULL;

	for (int32_t i = 0; i < 2; i++) {
		if (edgeIndexes[i] < nodes.getNumElements()) {
			ParamNode* potentialEdgeNode = nodes.getElement(edgeIndexes[i]);
			if (potentialEdgeNode->pos == edgePositions[i]) {
				edgeNodes[i] = potentialEdgeNode;
			}
		}
	}

	int32_t valueAtLateEdge;
	if (edgeNodes[!reversed]) {
		if (reversed && !edgeNodes[!reversed]->interpolated) {
			goto getValueNormalWay; // If reversed, and the node isn't interpolated, we'd actually need the value of the
			                        // further-left node.
		}

		valueAtLateEdge = edgeNodes[!reversed]->value;
	}
	else {
getValueNormalWay:
		valueAtLateEdge = getValueAtPos(edgePositions[!reversed], modelStack, reversed);
	}

	// Sort out rightmost node
	if (!edgeNodes[REGION_EDGE_RIGHT]) { // If we don't have a rightmost node yet...

		// If there's a further-left node we can just grab and repurpose...
		if (edgeIndexes[REGION_EDGE_RIGHT]
		    && (anyWrap || edgeIndexes[REGION_EDGE_RIGHT] > edgeIndexes[REGION_EDGE_LEFT] + 1)) {

			edgeIndexes[REGION_EDGE_RIGHT]--;
		}

		// Otherwise, insert one
		else {
			Error error = nodes.insertAtIndex(edgeIndexes[REGION_EDGE_RIGHT]);
			if (error != Error::NONE) {
				return -1;
			}
			edgeIndexes[REGION_EDGE_LEFT] += (int32_t)anyWrap;
			// Theoretically we'd re-get the other edgeNode here - but in fact, if it already existed, we won't access
			// it again anyway.
		}

		edgeNodes[REGION_EDGE_RIGHT] = nodes.getElement(edgeIndexes[REGION_EDGE_RIGHT]);
		edgeNodes[REGION_EDGE_RIGHT]->pos = edgePositions[REGION_EDGE_RIGHT];
	}
	edgeNodes[REGION_EDGE_RIGHT]->value = reversed ? startValue : valueAtLateEdge;
	edgeNodes[REGION_EDGE_RIGHT]->interpolated = interpolateRightNode;

	// And sort out leftmost node
	if (!edgeNodes[REGION_EDGE_LEFT]) { // If we don't have a leftmost node yet...

		// If there's a further-right node we can just grab and repurpose...
		if (edgeIndexes[REGION_EDGE_RIGHT] != edgeIndexes[REGION_EDGE_LEFT]
		    && edgeIndexes[REGION_EDGE_LEFT] < nodes.getNumElements()) {
			// Cool, we can just update the "pos" of that node.
		}

		// Otherwise, insert one
		else {
			Error error = nodes.insertAtIndex(edgeIndexes[REGION_EDGE_LEFT]);
			if (error != Error::NONE) {
				return -1;
			}
			edgeIndexes[REGION_EDGE_RIGHT] += (int32_t)(!anyWrap);
			// Theoretically we'd re-get the other edgeNode here - but in fact, if it already existed, we won't access
			// it again anyway.
		}

		edgeNodes[REGION_EDGE_LEFT] = nodes.getElement(edgeIndexes[REGION_EDGE_LEFT]);
		edgeNodes[REGION_EDGE_LEFT]->pos = edgePositions[REGION_EDGE_LEFT];
	}
	edgeNodes[REGION_EDGE_LEFT]->value = reversed ? valueAtLateEdge : startValue;
	edgeNodes[REGION_EDGE_LEFT]->interpolated = interpolateLeftNode;

	// Now delete extra nodes. This first bit will delete all of them if no wrap, or the before-wrap bit if there is a
	// wrap.
	int32_t indexToDeleteFrom = edgeIndexes[REGION_EDGE_LEFT] + 1;
	int32_t indexToDeleteTo = anyWrap ? nodes.getNumElements() : edgeIndexes[REGION_EDGE_RIGHT];
	int32_t numToDelete = indexToDeleteTo - indexToDeleteFrom;
	if (numToDelete) {
		nodes.deleteAtIndex(indexToDeleteFrom, numToDelete);
		// Theoretically we'd decrease edgeIndexes[REGION_EDGE_RIGHT] if no wrap, but it never gets used again in that
		// case of no-wrap.
	}

	// And now delete the nodes after the wrap if necessary.
	if (anyWrap && edgeIndexes[REGION_EDGE_RIGHT]) {
		nodes.deleteAtIndex(0, edgeIndexes[REGION_EDGE_RIGHT]);
		edgeIndexes[REGION_EDGE_LEFT] -= edgeIndexes[REGION_EDGE_RIGHT];
	}

#if ENABLE_SEQUENTIALITY_TESTS
	nodes.testSequentiality(
	    "E433"); // Was "GGGG". Leo got. Sven got. (Probably now solved). (Nope, Michael got on V4.1.0-alpha10 (OLED)!)
#endif
#if ALPHA_OR_BETA_VERSION
	if (nodes.getNumElements()) {
		ParamNode* rightmostNode = nodes.getElement(nodes.getNumElements() - 1);
		if (rightmostNode->pos >= effectiveLength) {
			FREEZE_WITH_ERROR("iiii");
		}
	}
#endif

	return edgeIndexes[REGION_EDGE_LEFT];
}

void AutoParam::homogenizeRegionTestSuccess(int32_t pos, int32_t regionEnd, int32_t startValue, bool interpolateStart,
                                            bool interpolateEnd) {

	nodes.testSequentiality("E317");

	int32_t startI = nodes.search(pos, GREATER_OR_EQUAL);
	int32_t endI = nodes.search(regionEnd, GREATER_OR_EQUAL);

	if (endI == startI + 1 || (endI == 0 && startI == nodes.getNumElements() - 1)) {
		// Fine
	}
	else {
		FREEZE_WITH_ERROR("E119");
	}

	ParamNode* startNode = nodes.getElement(startI);
	ParamNode* endNode = nodes.getElement(endI);

	if (!startNode || !endNode) {
		FREEZE_WITH_ERROR("E118");
	}

	if (startNode->value != startValue) {
		FREEZE_WITH_ERROR("E120");
	}
	if (startNode->interpolated != interpolateStart) {
		FREEZE_WITH_ERROR("E121");
	}
	if (endNode->interpolated != interpolateEnd) {
		FREEZE_WITH_ERROR("E122");
	}
}

int32_t AutoParam::getValuePossiblyAtPos(int32_t pos, ModelStackWithAutoParam* modelStack) {
	if (pos < 0) {
		return getCurrentValue();
	}
	else {
		return getValueAtPos(pos, modelStack);
	}
}

// The reason for specifying whether we're reversing is that at the exact pos of a non-interpolating node, where the
// value abruptly changes, well whether we want the value to the left or the right depends on which direction we're
// going.
int32_t AutoParam::getValueAtPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed) {

	if (!nodes.getNumElements()) {
		return currentValue;
	}

	int32_t rightI = nodes.search(pos + (int32_t)!reversed, GREATER_OR_EQUAL);
	if (rightI >= nodes.getNumElements()) {
		rightI = 0;
	}
	ParamNode* rightNode = nodes.getElement(rightI);

	int32_t leftI = rightI - 1;
	if (leftI < 0) {
		leftI += nodes.getNumElements();
	}
	ParamNode* leftNode = nodes.getElement(leftI);
	if (!rightNode->interpolated) {
returnLeftNodeValue:
		return leftNode->value;
	}

	int32_t ticksSinceLeftNode = pos - leftNode->pos;
	if (!ticksSinceLeftNode) {
		goto returnLeftNodeValue;
	}

	if (ticksSinceLeftNode < 0) { // If pos we're looking at is left of leftmost...
		int32_t lengthBeforeLoop = modelStack->getLoopLength();
		if (lengthBeforeLoop == 2147483647) { // If infinite length - and we know we're interpolating - well we'd have
			                                  // arrived at the next node value
			return rightNode->value;
		}
		ticksSinceLeftNode += lengthBeforeLoop;
	}

	int32_t ticksBetweenNodes = rightNode->pos - leftNode->pos;
	if (ticksBetweenNodes <= 0) { // If pos we're looking at is right of rightmost...
		int32_t lengthBeforeLoop = modelStack->getLoopLength();
		if (lengthBeforeLoop
		    == 2147483647) { // If infinite length, we'll still be on prev node's value, despite us interpolating
			goto returnLeftNodeValue;
		}
		ticksBetweenNodes += lengthBeforeLoop;
	}

	int64_t valueDistance = (int64_t)rightNode->value - (int64_t)leftNode->value;
	return leftNode->value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes);
}

// Returns whether a change was made to currentValue
bool AutoParam::grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack) {
	if (!nodes.getNumElements()) {
		return false;
	}

	int32_t oldValue = currentValue;
	currentValue = getValueAtPos(pos, modelStack);
	return (currentValue != oldValue);
}

void AutoParam::setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed) {

	valueIncrementPerHalfTick = 0; // We may calculate this, below
	renewedOverridingAtTime = 0;
	if (nodes.getNumElements()) {
		int32_t oldValue = currentValue;
		currentValue = getValueAtPos(pos, modelStack, reversed);

		// Get next node
		int32_t rightI = nodes.search(pos + (int32_t)!reversed, GREATER_OR_EQUAL);
		if (rightI == nodes.getNumElements()) {
			rightI = 0;
		}
		ParamNode* nextNodeOurDirection =
		    nodes.getElement(rightI); // This will initially point to the node to the right, regardless of direction;
		                              // it'll be corrected to left if we're reversed below.

		if (nextNodeOurDirection->interpolated) {

			if (reversed) {
				int32_t leftI = rightI - 1;
				if (leftI < 0) {
					leftI += nodes.getNumElements();
				}
				nextNodeOurDirection = nodes.getElement(leftI);
			}

			// Setup interpolation from the pos we're at now
			setupInterpolation(nextNodeOurDirection, modelStack->getLoopLength(), pos, reversed);
		}

		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, true, true);
	}
}

Error AutoParam::beenCloned(bool copyAutomation, int32_t reverseDirectionWithLength) {

	Error error = Error::NONE;

	if (copyAutomation) {

		int32_t numNodes = nodes.getNumElements();

		if (reverseDirectionWithLength && numNodes) {
			// Sneakily and temporarily clone this - still pointing to the old AutoParam's nodes' memory.
			ParamNodeVector oldNodes = nodes;
			nodes.init();

			error = nodes.insertAtIndex(0, numNodes);

			if (error == Error::NONE) {
				ParamNode* rightmostNode = (ParamNode*)oldNodes.getElementAddress(numNodes - 1);
				int32_t oldNodeToLeftValue = rightmostNode->value;

				ParamNode* leftmostNode = (ParamNode*)oldNodes.getElementAddress(0);
				bool anythingAtZero = !leftmostNode->pos;

				for (int32_t iOld = 0; iOld < numNodes; iOld++) {
					int32_t iNew = -iOld - !anythingAtZero;
					if (iNew < 0) {
						iNew += numNodes;
					}

					ParamNode* oldNode = (ParamNode*)oldNodes.getElementAddress(iOld);
					ParamNode* newNode = (ParamNode*)nodes.getElementAddress(iNew);

					int32_t iOldToRight = iOld + 1;
					if (iOldToRight == numNodes) {
						iOldToRight = 0;
					}
					ParamNode* oldNodeToRight = (ParamNode*)oldNodes.getElementAddress(iOldToRight);

					int32_t newPos = -oldNode->pos;
					if (newPos < 0) {
						newPos += reverseDirectionWithLength;
					}

					int32_t newValue = oldNode->interpolated ? oldNode->value : oldNodeToLeftValue;

					newNode->pos = newPos;
					newNode->value = newValue;
					newNode->interpolated = oldNodeToRight->interpolated;

					oldNodeToLeftValue = oldNode->value;
				}
			}

			// Because this is about to get destructed, we need to stop it pointing to the old
			// AutoParam's nodes' memory, cos we don't want that getting deallocated.
			oldNodes.init();
		}

		else {
			error = nodes.beenCloned();
		}
	}
	else {
		nodes.init();
	}

	renewedOverridingAtTime = 0;
	return error;
}

// Wait, surely this should be undoable?
void AutoParam::generateRepeats(uint32_t oldLength, uint32_t newLength, bool shouldPingpong) {

	if (!nodes.getNumElements()) {
		return;
	}

	// When recording session to arranger, you may occasionally end up with nodes beyond the Clip's length. These need
	// to be removed now
	ParamNode* firstNode = nodes.getFirst();
	deleteNodesBeyondPos(oldLength + firstNode->pos);

	// If pingponging, we have to do our own complicated thing.
	if (shouldPingpong) {

		ParamNode* nodeAfterWrap = (ParamNode*)nodes.getElementAddress(0);
		bool nothingAtZero = nodeAfterWrap->pos;

		// We may have to create a new node at pos 0 to represent the fact that a pingpong would suddenly occur in the
		// middle of an interpolating bit between nodes. Actually let's just always put it there, so the beginning
		// doesn't suddenly sound different if we have an odd number of repeats or something.
		if (nothingAtZero) {

			// This block is a quick simple alternative to calling getValueAtPos(), which would also require a
			// modelStack and check for a bunch of unnecessary stuff.
			ParamNode* nodeBeforeWrap = (ParamNode*)nodes.getElementAddress(nodes.getNumElements() - 1);

			bool nodeAfterWrapIsInterpolated =
			    nodeAfterWrap->interpolated; // Make copy, cos we need this even after our pointer is no longer valid,
			                                 // cos we insert below.

			int32_t valueAtZero;

			if (nodeAfterWrapIsInterpolated) {
				int64_t valueDistance = (int64_t)nodeAfterWrap->value - (int64_t)nodeBeforeWrap->value;
				int32_t ticksSinceLeftNode = oldLength - nodeBeforeWrap->pos;
				int32_t ticksBetweenNodes = ticksSinceLeftNode + nodeAfterWrap->pos;
				valueAtZero = nodeBeforeWrap->value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes);
			}
			else {
				valueAtZero = nodeBeforeWrap->value;
			}

			Error error = nodes.insertAtIndex(0);
			if (error != Error::NONE) {
				return;
			}

			ParamNode* zeroNode = (ParamNode*)nodes.getElementAddress(0);
			zeroNode->pos = 0;
			zeroNode->value = valueAtZero;
			zeroNode->interpolated = nodeAfterWrapIsInterpolated;

			nothingAtZero = false;
		}

		int32_t numRepeats =
		    (uint32_t)(newLength - 1) / oldLength + 1; // Rounded up. Including first "repeat", which already exists.

		int32_t numNodesBefore = nodes.getNumElements();

		int32_t numToInsert = (numRepeats - 1) * numNodesBefore;
		if (numToInsert) { // Should always be true?
			Error error = nodes.insertAtIndex(numNodesBefore, numToInsert);
			if (error != Error::NONE) {
				return;
			}
		}

		int32_t highestNodeIndex = numNodesBefore - 1;

		for (int32_t r = 1; r < numRepeats; r++) {

			for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numNodesBefore; iNewWithinRepeat++) {
				int32_t iOld = iNewWithinRepeat;

				if (r & 1) {
					iOld = -iOld - nothingAtZero;
					if (iOld < 0) {
						iOld += numNodesBefore;
					}
				}

				ParamNode* oldNode = (ParamNode*)nodes.getElementAddress(iOld);
				int32_t newPos = oldNode->pos;

				if (r & 1) {
					newPos = -newPos;
					if (newPos < 0) {
						newPos += oldLength;
					}
				}

				newPos += oldLength * r;
				if (newPos >= newLength) {
					break; // Crude way of stopping part-way through the final repeat if it was only a partial one.
				}

				int32_t newValue = oldNode->value;
				bool newInterpolated = oldNode->interpolated;

				// If reversing, we have to change the characteristics given to this node.
				if (r & 1) {

					if (!oldNode->interpolated) {
						int32_t iOldToLeft = iOld - 1;
						if (iOldToLeft < 0) {
							iOldToLeft += numNodesBefore;
						}
						ParamNode* oldNodeToLeft = (ParamNode*)nodes.getElementAddress(iOldToLeft);
						newValue = oldNodeToLeft->value;
					}

					int32_t iOldToRight = iOld + 1;
					if (iOldToRight >= numNodesBefore) {
						iOldToRight = 0;
					}
					ParamNode* oldNodeToRight = (ParamNode*)nodes.getElementAddress(iOldToRight);
					newInterpolated = oldNodeToRight->interpolated;
				}

				int32_t iNew = iNewWithinRepeat + numNodesBefore * r;
				ParamNode* newNode = (ParamNode*)nodes.getElementAddress(iNew);

				newNode->pos = newPos;
				newNode->value = newValue;
				newNode->interpolated = newInterpolated;

				highestNodeIndex = iNew;
			}
		}

		int32_t newNumNodes = highestNodeIndex + 1;
		int32_t numToDelete = nodes.getNumElements() - newNumNodes;
		if (numToDelete) {
			nodes.deleteAtIndex(newNumNodes, numToDelete);
		}
	}

	// Or if not pingponging, we just do a simple call.
	else {
		nodes.generateRepeats(oldLength, newLength);
	}
}

void AutoParam::appendParam(AutoParam* otherParam, int32_t oldLength, int32_t reverseThisRepeatWithLength,
                            bool pingpongingGenerally) {

	int32_t numToInsert = otherParam->nodes.getNumElements();
	if (!numToInsert) {
		return;
	}

	// When recording session to arranger, you may occasionally end up with nodes beyond the Clip's length. These need
	// to be removed now
	ParamNode* firstNode = otherParam->nodes.getFirst();
	deleteNodesBeyondPos(oldLength + firstNode->pos);

	ParamNode* nodeAfterWrap = (ParamNode*)otherParam->nodes.getElementAddress(0);
	bool nothingAtZero = nodeAfterWrap->pos;

	// We may have to create a new node at pos 0 (of the new repeat) to represent the fact that a pingpong would
	// suddenly occur in the middle of an interpolating bit between nodes. Hopefully the note at actual pos 0 got
	// created back when generateRepeats got called initially for this, at the start of recording etc.
	if (pingpongingGenerally && nothingAtZero && nodeAfterWrap->interpolated) {

		// This block is a quick simple alternative to calling getValueAtPos(), which would also require a modelStack
		// and check for a bunch of unnecessary stuff.
		ParamNode* nodeBeforeWrap = (ParamNode*)otherParam->nodes.getElementAddress(numToInsert - 1);
		int64_t valueDistance = (int64_t)nodeAfterWrap->value - (int64_t)nodeBeforeWrap->value;
		int32_t ticksSinceLeftNode = oldLength - nodeBeforeWrap->pos;
		int32_t ticksBetweenNodes = ticksSinceLeftNode + nodeAfterWrap->pos;
		int32_t valueAtZero = nodeBeforeWrap->value + (valueDistance * ticksSinceLeftNode / ticksBetweenNodes);

		int32_t newZeroNodeI = nodes.getNumElements();

		Error error = nodes.insertAtIndex(newZeroNodeI);
		if (error != Error::NONE) {
			return;
		}

		ParamNode* zeroNode = (ParamNode*)nodes.getElementAddress(newZeroNodeI);
		zeroNode->pos = oldLength;
		zeroNode->value = valueAtZero;
		zeroNode->interpolated = true;

		// nothingAtZero = false; // Unlike in generateRepeats(), above, the node we've added is not a part of the same
		// array that represents our source material.
	}

	int32_t oldNumNodes = nodes.getNumElements();
	Error error = nodes.insertAtIndex(oldNumNodes, numToInsert);
	if (error != Error::NONE) {
		return;
	}

	if (reverseThisRepeatWithLength) {

		for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numToInsert; iNewWithinRepeat++) {
			int32_t iOld = -iNewWithinRepeat - nothingAtZero;
			if (iOld < 0) {
				iOld += numToInsert;
			}

			ParamNode* oldNode = (ParamNode*)otherParam->nodes.getElementAddress(iOld);
			int32_t newPos = oldNode->pos;

			newPos = -newPos;
			if (newPos < 0) {
				newPos += reverseThisRepeatWithLength;
			}

			newPos += oldLength;

			int32_t newValue = oldNode->value;
			bool newInterpolated = oldNode->interpolated;

			if (!oldNode->interpolated) {
				int32_t iOldToLeft = iOld - 1;
				if (iOldToLeft < 0) {
					iOldToLeft += numToInsert;
				}
				ParamNode* oldNodeToLeft = (ParamNode*)otherParam->nodes.getElementAddress(iOldToLeft);
				newValue = oldNodeToLeft->value;
			}

			int32_t iOldToRight = iOld + 1;
			if (iOldToRight >= numToInsert) {
				iOldToRight = 0;
			}
			ParamNode* oldNodeToRight = (ParamNode*)otherParam->nodes.getElementAddress(iOldToRight);
			newInterpolated = oldNodeToRight->interpolated;

			int32_t iNew = iNewWithinRepeat + oldNumNodes;
			ParamNode* newNode = (ParamNode*)nodes.getElementAddress(iNew);

			newNode->pos = newPos;
			newNode->value = newValue;
			newNode->interpolated = newInterpolated;
		}
	}

	else {

		for (int32_t i = 0; i < numToInsert; i++) {
			ParamNode* oldNode = otherParam->nodes.getElement(i);
			ParamNode* newNode = nodes.getElement(oldNumNodes + i);
			newNode->pos = oldNode->pos + oldLength;
			newNode->interpolated = oldNode->interpolated;
			newNode->value = oldNode->value;
		}
	}
}

void AutoParam::deleteNodesBeyondPos(int32_t pos) {
	int32_t i = nodes.search(pos, GREATER_OR_EQUAL);
	int32_t numToDelete = nodes.getNumElements() - i;
	if (numToDelete) {
		nodes.deleteAtIndex(i, numToDelete);
	}
}

void AutoParam::trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack) {

	// If no nodes, nothing to do
	if (!nodes.getNumElements()) {
		return;
	}

	// If final node is within new length, also nothing to do
	ParamNode* lastNode = nodes.getLast();
	if (lastNode) { // Should always be one...
		if (lastNode->pos < newLength) {
			return;
		}
	}

	// To ensure that the effective value at pos 0 remains the same even after earlier nodes deleted, we might need to
	// add a new, non-interpolating node there.
	bool needNewNodeAt0 = nodes.getFirst()->pos; // Deactivated for now, but I'm going to enable in the ModelStacks
	                                             // branch, where we have a TimelineCounter here.
	int32_t oldValueAt0;
	if (needNewNodeAt0) {
		oldValueAt0 = getValueAtPos(0, modelStack);
	}

	int32_t newNumNodes = nodes.search(newLength, GREATER_OR_EQUAL);

	if (ALPHA_OR_BETA_VERSION && newNumNodes >= nodes.getNumElements()) {
		FREEZE_WITH_ERROR("E315");
	}

	// If still at least 2 nodes afterwards (1 is not allowed, actually wait it is now but let's keep this safe for
	// now)...
	if (newNumNodes >= 2) {

		// If no action, just basic trim
		if (!action) {
basicTrim: {
	int32_t numToDelete = nodes.getNumElements() - newNumNodes; // Will always be >= 1
	nodes.deleteAtIndex(newNumNodes, numToDelete);
}

addNewNodeAt0IfNecessary:
			if (needNewNodeAt0) {
				Error error = nodes.insertAtIndex(0);
				if (error == Error::NONE) { // Should be fine cos we just deleted some, so some free RAM
					ParamNode* newNode = nodes.getElement(0);
					newNode->pos = 0;
					newNode->value = oldValueAt0;
					newNode->interpolated = false;
				}
			}
		}

		// Or if action...
		else {

			// If action already has a backed up snapshot for this param, can still just do a basic trim
			if (action->containsConsequenceParamChange(modelStack->paramCollection, modelStack->paramId)) {
				goto basicTrim;

				// Or, if we need to snapshot, work with that
			}
			else {
				ParamNodeVector newNodes;
				Error error = newNodes.insertAtIndex(0, newNumNodes);
				if (error != Error::NONE) {
					goto basicTrim;
				}

				for (int32_t i = 0; i < newNumNodes; i++) {
					ParamNode* __restrict__ sourceNode = nodes.getElement(i);
					ParamNode* __restrict__ destNode = newNodes.getElement(i);

					*destNode = *sourceNode;
				}

				// We've kept the original Nodes separate in memory, so can steal them into an undo-accessible snapshot
				action->recordParamChangeDefinitely(modelStack, true); // Steal

				// And, need to swap the new Nodes in
				nodes.swapStateWith(&newNodes);

				goto addNewNodeAt0IfNecessary;
			}
		}
	}

	// Or if no nodes afterwards
	else {
		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStack, true); // Steal
		}
		nodes.empty();                 // Delete them - either if no action, or if the above chose not to steal them.
		valueIncrementPerHalfTick = 0; // In case we were interpolating.
	}
}

void AutoParam::writeToFile(Serializer& writer, bool writeAutomation, int32_t* valueForOverride) {
	char buffer[9];

	writer.write("0x");

	int32_t valueNow = (valueForOverride && isAutomated()) ? *valueForOverride : currentValue;

	intToHex(valueNow, buffer);
	writer.write(buffer);

	if (writeAutomation) {

		for (int32_t i = 0; i < nodes.getNumElements(); i++) {
			ParamNode* thisNode = nodes.getElement(i);
			intToHex(thisNode->value, buffer);
			writer.write(buffer);

			uint32_t pos = thisNode->pos;
			if (thisNode->interpolated) {
				pos |= ((uint32_t)1 << 31);
			}
			intToHex(pos, buffer);
			writer.write(buffer);
		}
	}
}

// Returns error code.
// If you're gonna call this, you probably need to tell the ParamSet that this Param has automation now, if it does.
// Or, to make things easier, you should just call the ParamSet instead, if possible.
Error AutoParam::readFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {

	// Must first delete any automation because sometimes, due to that annoying support I have to do for late-2016
	// files, we'll be overwriting a cloned ParamManager, which might have had automation.
	deleteAutomationBasicForSetup();

	if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
		return Error::NONE;
	}

	// char buffer[12];
	char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
	if (!firstChars) {
		return Error::NONE;
	}

	// If a decimal, then read the rest of the digits
	if (*(uint16_t*)firstChars != charsToIntegerConstant('0', 'x')) {
		char buffer[12];
		buffer[0] = firstChars[0];
		buffer[1] = firstChars[1];

		for (int32_t i = 2; i < 12 && (buffer[i] = reader.readNextCharOfTagOrAttributeValue()); i++) {}
		buffer[11] = 0;
		currentValue = stringToInt(buffer);
		return Error::NONE;
	}

	// Or, normal case - hex and automation...

	// First, read currentValue
	char const* hexChars = reader.readNextCharsOfTagOrAttributeValue(8);
	if (!hexChars) {
		return Error::NONE;
	}
	currentValue = hexToIntFixedLength(hexChars, 8);

	// And now read in the automation
	int32_t numElementsToAllocateFor = 0;

	if (readAutomationUpToPos) {

		int32_t prevPos = -1;

		while (true) {

			// Every time we've reached the end of a cluster...
			if (numElementsToAllocateFor <= 0) {

				// See how many more chars before the end of the cluster. If there are any...
				uint32_t charsRemaining = reader.getNumCharsRemainingInValueBeforeEndOfCluster();
				if (charsRemaining) {

					// Allocate space for the right number of notes, and remember how long it'll be before we need to do
					// this check again
					numElementsToAllocateFor = (uint32_t)(charsRemaining - 1) / 16 + 1;
					nodes.ensureEnoughSpaceAllocated(
					    numElementsToAllocateFor); // If it returns false... oh well. We'll fail later
				}
			}

			hexChars = reader.readNextCharsOfTagOrAttributeValue(16);
			if (!hexChars) {
				return Error::NONE;
			}
			int32_t value = hexToIntFixedLength(hexChars, 8);
			int32_t pos = hexToIntFixedLength(&hexChars[8], 8);

			bool interpolated = (pos & ((uint32_t)1 << 31));
			if (interpolated) {
				pos &= ~((uint32_t)1 << 31);
			}

			// Ensure there isn't some problem where nodes are out of order...
			if (pos <= prevPos) {
				D_PRINTLN("Automation nodes out of order");
				continue;
			}

			// If we've reached the end of our allowed timeline length for automation...
			if (pos >= readAutomationUpToPos) {

				// If there's a node actually right on the end-point - well, firmware <= 3.1.5 sometimes put one there
				// when it should have been at pos 0. So, reinterpret that data to make it right.
				if (pos == readAutomationUpToPos) {
					ParamNode* firstNode = nodes.getElement(0);
					if (!firstNode || firstNode->pos) {
						Error error = nodes.insertAtIndex(0);
						if (error != Error::NONE) {
							return error;
						}
						firstNode = nodes.getElement(0);
						firstNode->pos = 0;
						firstNode->value = value;
						firstNode->interpolated = interpolated;
					}
				}
				break;
			}

			prevPos = pos;

			int32_t nodeI = nodes.insertAtKey(pos, true);
			if (nodeI == -1) {
				return Error::INSUFFICIENT_RAM;
			}
			ParamNode* node = nodes.getElement(nodeI);
			node->value = value;
			node->interpolated = interpolated;

			numElementsToAllocateFor--;
		}
	}

	return Error::NONE;
}

bool AutoParam::containsSomething(uint32_t neutralValue) {
	if (isAutomated()) {
		return true;
	}
	uint32_t* a = (uint32_t*)&currentValue;
	return (*a != (uint32_t)neutralValue);
}

bool AutoParam::containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue) {
	return (wasAutomatedBefore || valueBefore != neutralValue);
}

void AutoParam::shiftValues(int32_t offset) {
	int64_t newValue = (int64_t)currentValue + offset;
	if (newValue >= (int64_t)2147483648u) {
		currentValue = 2147483647;
	}
	else if (newValue < (int64_t)2147483648u * -1) {
		currentValue = -2147483648;
	}
	else {
		currentValue = newValue;
	}

	for (int32_t i = 0; i < nodes.getNumElements(); i++) {
		ParamNode* thisNode = nodes.getElement(i);
		int64_t newValue = (int64_t)thisNode->value + offset;
		if (newValue >= (int64_t)2147483648u) {
			thisNode->value = 2147483647;
		}
		else if (newValue < (int64_t)2147483648u * -1) {
			thisNode->value = -2147483648;
		}
		else {
			thisNode->value = newValue;
		}
	}
}

void AutoParam::shiftParamVolumeByDB(float offset) {
	currentValue = shiftVolumeByDB(currentValue, offset);

	for (int32_t i = 0; i < nodes.getNumElements(); i++) {
		ParamNode* thisNode = nodes.getElement(i);
		thisNode->value = shiftVolumeByDB(thisNode->value, offset);
	}
}

void AutoParam::shiftHorizontally(int32_t amount, int32_t effectiveLength) {
	nodes.shiftHorizontal(amount, effectiveLength);
}

void AutoParam::swapState(AutoParamState* state, ModelStackWithAutoParam const* modelStack) {
	bool automatedBefore = isAutomated();

	int32_t oldValueHere = currentValue;
	currentValue = state->value;
	state->value = oldValueHere;
	nodes.swapStateWith(&state->nodes);

	bool automatedNow = isAutomated();

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValueHere, true, automatedBefore,
	                                                          automatedNow);
}

void AutoParam::paste(int32_t startPos, int32_t endPos, float scaleFactor, ModelStackWithAutoParam const* modelStack,
                      CopiedParamAutomation* copiedParamAutomation, bool isPatchCable) {

	bool automatedBefore = isAutomated();
	int32_t effectiveLength = modelStack->getLoopLength();
	int32_t wrappedEndPos = endPos % effectiveLength;
	bool overwrittingEntireRegion = startPos == 0 && endPos >= effectiveLength;

#if defined(ALPHA_OR_BETA_VERSION)
	if (copiedParamAutomation->nodes == nullptr || copiedParamAutomation->numNodes == 0) {
		FREEZE_WITH_ERROR("E453");
	}
#endif

	// Save the current value at the start and end of the region w'ere pasting in to, before we start messing with it.
	int32_t startValue = getValueAtPos(startPos, modelStack);
	int32_t endValue = getValueAtPos(wrappedEndPos, modelStack);

	// Clear out any nodes that already exiset in the region we're pasting in to
	int32_t iDeleteBegin = nodes.search(startPos, GREATER_OR_EQUAL);
	int32_t iDeleteEnd = nodes.search(endPos, GREATER_OR_EQUAL);
	int32_t numToDelete = iDeleteEnd - iDeleteBegin;
	if (numToDelete > 0) {
		nodes.deleteAtIndex(iDeleteBegin, numToDelete);
	}

	// Make sure that automation data before the paste region starts and after it ends aligns with what we're about to
	// put there
	if (!overwrittingEntireRegion) {
		// The copied parameter automation always has a node at t=0, if that doesn't match the existing content we need
		// to insert a node right before it to preserve the automation before the paste.
		if (startValue != copiedParamAutomation->nodes[0].value) {
			int32_t ticksBeforeStart = 1;
			int32_t resetPos = (ticksBeforeStart > startPos) ? (effectiveLength + startPos) - ticksBeforeStart
			                                                 : (startPos - ticksBeforeStart);
			int32_t resetI = nodes.search(resetPos, GREATER_OR_EQUAL);
			ParamNode* resetNode = nodes.getElement(resetI);

			if (resetNode == nullptr || resetNode->pos != resetPos) {
				bool previousNodeInterpolated = resetNode != nullptr ? resetNode->interpolated : true;
				auto error = nodes.insertAtIndex(resetI);
				if (error != Error::NONE) {
					return;
				}

				resetNode = nodes.getElement(resetI);
				resetNode->pos = resetPos;
				resetNode->interpolated = previousNodeInterpolated;
			}

			resetNode->value = startValue;
		}

		// If the final node does not match the value at the end position, we need to insert a node.
		ParamNode const& finalNode = copiedParamAutomation->nodes[copiedParamAutomation->numNodes - 1];
		if (endValue != finalNode.value) {
			int32_t resetPos = 0;
			// The copied automation has a node that will overlap, need to insert 1-past-the-end
			if (finalNode.pos == endPos - startPos) {
				// wrappedEndPos+1 is always within the sequence length since a sequence length of 0 or 1 is probably
				// super broken in other places.
				resetPos = wrappedEndPos + 1;
			}
			else {
				resetPos = wrappedEndPos;
			}

			int32_t resetI = nodes.search(resetPos, GREATER_OR_EQUAL);
			ParamNode* resetNode = nodes.getElement(resetI);

			if (!resetNode || resetNode->pos != resetPos) {
				auto error = nodes.insertAtIndex(resetI);
				if (error != Error::NONE) {
					return;
				}

				resetNode = nodes.getElement(resetI);
				resetNode->pos = resetPos;
				resetNode->interpolated = finalNode.interpolated;
			}

			resetNode->value = endValue;
		}
	}

	// Ok now paste the stuff
	int32_t minPos = 0;

	int32_t maxPos = std::min(endPos, effectiveLength);

	for (int32_t n = 0; n < copiedParamAutomation->numNodes; n++) {

		ParamNode* nodeSource = &copiedParamAutomation->nodes[n];

		int32_t newPos = startPos + (int32_t)roundf((float)nodeSource->pos * scaleFactor);

		// Make sure that with dividing and rounding, we're not overlapping the previous node - or past the end of the
		// screen / Clip
		if (newPos < minPos || newPos >= maxPos) {
			continue;
		}

		int32_t nodeDestI = nodes.insertAtKey(newPos);
		ParamNode* nodeDest = nodes.getElement(nodeDestI);
		if (!nodeDest) {
			return;
		}

		nodeDest->value = nodeSource->value;
		nodeDest->interpolated = nodeSource->interpolated;

		if (isPatchCable) {
			nodeDest->value >>= 1;
		}

		minPos = newPos + 1;
	}

	// TODO: should currentValue instantly change if we're playing?

	nodes.testSequentiality("E440");

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, currentValue, true, automatedBefore,
	                                                          isAutomated());
}

void AutoParam::copy(int32_t startPos, int32_t endPos, CopiedParamAutomation* copiedParamAutomation, bool isPatchCable,
                     ModelStackWithAutoParam const* modelStack) {

	// And if any of them are in the right zone...
	int32_t startI = nodes.search(startPos, GREATER_OR_EQUAL);
	int32_t endI = nodes.search(endPos, GREATER_OR_EQUAL);

	copiedParamAutomation->width = endPos - startPos;

	copiedParamAutomation->numNodes = endI - startI;

	bool insertingExtraNodeAtStart = false;

	if (copiedParamAutomation->numNodes) {
		ParamNode* firstNode = nodes.getElement(startI);
		if (firstNode->pos != startPos) {
			insertingExtraNodeAtStart = true;
			copiedParamAutomation->numNodes++;
		}
	}

	if (copiedParamAutomation->numNodes > 0) {
		// Allocate some memory for the nodes
		if (copiedParamAutomation->nodes != nullptr) {
			GeneralMemoryAllocator::get().dealloc(copiedParamAutomation->nodes);
		}

		copiedParamAutomation->nodes = (ParamNode*)GeneralMemoryAllocator::get().allocLowSpeed(
		    sizeof(ParamNode) * copiedParamAutomation->numNodes);

		if (!copiedParamAutomation->nodes) {
			copiedParamAutomation->numNodes = 0;
			display->displayError(Error::INSUFFICIENT_RAM);
			return;
		}

		int32_t n = 0;

		if (insertingExtraNodeAtStart) {
			ParamNode* newNode = &copiedParamAutomation->nodes[n];
			newNode->pos = 0;
			newNode->value = getValueAtPos(startPos, modelStack);
			newNode->interpolated = false;

			if (isPatchCable) {
				newNode->value = lshiftAndSaturate<1>(newNode->value);
			}

			n++;
		}

		// Fill in all the Nodes' details
		int32_t readingNodeI = startI;

		for (; n < copiedParamAutomation->numNodes; n++) {
			ParamNode* nodeToCopy = nodes.getElement(readingNodeI);
			ParamNode* newNode = &copiedParamAutomation->nodes[n];

			*newNode = *nodeToCopy;
			newNode->pos -= startPos;

			if (isPatchCable) {
				newNode->value = lshiftAndSaturate<1>(newNode->value);
			}

			readingNodeI++;
		}
	}
}

// For MIDI CCs, which prior to V2.0 did interpolation.
// And MIDI pitch bend, which prior to V3.2 did interpolation.
// Returns error code.
// quantizationRShift would be 25 for 7-bit CC values (cos 32 - 25 == 7).
// Or it'd ideally be 18 for 14-bit pitch bend data, but that'd be a bit overkill.
Error AutoParam::makeInterpolationGoodAgain(int32_t clipLength, int32_t quantizationRShift) {

	if (nodes.getNumElements() <= 1) {
		return Error::NONE;
	}

	int32_t stopAtElement = nodes.getNumElements();

	for (int32_t i = 0; i < stopAtElement; i++) {
		ParamNode* thisNode = nodes.getElement(i);

		if (thisNode->interpolated) {
			int32_t prevI = i - 1;
			if (prevI == -1) {
				prevI = nodes.getNumElements() - 1;
			}
			ParamNode* prevNode = nodes.getElement(prevI);

			// This function deals with "small" values, which for CCs will be between -64 and 64. Yup, they're
			// bidirectional.

			int32_t thisSmallValue = rshift_round_signed(thisNode->value >> 1, quantizationRShift - 1);
			int32_t lastSmallValue = rshift_round_signed(prevNode->value >> 1, quantizationRShift - 1);

			int32_t smallValueChange = thisSmallValue - lastSmallValue;

			int32_t absoluteSmallValueChange = smallValueChange;
			int32_t gradientDirection = 1;
			if (absoluteSmallValueChange < 0) {
				absoluteSmallValueChange = -absoluteSmallValueChange;
				gradientDirection = -1;
			}
			if (absoluteSmallValueChange < 2) {
				continue;
			}

			int32_t prevNodePos = prevNode->pos;
			int32_t distance = thisNode->pos - prevNodePos;
			if (distance < 0) {
				distance += clipLength;
			}

			if (distance < 2) {
				continue;
			}

			bool isSteep = (distance < absoluteSmallValueChange);
			int32_t maxJ = isSteep ? distance : absoluteSmallValueChange;
			for (int32_t j = 1; j < maxJ; j++) {

				int32_t thisPos;
				int32_t newSmallValue;

				if (isSteep) {
					thisPos = prevNodePos + j;
					newSmallValue = lastSmallValue + smallValueChange * j / distance;
				}
				else {
					thisPos = prevNodePos + (uint64_t)distance * j / absoluteSmallValueChange;
					newSmallValue = lastSmallValue + j * gradientDirection;
				}

				if (thisPos >= clipLength) {
					thisPos -= clipLength;
				}

				int32_t newNodeI = nodes.insertAtKey(thisPos);
				if (newNodeI == -1) {
					return Error::INSUFFICIENT_RAM;
				}
				if (newNodeI <= i) {
					i++;
					stopAtElement++;
				}
				ParamNode* newNode = nodes.getElement(newNodeI);
				newNode->interpolated = true;

				int32_t newBigValue;
				if (newSmallValue == (1 << (31 - quantizationRShift))) {
					newBigValue = 2147483647; // E.g. if a CC value has come out as high as 64, make sure it fits into
					                          // the 32-bit signed number when we left-shift.
				}
				else {
					newBigValue = newSmallValue << quantizationRShift;
				}
				newNode->value = newBigValue;
			}
		}
	}

	nodes.testSequentiality("E414");

	return Error::NONE;
}

void AutoParam::transposeCCValuesToChannelPressureValues() {
	for (int32_t i = 0; i < nodes.getNumElements(); i++) {
		ParamNode* thisNode = nodes.getElement(i);
		thisNode->value = (thisNode->value >> 1) + (1 << 30);
	}

	currentValue = (currentValue >> 1) + (1 << 30);
}

void AutoParam::deleteTime(int32_t startPos, int32_t lengthToDelete, ModelStackWithAutoParam* modelStack) {

	// No need to do any revertability with an Action here - ParamCollection::backUpAllAutomatedParamsToAction() should
	// have already been called.

	int32_t endPos = startPos + lengthToDelete;

	int32_t start = nodes.search(startPos, GREATER_OR_EQUAL);
	int32_t end = nodes.search(endPos, GREATER_OR_EQUAL);

	int32_t numToDelete = end - start;
	if (numToDelete > 0) {

		// We might want to put a new node right at the cut-point if not already one there

		bool shouldAddNodeAtPos0 = false;
		int32_t oldValue;

		// If we're chopping off the final node, we'll want to put one at pos 0 if none there
		if (end >= nodes.getNumElements()) {
			shouldAddNodeAtPos0 = nodes.getFirst()->pos;
			if (shouldAddNodeAtPos0) {
				oldValue = getValueAtPos(0, modelStack);
			}
		}

		// Or, if we're not chopping off the final node, we'll want to put one at end of deleted region (which becomes
		// the same as the start), if none there
		else if (nodes.getElement(end)->pos > endPos) {

			// We'll use the first node we were going to delete as the new one
			ParamNode* cutNode = nodes.getElement(start);
			cutNode->value = getValueAtPos(endPos, modelStack);
			cutNode->pos = startPos;
			cutNode->interpolated = false;

			// Ok, that's one node we're not doing to delete after all
			numToDelete--;
			start++;
			if (!numToDelete) {
				goto allDeleted;
			}
		}

		nodes.deleteAtIndex(start, numToDelete, !shouldAddNodeAtPos0);

		if (shouldAddNodeAtPos0) {
			Error error =
			    nodes.insertAtIndex(0); // Shouldn't ever fail as we told it not to shorten its memory previously
			if (error == Error::NONE) {
				ParamNode* newNode = nodes.getElement(0);
				newNode->value = oldValue;
				newNode->pos = 0;
				newNode->interpolated = false;
				start++; // Cos we've shifted everything along in the list by inserting at index 0
			}
		}
	}

allDeleted:
	for (int32_t i = start; i < nodes.getNumElements(); i++) {
		ParamNode* node = nodes.getElement(i);
		node->pos -= lengthToDelete;
	}

	// If only one node left, that's not allowed, so delete that too. Actually it is allowed now, but let's keep this
	// safe
	if (nodes.getNumElements() == 1) {
		nodes.deleteAtIndex(0);
	}
}

void AutoParam::insertTime(int32_t pos, int32_t lengthToInsert) {
	int32_t start = nodes.search(pos, GREATER_OR_EQUAL);

	for (int32_t i = start; i < nodes.getNumElements(); i++) {
		ParamNode* node = nodes.getElement(i);
		node->pos += lengthToInsert;
	}
}

// Offset must be either 1 or -1.
void AutoParam::moveRegionHorizontally(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t length,
                                       int32_t offset, int32_t lengthBeforeLoop, Action* action) {

	if (!nodes.getNumElements()) {
		return;
	}

	if (action) {
		action->recordParamChangeDefinitely(modelStack, false);
	}

	if (length == lengthBeforeLoop) {
justShiftEverything:
		shiftHorizontally(offset, lengthBeforeLoop);
		return;
	}

	int32_t endPos = pos + length;
	if (endPos > lengthBeforeLoop) { // Wrap

		endPos -= lengthBeforeLoop;
		int32_t resultingIndexes[2];
		{
			int32_t searchTerms[2];
			searchTerms[0] = endPos;
			searchTerms[1] = pos;
			nodes.searchDual(searchTerms, resultingIndexes);
		}

		if (resultingIndexes[0] == resultingIndexes[1]) {
			goto justShiftEverything;
		}

		// Moving right...
		if (offset == 1) {

			// If anything after wrap...
			if (resultingIndexes[0]) {
				// If rightmost node collides with endPos, delete it
				int32_t rightMostIndex = resultingIndexes[0] - 1;
				ParamNode* rightMost = nodes.getElement(rightMostIndex);
				if (rightMost->pos >= endPos - 1) {
					nodes.deleteAtIndex(rightMostIndex);
					resultingIndexes[0]--;
					resultingIndexes[1]--;
				}
			}

			// And if anything before wrap...
			if (resultingIndexes[1] < nodes.getNumElements()) {
				int32_t indexBeforeWrap = nodes.getNumElements() - 1;
				ParamNode* nodeBeforeWrap = nodes.getElement(indexBeforeWrap);

				// If that needs to move right and wrap around...
				if (nodeBeforeWrap->pos >= lengthBeforeLoop - 1) {
					ParamNode tempNode = *nodeBeforeWrap;
					nodes.deleteAtIndex(indexBeforeWrap, 1, false);
					nodes.insertAtIndex(0); // Shouldn't be able to fail....
					ParamNode* nodeAfterWrap = nodes.getElement(0);
					*nodeAfterWrap = tempNode;
					nodeAfterWrap->pos = -1; // It'll get incremented below
					resultingIndexes[0]++;
					resultingIndexes[1]++;
				}
			}

			for (int32_t i = 0; i < resultingIndexes[0]; i++) { // After wrap
				nodes.getElement(i)->pos++;
			}
			for (int32_t i = resultingIndexes[1]; i < nodes.getNumElements(); i++) { // Before wrap
				nodes.getElement(i)->pos++;
			}
		}

		// Moving left
		else {
			// If there's anything after the wrap...
			if (resultingIndexes[0]) {
				ParamNode* nodeAfterWrap = nodes.getElement(0);
				// If we need to wrap it around to the end...
				if (nodeAfterWrap->pos == 0) {
					ParamNode tempNode = *nodeAfterWrap;
					int32_t rightMostIndex = nodes.getNumElements() - 1;
					nodes.deleteAtIndex(0, 1, false);
					nodes.insertAtIndex(rightMostIndex); // Surely can't fail
					ParamNode* rightMostNode = nodes.getElement(rightMostIndex);
					*rightMostNode = tempNode;
					rightMostNode->pos = lengthBeforeLoop; // It'll get decremented below.
					resultingIndexes[0]--;
					resultingIndexes[1]--;
				}
			}

			// And now if our left edge is going to eat into anything...
			if (resultingIndexes[1] && resultingIndexes[1] < nodes.getNumElements()) {
				int32_t prevNodeIndex = resultingIndexes[1] - 1;
				ParamNode* prevNode = nodes.getElement(prevNodeIndex);
				if (prevNode->pos >= pos - 1) {
					nodes.deleteAtIndex(prevNodeIndex);
					resultingIndexes[1]--;
				}
			}
		}

		for (int32_t i = 0; i < resultingIndexes[0]; i++) { // After wrap
			nodes.getElement(i)->pos--;
		}
		for (int32_t i = resultingIndexes[1]; i < nodes.getNumElements(); i++) { // Before wrap
			nodes.getElement(i)->pos--;
		}
	}

	else { // No wrap
		int32_t resultingIndexes[2];
		{
			int32_t searchTerms[2];
			searchTerms[0] = pos;
			searchTerms[1] = endPos;
			nodes.searchDual(searchTerms, resultingIndexes);
		}

		if (!resultingIndexes[0] && resultingIndexes[1] == nodes.getNumElements()) {
			goto justShiftEverything;
		}

		if (resultingIndexes[1] != resultingIndexes[0]) { // Hmm I don't think we quite want to do this check...

			// If moving them right, that's pretty easy. Nothing can even wrap because our moving bit slams into a brick
			// wall to the right.
			if (offset == 1) {

				// If rightmost node collides with endPos, delete it
				int32_t rightMostIndex = resultingIndexes[1] - 1;
				ParamNode* rightMost = nodes.getElement(rightMostIndex);
				if (rightMost->pos >= endPos - 1) {
					nodes.deleteAtIndex(rightMostIndex);
					resultingIndexes[1]--;
				}

				for (int32_t i = resultingIndexes[0]; i < resultingIndexes[1]; i++) {
					nodes.getElement(i)->pos++;
				}
			}

			// Moving left
			else {

				// If there's anything to the left that we'll eat into...
				if (resultingIndexes[0] > 0) {

					int32_t prevNodeIndex = resultingIndexes[0] - 1;
					ParamNode* prevNode = nodes.getElement(prevNodeIndex);
					if (prevNode->pos >= pos - 1) {
						nodes.deleteAtIndex(prevNodeIndex);
						resultingIndexes[0]--;
						resultingIndexes[1]--;
					}
				}

				// Or if we don't have space to the left...
				else {
					ParamNode* leftMostNode = nodes.getElement(resultingIndexes[0]);
					if (leftMostNode->pos) {} // All good - it can go left
					else {
						// Ok, we have to wrap it.
						// Delete any node at the far right of the loop
						int32_t lastNodeIndex = nodes.getNumElements() - 1;
						ParamNode* lastNode = nodes.getElement(lastNodeIndex);

						// Well actually, if it's right there, we'll just reuse it and delete our old one
						if (lastNode->pos == lengthBeforeLoop - 1) {
							*lastNode = *leftMostNode;
						}

						// Otherwise, have to delete our old one and put a new one at the end.
						else {
							ParamNode tempNode = *leftMostNode;
							nodes.deleteAtIndex(0, 1, false);
							nodes.insertAtIndex(lastNodeIndex); // Yes, lastNodeIndex is still correct. And, this really
							                                    // shouldn't be able to give an error...
							lastNode = nodes.getElement(lastNodeIndex);
							*lastNode = tempNode;
						}

						lastNode->pos = lengthBeforeLoop - 1;
						resultingIndexes[1]--; // There's one less node we'll have to move below
					}
				}

				for (int32_t i = resultingIndexes[0]; i < resultingIndexes[1]; i++) {
					nodes.getElement(i)->pos--;
				}
			}
		}
	}
}

void AutoParam::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
                                                ModelStackWithAutoParam const* modelStack) {
	int32_t nodeI = nodes.searchExact(pos);
	if (nodeI != -1) {

		if (action) {
			action->recordParamChangeDefinitely(modelStack, false);
		}

		ParamNode* node = (ParamNode*)nodes.getElementAddress(nodeI);
		if (!node->interpolated) {

			int32_t newNodePos = pos + offset;
			int32_t nextNodeI;

			// If that's caused wrapping left...
			if (newNodePos < 0) {
				newNodePos += lengthBeforeLoop;
				nextNodeI = nodes.getNumElements() - 2; // Subtract 2 instead of the normal 1 cos we're about to delete
				                                        // one element before doing anything with this index number

doWrap:
				// There should never be just one node
				if (ALPHA_OR_BETA_VERSION && nodes.getNumElements() == 1) {
					FREEZE_WITH_ERROR("E335");
				}
				int32_t ourValue = node->value; // Grab this before deleting stuff

				// Delete the old node
				nodes.deleteAtIndex(nodeI);

				ParamNode* nextNode = (ParamNode*)nodes.getElementAddress(nextNodeI);

				// If that next node is at the pos we're wanting to nudge to, and would hence get deleted, we can just
				// copy to it
				if (nextNode->pos == newNodePos) {

					// But if that would all mean that we'd actually end up with only 1 node, well that's not allowed.
					// (Actually it is now, but let's be safe.)
					if (nodes.getNumElements() == 1) {
						nodes.empty();
					}

					// But yeah normally that'll be fine - just go copy to that node we've collided with
					else {
						goto setNodeValue;
					}
				}

				// Otherwise, create a new node
				else {
					nextNodeI = nodes.getNumElements();
					{
						Error error = nodes.insertAtIndex(
						    nextNodeI); // This shouldn't be able to fail, cos we just deleted a node
						if (ALPHA_OR_BETA_VERSION && error != Error::NONE) {
							FREEZE_WITH_ERROR("E333");
						}
					}

					nextNode = (ParamNode*)nodes.getElementAddress(nextNodeI);
					nextNode->pos = newNodePos;

setNodeValue:
					nextNode->value = ourValue;
					nextNode->interpolated = false;
				}
			}

			// Or if wrapping right...
			else if (newNodePos >= lengthBeforeLoop) {
				newNodePos -= lengthBeforeLoop;
				nextNodeI = 0;
				goto doWrap;
			}

			// Or if no wrapping
			else {

				// Nudge our node
				nextNodeI = nodeI + offset;
				node->pos = newNodePos;

				// If no previously existing nodes further in that direction at all, that's easy
				if (nextNodeI < 0 || nextNodeI >= nodes.getNumElements()) {}

				// Or if some node, have a look at it, and delete it if it's been collided with
				else {
					ParamNode* nextNode = (ParamNode*)nodes.getElementAddress(nextNodeI);
					if (nextNode->pos == newNodePos) {
						nodes.deleteAtIndex(nextNodeI);
					}
				}
			}
		}
	}

	if (!nodes.getNumElements()) {
		valueIncrementPerHalfTick = 0; // In case we were interpolating.
	}

	nodes.testSequentiality("E334");
}

void AutoParam::notifyPingpongOccurred() {
	valueIncrementPerHalfTick = -valueIncrementPerHalfTick;
}

void AutoParam::stealNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
                           int32_t loopLength, Action* action, StolenParamNodes* stolenNodeRecord) {

	int32_t stopAt = pos + regionLength;
	int32_t durationAfterWrap = (stopAt - loopLength);

	int32_t searchTerms[2];
	searchTerms[0] = pos;
	searchTerms[1] = stopAt;
	int32_t resultingIndexes[2];

	nodes.searchDual(searchTerms, resultingIndexes);

	int32_t numNodesToStealBeforeWrap = resultingIndexes[1] - resultingIndexes[0];

	int32_t numNodesToStealAfterWrap = 0;

	if (durationAfterWrap > 0) {
		numNodesToStealAfterWrap = nodes.search(durationAfterWrap, GREATER_OR_EQUAL);
	}

	if (stolenNodeRecord) {
		int32_t numNodesToStealTotal = numNodesToStealBeforeWrap + numNodesToStealAfterWrap;

		if (numNodesToStealTotal) {

			if (action) {
				action->recordParamChangeIfNotAlreadySnapshotted(modelStack);
			}

			void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(numNodesToStealTotal * sizeof(ParamNode));
			if (memory) {
				ParamNode* stolenNodes = (ParamNode*)memory;
				stolenNodeRecord->nodes = stolenNodes;
				stolenNodeRecord->num = numNodesToStealTotal;

				int32_t sourceI = resultingIndexes[0];
				int32_t stopAtI = resultingIndexes[1];
				int32_t destI = 0;

goAgain:
				while (sourceI < stopAtI) {
					memcpy(&stolenNodes[destI], nodes.getElementAddress(sourceI), sizeof(ParamNode));
					// stolenNodes[destI] = *(ParamNode*)nodes.getElementAddress(sourceI);

					stolenNodes[destI].pos -= pos;
					if (stolenNodes[destI].pos < 0) {
						stolenNodes[destI].pos += loopLength;
					}

					sourceI++;
					destI++;
				}

				if (stopAtI != numNodesToStealAfterWrap) {
					sourceI = 0;
					stopAtI = numNodesToStealAfterWrap;
					goto goAgain;
				}
			}
		}
	}

	// Now actually delete the source Nodes
	if (numNodesToStealBeforeWrap) {
		nodes.deleteAtIndex(resultingIndexes[0], numNodesToStealBeforeWrap);
	}

	if (numNodesToStealAfterWrap) {
		nodes.deleteAtIndex(0, numNodesToStealAfterWrap);
	}

	nodes.testSequentiality("E424");
}

void AutoParam::insertStolenNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
                                  int32_t loopLength, Action* action, StolenParamNodes* stolenNodeRecord) {

	bool wasAutomatedBefore = isAutomated();

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStack);
	}

	// First, clear the area
	stealNodes(modelStack, pos, regionLength, loopLength, action);

	// This is really inefficient.
	for (int32_t sourceI = 0; sourceI < stolenNodeRecord->num; sourceI++) {
		ParamNode* stolenNode = &stolenNodeRecord->nodes[sourceI];
		if (stolenNode->pos >= regionLength) {
			break; // If our destination region is shorter than that of the stolen nodes
		}
		int32_t destPos = stolenNode->pos + pos;
		if (destPos >= loopLength) {
			destPos -= loopLength;
		}

		int32_t destI = nodes.insertAtKey(destPos);
		if (destI == -1) {
			break;
		}
		ParamNode* destNode = (ParamNode*)nodes.getElementAddress(destI);

		memcpy(destNode, stolenNode, sizeof(ParamNode));
		//*destNode = *stolenNode;
		destNode->pos = destPos;
	}

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, currentValue, true, wasAutomatedBefore,
	                                                          isAutomated());

	nodes.testSequentiality("E423");
}

// Disregards a node that's right at pos.
// Maybe I ought to make this take cut-points into consideration... but I can't see a need yet...
int32_t AutoParam::getDistanceToNextNode(ModelStackWithAutoParam const* modelStack, int32_t pos, bool reversed) {

	int32_t effectiveLength = modelStack->getLoopLength();

	if (!nodes.getNumElements()) {
		return effectiveLength;
	}

	int32_t i = nodes.search(pos + !reversed, GREATER_OR_EQUAL) - reversed;
	if (i == -1) {
		i = nodes.getNumElements() - 1;
	}
	else if (i == nodes.getNumElements()) {
		i = 0;
	}

	ParamNode* node = (ParamNode*)nodes.getElementAddress(i);

	int32_t distance = node->pos - pos;
	if (reversed) {
		distance = -distance;
	}
	if (distance <= 0) {
		distance += effectiveLength;
	}

	return distance;
}

/*
    for (int32_t i = 0; i < nodes.getNumElements(); i++) {
        ParamNode* thisNode = nodes.getElement(i);

    }
 */
