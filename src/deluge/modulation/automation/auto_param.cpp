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
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h" // M2-C: engine edit ops forward to these DOM kernels
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
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/exceptions.h"
#include "util/functions.h"
#include <math.h>
#include <utility>

#define SAMPLES_TO_CLEAR_AFTER_RECORD 8820          // 200ms
#define SAMPLES_TO_IGNORE_AFTER_BEGIN_OVERRIDE 9200 // 200ms + a bit
#define TIME_TO_INTERPOLATE_WITHIN 6615             // 150ms

// These refer to how many samples to wait *after* the override length, which is specified above.
// And these only apply if we hit a non-interpolated node (which, remember, is all of them for MIDI CCs
#define UNINTERPOLATED_NODE_CANCELS_OVERRIDING_AFTER_SAMPLES                                                           \
	6630 // 150ms. Only seems to have an effect for MIDI, which is confusing me...

// M2-F: AutoParam is a pure interpolation cursor — it holds NO node storage. Node data lives in the DOM,
// resolved per-op via the owner (ModelStackWithAutoParam::doc()). currentValue is the RT cursor.
AutoParam::AutoParam() {
	currentValue = 0;
	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
}

AutoParam::AutoParam(AutoParam const& other) : currentValue(other.currentValue) {
	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
}

AutoParam& AutoParam::operator=(AutoParam const& other) {
	if (this == &other) {
		return *this;
	}
	currentValue = other.currentValue;
	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
	return *this;
}

AutoParam::AutoParam(AutoParam&& other) noexcept
    : currentValue(other.currentValue), valueIncrementPerHalfTick(other.valueIncrementPerHalfTick),
      value_increment_per_half_tick_float(other.value_increment_per_half_tick_float),
      interpolation_increment_remainder_float(other.interpolation_increment_remainder_float),
      renewedOverridingAtTime(other.renewedOverridingAtTime) {
}

AutoParam& AutoParam::operator=(AutoParam&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	currentValue = other.currentValue;
	valueIncrementPerHalfTick = other.valueIncrementPerHalfTick;
	value_increment_per_half_tick_float = other.value_increment_per_half_tick_float;
	interpolation_increment_remainder_float = other.interpolation_increment_remainder_float;
	renewedOverridingAtTime = other.renewedOverridingAtTime;
	return *this;
}

// The DOM node store is resolved from the OWNER, by paramId — no engine object holds node storage.
//
// S1 (toward id-addressable resolution / ModelStack removal): when the owner is the Song itself (song-global
// params, timelineCounter == song), resolve the DOM node from the project tree by a stable `ParamAddress`
// (owner + kind + paramId) instead of the threaded `paramCollection` pointer. Other owners (clips/drums/MIDI)
// fall through to the engine resolution until their slice relocates their storage into the tree. Behaviour-
// identical: for song-global the address resolves to the same `global_fx.params.unpatched` slot the engine
// path returns.
deluge::project::DocAutoParam* ModelStackWithAutoParam::doc() const {
	if (song != nullptr) {
		TimelineCounter* tc = getTimelineCounterAllowNull();
		deluge::project::OwnerId owner{};
		std::optional<uint32_t> clipSerial{}; // for INSTRUMENT/DRUM owners: which clip's override this lane is
		bool haveOwner = false;
		if (tc == static_cast<TimelineCounter*>(song)) {
			owner = deluge::project::OwnerId::song();
			haveOwner = true;
		}
		else if (tc != nullptr) {
			// Song and Clip are the only TimelineCounters, so a non-song tc is a Clip. Classify the owner by the
			// ModelStack context (S3c-S3g): a set noteRow ⇒ a per-drum param (OwnerId::drum, keyed by the drum's
			// serialId); else a synth/kit InstrumentClip ⇒ clip-level instrument-shared (OwnerId::instrument(output));
			// else an AudioClip ⇒ its own params (OwnerId::clip). noteRow is a RELIABLE drum signal — the ModelStack
			// builders always set it explicitly (addNoteRow(0, nullptr) for the no-noteRow path), never stale. Any
			// uncertain case (non-Sound drum, missing serialId) leaves haveOwner false → the engine fallback below.
			Clip* clip = static_cast<Clip*>(tc);
			if (std::optional<uint32_t> clipId = clip->serialId()) {
				NoteRow* noteRow = getNoteRowAllowNull();
				if (noteRow != nullptr) {
					if (noteRow->drum != nullptr && noteRow->drum->type == DrumType::SOUND) {
						if (std::optional<uint32_t> drumId = noteRow->drum->serialId()) {
							owner = deluge::project::OwnerId::drum(*drumId);
							clipSerial = clipId;
							haveOwner = true;
						}
					}
				}
				else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
				         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
					if (std::optional<uint32_t> outId = clip->output->serialId()) {
						owner = deluge::project::OwnerId::instrument(*outId);
						clipSerial = clipId;
						haveOwner = true;
					}
				}
				else {
					owner = deluge::project::OwnerId::clip(*clipId);
					haveOwner = true;
				}
			}
		}
		if (haveOwner) {
			deluge::project::ParamAddress address{owner, paramCollection->getParamKind(), paramId, clipSerial};
			if (deluge::project::DocAutoParam* resolved = song->project.resolveParam(address)) {
				return resolved;
			}
		}
	}
	return paramCollection->docParamForId(paramId);
}

void AutoParam::init(deluge::project::DocAutoParam& doc) {
	doc.nodes.init();
}

void AutoParam::copyOverridingFrom(AutoParam* otherParam) {
	if (otherParam->renewedOverridingAtTime) {
		renewedOverridingAtTime = otherParam->renewedOverridingAtTime;
		resetInterpolationIncrement();
	}
	currentValue = otherParam->currentValue;
}

