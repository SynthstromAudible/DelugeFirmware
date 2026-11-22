#pragma once

#include <cstdint>

namespace deluge::music {

/// @brief Determine whether two semitone values refer to the same chromatic note (differ by a multiple of 12).
/// @param a A semitone value.
/// @param b A semitone value.
/// @return True if @p a and @p b are the same pitch class.
bool isSameNote(int16_t a, int16_t b);

} // namespace deluge::music
