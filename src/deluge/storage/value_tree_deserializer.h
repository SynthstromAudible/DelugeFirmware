/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "storage/deserializer.h"
#include "storage/value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deluge::storage {

/// A `Deserializer` cursor that walks an in-memory `Value` tree instead of a byte stream.
///
/// This is the adapter half of the record-first deserializer design: it implements the same
/// 14-method streaming interface that XMLDeserializer/JsonDeserializer implement over a file, but
/// by walking a `Value` tree that some earlier stage (a format-specific parser) already built in
/// full. That lets existing `readFromFile(Deserializer&)` code run, unmodified, against a tree.
///
/// Design: an explicit stack of `{node, index}` frames tracks "where we are" in the tree. At any
/// time there is at most one *pending* Value -- the entry/element most recently produced by
/// readNextTagOrAttributeName() (or, for the very first call, the root itself) -- which
/// match('{')/match('[') may descend into, and which the various readTagOrAttributeValue*()
/// methods read from.
///
/// Two ways callers walk into a container are both supported:
///   1. `name = readNextTagOrAttributeName(); if (match('{')) { ... }` -- the map-entry idiom.
///   2. `while (match('{') && *(name = readNextTagOrAttributeName())) { ... }` -- the
///      array-of-objects idiom used throughout existing readFromFile() code, where match('{') is
///      called directly against an array frame with no prior readNextTagOrAttributeName() to set
///      pending. match() falls back to peeking the array's next unread element in that case.
class ValueTreeDeserializer final : public Deserializer {
public:
	explicit ValueTreeDeserializer(const Value& root) : root_(&root) { reset(); }

	bool prepareToReadTagOrAttributeValueOneCharAtATime() override;
	char readNextCharOfTagOrAttributeValue() override;
	int32_t getNumCharsRemainingInValueBeforeEndOfCluster() override;

	char const* readNextTagOrAttributeName() override;
	char const* readTagOrAttributeValue() override;
	int32_t readTagOrAttributeValueInt() override;
	int32_t readTagOrAttributeValueHex(int32_t errorValue) override;
	int readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) override;
	Error tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware = false) override;

	char const* readNextCharsOfTagOrAttributeValue(int32_t numChars) override;
	Error readTagOrAttributeValueString(std::string& string) override;
	bool match(char const ch) override;
	void exitTag(char const* exitTagName = nullptr, bool closeObject = false) override;

	void reset() override;

private:
	struct Frame {
		const Value* node;
		std::size_t index = 0;
	};

	const Value* root_;
	std::vector<Frame> stack_;
	// The Value most recently produced by readNextTagOrAttributeName() (or the root, before the
	// first call) -- what match()/readTagOrAttributeValue*() act on. Null once consumed/exhausted.
	const Value* pending_ = nullptr;
	// Cursor into pending_->str() for the one-char/n-chars-at-a-time reading methods.
	std::size_t charCursor_ = 0;
	// Scratch buffer backing the synthesized "name" returned for array elements (which, unlike
	// map entries, have no key of their own) by readNextTagOrAttributeName().
	std::string arrayIndexName_;
};

} // namespace deluge::storage