// This is mostly for "expression" params, which we frequently want to bump back to 0 - often when there is no
// automation, or when playback is stopped.
void AutoParam::setCurrentValueWithNoReversionOrRecording(ModelStackWithAutoParam const* modelStack, int32_t value) {
	int32_t oldValue = currentValue;
	currentValue = value;
	bool automatedNow = isAutomated(*modelStack->doc());
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
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;
	int32_t oldValue = currentValue;
	bool automatedBefore = isAutomated(*modelStack->doc());
	bool automationChanged = false;
	resetInterpolationIncrement();

	bool isPlaying =
	    (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn
	     && (!modelStack->timelineCounterIsSet() || modelStack->getTimelineCounter()->isPlayingAutomationNow()));

	if (isPlaying) {

		// If recording...
		if (playbackHandler.recording != RecordingMode::OFF && modelStack->timelineCounterIsSet()
		    && modelStack->getTimelineCounter()->armedForRecording) {

			// If in record mode and shift button held down, delete automation
			if (Buttons::isShiftButtonPressed()) {

				if (isAutomated(*modelStack->doc())) {
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
				deleteAutomation(nullptr, modelStack);
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

			if (!reversed && std::ssize(nodesRef)) { // Yeah turns out we just don't need the result from this if we're
				// reversed. RIP the work I put into making this code reverse-compatible.
				// The old GREATER_OR_EQUAL/LESS comparison arg was literally an offset added to the lower bound
				int32_t prevNodeI = nodesRef.firstAtOrAfter(livePos + (int32_t)reversed) - (reversed ? 0 : 1);
				if (prevNodeI >= 0 && prevNodeI < std::ssize(nodesRef)) { // If there was a Node before livePos...
investigatePrevNode:
					ParamNode* prevNode = (ParamNode*)&nodesRef[prevNodeI];
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
						prevNodeI = reversed ? 0 : (std::ssize(nodesRef) - 1);
						goto investigatePrevNode;
					}
				}
			}

			// M2-D.3: forward the recording node write to the recordLiveValue DOM kernel — it captures exactly
			// this branch's node mutation (setNodeAtPos for the MPE / zero-clear shortcut, else homogenizeRegion
			// + the reversed further-right-node fixup, then current_value=value). interpolateLeft is the engine's
			// already-decided `reversed || shouldInterpolateRegionStart`. recordingLinearly is only consulted deep
			// in homogenizeRegion's forward-wrap branch (Clip-recording context), so guard the Clip cast against a
			// Song timeline (same reasoning + fix as AutoParam::homogenizeRegion). The engine's leftI==-1
			// early-outs are dead here: the DOM container's inserts are infallible, so the kernel never returns -1.
			bool shouldInterpolateLeft = reversed || shouldInterpolateRegionStart;
			bool recordingLinearly = false;
			if (TimelineCounter* tc = modelStack->getTimelineCounterAllowNull();
			    tc && tc != static_cast<TimelineCounter*>(modelStack->song)) {
				recordingLinearly = static_cast<Clip*>(tc)->getCurrentlyRecordingLinearly();
			}
			int32_t leftI = deluge::project::recordLiveValue(*modelStack->doc(), livePos, value, ticksToClear,
			                                                 doMPEMode, shouldInterpolateLeft, effectiveLength,
			                                                 reversed, recordingLinearly, posAtWhichPlaybackWillCut);

			// Now that we've definitely left any previous nodes alone, see if they form a linear run and we can delete
			// some of them? We'll just not bother with this if reversed, for now... or ever...
			if (mayDeleteNodesInLinearRun && !reversed
			    && modelStack->paramCollection->mayParamInterpolate(modelStack->paramId) && std::ssize(nodesRef) >= 3) {
				bool backtrackingCouldLoopBackToEnd =
				    modelStack->getTimelineCounter()
				        ->backtrackingCouldLoopBackToEnd(); // Wait, I can no longer see why this matters...
				int32_t prevI = leftI - 1;
				if (prevI == -1) {
					if (!backtrackingCouldLoopBackToEnd) {
						goto skipThat;
					}
					prevI = std::ssize(nodesRef) - 1;
				}
				deleteRedundantNodeInLinearRun(*modelStack->doc(), prevI, effectiveLength,
				                               backtrackingCouldLoopBackToEnd);

skipThat: {}
			}

#if ENABLE_SEQUENTIALITY_TESTS
			nodesRef.testSequentiality("ffff");
#endif

			if (!doMPEMode) {
				renewedOverridingAtTime = 1; // Latch - until we come to the next node
			}
			automationChanged = true;

#if ALPHA_OR_BETA_VERSION
			if (!nodesRef.empty()) {
				ParamNode* rightmostNode = &nodesRef[std::ssize(nodesRef) - 1];
				if (rightmostNode->pos >= effectiveLength) {
					FREEZE_WITH_ERROR("llll");
				}
			}
#endif
		}

		// Or if not recording...
		else {
			if (!nodesRef.empty()) {
				renewedOverridingAtTime = AudioEngine::audioSampleTimer;
				if (renewedOverridingAtTime <= 1) {
					renewedOverridingAtTime = 0xFFFFFFFF;
				}
			}
		}
	}

	// If still unautomated (or not currently playing), record value change
	if (nodesRef.empty() || !isPlaying) {
		if (value != currentValue) {
			actionLogger.recordUnautomatedParamChange(modelStack);
		}
	}

getOut:
	currentValue = value;
	bool automatedNow = isAutomated(*modelStack->doc());
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          automatedNow);
}

