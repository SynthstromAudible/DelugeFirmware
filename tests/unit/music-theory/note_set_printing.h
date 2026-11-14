#pragma once

#include "music-theory/note_set.h"
#include "music-theory/preset_scales.h"

#include <ostream>

namespace deluge::music {

/// @brief A NoteSet as chromatic note names from C, e.g. "NoteSet(C, E, G)", for CppSpec's failure messages; a NoteSet
/// does not name its root, but this is the easiest form to read while debugging.
inline std::ostream& operator<<(std::ostream& output, const NoteSet& set) {
	static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	output << "NoteSet(";
	bool first = true;
	for (int i = 0; i < NoteSet::size; i++) {
		if (set.has(i)) {
			output << (first ? "" : ", ") << kNames[i];
			first = false;
		}
	}
	return output << ")";
}

/// @brief A Scale by its name, e.g. "MAJOR": the enum is a uint8_t, which would otherwise print as a raw byte.
inline std::ostream& operator<<(std::ostream& output, Scale scale) {
	return output << getScaleName(scale);
}

} // namespace deluge::music
