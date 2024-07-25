#pragma once

#include <bit>
#include <cstdint>
#include <initializer_list>

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
	/** Default constructor for an empty NoteSet. */
	NoteSet() : bits(0) {}
	/** Constructs a NoteSet from notes.
	 */
	NoteSet(std::initializer_list<uint8_t> notes);
	/** Add a note to NoteSet.
	 */
	void add(int8_t note) { bits = 0xfff & (bits | (1 << note)); }
	/** Remove a note to NoteSet.
	 */
	void remove(int8_t note) { bits = 0xfff & (bits & ~(1 << note)); }
	/** Returns true if note is part of the NoteSet.
	 */
	bool has(int8_t note) const { return (bits >> note) & 1; }
	/** Like add(), but ensures note is in range and higher than previous notes.
	 */
	void addUntrusted(uint8_t note);
	/** Return the index'th note, or -1 if there aren't that many notes present.
	 *
	 * If the NoteSet has a 0 and represents a scale, then is a scale degree as
	 * semitone offset from root.
	 *
	 * Ie. if a NoteSet has add(0), add(1), add(4), and optionally higher notes
	 * added, notesSet[2] will return 4.
	 */
	int8_t operator[](uint8_t index) const;
	/** Returns number of notes lower than the note given, or -1 if the note is not present.
	 *
	 * This is the scale degree of the note if the NoteSet represents a scale and has a root.
	 */
	int8_t degreeOf(uint8_t note) const;
	/** Applies changes specified by the array.
	 *
	 * Each element of the array describes a semitone offset
	 * to a scale degree.
	 *
	 * Root offset is applied relative to the other notes.
	 */
	void applyChanges(int8_t changes[12]);
	/** Marks all semitones as being part of the NoteSet.
	 */
	void fill() { bits = 0xfff; }
	/** Removes all semitones from the NoteSet.
	 */
	void clear() { bits = 0; }
	/** Returns true if the NoteSet is empty */
	bool isEmpty() const { return bits == 0; }
	/** Returns the highest note that has been added to the NoteSet.
	 */
	uint8_t highest() const { return 15 - std::countl_zero(bits); }
	/** Returns the highest note present in this NoteSet not present in the other.
	 *
	 * Returns -1 if there are no notes present unused in the other NoteSet..
	 */
	int8_t highestNotIn(NoteSet used) const;
	/** If this is a preset scale, returns the preset scale id.
	 *
	 * Otherwise returns CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES
	 */
	uint8_t presetScaleId() const;
	/** Returns number of notes in the NoteSet.
	 */
	int count() const { return std::popcount(bits); }
	/** Returns the size of the scale needed for this NoteSet.
	 */
	int scaleSize() const;
	/** True if two NoteSets are identical. */
	bool operator==(const NoteSet& other) const { return bits == other.bits; }
	/** Determines the majorness of the NoteSet. Positive is major, negative is minor. */
	int8_t majorness() const;
	/** True if this is a subset of the other NoteSet. */
	bool isSubsetOf(NoteSet other) const { return (other.bits & bits) == bits; }
	/** Union of two NoteSets
	 */
	NoteSet operator|(const NoteSet& other);
	/** Adds a mode-note to NoteSet, optionally specifying that we prefer it a semitone higher, although this may be
	 * overridden by what actual note is present
	 */
	void addMajorDependentModeNotes(uint8_t i, bool preferHigher, const NoteSet notesWithinOctavePresent);
	/** Size of NoteSet, ie. the maximum number of notes it can hold.
	 */
	NoteSet toImpliedScale() const;
	static const int8_t size = 12;

private:
	uint16_t bits;
};

const uint8_t kMaxScaleSize = NoteSet::size;

#ifdef IN_UNIT_TESTS
// For CppUTest CHECK_EQUAL() and debugging convenience

#include <iostream>
#include <string>

std::ostream& operator<<(std::ostream& output, const NoteSet& set);

class TestString {
public:
	TestString(std::string string_) : string(string_) {}
	const char* asCharString() const { return string.c_str(); }

private:
	std::string string;
};

const TestString StringFrom(const NoteSet&);
#endif