bool AutoParam::deleteRedundantNodeInLinearRun(deluge::project::DocAutoParam& doc, int32_t lastNodeInRunI,
                                               int32_t effectiveLength, bool mayLoopAroundBackToEnd) {

	if (std::ssize(doc.nodes) < 3) {
		return false;
	}

	ParamNode* lastNodeInRun = &doc.nodes[lastNodeInRunI];

	// But first, now that we've moved on from prevNode, see if prevNode concluded a linear run of nodes for which we
	// can now delete the middle node
	int32_t middleNodeInRunI = lastNodeInRunI - 1;
	if (middleNodeInRunI == -1) {
		if (!mayLoopAroundBackToEnd) {
			return false;
		}
		middleNodeInRunI = std::ssize(doc.nodes) - 1;
	}
	ParamNode* middleNodeInRun = &doc.nodes[middleNodeInRunI];

	if (lastNodeInRun->interpolated || !middleNodeInRun->interpolated) {

		int32_t firstNodeInRunI = middleNodeInRunI - 1;
		if (firstNodeInRunI == -1) {
			if (!mayLoopAroundBackToEnd) {
				return false;
			}
			firstNodeInRunI = std::ssize(doc.nodes) - 1;
		}
		ParamNode* firstNodeInRun = &doc.nodes[firstNodeInRunI];

		if (middleNodeInRun->value == firstNodeInRun->value
		    && (middleNodeInRun->value == lastNodeInRun->value || !middleNodeInRun->interpolated)) {
removeMiddleNodeInRun:
			doc.nodes.erase(doc.nodes.begin() + middleNodeInRunI);
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
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	bool wasAutomated = isAutomated(*modelStack->doc());

	if (action) {
		action->recordParamChangeIfNotAlreadySnapshotted(modelStack, true);
	}

	else {
		nodesRef.clear();
	}

	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;

	if (shouldNotify && wasAutomated) {
		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, getCurrentValue(), true, true, false);
	}
}

// Beware. As this will do no notifying, the caller must ensure that any required notification is done.
// I.e. a ParamSet must be notified if automation is deleted.
void AutoParam::deleteAutomationBasicForSetup(deluge::project::DocAutoParam& doc) {
	doc.nodes.clear();
	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
}

#define OVERRIDE_DURATION_MAGNITUDE_INTERPOLATING 15 // Means 2^x audio samples in length

int32_t AutoParam::processCurrentPos(ModelStackWithAutoParam const* modelStack, bool reversed, bool didPinpong,
                                     bool mayInterpolate, bool mustUpdateValueAtEveryNode) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	// If no automation...
	if (nodesRef.empty()) {
		return 2147483647;
	}

	int32_t currentPos = modelStack->getLastProcessedPos();
	int32_t effectiveLength = modelStack->getLoopLength();

	// Find next node - here or further along in our direction
	// The search "direction" is an offset on the lower-bound index: -1 when reversed (the old LESS), else 0
	int32_t searchDirection = -(int32_t)reversed;
	int32_t searchPos = currentPos + (int32_t)reversed;
	int32_t iJustReached = nodesRef.firstAtOrAfter(searchPos) + searchDirection;
	if (iJustReached < 0) {
		iJustReached += std::ssize(nodesRef);
	}
	else if (iJustReached >= std::ssize(nodesRef)) {
		iJustReached = 0;
	}

	ParamNode* nodeJustReached = &nodesRef[iJustReached];
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
	resetInterpolationIncrement();

	// Now start thinking about the *next* node, which we'll get to in a while
	int32_t iRight = iJustReached + 1;
	if (iRight >= std::ssize(nodesRef)) {
		iRight = 0;
	}

	ParamNode* nextNodeInOurDirection;

	if (reversed) {
		int32_t iLeft = iJustReached - 1;
		if (iLeft < 0) {
			iLeft += std::ssize(nodesRef);
		}
		ParamNode* nodeToLeft = &nodesRef[iLeft];

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
		ParamNode* nodeToRight = &nodesRef[iRight];
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
						if (nodesRef.firstAtOrAfter(1) - 1 >= 0) {
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
					    *modelStack->doc(), iJustReached,
					    effectiveLength); // Shouldn't I make it so this doesn't get called for MPE?
				}

				if (insertingNodeAtEndOfClearing) {
					int32_t iNew = nodesRef.insertSorted(posOverridingEnds)
					                   .value_or(-1); // Can only do this now, after updating nodeJustReached, above
					if (iNew != -1) {
						iRight = iNew;
						nextNodeInOurDirection = &nodesRef[iRight];
						nextNodeInOurDirection->value = valueOverridingEnds;
						nextNodeInOurDirection->interpolated = newNodeShouldBeInterpolated;

						/*
						D_PRINTLN("new one:  %d ,  %d", posOverridingEnds, valueOverridingEnds);
						*/
						if (!reversed) {
							needToReGetNextNode = deleteRedundantNodeInLinearRun(
							    *modelStack->doc(), iRight,
							    effectiveLength); // If returns false, storage wasn't changed, and we've got the next
							                      // node right here! Shouldn't I make it so this doesn't get called for
							                      // MPE?
						}
					}
				}

				// Figure out what's the next node, again - because we just possibly deleted a node, and that's possibly
				// changed the storage
				if (needToReGetNextNode) { // Not doing this if reversed.
					iRight = nodesRef.firstAtOrAfter(currentPos + 1);
					if (iRight == std::ssize(nodesRef)) {
						iRight = 0;
					}
					nextNodeInOurDirection = &nodesRef[iRight];
				}

#if ENABLE_SEQUENTIALITY_TESTS
				nodesRef.testSequentiality("eeee");
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
		if (!isAutomated(*modelStack->doc())) {
			FREEZE_WITH_ERROR("E372");
		}
#endif
		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, true, true);
	}

	if (mayInterpolate) {
		if ((reversed ? nodeJustReached : nextNodeInOurDirection)->interpolated) {
			setupInterpolation(modelStack, nextNodeInOurDirection, effectiveLength, currentPos, reversed);
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
	if (!nodesRef.empty()) {
		int32_t i = std::ssize(nodesRef) - 1;
		ParamNode* rightmostNode = &nodesRef[i];
		if (rightmostNode->pos >= effectiveLength) {
			nodesRef.erase(nodesRef.begin() + i);
			// FREEZE_WITH_ERROR("jjjj"); // drbourbon got! And Quixotic7, on V4.0.0-beta8.
		}
	}

	return ticksTilNextNode;
}

/// identifies if a parameter is currently interpolating
/// the value increment is used to increment / decrement the parameters current value
bool AutoParam::hasInterpolationIncrement() {
	return valueIncrementPerHalfTick != 0 || value_increment_per_half_tick_float != 0.0f;
}

/// resets interpolation increment to zero (can happen if we reached parameter value limits)
/// or if we've reached another node in which case we'll be re-calculating the increment
void AutoParam::resetInterpolationIncrement() {
	valueIncrementPerHalfTick = 0;
	value_increment_per_half_tick_float = 0.0f;
	interpolation_increment_remainder_float = 0.0f;
}

/// reverse the interpolation increment in case we're now interpolating backwards to the previous node
void AutoParam::reverseInterpolationIncrement() {
	valueIncrementPerHalfTick = -valueIncrementPerHalfTick;
	value_increment_per_half_tick_float = -value_increment_per_half_tick_float;
	interpolation_increment_remainder_float = -interpolation_increment_remainder_float;
}

/// identifies if the current parameter should use float interpolation (e.g. tempo)
/// used with parameters that require more accuracy and/or have a smaller internal value range
bool AutoParam::useFloatInterpolation(ModelStackWithAutoParam const* modelStack) {
	return modelStack->paramCollection->shouldInterpolateWithFloat(modelStack);
}

// You now much check before calling this that interpolation should happen at all
void AutoParam::setupInterpolation(ModelStackWithAutoParam const* modelStack, ParamNode* nextNodeInOurDirection,
                                   int32_t effectiveLength, int32_t currentPos, bool reversed) {

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

	bool use_float_interpolation = useFloatInterpolation(modelStack);

	calculateInterpolationIncrement(halfDistance, ticksTilNextNode, use_float_interpolation);

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
			potentiallyOverrideInterpolationIncrement(limit, use_float_interpolation);
		}
	}
}

