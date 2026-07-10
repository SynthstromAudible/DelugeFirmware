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
#include <utility>
#include <vector>

namespace deluge::storage {

enum class SourceFormat : uint8_t { Json, Xml };

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
	explicit ValueTreeDeserializer(const Value& root, SourceFormat format = SourceFormat::Json)
	    : root_(&root), format_(format) {
		reset();
	}

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

	// In-memory: the whole tree is already resident, so there is never anything to yield to. This
	// gates the AudioEngine render loop off during a tree-backed load (via the active-reader gate).
	[[nodiscard]] bool shouldCallRoutines() const override { return false; }

	/// The Value node this cursor is currently positioned at -- the child element/value most recently
	/// produced by readNextTagOrAttributeName() (null before the first read or once exhausted). Lets a
	/// tree-native loader (readClipFromTree) read a node's fields directly while the streaming interface
	/// still drives the rest. Read-only; does not advance the cursor.
	[[nodiscard]] const Value* currentValue() const { return pending_; }

	/// Run `body` against a specific child object node, then restore the cursor exactly. `body` drives the
	/// ordinary streaming interface (readNextTagOrAttributeName / match / exitTag) over `childObject`'s
	/// children -- exactly as a mid-stream readFromFile would -- letting a tree-native loader delegate a
	/// nested object to a shared streaming sub-reader (e.g. Sound::readParamsFromFile, readDeviceReferenceFromFile)
	/// without rewriting it.
	/// Hermetic: the entire cursor state is saved and restored around `body`, so the sub-read can never
	/// desync the parent stream regardless of format/depth subtleties.
	/// Position the cursor at a scalar Value so the char-at-a-time value readers
	/// (prepareToReadTagOrAttributeValueOneCharAtATime / readNextCharsOfTagOrAttributeValue / …) read from it,
	/// run `body`, then restore the cursor exactly. Lets a tree-native reader hand a scalar (e.g. a hex noteData
	/// blob) to existing char-decoding code without rewriting it. Hermetic (full save/restore).
	template <typename F>
	void readScalarValue(const Value& scalarNode, F&& body) {
		auto savedStack = stack_;
		const Value* savedPending = pending_;
		std::size_t savedCharCursor = charCursor_;
		int32_t savedCallerDepth = callerDepth_;

		pending_ = &scalarNode;
		charCursor_ = 0;
		body();

		stack_ = std::move(savedStack);
		pending_ = savedPending;
		charCursor_ = savedCharCursor;
		callerDepth_ = savedCallerDepth;
	}

	template <typename F>
	void readChildObject(const Value& childObject, F&& body) {
		auto savedStack = stack_;
		const Value* savedPending = pending_;
		std::size_t savedCharCursor = charCursor_;
		int32_t savedCallerDepth = callerDepth_;

		enterChildObject(childObject);
		body();

		stack_ = std::move(savedStack);
		pending_ = savedPending;
		charCursor_ = savedCharCursor;
		callerDepth_ = savedCallerDepth;
	}

private:
	struct Frame {
		const Value* node;
		std::size_t index = 0;
	};

	// The pending Value's scalar string, or nullptr when there is no pending scalar (pending is
	// null, or is itself a Map/Array). The value-read methods use this so that reading the "value"
	// of a container yields an empty/zero result instead of throwing bad_variant_access -- matching
	// the file readers, which return garbage rather than crash.
	[[nodiscard]] const std::string* pendingScalar() const;

	// Establish a fresh, self-consistent sub-cursor rooted at childObject (the state readChildObject runs
	// `body` in). childObject becomes the sole frame, so the sub-reader's readNextTagOrAttributeName() loop
	// reads its children directly and terminates when exhausted -- in both JSON and XML mode.
	void enterChildObject(const Value& childObject);

	// --- XML mode (SourceFormat::Xml) ---
	// These mirror XMLDeserializer's contract over the all-ordered-maps tree parseXml() produces.
	// XML has no object/array markers: structure is discovered purely by reading child names until
	// a container is exhausted, so match(*) is permissive and elements are entered implicitly by
	// continuing to read. See value_tree_deserializer.cpp for the depth-accounting derivation.
	char const* readNextTagOrAttributeNameXml();
	void exitTagXml();
	// Mirror of XMLDeserializer::tagDepthFile: the number of currently-open XML elements. Equals the
	// descended-frame count (excluding the document-root frame) plus one when the last-named entry is
	// an as-yet-unentered child Map (its element is "open" the moment its name is read, per the file
	// reader, even before we descend into it).
	[[nodiscard]] int32_t xmlFileDepth() const;

	const Value* root_;
	std::vector<Frame> stack_;
	// The Value most recently produced by readNextTagOrAttributeName() (or the root, before the
	// first call) -- what match()/readTagOrAttributeValue*() act on. Null once consumed/exhausted.
	const Value* pending_ = nullptr;
	// Cursor into the pending scalar for the one-char/n-chars-at-a-time reading methods.
	std::size_t charCursor_ = 0;
	SourceFormat format_;
	// XML mode only: mirror of XMLDeserializer::tagDepthCaller -- how many names the caller has
	// consumed since the last exitTag() reconciliation. Unused in JSON mode.
	int32_t callerDepth_ = 0;
};

} // namespace deluge::storage
