#pragma once

/// @file
/// @brief What a decode did that the document did not say, and why a document could not be read.

#include <cstdint>
#include <string>
#include <vector>

namespace deluge::serialization::schema {

/// @brief One thing a decode did that the document did not say, at a path such as `outputs/3/arp/numOctaves`.
struct LoadNote {
	enum class Kind : uint8_t {
		CLAMPED,                 ///< a value outside its field's range, stored at the nearest end of it
		UNKNOWN_WORD,            ///< a word outside its field's list; the field keeps its value
		UNKNOWN_KEY_KEPT,        ///< a key the schema does not know, kept and written back
		UNKNOWN_KEY_DROPPED,     ///< a key a closed record does not know, dropped
		PLUGIN_STATE_KEPT,       ///< a plugin node no registered plugin reads, kept and written back
		PLUGIN_VERSION_DIFFERS,  ///< a plugin node of another version than its plugin writes, read as it is
		SCHEMA_REVISION_DIFFERS, ///< a document of another schema revision, read best-effort
		TOO_MANY_ITEMS,          ///< an item past the end of a fixed-size list, dropped
	};
	Kind kind;
	std::string path;
	bool operator==(const LoadNote&) const = default;
};

/// @brief Everything a decode did that its document did not say.
struct LoadReport {
	std::vector<LoadNote> notes;
	/// @brief Whether the document said everything the decode did.
	/// @return True when there are no notes.
	[[nodiscard]] bool empty() const { return notes.empty(); }
	bool operator==(const LoadReport&) const = default;
};

/// @brief Why a document could not be read, and where.
struct LoadError {
	enum class Kind : uint8_t {
		NOT_A_DOCUMENT,   ///< the root is not a document with this format's header
		WRONG_KIND,       ///< the header names another kind of document
		WRONG_TYPE,       ///< a value of the wrong type for its field
		NO_KIND,          ///< a record of one of several kinds whose first key is not `kind`
		UNKNOWN_KIND,     ///< a `kind` no alternative has
		DUPLICATE_ID,     ///< two items of an id-keyed list with one id
		BAD_PLUGIN_STATE, ///< a plugin node that is not one
	};
	Kind kind;
	std::string path;
	bool operator==(const LoadError&) const = default;
};

/// @brief A decoded record and what its decode did that the document did not say.
/// @tparam T The record's type.
template <typename T>
struct Loaded {
	T value;
	LoadReport report;
};

} // namespace deluge::serialization::schema