/// calculates the amount to increment / decrement the current parameter value to interpolate to
/// the next node's parameter value
void AutoParam::calculateInterpolationIncrement(int32_t half_distance, int32_t ticks_til_next_node,
                                                bool use_float_interpolation) {
	if (use_float_interpolation) [[unlikely]] {
		value_increment_per_half_tick_float = (float)half_distance / (float)ticks_til_next_node;
	}
	else {
		valueIncrementPerHalfTick = half_distance / ticks_til_next_node;
	}
}

/// used to limit how fast you interpolate
void AutoParam::potentiallyOverrideInterpolationIncrement(int32_t limit, bool use_float_interpolation) {
	if (use_float_interpolation) [[unlikely]] {
		if (value_increment_per_half_tick_float > (float)limit) {
			value_increment_per_half_tick_float = (float)limit;
		}
		else if (value_increment_per_half_tick_float < (float)-limit) {
			value_increment_per_half_tick_float = (float)-limit;
		}
		else {
			// If we didn't even have to limit it, there's no need to be overriding anymore
			renewedOverridingAtTime = 0;
		}
	}
	else {
		if (valueIncrementPerHalfTick > limit) {
			valueIncrementPerHalfTick = limit;
		}
		else if (valueIncrementPerHalfTick < -limit) {
			valueIncrementPerHalfTick = -limit;
		}
		else {
			// If we didn't even have to limit it, there's no need to be overriding anymore
			renewedOverridingAtTime = 0;
		}
	}
}

/// used with float interpolation to accumulate increments between whole values
/// so that you know when to increment the current parameter value by a whole value
int32_t AutoParam::consumeFloatInterpolationIncrement(float value_increment) {
	// accumulate float increments
	interpolation_increment_remainder_float += value_increment;

	// truncate the accumulated float increment so it's just the whole value
	// (e.g. round 1.1 down to 1, or round 0.1 down to 0)
	float whole_value_increment = truncf(interpolation_increment_remainder_float);

	// subtract whole value from the accumulated amount to get the remainder (e.g. 1.1 - 1 = .1)
	interpolation_increment_remainder_float -= whole_value_increment;

	// return whole value increment (e.g. 1 or 0)
	return (int32_t)whole_value_increment;
}

/// applies the whole value increment and resets interpolation increment if we've reached current value limits
bool AutoParam::applyValueIncrement(int32_t value_increment) {
	// if increment is 0, return false --> current value won't change
	if (value_increment == 0) {
		return false;
	}

	// store current value (so we can check if it changed below)
	int32_t oldValue = currentValue;

	// add value increment, clamp within the int32 limits (-2147483648, 2147483647)
	currentValue = add_saturate(currentValue, value_increment);

	// check if overflow occurred (e.g. increment would push current value over limit)
	bool overflow_occurred = (value_increment > 0 && oldValue > INT32_MAX - value_increment)
	                         || (value_increment < 0 && oldValue < INT32_MIN - value_increment);

	// if overflow occurred, reset interpolation increment
	if (overflow_occurred) {
		resetInterpolationIncrement();
	}

	// return if we changed the current value
	return (currentValue != oldValue);
}

bool AutoParam::tickSamples(int32_t numSamples) {
	// if we don't have any interpolation to apply, return false
	if (!hasInterpolationIncrement()) {
		return false;
	}

	int32_t value_increment = 0;

	// non-float interpolation
	if (valueIncrementPerHalfTick != 0) [[likely]] {
		value_increment =
		    multiply_32x32_rshift32_rounded(valueIncrementPerHalfTick, playbackHandler.getTimePerInternalTickInverse())
		    * 6 * numSamples;
	}
	// float interpolation
	else {
		float half_ticks =
		    (float)playbackHandler.getTimePerInternalTickInverse() * (1.0f / 4294967296.0f) * 6.0f * numSamples;
		value_increment = consumeFloatInterpolationIncrement(value_increment_per_half_tick_float * half_ticks);
	}

	// potentially update current value
	// return if we've changed the current value
	return applyValueIncrement(value_increment);
}

