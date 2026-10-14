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

#include "serialization/value.h"
#include "storage/storage_manager.h"

#include <optional>

/// @brief A Serializer whose output is a Value: what the JSON writer would write, as the tree the JSON reader would
/// read from it. Every writer that writes through a Serializer can be captured as data with it.
///
/// It is a memory-backed JSON writer whose text is parsed by finish(), so it agrees with the JSON writer by
/// construction, raw fragments (writeTagNameAndSeperator followed by write) included.
class ValueSerializer final : public JsonSerializer {
public:
	/// @brief What the writes are: the members of an object (`"name": …`), or the items of a list, as an output or a
	/// drum is written into its song's list (a boxed element, `{"sound": {…}}`).
	enum class Writes : uint8_t {
		MEMBERS,
		ITEMS,
	};

	/// @brief Begin a capture.
	/// @param writes Whether the writes are an object's members or a list's items.
	explicit ValueSerializer(Writes writes = Writes::MEMBERS);
	/// @brief Parse everything written since construction or the last finish(), and start the next capture.
	/// @return A Map of the members, or an Array of the items; empty if the text does not parse (a writer's bug).
	[[nodiscard]] std::optional<deluge::serialization::Value> finish();

private:
	Writes writes_;
	[[nodiscard]] const char* opening() const { return writes_ == Writes::ITEMS ? "[" : "{"; }
	[[nodiscard]] const char* closing() const { return writes_ == Writes::ITEMS ? "]" : "}"; }
};
