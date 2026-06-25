/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

/// A forward-reading byte stream over an audio file — the input the WAV/AIFF header parser consumes. It
/// hides *how* bytes are fetched (SD clusters, a FatFS deserializer buffer, or an in-memory test buffer)
/// behind a four-call surface, and owns its own position state + cleanup. Replaces the leaky
/// `AudioFileReader` whose cluster cursor was poked from outside.
class AudioByteSource {
public:
	virtual ~AudioByteSource() = default;

	/// Read exactly `dest.size()` bytes into `dest`, advancing the cursor. Returns Error::FILE_CORRUPTED if
	/// the read would pass the end of the file (and reads nothing).
	[[nodiscard]] virtual Error read(std::span<std::byte> dest) = 0;

	/// Current absolute byte offset from the start of the file.
	[[nodiscard]] virtual uint32_t pos() = 0;

	/// Advance the cursor forward to absolute byte offset `absolutePos` (forward-only; `absolutePos >= pos()`).
	/// No bytes are read — a subsequent `read` fetches from the new position.
	virtual void seekForwardTo(uint32_t absolutePos) = 0;

	/// Total file size in bytes.
	[[nodiscard]] virtual uint32_t size() const = 0;
};
