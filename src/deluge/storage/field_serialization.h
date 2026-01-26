/*
 * Copyright © 2024-2025 Synthstrom Audible Limited
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

#include <cstdint>
#include <cstring>

class Serializer;
class Deserializer;

namespace deluge::storage {

// Forward declarations for helper functions (defined in .cpp)
void writeAttributeInt(Serializer& writer, const char* tag, int32_t value);
void writeAttributeHex(Serializer& writer, const char* tag, int32_t value);
int32_t readAndExitTag(Deserializer& reader, const char* tag);
int32_t readHexAndExitTag(Deserializer& reader, const char* tag);

/// Write an integer field if it differs from default
/// @tparam T Integer type (uint8_t, uint16_t, int32_t, etc.)
template <typename T>
inline void writeField(Serializer& writer, const char* tag, T value, T defaultValue = T{}) {
	if (value != defaultValue) {
		writeAttributeInt(writer, tag, static_cast<int32_t>(value));
	}
}

/// Write a float field with integer scaling (e.g., phase * 10 for 0.1 precision)
inline void writeFloatScaled(Serializer& writer, const char* tag, float value, float scale, float defaultValue = 0.0f) {
	if (value != defaultValue) {
		writeAttributeInt(writer, tag, static_cast<int32_t>(value * scale));
	}
}

/// Read an integer field if tag matches, returns true if handled
/// @tparam T Integer type to read into
template <typename T>
inline bool readField(Deserializer& reader, const char* tagName, const char* expectedTag, T& outValue) {
	if (std::strcmp(tagName, expectedTag) == 0) {
		outValue = static_cast<T>(readAndExitTag(reader, expectedTag));
		return true;
	}
	return false;
}

/// Read a float field with integer scaling, returns true if handled
inline bool readFloatScaled(Deserializer& reader, const char* tagName, const char* expectedTag, float& outValue,
                            float scale) {
	if (std::strcmp(tagName, expectedTag) == 0) {
		outValue = static_cast<float>(readAndExitTag(reader, expectedTag)) / scale;
		return true;
	}
	return false;
}

/// Read an integer field (hex format) if tag matches, returns true if handled
/// @tparam T Integer type to read into
template <typename T>
inline bool readFieldHex(Deserializer& reader, const char* tagName, const char* expectedTag, T& outValue) {
	if (std::strcmp(tagName, expectedTag) == 0) {
		outValue = static_cast<T>(readHexAndExitTag(reader, expectedTag));
		return true;
	}
	return false;
}

} // namespace deluge::storage

// ============================================================================
// Serialization Macros - reduce boilerplate to one line per field
// ============================================================================
// Usage in writeToFile():
//   WRITE_FIELD(writer, shapeX, "shaperX");
//   WRITE_FLOAT(writer, phase, "shaperPhase", 10.0f);
//
// Usage in readTag():
//   READ_FIELD(reader, tagName, shapeX, "shaperX");
//   READ_FLOAT(reader, tagName, phase, "shaperPhase", 10.0f);

/// Write integer field if not default (zero)
#define WRITE_FIELD(writer, field, tag) deluge::storage::writeField(writer, tag, field)

/// Write integer field if not specified default
#define WRITE_FIELD_DEFAULT(writer, field, tag, defaultVal)                                                            \
	deluge::storage::writeField(writer, tag, field, decltype(field){defaultVal})

/// Write float field with scaling if not zero
#define WRITE_FLOAT(writer, field, tag, scale) deluge::storage::writeFloatScaled(writer, tag, field, scale)

/// Write q31 zone value scaled for XML (>> 16)
#define WRITE_ZONE(writer, field, tag)                                                                                 \
	do {                                                                                                               \
		if ((field) != 0) {                                                                                            \
			deluge::storage::writeAttributeInt(writer, tag, (field) >> 16);                                            \
		}                                                                                                              \
	} while (0)

/// Read integer field, returns early if matched
#define READ_FIELD(reader, tagName, field, tag)                                                                        \
	if (deluge::storage::readField(reader, tagName, tag, field)) {                                                     \
		return true;                                                                                                   \
	}

/// Read float field with scaling, returns early if matched
#define READ_FLOAT(reader, tagName, field, tag, scale)                                                                 \
	if (deluge::storage::readFloatScaled(reader, tagName, tag, field, scale)) {                                        \
		return true;                                                                                                   \
	}

/// Read q31 zone value from scaled XML (<< 16)
#define READ_ZONE(reader, tagName, field, tag)                                                                         \
	if (std::strcmp(tagName, tag) == 0) {                                                                              \
		field = static_cast<q31_t>(deluge::storage::readAndExitTag(reader, tag)) << 16;                                \
		return true;                                                                                                   \
	}

/// Read integer field (hex format), returns early if matched
#define READ_FIELD_HEX(reader, tagName, field, tag)                                                                    \
	if (deluge::storage::readFieldHex(reader, tagName, tag, field)) {                                                  \
		return true;                                                                                                   \
	}

// ============================================================================
// Else-if chain variants - for use in while-loops (e.g., song.cpp)
// These don't return, just match and continue
// ============================================================================

/// Read integer field in else-if chain (no return)
#define READ_FIELD_ELSE(reader, tagName, field, tag)                                                                   \
	else if (deluge::storage::readField(reader, tagName, tag, field)) {                                                \
	}

/// Read float field with scaling in else-if chain (no return)
#define READ_FLOAT_ELSE(reader, tagName, field, tag, scale)                                                            \
	else if (deluge::storage::readFloatScaled(reader, tagName, tag, field, scale)) {                                   \
	}
