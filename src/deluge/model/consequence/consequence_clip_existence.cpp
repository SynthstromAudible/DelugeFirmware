/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_clip_existence.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "util/misc.h"

ConsequenceClipExistence::ConsequenceClipExistence(Clip* newClip, ClipArray* newClipArray,
                                                   ExistenceChangeType newType) {
	clip = newClip;
	clipArray = newClipArray;
	type = newType;
}

void ConsequenceClipExistence::prepareForDestruction(int32_t whichQueueActionIn, Song* song) {
	if (whichQueueActionIn != util::to_underlying(type)) {
		song->deleteBackedUpParamManagersForClip(clip);

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO) {
			if (((AudioClip*)clip)->recorder) {
				FREEZE_WITH_ERROR("i002"); // Trying to diversify Qui's E278
			}
		}
#endif

		clip->~Clip();
		delugeDealloc(clip);
	}
}

int32_t ConsequenceClipExistence::revert(TimeType time, ModelStack* modelStack) {
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);

	if (time != util::to_underlying(type)) { // (Re-)create

		if (!clipArray->ensureEnoughSpaceAllocated(1)) {
			return ERROR_INSUFFICIENT_RAM;
		}

		int32_t error = clip->undoDetachmentFromOutput(modelStackWithTimelineCounter);
		if (error) { // This shouldn't actually happen, but if it does...
#if ALPHA_OR_BETA_VERSION
			FREEZE_WITH_ERROR("E046");
#endif
			return error; // Run away. This and the Clip(?) will get destructed, and everything should be ok!
		}

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO && !clip->paramManager.summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E419"); // Trying to diversify Leo's E410
		}
#endif

		clipArray->insertClipAtIndex(clip, clipIndex);

		clip->activeIfNoSolo = false;   // So we can toggle it back on, below
		clip->armState = ArmState::OFF; // In case was left on before

		if (shouldBeActiveWhileExistent && !(playbackHandler.playbackState && currentPlaybackMode == &arrangement)) {
			session.toggleClipStatus(clip, &clipIndex, true, 0);
			if (!clip->activeIfNoSolo) {
				D_PRINTLN("still not active!");
			}
		}

		if (!clip->output->activeClip) {
			clip->output->setActiveClip(
			    modelStackWithTimelineCounter); // Must do this to avoid E170 error. If Instrument has no
			                                    // backedUpParamManager, it must have an activeClip
		}
	}

	else { // (Re-)delete

		// Make sure the currentClip isn't left pointing to this Clip. Most of the time, ActionLogger::revertAction()
		// reverts currentClip so we don't have to worry about it - but not if action->currentClip is NULL!
		if (modelStackWithTimelineCounter->song->currentClip == clip) {
			modelStackWithTimelineCounter->song->currentClip = NULL;
		}

		clip->stopAllNotesPlaying(
		    modelStackWithTimelineCounter->song); // Stops any MIDI-controlled auditioning / stuck notes

		shouldBeActiveWhileExistent = session.deletingClipWhichCouldBeAbandonedOverdub(
		    clip); // But should we really be calling this without checking the Clip is a session one?

		clip->abortRecording();
		clip->armState = ArmState::OFF; // Not 100% sure if necessary... probably.

		clipIndex = clipArray->getIndexForClip(clip);
		if (clipIndex == -1) {
			FREEZE_WITH_ERROR("E244");
		}

		if (clipArray == &modelStackWithTimelineCounter->song->sessionClips) {

			// Must unsolo the Clip before we delete it, in case its play-pos needs to be grabbed for another Clip - and
			// also so overall soloing may be cancelled if no others soloing
			if (clip->soloingInSessionMode) {
				session.unsoloClip(clip);
			}

			modelStackWithTimelineCounter->song->removeSessionClipLowLevel(clip, clipIndex);
		}
		else {
			clipArray->deleteAtIndex(clipIndex); // Deletes the array's pointer to the Clip - not the Clip itself.
		}

		// This next call will back up all ParamManagers, including for Drums.
		// But it will (unusually) leave clip->output pointing to the Output, and the same for any NoteRows' Drums. So
		// that we can revert this stuff later.
		Output* oldOutput = clip->output;

		if (clip->isActiveOnOutput() && playbackHandler.isEitherClockActive()) {
			clip->expectNoFurtherTicks(modelStackWithTimelineCounter->song); // Still necessary? Probably.
		}

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO) {
			if (((AudioClip*)clip)->recorder) {
				FREEZE_WITH_ERROR("i003"); // Trying to diversify Qui's E278
			}
		}
#endif

#if ALPHA_OR_BETA_VERSION
		if (clip->type == ClipType::AUDIO && !clip->paramManager.summaries[0].paramCollection) {
			FREEZE_WITH_ERROR("E420"); // Trying to diversify Leo's E410
		}
#endif

		clip->detachFromOutput(modelStackWithTimelineCounter, false, false, true);
		// modelStackWithTimelineCounter may not be used again after this!
		// ------------------------------------------------
		oldOutput->pickAnActiveClipIfPossible(modelStack); // Yup, we're required to call this after detachFromOutput().
	}

	return NO_ERROR;
}
