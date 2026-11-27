#pragma once

/// @file
/// @brief Where a decode reads a document from.

#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <string>

namespace deluge::serialization::schema {

/// @brief What the next value of a document is.
enum class Token : uint8_t {
	RECORD,  ///< a record: enter_record(), then next_key() for each value
	LIST,    ///< a list: enter_list(), then next_item() for each value
	INTEGER, ///< a whole number
	REAL,    ///< a number with a fraction or an exponent
	BOOLEAN, ///< `true` or `false`
	TEXT,    ///< text
	NONE,    ///< `null`
	END,     ///< nothing: the reader is between a record's or a list's values, or past the document
};

/// @brief Where a decode reads a document from, one value at a time.
///
/// Reading a scalar, or value(), consumes the value whatever its type. A scalar of the wrong type reads as nothing, so
/// the caller can report it and go on.
class Reader {
public:
	Reader() = default;
	Reader(const Reader&) = delete;
	Reader& operator=(const Reader&) = delete;
	Reader(Reader&&) = delete;
	Reader& operator=(Reader&&) = delete;
	virtual ~Reader() = default;

	/// @brief What the next value is.
	/// @return Its token; END when there is none.
	[[nodiscard]] virtual Token peek() const = 0;
	/// @brief Enter the record peek() names; its values follow from next_key().
	virtual void enter_record() = 0;
	/// @brief Step to the next value of the record entered last.
	/// @return Its key, the reader positioned on its value; nullopt, the record left, when it has no more.
	[[nodiscard]] virtual std::optional<std::string> next_key() = 0;
	/// @brief Enter the list peek() names; its values follow from next_item().
	virtual void enter_list() = 0;
	/// @brief Step to the next value of the list entered last.
	/// @return Whether there is one, the reader positioned on it; false, the list left, when there is none.
	[[nodiscard]] virtual bool next_item() = 0;
	/// @brief Consume the next value as a whole number.
	/// @return The number, or nullopt if the value is not one.
	[[nodiscard]] virtual std::optional<int64_t> integer() = 0;
	/// @brief Consume the next value as a real number (a whole number reads as one too).
	/// @return The number, or nullopt if the value is not a number.
	[[nodiscard]] virtual std::optional<double> real() = 0;
	/// @brief Consume the next value as a truth value.
	/// @return The value, or nullopt if it is not `true` or `false`.
	[[nodiscard]] virtual std::optional<bool> boolean() = 0;
	/// @brief Consume the next value as text.
	/// @return The text, or nullopt if the value is not text.
	[[nodiscard]] virtual std::optional<std::string> text() = 0;
	/// @brief Consume the next value whole, whatever it is.
	/// @return The value as a tree; `null` when there is none.
	[[nodiscard]] virtual Value value() = 0;
};

} // namespace deluge::serialization::schema
