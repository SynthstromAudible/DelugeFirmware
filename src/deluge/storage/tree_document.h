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

#include "definitions_cxx.hpp"
#include "storage/value.h"
#include "storage/value_tree_deserializer.h"

#include <cstdint>
#include <optional>
#include <span>

class Deserializer;
/// The reader the load in progress is consuming; the audio-yield gate asks it whether to pump the engine.
extern Deserializer* activeReader;

namespace deluge::storage {

/// @brief A Deluge document parsed whole into a Value tree, with a tree reader positioned inside its root tag.
///
/// Every document whose readers take a tag's fields from its Value node (treeReaderOf()) is opened as one of
/// these: songs, instrument and kit-row presets, and MIDI device definitions. Each loader owns its own, so a song
/// held between passes and a preset parsed meanwhile never share a tree.
class TreeDocument {
public:
	TreeDocument() = default;
	TreeDocument(const TreeDocument&) = delete;
	TreeDocument& operator=(const TreeDocument&) = delete;

	/// @brief Parse @p bytes and enter @p firstTagName (or @p altTagName), consuming the firmware-version tags
	///        before it as XMLDeserializer::openXMLFile does.
	///
	/// @p bytes are only read, and may be freed as soon as this returns: the tree owns its own copies. Any
	/// document this one held is released first.
	///
	/// @param isJson `true` to parse as JSON, `false` as XML.
	/// @return Error::NONE; Error::FILE_CORRUPTED if the bytes did not parse or held no root tag; or the
	///         firmware-tag check's error. On failure nothing is held.
	Error open(std::span<const uint8_t> bytes, bool isJson, char const* firstTagName, char const* altTagName = "",
	           bool ignoreIncorrectFirmware = false);

	/// @brief Free the tree. Safe on a document that holds nothing.
	void release();

	[[nodiscard]] bool isOpen() const { return reader_.has_value(); }

	/// @brief The reader, inside the root tag. Only while isOpen().
	[[nodiscard]] ValueTreeDeserializer& reader() { return *reader_; }

private:
	// The reader points into the tree, so neither ever moves: both are built in place.
	std::optional<Value> tree_;
	std::optional<ValueTreeDeserializer> reader_;
};

/// @brief Points `activeReader` at a reader for one parse, and puts the previous one back on every exit.
class ActiveReaderScope {
public:
	explicit ActiveReaderScope(Deserializer& reader) : previous_(activeReader) { activeReader = &reader; }
	~ActiveReaderScope() { activeReader = previous_; }
	ActiveReaderScope(const ActiveReaderScope&) = delete;
	ActiveReaderScope& operator=(const ActiveReaderScope&) = delete;

private:
	Deserializer* previous_;
};

} // namespace deluge::storage
