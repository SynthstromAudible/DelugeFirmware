#pragma once

/// @file
/// @brief Where an encode puts a document.

#include "serialization/value.h"

#include <cstdint>
#include <string_view>

namespace deluge::serialization::schema {

/// @brief Where an encode puts a document: records of keyed values, lists of values, and scalars.
///
/// A record is begin_record(), then key() before each of its values, then end_record(); a list is begin_list(), its
/// values, then end_list(). The backend decides the bytes.
class Writer {
public:
	Writer() = default;
	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;
	Writer(Writer&&) = delete;
	Writer& operator=(Writer&&) = delete;
	virtual ~Writer() = default;

	/// @brief Open a record.
	virtual void begin_record() = 0;
	/// @brief Name the next value of the open record.
	/// @param name The key.
	virtual void key(std::string_view name) = 0;
	/// @brief Close the open record.
	virtual void end_record() = 0;
	/// @brief Open a list.
	virtual void begin_list() = 0;
	/// @brief Close the open list.
	virtual void end_list() = 0;
	/// @brief Write a whole number.
	/// @param value The number.
	virtual void integer(int64_t value) = 0;
	/// @brief Write a truth value.
	/// @param value The value.
	virtual void boolean(bool value) = 0;
	/// @brief Write text.
	/// @param value The text.
	virtual void text(std::string_view value) = 0;
	/// @brief Write a finite real number so that reading it back gives the same double.
	/// @param value The number.
	virtual void real(double value) = 0;
	/// @brief Write the absence of a value.
	virtual void null() = 0;
	/// @brief Write @p subtree as it is: a plugin's state, or an entry kept from the file.
	/// @param subtree The tree.
	virtual void value(const Value& subtree) = 0;
};

} // namespace deluge::serialization::schema
