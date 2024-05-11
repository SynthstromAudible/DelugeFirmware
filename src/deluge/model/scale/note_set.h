#pragma once

#include <bit>
#include <cstdint>

/** Set of chromatic notes.
 *
 * NoteSet represents a set of chromatic notes within an octave.
 *
 * 0 is root, 1 is minor second, 2 is major second, etc.
 *
 * NoteSet::size is always 12, provided for convenience of iteration.
 * NoteSet::count() is number of notes in the set.
 *
 * The class is just a thin wrapper around a integer bitfield.
 * First 12 bits represent semitones. Last 4 bits curretly are unused,
 * but there is no range checking.
 */
class NoteSet {
public:
	NoteSet() : bits(0) {}
	/** Add a note to NoteSet.
	 */
	void add(int8_t note) { bits = 0xfff & (bits | (1 << note)); }
	/** Returns true if note is part of the NoteSet.
	 */
	bool has(int8_t note) const { return (bits >> note) & 1; }
	/** Like add(), but ensures note is in range and higher than previous notes.
	 */
	void addUntrusted(uint8_t note) {
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
	/** Return the note at specified scale degree as semitone offset from root.
	 *
	 * Ie. if a NoteSet has add(0), add(1), add(4), and optionally higher notes
	 * added, notesSet[2] will return 4.
	 */
	uint8_t operator[](uint8_t index) {
		// Shift out the root note bit, ie. scale degree zero. We don't
		// actually check it it's set, because counting scale degrees
		// doesn't make sense if the root is not set.
		uint16_t wip = bits >> 1;
		uint8_t note = 0;
		while (index--) {
			if (!wip) {
				// The desired degree does not exist.
				return 0;
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
	/** Clears existing notes and adds notes from the scaleNotes[] array.
	 */
	void fromScaleNotes(const uint8_t scaleNotes[7]) {
		clear();
		for (int32_t n = 0; n < 7; n++) {
			int32_t newNote = scaleNotes[n];
			// Non-diatonic scales have trailing zero notes
			// but adding zero to the set again doesn't hurt.
			add(newNote);
		}
	}
	/** Applies changes specified by the array.
	 *
	 * Each element of the array describes a semitone offset
	 * to a scale degree.
	 *
	 * Root offset is applied relative to the other notes.
	 */
	void applyChanges(int8_t changes[12]) {
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
	/** Marks all semitones as being part of the NoteSet.
	 */
	void fill() { bits = 0xfff; }
	/** Removes all semitones from the NoteSet.
	 */
	void clear() { bits = 0; }
	/** Returns the highest note that has been added to the NoteSet.
	 */
	uint8_t highest() { return 15 - std::countl_zero(bits); }
	/** Returns number of notes in the NoteSet.
	 */
	int count() { return std::popcount(bits); }
	/** Size of NoteSet, ie. the maximum number of notes it can hold.
	 */
	static const int8_t size = 12;

private:
	uint16_t bits;
};