bool AutoParam::tickTicks(int32_t numTicks) {
	// if we don't have any interpolation to apply, return false
	if (!hasInterpolationIncrement()) {
		return false;
	}

	int32_t value_increment = 0;
	int32_t half_ticks = numTicks * 2;

	// non-float interpolation
	if (valueIncrementPerHalfTick != 0) [[likely]] {
		value_increment = valueIncrementPerHalfTick * half_ticks;
	}
	// float interpolation
	else {
		value_increment = consumeFloatInterpolationIncrement(value_increment_per_half_tick_float * (float)half_ticks);
	}

	// potentially update current value
	// return if we've changed the current value
	return applyValueIncrement(value_increment);
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
	// M2-C: keep the whole wiring seam (Action, whole-loop deleteAutomation, notify); forward only the partial
	// region erase to the DOM kernel (value sync-bridge, since it may zero current_value when nothing remains).
	if (!isAutomated(*modelStack->doc())) {
		return;
	}
	int32_t oldValue = currentValue;
	int32_t effectiveLength = modelStack->getLoopLength();
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (length >= effectiveLength) {
		deleteAutomation(action, modelStack); // whole loop — keep the engine's own snapshot/notify path
	}
	else {
		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStack, false);
		}
		deluge::project::DocAutoParam* doc = modelStack->doc();
		doc->current_value = currentValue;
		deluge::project::deleteNodesWithinRegion(*doc, pos, length, effectiveLength);
		currentValue = doc->current_value;
	}
	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, true, true,
	                                                          isAutomated(*modelStack->doc()));
}

int32_t AutoParam::setNodeAtPos(deluge::project::DocAutoParam& doc, int32_t pos, int32_t value,
                                bool shouldInterpolate) {
	// M2-C: forward the node mutation to the DOM kernel (oracle-proven identical). The kernel is infallible on
	// this container (index-based insert), so unlike the old engine op it never returns -1.
	return deluge::project::setNodeAtPos(doc, pos, value, shouldInterpolate);
}

void AutoParam::setValueForRegion(uint32_t pos, uint32_t length, int32_t value,
                                  ModelStackWithAutoParam const* modelStack, ActionType actionType) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	int32_t oldValue = currentValue;
	bool automatedBefore = isAutomated(*modelStack->doc());
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
		if (isAutomated(*modelStack->doc())) {
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
		nodesRef.testSequentiality("E441");
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
		mostRecentI = nodesRef.firstAtOrAfter(modelStack->getLivePos() + !modelStack->isCurrentlyPlayingReversed()) - 1;
		if (mostRecentI == -1) {
			mostRecentI = std::ssize(nodesRef) - 1;
		}
		if (mostRecentI == firstI) {
			resetInterpolationIncrement();
yesChangeCurrentValue:
			currentValue = value;
		}
		else {
			view.notifyParamAutomationOccurred(modelStack->paramManager);
		}
	}

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, automationChanged, automatedBefore,
	                                                          isAutomated(*modelStack->doc()));
}

#define REGION_EDGE_LEFT 0
#define REGION_EDGE_RIGHT 1

// Returns index of leftmost node of region, or -1 if error
int32_t AutoParam::homogenizeRegion(ModelStackWithAutoParam const* modelStack, int32_t startPos, int32_t length,
                                    int32_t startValue, bool interpolateLeftNode, bool interpolateRightNode,
                                    int32_t effectiveLength, bool reversed, int32_t posAtWhichClipWillCut) {
	// M2-C: forward to the DOM kernel. The engine read recordingLinearly by deref-ing the timeline counter as
	// a Clip, but only inside the forward-wrap-no-cut branch, where the counter is a real *Clip* (recording
	// context). getCurrentlyRecordingLinearly() is pure-virtual on Clip only — the Song (arrangement) as
	// timeline counter is NOT a Clip, so the cast must be guarded: the song-as-timeline case never reaches that
	// branch (its effectiveLength is 2147483647, so anyWrap is false and the flag goes unused), so false is
	// correct there. Guard on `tc != song` (the codebase's own song-as-timeline test) to avoid an invalid
	// Song->Clip cast + out-of-bounds read. (Bug caught by the param_record_diff harness — the oracle used a
	// NoteRow stack and never hit a Song timeline.)
	bool recordingLinearly = false;
	TimelineCounter* tc = modelStack->getTimelineCounterAllowNull();
	if (tc && tc != static_cast<TimelineCounter*>(modelStack->song)) {
		recordingLinearly = static_cast<Clip*>(tc)->getCurrentlyRecordingLinearly();
	}
	return deluge::project::homogenizeRegion(*modelStack->doc(), startPos, length, startValue, interpolateLeftNode,
	                                         interpolateRightNode, effectiveLength, reversed, recordingLinearly,
	                                         posAtWhichClipWillCut);
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
	// M2-D.1: forward to the DOM read kernel. getLoopLength() is safe to fetch eagerly (a length, not a
	// null-deref; processCurrentPos already reads it unconditionally). The kernel special-cases the infinite
	// length (2147483647) exactly as the engine did — the oracle covers both finite + infinite.
	return deluge::project::getValueAtPos(*modelStack->doc(), pos, modelStack->getLoopLength(), reversed);
}

// Returns whether a change was made to currentValue
bool AutoParam::grabValueFromPos(uint32_t pos, ModelStackWithAutoParam const* modelStack) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;
	if (nodesRef.empty()) {
		return false;
	}

	int32_t oldValue = currentValue;
	currentValue = getValueAtPos(pos, modelStack);
	return (currentValue != oldValue);
}

void AutoParam::setPlayPos(uint32_t pos, ModelStackWithAutoParam const* modelStack, bool reversed) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	resetInterpolationIncrement(); // We may calculate this, below
	renewedOverridingAtTime = 0;
	if (!nodesRef.empty()) {
		int32_t oldValue = currentValue;
		currentValue = getValueAtPos(pos, modelStack, reversed);

		// Get next node
		int32_t rightI = nodesRef.firstAtOrAfter(pos + (int32_t)!reversed);
		if (rightI == std::ssize(nodesRef)) {
			rightI = 0;
		}
		ParamNode* nextNodeOurDirection =
		    &nodesRef[rightI]; // This will initially point to the node to the right, regardless of direction;
		                       // it'll be corrected to left if we're reversed below.

		if (nextNodeOurDirection->interpolated) {

			if (reversed) {
				int32_t leftI = rightI - 1;
				if (leftI < 0) {
					leftI += std::ssize(nodesRef);
				}
				nextNodeOurDirection = &nodesRef[leftI];
			}

			// Setup interpolation from the pos we're at now
			setupInterpolation(modelStack, nextNodeOurDirection, modelStack->getLoopLength(), pos, reversed);
		}

		modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValue, false, true, true);
	}
}

