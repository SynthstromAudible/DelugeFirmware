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
 * but there is no range checking. Only currently required set-operations
 * are implemented, to keep things simple and avoid bloat. More should be
 * added as required.
 */
class NoteSet {
public:
	NoteSet() : bits(0) {}
	void add(int8_t note) { bits = 0xfff & (bits | (1 << note)); }
	void fill() { bits = 0xfff; }
	int count() { return std::popcount(bits); }
	bool has(int8_t note) const { return (bits >> note) & 1; }
	static const int8_t size = 12;

private:
	uint16_t bits;
};
