#include "model/scale/musical_key.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "util/const_functions.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

MusicalKey::MusicalKey() {
	modeNotes.add(0);
	rootNote = 0;
}

uint8_t MusicalKey::intervalOf(int32_t noteCode) const {
	return mod(noteCode - rootNote, 12);
}

int8_t MusicalKey::degreeOf(int32_t noteCode) const {
	return modeNotes.degreeOf(intervalOf(noteCode));
}

int32_t MusicalKey::getYVisualFromYNote(int32_t yNote, bool inKeyMode) const {
	if (!inKeyMode) {
		return yNote;
	}
	int32_t yNoteRelativeToRoot = yNote - rootNote;
	int32_t yNoteWithinOctave = (uint16_t)(yNoteRelativeToRoot + 120) % 12;

	int32_t octave = (uint16_t)(yNoteRelativeToRoot + 120 - yNoteWithinOctave) / 12 - 10;

	int32_t yVisualWithinOctave = 0;
	for (int32_t i = 0; i < modeNotes.count() && modeNotes[i] <= yNoteWithinOctave; i++) {
		yVisualWithinOctave = i;
	}
	return yVisualWithinOctave + octave * modeNotes.count() + rootNote;
}

int32_t MusicalKey::getYNoteFromYVisual(int32_t yVisual, bool inKeyMode) const {
	if (!inKeyMode) {
		return yVisual;
	}
	int32_t yVisualRelativeToRoot = yVisual - rootNote;
	int32_t yVisualWithinOctave = yVisualRelativeToRoot % modeNotes.count();
	if (yVisualWithinOctave < 0) {
		yVisualWithinOctave += modeNotes.count();
	}

	int32_t octave = (yVisualRelativeToRoot - yVisualWithinOctave) / modeNotes.count();

	int32_t yNoteWithinOctave = modeNotes[yVisualWithinOctave];
	return yNoteWithinOctave + octave * 12 + rootNote;
}

int32_t MusicalKey::incrementYNoteInKey(int32_t yNote, int32_t increment, bool inOctave) const {
	D_PRINTLN("Incrementing note %i by %i", yNote, increment);
	auto inc = std::clamp<int32_t>(increment, -1, 1);
	auto num_scale_notes = modeNotes.count();
	int32_t y_note_relative_to_root = yNote - rootNote;
	int32_t y_note_within_octave = (uint16_t)(y_note_relative_to_root + 120) % 12;

	int32_t octave = ((uint16_t)(y_note_relative_to_root + 120 - y_note_within_octave) / 12) - 10;
	auto old_octave = octave;
	int32_t y_visual_within_octave = 0;
	for (int32_t i = 0; i < num_scale_notes && modeNotes[i] <= y_note_within_octave; i++) {
		y_visual_within_octave = i;
	}
	auto old_mode_note = y_visual_within_octave;
	if (inOctave) {
		y_visual_within_octave = (y_visual_within_octave + inc) % num_scale_notes;
		if (y_visual_within_octave < 0) {
			y_visual_within_octave = y_visual_within_octave + num_scale_notes;
		}
	}
	else {
		y_visual_within_octave = y_visual_within_octave + inc;
		if (y_visual_within_octave >= num_scale_notes) {
			y_visual_within_octave = y_visual_within_octave - num_scale_notes;
			octave++;
		}
		else if (y_visual_within_octave < 0) {
			y_visual_within_octave = y_visual_within_octave + num_scale_notes;
			octave--;
		}
	}
	auto new_note = modeNotes[y_visual_within_octave] + (octave * 12) + rootNote;
	if (std::abs(new_note - yNote) > 2) {
		D_PRINTLN("new note is too different");
	}
	D_PRINTLN("incremented from %i to %i", yNote, new_note);
	D_PRINTLN("Octave %i, mode_note %i, old_octave %i, old mode note % i", octave, y_visual_within_octave, old_octave,
	          old_mode_note);

	return new_note;
}

bool MusicalKey::mayMoveModeNote(int16_t yVisualWithinOctave, int8_t newOffset) const {
	// If it's the root note and moving down, special criteria
	if (yVisualWithinOctave == 0 && newOffset == -1) {
		return (modeNotes[modeNotes.count() - 1]
		        < 11); // May ony move down if the top note in scale isn't directly below (at semitone 11)
	}

	else {
		return ((newOffset == 1 &&                   // We're moving up and
		         modeNotes[yVisualWithinOctave] < 11 // We're not already at the top of the scale and
		         && (yVisualWithinOctave == modeNotes.count() - 1
		             || // Either we're the top note, so don't need to check for a higher neighbour
		             modeNotes[yVisualWithinOctave + 1] > modeNotes[yVisualWithinOctave] + 1) // Or space to move up
		         )
		        || (newOffset == -1 &&                 // We're moving down and
		            modeNotes[yVisualWithinOctave] > 1 // We're not already at the bottom (apart from root) and
		            && modeNotes[yVisualWithinOctave - 1] < modeNotes[yVisualWithinOctave] - 1 // space to move down
		            ));
	}
}

Scale MusicalKey::getCurrentScale() const {
	return getScale(modeNotes);
}
