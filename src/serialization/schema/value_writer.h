#pragma once

/// @file
/// @brief A Writer that builds a Value tree.

#include "serialization/schema/writer.h"
#include "serialization/value.h"

#include <optional>
#include <string>
#include <vector>

namespace deluge::serialization::schema {

/// @brief A Writer that builds a Value tree: a record is a Map, a list an Array, a real a NUMBER scalar in its
/// shortest exact form, `true`/`false`/`null` LITERAL scalars.
class ValueWriter final : public Writer {
public:
	void begin_record() override;
	void key(std::string_view name) override;
	void end_record() override;
	void begin_list() override;
	void end_list() override;
	void integer(int64_t value) override;
	void boolean(bool value) override;
	void text(std::string_view value) override;
	void real(double value) override;
	void null() override;
	void value(const Value& subtree) override;

	/// @brief The tree written, once every record and list is closed.
	/// @return The tree; an empty Map if nothing was written.
	[[nodiscard]] Value take() &&;

private:
	/// A record or list still open, and the key it goes under in its parent.
	struct Frame {
		Value node;
		std::string key;
	};
	void open(Value node);
	void close();
	void put(Value node);

	std::vector<Frame> frames_;
	std::string pending_key_;
	std::optional<Value> root_;
};

} // namespace deluge::serialization::schema
