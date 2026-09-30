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

#include "libdeluge/app.h"
#include "processing/engines/audio_engine.h"
#include <cstdint>

/// @brief The clock the Rust resource manager measures loader service latency with (`libdeluge/app.h`).
///
/// Output frames (the `AudioEngine::audioSampleTimer` unit), which is
/// the natural unit here: a 32 KB cluster is ~8192 frames of 44.1 kHz stereo 16-bit playback, so a
/// latency in frames is directly comparable to the budget it has to fit inside.
///
/// A plain 32-bit read, so it is atomic on this target even though the audio ISR writes the timer —
/// see the deadline-scheduling design for why the timer is deliberately NOT widened to 64 bits.
extern "C" uint32_t deluge_debug_now_frames() noexcept {
	return AudioEngine::audioSampleTimer;
}
