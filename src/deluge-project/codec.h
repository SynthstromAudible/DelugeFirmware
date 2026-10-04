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
	/// Whether the file stores `swingInterval` relative to the tick magnitude, as firmware 3.1.0-alpha2 and later do.
	bool swing_interval_is_relative{true};
};

/// @brief A sync level as a file stores it, from the internal value (1–9, 0 = off), relative to
/// @p input_tick_magnitude; never below 1 unless off.
[[nodiscard]] constexpr int32_t sync_level_to_file(int32_t internal, int32_t input_tick_magnitude) {
	if (internal == 0) {
		return 0;
	}
	const int32_t file = internal - 1 + input_tick_magnitude;
	return file < 1 ? 1 : file;
}

/// @brief The internal sync level (1–9, 0 = off) from the value a file stores relative to @p input_tick_magnitude.
[[nodiscard]] constexpr int32_t sync_level_from_file(int32_t file, int32_t input_tick_magnitude) {
	if (file == 0) {
		return 0;
	}
	const int32_t internal = file + 1 - input_tick_magnitude;
	return internal < 1 ? 1 : (internal > 9 ? 9 : internal);
}

/// @brief The context for decoding @p song from the document @p root. The firmware version is the song's
/// `firmwareVersion` attribute or, in old files, the root's `firmwareVersion` sibling of the song; a song with
/// neither is from the oldest firmware.
[[nodiscard]] inline DecodeContext decode_context(const serialization::Value& root, const serialization::Value& song) {
	DecodeContext context;
	const serialization::Value* version = song.isMap() ? song.find("firmwareVersion") : nullptr;
	if (version == nullptr && root.isMap()) {
		version = root.find("firmwareVersion");
	}
	const std::string* text = serialization::text_of(version);
	const FirmwareVersion fileVersion =
	    (text != nullptr && !text->empty()) ? FirmwareVersion::parse(*text) : FirmwareVersion::official({0, 0, 0});
	context.swing_interval_is_relative = fileVersion >= FirmwareVersion::official({3, 1, 0, "alpha2"});
	if (song.isMap()) {
		if (auto magnitude = serialization::int_of(song.find("inputTickMagnitude"))) {
			context.input_tick_magnitude = *magnitude;
		}
	}
	return context;
}

} // namespace deluge::project
