#include "model/scale/scale_change.h"
#include "model/scale/note_set.h"

NoteSet ScaleChange::applyTo(NoteSet notes) const {
	NoteSet newSet;
	for (int i = 0; i < source.count(); i++) {
		uint8_t note = source[i];
		if (notes.has(note)) {
			newSet.add(note + degreeOffset[i]);
		}
	}
	return newSet;
}
