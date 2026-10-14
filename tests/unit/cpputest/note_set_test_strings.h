#pragma once

// CppUTest's CHECK_EQUAL prints the values it compares through StringFrom(); found by argument-dependent lookup.

#include "CppUTest/SimpleString.h"
#include "music-theory/note_set.h"

#include <ostream>
#include <string>

namespace deluge::music {

/// @brief A NoteSet as chromatic note names from C, e.g. "NoteSet(C, E, G)"; a NoteSet does not name its root, but
/// this is the easiest form to read while debugging.
inline SimpleString StringFrom(const NoteSet& set) {
	static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	std::string out = "NoteSet(";
	bool first = true;
	for (int i = 0; i < NoteSet::size; i++) {
		if (set.has(i)) {
			out.append(first ? "" : ", ").append(kNames[i]);
			first = false;
		}
	}
	out.append(")");
	return SimpleString(out.c_str());
}

inline std::ostream& operator<<(std::ostream& output, const NoteSet& set) {
	return output << StringFrom(set).asCharString();
}

} // namespace deluge::music
