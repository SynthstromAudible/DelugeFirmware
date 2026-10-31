/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <span>

/// @file
/// @brief The audible window's parameter dump: each matrix pass's final values and each voice's source states, written
/// on the host to compare two builds' renders. Compiled in only under DELUGE_PARAM_DUMP (the host-app tree); elsewhere
/// every call is an empty inline.

namespace deluge::param_dump {

/// @brief One unison part's source, as a voice's block leaves it.
struct SourceState {
	uint8_t part;   ///< the unison part
	uint8_t source; ///< the source slot
	bool active;    ///< it sounds in this part (false when off, or above the 22.05 kHz cut-off)
	bool cache;     ///< its sample may not be cached: a source that changes during the note modulates its pitch
	uint32_t phase_increment; ///< its pitch before modulation, UQ0.32
};

#if defined(DELUGE_PARAM_DUMP)
/// @brief A note started on @p voice of @p sound: the voice's records carry the sound's note-on count from now on.
void note_on(const void* sound, const void* voice);
/// @brief A matrix pass of @p scope (0 a voice, 1 the sound) left @p finals; @p voice is null for the sound.
void plane(uint8_t scope, const void* sound, const void* voice, std::span<const int32_t> finals);
/// @brief A voice's block left its sources as @p states.
void voice_sources(const void* sound, const void* voice, std::span<const SourceState> states);
#else
// Always inlined: app code builds with -fno-inline-functions, and an empty call must not survive into device images
[[gnu::always_inline]] inline void note_on(const void* /*sound*/, const void* /*voice*/) {
}
[[gnu::always_inline]] inline void plane(uint8_t /*scope*/, const void* /*sound*/, const void* /*voice*/,
                                         std::span<const int32_t> /*finals*/) {
}
[[gnu::always_inline]] inline void voice_sources(const void* /*sound*/, const void* /*voice*/,
                                                 std::span<const SourceState> /*states*/) {
}
#endif

} // namespace deluge::param_dump
