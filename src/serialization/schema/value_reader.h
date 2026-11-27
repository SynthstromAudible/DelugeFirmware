#pragma once

/// @file
/// @brief A Reader over a Value tree.

#include "serialization/schema/reader.h"
#include "serialization/value.h"

#include <cstddef>
#include <vector>

namespace deluge::serialization::schema {

/// @brief A Reader over a Value tree, as parseJson() builds one. A NUMBER scalar is an INTEGER when its text has no
/// fraction or exponent, else a REAL; LITERAL scalars are `true`, `false` and `null`.
class ValueReader final : public Reader {
public:
	/// @brief A reader positioned on @p root, which must outlive it.
	/// @param root The document.
	explicit ValueReader(const Value& root) : current_{&root} {}

	[[nodiscard]] Token peek() const override;
	void enter_record() override;
	[[nodiscard]] std::optional<std::string> next_key() override;
	void enter_list() override;
	[[nodiscard]] bool next_item() override;
	[[nodiscard]] std::optional<int64_t> integer() override;
	[[nodiscard]] std::optional<double> real() override;
	[[nodiscard]] std::optional<bool> boolean() override;
	[[nodiscard]] std::optional<std::string> text() override;
	[[nodiscard]] Value value() override;

private:
	/// A record or list entered, and the index of its next value.
	struct Frame {
		const Value* node{nullptr};
		std::size_t next{0};
	};
	/// The value the reader is on, consumed.
	[[nodiscard]] const Value* take();

	std::vector<Frame> frames_;
	const Value* current_;
};

} // namespace deluge::serialization::schema