void AutoParam::flattenReverse(deluge::project::DocAutoParam& doc, int32_t reverseDirectionWithLength) {
	deluge::project::flattenReverse(doc, reverseDirectionWithLength); // M2-C: forward to the DOM kernel
}

// Wait, surely this should be undoable?
void AutoParam::generateRepeats(deluge::project::DocAutoParam& doc, uint32_t oldLength, uint32_t newLength,
                                bool shouldPingpong) {
	deluge::project::generateRepeats(doc, oldLength, newLength, shouldPingpong); // M2-C: forward to DOM kernel
}

void AutoParam::appendParam(deluge::project::DocAutoParam& doc, deluge::project::DocAutoParam const& srcDoc,
                            int32_t oldLength, int32_t reverseThisRepeatWithLength, bool pingpongingGenerally) {
	// M2-C: forward to the DOM kernel; the source param's nodes come from its DocAutoParam handle.
	deluge::project::appendParam(doc, srcDoc, oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
}

void AutoParam::trimToLength(uint32_t newLength, Action* action, ModelStackWithAutoParam const* modelStack) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	// If no nodes, nothing to do
	if (nodesRef.empty()) {
		return;
	}

	// If final node is within new length, also nothing to do
	ParamNode* lastNode = nodesRef.tryGetLast();
	if (lastNode) { // Should always be one...
		if (lastNode->pos < newLength) {
			return;
		}
	}

	// To ensure that the effective value at pos 0 remains the same even after earlier nodes deleted, we might need to
	// add a new, non-interpolating node there.
	bool needNewNodeAt0 = nodesRef.tryGet(0)->pos; // Deactivated for now, but I'm going to enable in the ModelStacks
	                                               // branch, where we have a TimelineCounter here.
	int32_t oldValueAt0;
	if (needNewNodeAt0) {
		oldValueAt0 = getValueAtPos(0, modelStack);
	}

	int32_t newNumNodes = nodesRef.firstAtOrAfter(newLength);

	if (ALPHA_OR_BETA_VERSION && newNumNodes >= std::ssize(nodesRef)) {
		FREEZE_WITH_ERROR("E315");
	}

	// If still at least 2 nodes afterwards (1 is not allowed, actually wait it is now but let's keep this safe for
	// now)...
	if (newNumNodes >= 2) {

		// If no action, just basic trim
		if (!action) {
basicTrim: {
	int32_t numToDelete = std::ssize(nodesRef) - newNumNodes; // Will always be >= 1
	nodesRef.erase(nodesRef.begin() + newNumNodes, nodesRef.begin() + newNumNodes + numToDelete);
}

addNewNodeAt0IfNecessary:
			if (needNewNodeAt0) {
				Error error = nodesRef.insertAt(0).error_or(Error::NONE);
				if (error == Error::NONE) { // Should be fine cos we just deleted some, so some free RAM
					ParamNode* newNode = &nodesRef[0];
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
				Error error = newNodes.insertAt(0, newNumNodes).error_or(Error::NONE);
				if (error != Error::NONE) {
					goto basicTrim;
				}

				for (int32_t i = 0; i < newNumNodes; i++) {
					ParamNode* __restrict__ sourceNode = &nodesRef[i];
					ParamNode* __restrict__ destNode = &newNodes[i];

					*destNode = *sourceNode;
				}

				// We've kept the original Nodes separate in memory, so can steal them into an undo-accessible snapshot
				action->recordParamChangeDefinitely(modelStack, true); // Steal

				// And, need to swap the new Nodes in
				nodesRef.swap(newNodes);

				goto addNewNodeAt0IfNecessary;
			}
		}
	}

	// Or if no nodes afterwards
	else {
		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStack, true); // Steal
		}
		nodesRef.clear();              // Delete them - either if no action, or if the above chose not to steal them.
		resetInterpolationIncrement(); // In case we were interpolating.
	}
}

void AutoParam::writeToFile(deluge::project::DocAutoParam const& doc, Serializer& writer, bool writeAutomation,
                            int32_t* valueForOverride) {
	char buffer[9];

	writer.write("0x");

	int32_t valueNow = (valueForOverride && isAutomated(doc)) ? *valueForOverride : currentValue;

	intToHex(valueNow, buffer);
	writer.write(buffer);

	if (writeAutomation) {

		for (int32_t i = 0; i < std::ssize(doc.nodes); i++) {
			const ParamNode* thisNode = &doc.nodes[i];
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
Error AutoParam::readFromFile(deluge::project::DocAutoParam& doc, Deserializer& reader, int32_t readAutomationUpToPos) {

	// Must first delete any automation because sometimes, due to that annoying support I have to do for late-2016
	// files, we'll be overwriting a cloned ParamManager, which might have had automation.
	deleteAutomationBasicForSetup(doc);

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
					(void)doc.nodes.reserveExtra(
					    numElementsToAllocateFor); // If this fails... oh well. We'll fail later
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
					ParamNode* firstNode = doc.nodes.tryGet(0);
					if (!firstNode || firstNode->pos) {
						Error error = doc.nodes.insertAt(0).error_or(Error::NONE);
						if (error != Error::NONE) {
							return error;
						}
						firstNode = &doc.nodes[0];
						firstNode->pos = 0;
						firstNode->value = value;
						firstNode->interpolated = interpolated;
					}
				}
				break;
			}

			prevPos = pos;

			int32_t nodeI = doc.nodes.insertSorted(pos, true).value_or(-1);
			if (nodeI == -1) {
				return Error::INSUFFICIENT_RAM;
			}
			ParamNode* node = &doc.nodes[nodeI];
			node->value = value;
			node->interpolated = interpolated;

			numElementsToAllocateFor--;
		}
	}

	return Error::NONE;
}

bool AutoParam::containsSomething(const deluge::project::DocAutoParam& doc, uint32_t neutralValue) {
	if (isAutomated(doc)) {
		return true;
	}
	uint32_t* a = (uint32_t*)&currentValue;
	return (*a != (uint32_t)neutralValue);
}

