#include "model/scale/note_set.h"

NoteSet::NoteSet(std::initializer_list<uint8_t> notes) : bits{0} {
	for (uint8_t note : notes) {
		add(note);
	}
}

void NoteSet::addUntrusted(uint8_t note) {
	// Constrain added note to be strictly increasing, and < 12.
	//
	// Used when reading mode notes from song files, and other
	// untrusted sources.
	if (count() > 0 && note <= highest()) {
		note = highest() + 1;
	}
	if (note > 11) {
		note = 11;
	}
	add(note);
}

int8_t NoteSet::operator[](uint8_t index) const {
	uint16_t wip = bits;
	int8_t note = -1;
	int8_t n = -1;
	while (index > n++) {
		if (!wip) {
			// The desired degree does not exist.
			return -1;
		}
		// For each subsequent scale degree, find the number
		// of semitones, increment the note counter
		// and shift out the bit.
		uint8_t step = std::countr_zero(wip) + 1;
		wip >>= step;
		note += step;
	}
	// Return the note
	return note;
}

int8_t NoteSet::highestNotIn(NoteSet other) const {
	for (int8_t i = highest(); i >= 0; i--) {
		if (has(i) && !other.has(i)) {
			return i;
		}
	}
	return -1;
}

void NoteSet::applyChanges(int8_t changes[12]) {
	NoteSet newSet;
	uint8_t n = 1;
	for (int note = 1; note < 12; note++) {
		if (has(note)) {
			// n'th degree has semitone t, compute
			// the transpose and save to new noteset
			newSet.add(note + changes[n++] - changes[0]);
		}
	}
	newSet.add(0);
	bits = newSet.bits;
}
#ifdef IN_UNIT_TESTS
const TestString StringFrom(const NoteSet& set) {
	// We print out as chromatic notes across C, even though NoteSet does _not_ specify the root.
    // This is just easier to read and think about when debugging.
	std::string out;
	static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	out.append("NoteSet(");
	bool first = true;
	for (int i = 0; i < NoteSet::size; i++) {
		if (set.has(i)) {
			if (!first) {
					out.append( ", ");
			}
			else {
				first = false;
			}
			out.append(names[i]);
		}
	}
	out.append(")");
	return TestString(out);
}

std::ostream& operator<<(std::ostream& output, const NoteSet& set) {
	output << StringFrom(set).asCharString();
	return output;
}
#endif
