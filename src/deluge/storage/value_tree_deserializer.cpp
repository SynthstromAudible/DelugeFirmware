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

#include "storage/value_tree_deserializer.h"

#include <charconv>

namespace deluge::storage {

namespace {

bool nibbleFromHexChar(char c, int& outNibble) {
	if (c >= '0' && c <= '9') {
		outNibble = c - '0';
		return true;
	}
	if (c >= 'a' && c <= 'f') {
		outNibble = c - 'a' + 10;
		return true;
	}
	if (c >= 'A' && c <= 'F') {
		outNibble = c - 'A' + 10;
		return true;
	}
	return false;
}

} // namespace

void ValueTreeDeserializer::reset() {
	stack_.clear();
	pending_ = root_;
	charCursor_ = 0;
}

char const* ValueTreeDeserializer::readNextTagOrAttributeName() {
	if (stack_.empty()) {
		// Nothing has been descended into yet (or exitTag() popped the root frame).
		pending_ = nullptr;
		return "";
	}

	Frame& top = stack_.back();

	if (top.node->isMap()) {
		const auto& entries = top.node->entries();
		if (top.index >= entries.size()) {
			pending_ = nullptr;
			return "";
		}
		const auto& [key, value] = entries[top.index];
		++top.index;
		pending_ = &value;
		return key.c_str();
	}

	if (top.node->isArray()) {
		const auto& items = top.node->items();
		if (top.index >= items.size()) {
			pending_ = nullptr;
			return "";
		}
		pending_ = &items[top.index];
		// Array elements have no key of their own; synthesize one from the index so callers
		// that treat the return value as a truthy "there's more" signal still see a non-empty
		// string, while "" is reserved for genuine exhaustion.
		arrayIndexName_ = std::to_string(top.index);
		++top.index;
		return arrayIndexName_.c_str();
	}

	// A scalar frame can't happen via normal use (match() only pushes Map/Array), but cope
	// gracefully rather than crash if it ever does.
	pending_ = nullptr;
	return "";
}

bool ValueTreeDeserializer::match(char const ch) {
	if (ch != '{' && ch != '[') {
		return false;
	}
	const bool wantMap = (ch == '{');

	if (pending_ != nullptr) {
		const bool matches = wantMap ? pending_->isMap() : pending_->isArray();
		if (!matches) {
			return false;
		}
		stack_.push_back(Frame{pending_, 0});
		pending_ = nullptr;
		return true;
	}

	// No pending value was explicitly staged by readNextTagOrAttributeName(). This is the
	// `while (match('{') && *(name = readNextTagOrAttributeName())) { ... }` idiom used
	// throughout existing readFromFile() code to walk an array of objects: peek the current
	// array frame's next unread element and consume it if it's the right kind.
	if (!stack_.empty()) {
		Frame& top = stack_.back();
		if (top.node->isArray()) {
			const auto& items = top.node->items();
			if (top.index < items.size()) {
				const Value* item = &items[top.index];
				const bool matches = wantMap ? item->isMap() : item->isArray();
				if (matches) {
					++top.index;
					stack_.push_back(Frame{item, 0});
					return true;
				}
			}
		}
	}
	return false;
}

void ValueTreeDeserializer::exitTag(char const* exitTagName, bool closeObject) {
	(void)exitTagName;
	(void)closeObject;
	if (!stack_.empty()) {
		stack_.pop_back();
	}
	pending_ = nullptr;
}

char const* ValueTreeDeserializer::readTagOrAttributeValue() {
	if (pending_ == nullptr) {
		return "";
	}
	return pending_->str().c_str();
}

int32_t ValueTreeDeserializer::readTagOrAttributeValueInt() {
	if (pending_ == nullptr) {
		return 0;
	}
	const std::string& s = pending_->str();
	int32_t result = 0;
	std::from_chars(s.data(), s.data() + s.size(), result, 10);
	return result;
}

int32_t ValueTreeDeserializer::readTagOrAttributeValueHex(int32_t errorValue) {
	if (pending_ == nullptr) {
		return errorValue;
	}
	const std::string& s = pending_->str();
	if (s.size() < 2 || s[0] != '0' || s[1] != 'x') {
		return errorValue;
	}
	uint32_t result = 0;
	auto [ptr, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), result, 16);
	if (ec != std::errc{}) {
		return errorValue;
	}
	return static_cast<int32_t>(result);
}

int ValueTreeDeserializer::readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) {
	if (pending_ == nullptr) {
		return 0;
	}
	const std::string& s = pending_->str();
	int count = 0;
	std::size_t pos = 0;
	while (count < maxLen && pos + 2 <= s.size()) {
		int hi = 0;
		int lo = 0;
		if (!nibbleFromHexChar(s[pos], hi) || !nibbleFromHexChar(s[pos + 1], lo)) {
			break;
		}
		bytes[count++] = static_cast<uint8_t>((hi << 4) | lo);
		pos += 2;
	}
	return count;
}

Error ValueTreeDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {
	(void)tagName;
	(void)ignoreIncorrectFirmware;
	// Firmware-version handling is a Step-2 (later) concern for the record-first deserializer;
	// the tree walker itself has nothing to check here.
	return Error::NONE;
}

Error ValueTreeDeserializer::readTagOrAttributeValueString(std::string& string) {
	string = (pending_ != nullptr) ? pending_->str() : std::string{};
	return Error::NONE;
}

bool ValueTreeDeserializer::prepareToReadTagOrAttributeValueOneCharAtATime() {
	charCursor_ = 0;
	return pending_ != nullptr;
}

char ValueTreeDeserializer::readNextCharOfTagOrAttributeValue() {
	if (pending_ == nullptr) {
		return 0;
	}
	const std::string& s = pending_->str();
	if (charCursor_ >= s.size()) {
		return 0;
	}
	return s[charCursor_++];
}

int32_t ValueTreeDeserializer::getNumCharsRemainingInValueBeforeEndOfCluster() {
	if (pending_ == nullptr) {
		return 0;
	}
	const std::string& s = pending_->str();
	if (charCursor_ >= s.size()) {
		return 0;
	}
	return static_cast<int32_t>(s.size() - charCursor_);
}

char const* ValueTreeDeserializer::readNextCharsOfTagOrAttributeValue(int32_t numChars) {
	if (pending_ == nullptr || numChars < 0) {
		return nullptr;
	}
	const std::string& s = pending_->str();
	if (charCursor_ + static_cast<std::size_t>(numChars) > s.size()) {
		return nullptr;
	}
	const char* result = s.data() + charCursor_;
	charCursor_ += static_cast<std::size_t>(numChars);
	return result;
}

} // namespace deluge::storage