bool AutoParam::containedSomethingBefore(bool wasAutomatedBefore, uint32_t valueBefore, uint32_t neutralValue) {
	return (wasAutomatedBefore || valueBefore != neutralValue);
}

// M2-C sync-bridge: the kernel mutates DocAutoParam::current_value (the DOM base), but the engine's live RT
// cursor is the separate `currentValue` member (kept distinct until M2-E). So for value-touching ops we sync
// currentValue → doc_->current_value, run the kernel, then read it back. (doc_->current_value is otherwise
// engine-dead in M2-B, so this is a safe scratch use; the node vector is shared directly.)
void AutoParam::shiftValues(deluge::project::DocAutoParam& doc, int32_t offset) {
	doc.current_value = currentValue;
	deluge::project::shiftValues(doc, offset);
	currentValue = doc.current_value;
}

void AutoParam::shiftParamVolumeByDB(deluge::project::DocAutoParam& doc, float offset) {
	doc.current_value = currentValue; // M2-C sync-bridge (see shiftValues)
	deluge::project::shiftParamVolumeByDB(doc, offset);
	currentValue = doc.current_value;
}

void AutoParam::shiftHorizontally(deluge::project::DocAutoParam& doc, int32_t amount, int32_t effectiveLength) {
	deluge::project::shiftHorizontally(doc, amount, effectiveLength); // M2-C: forward to the DOM kernel
}

void AutoParam::swapState(AutoParamState* state, ModelStackWithAutoParam const* modelStack) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;
	bool automatedBefore = isAutomated(*modelStack->doc());

	int32_t oldValueHere = currentValue;
	currentValue = state->value;
	state->value = oldValueHere;
	nodesRef.swap(state->nodes);

	bool automatedNow = isAutomated(*modelStack->doc());

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, oldValueHere, true, automatedBefore,
	                                                          automatedNow);
}

