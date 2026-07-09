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

#include "util/firmware_version.h"

#include <charconv>
#include <cstring>

// Defined in storage_manager.cpp; forward-declared here to keep this lean adapter free of the
// FatFS-heavy storage_manager.h. tryReadingFirmwareTagFromFile sets it, exactly as XMLDeserializer does.
extern FirmwareVersion song_firmware_version;

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

const std::string* ValueTreeDeserializer::pendingScalar() const {
	return (pending_ != nullptr && pending_->isScalar()) ? &pending_->str() : nullptr;
}

void ValueTreeDeserializer::reset() {
	stack_.clear();
	charCursor_ = 0;
	callerDepth_ = 0;
	if (format_ == SourceFormat::Xml) {
		// XML mode walks entries out of a frame stack from the very first read (the document root is
		// itself a Map whose sole entry is the root element), so seed the stack with the root frame
		// rather than staging the root as pending the way JSON mode does.
		pending_ = nullptr;
		stack_.push_back(Frame{root_, 0});
	}
	else {
		pending_ = root_;
	}
}

int32_t ValueTreeDeserializer::xmlFileDepth() const {
	int32_t depth = static_cast<int32_t>(stack_.size()) - 1; // the document-root frame is depth 0
	if (depth < 0) {
		depth = 0;
	}
	if (pending_ != nullptr && pending_->isMap()) {
		++depth; // a named-but-not-yet-entered child element is already "open"
	}
	return depth;
}

char const* ValueTreeDeserializer::readNextTagOrAttributeNameXml() {
	// Implicit descent: XML "enters" a child element simply by continuing to read. If the previous
	// read named a Map (a structured child element) and the caller neither read its value nor exited
	// it, this next read descends into it -- mirroring the file reader continuing past the tag name
	// into the element's contents with no match('{').
	if (pending_ != nullptr && pending_->isMap()) {
		stack_.push_back(Frame{pending_, 0});
	}
	pending_ = nullptr;

	if (stack_.empty()) {
		return "";
	}
	Frame& top = stack_.back();
	if (!top.node->isMap() || top.index >= top.node->entries().size()) {
		// Element-content end. Mirror the file reader reading the close tag: the element closes
		// (fileDepth drops) and "" is returned. Popping here (rather than deferring to a match('}'),
		// which is a permissive no-op in XML mode) keeps xmlFileDepth() in lockstep with the real
		// tagDepthFile, whose close-tag decrement happens exactly at this "" return.
		if (stack_.size() > 1) {
			stack_.pop_back();
		}
		return "";
	}
	const auto& [key, value] = top.node->entries()[top.index];
	++top.index;
	pending_ = &value;
	++callerDepth_; // mirror tagDepthCaller++ on every non-empty name
	return key.c_str();
}

void ValueTreeDeserializer::exitTagXml() {
	// Mirror XMLDeserializer::exitTag: back the file depth out to below the caller depth, then
	// reconcile the caller depth to it. exitTagName/closeObject are ignored, exactly as in the real
	// reader -- the depth arithmetic alone decides how far to pop. Each step here closes the
	// innermost open element: an as-yet-unentered pending child (drop it, skipping its content) or a
	// descended frame (pop it, discarding any siblings the caller didn't read).
	while (xmlFileDepth() >= callerDepth_) {
		if (pending_ != nullptr && pending_->isMap()) {
			pending_ = nullptr;
		}
		else if (stack_.size() > 1) {
			stack_.pop_back();
		}
		else {
			break;
		}
	}
	pending_ = nullptr;
	callerDepth_ = xmlFileDepth();
}

char const* ValueTreeDeserializer::readNextTagOrAttributeName() {
	if (format_ == SourceFormat::Xml) {
		return readNextTagOrAttributeNameXml();
	}
	pending_ = nullptr;
	if (stack_.empty()) {
		// Nothing has been descended into yet (or a close popped the last frame).
		return "";
	}

	Frame& top = stack_.back();
	// Real callers only read names from inside an object box that a prior match('{') descended
	// into, so the current frame is always a Map. (Arrays are walked via match('{') in the loop
	// head -- their elements are never named directly.) A non-Map top therefore just means "no
	// more names here".
	if (!top.node->isMap()) {
		return "";
	}

	const auto& entries = top.node->entries();
	if (top.index >= entries.size()) {
		return ""; // Exhausted: the file reader's end-of-object signal.
	}
	const auto& [key, value] = entries[top.index];
	++top.index;
	pending_ = &value;
	return key.c_str();
}

bool ValueTreeDeserializer::match(char const ch) {
	if (format_ == SourceFormat::Xml) {
		// XMLDeserializer::match returns true unconditionally ("Only used by Json"). XML structure is
		// discovered by reading child names, not by structural markers, so match is a permissive
		// no-op here: it never descends or pops.
		return true;
	}
	switch (ch) {
	case '{':
	case '[': {
		const bool wantMap = (ch == '{');

		// Prefer descending into the value staged by the last readNextTagOrAttributeName().
		if (pending_ != nullptr) {
			if (wantMap ? pending_->isMap() : pending_->isArray()) {
				stack_.push_back(Frame{pending_, 0});
				pending_ = nullptr;
				return true;
			}
			return false; // Scalar (or wrong container kind): no consume.
		}

		// No value was staged: this is the `while (match('{') && *readNextTagOrAttributeName())`
		// idiom that walks an array of object-boxes. Consume the current array frame's next
		// element if it is the requested kind; return false (ending the loop) when exhausted.
		if (!stack_.empty() && stack_.back().node->isArray()) {
			Frame& top = stack_.back();
			const auto& items = top.node->items();
			if (top.index < items.size()) {
				const Value* item = &items[top.index];
				if (wantMap ? item->isMap() : item->isArray()) {
					++top.index;
					stack_.push_back(Frame{item, 0});
					return true;
				}
			}
		}
		return false;
	}

	case '}':
		// Structural close of the current object -- pop back to the parent frame.
		if (!stack_.empty() && stack_.back().node->isMap()) {
			stack_.pop_back();
			pending_ = nullptr;
			return true;
		}
		return false;

	case ']':
		// Structural close of the current array.
		if (!stack_.empty() && stack_.back().node->isArray()) {
			stack_.pop_back();
			pending_ = nullptr;
			return true;
		}
		return false;

	default:
		return false;
	}
}

