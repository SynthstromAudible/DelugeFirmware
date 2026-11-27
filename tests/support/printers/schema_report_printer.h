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

#include "serialization/schema/reader.h"
#include "serialization/schema/report.h"

#include <ostream>
#include <string_view>

/// @file
/// @brief Prints the schema's tokens, load notes, reports and errors in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operators live in the types' own namespace so that lookup finds them from inside CppSpec.

namespace deluge::serialization::schema {

/// @brief A token's name.
/// @param token The token.
/// @return Its enumerator's name.
inline std::string_view name_of(Token token) {
	switch (token) {
	case Token::RECORD:
		return "RECORD";
	case Token::LIST:
		return "LIST";
	case Token::INTEGER:
		return "INTEGER";
	case Token::REAL:
		return "REAL";
	case Token::BOOLEAN:
		return "BOOLEAN";
	case Token::TEXT:
		return "TEXT";
	case Token::NONE:
		return "NONE";
	case Token::END:
		return "END";
	}
	return "?";
}

/// @brief A note kind's name.
/// @param kind The kind.
/// @return Its enumerator's name.
inline std::string_view name_of(LoadNote::Kind kind) {
	switch (kind) {
	case LoadNote::Kind::CLAMPED:
		return "CLAMPED";
	case LoadNote::Kind::UNKNOWN_WORD:
		return "UNKNOWN_WORD";
	case LoadNote::Kind::UNKNOWN_KEY_KEPT:
		return "UNKNOWN_KEY_KEPT";
	case LoadNote::Kind::UNKNOWN_KEY_DROPPED:
		return "UNKNOWN_KEY_DROPPED";
	case LoadNote::Kind::PLUGIN_STATE_KEPT:
		return "PLUGIN_STATE_KEPT";
	case LoadNote::Kind::PLUGIN_VERSION_DIFFERS:
		return "PLUGIN_VERSION_DIFFERS";
	case LoadNote::Kind::SCHEMA_REVISION_DIFFERS:
		return "SCHEMA_REVISION_DIFFERS";
	case LoadNote::Kind::TOO_MANY_ITEMS:
		return "TOO_MANY_ITEMS";
	}
	return "?";
}

/// @brief An error kind's name.
/// @param kind The kind.
/// @return Its enumerator's name.
inline std::string_view name_of(LoadError::Kind kind) {
	switch (kind) {
	case LoadError::Kind::NOT_A_DOCUMENT:
		return "NOT_A_DOCUMENT";
	case LoadError::Kind::WRONG_KIND:
		return "WRONG_KIND";
	case LoadError::Kind::WRONG_TYPE:
		return "WRONG_TYPE";
	case LoadError::Kind::NO_KIND:
		return "NO_KIND";
	case LoadError::Kind::UNKNOWN_KIND:
		return "UNKNOWN_KIND";
	case LoadError::Kind::DUPLICATE_ID:
		return "DUPLICATE_ID";
	case LoadError::Kind::BAD_PLUGIN_STATE:
		return "BAD_PLUGIN_STATE";
	}
	return "?";
}

/// @brief Prints a token by name, e.g. `RECORD`.
/// @param os The stream to print to.
/// @param token The token.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, Token token) {
	return os << name_of(token);
}
/// @brief Prints a note as its kind and path, e.g. `{CLAMPED at level}`.
/// @param os The stream to print to.
/// @param note The note.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const LoadNote& note) {
	return os << '{' << name_of(note.kind) << " at " << note.path << '}';
}
/// @brief Prints a report as its notes, e.g. `[{CLAMPED at level}]`.
/// @param os The stream to print to.
/// @param report The report.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const LoadReport& report) {
	os << '[';
	for (const LoadNote& note : report.notes) {
		os << note;
	}
	return os << ']';
}
/// @brief Prints an error as its kind and path, e.g. `{WRONG_TYPE at knobs/1/number}`.
/// @param os The stream to print to.
/// @param error The error.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const LoadError& error) {
	return os << '{' << name_of(error.kind) << " at " << error.path << '}';
}

} // namespace deluge::serialization::schema
