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

#include "engine-api/types/sync.h"
#include "serialization/firmware_version.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <cstdint>

namespace deluge::project {

/// @brief Why a document could not be read into the model.
enum class LoadError : uint8_t {
	NOT_A_MAP,    ///< a record's node is not a Map
	MISSING_SONG, ///< the document holds no song
};

/// @brief What decoding a song needs to know about its document beyond each record's own keys.
struct DecodeContext {
	/// The file's input tick magnitude (`inputTickMagnitude`): sync values are stored relative to it.
	int32_t input_tick_magnitude{1};
};

using deluge::engine::sync_level_from_file;
using deluge::engine::sync_level_to_file;

/// @brief The context for decoding @p song: its input tick magnitude.
[[nodiscard]] inline DecodeContext decode_context(const serialization::Value& /*root*/,
                                                  const serialization::Value& song) {
	DecodeContext context;
	if (song.isMap()) {
		if (auto magnitude = serialization::int_of(song.find("inputTickMagnitude"))) {
			context.input_tick_magnitude = *magnitude;
		}
	}
	return context;
}

} // namespace deluge::project
