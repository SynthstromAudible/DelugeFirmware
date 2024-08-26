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

#include "model/drum/drum.h"

#include "definitions_cxx.hpp"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <string.h>

Drum::Drum(DrumType newType) : type(newType), arpeggiator(), arpSettings() {
	next = nullptr;

	earlyNoteVelocity = 0;
	earlyNoteStillActive = false;

	auditioned = false;
	lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE;

	kit = NULL;

	memset(lastExpressionInputsReceived, 0, sizeof(lastExpressionInputsReceived));
}

void Drum::writeMIDICommandsToFile(Serializer& writer) {
	midiInput.writeNoteToFile(writer, "midiInput");
	muteMIDICommand.writeNoteToFile(writer, "midiMuteCommand");
}

bool Drum::readDrumTagFromFile(Deserializer& reader, char const* tagName) {

	if (!strcmp(tagName, "midiMuteCommand")) {
		muteMIDICommand.readNoteFromFile(reader);
		reader.exitTag();
	}

	else if (!strcmp(tagName, "midiInput")) {
		midiInput.readNoteFromFile(reader);
		reader.exitTag();
	}

	else {
		return false;
	}

	return true;
}

void Drum::recordNoteOnEarly(int32_t velocity, bool noteTailsAllowed) {
	earlyNoteVelocity = velocity;
	earlyNoteStillActive = noteTailsAllowed;
}

void Drum::drumWontBeRenderedForAWhile() { // This is virtual, and gets extended in SoundDrum
	unassignAllVoices();
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void Drum::getCombinedExpressionInputs(int16_t* combined) {
	for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
		int32_t combinedValue =
		    (int32_t)lastExpressionInputsReceived[0][i] + (int32_t)lastExpressionInputsReceived[1][i];
		combined[i] = lshiftAndSaturate<8>(combinedValue);
	}
}

void Drum::expressionEventPossiblyToRecord(ModelStackWithTimelineCounter* modelStack, int16_t newValue,
                                           int32_t whichExpressionimension, int32_t level) {

	// Ok we have to first combine the expression inputs that the user might have sent at both MPE/polyphonic/finger
	// level, *and* at channel/instrument level. Yes, we combine these here at the input before the data gets recorded
	// or sounded, because unlike for Instruments, we're a Drum, and all we have is the NoteRow level to store this
	// stuff.
	lastExpressionInputsReceived[level][whichExpressionimension] = newValue >> 8; // Store value
	int32_t combinedValue =
	    (int32_t)newValue + ((int32_t)lastExpressionInputsReceived[!level][whichExpressionimension] << 8);
	combinedValue = lshiftAndSaturate<16>(combinedValue);

	expressionValueChangesMustBeDoneSmoothly = true;

	// If recording, we send the new value to the AutoParam, which will also sound that change right now.
	if (modelStack
	        ->timelineCounterIsSet()) { // && playbackHandler.isEitherClockActive() && playbackHandler.recording) {
		modelStack->getTimelineCounter()->possiblyCloneForArrangementRecording(modelStack);

		ModelStackWithNoteRow* modelStackWithNoteRow =
		    ((InstrumentClip*)modelStack->getTimelineCounter())->getNoteRowForDrum(modelStack, this);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (!noteRow) {
			goto justSend;
		}

		bool success = noteRow->recordPolyphonicExpressionEvent(modelStackWithNoteRow, combinedValue,
		                                                        whichExpressionimension, true);
		if (!success) {
			goto justSend;
		}
	}

	// Or if not recording, just sound the change ourselves here (as opposed to the AutoParam doing it).
	else {
justSend:
		expressionEvent(combinedValue, whichExpressionimension); // Virtual function
	}

	expressionValueChangesMustBeDoneSmoothly = false;
}