void ValueTreeDeserializer::exitTag(char const* exitTagName, bool closeObject) {
	if (format_ == SourceFormat::Xml) {
		(void)exitTagName;
		(void)closeObject;
		exitTagXml();
		return;
	}
	(void)exitTagName;
	// closeObject mirrors the file readers, where it closes one object box (like match('}')). In the
	// array-of-boxes pattern `while (match('{') && readNext()) { …; match('}'); exitTag(nullptr, true); }`
	// the caller's own match('}') has usually already popped the box, leaving the enclosing ARRAY frame on
	// top -- so only pop when the top is a Map (an as-yet-unclosed object box). Popping unconditionally
	// would eat the array frame too and end the loop after a single element (the noteRows/patchCables bug).
	if (closeObject && !stack_.empty() && stack_.back().node->isMap()) {
		stack_.pop_back();
	}
	pending_ = nullptr;
}

char const* ValueTreeDeserializer::readTagOrAttributeValue() {
	const std::string* s = pendingScalar();
	return (s != nullptr) ? s->c_str() : "";
}

int32_t ValueTreeDeserializer::readTagOrAttributeValueInt() {
	const std::string* s = pendingScalar();
	if (s == nullptr) {
		return 0;
	}
	int32_t result = 0;
	std::from_chars(s->data(), s->data() + s->size(), result, 10);
	return result;
}

int32_t ValueTreeDeserializer::readTagOrAttributeValueHex(int32_t errorValue) {
	const std::string* s = pendingScalar();
	if (s == nullptr || s->size() < 2 || (*s)[0] != '0' || (*s)[1] != 'x') {
		return errorValue;
	}
	uint32_t result = 0;
	auto [ptr, ec] = std::from_chars(s->data() + 2, s->data() + s->size(), result, 16);
	if (ec != std::errc{}) {
		return errorValue;
	}
	return static_cast<int32_t>(result);
}

int ValueTreeDeserializer::readTagOrAttributeValueHexBytes(uint8_t* bytes, int32_t maxLen) {
	const std::string* s = pendingScalar();
	if (s == nullptr) {
		return 0;
	}
	int count = 0;
	std::size_t pos = 0;
	while (count < maxLen && pos + 2 <= s->size()) {
		int hi = 0;
		int lo = 0;
		if (!nibbleFromHexChar((*s)[pos], hi) || !nibbleFromHexChar((*s)[pos + 1], lo)) {
			break;
		}
		bytes[count++] = static_cast<uint8_t>((hi << 4) | lo);
		pos += 2;
	}
	return count;
}

Error ValueTreeDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {
	// Format-agnostic: identical logic to XMLDeserializer::tryReadingFirmwareTagFromFile. Sets the
	// global song_firmware_version, which gates dozens of version-conditional parse branches.
	if (!strcmp(tagName, "firmwareVersion")) {
		song_firmware_version = FirmwareVersion::parse(readTagOrAttributeValue());
	}
	// If this tag doesn't exist, it's from old firmware so is ok
	else if (!strcmp(tagName, "earliestCompatibleFirmware")) {
		auto earliestFirmware = FirmwareVersion::parse(readTagOrAttributeValue());
		if (earliestFirmware > FirmwareVersion::current() && !ignoreIncorrectFirmware) {
			// (XMLDeserializer closeWriter()s here; the adapter has no writer -- only the Error matters.)
			return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}
	else {
		return Error::RESULT_TAG_UNUSED;
	}
	return Error::NONE;
}

Error ValueTreeDeserializer::readTagOrAttributeValueString(std::string& string) {
	const std::string* s = pendingScalar();
	string = (s != nullptr) ? *s : std::string{};
	return Error::NONE;
}

bool ValueTreeDeserializer::prepareToReadTagOrAttributeValueOneCharAtATime() {
	charCursor_ = 0;
	return pendingScalar() != nullptr;
}

char ValueTreeDeserializer::readNextCharOfTagOrAttributeValue() {
	const std::string* s = pendingScalar();
	if (s == nullptr || charCursor_ >= s->size()) {
		return 0;
	}
	return (*s)[charCursor_++];
}

int32_t ValueTreeDeserializer::getNumCharsRemainingInValueBeforeEndOfCluster() {
	const std::string* s = pendingScalar();
	if (s == nullptr || charCursor_ >= s->size()) {
		return 0;
	}
	return static_cast<int32_t>(s->size() - charCursor_);
}

char const* ValueTreeDeserializer::readNextCharsOfTagOrAttributeValue(int32_t numChars) {
	const std::string* s = pendingScalar();
	if (s == nullptr || numChars < 0 || charCursor_ + static_cast<std::size_t>(numChars) > s->size()) {
		return nullptr;
	}
	const char* result = s->data() + charCursor_;
	charCursor_ += static_cast<std::size_t>(numChars);
	return result;
}

} // namespace deluge::storage
