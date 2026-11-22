#include "music/scale_change.h"
#include "music/note_set.h"

namespace deluge::music {

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

} // namespace deluge::music