void AutoParam::paste(int32_t startPos, int32_t endPos, float scaleFactor, ModelStackWithAutoParam const* modelStack,
                      CopiedParamAutomation* copiedParamAutomation, bool isPatchCable) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	bool automatedBefore = isAutomated(*modelStack->doc());
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
	int32_t iDeleteBegin = nodesRef.firstAtOrAfter(startPos);
	int32_t iDeleteEnd = nodesRef.firstAtOrAfter(endPos);
	int32_t numToDelete = iDeleteEnd - iDeleteBegin;
	if (numToDelete > 0) {
		nodesRef.erase(nodesRef.begin() + iDeleteBegin, nodesRef.begin() + iDeleteBegin + numToDelete);
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
			int32_t resetI = nodesRef.firstAtOrAfter(resetPos);
			ParamNode* resetNode = nodesRef.tryGet(resetI);

			if (resetNode == nullptr || resetNode->pos != resetPos) {
				bool previousNodeInterpolated = resetNode != nullptr ? resetNode->interpolated : true;
				auto error = nodesRef.insertAt(resetI).error_or(Error::NONE);
				if (error != Error::NONE) {
					return;
				}

				resetNode = &nodesRef[resetI];
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

			int32_t resetI = nodesRef.firstAtOrAfter(resetPos);
			ParamNode* resetNode = nodesRef.tryGet(resetI);

			if (!resetNode || resetNode->pos != resetPos) {
				auto error = nodesRef.insertAt(resetI).error_or(Error::NONE);
				if (error != Error::NONE) {
					return;
				}

				resetNode = &nodesRef[resetI];
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

		int32_t nodeDestI = nodesRef.insertSorted(newPos).value_or(-1);
		ParamNode* nodeDest = nodesRef.tryGet(nodeDestI);
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

	nodesRef.testSequentiality("E440");

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, currentValue, true, automatedBefore,
	                                                          isAutomated(*modelStack->doc()));
}

void AutoParam::copy(int32_t startPos, int32_t endPos, CopiedParamAutomation* copiedParamAutomation, bool isPatchCable,
                     ModelStackWithAutoParam const* modelStack) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	// And if any of them are in the right zone...
	int32_t startI = nodesRef.firstAtOrAfter(startPos);
	int32_t endI = nodesRef.firstAtOrAfter(endPos);

	copiedParamAutomation->width = endPos - startPos;

	copiedParamAutomation->numNodes = endI - startI;

	bool insertingExtraNodeAtStart = false;

	if (copiedParamAutomation->numNodes) {
		ParamNode* firstNode = &nodesRef[startI];
		if (firstNode->pos != startPos) {
			insertingExtraNodeAtStart = true;
			copiedParamAutomation->numNodes++;
		}
	}

	if (copiedParamAutomation->numNodes > 0) {
		// Allocate some memory for the nodes
		if (copiedParamAutomation->nodes != nullptr) {
			deluge::memory::dealloc(copiedParamAutomation->nodes);
		}

		copiedParamAutomation->nodes =
		    (ParamNode*)deluge::memory::alloc_sdram(sizeof(ParamNode) * copiedParamAutomation->numNodes);

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
			ParamNode* nodeToCopy = &nodesRef[readingNodeI];
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
Error AutoParam::makeInterpolationGoodAgain(deluge::project::DocAutoParam& doc, int32_t clipLength,
                                            int32_t quantizationRShift) {
	// M2-C: forward to the DOM kernel; it returns false only on the engine's INSUFFICIENT_RAM path.
	return deluge::project::makeInterpolationGoodAgain(doc, clipLength, quantizationRShift) ? Error::NONE
	                                                                                        : Error::INSUFFICIENT_RAM;
}

void AutoParam::transposeCCValuesToChannelPressureValues(deluge::project::DocAutoParam& doc) {
	doc.current_value = currentValue; // M2-C sync-bridge (see shiftValues)
	deluge::project::transposeCCValuesToChannelPressure(doc);
	currentValue = doc.current_value;
}

/// this is used in arranger view to delete time between automation nodes (shift + <>)
void AutoParam::deleteTime(int32_t startPos, int32_t lengthToDelete, ModelStackWithAutoParam* modelStack) {
	// M2-C: forward to the DOM kernel (revertability Action is handled by the caller, per the original note).
	deluge::project::deleteTime(*modelStack->doc(), startPos, lengthToDelete, modelStack->getLoopLength());
}

/// this is used in arranger view to insert time between automation nodes (shift + <>)
void AutoParam::insertTime(deluge::project::DocAutoParam& doc, int32_t pos, int32_t lengthToInsert) {
	deluge::project::insertTime(doc, pos, lengthToInsert); // M2-C: forward to the DOM kernel
}

// Offset must be either 1 or -1.
void AutoParam::moveRegionHorizontally(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t length,
                                       int32_t offset, int32_t lengthBeforeLoop, Action* action) {
	// M2-C: keep the Action-snapshot wiring seam; forward the node mutation to the DOM kernel. The empty-gate
	// stays here so we don't snapshot an Action when there's nothing to move (matching the engine order).
	if (modelStack->doc()->nodes.empty()) {
		return;
	}
	if (action) {
		action->recordParamChangeDefinitely(modelStack, false);
	}
	deluge::project::moveRegionHorizontally(*modelStack->doc(), pos, length, offset, lengthBeforeLoop);
}

void AutoParam::nudgeNonInterpolatingNodesAtPos(int32_t pos, int32_t offset, int32_t lengthBeforeLoop, Action* action,
                                                ModelStackWithAutoParam const* modelStack) {
	// M2-C: the Action snapshot fires whenever a node exists at pos (engine gate), regardless of whether the
	// node is interpolated; forward the node mutation to the DOM kernel, then keep the RT interpolation reset.
	if (action && modelStack->doc()->nodes.find_key(pos) != modelStack->doc()->nodes.end()) {
		action->recordParamChangeDefinitely(modelStack, false);
	}
	deluge::project::nudgeNonInterpolatingNodesAtPos(*modelStack->doc(), pos, offset, lengthBeforeLoop);
	if (modelStack->doc()->nodes.empty()) {
		resetInterpolationIncrement(); // In case we were interpolating.
	}
}

void AutoParam::notifyPingpongOccurred() {
	reverseInterpolationIncrement();
}

void AutoParam::stealNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
                           int32_t loopLength, Action* action, StolenParamNodes* stolenNodeRecord) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	int32_t stopAt = pos + regionLength;
	int32_t durationAfterWrap = (stopAt - loopLength);

	int32_t searchTerms[2];
	searchTerms[0] = pos;
	searchTerms[1] = stopAt;
	int32_t resultingIndexes[2];

	nodesRef.searchDual(searchTerms, resultingIndexes);

	int32_t numNodesToStealBeforeWrap = resultingIndexes[1] - resultingIndexes[0];

	int32_t numNodesToStealAfterWrap = 0;

	if (durationAfterWrap > 0) {
		numNodesToStealAfterWrap = nodesRef.firstAtOrAfter(durationAfterWrap);
	}

	if (stolenNodeRecord) {
		int32_t numNodesToStealTotal = numNodesToStealBeforeWrap + numNodesToStealAfterWrap;

		if (numNodesToStealTotal) {

			if (action) {
				action->recordParamChangeIfNotAlreadySnapshotted(modelStack);
			}

			void* memory = deluge::memory::alloc_fast(numNodesToStealTotal * sizeof(ParamNode));
			if (memory) {
				ParamNode* stolenNodes = (ParamNode*)memory;
				stolenNodeRecord->nodes = stolenNodes;
				stolenNodeRecord->num = numNodesToStealTotal;

				int32_t sourceI = resultingIndexes[0];
				int32_t stopAtI = resultingIndexes[1];
				int32_t destI = 0;

goAgain:
				while (sourceI < stopAtI) {
					memcpy(&stolenNodes[destI], &nodesRef[sourceI], sizeof(ParamNode));
					// stolenNodes[destI] = *(ParamNode*)&nodes[sourceI];

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
		nodesRef.erase(nodesRef.begin() + resultingIndexes[0],
		               nodesRef.begin() + resultingIndexes[0] + numNodesToStealBeforeWrap);
	}

	if (numNodesToStealAfterWrap) {
		nodesRef.erase(nodesRef.begin() + 0, nodesRef.begin() + 0 + numNodesToStealAfterWrap);
	}

	nodesRef.testSequentiality("E424");
}

void AutoParam::insertStolenNodes(ModelStackWithAutoParam const* modelStack, int32_t pos, int32_t regionLength,
                                  int32_t loopLength, Action* action, StolenParamNodes* stolenNodeRecord) {
	// M2-E: resolve the DOM node store via the ModelStack (== nodes() in E1; owner-rooted in E3).
	auto& nodesRef = modelStack->doc()->nodes;

	bool wasAutomatedBefore = isAutomated(*modelStack->doc());

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

		int32_t destI = nodesRef.insertSorted(destPos).value_or(-1);
		if (destI == -1) {
			break;
		}
		ParamNode* destNode = (ParamNode*)&nodesRef[destI];

		memcpy(destNode, stolenNode, sizeof(ParamNode));
		//*destNode = *stolenNode;
		destNode->pos = destPos;
	}

	modelStack->paramCollection->notifyParamModifiedInSomeWay(modelStack, currentValue, true, wasAutomatedBefore,
	                                                          isAutomated(*modelStack->doc()));

	nodesRef.testSequentiality("E423");
}

// Disregards a node that's right at pos.
// Maybe I ought to make this take cut-points into consideration... but I can't see a need yet...
int32_t AutoParam::getDistanceToNextNode(ModelStackWithAutoParam const* modelStack, int32_t pos, bool reversed) {
	// M2-C: forward to the DOM read kernel, supplying the timeline context (loop length) it takes explicitly.
	return deluge::project::getDistanceToNextNode(*modelStack->doc(), pos, modelStack->getLoopLength(), reversed);
}

/*
    for (int32_t i = 0; i < std::ssize(nodes); i++) {
        ParamNode* thisNode = &nodes[i];

    }
 */
