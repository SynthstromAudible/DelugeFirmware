#pragma once

#include <cstdint>

namespace deluge::music {

/** Determines if two semitone values refer to the same chromatic note.
 */
bool isSameNote(int16_t a, int16_t b);

} // namespace deluge::music
